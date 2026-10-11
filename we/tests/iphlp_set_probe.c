/* iphlpapi functions that change interfaces, addresses and routes: Initialize* defaults,
 * SetIpInterfaceEntry, Create/Get/Set/DeleteUnicastIpAddressEntry and
 * Create/Get/Set/DeleteIpForwardEntry2, including their error codes.
 * Uses the TEST-NET-2 range 198.51.100.0/24 on the interface that reaches 8.8.8.8 and removes
 * everything it adds. Needs to run elevated. */
#define _WIN32_WINNT 0x0A00
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdio.h>

static void dump_forward(const char *label, const MIB_IPFORWARD_ROW2 *r)
{
    char pfx[64], nh[64];
    inet_ntop(AF_INET, &r->DestinationPrefix.Prefix.Ipv4.sin_addr, pfx, sizeof(pfx));
    inet_ntop(AF_INET, &r->NextHop.Ipv4.sin_addr, nh, sizeof(nh));
    printf("%s: luid=%s idx=%s prefix=%s/%u fam=%u nexthop=%s fam=%u site=%u valid=%#lx pref=%#lx metric=%lu proto=%u "
           "loopback=%u autoconf=%u publish=%u immortal=%u age=%lu origin=%u\n", label,
           r->InterfaceLuid.Value ? "set" : "0", r->InterfaceIndex ? "set" : "0", pfx, r->DestinationPrefix.PrefixLength,
           r->DestinationPrefix.Prefix.si_family, nh, r->NextHop.si_family, r->SitePrefixLength,
           r->ValidLifetime, r->PreferredLifetime, r->Metric, r->Protocol, r->Loopback, r->AutoconfigureAddress,
           r->Publish, r->Immortal, r->Age, r->Origin);
}

static void dump_unicast(const char *label, const MIB_UNICASTIPADDRESS_ROW *r)
{
    char a[64];
    inet_ntop(AF_INET, &r->Address.Ipv4.sin_addr, a, sizeof(a));
    printf("%s: addr=%s fam=%u luid=%s idx=%s prefix_origin=%d suffix_origin=%d valid=%#lx pref=%#lx onlink=%u "
           "skip=%u dad=%d scope=%lu\n", label, a, r->Address.si_family, r->InterfaceLuid.Value ? "set" : "0",
           r->InterfaceIndex ? "set" : "0", r->PrefixOrigin, r->SuffixOrigin, r->ValidLifetime,
           r->PreferredLifetime, r->OnLinkPrefixLength, r->SkipAsSource, r->DadState, r->ScopeId.Value);
}

static void dump_interface(const char *label, const MIB_IPINTERFACE_ROW *r)
{
    printf("%s: fam=%u luid=%s idx=%s maxreasm=%lu ifid=%#llx minra=%lu maxra=%lu advertising=%u forwarding=%u "
           "weak_send=%u weak_recv=%u auto_metric=%u nud=%u managed=%u other=%u adv_default=%u router_disc=%d "
           "dad=%lu base_reach=%lu retrans=%lu pmtu_to=%lu link_local=%d ll_timeout=%lu zones=%lu,%lu,%lu "
           "site_prefix=%lu metric=%lu mtu=%lu connected=%u wakeup=%u nd=%u rd=%u reach=%lu disable_default=%u\n",
           label, r->Family, r->InterfaceLuid.Value ? "set" : "0", r->InterfaceIndex ? "set" : "0",
           r->MaxReassemblySize, r->InterfaceIdentifier, r->MinRouterAdvertisementInterval,
           r->MaxRouterAdvertisementInterval, r->AdvertisingEnabled, r->ForwardingEnabled, r->WeakHostSend,
           r->WeakHostReceive, r->UseAutomaticMetric, r->UseNeighborUnreachabilityDetection,
           r->ManagedAddressConfigurationSupported, r->OtherStatefulConfigurationSupported, r->AdvertiseDefaultRoute,
           r->RouterDiscoveryBehavior, r->DadTransmits, r->BaseReachableTime, r->RetransmitTime,
           r->PathMtuDiscoveryTimeout, r->LinkLocalAddressBehavior, r->LinkLocalAddressTimeout, r->ZoneIndices[0],
           r->ZoneIndices[1], r->ZoneIndices[2], r->SitePrefixLength, r->Metric, r->NlMtu, r->Connected,
           r->SupportsWakeUpPatterns, r->SupportsNeighborDiscovery, r->SupportsRouterDiscovery, r->ReachableTime,
           r->DisableDefaultRoutes);
}

