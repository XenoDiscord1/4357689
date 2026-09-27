// language: C++17, file: recoil.cpp, target: Windows 10/11 x64, MSVC
// Логика компенсации. Kernel path: IOCTL → драйвер инжектит до античита.
// User path: SendInput (детектируется EAC/BE через injected-бит — используй kernel).
#include "recoil.h"
#include "driver_shared.h"
#include "logger.h"
#include <stdexcept>

// ─── Встроенные профили ───────────────────────────────────────────────────
WeaponProfile MakeAK47Profile() {
    WeaponProfile p;
    p.weapon          = "AK47";
    p.calibrated_date = "2024-01-20";
    p.spray_pattern   = "standard";
    p.offsets = {
        { 0, 8,10},{ 2, 7,10},{-2, 6,10},{ 3, 5,10},
        {-1, 4,10},{ 1, 3,10},{ 0, 2,10},{ 1, 1,10},
        { 0, 0,10},{ 1, 7,10},{-1, 6,10},{ 2, 5,10},
        {-2, 4,10},{ 0, 3,10},{ 1, 2,10},{ 0, 1,10},
        { 0, 0,10},{ 1, 6,10},{-1, 5,10},{ 2, 4,10},
        {-2, 3,10},{ 0, 2,10},{ 1, 1,10},{ 0, 0,10},
        { 0, 0,10}
    };
    return p;
}

WeaponProfile MakeM4A1Profile() {
    WeaponProfile p;
    p.weapon          = "M4A1";
    p.calibrated_date = "2024-01-20";
    p.spray_pattern   = "standard";
    p.offsets = {
        { 0, 6,10},{ 1, 5,10},{-1, 5,10},{ 2, 4,10},
        {-1, 3,10},{ 0, 3,10},{ 1, 2,10},{-1, 2,10},
        { 0, 1,10},{ 0, 0,10},{ 1, 5,10},{-1, 4,10},
        { 2, 3,10},{-1, 3,10},{ 0, 2,10},{ 1, 1,10},
        { 0, 0,10},{ 0, 0,10},{ 0, 0,10},{ 0, 0,10}
    };
    return p;
}

WeaponProfile MakeAWPProfile() {
    // AWP: нет очереди, только одиночный вертикальный kicked
    WeaponProfile p;
    p.weapon          = "AWP";
    p.calibrated_date = "2024-01-20";
    p.spray_pattern   = "single";
    p.offsets = {
        { 0, 12,10},{ 0, 8,10},{ 0, 4,10},{ 0, 1,10},{ 0, 0,10}
    };
    return p;
}

// ─── RecoilCompensator ────────────────────────────────────────────────────
RecoilCompensator::RecoilCompensator(bool use_kernel)
    : use_kernel_(use_kernel)
{
    QueryPerformanceFrequency(&qpc_freq_);
    profile_ = MakeAK47Profile();

    if (use_kernel_ && !OpenKernelDriver())
        use_kernel_ = false; // fallback

    h_fire_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    h_stop_ = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
    if (!h_fire_ || !h_stop_) throw std::runtime_error("CreateEvent failed");

    h_thread_ = CreateThread(nullptr, 0, WorkerProc, this, 0, nullptr);
    if (h_thread_ == INVALID_HANDLE_VALUE) throw std::runtime_error("CreateThread failed");
    SetThreadPriority(h_thread_, THREAD_PRIORITY_HIGHEST);
}

RecoilCompensator::~RecoilCompensator() {
    running_.store(false, std::memory_order_release);
    SetEvent(h_stop_);
    SetEvent(h_fire_);
    WaitForSingleObject(h_thread_, 3000);
    CloseHandle(h_thread_);
    CloseHandle(h_fire_);
    CloseHandle(h_stop_);
    if (h_driver_ != INVALID_HANDLE_VALUE) CloseHandle(h_driver_);
}

bool RecoilCompensator::OpenKernelDriver() {
    h_driver_ = CreateFileW(
        RECOIL_USER_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr
    );
    return h_driver_ != INVALID_HANDLE_VALUE;
}

void RecoilCompensator::StartFiring() {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    if (firing_.load(std::memory_order_acquire))   return;
    step_.store(0, std::memory_order_relaxed);
    firing_.store(true, std::memory_order_release);
    SetEvent(h_fire_);
    Logger::Get().Log("fire_start", {{"weapon", profile_.weapon}});
}

void RecoilCompensator::StopFiring() {
    if (!firing_.load(std::memory_order_acquire)) return;
    int s = step_.load(std::memory_order_relaxed);
    firing_.store(false, std::memory_order_release);
    step_.store(0, std::memory_order_relaxed);
    Logger::Get().Log("fire_end", {{"total_steps", std::to_string(s)}});
}

void RecoilCompensator::SetProfile(const WeaponProfile& p) {
    StopFiring();
    profile_ = p;
    Logger::Get().Log("profile_switch", {{"weapon", p.weapon}});
}

void RecoilCompensator::ApplyOffset(const Offset& off, int idx) {
    if (use_kernel_ && h_driver_ != INVALID_HANDLE_VALUE) {
        RC_OFFSET_MSG msg{};
        msg.offset_id    = static_cast<unsigned>(idx);
        msg.x            = static_cast<short>(off.x);
        msg.y            = static_cast<short>(off.y);
        msg.delay_us     = static_cast<unsigned>(off.delay_ms) * 1000u;
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        msg.ts_perfcount = static_cast<unsigned long long>(t.QuadPart);

        DWORD ret = 0;
        DeviceIoControl(h_driver_, IOCTL_RC_PUSH_OFFSET,
                        &msg, sizeof(msg), nullptr, 0, &ret, nullptr);
    } else {
        // User-mode fallback — injected-бит будет выставлен, видим EAC/BE
        INPUT inp{};
        inp.type       = INPUT_MOUSE;
        inp.mi.dwFlags = MOUSEEVENTF_MOVE;
        inp.mi.dx      = static_cast<LONG>(off.x);
        inp.mi.dy      = static_cast<LONG>(off.y);
        SendInput(1, &inp, sizeof(INPUT));
    }
    Logger::Get().Log("offset_applied",
        {{"step", std::to_string(idx)},
         {"x",    std::to_string(off.x)},
         {"y",    std::to_string(off.y)}});
}

DWORD WINAPI RecoilCompensator::WorkerProc(LPVOID param) {
    reinterpret_cast<RecoilCompensator*>(param)->RunWorker();
    return 0;
}

void RecoilCompensator::RunWorker() {
    const HANDLE ev[2] = { h_fire_, h_stop_ };
    while (running_.load(std::memory_order_acquire)) {
        if (WaitForMultipleObjects(2, ev, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
            break;

        while (firing_.load(std::memory_order_acquire) &&
               running_.load(std::memory_order_acquire))
        {
            if (profile_.offsets.empty()) { Sleep(1); continue; }

            int s   = step_.load(std::memory_order_relaxed);
            int idx = s % static_cast<int>(profile_.offsets.size());
            const Offset& off = profile_.offsets[idx];

            ApplyOffset(off, s);
            step_.store(s + 1, std::memory_order_relaxed);

            // QPC spin-wait — точнее Sleep для <10ms
            LARGE_INTEGER start, now;
            QueryPerformanceCounter(&start);
            LONGLONG tgt = (static_cast<LONGLONG>(off.delay_ms) * qpc_freq_.QuadPart) / 1000LL;
            do {
                QueryPerformanceCounter(&now);
                if (!firing_.load(std::memory_order_relaxed)) break;
            } while ((now.QuadPart - start.QuadPart) < tgt);
        }
    }
}
