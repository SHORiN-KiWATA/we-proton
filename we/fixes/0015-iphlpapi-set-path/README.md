# wine 0015：iphlpapi 的设置通路（接口参数、单播地址、路由）

## 现象

程序要配置网卡时（`SetIpInterfaceEntry` 改 MTU/metric、`CreateUnicastIpAddressEntry` 加 IP、`CreateIpForwardEntry2` 加路由），Wine 里这些函数要么是 stub 要么没有实现，返回"未实现"。需要先建一块虚拟网卡、再给它配地址和路由的程序（VPN、加速器的 TUN 模式）走到这一步就失败或崩溃。

## 依据

`we/tests/iphlp_set_probe.c` 在 Windows 测试机上实测（用保留地址段 TEST-NET-2 `198.51.100.0/24`，不碰真实网络），输出见 `iphlp_set_probe.windows.txt`：

- 三个 `Initialize*` 的默认值（0、`0xffffffff`、`0xff` 等）和结构布局；
- `SetIpInterfaceEntry`：原样写回 → 87；`SitePrefixLength=1` → 0；`luid=0` + index → 0；`luid=0` + index=0 或 family=0 → 87；把 `Initialize*` 出来的值直接写 → 0；metric+7 写回后生效且 `UseAutomaticMetric` 被清 0；同一 MTU 再设 → 0；
- `CreateIpForwardEntry2`：重复加（含不同 metric）→ 5010；接口不存在 → 2；前缀长度 33 或主机位非 0 → 87；加完能 `GetIpForwardEntry2` 读回、`SetIpForwardEntry2` 改 metric、能按 index 建/删；删两次第二次 → 1168；
- `CreateUnicastIpAddressEntry`：重复 → 5010；加完能读回，并生成 on-link 路由；`SetUnicastIpAddressEntry` 改前缀长度 → 0；删两次第二次 → 87；接口不存在 → 2。

## 修复

把"设置"这一路从 iphlpapi 打通到 unix 侧（Wine 此前只有读取）：

- iphlpapi：实现 `SetIpInterfaceEntry`、`Create/Get/Set/DeleteUnicastIpAddressEntry`、`Create/Get/Set/DeleteIpForwardEntry2` 和三个 `Initialize*`，参数校验和错误码照上面的实测；
- nsi.dll：`NsiSetAllParameters`/`NsiSetAllParametersEx` 不再是 stub，走同样的设备接口；
- nsiproxy.sys：各表加 `set_all_parameters` 通路；ip 模块用 netlink 真正改 Linux 的接口参数（MTU、metric）、单播地址和路由，接口 metric 记在本地表里读回来；
- 顺带修一个 Wine 原有 bug：`/proc/net/route` 的 Metric 列是十进制，原来按十六进制解析，主机上 metric 100 的默认路由会被读成 256。

## 验证

`iphlp_set_probe` 在 Windows 和 Wine（网络命名空间里的一块假网卡，`netns-probe.sh` 起）各跑一遍，逐行比对（`iphlp_set_probe.windows.txt` / `iphlp_set_probe.wine.txt`）。所有写操作和返回码一致；跑完后命名空间里的 `ip route`/`ip addr` 能看到加进去又删掉的条目（输出末尾那段）。

读回值还有几处差异，都在 Wine 原有的读取代码里，不属于这次加的设置通路：

- `GetIpInterfaceEntry` 的读取路径填不全字段（Windows 的 `maxreasm=0`、`ifid=0`、`zones=4,4,4`、`connected=1` 等，Wine 给的是 `0xffffffff`/0 的默认值）；
- metric 写回后 `UseAutomaticMetric` 读回是 1（Windows 0）；
- 路由读回的 `proto`/`immortal` 不同（Windows 3/0，Wine 2/1）；
- 单播地址读回的 `prefix_origin`/`valid`/`pref` 是 DHCP 默认值（Windows 1/`0xffffffff`）。

## 未验证

- IPv6 的地址和路由；
- 除 `SitePrefixLength`、metric、MTU 之外更多接口参数的写路径；
- 并发修改同一张表的竞态（只做了单线程验证）。

## 客户端验证（2026-10-08）

用带 0015 的 runner（we-proton-11.0-14-15）跑客户端的加速引擎，线路模式三加速成功；命名空间里能看到引擎给网卡配的 `172.21.0.2/24`，以及拆分的默认路由（`1.0.0.0/8` … `128.0.0.0/1` 走建出来的那块网卡），说明设置通路（netlink）真的生效。
