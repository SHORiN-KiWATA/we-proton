/* Connecting a UDP socket must complete a pending IOCTL_AFD_POLL that waits
 * for AFD_POLL_CONNECT (Windows behaviour, see test_poll() in
 * dlls/ws2_32/tests/afd.c). Without it, a thread woken this way only returns
 * when the poll's timeout expires. */
#include <winsock2.h>
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

#define IOCTL_AFD_POLL   0x12024
#define AFD_POLL_CONNECT 0x0040

struct afd_poll_params
{
    LONGLONG timeout;
    unsigned int count;
    BOOLEAN exclusive;
    BOOLEAN padding[3];
    struct { SOCKET socket; int flags; int status; } sockets[1];
};

NTSTATUS NTAPI NtDeviceIoControlFile(HANDLE, HANDLE, PIO_APC_ROUTINE, void *, IO_STATUS_BLOCK *,
                                     ULONG, void *, ULONG, void *, ULONG);

int main(void)
{
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    struct sockaddr_in a = {0}; int len = sizeof(a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    SOCKET peer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bind(peer, (void *)&a, sizeof(a)); getsockname(peer, (void *)&a, &len);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);

    struct afd_poll_params in = {0}, out = {0};
    IO_STATUS_BLOCK io = {0};
    in.timeout = -1000 * 10000;            /* 1 s */
    in.count = 1;
    in.sockets[0].socket = s;
    in.sockets[0].flags = AFD_POLL_CONNECT;
    NTSTATUS st = NtDeviceIoControlFile((HANDLE)s, ev, NULL, NULL, &io, IOCTL_AFD_POLL,
                                        &in, sizeof(in), &out, sizeof(out));
    printf("poll: %#lx (expect 0x103 pending)\n", st);

    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    int ret = connect(s, (void *)&a, sizeof(a));
    DWORD w = WaitForSingleObject(ev, 2000);
    QueryPerformanceCounter(&t1);
    double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
    printf("connect %d, poll done after %.1f ms, status %#lx, flags %#x\n", ret, ms, io.Status, out.sockets[0].flags);
    int ok = st == 0x103 && !ret && !w && ms < 100 && !io.Status && out.sockets[0].flags == AFD_POLL_CONNECT;
    puts(ok ? "PASS" : "FAIL: connect() did not complete the pending AFD_POLL_CONNECT poll");
    return !ok;
}
