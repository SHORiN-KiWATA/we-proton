/* For 0050. A window that makes itself fullscreen the way games do (WS_POPUP covering the
 * monitor) after 2 s and goes back to a normal window after 5 s. */
#include <windows.h>
static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MONITORINFO mi = { sizeof(mi) };
    switch (msg)
    {
    case WM_TIMER:
        if (wp == 1)
        {
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        else if (wp == 2)
        {
            SetWindowLongW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            SetWindowPos(hwnd, HWND_TOP, 100, 100, 800, 600, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        else PostQuitMessage(0);
        KillTimer(hwnd, wp);
        return 0;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); FillRect(dc, &ps.rcPaint, GetStockObject(GRAY_BRUSH)); EndPaint(hwnd, &ps); return 0; }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSW wc = { 0, proc, 0, 0, inst, 0, LoadCursorW(0, (LPCWSTR)IDC_ARROW), 0, 0, L"wayland_fullscreen_probe" };
    HWND hwnd; MSG m;
    RegisterClassW(&wc);
    hwnd = CreateWindowW(L"wayland_fullscreen_probe", L"wayland_fullscreen_probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 800, 600, 0, 0, inst, 0);
    SetTimer(hwnd, 1, 2000, NULL); SetTimer(hwnd, 2, 5000, NULL); SetTimer(hwnd, 3, 7000, NULL);
    while (GetMessageW(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
