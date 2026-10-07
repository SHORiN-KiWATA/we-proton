/* What does connect(udp, 0.0.0.0:0) do on Windows, and does it wake a
 * select() that has the socket in exceptfds? Some applications wake a network
 * thread this way. Run on Windows and under Wine and compare the output. */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

static struct sockaddr_in zero, loop;
static SOCKET peer;

static int last_err(int ret) { return ret ? WSAGetLastError() : 0; }

static int so_error(SOCKET s)
{
    int e = -1, l = sizeof(e);
    getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&e, &l);
    return e;
}

static SOCKET make(const char *state)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!strcmp(state, "bound") || !strcmp(state, "connected"))
    {
        struct sockaddr_in a = loop; a.sin_port = 0;
        bind(s, (void *)&a, sizeof(a));
    }
    if (!strcmp(state, "connected")) connect(s, (void *)&loop, sizeof(loop));
    return s;
}

static void plain(const char *state)
{
    SOCKET s = make(state);
    int r1 = connect(s, (void *)&zero, sizeof(zero)), e1 = last_err(r1), so1 = so_error(s);
    int r2 = connect(s, (void *)&zero, sizeof(zero)), e2 = last_err(r2), so2 = so_error(s);
    printf("[%-9s] connect(0.0.0.0:0) #1 ret %d err %d SO_ERROR %d | #2 ret %d err %d SO_ERROR %d\n",
           state, r1, e1, so1, r2, e2, so2);
    closesocket(s);
}

struct waiter { SOCKET s; int sets; int ret; int in_r, in_w, in_e; double ms; };

static DWORD WINAPI wait_thread(void *arg)
{
    struct waiter *w = arg;
    fd_set r, wr, e; FD_ZERO(&r); FD_ZERO(&wr); FD_ZERO(&e);
    if (w->sets & 1) FD_SET(w->s, &r);
    if (w->sets & 2) FD_SET(w->s, &wr);
    if (w->sets & 4) FD_SET(w->s, &e);
    struct timeval tv = { 1, 0 };
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    w->ret = select(0, (w->sets & 1) ? &r : NULL, (w->sets & 2) ? &wr : NULL, (w->sets & 4) ? &e : NULL, &tv);
    QueryPerformanceCounter(&t1);
    w->ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
    w->in_r = FD_ISSET(w->s, &r); w->in_w = FD_ISSET(w->s, &wr); w->in_e = FD_ISSET(w->s, &e);
    return 0;
}

/* twice: like a network thread that itself calls connect(0) twice around
 * a zero-timeout select before blocking */
static void wake(const char *state, int sets, int twice, const char *wakeop)
{
    SOCKET s = make(state);
    if (twice)
    {
        fd_set e; struct timeval tv0 = { 0, 0 };
        connect(s, (void *)&zero, sizeof(zero));
        FD_ZERO(&e); FD_SET(s, &e); select(0, NULL, NULL, &e, &tv0);
        connect(s, (void *)&zero, sizeof(zero));
    }
    struct waiter w = { s, sets };
    HANDLE th = CreateThread(NULL, 0, wait_thread, &w, 0, NULL);
    Sleep(100);
    int r = -2, e = 0;
    if (!strcmp(wakeop, "connect0")) { r = connect(s, (void *)&zero, sizeof(zero)); e = last_err(r); }
    else if (!strcmp(wakeop, "connectlo")) { r = connect(s, (void *)&loop, sizeof(loop)); e = last_err(r); }
    WaitForSingleObject(th, 3000);
    printf("[%-9s sets %c%c%c%s] wake by %-9s ret %d err %d -> select ret %d after %6.1f ms, r%d w%d e%d, SO_ERROR %d\n",
           state, (sets & 1) ? 'R' : '-', (sets & 2) ? 'W' : '-', (sets & 4) ? 'E' : '-', twice ? " twice" : "",
           wakeop, r, e, w.ret, w.ms, !!w.in_r, !!w.in_w, !!w.in_e, so_error(s));
    closesocket(s);
}

int main(void)
{
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    zero.sin_family = AF_INET;
    loop.sin_family = AF_INET; loop.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    peer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bind(peer, (void *)&loop, sizeof(loop));
    int len = sizeof(loop); getsockname(peer, (void *)&loop, &len);

    OSVERSIONINFOA v = { sizeof(v) }; GetVersionExA(&v);
    printf("udp_wakeup_probe on %lu.%lu.%lu, wine %s\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber,
           GetProcAddress(GetModuleHandleA("ntdll"), "wine_get_version") ? "yes" : "no");

    const char *states[] = { "fresh", "bound", "connected" };
    for (int i = 0; i < 3; i++) plain(states[i]);
    for (int i = 0; i < 3; i++)
    {
        wake(states[i], 4, 0, "connect0");
        wake(states[i], 4, 1, "connect0");
        wake(states[i], 1 | 4, 1, "connect0");
        wake(states[i], 4, 1, "connectlo");
        wake(states[i], 4, 1, "none");
    }
    return 0;
}
