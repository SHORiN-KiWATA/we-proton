/* A window that gets WS_EX_TRANSPARENT (with "layered" also WS_EX_LAYERED) after
 * a second; prints what the server's hit test (WindowFromPoint) says about the
 * centre of the window before and after. Its X input shape is read from
 * outside, see input_shape_probe.sh.
 *
 *   input_shape_probe.exe <log file> [layered]
 *
 * The window is called "input_shape_probe" and stays up for about 9 seconds. */
#include <windows.h>
#include <stdio.h>
static FILE *out;
int main(int argc, char **argv)
{
    BOOL layered = argc > 2 && !strcmp(argv[2], "layered");
    HWND hwnd;
    POINT pt;
    RECT rc;
    MSG msg;
    DWORD end;
    WNDCLASSW wc = {0};
    out = fopen(argv[1], "w");
    wc.lpfnWndProc = DefWindowProcW; wc.lpszClassName = L"input_shape_probe"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"input_shape_probe", L"input_shape_probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 300, 200, NULL, NULL, NULL, NULL);
    for (end = GetTickCount() + 1500; GetTickCount() < end;) { while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(10); }
    GetWindowRect(hwnd, &rc); pt.x = (rc.left + rc.right) / 2; pt.y = (rc.top + rc.bottom) / 2;
    fprintf(out, "before: exstyle %#lx WindowFromPoint(centre)==window %d\n", GetWindowLongW(hwnd, GWL_EXSTYLE), WindowFromPoint(pt) == hwnd);
    SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT | (layered ? WS_EX_LAYERED : 0));
    if (layered) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    for (end = GetTickCount() + 1500; GetTickCount() < end;) { while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(10); }
    fprintf(out, "after:  exstyle %#lx WindowFromPoint(centre)==window %d\n", GetWindowLongW(hwnd, GWL_EXSTYLE), WindowFromPoint(pt) == hwnd);
    fflush(out);
    for (end = GetTickCount() + 6000; GetTickCount() < end;) { while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(10); }
    return 0;
}
