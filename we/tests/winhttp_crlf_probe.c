/* WinHttpAddRequestHeaders with header blocks that start with an empty line, or contain
 * empty lines between headers. Fresh request handle per case, nothing is sent. */
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
        else putchar(s[i] < 0x80 ? s[i] : '?');
    }
}

int main(void)
{
    static const WCHAR *headers[] =
    {
        L"\r\nX-A:one\r\nX-B:k1=v1&k2=v2",
        L"\r\nX-A:one\r\nX-B:k1=v1&k2=v2\r\n",
        L"\r\nX-A: one\r\nX-B: two",
        L"\r\n\r\nX-A:one",
        L"\nX-A:one",
        L"\rX-A:one",
        L"X-A:one\r\n\r\nX-B:two",
        L"X-A:one\r\n\r\n",
        L"\r\n",
        L"",
        L" \r\nX-A:one",
        L"X-A:one\r\nX-B:two",
        L"X-A:one",
    };
    static const DWORD flags[] = {WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE, WINHTTP_ADDREQ_FLAG_ADD, 0};
    HINTERNET ses, con, req;
    unsigned int h, f, l;

    ses = WinHttpOpen(L"probe", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    con = WinHttpConnect(ses, L"example.invalid", 443, 0);
    for (h = 0; h < ARRAYSIZE(headers); h++)
    for (f = 0; f < ARRAYSIZE(flags); f++)
    for (l = 0; l < 2; l++)
    {
        WCHAR out[2048];
        DWORD outlen = sizeof(out), len = l ? wcslen(headers[h]) : (DWORD)-1, err;
        BOOL ret;

        req = WinHttpOpenRequest(con, L"GET", L"/q", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
        SetLastError(0xdeadbeef);
        ret = WinHttpAddRequestHeaders(req, headers[h], len, flags[f]);
        err = GetLastError();
        printf("h%02u ", h);
        show(headers[h], 256);
        printf(" len %s flags %#lx -> %d err %lu | ", l ? "exact" : "-1", flags[f], ret, ret ? 0 : err);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF | WINHTTP_QUERY_FLAG_REQUEST_HEADERS,
                WINHTTP_HEADER_NAME_BY_INDEX, out, &outlen, WINHTTP_NO_HEADER_INDEX))
        {
            WCHAR *p = wcsstr(out, L"\r\n");
            if (p) { p += 2; show(p, outlen / sizeof(WCHAR) - (p - out)); }
        }
        printf("\n");
        WinHttpCloseHandle(req);
    }
    return 0;
}
