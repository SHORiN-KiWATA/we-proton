# 0048：wintun 网卡的描述改成 Windows 的 "<tunnel type> Tunnel"

补丁：`patches/wine/0048-wintun-Report-the-adapter-description-Windows-prog.patch`（`dlls/wintun/unixlib.h`、`dlls/wintun/wintun.c`、`dlls/wintun/unixlib.c`、`dlls/nsiproxy.sys/ndis.c`）。

## 现象

网易 UU 加速器 6.19.0 用「路由模式」加速失败，客户端弹「加速断开，发现 50004 错误」，加速前后的网络命名空间里没有出现 UU 的虚拟网卡。

## 定位

1. `uu.exe` 里产生 50004 的代码只有一处：`RouteTable::AddAllRouteItem` 的 `vpn adapter not ready` 分支。它用 `GetAdaptersInfo` 枚举网卡，找 **Description 含 `Wintun`** 并且 IPv4 是 `172.19.84.237` 的那块（关键字符串和地址常量都是从程序里反汇编出来的）。
2. `tun2proxy.dll` 建网卡调用的是 `WintunCreateAdapter(L"UU", L"UU Wintun", &guid)`：网卡名是 `UU`，tunnel type 是 `UU Wintun`。Windows 上 wintun 驱动报的 Description 是 `<tunnel type> Tunnel`（= `UU Wintun Tunnel`，实测见 0016 的报告），UU 就是按这个描述找自己建的网卡。
3. Wine 的内置 wintun 忽略了 `tunnel_type`：nsiproxy 把 Linux 接口名（由网卡名得来）当 Description，所以 UU 看到的是 `UU`，关键字对不上，检查失败。
4. 探针实测（32 位、pasta 命名空间、新建前缀；`/tmp/uu-probe/adapter_probe*.exe` 的输出）：
   - `WintunCreateAdapter(L"UU Wintun", L"Wintun")`：`GetAdaptersInfo` 里 `desc="UU_Wintun"`，含 `Wintun`，UU 的检查会过；
   - 换成 UU 的真实参数 `L"UU"` + `L"UU Wintun"`：`desc="UU"`，不含 `Wintun` → 50004。
   两次 `WintunCreateAdapter`、`CreateUnicastIpAddressEntry(172.19.84.237)` 和 IP 检查都成功，唯一差异就是描述。

## 修复

- 建网卡时把 Windows 风格的描述写进 Linux 接口的 alias（`IFLA_IFALIAS`）：unix 侧通过 netlink 发 `RTM_NEWLINK`。
- nsiproxy 填 `MIB_IF_ROW2.Description`（`GetAdaptersInfo` / `GetAdaptersAddresses` 的 Description 都从这里来）时优先用 alias，没有 alias 的网卡照旧用接口名；alias 通过 netlink `RTM_GETLINK` dump 读回（每次枚举网卡时刷新）。
- 接口名、FriendlyName、`WintunOpenAdapter` 按名字开网卡的语义都不变。

两边都走 netlink 是被环境逼出来的：Steam Runtime 的容器里 `/sys` 是只读挂载，而且容器里的 `/sys/class/net` 视图不跟着进程的 pasta 网络命名空间走。第一版用 sysfs 写 alias，探针里 netlink 确认 alias 写进去了，但 nsiproxy 读 `/sys/class/net/UU/ifalias` 却是 "no such file"，所以改成 netlink 读。

## 验证

1. 探针（32 位、pasta 命名空间、新建前缀、`wintun=b`，用 UU 的真实参数 `WintunCreateAdapter(L"UU", L"UU Wintun", ...)`）：
   - 改之前：`GetAdaptersInfo` 里 `desc="UU"`，不含关键字 → UU 会报 50004；
   - 改之后：`desc="UU Wintun Tunnel"`、`wintun_kw=1`、IP `172.19.84.237` 匹配；`GetAdaptersAddresses` 里 `friendly="UU"`、`desc="UU Wintun Tunnel"`。
2. 用和 `we/tests/wintun_probe.c` 相同的参数（`L"WintunProbe"`、`L"Probe"`）：Wine 现在报 `description=Probe Tunnel`，和 Windows 上实测的 `adapter: name=WintunProbe description=Probe Tunnel` 一致；`friendly` 仍是 `WintunProbe`。
3. 补丁在干净源码树上按序（0016 + 0048）干净应用；`we/overlay-build.sh` 能重新准备源码树并编译出 `dlls/wintun`、`dlls/nsiproxy.sys` 的全部目标。
4. **网易 UU 6.19.0 路由模式实机加速成功**（2026-10-12，release 26 的测试 runner，`~/.proton`）：
   - Wine 追踪：`trace:wintun:WintunCreateAdapter L"UU", L"UU Wintun"` → `created UU, ifindex 3` → `WintunStartSession`；
   - 加速中的网络命名空间里 `UU` 网卡 UP，`alias UU Wintun Tunnel`，地址 `172.19.84.237/24`，收发计数在走（说明隧道真的在传包）；
   - 至此不再报 50004。
   - 同一次运行里 `uunetfilter`（c0000142）、`uupacket`（c0000001）仍然加载失败，但路由模式照样成功：对 PC 路由模式这俩不是硬依赖，DNS 劫持/抓包 uufp 的缺失没有拦住加速（只影响对应的可选能力）。
5. **雷神模式三回归通过**（同一天，安装后的 runner）：它的追踪是 `trace:wintun:WintunCreateAdapter L"utun233", L"utun233"` → `created utun233` → `WintunStartSession 67108864`，加速正常。雷神网卡名和 tunnel type 同名，描述从 `utun233` 变成 `utun233 Tunnel`；它按 LUID/接口名找网卡、不看描述，所以不受影响。

## 未验证

- IfType/OperStatus 仍按 Linux/ARPHRD 生成（23=PPP、1），和 Windows 的 53/2 不同；UU 这次没查这两项，其它程序需要时再单独映射。
