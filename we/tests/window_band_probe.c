/* window_band_probe：GetWindowBand / NtUserGetWindowBand 在各种窗口上的返回值和错误码。
 * 用法：window_band_probe.exe            —— 常规用例
 *       window_band_probe.exe nullptr    —— 有效窗口 + 空的输出指针（可能直接崩）
 *       window_band_probe.exe nullhwnd   —— 空窗口句柄 + 空的输出指针 */
#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *band_fn)(HWND, DWORD *);
static band_fn user_band, nt_band;

static void probe(const char *what, HWND hwnd)
{
    DWORD b1 = 0xdeadbeef, b2 = 0xdeadbeef, e1, e2;
    BOOL r1 = FALSE, r2 = FALSE;
    SetLastError(0xcafe);
    if (user_band) r1 = user_band(hwnd, &b1);
    e1 = GetLastError();
    SetLastError(0xcafe);
    if (nt_band) r2 = nt_band(hwnd, &b2);
    e2 = GetLastError();
    printf("%-28s hwnd=%p  user32: ret=%d band=%#lx err=%lu  win32u: ret=%d band=%#lx err=%lu\n",
           what, hwnd, r1, b1, e1, r2, b2, e2);
}

int main(int argc, char **argv)
{
    HMODULE u = LoadLibraryA("user32.dll"), w = LoadLibraryA("win32u.dll");
    user_band = (band_fn)GetProcAddress(u, "GetWindowBand");
    nt_band = w ? (band_fn)GetProcAddress(w, "NtUserGetWindowBand") : NULL;
    printf("GetWindowBand=%p NtUserGetWindowBand=%p\n", user_band, nt_band);
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 1) {
        HWND h = NULL;
        if (!strcmp(argv[1], "nullptr"))
            h = CreateWindowExA(0, "static", "x", WS_OVERLAPPED, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
        SetLastError(0xcafe);
        BOOL r = user_band ? user_band(h, NULL) : -1;
        printf("user32 %s: ret=%d err=%lu\n", argv[1], r, GetLastError());
        SetLastError(0xcafe);
        r = nt_band ? nt_band(h, NULL) : -1;
        printf("win32u %s: ret=%d err=%lu\n", argv[1], r, GetLastError());
        return 0;
    }

    HWND top = CreateWindowExA(0, "static", "top", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    HWND vis = CreateWindowExA(0, "static", "visible", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    HWND topmost = CreateWindowExA(WS_EX_TOPMOST, "static", "topmost", WS_POPUP | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    HWND overlay = CreateWindowExA(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                   "static", "overlay", WS_POPUP | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    HWND child = CreateWindowExA(0, "static", "child", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, vis, NULL, NULL, NULL);
    HWND msgonly = CreateWindowExA(0, "static", "msg", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    HWND owned = CreateWindowExA(0, "static", "owned", WS_POPUP, 0, 0, 10, 10, vis, NULL, NULL, NULL);

    probe("top-level hidden", top);
    probe("top-level visible", vis);
    probe("topmost popup", topmost);
    probe("layered transparent overlay", overlay);
    probe("child", child);
    probe("owned popup", owned);
    probe("message-only", msgonly);
    probe("desktop", GetDesktopWindow());
    probe("message parent", GetAncestor(msgonly, GA_PARENT));
    probe("Shell_TrayWnd", FindWindowA("Shell_TrayWnd", NULL));
    probe("Progman", FindWindowA("Progman", NULL));
    probe("console", GetConsoleWindow());
    probe("NULL", NULL);
    probe("invalid 0x12345", (HWND)(ULONG_PTR)0x12345);
    DestroyWindow(top);
    probe("destroyed", top);
    return 0;
}
