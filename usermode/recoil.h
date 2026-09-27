// language: C++17, file: recoil.h, target: Windows 10/11 x64, MSVC
#pragma once
#include <windows.h>
#include <atomic>
#include <vector>
#include <string>

struct Offset { int x, y, delay_ms; };

struct WeaponProfile {
    std::string         weapon;
    std::vector<Offset> offsets;
    std::string         calibrated_date;
    std::string         spray_pattern;
};

WeaponProfile MakeAK47Profile();
WeaponProfile MakeM4A1Profile();
WeaponProfile MakeAWPProfile();

class RecoilCompensator {
public:
    explicit RecoilCompensator(bool use_kernel = false);
    ~RecoilCompensator();

    void StartFiring();
    void StopFiring();
    void SetProfile(const WeaponProfile& p);
    void SetEnabled(bool v) { enabled_.store(v, std::memory_order_release); }

    bool IsEnabled() const { return enabled_.load(std::memory_order_relaxed); }
    bool IsFiring()  const { return firing_.load(std::memory_order_relaxed);  }
    int  CurrentStep() const { return step_.load(std::memory_order_relaxed);  }
    const WeaponProfile& Profile() const { return profile_; }

private:
    static DWORD WINAPI WorkerProc(LPVOID param);
    void RunWorker();
    void ApplyOffset(const Offset& off, int idx);
    bool OpenKernelDriver();

    WeaponProfile     profile_;
    bool              use_kernel_;
    HANDLE            h_driver_{ INVALID_HANDLE_VALUE };

    std::atomic<bool> running_{ true };
    std::atomic<bool> enabled_{ true };
    std::atomic<bool> firing_{ false };
    std::atomic<int>  step_{ 0 };

    HANDLE            h_thread_{ INVALID_HANDLE_VALUE };
    HANDLE            h_fire_{ nullptr };
    HANDLE            h_stop_{ nullptr };
    LARGE_INTEGER     qpc_freq_{};
};
