/* Layered child windows (WS_CHILD | WS_EX_LAYERED, Windows 8+) updated with
 * UpdateLayeredWindow: where does their content end up on screen?
 *
 * A borderless popup host (WS_CAPTION | WS_THICKFRAME | WS_CLIPCHILDREN, the
 * frame removed in WM_NCCALCSIZE) paints itself blue. A layered child covers
 * its client area and is filled with UpdateLayeredWindow from a premultiplied
 * 32 bpp DIB:
 *
 *   left half          opaque green
 *   right, top half    red at 50 % alpha
 *   right, bottom half fully transparent
 *
 * At each step the probe prints "SAMPLE <step> <x> <y>" (the host's screen
 * position) and waits two seconds, so that a screen grabber outside the
 * program can read the pixels at the centre of each region; reading the screen
 * back through GDI is not reliable under Wine. layered_child_grab.py runs the
 * probe on an X display and does the grabbing. The steps: shown, after moving
 * the host, after updating the child again, after moving the child right by
 * half its width with pptDst. Under Wine the probe also prints
 * whether the child got its own X window (__wine_x11_whole_window).
 *
 *   layered_child_probe.exe [pos]   pos: pass pptDst to UpdateLayeredWindow
 */
#include <windows.h>
#include <stdio.h>

#define W 320
#define H 200

static HWND host, child;

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_NCCALCSIZE:
        return 0;  /* no frame: client area == window rect */
    case WM_ERASEBKGND:
    {
        RECT rc;
        HBRUSH blue = CreateSolidBrush(RGB(0, 0, 255));
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, blue);
        DeleteObject(blue);
        return 1;
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        Sleep(10);
    }
}

static void update_child(BOOL with_pos, int dst_x)
{
    BITMAPINFO bmi = {{sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB}};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT src = {0, 0}, dst = {dst_x, 0};
    SIZE size = {W, H};
    DWORD *bits;
    HDC hdc = CreateCompatibleDC(0);
    HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&bits, 0, 0);
    HGDIOBJ old = SelectObject(hdc, bmp);
    int x, y;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            bits[y * W + x] = x < W / 2 ? 0xff00ff00          /* opaque green */
                            : y < H / 2 ? 0x80800000          /* red, alpha 128, premultiplied */
                            : 0x00000000;                     /* transparent */

    if (!UpdateLayeredWindow(child, 0, with_pos ? &dst : NULL, &size, hdc, &src, 0, &blend, ULW_ALPHA))
        printf("UpdateLayeredWindow failed, error %lu\n", GetLastError());
    SelectObject(hdc, old);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

static void sample(const char *step)
{
    RECT rc;

    GetWindowRect(host, &rc);
    printf("SAMPLE %s %ld %ld\n", step, rc.left, rc.top);
    fflush(stdout);
    pump(2000);
}

int main(int argc, char **argv)
{
    BOOL with_pos = argc > 1 && !strcmp(argv[1], "pos");
    WNDCLASSA wc = {0};
    RECT rc;

    setvbuf(stdout, NULL, _IONBF, 0);

    wc.lpfnWndProc = host_proc;
    wc.hInstance = GetModuleHandleA(0);
    wc.lpszClassName = "probe_host";
    wc.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.lpszClassName = "probe_child";
    RegisterClassA(&wc);

    host = CreateWindowExA(0, "probe_host", "probe host",
                           WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                           200, 150, W, H, 0, 0, 0, 0);
    child = CreateWindowExA(WS_EX_LAYERED | WS_EX_NOACTIVATE, "probe_child", "",
                            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, W, H, host, 0, 0, 0);
    printf("host %p child %p, pptDst %s\n", host, child, with_pos ? "{0,0}" : "NULL");

    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    update_child(with_pos, 0);
    pump(1500);

    GetWindowRect(child, &rc);
    printf("child rect %ld,%ld-%ld,%ld, own X window: %#lx\n", rc.left, rc.top, rc.right, rc.bottom,
           (unsigned long)(ULONG_PTR)GetPropA(child, "__wine_x11_whole_window"));
    sample("shown");

    SetWindowPos(host, 0, 400, 300, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    pump(1000);
    sample("moved");

    update_child(with_pos, 0);
    pump(1000);
    sample("updated");

    /* move the child right by half its width: the left half of the host is
     * uncovered and should show the host's own blue */
    update_child(TRUE, W / 2);
    pump(1000);
    sample("child-moved");

    DestroyWindow(host);
    return 0;
}
