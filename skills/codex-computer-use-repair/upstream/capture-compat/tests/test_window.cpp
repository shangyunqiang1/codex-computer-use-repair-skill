#include <windows.h>

LRESULT CALLBACK TestWindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (message == WM_TIMER) { DestroyWindow(window); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{}; GetClientRect(window, &rect);
        HBRUSH brush = CreateSolidBrush(RGB(32, 128, 224));
        FillRect(dc, &rect, brush); DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, L"Computer Use screenshot test - blue panel", -1, &rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(window, &paint); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    WNDCLASSW klass{};
    klass.lpfnWndProc = TestWindowProc; klass.hInstance = instance;
    klass.lpszClassName = L"CodexCaptureTestWindow";
    if (!RegisterClassW(&klass)) return 1;
    HWND window = CreateWindowExW(0, klass.lpszClassName, L"Codex capture fixture",
        WS_OVERLAPPEDWINDOW, 80, 80, 640, 400, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    SetTimer(window, 1, 600000, nullptr);
    ShowWindow(window, SW_SHOWNOACTIVATE); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    return 0;
}
