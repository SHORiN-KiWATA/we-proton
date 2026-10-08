# wine 0017：IP_UNICAST_IF 对 TCP 也生效

## 现象

用 TUN 模式的程序（加速器、VPN）把网关/默认路由指到自己的虚拟网卡之后，它自己"不走隧道"的连接需要从物理网卡出去。Windows 上这是用 `setsockopt(IP_UNICAST_IF, 网络序索引)` 做的；在 Wine 里这个调用**成功**了，但 TCP 连接还是按路由表走，于是这些连接又被送进自己的虚拟网卡，形成环路：加速器日志里所有 `@direct` 连接都是 `down:0B`、`reason:dropped`，程序（和整个会话）表现为完全没有网络。

## 依据

- 在 Wine 里抓引擎的 strace：`setsockopt(166, SOL_IP, IP_UNICAST_IF, [33554432], 4) = 0`（33554432 = 网络序的 2，成功）。
- 手工验证 Linux 语义（在同一个网络命名空间里，路由里放一块诱饵网卡）：
  - 设 `IP_UNICAST_IF` 之后 TCP `connect()` 的源地址仍是诱饵网卡的地址（`172.30.0.2`），说明 Linux 的 `IP_UNICAST_IF` **只影响 UDP**；
  - 换成 `SO_BINDTODEVICE` 后源地址立刻变成物理网卡的地址（`192.168.0.55`）。

## 修复

`dlls/ntdll/unix/socket.c`：`IOCTL_AFD_WINE_SET_IP_UNICAST_IF` 在处理时除设置 Linux 的 `IP_UNICAST_IF` 外，再按接口名 `setsockopt(SO_BINDTODEVICE)`。顺序是先设选项、后绑设备，这样 `getsockopt(IP_UNICAST_IF)` 仍然能读回索引（先绑设备会让原始选项设不进去）。设备绑定失败（比如没有权限）时退回原来的行为，不报错。

## 验证

`we/tests/unicast_if_probe.c`：在 pasta 网络命名空间里放一块诱饵网卡 `dummy0` 并把 `0.0.0.0/1`、`128.0.0.0/1` 指向它（复刻加速器的路由接管），然后：

| | 设选项前 | 设选项后 |
|---|---|---|
| 修复前（r15 的 ntdll.so） | 源地址 172.30.0.2（诱饵） | **还是** 172.30.0.2 |
| 修复后 | 源地址 172.30.0.2 | **192.168.0.55（物理网卡）** |

`getsockopt(IP_UNICAST_IF)` 在修复后读回 `33554432`（网络序索引）不变。

## 未验证

- Windows 上 `IP_UNICAST_IF` 对 TCP 的语义：测试机只有一块网卡，源地址看不出来。能确认的是：
  - Windows 接受网络序的索引（设 `htonl(4)` 成功），这点和引擎传的值一致；
  - `getsockopt` 在 Windows 上读回的是**主机序**的索引（设 67108864 读回 4），而 Wine 读回的是网络序（和设置时一致）——这是 Wine 原有的行为，本补丁没动它。
- IPv6 的 `IPV6_UNICAST_IF`（Linux 上它设置的就是绑设备，看起来不需要同样处理，但没实测）。
- 只绑设备会让该 socket 只收该设备上的包；对“指定了出口接口”的程序来说这是想要的行为，但没有逐个程序验证。

## 客户端验证（2026-10-09）

带 0017 的 runner（we-proton-11.0-14-16）重开加速器后，整个会话（WeGame 本身、客户端自己的连接）的网络保持正常，不再“完全没网”。引擎日志里那些 `@direct` 连接开始有下行字节（`down:57B`/`42B`/`150B`…，修复前同一条日志里 16700 条都是 `down:0B`），`reason:dropped` 也大幅减少。
