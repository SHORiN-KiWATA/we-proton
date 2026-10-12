# 0053：旧的路由函数 CreateIpForwardEntry、SetIpForwardEntry、DeleteIpForwardEntry

补丁：`patches/wine/0053-iphlpapi-Implement-CreateIpForwardEntry-SetIpForward.patch`（`dlls/iphlpapi/iphlpapi_main.c`）。建在 0015 的设置通路上：`Create/Set/DeleteIpForwardEntry2` → NSI → nsiproxy → netlink。

## 现象

网易 UU 加速器的路由模式（2026-10-12，shorin-proton-wrapper 的独立网络，we-proton-ge11-7-25）：点「加速」后 UU 显示加速中，`UU` 虚拟网卡建好了，也配上了地址（0015、0016、0048）。但网络命名空间的路由表里只有 3 条路由：默认路由，加上两块网卡各自的网段，没有一条走 `UU`。要加速的流量全从真实网卡出去，加速没有效果。

这是在试着让同一个命名空间里的 Linux Steam 也吃到加速时发现的（shorin-proton-wrapper 的「在独立网络里运行 Linux 程序」）：路由根本没加上，同一个前缀里的 Windows 程序也一样没被加速。

## 根因

UU 加删路由用的是旧的 `CreateIpForwardEntry`、`DeleteIpForwardEntry`（参数是 `MIB_IPFORWARDROW`），不是 0015 实现的 `*IpForwardEntry2`。Wine 里这三个旧函数是 stub：打一行 `FIXME`，什么都不做，返回成功，UU 就以为路由加上了。一次加速的日志里有 165 次 `CreateIpForwardEntry`、20 次 `DeleteIpForwardEntry` 的 stub 记录。

UU 实际传的参数（修复后用 `WINEDEBUG=+iphlpapi` 记下）：

- 加路由：大多是 /32 的单个地址，另有 /24、/16。经 `UU` 网卡网关（172.19.84.1）的 type 是 4；经真实网卡网关（192.168.0.1）的 type 是 3，这些是要直连的地址（国内服务器和 UU 自己的服务器）。协议都是 3（`MIB_IPPROTO_NETMGMT`），metric1 是 0 或 100。开始加之前调了两次 `GetIpInterfaceEntry`。
- 每加一条 /24 之前，先删一次同一个目标：接口是 `0xffffffff`，下一跳、type、协议、metric 全是 0。
- 停止加速时删掉自己加的 /24：写了具体网关，type、协议、metric 是 0。

## Windows 上的行为

`we/tests/iphlp_forward1_probe.c` 在 Windows 11（26200）测试机上以管理员身份实测，输出见 `iphlp_forward1_probe.windows.txt`。探针用 TEST-NET-2 `198.51.100.0/24`，路由加在通往 8.8.8.8 的那块网卡上，跑完全部删掉。输出里把地址写成相对这块网卡的 gw（网关）、self（本机地址）、link（同网段的另一个地址），metric 写成接口 metric M 加多少，这样不同机器的输出可以逐行比较。

- metric：`dwForwardMetric1` 是路由 metric 加上接口 metric M。metric1 小于 M 时返回 160（`ERROR_BAD_ARGUMENTS`）；路由 metric（metric1 − M）超过 0x7fffffff 时返回 87。`GetIpForwardTable` 读回的 metric1 也是两者之和，用 `SetIpInterfaceEntry` 改了接口 metric，读回的 metric1 跟着变。
- 协议：加和改都必须是 3（`MIB_IPPROTO_NETMGMT`），否则返回 87；删不看协议。
- 不看的字段：type、policy、age、next hop AS、metric2–5。读回时，有网关的路由 type 是 4，在链路上的是 3。
- 下一跳：加路由时，0 或网卡自己的地址表示目标就在链路上。新函数读回的下一跳是 0，旧函数读回的是网卡自己的地址，Windows 另外加一条协议 2、metric 256 的 /32 广播路由。改和删时，0 或网卡自己的地址表示「任意下一跳」，取目标、掩码、接口都相同的第一条路由；写了具体网关就必须完全一致，否则返回 1168。
- 删不看 metric（metric 0 也能删）。
- 不连续的掩码、目标带主机位：87。接口为 0 或不存在：2。UU 那种接口 `0xffffffff`、其余全 0 的删除也返回 2，路由不删。重复加：5010。改或删不存在的路由：1168。参数为 NULL：87。
- 下一跳不在任何直连网段里（203.0.113.1，或在目标网段之内的地址），Windows 也接受。

## 修复