int main(void)
{
    MIB_IPFORWARD_ROW2 fwd, fwd2;
    MIB_UNICASTIPADDRESS_ROW uni, uni2;
    MIB_IPINTERFACE_ROW ifr, ifr2;
    SOCKADDR_INET dst = {0};
    NET_LUID luid;
    DWORD idx, ret;
    ULONG old_metric;
    BOOL old_auto;

    setvbuf(stdout, NULL, _IONBF, 0);

    memset(&fwd, 0xcc, sizeof(fwd));
    InitializeIpForwardEntry(&fwd);
    dump_forward("InitializeIpForwardEntry", &fwd);
    memset(&uni, 0xcc, sizeof(uni));
    InitializeUnicastIpAddressEntry(&uni);
    dump_unicast("InitializeUnicastIpAddressEntry", &uni);
    memset(&ifr, 0xcc, sizeof(ifr));
    InitializeIpInterfaceEntry(&ifr);
    dump_interface("InitializeIpInterfaceEntry", &ifr);

    dst.si_family = AF_INET;
    dst.Ipv4.sin_family = AF_INET;
    dst.Ipv4.sin_addr.s_addr = htonl(0x08080808);
    if ((ret = GetBestInterfaceEx((struct sockaddr *)&dst, &idx))) { printf("GetBestInterfaceEx %lu\n", ret); return 1; }
    ConvertInterfaceIndexToLuid(idx, &luid);

    /* interface */
    InitializeIpInterfaceEntry(&ifr);
    ifr.Family = AF_INET;
    ifr.InterfaceLuid = luid;
    printf("GetIpInterfaceEntry ret=%lu\n", GetIpInterfaceEntry(&ifr));
    dump_interface("current", &ifr);
    old_metric = ifr.Metric;
    old_auto = ifr.UseAutomaticMetric;
    printf("SetIpInterfaceEntry unchanged ret=%lu\n", SetIpInterfaceEntry(&ifr));
    ifr2 = ifr; ifr2.SitePrefixLength = 1;
    printf("SetIpInterfaceEntry SitePrefixLength=1 ret=%lu\n", SetIpInterfaceEntry(&ifr2));
    ifr2 = ifr; ifr2.SitePrefixLength = 0; ifr2.InterfaceLuid.Value = 0;
    printf("SetIpInterfaceEntry luid 0, index set ret=%lu\n", SetIpInterfaceEntry(&ifr2));
    ifr2 = ifr; ifr2.SitePrefixLength = 0; ifr2.InterfaceLuid.Value = 0; ifr2.InterfaceIndex = 0;
    printf("SetIpInterfaceEntry luid 0, index 0 ret=%lu\n", SetIpInterfaceEntry(&ifr2));
    ifr2 = ifr; ifr2.SitePrefixLength = 0; ifr2.Family = 0;
    printf("SetIpInterfaceEntry family 0 ret=%lu\n", SetIpInterfaceEntry(&ifr2));
    InitializeIpInterfaceEntry(&ifr2);
    ifr2.Family = AF_INET; ifr2.InterfaceLuid = luid;
    printf("SetIpInterfaceEntry straight from Initialize ret=%lu\n", SetIpInterfaceEntry(&ifr2));
    ifr2 = ifr; ifr2.SitePrefixLength = 0; ifr2.UseAutomaticMetric = FALSE; ifr2.Metric = old_metric + 7;
    printf("SetIpInterfaceEntry metric+7 ret=%lu\n", SetIpInterfaceEntry(&ifr2));
    InitializeIpInterfaceEntry(&ifr2); ifr2.Family = AF_INET; ifr2.InterfaceLuid = luid;
    GetIpInterfaceEntry(&ifr2);
    printf("after: metric is old+%ld auto=%u\n", (long)(ifr2.Metric - old_metric), ifr2.UseAutomaticMetric);
    ifr2.SitePrefixLength = 0; ifr2.UseAutomaticMetric = old_auto; ifr2.Metric = old_metric;
    printf("SetIpInterfaceEntry restore ret=%lu\n", SetIpInterfaceEntry(&ifr2));
    ifr2 = ifr; ifr2.SitePrefixLength = 0; ifr2.NlMtu = ifr.NlMtu;
    printf("SetIpInterfaceEntry same mtu ret=%lu\n", SetIpInterfaceEntry(&ifr2));

    /* routes */
    InitializeIpForwardEntry(&fwd);
    fwd.InterfaceLuid = luid;
    fwd.DestinationPrefix.Prefix.si_family = AF_INET;
    fwd.DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
    inet_pton(AF_INET, "198.51.100.0", &fwd.DestinationPrefix.Prefix.Ipv4.sin_addr);
    fwd.DestinationPrefix.PrefixLength = 24;
    fwd.NextHop.si_family = AF_INET;
    fwd.NextHop.Ipv4.sin_family = AF_INET;
    fwd.Metric = 5;
    printf("CreateIpForwardEntry2 ret=%lu\n", CreateIpForwardEntry2(&fwd));
    printf("CreateIpForwardEntry2 again ret=%lu\n", CreateIpForwardEntry2(&fwd));
    fwd2 = fwd; fwd2.Metric = 9;
    printf("CreateIpForwardEntry2 again, other metric ret=%lu\n", CreateIpForwardEntry2(&fwd2));
    memset(&fwd2, 0, sizeof(fwd2));
    fwd2.InterfaceLuid = luid; fwd2.DestinationPrefix = fwd.DestinationPrefix; fwd2.NextHop = fwd.NextHop;
    printf("GetIpForwardEntry2 ret=%lu\n", GetIpForwardEntry2(&fwd2));
    dump_forward("read back", &fwd2);
    fwd2.Metric = 11;
    printf("SetIpForwardEntry2 metric 11 ret=%lu\n", SetIpForwardEntry2(&fwd2));
    memset(&fwd2, 0, sizeof(fwd2));
    fwd2.InterfaceLuid = luid; fwd2.DestinationPrefix = fwd.DestinationPrefix; fwd2.NextHop = fwd.NextHop;
    GetIpForwardEntry2(&fwd2);
    printf("metric after set=%lu\n", fwd2.Metric);
    fwd2 = fwd; fwd2.InterfaceLuid.Value = 0; fwd2.InterfaceIndex = idx;
    inet_pton(AF_INET, "198.51.100.128", &fwd2.DestinationPrefix.Prefix.Ipv4.sin_addr);
    fwd2.DestinationPrefix.PrefixLength = 25;
    printf("CreateIpForwardEntry2 by index ret=%lu\n", CreateIpForwardEntry2(&fwd2));
    printf("DeleteIpForwardEntry2 by index ret=%lu\n", DeleteIpForwardEntry2(&fwd2));
    fwd2 = fwd; fwd2.InterfaceLuid.Value = 0; fwd2.InterfaceIndex = 0;
    printf("CreateIpForwardEntry2 no interface ret=%lu\n", CreateIpForwardEntry2(&fwd2));
    fwd2 = fwd; fwd2.DestinationPrefix.PrefixLength = 33;
    printf("CreateIpForwardEntry2 prefix 33 ret=%lu\n", CreateIpForwardEntry2(&fwd2));
    fwd2 = fwd; inet_pton(AF_INET, "198.51.100.1", &fwd2.DestinationPrefix.Prefix.Ipv4.sin_addr);
    printf("CreateIpForwardEntry2 host bits set ret=%lu\n", CreateIpForwardEntry2(&fwd2));
    printf("DeleteIpForwardEntry2 host bits set ret=%lu\n", DeleteIpForwardEntry2(&fwd2));
    printf("DeleteIpForwardEntry2 ret=%lu\n", DeleteIpForwardEntry2(&fwd));
    printf("DeleteIpForwardEntry2 again ret=%lu\n", DeleteIpForwardEntry2(&fwd));
    printf("GetIpForwardEntry2 after delete ret=%lu\n", GetIpForwardEntry2(&fwd));

    /* unicast addresses */
    InitializeUnicastIpAddressEntry(&uni);
    uni.InterfaceLuid = luid;
    uni.Address.si_family = AF_INET;
    uni.Address.Ipv4.sin_family = AF_INET;
    inet_pton(AF_INET, "198.51.100.7", &uni.Address.Ipv4.sin_addr);
    uni.OnLinkPrefixLength = 24;
    uni.DadState = IpDadStatePreferred;
    printf("CreateUnicastIpAddressEntry ret=%lu\n", CreateUnicastIpAddressEntry(&uni));
    printf("CreateUnicastIpAddressEntry again ret=%lu\n", CreateUnicastIpAddressEntry(&uni));
    memset(&uni2, 0, sizeof(uni2));
    uni2.InterfaceLuid = luid; uni2.Address = uni.Address;
    printf("GetUnicastIpAddressEntry ret=%lu\n", GetUnicastIpAddressEntry(&uni2));
    dump_unicast("read back", &uni2);
    memset(&fwd2, 0, sizeof(fwd2));
    fwd2.InterfaceLuid = luid; fwd2.DestinationPrefix = fwd.DestinationPrefix; fwd2.NextHop = fwd.NextHop;
    printf("on-link route for the new address: GetIpForwardEntry2 ret=%lu\n", GetIpForwardEntry2(&fwd2));
    uni2.OnLinkPrefixLength = 25;
    printf("SetUnicastIpAddressEntry prefix 25 ret=%lu\n", SetUnicastIpAddressEntry(&uni2));
    printf("DeleteUnicastIpAddressEntry ret=%lu\n", DeleteUnicastIpAddressEntry(&uni));
    printf("DeleteUnicastIpAddressEntry again ret=%lu\n", DeleteUnicastIpAddressEntry(&uni));
    uni2 = uni; uni2.InterfaceLuid.Value = 0; uni2.InterfaceIndex = 0;
    printf("CreateUnicastIpAddressEntry no interface ret=%lu\n", CreateUnicastIpAddressEntry(&uni2));
    uni2 = uni; uni2.OnLinkPrefixLength = 33;
    printf("CreateUnicastIpAddressEntry prefix 33 ret=%lu\n", CreateUnicastIpAddressEntry(&uni2));
    printf("DeleteUnicastIpAddressEntry prefix 33 ret=%lu\n", DeleteUnicastIpAddressEntry(&uni2));
    return 0;
}
