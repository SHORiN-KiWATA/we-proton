/*
 * Exercise interface identity after deleting and recreating a same-name adapter.
 * Run only in an isolated test environment, with --isolated.
 * Under Wine use the builtin Wintun module; on Windows use the native DLL.
 * The probe does not configure IP addresses/routes or send network packets.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define WIN32_LEAN_AND_MEAN
#define __USE_MINGW_ANSI_STDIO 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

typedef void *WINTUN_ADAPTER_HANDLE;
static WINTUN_ADAPTER_HANDLE (WINAPI *pCreateAdapter)( const WCHAR *, const WCHAR *, const GUID * );
static void (WINAPI *pCloseAdapter)( WINTUN_ADAPTER_HANDLE );
static void (WINAPI *pGetAdapterLUID)( WINTUN_ADAPTER_HANDLE, LUID * );

static WCHAR adapter_name[32];
static unsigned int failures;

static void check( int condition, const char *phase, const char *operation )
{
    printf( "CHECK phase=%s op=%s result=%s\n", phase, operation, condition ? "PASS" : "FAIL" );
    if (!condition) failures++;
}

static unsigned int table_snapshot( const char *phase, NET_IFINDEX index, const NET_LUID *luid )
{
    MIB_IF_TABLE2 *table = NULL;
    DWORD err = GetIfTable2( &table );
    unsigned int i, own_count = 0, current_count = 0;

    printf( "TABLE phase=%s error=%lu\n", phase, err );
    check( !err, phase, "GetIfTable2" );
    if (err) return ~0u;
    for (i = 0; i < table->NumEntries; i++)
    {
        MIB_IF_ROW2 *row = table->Table + i;
        if (wcscmp( row->Alias, adapter_name )) continue;
        own_count++;
        if (luid && row->InterfaceIndex == index && row->InterfaceLuid.Value == luid->Value) current_count++;
        printf( "TABLE_ROW phase=%s index=%lu luid=%llx alias=%ls status=%u\n", phase,
                row->InterfaceIndex, (unsigned long long)row->InterfaceLuid.Value,
                row->Alias, (unsigned int)row->OperStatus );
    }
    FreeMibTable( table );
    if (luid) check( current_count == 1, phase, "table-current-identity-once" );
    check( own_count == (luid ? 1u : 0u), phase, "table-no-stale-name-records" );
    return own_count;
}

static void address_snapshot( const char *phase, NET_IFINDEX index, const NET_LUID *luid )
{
    const ULONG flags = GAA_FLAG_INCLUDE_ALL_INTERFACES | GAA_FLAG_SKIP_ANYCAST |
                        GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    IP_ADAPTER_ADDRESSES *buffer = NULL, *row;
    ULONG size = 16384;
    DWORD err = ERROR_BUFFER_OVERFLOW;
    unsigned int attempt, own_count = 0, current_count = 0;

    for (attempt = 0; attempt < 4 && err == ERROR_BUFFER_OVERFLOW; attempt++)
    {
        free( buffer );
        buffer = malloc( size );
        if (!buffer) { err = ERROR_NOT_ENOUGH_MEMORY; break; }
        err = GetAdaptersAddresses( AF_UNSPEC, flags, NULL, buffer, &size );
    }
    printf( "ADDRESSES phase=%s error=%lu\n", phase, err );
    check( !err, phase, "GetAdaptersAddresses" );
    if (!err)
    {
        for (row = buffer; row; row = row->Next)
        {
            if (!row->FriendlyName || wcscmp( row->FriendlyName, adapter_name )) continue;
            own_count++;
            if (luid && row->IfIndex == index && row->Luid.Value == luid->Value) current_count++;
            printf( "ADDRESS_ROW phase=%s index=%lu luid=%llx alias=%ls status=%u\n", phase,
                    row->IfIndex, (unsigned long long)row->Luid.Value,
                    row->FriendlyName, (unsigned int)row->OperStatus );
        }
        if (luid) check( current_count == 1, phase, "addresses-current-identity-once" );
        check( own_count == (luid ? 1u : 0u), phase, "addresses-no-stale-name-records" );
    }
    free( buffer );
}

static void alias_snapshot( const char *phase, const NET_LUID *expected )
{
    NET_LUID actual = {0};
    DWORD err = ConvertInterfaceAliasToLuid( adapter_name, &actual );

    printf( "ALIAS phase=%s error=%lu luid=%llx\n", phase, err, (unsigned long long)actual.Value );
    check( expected ? (!err && actual.Value == expected->Value) : !!err,
           phase, expected ? "alias-maps-current" : "removed-alias-fails" );
}

static void check_absent( const char *phase, NET_IFINDEX index, const NET_LUID *luid )
{
    MIB_IF_ROW2 row = {0};
    NET_LUID converted = {0};
    NET_IFINDEX converted_index = 0;
    DWORD err;

    row.InterfaceIndex = index;
    err = GetIfEntry2( &row );
    printf( "OLD_INDEX phase=%s index=%lu error=%lu\n", phase, index, err );
    check( !!err, phase, "removed-index-GetIfEntry2-fails" );
    memset( &row, 0, sizeof(row) );
    row.InterfaceLuid = *luid;
    err = GetIfEntry2( &row );
    printf( "OLD_LUID phase=%s luid=%llx error=%lu\n", phase, (unsigned long long)luid->Value, err );
    check( !!err, phase, "removed-luid-GetIfEntry2-fails" );
    err = ConvertInterfaceIndexToLuid( index, &converted );
    printf( "OLD_INDEX_CONVERT phase=%s error=%lu luid=%llx\n", phase, err,
            (unsigned long long)converted.Value );
    check( !!err, phase, "removed-index-conversion-fails" );
    err = ConvertInterfaceLuidToIndex( luid, &converted_index );
    printf( "OLD_LUID_CONVERT phase=%s error=%lu index=%lu\n", phase, err, converted_index );
    check( !!err, phase, "removed-luid-conversion-fails" );
}

static void wait_for_removal( NET_IFINDEX index )
{
    unsigned int i;

    /* Bound native Windows removal timing without hiding an indefinitely stale
     * Wine cache. Failure assertions follow even if the old row never vanishes. */
    for (i = 0; i < 20; i++)
    {
        MIB_IF_ROW2 row = {0};
        row.InterfaceIndex = index;
        if (GetIfEntry2( &row )) return;
        Sleep( 100 );
    }
}

