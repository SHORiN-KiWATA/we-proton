/*
 * Probe the wintun.dll next to this program: API results, error codes and
 * session behaviour. Run it on Windows and under Wine, then compare the two
 * outputs line by line.
 *
 * On Windows, copy the wintun.dll to inspect next to the .exe.
 * Under Wine, select the builtin with WINEDLLOVERRIDES=wintun=b.
 *
 * With "loopback" as the first argument, after starting the session the probe
 * writes a "probe-ready" file and waits for a "go" file, so that the caller
 * can configure the adapter, then sends an ICMP echo request into the adapter
 * and checks that the echo reply comes back out of it.
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
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

typedef void *WINTUN_ADAPTER_HANDLE;
typedef void *WINTUN_SESSION_HANDLE;
typedef void (WINAPI *WINTUN_LOGGER_CALLBACK)( int level, ULONG64 timestamp, const WCHAR *message );

static WINTUN_ADAPTER_HANDLE (WINAPI *pCreateAdapter)( const WCHAR *, const WCHAR *, const GUID * );
static WINTUN_ADAPTER_HANDLE (WINAPI *pOpenAdapter)( const WCHAR * );
static void (WINAPI *pCloseAdapter)( WINTUN_ADAPTER_HANDLE );
static BOOL (WINAPI *pDeleteDriver)( void );
static void (WINAPI *pGetAdapterLUID)( WINTUN_ADAPTER_HANDLE, LUID * );
static DWORD (WINAPI *pGetRunningDriverVersion)( void );
static void (WINAPI *pSetLogger)( WINTUN_LOGGER_CALLBACK );
static WINTUN_SESSION_HANDLE (WINAPI *pStartSession)( WINTUN_ADAPTER_HANDLE, DWORD );
static void (WINAPI *pEndSession)( WINTUN_SESSION_HANDLE );
static HANDLE (WINAPI *pGetReadWaitEvent)( WINTUN_SESSION_HANDLE );
static BYTE *(WINAPI *pReceivePacket)( WINTUN_SESSION_HANDLE, DWORD * );
static void (WINAPI *pReleaseReceivePacket)( WINTUN_SESSION_HANDLE, const BYTE * );
static BYTE *(WINAPI *pAllocateSendPacket)( WINTUN_SESSION_HANDLE, DWORD );
static void (WINAPI *pSendPacket)( WINTUN_SESSION_HANDLE, const BYTE * );

typedef enum { ProbeEventNotification = 0, ProbeEventSynchronization = 1 } PROBE_EVENT_TYPE;
typedef struct { PROBE_EVENT_TYPE type; LONG state; } PROBE_EVENT_BASIC_INFORMATION;

static NTSTATUS (NTAPI *pNtQueryEvent)( HANDLE, int, void *, ULONG, ULONG * );

static void WINAPI logger_cb( int level, ULONG64 timestamp, const WCHAR *message )
{
    printf( "logger: level=%d timestamp=%I64u message=%ls\n", level, timestamp, message );
}

static void print_error( const char *what )
{
    printf( "%s: NULL err=%lu\n", what, GetLastError() );
}

static unsigned short ip_checksum( const void *data, int len )
{
    const unsigned char *p = data;
    unsigned long sum = 0;

    while (len > 1) { sum += (p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len) sum += p[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (unsigned short)~sum;
}

static void describe_packet( const BYTE *packet, DWORD size )
{
    if (size >= 1)
    {
        unsigned version = packet[0] >> 4;
        printf( " (ipv%u", version );
        if (version == 4 && size >= 20) printf( " proto=%u", packet[9] );
        else if (version == 6 && size >= 40) printf( " next=%u", packet[6] );
        printf( ")" );
    }
}

/* Wait for a file to appear, polling for up to timeout_ms. */
static int wait_file( const char *name, int timeout_ms )
{
    int i;

    for (i = 0; i < timeout_ms / 100; i++)
    {
        FILE *f = fopen( name, "rb" );
        if (f) { fclose( f ); return 1; }
        Sleep( 100 );
    }
    return 0;
}

