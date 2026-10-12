/* The old route functions CreateIpForwardEntry, SetIpForwardEntry and DeleteIpForwardEntry:
 * return codes, how MIB_IPFORWARDROW maps to the route that is created (metric, next hop, type,
 * protocol) and how it reads back through GetIpForwardTable and GetIpForwardTable2. Uses the
 * TEST-NET-2 range 198.51.100.0/24 on the interface that reaches 8.8.8.8 and removes everything
 * it adds. Addresses are printed relative to that interface ("gw" is its gateway, "self" its own
 * address, "link" another address on its link) and metrics relative to its interface metric
 * ("M"), so that runs on different machines compare line by line. Needs to run elevated. An
 * optional argument names a file for the output. */
#define _WIN32_WINNT 0x0A00
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdio.h>
#include <string.h>

static DWORD idx, gw, self, other, imetric;
static NET_LUID luid;

static DWORD addr(const char *s)
{
    IN_ADDR a;
    inet_pton(AF_INET, s, &a);
    return a.s_addr;
}

static const char *rel(DWORD a, char *buf)
{
    if (a == gw) return "gw";
    if (a == self) return "self";
    if (a == other) return "link";
    if (!a) return "0";
    inet_ntop(AF_INET, &a, buf, 16);
    return buf;
}

static BOOL in_test_net(DWORD a)
{
    return (a & htonl(0xffffff00)) == addr("198.51.100.0");
}

static void dump(void)
{
    MIB_IPFORWARD_TABLE2 *t2;
    MIB_IPFORWARDTABLE *t1;
    ULONG size = 0, i, n = 0;
    char b1[16], b2[16], b3[16];

    if (!GetIpForwardTable2(AF_INET, &t2))
    {
        for (i = 0; i < t2->NumEntries; i++)
        {
            MIB_IPFORWARD_ROW2 *r = t2->Table + i;
            if (!in_test_net(r->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr)) continue;
            printf("  table2: %s/%u nexthop=%s if=%s metric=%lu proto=%u origin=%u\n",
                   rel(r->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr, b1), r->DestinationPrefix.PrefixLength,
                   rel(r->NextHop.Ipv4.sin_addr.s_addr, b2), r->InterfaceIndex == idx ? "this" : "other",
                   r->Metric, r->Protocol, r->Origin);
            n++;
        }
        FreeMibTable(t2);
    }
    GetIpForwardTable(NULL, &size, FALSE);
    t1 = malloc(size);
    if (!GetIpForwardTable(t1, &size, FALSE))
    {
        for (i = 0; i < t1->dwNumEntries; i++)
        {
            MIB_IPFORWARDROW *r = t1->table + i;
            if (!in_test_net(r->dwForwardDest)) continue;
            printf("  table1: %s mask=%s nexthop=%s if=%s type=%lu proto=%lu metric1=M%+ld\n",
                   rel(r->dwForwardDest, b1), inet_ntop(AF_INET, &r->dwForwardMask, b3, 16),
                   rel(r->dwForwardNextHop, b2), r->dwForwardIfIndex == idx ? "this" : "other",
                   r->dwForwardType, r->dwForwardProto, (long)(r->dwForwardMetric1 - imetric));
        }
    }
    free(t1);
    if (!n) printf("  (no route)\n");
}

/* remove every route in the test range, whatever the probe left */
static void cleanup(void)
{
    MIB_IPFORWARD_TABLE2 *t2;
    ULONG i;

    if (GetIpForwardTable2(AF_INET, &t2)) return;
    for (i = 0; i < t2->NumEntries; i++)
        if (in_test_net(t2->Table[i].DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr))
            DeleteIpForwardEntry2(t2->Table + i);
    FreeMibTable(t2);
}

static void base(MIB_IPFORWARDROW *r)
{
    memset(r, 0, sizeof(*r));
    r->dwForwardDest = addr("198.51.100.0");
    r->dwForwardMask = addr("255.255.255.0");
    r->dwForwardNextHop = gw;
    r->dwForwardIfIndex = idx;
    r->dwForwardType = MIB_IPROUTE_TYPE_INDIRECT;
    r->dwForwardProto = MIB_IPPROTO_NETMGMT;
    r->dwForwardMetric1 = imetric + 5;
}

static void create(const char *label, MIB_IPFORWARDROW *r)
{
    printf("%s: CreateIpForwardEntry ret=%lu\n", label, CreateIpForwardEntry(r));
    dump();
    cleanup();
}

