// language: C++17, file: overlay.cpp, target: Windows 10/11 x64, MSVC
// WS_EX_LAYERED + WS_EX_TRANSPARENT: клики сквозные, поверх всего
#include "overlay.h"
#include <cstdio>
#include <cstring>

static const wchar_t* OV_CLASS = L"RC_OV";

bool Overlay::Create(HINSTANCE hInst) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = OV_CLASS;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        OV_CLASS, L"", WS_POPUP,
        10, 10, W, H,
        nullptr, nullptr, hInst, this
    );
    if (!hwnd_) return false;
    // Чёрный — colorkey прозрачности, 80% непрозрачность для текста
    SetLayeredWindowAttributes(hwnd_, RGB(0,0,0), 204, LWA_ALPHA | LWA_COLORKEY);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    return true;
}

void Overlay::Destroy() {
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
}

void Overlay::Update(const OverlayState& s) {
    state_ = s;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void Overlay::Show(bool v) {
    if (hwnd_) ShowWindow(hwnd_, v ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void Overlay::Paint(HDC hdc) {
    HFONT font = CreateFontA(14, 0, 0, 0, FW_BOLD, 0, 0, 0,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, "Consolas");
    HFONT old = reinterpret_cast<HFONT>(SelectObject(hdc, font));
    SetBkMode(hdc, TRANSPARENT);

    char line[64];

    // Статус RC
    SetTextColor(hdc, state_.active ? RGB(0, 230, 80) : RGB(220, 60, 60));
    snprintf(line, sizeof(line), "[RC] %s  %s",
             state_.active ? "ON " : "OFF",
             state_.weapon);
    TextOutA(hdc, 6, 6, line, (int)strlen(line));

    // Состояние выстрела
    SetTextColor(hdc, state_.firing ? RGB(255, 200, 0) : RGB(150, 150, 150));
    snprintf(line, sizeof(line), "%s  step:%d",
             state_.firing ? "FIRING " : "IDLE   ",
             state_.current_step);
    TextOutA(hdc, 6, 24, line, (int)strlen(line));

    // Текущий offset
    SetTextColor(hdc, RGB(180, 180, 255));
    snprintf(line, sizeof(line), "dX=%+d  dY=%+d",
             state_.step_x, state_.step_y);
    TextOutA(hdc, 6, 42, line, (int)strlen(line));

    SelectObject(hdc, old);
    DeleteObject(font);
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Overlay* self = nullptr;
    if (msg == WM_CREATE) {
        self = reinterpret_cast<Overlay*>(
            reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (msg == WM_PAINT && self) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        self->Paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
