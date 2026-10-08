# wine 0016：内置 wintun.dll（wintun 用户态隧道接口）

## 现象

一些把流量导进隧道的程序（VPN、游戏加速器）在 Windows 上用 wintun 接口创建一块虚拟网卡（TUN），再给这块网卡配地址和路由。它们在 Wine 里跑的时候，程序自带的 `wintun.dll` 会去装一个 Windows 内核驱动（驱动服务、`.inf`、`oemN.inf`），这在 Wine 里装不上；程序走到"创建虚拟网卡"这一步就失败，表现为连接/加速卡住然后超时。

Wine 里此前既没有 wintun 的实现，也没有打开 Linux TUN 设备的代码（`grep -r TUNSETIFF dlls/` 是空的）。

## 根因

wintun 不是必须依赖内核驱动才能提供的接口：它是 WireGuard 项目发布的一套公开 API（`wintun.h` 公开，官方 DLL 负责在 Windows 上装驱动）。要让用它的程序在 Wine 里工作，只需要提供同名的内置 DLL，把这套 API 映射到 Linux 内核自带的 `/dev/net/tun` 上。

## Windows 实测（依据）

在 Windows 11 测试机上用 `we/tests/wintun_probe.c` 加载体外程序自带的那份 `wintun.dll`（32 位；导出与 WireGuard 官方一致，序号 1..14 按名字排序）。原文见 `we/tests/wintun_probe.windows.txt`：

- 驱动版本：没装驱动时返回 0；建过一次适配器后返回 `0x0000000e`（=14，编码是 `MAKELONG(minor, major)`）；
- `WintunCreateAdapter` 用同一个名字再建会替换旧实例并返回新句柄；建完后在 `GetAdaptersAddresses` 里 FriendlyName 是创建时给的名字，Description 是 `<TunnelType> Tunnel`，IfType=53（`IF_TYPE_PROP_VIRTUAL`），OperStatus=2；
- 容量校验：0、0x1000、0x30000（不是 2 的幂）、0x8000000（超过 64 MB）都返回 NULL + 87（`ERROR_INVALID_PARAMETER`）；0x20000 可以；
- 会话已存在时再 `WintunStartSession` 返回 NULL + 1247（`ERROR_ALREADY_INITIALIZED`）；
- 读等待事件是自动复位（同步）事件（`NtQueryEvent` type=1），初始未触发；
- 空会话 `WintunReceivePacket` 返回 NULL；`WintunAllocateSendPacket(0x20001)`（容量 0x20000）返回 NULL + 111（`ERROR_BUFFER_OVERFLOW`）；
- 关掉最后一个句柄后按名字打开返回 NULL + 1168（`ERROR_NOT_FOUND`）；
- `WintunDeleteDriver` 会真的卸载驱动（日志里 `Removing driver oem8.inf`）并返回 TRUE。

## 修复

新增内置模块 `dlls/wintun`（PE + unixlib），导出和程序那份 DLL 同名同序的 14 个函数：

- **建/开适配器**（unix 侧）：把传入的名字规范化成合法 Linux 接口名（只留 `[A-Za-z0-9_-]`、最长 15 字节，太长用 FNV-1a 哈希），`open("/dev/net/tun")` + `TUNSETIFF(IFF_TUN|IFF_NO_PI|IFF_PERSIST)`，把接口拉起来。按接口名做引用计数：同名再建复用同一块网卡。名字是确定性的，进程崩了留下的 persist 网卡还能被 `WintunOpenAdapter` 找回来；关掉最后一个句柄时撤销 persist 并关闭（删掉网卡）。
- **LUID**：用 `ConvertInterfaceIndexToLuid(ifindex)` 取 Wine 的 NDIS 表给这块网卡的 LUID，保证 `SetIpInterfaceEntry`、`CreateUnicastIpAddressEntry`、`CreateIpForwardEntry2`（0015）认得同一块网卡；`WintunGetAdapterLUID` 返回同一个值。
- **会话**：收到和待发的包各自记账，未释放总量按 capacity 卡住（语义与真 wintun 的环形缓冲一致：指针有效期到 `Release`/`Send` 为止）；`WintunGetReadWaitEvent` 返回自动复位事件，每条会话一个等待线程（unix 侧 `poll` + 唤醒管道，PE 侧 `SetEvent`；用内部 consume 事件避免忙等），`WintunEndSession` 叫醒线程退出。
- **32 位程序**：参数结构不依赖指针宽度（64 位字段在前、地址按 `UINT64` 传），同一组函数同时挂在 wow64 调用表上，所以 32 位引擎在新 WoW64 下可以直接用。
- **版本号和错误码**按上面的实测对齐：未建过适配器返回 0、建过返回 `0x0000000e`；重复开会话 1247；打开不存在的适配器 1168；容量校验 87；分配超限 111。