三个旧函数把 `MIB_IPFORWARDROW` 换成 `MIB_IPFORWARD_ROW2`，交给 0015 实现的 `CreateIpForwardEntry2`、`SetIpForwardEntry2`、`DeleteIpForwardEntry2`：

- 掩码换成前缀长度，不连续时返回 87；接口序号换成 LUID，换不了时返回 2；
- 下一跳是 0 或网卡自己的地址时：加路由当作在链路上（下一跳 0）；改和删从 `GetIpForwardTable2` 里找同一接口、同一前缀的第一条路由，用它的下一跳；
- 加和改：协议必须是 3；metric1 减去接口 metric 得到路由 metric，校验同上。接口 metric 从 nsiproxy 的接口表读，是 0015 记下来的值，没设过时为 0；
- 删：不看协议和 metric；
- `GetIpForwardTable` 读回的 metric1 加上接口 metric，原来那里是 `FIXME: add interface metric`；
- 去掉三个函数注释里过时的「Stub」。

## 验证

1. 探针：Wine 在 shorin-proton-wrapper 的独立网络命名空间里跑，不碰主机的路由。
   - 修复前（we-proton-ge11-7-25，`iphlp_forward1_probe.wine25.txt`）：所有调用都返回 0，路由表里一条也没有。
   - 修复后（`iphlp_forward1_probe.wine25-0053.txt`）和 Windows 逐行比较，返回码和读回的路由一致，只有下面几处不同：
     - Wine 的接口 metric 是 0（测试机上 M 是 15）。所以 metric 0、1 能加上（Windows 返回 160），M−1 回绕成 0xffffffff，返回 87；metric 9999 读回 9999，Windows 读回 9984（9999 − M）。按 M = 0 算，这些都对。
     - 下一跳不在直连网段里（203.0.113.1、目标网段内的地址）时返回 1231（`ERROR_NETWORK_UNREACHABLE`），Windows 能加上：Linux 内核要带 `onlink` 才接受这样的网关。
     - 同一个目标再加一条下一跳不同的路由时返回 5010，Windows 能加两条：Linux 同一个表里同一前缀、同一 metric 只能有一条。后面「两条路由时用下一跳 0 删除」一项也就跟着不同。
     - 在链路上的路由读回协议 2（Windows 是 3），也没有 /32 广播路由：这是 nsiproxy 读路由表的方式，和这个补丁无关。
2. 网易 UU（2026-10-12，`~/.proton`，独立网络，测试 runner we-proton-ge11-7-25-0053）：
   - 第一版（metric 原样传，协议写死成 3，见下面的弯路）：一次加速加 194 次、删 10 次，全部进了 Linux 路由表，69 条走 `UU`，59 条走真实网卡。在命名空间里 `curl https://store.steampowered.com/`，`UU` 网卡的收发各多了约 1 MB，页面是 0.9 MB。
   - 现在的版本：加 257 次、删 30 次。中途停过一次加速，停止时按具体网关删掉了那 10 条 /24，重新加速后又加了回来。71 条走 `UU`，商店页面同样经 `UU` 传输。
   - 命名空间里的 Linux Steam（`shorin-proton-wrapper steam`）：商店、社区的连接走 `UU`。
3. 回归：`we/tests/regress.sh` 的结果和 we-proton-ge11-7-25 全部相同。

## 排查过程和弯路

- 开始以为 UU 没认出 Steam 的进程。路由模式不按进程挑流量，看了 `+iphlpapi` 日志才发现加路由的调用全是 stub。
- 第一版把 metric1 直接当路由 metric，协议写死成 3，下一跳只处理了网卡自己的地址。在 Windows 上实测以后，按上面的规则改成了现在的版本：metric1 要减去接口 metric，协议必须是 3，改和删时下一跳 0 或本机地址表示任意下一跳。Wine 的接口 metric 默认是 0，UU 传的协议又都是 3，所以对 UU 来说两版的结果相同。

## 没处理、没验证

- 下一跳不在直连网段里的路由：要在 0015 的 nsiproxy 里给 netlink 加 `RTNH_F_ONLINK`。目前没有程序用到。
- 同一前缀多个下一跳：Linux 要用多路径或 `NLM_F_APPEND`。目前没有程序用到。
- 在命名空间里，网易 UU 的 Steam 加速只放行商店、社区等名单里的域名（按 TLS 握手里的域名，和走哪个 IP 无关）。在海外，Akamai 用同一批 IP 同时服务商店页面和 `store.akamai.steamstatic.com` 等静态资源，这些资源被卷进隧道后连接被 UU 断开，商店页面加载不出来。这是 UU 那边的规则，Windows 上在海外是否一样没测。
