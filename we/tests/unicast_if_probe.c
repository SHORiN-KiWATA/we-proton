/*
 * Probe what IP_UNICAST_IF does to a TCP socket: after setting the option,
 * which source address does the stack pick, and does the connection use the
 * chosen interface. Run it where a tunnel interface sits in front of the
 * default route: without a working option the source address comes from the
 * tunnel interface, with it from the chosen one.
 *
 * Usage: unicast_if_probe.exe [interface index] [destination]
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ws2ipdef.h>

static void print_source( SOCKET s, const char *what )
{
    struct sockaddr_in src;
    int len = sizeof(src);
    char buf[64] = "-";

    memset( &src, 0, sizeof(src) );
    if (getsockname( s, (struct sockaddr *)&src, &len ))
        printf( "%s: getsockname failed err=%lu\n", what, WSAGetLastError() );
    else
    {
        inet_ntop( AF_INET, &src.sin_addr, buf, sizeof(buf) );
        printf( "%s: source=%s port=%u\n", what, buf, ntohs( src.sin_port ) );
    }
}

int main( int argc, char **argv )
{
    WSADATA wsadata;
    IP_ADAPTER_ADDRESSES *addrs, *a;
    ULONG size = 0;
    DWORD index = 0;
    SOCKET s;
    struct sockaddr_in dst;
    u_long nonblock = 1;

    WSAStartup( MAKEWORD( 2, 2 ), &wsadata );

    GetAdaptersAddresses( AF_INET, 0, NULL, NULL, &size );
    if (!(addrs = malloc( size )) ||
        GetAdaptersAddresses( AF_INET, 0, NULL, addrs, &size ))
    {
        printf( "adapters: enumeration failed\n" );
        return 1;
    }

    for (a = addrs; a; a = a->Next)
    {
        char buf[64] = "-", name[128] = "";
        if (a->FirstUnicastAddress && a->FirstUnicastAddress->Address.lpSockaddr->sa_family == AF_INET)
            inet_ntop( AF_INET, &((struct sockaddr_in *)a->FirstUnicastAddress->Address.lpSockaddr)->sin_addr,
                       buf, sizeof(buf) );
        WideCharToMultiByte( CP_UTF8, 0, a->FriendlyName, -1, name, sizeof(name), NULL, NULL );
        printf( "adapter %lu \"%s\" %s gw=%d\n", (unsigned long)a->IfIndex, name, buf,
                a->FirstGatewayAddress != NULL );
    }

    if (argc > 1) index = atoi( argv[1] );
    else for (a = addrs; a; a = a->Next)
        if (a->FirstGatewayAddress && a->FirstUnicastAddress) { index = a->IfIndex; break; }
    printf( "chosen index=%lu\n", (unsigned long)index );

    memset( &dst, 0, sizeof(dst) );
    dst.sin_family = AF_INET;
    dst.sin_port = htons( 443 );
    inet_pton( AF_INET, argc > 2 ? argv[2] : "8.8.8.8", &dst.sin_addr );

    s = socket( AF_INET, SOCK_STREAM, 0 );
    ioctlsocket( s, FIONBIO, &nonblock );
    if (connect( s, (struct sockaddr *)&dst, sizeof(dst) ) == SOCKET_ERROR)
        printf( "tcp baseline connect err=%lu\n", WSAGetLastError() );
    print_source( s, "tcp baseline" );
    closesocket( s );

    {
        unsigned long value = htonl( index );
        unsigned long got = 0;
        int len = sizeof(got);

        s = socket( AF_INET, SOCK_STREAM, 0 );
        if (setsockopt( s, IPPROTO_IP, IP_UNICAST_IF, (char *)&value, sizeof(value) ))
            printf( "setsockopt(IP_UNICAST_IF, %lu) failed err=%lu\n", value, WSAGetLastError() );
        else
            printf( "setsockopt(IP_UNICAST_IF, %lu) ok\n", value );

        if (!getsockopt( s, IPPROTO_IP, IP_UNICAST_IF, (char *)&got, &len ))
            printf( "getsockopt(IP_UNICAST_IF) -> %lu (index %lu)\n", got, ntohl( got ) );
        else
            printf( "getsockopt(IP_UNICAST_IF) failed err=%lu\n", WSAGetLastError() );

        ioctlsocket( s, FIONBIO, &nonblock );
        if (connect( s, (struct sockaddr *)&dst, sizeof(dst) ) == SOCKET_ERROR)
            printf( "tcp with option connect err=%lu\n", WSAGetLastError() );
        print_source( s, "tcp with option" );
        closesocket( s );
    }

    return 0;
}
