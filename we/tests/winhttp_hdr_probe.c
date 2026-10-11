/* WinHttpAddRequestHeaders with Range-style headers, various lengths and flags.
 * Every case uses a fresh request handle (nothing is sent). Prints the result,
 * the last error and the request headers afterwards. */
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <wchar.h>

static void show(const WCHAR *s, DWORD n)
{
    DWORD i;
    for (i = 0; i < n && s[i]; i++)
    {
        if (s[i] == '\r') printf("\\r");
        else if (s[i] == '\n') printf("\\n");
        else if (s[i] < 0x80) putchar(s[i]);
        else printf("\\u%04x", s[i]);
    }
}

int main(void)
{
    static const WCHAR *headers[] =
    {
        L"Range: bytes=0-",
        L"Range: bytes=0-\r\n",
        L"Range: bytes=1024-",
        L"Range:bytes=0-",
        L"range: bytes=0-",
        L"Range: bytes=0-\r\n\r\n",
        L"Range: ",
        L"Range:",
        L"Range: bytes=0-0",
        L"Range: bytes=0-\r\nIf-Range: \"abc\"",
        L"If-Range: Mon, 01 Jan 2024 00:00:00 GMT",
        L" Range: bytes=0-",
        L"Range : bytes=0-",
        L"Range: bytes=0-\n",
    };
    static const struct { DWORD f; const char *n; } flags[] =
    {
        {0, "0"},
        {WINHTTP_ADDREQ_FLAG_ADD, "ADD"},
        {WINHTTP_ADDREQ_FLAG_ADD_IF_NEW, "ADD_IF_NEW"},
        {WINHTTP_ADDREQ_FLAG_REPLACE, "REPLACE"},
        {WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE, "ADD|REPLACE"},
        {WINHTTP_ADDREQ_FLAG_COALESCE, "COALESCE"},
        {WINHTTP_ADDREQ_FLAG_COALESCE_WITH_SEMICOLON, "COALESCE_SEMI"},
    };
    static const char *lens[] = {"-1", "exact", "exact+1", "bytes", "0"};
    HINTERNET ses, con, req;
    unsigned int h, f, l, pre;

    ses = WinHttpOpen(L"probe", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    con = WinHttpConnect(ses, L"example.invalid", 443, 0);
    for (pre = 0; pre < 2; pre++)
    for (h = 0; h < ARRAYSIZE(headers); h++)
    for (l = 0; l < ARRAYSIZE(lens); l++)
    for (f = 0; f < ARRAYSIZE(flags); f++)
    {
        WCHAR buf[256] = {0}, out[2048];
        DWORD len, outlen = sizeof(out), err;
        BOOL ret;

        req = WinHttpOpenRequest(con, L"GET", L"/identity/queryTest?query_game_id=1", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
        if (pre)
            WinHttpAddRequestHeaders(req, L"Range: bytes=5-", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        wcscpy(buf, headers[h]);
        switch (l)
        {
            case 0: len = (DWORD)-1; break;
            case 1: len = wcslen(buf); break;
            case 2: len = wcslen(buf) + 1; break;
            case 3: len = wcslen(buf) * sizeof(WCHAR); break;  /* rest of buf is zero */
            default: len = 0; break;
        }
        SetLastError(0xdeadbeef);
        ret = WinHttpAddRequestHeaders(req, buf, len, flags[f].f);
        err = GetLastError();
        printf("pre%u h%02u len %-7s %-13s -> %d err %lu | ", pre, h, lens[l], flags[f].n, ret, ret ? 0 : err);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF | WINHTTP_QUERY_FLAG_REQUEST_HEADERS,
                WINHTTP_HEADER_NAME_BY_INDEX, out, &outlen, WINHTTP_NO_HEADER_INDEX))
        {
            /* drop the request line */
            WCHAR *p = wcsstr(out, L"\r\n");
            if (p) { p += 2; show(p, outlen / sizeof(WCHAR) - (p - out)); }
        }
        else printf("(query err %lu)", GetLastError());
        printf("\n");
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return 0;
}
