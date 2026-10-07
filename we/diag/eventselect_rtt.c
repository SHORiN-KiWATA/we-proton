/* Request/response over localhost. The server loop does what a typical event
 * based local server does: WSAEnumNetworkEvents, WSAEventSelect again, ResetEvent,
 * then wait on the event with a 1 s timeout. If Wine fails to signal the
 * event when a request arrives, every round trip takes ~1000 ms. */
#include <winsock2.h>
#include <stdio.h>

static SOCKET listener, server, client;
static HANDLE ev;
static volatile LONG stop;

static DWORD WINAPI server_loop(void *arg)
{
    char buf[256];
    while (!stop)
    {
        WSANETWORKEVENTS ne;
        WSAEnumNetworkEvents(server, NULL, &ne);
        WSAEventSelect(server, ev, FD_READ | FD_CLOSE);
        ResetEvent(ev);
        int n = recv(server, buf, sizeof(buf), 0);
        if (n > 0) { send(server, buf, n, 0); continue; }
        WaitForMultipleObjects(1, &ev, FALSE, 1000);
    }
    return 0;
}

int main(void)
{
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    struct sockaddr_in a = {0}; int len = sizeof(a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    bind(listener, (void *)&a, sizeof(a)); listen(listener, 1);
    getsockname(listener, (void *)&a, &len);
    client = socket(AF_INET, SOCK_STREAM, 0);
    connect(client, (void *)&a, sizeof(a));
    server = accept(listener, NULL, NULL);
    ev = WSACreateEvent();
    u_long nb = 1; ioctlsocket(server, FIONBIO, &nb);
    HANDLE th = CreateThread(NULL, 0, server_loop, NULL, 0, NULL);
    Sleep(100);

    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
    double worst = 0, total = 0; const int rounds = 10;
    for (int i = 0; i < rounds; i++)
    {
        char c = 'x', r;
        WSAPOLLFD pfd = { client, POLLRDNORM, 0 };
        QueryPerformanceCounter(&t0);
        send(client, &c, 1, 0);
        while (WSAPoll(&pfd, 1, 0) == 0) ;         /* client spins on a zero-timeout poll */
        recv(client, &r, 1, 0);
        QueryPerformanceCounter(&t1);
        double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
        total += ms; if (ms > worst) worst = ms;
        printf("round %d: %.2f ms\n", i, ms);
        Sleep(50);
    }
    printf("avg %.2f ms, worst %.2f ms -> %s\n", total / rounds, worst, worst > 500 ? "FAIL: missed wakeups" : "PASS");
    stop = 1; WaitForSingleObject(th, 2000);
    return worst > 500;
}
