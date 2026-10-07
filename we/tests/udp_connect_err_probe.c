/* Follow-up to udp_wakeup_probe: is the exceptfds condition left by a failed
 * connect(udp, 0.0.0.0:0) one-shot or level-triggered, what clears it, and
 * which AFD poll flags/status does it map to? */
#include <winsock2.h>
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

#define IOCTL_AFD_POLL 0x12024
struct afd_poll_params
{
    LONGLONG timeout; unsigned int count; BOOLEAN exclusive; BOOLEAN padding[3];
    struct { SOCKET socket; int flags; int status; } sockets[1];
};
NTSTATUS NTAPI NtDeviceIoControlFile(HANDLE, HANDLE, PIO_APC_ROUTINE, void *, IO_STATUS_BLOCK *,
                                     ULONG, void *, ULONG, void *, ULONG);

static struct sockaddr_in zero, loop;

static int sel0(SOCKET s)
{
    fd_set e; struct timeval tv = { 0, 0 };
    FD_ZERO(&e); FD_SET(s, &e);
    return select(0, NULL, NULL, &e, &tv) > 0 && FD_ISSET(s, &e);
}

static double sel_ms(SOCKET s, int ms, int *hit)
{
    fd_set e; struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    FD_ZERO(&e); FD_SET(s, &e);
    *hit = select(0, NULL, NULL, &e, &tv) > 0 && FD_ISSET(s, &e);
    QueryPerformanceCounter(&b);
    return (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
}

static void afd(SOCKET s, const char *what)
{
    struct afd_poll_params in = {0}, out = {0}; IO_STATUS_BLOCK io = {0};
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    in.timeout = 0; in.count = 1; in.sockets[0].socket = s; in.sockets[0].flags = ~0;
    NTSTATUS st = NtDeviceIoControlFile((HANDLE)s, ev, NULL, NULL, &io, IOCTL_AFD_POLL, &in, sizeof(in), &out, sizeof(out));
    if (st == STATUS_PENDING) { WaitForSingleObject(ev, 1000); st = io.Status; }
    printf("  afd poll(all) %-28s st %#lx count %u flags %#x status %#x\n", what, st, out.count,
           out.count ? out.sockets[0].flags : 0, out.count ? out.sockets[0].status : 0);
    CloseHandle(ev);
}

static int c0(SOCKET s) { return connect(s, (void *)&zero, sizeof(zero)) ? WSAGetLastError() : 0; }

int main(void)
{
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    zero.sin_family = AF_INET;
    loop.sin_family = AF_INET; loop.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    SOCKET peer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bind(peer, (void *)&loop, sizeof(loop));
    int len = sizeof(loop); getsockname(peer, (void *)&loop, &len);
    printf("udp_connect_err_probe, wine %s\n", GetProcAddress(GetModuleHandleA("ntdll"), "wine_get_version") ? "yes" : "no");

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    afd(s, "fresh");
    printf("T1 connect0 -> %d\n", c0(s));
    afd(s, "after failed connect");
    printf("T1 sel0 x3: %d %d %d\n", sel0(s), sel0(s), sel0(s));
    afd(s, "after 3 selects");
    printf("T2 connect0 -> %d, then connect0 -> %d\n", c0(s), c0(s));
    printf("T2 sel0 x2: %d %d\n", sel0(s), sel0(s));
    int r = connect(s, (void *)&loop, sizeof(loop));
    printf("T3 connect(loopback) -> %d\n", r ? WSAGetLastError() : 0);
    printf("T3 sel0 x2 after good connect: %d %d\n", sel0(s), sel0(s));
    afd(s, "after good connect");
    printf("T4 connect0 (connected) -> %d, sel0 %d\n", c0(s), sel0(s));
    printf("T4 connect0 (now unconnected) -> %d, sel0 %d\n", c0(s), sel0(s));
    closesocket(s);

    /* T5: a network thread loop: connect(0), poll, connect(0), block in select */
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    for (int i = 0; i < 4; i++)
    {
        int e1 = c0(s), z = sel0(s), e2 = c0(s), hit;
        double ms = sel_ms(s, 300, &hit);
        printf("T5 loop %d: connect0 %d, sel0 %d, connect0 %d, select(300ms) hit %d after %.1f ms\n", i, e1, z, e2, hit, ms);
    }
    closesocket(s);

    /* T6: recv/getsockopt clearing? */
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    c0(s);
    int e = -1, l = sizeof(e); getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&e, &l);
    printf("T6 after failed connect, SO_ERROR %d, sel0 %d\n", e, sel0(s));
    closesocket(s);
    return 0;
}
