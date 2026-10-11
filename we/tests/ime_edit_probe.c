/* Which messages does a window get while text is typed with an input method,
 * and what text arrives?
 *
 * The program shows an edit control and prints the IME and character
 * messages that reach it, then after a while the edit control's text. An
 * outside script types into it.
 *
 *   ime_edit_probe.exe [seconds] [disable]
 *
 * With "disable" the program first calls ImmDisableIME(0), so its thread has
 * no IME window, like programs that only take input method text through TSF.
 */
#include <windows.h>
#include <imm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static WNDPROC edit_proc;
static HWND edit;

static void print_utf8(const WCHAR *str, int len)
{
    char buf[1024];
    int n = WideCharToMultiByte(CP_UTF8, 0, str, len, buf, sizeof(buf) - 1, NULL, NULL);
    buf[n] = 0;
    printf("\"%s\"", buf);
}

static void print_comp(HIMC himc, DWORD index, const char *name)
{
    WCHAR buf[256];
    LONG len = ImmGetCompositionStringW(himc, index, buf, sizeof(buf));
    if (len < 0) return;
    printf(" %s ", name);
    print_utf8(buf, len / sizeof(WCHAR));
}

static LRESULT CALLBACK edit_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    HIMC himc;

    switch (msg)
    {
    case WM_IME_STARTCOMPOSITION: printf("WM_IME_STARTCOMPOSITION\n"); break;
    case WM_IME_ENDCOMPOSITION: printf("WM_IME_ENDCOMPOSITION\n"); break;
    case WM_IME_COMPOSITION:
        printf("WM_IME_COMPOSITION %#x", (unsigned int)lparam);
        if ((himc = ImmGetContext(hwnd)))
        {
            if (lparam & GCS_COMPSTR) print_comp(himc, GCS_COMPSTR, "comp");
            if (lparam & GCS_RESULTSTR) print_comp(himc, GCS_RESULTSTR, "result");
            ImmReleaseContext(hwnd, himc);
        }
        printf("\n");
        break;
    case WM_IME_CHAR:
        printf("WM_IME_CHAR ");
        print_utf8((WCHAR *)&wparam, 1);
        printf("\n");
        break;
    case WM_CHAR:
        printf("WM_CHAR ");
        print_utf8((WCHAR *)&wparam, 1);
        printf("\n");
        break;
    case WM_IME_SETCONTEXT: printf("WM_IME_SETCONTEXT %u %#x\n", (unsigned int)wparam, (unsigned int)lparam); break;
    case WM_IME_NOTIFY: printf("WM_IME_NOTIFY %#x\n", (unsigned int)wparam); break;
    case WM_SETFOCUS: printf("WM_SETFOCUS\n"); break;
    case WM_KILLFOCUS: printf("WM_KILLFOCUS\n"); break;
    }
    return CallWindowProcW(edit_proc, hwnd, msg, wparam, lparam);
}

int main(int argc, char **argv)
{
    DWORD seconds = argc > 1 ? atoi(argv[1]) : 30;
    BOOL disable = argc > 2 && !strcmp(argv[2], "disable");
    WCHAR text[256];
    HWND hwnd;
    MSG msg;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (disable) printf("ImmDisableIME(0) %d\n", ImmDisableIME(0));
    hwnd = CreateWindowW(L"STATIC", L"ime_edit_probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 400, 120,
                         NULL, NULL, NULL, NULL);
    edit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 10, 360, 30, hwnd, NULL, NULL, NULL);
    edit_proc = (WNDPROC)SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)edit_wndproc);
    SetForegroundWindow(hwnd);
    SetFocus(edit);
    printf("default IME window %s\n", ImmGetDefaultIMEWnd(edit) ? "present" : "none");
    printf("ready\n");

    SetTimer(NULL, 0, seconds * 1000, NULL);
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_TIMER && !msg.hwnd) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GetWindowTextW(edit, text, ARRAYSIZE(text));
    printf("text ");
    print_utf8(text, -1);
    printf("\n");
    return 0;
}