int main( int argc, char **argv )
{
    int missing = 0, i, loopback = argc > 1 && !strcmp( argv[1], "loopback" );
    HMODULE lib, ntdll;
    ULONG64 version;
    WINTUN_ADAPTER_HANDLE adapter, adapter2;
    WINTUN_SESSION_HANDLE session;
    HANDLE event;
    NET_LUID luid, luid2;
    NET_IFINDEX index;
    DWORD size, err, count;
    BYTE *packet;

    SetConsoleOutputCP( CP_UTF8 );

    lib = LoadLibraryW( L"wintun.dll" );
    if (!lib)
    {
        printf( "load wintun.dll: failed err=%lu\n", GetLastError() );
        return 1;
    }

#define LOAD(f) p##f = (void *)GetProcAddress( lib, "Wintun" #f ); \
    printf( "export Wintun%s=%s\n", #f, p##f ? "yes" : "NO" ); \
    if (!p##f) missing++;

    LOAD( CreateAdapter )
    LOAD( OpenAdapter )
    LOAD( CloseAdapter )
    LOAD( DeleteDriver )
    LOAD( GetAdapterLUID )
    LOAD( GetRunningDriverVersion )
    LOAD( SetLogger )
    LOAD( StartSession )
    LOAD( EndSession )
    LOAD( GetReadWaitEvent )
    LOAD( ReceivePacket )
    LOAD( ReleaseReceivePacket )
    LOAD( AllocateSendPacket )
    LOAD( SendPacket )

    if (missing)
    {
        printf( "missing exports, stopping\n" );
        return 1;
    }

    ntdll = LoadLibraryW( L"ntdll.dll" );
    pNtQueryEvent = ntdll ? (void *)GetProcAddress( ntdll, "NtQueryEvent" ) : NULL;

    pSetLogger( logger_cb );

    printf( "== driver ==\n" );
    version = pGetRunningDriverVersion();
    printf( "driver-version=0x%08lx\n", (unsigned long)version );
    SetLastError( 0xdeadbeef );
    err = !!pDeleteDriver();
    printf( "delete-driver(no driver yet)=%lu err=%lu\n", err, GetLastError() );

    printf( "== create ==\n" );
    SetLastError( 0xdeadbeef );
    adapter = pCreateAdapter( L"WintunProbe", L"Probe", NULL );
    err = GetLastError();
    if (!adapter)
    {
        print_error( "create-adapter" );
        return 1;
    }
    printf( "create-adapter=ok\n" );

    memset( &luid, 0, sizeof(luid) );
    pGetAdapterLUID( adapter, (LUID *)&luid );
    printf( "luid=%08lx-%08lx\n", (unsigned long)(luid.Value & 0xffffffff),
            (unsigned long)(luid.Value >> 32) );

    index = 0;
    err = ConvertInterfaceLuidToIndex( &luid, &index );
    printf( "luid-to-index err=%lu index=%lu\n", err, index );

    memset( &luid2, 0, sizeof(luid2) );
    err = ConvertInterfaceIndexToLuid( index, &luid2 );
    printf( "index-to-luid err=%lu luid=%I64x equal=%d\n", err, (unsigned long long)luid2.Value,
            luid2.Value == luid.Value );

    {
        WCHAR alias[256];
        err = ConvertInterfaceLuidToAlias( &luid, alias, ARRAYSIZE(alias) );
        printf( "luid-to-alias err=%lu alias=%ls\n", err, alias );
    }
    {
        ULONG needed = 0;
        IP_ADAPTER_ADDRESSES *addrs, *a;
        GetAdaptersAddresses( AF_UNSPEC, 0, NULL, NULL, &needed );
        addrs = malloc( needed );
        if (addrs && !GetAdaptersAddresses( AF_UNSPEC, 0, NULL, addrs, &needed ))
        {
            for (a = addrs; a; a = a->Next)
            {
                if (a->Luid.Value != luid.Value) continue;
                printf( "adapter: name=%ls description=%ls iftype=%lu operstatus=%lu\n",
                        a->FriendlyName, a->Description, (unsigned long)a->IfType,
                        (unsigned long)a->OperStatus );
            }
        }
        free( addrs );
    }

    SetLastError( 0xdeadbeef );
    adapter2 = pCreateAdapter( L"WintunProbe", L"Probe", NULL );
    err = GetLastError();
    printf( "create-same-name=%s err=%lu\n", adapter2 ? "ok" : "NULL", err );
    if (adapter2) pCloseAdapter( adapter2 );

    version = pGetRunningDriverVersion();
    printf( "driver-version-after-create=0x%08lx\n", (unsigned long)version );

    SetLastError( 0xdeadbeef );
    adapter2 = pOpenAdapter( L"WintunProbe" );
    err = GetLastError();
    printf( "open-adapter=%s err=%lu\n", adapter2 ? "ok" : "NULL", err );
    if (adapter2) pCloseAdapter( adapter2 );

    printf( "== session ==\n" );
    SetLastError( 0xdeadbeef );
    adapter2 = (void *)pStartSession( adapter, 0 );
    err = GetLastError();
    printf( "start-session(0)=%s err=%lu\n", adapter2 ? "ok" : "NULL", err );
    if (adapter2) pEndSession( adapter2 );

    SetLastError( 0xdeadbeef );
    adapter2 = (void *)pStartSession( adapter, 0x1000 );
    err = GetLastError();
    printf( "start-session(0x1000)=%s err=%lu\n", adapter2 ? "ok" : "NULL", err );
    if (adapter2) pEndSession( adapter2 );

    SetLastError( 0xdeadbeef );
    adapter2 = (void *)pStartSession( adapter, 0x30000 );
    err = GetLastError();
    printf( "start-session(0x30000)=%s err=%lu\n", adapter2 ? "ok" : "NULL", err );
    if (adapter2) pEndSession( adapter2 );

    SetLastError( 0xdeadbeef );
    adapter2 = (void *)pStartSession( adapter, 0x8000000 );
    err = GetLastError();
    printf( "start-session(0x8000000)=%s err=%lu\n", adapter2 ? "ok" : "NULL", err );
    if (adapter2) pEndSession( adapter2 );

    SetLastError( 0xdeadbeef );
    session = pStartSession( adapter, 0x20000 );
    err = GetLastError();
    printf( "start-session(0x20000)=%s err=%lu\n", session ? "ok" : "NULL", err );
    if (!session) return 1;

    SetLastError( 0xdeadbeef );
    adapter2 = (void *)pStartSession( adapter, 0x20000 );
    err = GetLastError();
    printf( "start-session-second=%s err=%lu\n", adapter2 ? "ok" : "NULL", err );
    if (adapter2) pEndSession( adapter2 );

    event = pGetReadWaitEvent( session );
    printf( "read-wait-event=%p err=%lu\n", event, GetLastError() );
    if (event && pNtQueryEvent)
    {
        PROBE_EVENT_BASIC_INFORMATION info;
        ULONG len;
        if (!pNtQueryEvent( event, 0, &info, sizeof(info), &len ))
            printf( "event-type=%d state=%ld\n", info.type, info.state );
    }
    if (event) printf( "event-wait(0)=%lu\n", WaitForSingleObject( event, 0 ) );

    /* drain whatever the adapter has queued already */
    count = 0;
    for (i = 0; i < 64; i++)
    {
        size = 0;
        packet = pReceivePacket( session, &size );
        if (!packet) break;
        if (!count) { printf( "receive-drain:" ); }
        printf( " %lu", size );
        describe_packet( packet, size );
        pReleaseReceivePacket( session, packet );
        count++;
    }
    if (!count) printf( "receive-drain: none\n" ); else printf( "\n" );

    SetLastError( 0xdeadbeef );
    packet = pAllocateSendPacket( session, 0x20001 );
    err = GetLastError();
    printf( "allocate(0x20001)=%s err=%lu\n", packet ? "packet" : "NULL", err );

    packet = pAllocateSendPacket( session, 1500 );
    err = GetLastError();
    printf( "allocate(1500)=%s err=%lu\n", packet ? "packet" : "NULL", err );
    if (packet)
    {
        memset( packet, 0x5a, 1500 );
        pSendPacket( session, packet );
        printf( "send=done err=%lu\n", GetLastError() );
    }

    if (loopback)
    {
        FILE *f;
        unsigned char frame[64];

        printf( "loopback: ready\n" );
        if (!(f = fopen( "probe-ready", "wb" ))) { printf( "loopback: no marker\n" ); return 1; }
        fclose( f );
        if (!wait_file( "go", 60000 )) { printf( "loopback: no go file\n" ); return 1; }

        memset( frame, 0, sizeof(frame) );
        frame[0] = 0x45;                 /* version 4, header length 20 */
        frame[2] = 0; frame[3] = 40;     /* total length */
        frame[4] = 0x12; frame[5] = 0x34;/* id */
        frame[8] = 64;                   /* ttl */
        frame[9] = 1;                    /* ICMP */
        frame[12] = 10; frame[13] = 9; frame[14] = 9; frame[15] = 1;   /* source */
        frame[16] = 10; frame[17] = 9; frame[18] = 9; frame[19] = 2;   /* destination */
        {
            unsigned short chk = ip_checksum( frame, 20 );
            frame[10] = chk >> 8; frame[11] = chk & 0xff;
        }

        frame[20] = 8;                   /* echo request */
        frame[24] = 0x12; frame[25] = 0x34;
        frame[26] = 0; frame[27] = 1;    /* sequence */
        memcpy( frame + 28, "wintun-probe", 12 );
        {
            unsigned short chk = ip_checksum( frame + 20, 20 );
            frame[22] = chk >> 8; frame[23] = chk & 0xff;
        }

        packet = pAllocateSendPacket( session, 40 );
        if (!packet) { printf( "loopback: allocate failed err=%lu\n", GetLastError() ); return 1; }
        memcpy( packet, frame, 40 );
        pSendPacket( session, packet );

        if (event) WaitForSingleObject( event, 5000 );
        count = 0;
        for (i = 0; i < 64; i++)
        {
            size = 0;
            packet = pReceivePacket( session, &size );
            if (!packet) break;
            printf( "loopback: got %lu", size );
            describe_packet( packet, size );
            if (size >= 28 && (packet[0] >> 4) == 4 && packet[9] == 1 && packet[20] == 0)
                printf( " echo-reply=yes" );
            printf( "\n" );
            pReleaseReceivePacket( session, packet );
            count++;
        }
        if (!count) printf( "loopback: nothing received\n" );
    }

    pEndSession( session );
    printf( "end-session=done\n" );

    printf( "== close ==\n" );
    pCloseAdapter( adapter );
    SetLastError( 0xdeadbeef );
    adapter2 = pOpenAdapter( L"WintunProbe" );
    err = GetLastError();
    printf( "open-after-close=%s err=%lu\n", adapter2 ? "ok" : "NULL", err );
    if (adapter2) pCloseAdapter( adapter2 );

    SetLastError( 0xdeadbeef );
    err = !!pDeleteDriver();
    printf( "delete-driver=%lu err=%lu\n", err, GetLastError() );
    printf( "done\n" );
    return 0;
}
