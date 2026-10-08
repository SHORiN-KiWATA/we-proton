/* WinHttpAddRequestHeaders on a request handle in different states.
 * usage: winhttp_state_probe.exe <host> <port>   (an HTTP server serving /f) */
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>

static WCHAR host[64];
static INTERNET_PORT port;

static void add(const char *what, HINTERNET req, DWORD flags)
{
    BOOL ret;
    SetLastError(0xdeadbeef);
    ret = WinHttpAddRequestHeaders(req, L"Range: bytes=10-", (DWORD)-1, flags);
    printf("  %-34s add(%#lx) -> %d err %lu\n", what, flags, ret, ret ? 0 : GetLastError());
}

static void drain(HINTERNET req)
{
    char buf[4096]; DWORD n;
    while (WinHttpReadData(req, buf, sizeof(buf), &n) && n) ;
}

static DWORD status(HINTERNET req)
{
    DWORD code = 0, size = sizeof(code);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
            &code, &size, WINHTTP_NO_HEADER_INDEX);
    return code;
}

static void sendrecv(const char *what, HINTERNET req)
{
    BOOL s, r = FALSE; DWORD e1, e2 = 0;
    s = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0); e1 = GetLastError();
    if (s) { r = WinHttpReceiveResponse(req, NULL); e2 = GetLastError(); }
    printf("  %-34s send %d (%lu) recv %d (%lu) status %lu\n", what, s, s ? 0 : e1, r, r ? 0 : e2, r ? status(req) : 0);
}

static volatile LONG async_done;
static void CALLBACK cb(HINTERNET h, DWORD_PTR ctx, DWORD st, void *info, DWORD len)
{
    if (st == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE || st == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
        InterlockedExchange(&async_done, st);
}

int main(int argc, char **argv)
{
    HINTERNET ses, con, req, bad;
    unsigned int i;
    static const DWORD fl[] = {WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE, WINHTTP_ADDREQ_FLAG_ADD};

    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, host, 64);
    port = (INTERNET_PORT)atoi(argv[2]);
    ses = WinHttpOpen(L"probe", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    con = WinHttpConnect(ses, host, port, 0);
    bad = WinHttpConnect(ses, host, (INTERNET_PORT)(port + 1), 0); /* nothing listens there */

    for (i = 0; i < ARRAYSIZE(fl); i++)
    {
        printf("flags %#lx\n", fl[i]);

        req = WinHttpOpenRequest(con, L"GET", L"/f", NULL, NULL, NULL, 0);
        add("before send", req, fl[i]);
        sendrecv("send", req);
        WinHttpCloseHandle(req);

        req = WinHttpOpenRequest(con, L"GET", L"/f", NULL, NULL, NULL, 0);
        sendrecv("first send", req);
        add("after receive, body unread", req, fl[i]);
        sendrecv("resend", req);
        WinHttpCloseHandle(req);

        req = WinHttpOpenRequest(con, L"GET", L"/f", NULL, NULL, NULL, 0);
        sendrecv("first send", req);
        drain(req);
        add("after body read", req, fl[i]);
        sendrecv("resend", req);
        WinHttpCloseHandle(req);

        req = WinHttpOpenRequest(bad, L"GET", L"/f", NULL, NULL, NULL, 0);
        sendrecv("send to closed port", req);
        add("after failed send", req, fl[i]);
        WinHttpCloseHandle(req);

        add("NULL handle", NULL, fl[i]);
        add("session handle", ses, fl[i]);
        add("connect handle", con, fl[i]);
    }

    /* async: add while the send is in flight and after it completed */
    {
        HINTERNET as = WinHttpOpen(L"probe", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, WINHTTP_FLAG_ASYNC);
        HINTERNET ac = WinHttpConnect(as, host, port, 0);
        HINTERNET ar = WinHttpOpenRequest(ac, L"GET", L"/f", NULL, NULL, NULL, 0);
        BOOL s;
        printf("async\n");
        WinHttpSetStatusCallback(ar, cb, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS, 0);
        add("before send", ar, fl[0]);
        s = WinHttpSendRequest(ar, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        printf("  %-34s send %d (%lu)\n", "async send", s, s ? 0 : GetLastError());
        add("right after async send", ar, fl[0]);
        for (i = 0; i < 300 && !async_done; i++) Sleep(10);
        printf("  %-34s status %#lx\n", "send completion", (unsigned long)async_done);
        add("after send completion", ar, fl[0]);
        WinHttpSetStatusCallback(ar, NULL, 0, 0);
        WinHttpCloseHandle(ar); WinHttpCloseHandle(ac); WinHttpCloseHandle(as);
    }
    return 0;
}
