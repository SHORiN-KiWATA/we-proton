/* Does a pending quit (PostQuitMessage) get past message filters?
 *
 * For each case: PostQuitMessage(), make sure a matching message for the
 * filter is also available where the case needs one (a 1 ms timer on the
 * window), then call GetMessage/PeekMessage with the filter and print what
 * came back. Afterwards the quit state is cleared with an unfiltered
 * PeekMessage(PM_REMOVE), so the cases do not affect each other.
 *
 *   quit_filter_probe.exe
 */
#include <windows.h>
#include <stdio.h>

static HWND hwnd, other_hwnd;

static const char *name(UINT msg)
{
    static char buf[32];
    switch (msg)
    {
    case WM_QUIT: return "WM_QUIT";
    case WM_TIMER: return "WM_TIMER";
    case WM_USER: return "WM_USER";
    case WM_NULL: return "WM_NULL";
    }
    sprintf(buf, "0x%04x", msg);
    return buf;
}

static void clear(void)
{
    MSG msg;
    KillTimer(hwnd, 1);
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) ;
}

static DWORD WINAPI thread_proc(void *arg)
{
    MSG msg;
    other_hwnd = CreateWindowW(L"static", L"other", WS_POPUP, 0, 0, 1, 1, NULL, NULL, NULL, NULL);
    SetEvent(arg);
    while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    return 0;
}

static void get_case(const char *label, HWND filter, UINT first, UINT last, BOOL timer)
{
    MSG msg = {0};
    BOOL ret;
    DWORD start;

    clear();
    PostQuitMessage(0x55);
    if (timer) SetTimer(hwnd, 1, 1, NULL);
    start = GetTickCount();
    ret = GetMessageW(&msg, filter, first, last);
    printf("GetMessage  %-40s ret=%d msg=%-8s hwnd=%s after %lu ms\n", label, ret, name(msg.message),
           msg.hwnd == hwnd ? "hwnd" : msg.hwnd ? "other" : "NULL", GetTickCount() - start);
    clear();
}

static void peek_case(const char *label, HWND filter, UINT first, UINT last, BOOL timer)
{
    MSG msg = {0};
    BOOL ret;

    clear();
    PostQuitMessage(0x55);
    if (timer) { SetTimer(hwnd, 1, 1, NULL); Sleep(20); }
    ret = PeekMessageW(&msg, filter, first, last, PM_REMOVE);
    printf("PeekMessage %-40s ret=%d msg=%s\n", label, ret, ret ? name(msg.message) : "-");
    clear();
}

int main(void)
{
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);

    setvbuf(stdout, NULL, _IONBF, 0);
    hwnd = CreateWindowW(L"static", L"probe", WS_POPUP, 0, 0, 1, 1, NULL, NULL, NULL, NULL);
    CloseHandle(CreateThread(NULL, 0, thread_proc, event, 0, NULL));
    WaitForSingleObject(event, INFINITE);

    get_case("filter NULL, timer pending", NULL, 0, 0, TRUE);
    get_case("filter hwnd, timer pending", hwnd, 0, 0, TRUE);
    get_case("filter hwnd, range WM_TIMER, timer pending", hwnd, WM_TIMER, WM_TIMER, TRUE);
    get_case("filter NULL, range WM_TIMER, timer pending", NULL, WM_TIMER, WM_TIMER, TRUE);
    peek_case("filter (HWND)-1, timer pending", (HWND)-1, 0, 0, TRUE);
    peek_case("filter other thread's hwnd", other_hwnd, 0, 0, FALSE);

    peek_case("filter NULL", NULL, 0, 0, FALSE);
    peek_case("filter hwnd", hwnd, 0, 0, FALSE);
    peek_case("filter hwnd, timer pending", hwnd, 0, 0, TRUE);
    peek_case("filter NULL, range WM_USER", NULL, WM_USER, WM_USER, FALSE);
    peek_case("filter hwnd, range WM_USER", hwnd, WM_USER, WM_USER, FALSE);
    peek_case("filter (HWND)-1", (HWND)-1, 0, 0, FALSE);

    /* WM_QUIT posted as a thread message (hwnd NULL), the way ATL ends a server,
     * is a posted message rather than the quit state of PostQuitMessage */
    {
        static const struct { const char *label; HWND filter; UINT first, last; BOOL get; } cases[] =
        {
            {"posted WM_QUIT, filter NULL", NULL, 0, 0, FALSE},
            {"posted WM_QUIT, filter hwnd", (HWND)1, 0, 0, FALSE},
            {"posted WM_QUIT, filter hwnd, timer pending", (HWND)1, 0, 0, TRUE},
            {"posted WM_QUIT, filter NULL, range WM_USER", NULL, WM_USER, WM_USER, FALSE},
            {"posted WM_QUIT, filter (HWND)-1", (HWND)-1, 0, 0, FALSE},
        };
        unsigned int i;

        for (i = 0; i < ARRAYSIZE(cases); i++)
        {
            HWND filter = cases[i].filter == (HWND)1 ? hwnd : cases[i].filter;
            MSG msg = {0};
            BOOL ret;

            clear();
            PostThreadMessageW(GetCurrentThreadId(), WM_QUIT, 0x77, 0);
            if (cases[i].get)
            {
                SetTimer(hwnd, 1, 1, NULL);
                ret = GetMessageW(&msg, filter, cases[i].first, cases[i].last);
                printf("GetMessage  %-40s ret=%d msg=%s\n", cases[i].label, ret, name(msg.message));
            }
            else
            {
                ret = PeekMessageW(&msg, filter, cases[i].first, cases[i].last, PM_REMOVE);
                printf("PeekMessage %-40s ret=%d msg=%s\n", cases[i].label, ret, ret ? name(msg.message) : "-");
            }
            clear();
        }
    }

    /* is the quit still pending after a filtered call that did not return it? */
    clear();
    PostQuitMessage(0x55);
    {
        MSG msg = {0};
        BOOL ret;
        PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE);
        ret = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
        printf("after a PeekMessage filtered on hwnd, unfiltered PeekMessage ret=%d msg=%s\n",
               ret, ret ? name(msg.message) : "-");
    }
    clear();

    DestroyWindow(hwnd);
    return 0;
}
