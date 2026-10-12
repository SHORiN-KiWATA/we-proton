/* Log WM_ACTIVATEAPP / WM_NCACTIVATE / WM_ACTIVATE (wParam, lParam) on
 * foreground changes: the first window of a process, two threads of one
 * process, two processes, switching to the desktop window and back, and
 * (with --x11-sync) focus moving to a native X11 window and back.
 * The lParam of WM_ACTIVATEAPP is translated to "which thread/process".
 * Run on Windows (interactive session) and under Wine and compare.
 *
 *   activateapp_probe.exe [--x11-sync C:\dir]
 *   activateapp_probe.exe --child <args>      (internal) the other process
 *
 * --x11-sync: write C:\dir\<step>.req and wait for C:\dir\<step>.done, a host
 * helper performs the X11 focus change in between. */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#define CMD_CREATE   (WM_APP + 1)
#define CMD_FG       (WM_APP + 2)
#define CMD_FG_DESK  (WM_APP + 3)
#define CMD_DESTROY  (WM_APP + 4)
#define CMD_QUIT     (WM_APP + 5)
#define CMD_FG_A     (WM_APP + 6)
#define CMD_FG_B     (WM_APP + 7)

static BOOL is_child;
static DWORD p_pid, p_main, p_worker, c_pid, c_main, desk_tid, desk_pid;
static HWND wa, wb, wc;
static HANDLE out, worker_done, child_ack, child_ready;
static LARGE_INTEGER freq, base;
static char x11_dir[MAX_PATH];

static void say(const char *fmt, ...)
{
    char buf[1024];
    LARGE_INTEGER now;
    DWORD len, written;
    va_list ap;

    QueryPerformanceCounter(&now);
    len = snprintf(buf, sizeof(buf), "%8.1f %s ", (double)(now.QuadPart - base.QuadPart) * 1000.0 / freq.QuadPart,
                   is_child ? "[C]" : "[P]");
    va_start(ap, fmt);
    len += vsnprintf(buf + len, sizeof(buf) - len - 2, fmt, ap);
    va_end(ap);
    if (len > sizeof(buf) - 2) len = sizeof(buf) - 2;
    buf[len++] = '\r';
    buf[len++] = '\n';
    WriteFile(out, buf, len, &written, NULL);
}

static void plain(const char *fmt, ...)
{
    char buf[1024];
    DWORD len, written;
    va_list ap;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (len > sizeof(buf) - 2) len = sizeof(buf) - 2;
    buf[len++] = '\r';
    buf[len++] = '\n';
    WriteFile(out, buf, len, &written, NULL);
}

static const char *proc_name(DWORD pid)
{
    static char buf[4][MAX_PATH];
    static int i;
    char *s = buf[i++ & 3], *p;
    DWORD size = MAX_PATH;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    strcpy(s, "?");
    if (!h) return s;
    if (QueryFullProcessImageNameA(h, 0, s, &size))
    {
        if ((p = strrchr(s, '\\'))) memmove(s, p + 1, strlen(p + 1) + 1);
    }
    CloseHandle(h);
    return s;
}

static const char *tid_name(DWORD tid, DWORD self)
{
    static char buf[8][160];
    static int i;
    char *s = buf[i++ & 7];
    char who[96];

    if (!tid) return "0 (no thread)";
    if (tid == p_main) strcpy(who, "P.main");
    else if (tid == p_worker) strcpy(who, "P.worker");
    else if (c_main && tid == c_main) strcpy(who, "C.main");
    else
    {
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
        DWORD pid = 0;
        if (h)
        {
            pid = GetProcessIdOfThread(h);
            CloseHandle(h);
        }
        if (tid == desk_tid) snprintf(who, sizeof(who), "desktop-window thread, pid %lu %s", desk_pid, proc_name(desk_pid));
        else if (pid) snprintf(who, sizeof(who), "other thread, pid %lu %s", pid, proc_name(pid));
        else strcpy(who, "unknown thread");
    }
    snprintf(s, 160, "%#lx=%s%s", tid, who, tid == self ? " <== SELF (receiving thread)" : "");
    return s;
}

