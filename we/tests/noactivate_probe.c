/* Which window becomes active / foreground when a WS_EX_NOACTIVATE window is
 * around? The docs say the system does not bring such a window to the
 * foreground when the foreground window is minimized or closed; check that and
 * the other activation paths, with the WS_EX_NOACTIVATE window in this process
 * and in another one. Run on Windows (in the interactive session) and under
 * Wine and compare.
 *
 *   noactivate_probe.exe            run all cases
 *   noactivate_probe.exe child      (internal) owns the other-process window */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HWND a, b;
static void report(const char *step);

static const char *name(HWND hwnd)
{
    static char buf[4][64];
    static int i;
    char *s = buf[i++ & 3];
    if (!hwnd) return "(none)";
    if (!GetWindowTextA(hwnd, s, 64)) snprintf(s, 64, "%p", hwnd);
    return s;
}

static void pump(void)
{
    MSG msg;
    DWORD end = GetTickCount() + 300;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(10);
    }
}

static void report(const char *step)
{
    pump();
    printf("  %-44s fg %-8s active %s\n", step, name(GetForegroundWindow()), name(GetActiveWindow()));
}

static HWND make(const char *title, DWORD ex, DWORD style, int x)
{
    HWND hwnd = CreateWindowExA(ex, "probe", title, style, x, 100, 300, 200, 0, 0, 0, 0);
    if (ex & WS_EX_LAYERED) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    return hwnd;
}

static void activate_a(void)
{
    ShowWindow(a, SW_RESTORE);
    ShowWindow(a, SW_SHOW);
    SetForegroundWindow(a);
    SetActiveWindow(a);
    pump();
    if (GetForegroundWindow() != a || GetActiveWindow() != a) report("  !! A could not be re-activated");
}

static void run_cases(const char *label, BOOL other_process)
{
    printf("%s\n", label);
    activate_a();
    report("start, A activated");

    ShowWindow(a, SW_MINIMIZE);
    report("ShowWindow(A, SW_MINIMIZE)");
    activate_a();

    ShowWindow(a, SW_HIDE);
    report("ShowWindow(A, SW_HIDE)");
    activate_a();

    {
        HWND c = make("C", 0, WS_OVERLAPPEDWINDOW, 700);
        ShowWindow(c, SW_SHOW);
        SetForegroundWindow(c);
        ShowWindow(a, SW_MINIMIZE);
        report("C active, A minimized, DestroyWindow(C)");
        DestroyWindow(c);
        report("  -> after destroy");
        activate_a();
    }

    if (!other_process)
    {
        ShowWindow(b, SW_HIDE);
        ShowWindow(b, SW_SHOW);
        report("ShowWindow(B, SW_HIDE then SW_SHOW)");
        activate_a();

        SetWindowPos(b, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        report("SetWindowPos(B, HWND_TOP, no NOACTIVATE)");
        activate_a();

        SetForegroundWindow(b);
        report("SetForegroundWindow(B)");
        activate_a();
    }
}

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(int argc, char **argv)
{
    WNDCLASSA cls = {0};
    cls.lpfnWndProc = proc;
    cls.lpszClassName = "probe";
    cls.hbrBackground = GetStockObject(GRAY_BRUSH);
    RegisterClassA(&cls);

    if (argc > 1 && !strcmp(argv[1], "child"))
    {
        /* like the overlay host: a layered, tool, no-activate popup */
        HWND w = make("B-other", WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED, WS_POPUP, 400);
        ShowWindow(w, SW_SHOWNOACTIVATE);
        for (int i = 0; i < 300; i++) pump();  /* ~90 s; the parent kills us */
        return 0;
    }

    printf("noactivate_probe, wine %s\n", GetProcAddress(GetModuleHandleA("ntdll"), "wine_get_version") ? "yes" : "no");
    a = make("A", 0, WS_OVERLAPPEDWINDOW, 100);
    ShowWindow(a, SW_SHOW);

    b = make("B-plain", WS_EX_NOACTIVATE, WS_POPUP, 400);
    ShowWindow(b, SW_SHOWNOACTIVATE);
    run_cases("B: WS_EX_NOACTIVATE popup, same process", FALSE);
    DestroyWindow(b);

    b = make("B-overlay", WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED, WS_POPUP, 400);
    ShowWindow(b, SW_SHOWNOACTIVATE);
    run_cases("B: NOACTIVATE|TOOLWINDOW|LAYERED popup, same process", FALSE);
    DestroyWindow(b);

    b = make("B-normal", 0, WS_POPUP, 400);
    ShowWindow(b, SW_SHOWNOACTIVATE);
    run_cases("control, B: plain popup without NOACTIVATE", FALSE);
    DestroyWindow(b);

    {
        char path[MAX_PATH], cmd[MAX_PATH + 16];
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        GetModuleFileNameA(0, path, sizeof(path));
        snprintf(cmd, sizeof(cmd), "\"%s\" child", path);
        if (CreateProcessA(path, cmd, 0, 0, FALSE, 0, 0, 0, &si, &pi))
        {
            Sleep(1500);
            run_cases("B: NOACTIVATE|TOOLWINDOW|LAYERED popup, other process", TRUE);
            TerminateProcess(pi.hProcess, 0);
        }
    }
    return 0;
}