int main( int argc, char **argv )
{
    const unsigned int cycles = 3;
    NET_LUID previous_luid = {0};
    NET_IFINDEX previous_index = 0;
    HMODULE library;
    unsigned int cycle;

    if (argc != 2 || strcmp( argv[1], "--isolated" ))
    {
        fprintf( stderr, "Usage: %s --isolated\nRun only in a separate test prefix/network namespace or Windows VM.\n", argv[0] );
        return 2;
    }
    setvbuf( stdout, NULL, _IONBF, 0 );
    swprintf( adapter_name, sizeof(adapter_name) / sizeof(*adapter_name), L"Cache%08lx", GetCurrentProcessId() );
    printf( "PROBE name=%ls cycles=%u\n", adapter_name, cycles );
    if (table_snapshot( "preflight", 0, NULL ))
    {
        fprintf( stderr, "Refusing to reuse an existing same-name interface.\n" );
        return 2;
    }
    library = LoadLibraryW( L"wintun.dll" );
    if (!library)
    {
        printf( "LOAD error=%lu\n", GetLastError() );
        return 2;
    }
    pCreateAdapter = (void *)GetProcAddress( library, "WintunCreateAdapter" );
    pCloseAdapter = (void *)GetProcAddress( library, "WintunCloseAdapter" );
    pGetAdapterLUID = (void *)GetProcAddress( library, "WintunGetAdapterLUID" );
    if (!pCreateAdapter || !pCloseAdapter || !pGetAdapterLUID) return 2;

    for (cycle = 0; cycle < cycles; cycle++)
    {
        WINTUN_ADAPTER_HANDLE adapter;
        NET_IFINDEX index = 0;
        NET_LUID luid = {0}, converted = {0};
        char present_phase[32], absent_phase[32];
        DWORD err;

        snprintf( present_phase, sizeof(present_phase), "cycle%u-present", cycle + 1 );
        snprintf( absent_phase, sizeof(absent_phase), "cycle%u-removed", cycle + 1 );
        adapter = pCreateAdapter( adapter_name, L"CacheLifecycle", NULL );
        err = GetLastError();
        printf( "CREATE phase=%s success=%d error_if_failed=%lu\n", present_phase, !!adapter, adapter ? 0 : err );
        if (!adapter) { failures++; break; }
        pGetAdapterLUID( adapter, (LUID *)&luid );
        err = ConvertInterfaceLuidToIndex( &luid, &index );
        printf( "CURRENT phase=%s index=%lu luid=%llx error=%lu\n", present_phase,
                index, (unsigned long long)luid.Value, err );
        if (err || !index) { failures++; pCloseAdapter( adapter ); break; }
        err = ConvertInterfaceIndexToLuid( index, &converted );
        check( !err && converted.Value == luid.Value, present_phase, "current-index-luid-roundtrip" );
        table_snapshot( present_phase, index, &luid );
        address_snapshot( present_phase, index, &luid );
        alias_snapshot( present_phase, &luid );
        if (previous_index && previous_index != index && previous_luid.Value != luid.Value)
            check_absent( present_phase, previous_index, &previous_luid );
        else if (previous_index)
            printf( "NOTE phase=%s reused-index-or-luid old-index=%lu new-index=%lu\n",
                    present_phase, previous_index, index );

        pCloseAdapter( adapter );
        wait_for_removal( index );
        check_absent( absent_phase, index, &luid );
        table_snapshot( absent_phase, 0, NULL );
        address_snapshot( absent_phase, 0, NULL );
        alias_snapshot( absent_phase, NULL );
        previous_luid = luid;
        previous_index = index;
    }
    FreeLibrary( library );
    printf( "RESULT failures=%u\n", failures );
    return failures ? 1 : 0;
}