static const char *hwnd_name(HWND hwnd)
{
    static char buf[8][128];
    static int i;
    char *s = buf[i++ & 7];
    DWORD pid, tid;

    if (!hwnd) return "NULL";
    if (hwnd == wa) return "A(P.main)";
    if (hwnd == wb) return "B(P.worker)";
    if (hwnd == wc) return "C(C.main)";
    if (hwnd == GetDesktopWindow()) return "desktop";
    tid = GetWindowThreadProcessId(hwnd, &pid);
    snprintf(s, 128, "%p(%s)", hwnd, tid ? tid_name(tid, 0) : "not a window");
    return s;
}

static const char *wname(HWND hwnd)
{
    if (hwnd == wa) return "A";
    if (hwnd == wb) return "B";
    if (hwnd == wc) return "C";
    return "?";
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DWORD self = GetCurrentThreadId();
    LRESULT ret;

    switch (msg)
    {
    case WM_ACTIVATEAPP:
        say("  %s WM_ACTIVATEAPP  wParam=%d lParam=%s", wname(hwnd), (int)wp, tid_name((DWORD)lp, self));
        break;
    case WM_NCACTIVATE:
        ret = DefWindowProcA(hwnd, msg, wp, lp);
        say("  %s WM_NCACTIVATE   wParam=%d lParam=%s", wname(hwnd), (int)wp, hwnd_name((HWND)lp));
        return ret;
    case WM_ACTIVATE:
        say("  %s WM_ACTIVATE     wParam=%s%s lParam=%s", wname(hwnd),
             LOWORD(wp) == WA_INACTIVE ? "WA_INACTIVE" : LOWORD(wp) == WA_ACTIVE ? "WA_ACTIVE" : "WA_CLICKACTIVE",
             HIWORD(wp) ? "|minimized" : "", hwnd_name((HWND)lp));
        break;
    case CMD_FG:
        if (is_child)
        {
            BOOL r = SetForegroundWindow(hwnd);
            say("  (child: SetForegroundWindow(C) -> %d)", r);
            SetEvent(child_ack);
            return 0;
        }
        break;
    case CMD_FG_DESK:
        if (is_child)
        {
            BOOL r = SetForegroundWindow(GetDesktopWindow());
            say("  (child: SetForegroundWindow(desktop) -> %d)", r);
            SetEvent(child_ack);
            return 0;
        }
        break;
    case CMD_FG_A:
    case CMD_FG_B:
        if (is_child)
        {
            BOOL r = SetForegroundWindow(msg == CMD_FG_A ? wa : wb);
            say("  (child: SetForegroundWindow(%s) -> %d)", msg == CMD_FG_A ? "A" : "B", r);
            SetEvent(child_ack);
            return 0;
        }
        break;
    case CMD_DESTROY:
        if (is_child)
        {
            say("  (child: DestroyWindow(C))");
            DestroyWindow(hwnd);
            wc = 0;
            say("  (child: DestroyWindow done, fg now %s)", hwnd_name(GetForegroundWindow()));
            SetEvent(child_ack);
            PostQuitMessage(0);
            return 0;
        }
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND make(const char *title, int x)
{
    return CreateWindowExA(0, "actprobe", title, WS_OVERLAPPEDWINDOW, x, 100, 240, 160, 0, 0, 0, 0);
}

static void pump_for(DWORD ms)
{
    MSG msg;
    DWORD end = GetTickCount() + ms;
    for (;;)
    {
        LONG left = (LONG)(end - GetTickCount());
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        if (left <= 0) break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, left, QS_ALLINPUT);
    }
}

static BOOL wait_pump(HANDLE ev, DWORD ms)
{
    MSG msg;
    DWORD end = GetTickCount() + ms;
    for (;;)
    {
        LONG left = (LONG)(end - GetTickCount());
        DWORD r;
        if (left <= 0) return FALSE;
        r = MsgWaitForMultipleObjects(1, &ev, FALSE, left, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) return TRUE;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    }
}

static void gti(const char *who, DWORD tid, char *s, size_t n)
{
    GUITHREADINFO info = { sizeof(info) };
    if (!tid) return;
    if (!GetGUIThreadInfo(tid, &info)) { snprintf(s + strlen(s), n - strlen(s), " | %s: n/a", who); return; }
    snprintf(s + strlen(s), n - strlen(s), " | %s active=%s focus=%s", who, hwnd_name(info.hwndActive),
             hwnd_name(info.hwndFocus));
}

static void state(void)
{
    char s[512];
    pump_for(500);
    snprintf(s, sizeof(s), "  state: fg=%s", hwnd_name(GetForegroundWindow()));
    gti("P.main", p_main, s, sizeof(s));
    gti("P.worker", p_worker, s, sizeof(s));
    if (wc) gti("C.main", c_main, s, sizeof(s));
    say("%s", s);
}

static void step(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    plain("");
    say("=== %s", buf);
}

/* ---- worker thread of the main process ---- */
static DWORD WINAPI worker(void *arg)
{
    MSG msg;
    PeekMessageA(&msg, 0, 0, 0, PM_NOREMOVE);
    SetEvent(worker_done);
    while (GetMessageA(&msg, 0, 0, 0) > 0)
    {
        if (!msg.hwnd)
        {
            switch (msg.message)
            {
            case CMD_CREATE:
                wb = make("B (P.worker)", 300);
                say("  (worker: ShowWindow(B, SW_SHOW))");
                ShowWindow(wb, SW_SHOW);
                UpdateWindow(wb);
                SetEvent(worker_done);
                continue;
            case CMD_FG:
            {
                BOOL r = SetForegroundWindow(wb);
                say("  (worker: SetForegroundWindow(B) -> %d)", r);
                SetEvent(worker_done);
                continue;
            }
            case CMD_QUIT:
                DestroyWindow(wb);
                return 0;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

static void worker_cmd(UINT cmd)
{
    ResetEvent(worker_done);
    PostThreadMessageA(p_worker, cmd, 0, 0);
    if (!wait_pump(worker_done, 5000)) say("  !! worker did not answer");
}

static void child_cmd(UINT cmd)
{
    ResetEvent(child_ack);
    AllowSetForegroundWindow(c_pid);
    PostMessageA(wc, cmd, 0, 0);
    if (!wait_pump(child_ack, 5000)) say("  !! child did not answer");
}

static void main_fg(HWND hwnd, const char *what)
{
    BOOL r = SetForegroundWindow(hwnd);
    say("  (main: SetForegroundWindow(%s) -> %d)", what, r);
}

static BOOL x11_step(const char *name)
{
    char req[MAX_PATH], done[MAX_PATH];
    HANDLE h;
    DWORD end;

    snprintf(req, sizeof(req), "%s\\%s.req", x11_dir, name);
    snprintf(done, sizeof(done), "%s\\%s.done", x11_dir, name);
    DeleteFileA(done);
    h = CreateFileA(req, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { say("  !! cannot create %s", req); return FALSE; }
    CloseHandle(h);
    end = GetTickCount() + 15000;
    while (GetFileAttributesA(done) == INVALID_FILE_ATTRIBUTES)
    {
        if ((LONG)(end - GetTickCount()) <= 0) { say("  !! host helper did not do %s", name); return FALSE; }
        pump_for(50);
    }
    say("  (host helper: %s done)", name);
    return TRUE;
}

static int run_child(int argc, char **argv)
{
    MSG msg;
    char name[64];

    /* --child p_pid p_main p_worker hwndA hwndB base */
    p_pid = strtoul(argv[2], NULL, 0);
    p_main = strtoul(argv[3], NULL, 0);
    p_worker = strtoul(argv[4], NULL, 0);
    wa = (HWND)(ULONG_PTR)strtoull(argv[5], NULL, 0);
    wb = (HWND)(ULONG_PTR)strtoull(argv[6], NULL, 0);
    base.QuadPart = _strtoi64(argv[7], NULL, 0);
    c_pid = GetCurrentProcessId();
    c_main = GetCurrentThreadId();

    snprintf(name, sizeof(name), "actprobe_ack_%lu", p_pid);
    child_ack = OpenEventA(EVENT_ALL_ACCESS, FALSE, name);
    snprintf(name, sizeof(name), "actprobe_ready_%lu", p_pid);
    child_ready = OpenEventA(EVENT_ALL_ACCESS, FALSE, name);

    say("  (child: pid %lu, C.main %#lx, fg at start %s)", c_pid, c_main, hwnd_name(GetForegroundWindow()));
    wc = make("C (child)", 560);
    say("  (child: ShowWindow(C, SW_SHOW))");
    ShowWindow(wc, SW_SHOW);
    UpdateWindow(wc);
    SetEvent(child_ready);
    while (GetMessageA(&msg, 0, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

int main(int argc, char **argv)
{
    WNDCLASSA cls = {0};
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    char cmd[512], exe[MAX_PATH], name[64];
    HANDLE thread, inh;
    int i;

    out = GetStdHandle(STD_OUTPUT_HANDLE);
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&base);
    desk_tid = GetWindowThreadProcessId(GetDesktopWindow(), &desk_pid);

    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.lpszClassName = "actprobe";
    RegisterClassA(&cls);

    if (argc >= 8 && !strcmp(argv[1], "--child"))
    {
        is_child = TRUE;
        return run_child(argc, argv);
    }
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--x11-sync") && i + 1 < argc) lstrcpynA(x11_dir, argv[++i], sizeof(x11_dir));

    p_pid = GetCurrentProcessId();
    p_main = GetCurrentThreadId();
    worker_done = CreateEventA(NULL, TRUE, FALSE, NULL);
    snprintf(name, sizeof(name), "actprobe_ack_%lu", p_pid);
    child_ack = CreateEventA(NULL, FALSE, FALSE, name);
    snprintf(name, sizeof(name), "actprobe_ready_%lu", p_pid);
    child_ready = CreateEventA(NULL, FALSE, FALSE, name);
    thread = CreateThread(NULL, 0, worker, NULL, 0, &p_worker);
    WaitForSingleObject(worker_done, INFINITE);

    plain("activateapp_probe, wine %s, %d-bit", GetProcAddress(GetModuleHandleA("ntdll"), "wine_get_version") ? "yes" : "no",
          (int)sizeof(void *) * 8);
    plain("P pid %lu: P.main %#lx, P.worker %#lx; desktop window thread %#lx (pid %lu %s)", p_pid, p_main, p_worker,
          desk_tid, desk_pid, proc_name(desk_pid));
    plain("lParam of WM_ACTIVATEAPP is printed as tid=owner; lParam of WM_ACTIVATE/WM_NCACTIVATE as window");
    {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0, tid = fg ? GetWindowThreadProcessId(fg, &pid) : 0;
        plain("foreground at start: %p (thread %s)", fg, tid_name(tid, 0));
    }

    step("1. first window of the process: A (P.main) ShowWindow(SW_SHOW)");
    wa = make("A (P.main)", 40);
    ShowWindow(wa, SW_SHOW);
    UpdateWindow(wa);
    state();

    step("2. first window of the worker thread: B (P.worker) ShowWindow(SW_SHOW)");
    worker_cmd(CMD_CREATE);
    state();

    step("3. same process, other thread: main SetForegroundWindow(A)");
    main_fg(wa, "A");
    state();

    step("4. same process, other thread: worker SetForegroundWindow(B)");
    worker_cmd(CMD_FG);
    state();

    step("5. main SetForegroundWindow(A)");
    main_fg(wa, "A");
    state();

    step("6. other process starts, its first window C ShowWindow(SW_SHOW) (fg was A)");
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    snprintf(cmd, sizeof(cmd), "\"%s\" --child %lu %lu %lu %#llx %#llx %lld", exe, p_pid, p_main, p_worker,
             (unsigned long long)(ULONG_PTR)wa, (unsigned long long)(ULONG_PTR)wb, (long long)base.QuadPart);
    DuplicateHandle(GetCurrentProcess(), out, GetCurrentProcess(), &inh, 0, TRUE, DUPLICATE_SAME_ACCESS);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = inh;
    si.hStdError = inh;
    AllowSetForegroundWindow(ASFW_ANY);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        say("  !! CreateProcess failed %lu", GetLastError());
        return 1;
    }
    c_pid = pi.dwProcessId;
    c_main = pi.dwThreadId;
    if (!wait_pump(child_ready, 10000)) say("  !! child not ready");
    wc = FindWindowA("actprobe", "C (child)");
    state();

    /* Wine (like Windows) refuses SetForegroundWindow from a background
     * process, so each cross-process switch is made by the process that
     * currently owns the foreground. */
    step("7. other process: child SetForegroundWindow(A) (C -> A)");
    child_cmd(CMD_FG_A);
    state();

    step("8. other process: main SetForegroundWindow(C) (A -> C)");
    main_fg(wc, "C");
    state();

    step("9. other process, other thread: child SetForegroundWindow(B) (C -> B)");
    child_cmd(CMD_FG_B);
    state();

    step("10. main SetForegroundWindow(A) (B -> A), then main SetForegroundWindow(GetDesktopWindow()) (switch away)");
    main_fg(wa, "A");
    pump_for(300);
    main_fg(GetDesktopWindow(), "desktop");
    state();

    step("11. back from the desktop window: main SetForegroundWindow(A)");
    main_fg(wa, "A");
    state();

    step("11b. back from the desktop window to the other process: main SetForegroundWindow(desktop), then child SetForegroundWindow(C)");
    main_fg(GetDesktopWindow(), "desktop");
    pump_for(300);
    child_cmd(CMD_FG);
    state();

    step("11c. child SetForegroundWindow(A) (C -> A)");
    child_cmd(CMD_FG_A);
    state();

    if (x11_dir[0])
    {
        step("12. X11: WM_TAKE_FOCUS to A (A is already foreground; this gives A the X input focus)");
        x11_step("back");
        state();
        step("13. X11: input focus moves to a native (non-Wine) X11 window (host app activated)");
        x11_step("away");
        state();
        step("14. X11: WM_TAKE_FOCUS to A (back from the host app)");
        x11_step("back");
        state();
        step("15. X11: native window takes focus, then WM_TAKE_FOCUS to C (host app -> other process)");
        x11_step("away");
        pump_for(300);
        x11_step("back_c");
        state();
        step("15b. X11: WM_TAKE_FOCUS to A (C -> A through the window manager)");
        x11_step("back");
        state();
    }

    step("16. child SetForegroundWindow(C), then child DestroyWindow(C) while foreground");
    child_cmd(CMD_FG);
    pump_for(300);
    child_cmd(CMD_DESTROY);
    WaitForSingleObject(pi.hProcess, 5000);
    wc = 0;
    state();

    step("17. main SetForegroundWindow(A) after that");
    main_fg(wa, "A");
    state();

    step("18. AttachThreadInput(P.worker, P.main): worker SetForegroundWindow(B) (A -> B, shared input)");
    say("  (AttachThreadInput -> %d)", AttachThreadInput(p_worker, p_main, TRUE));
    pump_for(200);
    worker_cmd(CMD_FG);
    state();

    step("19. shared input: main SetForegroundWindow(A) (B -> A), then detach");
    main_fg(wa, "A");
    state();
    say("  (detach -> %d)", AttachThreadInput(p_worker, p_main, FALSE));

    PostThreadMessageA(p_worker, CMD_QUIT, 0, 0);
    while (MsgWaitForMultipleObjects(1, &thread, FALSE, 5000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) pump_for(10);
    if (x11_dir[0]) x11_step("quit");
    DestroyWindow(wa);
    plain("");
    plain("done");
    return 0;
}
