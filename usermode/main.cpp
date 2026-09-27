// language: C++17, file: main.cpp, target: Windows 10/11 x64, MSVC
// INSERT = toggle компенсации.  ЛКМ = начать очередь.  Tray = управление.
// Kernel driver подключается автоматически если загружен.
#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "recoil.h"
#include "profile.h"
#include "logger.h"
#include "overlay.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

// ─── Константы ────────────────────────────────────────────────────────────
static constexpr UINT WM_TRAYICON   = WM_USER + 1;
static constexpr UINT ID_TRAY_EXIT  = 1001;
static constexpr UINT ID_TRAY_TOG   = 1002;
static constexpr UINT ID_WPN_AK     = 2001;
static constexpr UINT ID_WPN_M4     = 2002;
static constexpr UINT ID_WPN_AWP    = 2003;
static const wchar_t* MAIN_CLASS    = L"RCMain";

// Имя класса / тайтл игры (devblog old Rust — legacy Unity build)
// FindWindow ищет по классу. Для old rust: "UnityWndClass" / "Rust"
static const wchar_t* GAME_CLASS    = L"UnityWndClass";
static const wchar_t* GAME_TITLE    = L"Rust";

// ─── Глобальное состояние ─────────────────────────────────────────────────
static std::atomic<bool>  g_active{ false };
static std::atomic<bool>  g_game_focused{ false };
static RecoilCompensator* g_comp    = nullptr;
static Overlay*           g_overlay = nullptr;
static ProfileManager*    g_pm      = nullptr;
static NOTIFYICONDATAW    g_nid{};
static HWND               g_hwnd    = nullptr;

// ─── Вспомогательные ──────────────────────────────────────────────────────
static HWND FindGame() {
    return FindWindowW(GAME_CLASS, GAME_TITLE);
}

static void TraySetTip(bool active) {
    wcscpy_s(g_nid.szTip,
              active ? L"RC: ACTIVE  [INSERT=toggle]"
                     : L"RC: INACTIVE [INSERT=toggle]");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayShowMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_TOG,
                g_active ? L"Деактивировать" : L"Активировать");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_WPN_AK,  L"AK47");
    AppendMenuW(menu, MF_STRING, ID_WPN_M4,  L"M4A1");
    AppendMenuW(menu, MF_STRING, ID_WPN_AWP, L"AWP");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Выход");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

static void SwitchWeapon(const char* name) {
    if (!g_pm || !g_comp) return;
    const WeaponProfile* p = g_pm->Get(name);
    if (p) g_comp->SetProfile(*p);
}

// ─── WndProc ──────────────────────────────────────────────────────────────
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    // ── Создание: регистрируем raw input ──────────────────────────────────
    case WM_CREATE: {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;      // Generic Desktop
        rid.usUsage     = 0x02;      // Mouse
        rid.dwFlags     = RIDEV_INPUTSINK;
        rid.hwndTarget  = hwnd;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        return 0;
    }

    // ── Raw mouse: определяем ЛКМ ─────────────────────────────────────────
    case WM_INPUT: {
        if (!g_active.load() || !g_game_focused.load()) break;

        UINT sz = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),
                        RID_INPUT, nullptr, &sz, sizeof(RAWINPUTHEADER));
        std::vector<BYTE> buf(sz);
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),
                        RID_INPUT, buf.data(), &sz, sizeof(RAWINPUTHEADER));

        const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buf.data());
        if (raw->header.dwType == RIM_TYPEMOUSE) {
            if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
                g_comp->StartFiring();
            if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
                g_comp->StopFiring();
        }
        break;
    }

    // ── Таймер: опрос фокуса игры + обновление оверлея ───────────────────
    case WM_TIMER: {
        HWND fg = GetForegroundWindow();
        HWND gw = FindGame();
        bool focused = (gw != nullptr && fg == gw);
        g_game_focused.store(focused);

        if (!focused && g_comp->IsFiring())
            g_comp->StopFiring();

        if (g_overlay) {
            // Оверлей показываем только когда игра НЕ в фокусе
            g_overlay->Show(!focused && g_active.load());

            int s   = g_comp->CurrentStep();
            int idx = (!g_comp->Profile().offsets.empty())
                      ? s % (int)g_comp->Profile().offsets.size() : 0;

            OverlayState os{};
            os.firing        = g_comp->IsFiring();
            os.active        = g_active.load();
            os.current_step  = s;
            if (!g_comp->Profile().offsets.empty() && idx < (int)g_comp->Profile().offsets.size()) {
                os.step_x = g_comp->Profile().offsets[idx].x;
                os.step_y = g_comp->Profile().offsets[idx].y;
            }
            strncpy_s(os.weapon, g_comp->Profile().weapon.c_str(), 15);
            g_overlay->Update(os);
        }
        break;
    }

    // ── INSERT — переключить компенсацию ─────────────────────────────────
    case WM_HOTKEY: {
        if (wp == 1) {
            bool cur = g_active.load();
            g_active.store(!cur);
            g_comp->SetEnabled(!cur);
            TraySetTip(!cur);
            Logger::Get().Log("toggle", {{"active", (!cur) ? "1" : "0"}});
            if (cur && g_comp->IsFiring()) g_comp->StopFiring();
        }
        break;
    }

    // ── Трей ──────────────────────────────────────────────────────────────
    case WM_TRAYICON:
        if (lp == WM_RBUTTONUP) TrayShowMenu(hwnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_TRAY_EXIT: PostQuitMessage(0);             break;
        case ID_TRAY_TOG: {
            bool cur = g_active.load();
            g_active.store(!cur); g_comp->SetEnabled(!cur);
            TraySetTip(!cur);
            break;
        }
        case ID_WPN_AK:  SwitchWeapon("AK47"); break;
        case ID_WPN_M4:  SwitchWeapon("M4A1"); break;
        case ID_WPN_AWP: SwitchWeapon("AWP");  break;
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─── WinMain ──────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    Logger::Get().Init("logs");
    Logger::Get().Log("session_start");

    // Kernel driver: пробуем подключиться; fallback — user-mode SendInput
    RecoilCompensator comp(/*use_kernel=*/true);
    g_comp = &comp;

    ProfileManager pm;
    pm.AddDefaults();
    pm.LoadDir("profiles");
    g_pm = &pm;

    Overlay overlay;
    g_overlay = &overlay;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = MAIN_CLASS;
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        0, MAIN_CLASS, L"RecoilComp",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
        nullptr, nullptr, hInst, nullptr
    );
    ShowWindow(g_hwnd, SW_HIDE);

    // Трей
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIconW(nullptr, IDI_SHIELD);
    wcscpy_s(g_nid.szTip, L"RC: INACTIVE [INSERT=toggle]");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    overlay.Create(hInst);
    overlay.Show(false);

    // INSERT — глобальный хоткей переключения
    RegisterHotKey(g_hwnd, 1, 0, VK_INSERT);

    // Таймер опроса фокуса (100ms)
    SetTimer(g_hwnd, 1, 100, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Logger::Get().Log("session_end");
    overlay.Destroy();
    return 0;
}