int main(int argc, char **argv)
{
    MIB_IPFORWARDROW r, r2;
    MIB_IPINTERFACE_ROW ifr;
    BOOLEAN old_auto;
    MIB_IPFORWARDROW best;
    MIB_UNICASTIPADDRESS_TABLE *uni;
    ULONG i;

    /* where the output goes when stdout doesn't reach the caller */
    if (argc > 1 && !freopen(argv[1], "w", stdout)) return 1;
    setvbuf(stdout, NULL, _IONBF, 0);

    if (GetBestRoute(addr("8.8.8.8"), 0, &best)) { printf("GetBestRoute failed\n"); return 1; }
    idx = best.dwForwardIfIndex;
    gw = best.dwForwardNextHop;
    ConvertInterfaceIndexToLuid(idx, &luid);
    InitializeIpInterfaceEntry(&ifr);
    ifr.Family = AF_INET;
    ifr.InterfaceLuid = luid;
    if (GetIpInterfaceEntry(&ifr)) { printf("GetIpInterfaceEntry failed\n"); return 1; }
    imetric = ifr.Metric;
    if (!GetUnicastIpAddressTable(AF_INET, &uni))
    {
        for (i = 0; i < uni->NumEntries; i++)
            if (uni->Table[i].InterfaceIndex == idx) { self = uni->Table[i].Address.Ipv4.sin_addr.s_addr; break; }
        FreeMibTable(uni);
    }
    for (i = 0xfe; i > 0xf0; i--)
    {
        other = (self & htonl(0xffffff00)) | htonl(i);
        if (other != self && other != gw) break;
    }
    printf("interface: metric M=%s gateway=%s address=%s\n", imetric ? "set" : "0", gw ? "set" : "0",
           self ? "set" : "0");
    printf("default route: type=%lu proto=%lu metric1=M%+ld\n", best.dwForwardType, best.dwForwardProto,
           (long)(best.dwForwardMetric1 - imetric));
    cleanup();

    /* create, duplicates, set, delete */
    base(&r);
    printf("CreateIpForwardEntry ret=%lu\n", CreateIpForwardEntry(&r));
    dump();
    printf("CreateIpForwardEntry again ret=%lu\n", CreateIpForwardEntry(&r));
    r2 = r; r2.dwForwardMetric1 = imetric + 9;
    printf("CreateIpForwardEntry again, metric M+9 ret=%lu\n", CreateIpForwardEntry(&r2));
    dump();
    r2 = r; r2.dwForwardMetric1 = imetric + 11;
    printf("SetIpForwardEntry metric M+11 ret=%lu\n", SetIpForwardEntry(&r2));
    dump();
    r2 = r; r2.dwForwardMetric1 = imetric + 2; r2.dwForwardType = MIB_IPROUTE_TYPE_DIRECT; r2.dwForwardProto = 0;
    printf("SetIpForwardEntry metric M+2, type 3, proto 0 ret=%lu\n", SetIpForwardEntry(&r2));
    dump();
    r2 = r; r2.dwForwardDest = addr("198.51.100.128"); r2.dwForwardMask = addr("255.255.255.128");
    printf("SetIpForwardEntry not existing ret=%lu\n", SetIpForwardEntry(&r2));
    dump();
    r2 = r; r2.dwForwardMetric1 = imetric + 1;
    printf("DeleteIpForwardEntry other metric ret=%lu\n", DeleteIpForwardEntry(&r2));
    dump();
    base(&r);
    CreateIpForwardEntry(&r);
    r2 = r; r2.dwForwardNextHop = 0;
    printf("DeleteIpForwardEntry next hop 0 ret=%lu\n", DeleteIpForwardEntry(&r2));
    dump();
    r2 = r; r2.dwForwardType = 0; r2.dwForwardProto = 0;
    printf("DeleteIpForwardEntry type 0, proto 0 ret=%lu\n", DeleteIpForwardEntry(&r2));
    dump();
    base(&r);
    CreateIpForwardEntry(&r);
    printf("DeleteIpForwardEntry ret=%lu\n", DeleteIpForwardEntry(&r));
    printf("DeleteIpForwardEntry again ret=%lu\n", DeleteIpForwardEntry(&r));
    dump();
    cleanup();

    /* which route a delete or set with other fields finds */
    base(&r); CreateIpForwardEntry(&r);
    r2 = r; r2.dwForwardNextHop = other;
    printf("DeleteIpForwardEntry gw route with next hop on the link ret=%lu\n", DeleteIpForwardEntry(&r2));
    dump();
    cleanup();
    base(&r); CreateIpForwardEntry(&r);
    r2 = r; r2.dwForwardNextHop = self;
    printf("DeleteIpForwardEntry gw route with next hop self ret=%lu\n", DeleteIpForwardEntry(&r2));
    dump();
    cleanup();
    base(&r); r.dwForwardNextHop = 0; CreateIpForwardEntry(&r);
    r2 = r; r2.dwForwardNextHop = gw;
    printf("DeleteIpForwardEntry on-link route with next hop gw ret=%lu\n", DeleteIpForwardEntry(&r2));
    dump();
    cleanup();
    base(&r); CreateIpForwardEntry(&r);
    r2 = r; r2.dwForwardNextHop = other;
    printf("CreateIpForwardEntry same destination, next hop on the link ret=%lu\n", CreateIpForwardEntry(&r2));
    dump();
    r2 = r; r2.dwForwardNextHop = 0;
    printf("DeleteIpForwardEntry next hop 0 with two routes ret=%lu\n", DeleteIpForwardEntry(&r2));
    dump();
    cleanup();
    base(&r); CreateIpForwardEntry(&r);
    r2 = r; r2.dwForwardProto = 0;
    printf("DeleteIpForwardEntry proto 0 ret=%lu\n", DeleteIpForwardEntry(&r2));
    dump();
    r2 = r; r2.dwForwardIfIndex = 0;
    printf("DeleteIpForwardEntry interface 0 ret=%lu\n", DeleteIpForwardEntry(&r2));
    r2 = r; r2.dwForwardDest = addr("198.51.100.1");
    printf("DeleteIpForwardEntry host bits ret=%lu\n", DeleteIpForwardEntry(&r2));
    r2 = r; r2.dwForwardMask = addr("255.0.255.0");
    printf("DeleteIpForwardEntry mask 255.0.255.0 ret=%lu\n", DeleteIpForwardEntry(&r2));
    base(&r); CreateIpForwardEntry(&r);
    r2 = r; r2.dwForwardMetric1 = 0;
    printf("DeleteIpForwardEntry metric 0 ret=%lu\n", DeleteIpForwardEntry(&r2));
    dump();
    cleanup();
    /* what NetEase UU passes before it creates a route */
    base(&r); CreateIpForwardEntry(&r);
    memset(&r2, 0, sizeof(r2));
    r2.dwForwardDest = r.dwForwardDest; r2.dwForwardMask = r.dwForwardMask; r2.dwForwardIfIndex = 0xffffffff;
    printf("DeleteIpForwardEntry interface -1, everything else 0 ret=%lu\n", DeleteIpForwardEntry(&r2));
    dump();
    cleanup();
    base(&r); CreateIpForwardEntry(&r);
    r2 = r; r2.dwForwardProto = 0;
    printf("SetIpForwardEntry proto 0 ret=%lu\n", SetIpForwardEntry(&r2));
    r2 = r; r2.dwForwardType = MIB_IPROUTE_TYPE_DIRECT; r2.dwForwardMetric1 = imetric + 6;
    printf("SetIpForwardEntry type 3, metric M+6 ret=%lu\n", SetIpForwardEntry(&r2));
    dump();
    r2 = r; r2.dwForwardMetric1 = 0;
    printf("SetIpForwardEntry metric 0 ret=%lu\n", SetIpForwardEntry(&r2));
    r2 = r; r2.dwForwardNextHop = 0; r2.dwForwardMetric1 = imetric + 7;
    printf("SetIpForwardEntry next hop 0, metric M+7 ret=%lu\n", SetIpForwardEntry(&r2));
    dump();
    r2 = r; r2.dwForwardNextHop = other; r2.dwForwardMetric1 = imetric + 8;
    printf("SetIpForwardEntry next hop on the link, metric M+8 ret=%lu\n", SetIpForwardEntry(&r2));
    dump();
    cleanup();

    /* metric */
    base(&r); r.dwForwardMetric1 = 0; create("metric 0", &r);
    base(&r); r.dwForwardMetric1 = 1; create("metric 1", &r);
    base(&r); r.dwForwardMetric1 = imetric - 1; create("metric M-1", &r);
    base(&r); r.dwForwardMetric1 = imetric; create("metric M", &r);
    base(&r); r.dwForwardMetric1 = imetric + 1; create("metric M+1", &r);
    base(&r); r.dwForwardMetric1 = 9999; create("metric 9999", &r);
    base(&r); r.dwForwardMetric1 = imetric + 0xffff; create("metric M+65535", &r);
    base(&r); r.dwForwardMetric1 = imetric + 0x10000; create("metric M+65536", &r);
    base(&r); r.dwForwardMetric1 = imetric + 0x7fffffff; create("metric M+0x7fffffff", &r);
    base(&r); r.dwForwardMetric1 = imetric + 0x80000000; create("metric M+0x80000000", &r);
    base(&r); r.dwForwardMetric1 = 0xfffffffe; create("metric 0xfffffffe", &r);
    base(&r); r.dwForwardMetric1 = ~0u; create("metric 0xffffffff", &r);

    /* protocol */
    base(&r); r.dwForwardProto = 0; create("proto 0", &r);
    base(&r); r.dwForwardProto = MIB_IPPROTO_LOCAL; create("proto 2 (local)", &r);
    base(&r); r.dwForwardProto = MIB_IPPROTO_NT_STATIC; create("proto 10002 (static)", &r);
    base(&r); r.dwForwardProto = MIB_IPPROTO_NT_AUTOSTATIC; create("proto 10001 (autostatic)", &r);
    base(&r); r.dwForwardProto = 99; create("proto 99", &r);

    /* type */
    base(&r); r.dwForwardType = 0; create("type 0", &r);
    base(&r); r.dwForwardType = MIB_IPROUTE_TYPE_INVALID; create("type 2 (invalid)", &r);
    base(&r); r.dwForwardType = MIB_IPROUTE_TYPE_DIRECT; create("type 3 (direct), next hop gw", &r);
    base(&r); r.dwForwardType = 9; create("type 9", &r);

    /* next hop */
    base(&r); r.dwForwardNextHop = self; create("next hop self, type 4", &r);
    base(&r); r.dwForwardNextHop = self; r.dwForwardType = MIB_IPROUTE_TYPE_DIRECT; create("next hop self, type 3", &r);
    base(&r); r.dwForwardNextHop = 0; create("next hop 0, type 4", &r);
    base(&r); r.dwForwardNextHop = 0; r.dwForwardType = MIB_IPROUTE_TYPE_DIRECT; create("next hop 0, type 3", &r);
    base(&r); r.dwForwardNextHop = addr("203.0.113.1"); create("next hop off-link 203.0.113.1", &r);
    base(&r); r.dwForwardNextHop = addr("198.51.100.1"); create("next hop inside the destination", &r);
    base(&r); r.dwForwardNextHop = other; create("next hop another address on the link", &r);

    /* destination and mask */
    base(&r); r.dwForwardMask = addr("255.0.255.0"); create("mask 255.0.255.0", &r);
    base(&r); r.dwForwardDest = addr("198.51.100.1"); create("destination with host bits", &r);
    base(&r); r.dwForwardDest = addr("198.51.100.7"); r.dwForwardMask = addr("255.255.255.255"); create("host route /32", &r);
    base(&r); r.dwForwardDest = addr("198.51.100.64"); r.dwForwardMask = addr("255.255.255.192"); create("prefix /26", &r);

    /* interface */
    base(&r); r.dwForwardIfIndex = 0; create("interface 0", &r);
    base(&r); r.dwForwardIfIndex = 99999; create("interface not existing", &r);

    /* fields that the route has no use for */
    base(&r); r.dwForwardPolicy = 7; r.dwForwardAge = 1234; r.dwForwardNextHopAS = 5;
    r.dwForwardMetric2 = r.dwForwardMetric3 = r.dwForwardMetric4 = r.dwForwardMetric5 = 3;
    create("policy, age, AS and metric2-5 set", &r);
    base(&r); r.dwForwardMetric2 = r.dwForwardMetric3 = r.dwForwardMetric4 = r.dwForwardMetric5 = ~0u;
    create("metric2-5 0xffffffff", &r);

    /* the interface metric in what the old functions take and give */
    InitializeIpInterfaceEntry(&ifr);
    ifr.Family = AF_INET;
    ifr.InterfaceLuid = luid;
    GetIpInterfaceEntry(&ifr);
    old_auto = ifr.UseAutomaticMetric;
    ifr.SitePrefixLength = 0;
    base(&r); CreateIpForwardEntry(&r);
    ifr.UseAutomaticMetric = FALSE;
    ifr.Metric = imetric + 10;
    printf("SetIpInterfaceEntry metric M+10 ret=%lu\n", SetIpInterfaceEntry(&ifr));
    dump();
    imetric += 10;
    printf("relative to the new interface metric:\n");
    dump();
    r2 = r; r2.dwForwardMetric1 = imetric + 3;
    printf("SetIpForwardEntry metric (M+10)+3 ret=%lu\n", SetIpForwardEntry(&r2));
    dump();
    r2 = r; r2.dwForwardMetric1 = imetric - 1;
    printf("SetIpForwardEntry metric (M+10)-1 ret=%lu\n", SetIpForwardEntry(&r2));
    cleanup();
    imetric -= 10;
    ifr.UseAutomaticMetric = old_auto;
    ifr.Metric = imetric;
    printf("SetIpInterfaceEntry restore ret=%lu\n", SetIpInterfaceEntry(&ifr));

    /* NULL */
    printf("CreateIpForwardEntry NULL ret=%lu\n", CreateIpForwardEntry(NULL));
    printf("SetIpForwardEntry NULL ret=%lu\n", SetIpForwardEntry(NULL));
    printf("DeleteIpForwardEntry NULL ret=%lu\n", DeleteIpForwardEntry(NULL));

    cleanup();
    dump();
    return 0;
}
