// language: C++17, file: overlay.h, target: Windows 10/11 x64, MSVC
#pragma once
#include <windows.h>

struct OverlayState {
    int  step_x{ 0 }, step_y{ 0 };
    bool firing{ false };
    bool active{ false };
    char weapon[16]{ "AK47" };
    int  current_step{ 0 };
};

class Overlay {
public:
    bool Create(HINSTANCE hInst);
    void Destroy();
    void Update(const OverlayState& s);
    void Show(bool visible);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Paint(HDC hdc);

    HWND         hwnd_{ nullptr };
    OverlayState state_{};
    static constexpr int W = 200, H = 72;
};
