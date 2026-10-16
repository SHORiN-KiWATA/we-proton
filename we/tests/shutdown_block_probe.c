/* For 0060: ShutdownBlockReasonCreate/Query/Destroy on own, foreign and bad windows. */
#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *create_fn)(HWND, LPCWSTR);
typedef BOOL (WINAPI *query_fn)(HWND, LPWSTR, DWORD *);
typedef BOOL (WINAPI *destroy_fn)(HWND);
static create_fn pCreate;
static query_fn pQuery;
static destroy_fn pDestroy;

static void create(const char *what, HWND hwnd, const WCHAR *reason)
{
    BOOL ret;
    SetLastError(0xdeadbeef);
    ret = pCreate(hwnd, reason);
    printf("create  %-28s ret %d err %lu\n", what, ret, GetLastError());
}

static void destroy(const char *what, HWND hwnd)
{
    BOOL ret;
    SetLastError(0xdeadbeef);
    ret = pDestroy(hwnd);
    printf("destroy %-28s ret %d err %lu\n", what, ret, GetLastError());
}

static void query(const char *what, HWND hwnd, BOOL with_buf, DWORD size)
{
    WCHAR buf[64];
    DWORD len = size;
    BOOL ret;
    memset(buf, 0, sizeof(buf));
    SetLastError(0xdeadbeef);
    ret = pQuery(hwnd, with_buf ? buf : NULL, &len);
    printf("query   %-28s ret %d err %lu len %lu text \"%ls\"\n", what, ret, GetLastError(), len, buf);
}

static DWORD WINAPI thread_proc(void *arg)
{
    HWND hwnd = arg;
    create("other thread, main window", hwnd, L"from thread");
    query("other thread, main window", hwnd, TRUE, 64);
    destroy("other thread, main window", hwnd);
    return 0;
}

int main(int argc, char **argv)
{
    HMODULE user32 = GetModuleHandleA("user32.dll");
    HWND hwnd, child, desktop = GetDesktopWindow();
    HANDLE thread;

    pCreate = (create_fn)GetProcAddress(user32, "ShutdownBlockReasonCreate");
    pQuery = (query_fn)GetProcAddress(user32, "ShutdownBlockReasonQuery");
    pDestroy = (destroy_fn)GetProcAddress(user32, "ShutdownBlockReasonDestroy");
    printf("exports create %d query %d destroy %d\n", !!pCreate, !!pQuery, !!pDestroy);
    if (!pCreate || !pQuery || !pDestroy) return 1;

    hwnd = CreateWindowExW(0, L"static", L"probe", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0);
    child = CreateWindowExW(0, L"static", L"child", WS_CHILD, 0, 0, 10, 10, hwnd, 0, 0, 0);

    query("own, before create", hwnd, TRUE, 64);
    destroy("own, before create", hwnd);
    create("own", hwnd, L"Please wait");
    query("own", hwnd, TRUE, 64);
    query("own, NULL buffer", hwnd, FALSE, 0);
    query("own, size 5", hwnd, TRUE, 5);
    query("own, size 12 (exact)", hwnd, TRUE, 12);
    create("own, again", hwnd, L"Second reason");
    query("own, after second create", hwnd, TRUE, 64);
    create("own, NULL reason", hwnd, NULL);
    query("own, after NULL reason", hwnd, TRUE, 64);
    create("own, empty reason", hwnd, L"");
    query("own, after empty reason", hwnd, TRUE, 64);
    destroy("own", hwnd);
    query("own, after destroy", hwnd, TRUE, 64);
    destroy("own, twice", hwnd);

    create("child", child, L"child reason");
    query("child", child, TRUE, 64);
    destroy("child", child);

    create("NULL hwnd", NULL, L"x");
    query("NULL hwnd", NULL, TRUE, 64);
    destroy("NULL hwnd", NULL);
    create("bad hwnd", (HWND)0xdead0, L"x");
    query("bad hwnd", (HWND)0xdead0, TRUE, 64);
    destroy("bad hwnd", (HWND)0xdead0);
    create("desktop", desktop, L"x");
    query("desktop", desktop, TRUE, 64);
    destroy("desktop", desktop);

    thread = CreateThread(NULL, 0, thread_proc, hwnd, 0, NULL);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    create("own, before DestroyWindow", hwnd, L"gone");
    DestroyWindow(hwnd);
    query("destroyed window", hwnd, TRUE, 64);
    return 0;
}
