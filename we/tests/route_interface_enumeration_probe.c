/* Persistent read-only observer for isolated route identity regression tests. */
#define _WIN32_WINNT 0x0601
#define __USE_MINGW_ANSI_STDIO 1
#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void routes(ADDRESS_FAMILY family, const char *label)
{
    static const BYTE prefix6[16] = {0x20, 1, 0x0d, 0xb8, 0, 0x77};
    static const BYTE zero[16];
    MIB_IPFORWARD_TABLE2 *table = NULL;
    DWORD error = GetIpForwardTable2(family, &table);
    ULONG i;
    printf("STATUS query=%s error=%lu\n", label, error);
    if (error) return;
    for (i = 0; i < table->NumEntries; i++)
    {
        MIB_IPFORWARD_ROW2 *row = &table->Table[i];
        const BYTE *address;
        unsigned id, fam, next_zero;
        if (row->DestinationPrefix.Prefix.si_family == AF_INET)
        {
            address = (const BYTE *)&row->DestinationPrefix.Prefix.Ipv4.sin_addr;
            if (address[0] != 198 || address[1] != 18 || address[2] != 11) continue;
            id = address[3];
            fam = 4;
            next_zero = !memcmp(&row->NextHop.Ipv4.sin_addr, zero, 4);
        }
        else if (row->DestinationPrefix.Prefix.si_family == AF_INET6)
        {
            address = (const BYTE *)&row->DestinationPrefix.Prefix.Ipv6.sin6_addr;
            if (memcmp(address, prefix6, 15)) continue;
            id = address[15];
            fam = 6;
            next_zero = !memcmp(&row->NextHop.Ipv6.sin6_addr, zero, 16);
        }
        else continue;
        printf("ROUTE query=%s family=%u id=%u index=%lu luid=%llx metric=%lu "
               "prefix=%u next_zero=%u\n", label, fam, id, row->InterfaceIndex,
               (unsigned long long)row->InterfaceLuid.Value, row->Metric,
               (unsigned)row->DestinationPrefix.PrefixLength, next_zero);
    }
    FreeMibTable(table);
}

static void snapshot(const char *phase)
{
    static const WCHAR *names[] = {L"RouteTapA", L"RouteTapB"};
    unsigned i;
    printf("BEGIN phase=%s\n", phase);
    for (i = 0; i < ARRAYSIZE(names); i++)
    {
        NET_LUID luid = {0};
        NET_IFINDEX index = 0;
        DWORD error = ConvertInterfaceAliasToLuid(names[i], &luid);
        DWORD index_error = error ? error : ConvertInterfaceLuidToIndex(&luid, &index);
        printf("MAP name=%ls error=%lu index_error=%lu index=%lu luid=%llx\n",
               names[i], error, index_error, index, (unsigned long long)luid.Value);
    }
    routes(AF_INET, "v4");
    routes(AF_INET6, "v6");
    routes(AF_UNSPEC, "both");
    printf("END phase=%s\n", phase);
}

int main(void)
{
    char line[128];
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("READY pid=%lu\n", GetCurrentProcessId());
    while (fgets(line, sizeof(line), stdin))
    {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "quit")) break;
        snapshot(line);
    }
    return 0;
}
