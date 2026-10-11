/* Does a click-through window still see the cursor come back?
 *
 * Mimics Electron's setIgnoreMouseEvents(true, { forward: true }): after a
 * second the window gets WS_EX_LAYERED | WS_EX_TRANSPARENT, and a low-level
 * mouse hook watches the cursor; once the hook sees it over the window, the
 * window takes input again (the styles are cleared). input_shape_probe.sh moves
 * the pointer from outside and reads the X side of the window.
 *
 *   input_shape_probe.exe <log file> [topmost]
 *
 * With "topmost" the window is WS_EX_TOPMOST, like an overlay.
 *
 * The window is called "input_shape_probe" and stays up for about 10 seconds.
 * It writes "transparent" to the log when the styles are set, then the result. */
#include <windows.h>
#include <stdio.h>

#define WM_TAKE_INPUT (WM_APP + 1)

static HWND hwnd;
static FILE *out;
static BOOL transparent;
static LONG hook_moves, hook_inside;
static POINT last_pt;

static LRESULT CALLBACK mouse_hook(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && wparam == WM_MOUSEMOVE && transparent)
    {
        MSLLHOOKSTRUCT *info = (MSLLHOOKSTRUCT *)lparam;
        RECT rc;

        hook_moves++;
        last_pt = info->pt;
        GetWindowRect(hwnd, &rc);
        if (PtInRect(&rc, info->pt))
        {
            hook_inside++;
            PostMessageW(hwnd, WM_TAKE_INPUT, 0, 0);
        }
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_TAKE_INPUT && transparent)
    {
        SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT));
        transparent = FALSE;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;

    while (GetTickCount() < end)
    {
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(5);
    }
}

int main(int argc, char **argv)
{
    WNDCLASSW wc = {0};
    POINT pt;

    out = fopen(argv[1], "w");
    wc.lpfnWndProc = wndproc; wc.lpszClassName = L"input_shape_probe"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(argc > 2 && !strcmp(argv[2], "topmost") ? WS_EX_TOPMOST : 0, L"input_shape_probe", L"input_shape_probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           100, 100, 300, 200, NULL, NULL, NULL, NULL);
    SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, GetModuleHandleW(NULL), 0);
    pump(1500);

    SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    transparent = TRUE;
    fprintf(out, "transparent: exstyle %#lx\n", GetWindowLongW(hwnd, GWL_EXSTYLE));
    fflush(out);
    pump(6000);

    GetCursorPos(&pt);
    fprintf(out, "hook saw %ld moves, %ld over the window, last (%ld,%ld); GetCursorPos (%ld,%ld)\n",
            hook_moves, hook_inside, last_pt.x, last_pt.y, pt.x, pt.y);
    fprintf(out, "result: exstyle %#lx -> %s\n", GetWindowLongW(hwnd, GWL_EXSTYLE),
            transparent ? "still click-through (stuck)" : "takes input again");
    fclose(out);
    pump(2000);
    return 0;
}