## 验证

1. `we/tests/wintun_probe.c` 在 Windows（程序自带 DLL）和 Wine（本补丁的内置 DLL；r14 runner + 独立前缀，在 pasta 命名空间里）各跑一遍，逐行比对：`we/tests/wintun_probe.windows.txt` / `we/tests/wintun_probe.wine14.txt`。导出、LUID 一致性、容量/重复会话/分配超限/打开不存在的错误码、事件类型、会话流程全部一致（差异见下节）。
2. 回环测试：probe 的 `loopback` 模式把一条 ICMP echo request 交给 `WintunSendPacket`，Linux 内核网络栈收到后生成 echo reply，再由 `WintunReceivePacket` 读回（`we/tests/wintun_probe_loopback.wine14.txt`，`echo-reply=yes`）。收发两条路都真的通了，走的是 32 位 probe（和引擎一样）。
3. 把补丁按号序（0001..0014、0016）在干净源码树上用 `patch -Np1` 逐个应用，全部干净通过；`we/overlay-build.sh` 认 `dlls/wintun` 并把它列进构建目标。

## 已知差异（有意或环境造成）

- 适配器在 `GetAdaptersAddresses` 里的描述/IfType/OperStatus 与 Windows 不同：Wine 的 NDIS 表按 Linux 接口名和 ARPHRD 生成（描述=接口名，IfType 被映射成 23=PPP，OperStatus=1）。如果程序按"描述"找网卡，还要在 nsiproxy 那侧加一处映射，目前没做（未确认引擎是否需要）。
- 驱动版本是进程内状态：Windows 上驱动装了就一直在（换进程也是 0x0e），Wine 里每个进程从 0 开始、第一次建适配器后变成 0x0e。两者都会让程序走到 `WintunCreateAdapter`。
- `WintunSetLogger` 只保存回调，不像 Windows 那样打安装/创建消息。
- Wine 下新建的 TUN 上内核会先发几个 IPv6 路由器请求（probe 排空阶段能看到 48 字节的 ICMPv6 包），Windows 的适配器此时没有流量。环境差异，不影响。
- `WintunCreateAdapter` 同名再建：Windows 替换旧实例（之后按名字 `Open` 会 1168），我们复用同一个实例（旧句柄继续有效）。两边都是"建完能用"。

## 排查过程（弯路）

- 内置模块第一次跑时所有 unix 调用都失败：`__wine_init_unix_call()` 填的是模块自己的 `__wine_unixlib_handle`，我一开始用了自己声明的零初始化变量，`__wine_unix_call` 收到句柄 0。改用 `WINE_UNIX_CALL` 宏。
- unix 侧要先 `#include "ntstatus.h"` 再 `#define WIN32_NO_STATUS`，否则 `STATUS_*` 和 winnt.h 里的定义重复；PE 侧 `netioapi.h` 之前要先包含 winsock2/ws2ipdef/ifdef/nldef。
- 这个树里 `tools/make_makefiles` 会因为一个无关的 `.idl` 报错退出，`WINE_CONFIG_MAKEFILE(dlls/wintun)` 是手工补的；`we/overlay-build.sh` 也要给新模块加 TARGETS 条目，并放行 `configure.ac`/`include/*` 这类不产出二进制文件的路径。
- probe 里直接把函数调用和 `GetLastError()` 写进 `printf` 参数，求值顺序不定，错误码会被读早或读晚；已拆成独立语句。
- 回环包一开始被内核丢掉：IP/ICMP 校验和按主机序写了，应该按网络序。

## 未验证

- 真实引擎（TUN 模式）实机走通：需要 0013（私有命名空间）和 0015（iphlpapi 设置通路）也进同一个 runner 才能测。
- 描述/IfType 是否要与 Windows 完全一致，以及引擎是否按描述找网卡。
- 会话在单读单写以外的并发用法（wintun 本身就规定单人收发，未额外验证）。
- 非 Linux 平台的 unix 实现（返回 `ERROR_NOT_SUPPORTED`）。
