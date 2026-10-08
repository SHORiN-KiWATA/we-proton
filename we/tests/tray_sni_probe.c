/* What does the owner of a notification icon get when the icon is shown by a
 * StatusNotifierItem host instead of an XEmbed system tray?
 *
 * The program adds an icon, changes its tip, shows a balloon and then prints
 * every callback message with the coordinates it carries and the cursor
 * position, until it gets a middle click; then it deletes the icon and exits.
 * sni_host.py plays the panel and does the clicking.
 *
 *   tray_sni_probe.exe        NOTIFYICON_VERSION_4, an icon with alpha
 *   tray_sni_probe.exe v0     the old callback format, an icon with a mask only
 *
 * The icon is 32x32: the left half is red, the right top quarter green (50%
 * transparent in the alpha icon) and the right bottom quarter transparent.
 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#define WM_TRAY (WM_APP + 1)
#define ICON_ID 7

static BOOL v4 = TRUE;
static NOTIFYICONDATAW nid;

static const char *msg_name(UINT msg)
{
    static char buf[16];
    switch (msg)
    {
    case WM_MOUSEMOVE: return "WM_MOUSEMOVE";
    case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
    case WM_LBUTTONUP: return "WM_LBUTTONUP";
    case WM_LBUTTONDBLCLK: return "WM_LBUTTONDBLCLK";
    case WM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
    case WM_RBUTTONUP: return "WM_RBUTTONUP";
    case WM_RBUTTONDBLCLK: return "WM_RBUTTONDBLCLK";
    case WM_MBUTTONDOWN: return "WM_MBUTTONDOWN";
    case WM_MBUTTONUP: return "WM_MBUTTONUP";
    case WM_CONTEXTMENU: return "WM_CONTEXTMENU";
    case NIN_SELECT: return "NIN_SELECT";
    case NIN_KEYSELECT: return "NIN_KEYSELECT";
    case NIN_BALLOONSHOW: return "NIN_BALLOONSHOW";
    case NIN_BALLOONHIDE: return "NIN_BALLOONHIDE";
    case NIN_BALLOONTIMEOUT: return "NIN_BALLOONTIMEOUT";
    case NIN_BALLOONUSERCLICK: return "NIN_BALLOONUSERCLICK";
    case NIN_POPUPOPEN: return "NIN_POPUPOPEN";
    case NIN_POPUPCLOSE: return "NIN_POPUPCLOSE";
    }
    snprintf(buf, sizeof(buf), "%#x", msg);
    return buf;
}

static HICON make_icon(BOOL alpha)
{
    BITMAPINFO info = {{sizeof(info.bmiHeader), 32, -32, 1, 32, BI_RGB}};
    BYTE mask_bits[32 * 4];
    ICONINFO ii = {TRUE};
    DWORD *bits;
    HICON icon;
    int x, y;

    ii.hbmColor = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    memset(mask_bits, 0, sizeof(mask_bits));
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++)
        {
            DWORD *p = &bits[y * 32 + x];
            if (x < 16) *p = 0xffff0000;
            else if (y < 16) *p = alpha ? 0x8000ff00 : 0x0000ff00;  /* icons are not premultiplied */
            else
            {
                *p = 0;
                mask_bits[y * 4 + x / 8] |= 0x80 >> (x % 8);
            }
            if (!alpha) *p &= 0x00ffffff;
        }
    ii.hbmMask = CreateBitmap(32, 32, 1, 1, mask_bits);
    icon = CreateIconIndirect(&ii);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    return icon;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_TRAY)
    {
        UINT event, id;
        POINT cursor;
        char at[32] = "";

        GetCursorPos(&cursor);
        if (v4)
        {
            event = LOWORD(lparam);
            id = HIWORD(lparam);
            snprintf(at, sizeof(at), " at (%d,%d)", (short)LOWORD(wparam), (short)HIWORD(wparam));
        }
        else
        {
            event = lparam;
            id = wparam;
        }
        printf("callback %s id %u%s, cursor (%ld,%ld)\n", msg_name(event), id, at, cursor.x, cursor.y);

        if (event == WM_MBUTTONUP)
        {
            printf("delete %d\n", Shell_NotifyIconW(NIM_DELETE, &nid));
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int main(int argc, char **argv)
{
    WNDCLASSW cls = {0};
    MSG msg;
    HWND hwnd;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "v0")) v4 = FALSE;

    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"tray_sni_probe";
    RegisterClassW(&cls);
    hwnd = CreateWindowW(L"tray_sni_probe", L"tray_sni_probe", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                         NULL, NULL, cls.hInstance, NULL);

    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = ICON_ID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = make_icon(v4);
    wcscpy(nid.szTip, L"probe tip 1");
    printf("add %d\n", Shell_NotifyIconW(NIM_ADD, &nid));
    if (v4)
    {
        nid.uVersion = NOTIFYICON_VERSION_4;
        printf("set version %d\n", Shell_NotifyIconW(NIM_SETVERSION, &nid));
    }

    Sleep(1000);
    nid.uFlags = NIF_TIP;
    wcscpy(nid.szTip, L"probe tip 2 \x4e2d\x6587");
    printf("modify tip %d\n", Shell_NotifyIconW(NIM_MODIFY, &nid));
    nid.uFlags = NIF_INFO;
    wcscpy(nid.szInfoTitle, L"balloon title");
    wcscpy(nid.szInfo, L"balloon text");
    nid.uTimeout = 5000;
    nid.dwInfoFlags = NIIF_INFO;
    printf("balloon %d\n", Shell_NotifyIconW(NIM_MODIFY, &nid));

    SetTimer(hwnd, 1, 30000, NULL);
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_TIMER)
        {
            printf("timed out\n");
            Shell_NotifyIconW(NIM_DELETE, &nid);
            break;
        }
        DispatchMessageW(&msg);
    }
    return 0;
}
