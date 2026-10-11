/* For 0050. A Direct3D 9 window in windowed mode the way games make it: WS_POPUP | WS_BORDER,
 * client area the monitor's size (so the border lies outside the monitor), or argv[2] pixels
 * high in all. It presents continuously and puts its rectangle back whenever it gets resized.
 * Exits after argv[1] seconds. */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
static DWORD start;
static RECT game_rect;
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("%6.2f ", (GetTickCount() - start) / 1000.0);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n"); fflush(stdout);
}
static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RECT r;
    if (msg == WM_SIZE && wp == SIZE_RESTORED)
    {
        GetWindowRect(hwnd, &r);
        say("WM_SIZE (%ld,%ld)-(%ld,%ld)", r.left, r.top, r.right, r.bottom);
        if (r.right - r.left != game_rect.right - game_rect.left || r.bottom - r.top != game_rect.bottom - game_rect.top)
            SetWindowPos(hwnd, NULL, game_rect.left, game_rect.top, game_rect.right - game_rect.left,
                         game_rect.bottom - game_rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
int main(int argc, char **argv)
{
    WNDCLASSW wc = { 0, proc, 0, 0, GetModuleHandleW(0), 0, LoadCursorW(0, (LPCWSTR)IDC_ARROW), 0, 0, L"wayland_fullscreen_d3d9" };
    DWORD style = WS_POPUP | WS_BORDER | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE;
    DWORD secs = argc > 1 ? atoi(argv[1]) : 30;
    int height = argc > 2 ? atoi(argv[2]) : 0, frame = 0;
    MONITORINFO mi = { sizeof(mi) }; POINT pt = {0, 0};
    D3DPRESENT_PARAMETERS pp = {0};
    IDirect3DDevice9 *dev;
    IDirect3D9 *d3d;
    HWND hwnd; MSG m;
    start = GetTickCount();
    RegisterClassW(&wc);
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    game_rect = mi.rcMonitor;
    AdjustWindowRect(&game_rect, style, FALSE);
    if (height) game_rect.bottom = game_rect.top + height;
    hwnd = CreateWindowW(wc.lpszClassName, wc.lpszClassName, style, game_rect.left, game_rect.top,
                         game_rect.right - game_rect.left, game_rect.bottom - game_rect.top, 0, 0, wc.hInstance, 0);
    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (FAILED(IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev)))
    {
        say("CreateDevice failed");
        return 1;
    }
    while (GetTickCount() - start < secs * 1000)
    {
        while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB((frame * 3) & 255, 80, 160), 1.0f, 0);
        IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
        frame++;
    }
    return 0;
}
