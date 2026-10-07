/* Edge cases for datagram connect(): unspecified address with a port, IPv6,
 * and poll flags across connect / disconnect / failed connect. */
#include <winsock2.h>
#include <ws2tcpip.h>
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

static void afd(SOCKET s, const char *what)
{
    struct afd_poll_params in = {0}, out = {0}; IO_STATUS_BLOCK io = {0};
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    in.timeout = 0; in.count = 1; in.sockets[0].socket = s; in.sockets[0].flags = ~0;
    NTSTATUS st = NtDeviceIoControlFile((HANDLE)s, ev, NULL, NULL, &io, IOCTL_AFD_POLL, &in, sizeof(in), &out, sizeof(out));
    if (st == STATUS_PENDING) { WaitForSingleObject(ev, 1000); st = io.Status; }
    printf("    %-34s poll flags %#x status %#x\n", what, out.count ? out.sockets[0].flags : 0, out.count ? out.sockets[0].status : 0);
    CloseHandle(ev);
}

static int con(SOCKET s, const void *a, int len) { return connect(s, a, len) ? WSAGetLastError() : 0; }

int main(void)
{
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    struct sockaddr_in z = { AF_INET }, z80 = { AF_INET }, lo = { AF_INET };
    z80.sin_port = htons(80);
    lo.sin_addr.s_addr = htonl(INADDR_LOOPBACK); lo.sin_port = htons(9);
    struct sockaddr_in6 z6 = { AF_INET6 }, lo6 = { AF_INET6 };
    lo6.sin6_addr.s6_addr[15] = 1; lo6.sin6_port = htons(9);
    printf("udp_connect_err_probe2, wine %s\n", GetProcAddress(GetModuleHandleA("ntdll"), "wine_get_version") ? "yes" : "no");

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    printf("A fresh: connect 0.0.0.0:80 -> %d\n", con(s, &z80, sizeof(z80))); afd(s, "after 0.0.0.0:80");
    closesocket(s);

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    printf("B connect lo -> %d\n", con(s, &lo, sizeof(lo))); afd(s, "connected");
    printf("B connect 0.0.0.0:0 -> %d\n", con(s, &z, sizeof(z))); afd(s, "after disconnect");
    printf("B connect 0.0.0.0:0 -> %d\n", con(s, &z, sizeof(z))); afd(s, "after failed connect (was connected)");
    printf("B connect 0.0.0.0:80 -> %d\n", con(s, &z80, sizeof(z80))); afd(s, "after 0.0.0.0:80");
    printf("B connect lo -> %d\n", con(s, &lo, sizeof(lo))); afd(s, "reconnected");
    closesocket(s);

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    printf("C fail 0.0.0.0:0 -> %d\n", con(s, &z, sizeof(z))); afd(s, "after failed connect");
    printf("C connect lo -> %d\n", con(s, &lo, sizeof(lo))); afd(s, "after good connect");
    printf("C connect 0.0.0.0:0 -> %d\n", con(s, &z, sizeof(z))); afd(s, "after disconnect");
    closesocket(s);

    s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    printf("D v6 fresh: connect [::]:0 -> %d\n", con(s, &z6, sizeof(z6))); afd(s, "v6 after failed connect");
    printf("D v6 connect [::1] -> %d\n", con(s, &lo6, sizeof(lo6))); afd(s, "v6 connected");
    printf("D v6 connect [::]:0 -> %d\n", con(s, &z6, sizeof(z6))); afd(s, "v6 after disconnect");
    closesocket(s);

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in b = { AF_INET }; b.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, (void *)&b, sizeof(b));
    printf("E bound: connect 0.0.0.0:0 -> %d\n", con(s, &z, sizeof(z))); afd(s, "bound, after failed connect");
    char buf[4]; int r = sendto(s, "x", 1, 0, (void *)&lo, sizeof(lo));
    printf("E sendto after failed connect -> %d (%d)\n", r, r < 0 ? WSAGetLastError() : 0); afd(s, "after sendto");
    closesocket(s);
    return 0;
}
