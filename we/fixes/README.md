# 修复记录

每个补丁一份报告：现象、根因、依据（能在 Windows 上实测的都实测过）、修复、验证、排查过程和弯路。

从 `we-proton-ge11-7-*` 起 WE-Proton 以 GE-Proton 为底（见 [`../ge-dw-merge.md`](../ge-dw-merge.md)），下表中标了「这一版没有」的补丁不再打，报告留作记录。

| 补丁 | 问题 | 报告 |
|---|---|---|
| wine 0001（已由 0018 撤回；这一版没有） | 第三方 KMDF 驱动加载失败：`ZwQueryValueKey` 不容忍值名末尾的 NUL，另有三个函数未实现 | `0001-ntoskrnl-driver-functions/` |
| wine 0002 | wineserver 在 `attach_thread_input` 里释放后使用，整个前缀一起崩溃 | `0002-wineserver-thread-input-uaf/` |
| wine 0003 | 原生 `syscall` 指令返回时写坏 `[rsp-8]` | `0003-native-syscall-red-zone/` |
| wine 0004 | UDP socket `connect(0.0.0.0)` 的语义和 Windows 不同，靠它唤醒的 `select()` 要等满超时 | `0004-udp-connect-unspecified/` |
| wine 0005 | 写过的写时复制页仍报告 `PAGE_WRITECOPY`，CEF 渲染进程的断言失败 | `0005-write-copy-tracking/` |
| wine 0006 | 活动窗口消失后，Wine 会把 `WS_EX_NOACTIVATE` 窗口设成前台 | `0006-noactivate-activation/` |
| wine 0007 | 分层子窗口被做成单独的 override-redirect 顶层窗口，位置按屏幕原点算、不跟父窗口走 | `0007-layered-child-windows/` |
| wine 0008 | 类型库封送把没有方向的参数只当输入，进程外 COM 服务器通过这类参数返回的结果全部丢失 | `0008-typelib-param-direction/` |
| wine 0009 | d3dcompiler 的反射读不出最低精度签名（ISG1/OSG1/PSG1），按反射结果拼输入布局的程序得到空布局，创建管线失败 | `0009-d3dcompiler-min-precision-signatures/` |
| wine 0010 | WinHttpAddRequestHeaders 遇到开头的空行就返回 ERROR_WINHTTP_INVALID_HEADER，自定义请求头加不上，请求发不出去 | `0010-winhttp-leading-empty-lines/` |
| wine 0011 | 通知区图标只能嵌进 XEmbed 系统托盘，在只支持 StatusNotifierItem 的面板（waybar 等 Wayland 面板）上看不到，隐藏到托盘的程序叫不回来 | `0011-explorer-status-notifier-items/` |
| wine 0012（这一版没有：GE-Proton 的 `qcap-dshow-fixes` 修了同一个 bug） | WoW64 下视频采集返回给 32 位程序的媒体类型带着未初始化的 `pUnk`，枚举摄像头格式的程序（如 Chromium/CEF）释放时崩溃 | `0012-qcap-wow64-media-type-punk/` |
| wine 0013 | 用私有命名空间（`CreatePrivateNamespace`/`OpenPrivateNamespace`、边界描述符）做单实例检测的客户端在 Wine 里一启动就崩 | `0013-private-namespaces/` |
| wine 0014 | 线程调用 `ImmDisableIME` 后，宿主输入法提交的文字被丢掉，靠 TSF 收输入法文字的程序能看到候选框，但打不了中文 | `0014-ime-disabled-thread-text/` |
| wine 0015 | 设置网卡参数、给网卡加地址和路由的 iphlpapi 函数没有实现，程序配置虚拟网卡时失败 | `0015-iphlpapi-set-path/` |
| wine 0016 | 用 wintun 接口创建虚拟网卡的程序（VPN、游戏加速器等）在 Wine 里没有实现，建网卡一步失败 | `0016-wintun-dll/` |
| wine 0017 | 用 `IP_UNICAST_IF` 指定出口网卡的程序（TUN 模式的加速器/VPN）在 Wine 里 TCP 仍按路由表走，“直连”流量被自己的虚拟网卡吃掉，表现为整个会话断网 | `0017-ip-unicast-if-tcp/` |
| wine 0018（这一版没有） | 撤回 0001：带上 0001 时有程序报运行环境异常，官方包不报 | `0001-ntoskrnl-driver-functions/` |
| wine 0019 | 撤回 0001 后《王者万象棋》闪退：驱动调用的 `InbvAcquireDisplayOwnership`/`InbvResetDisplay` 在 Wine 里是会 abort 的 stub，只把这两个空操作加回来 | `0019-ntoskrnl-inbv-display-functions/` |
| wine 0020 | 《暗区突围：无限》进游戏两分多钟后闪退：查询窗口层级的 `NtUserGetWindowBand` 在 Wine 里是一调用就中止进程的 stub | `0020-win32u-window-band/` |
| wine 0021 | 《诡秘之主》启动时提示缺少 Microsoft Visual C++ 2015-2022 Redistributable (x64)：引导程序要求注册表和 DLL 的版本都 ≥ 14.44.35211，Wine 内置的是 14.42.34433；把内置 DLL 的版本号改成 14.99.65535 | `0021-vcruntime-version/` |
| wine 0022 | 同上，注册表那一项：`wine.inf` 不覆盖已有值，旧前缀的运行库版本停在建前缀时的值；wineboot 每次启动把它提到 `msvcp140.dll` 的版本 | `0021-vcruntime-version/` |
| wine 0023 | 《诡秘之主》的启动器（Electron）切几次焦点就点不动：鼠标落在透明背景上时程序把窗口设成鼠标穿透，靠低级鼠标钩子看鼠标何时回来，而 XWayland 下 X 收不到穿透窗口上的指针；普通窗口穿透时仍然接收鼠标 | `0023-winex11-input-shape/` |
| wine 0024–0042、vkd3d-proton 0002、dxvk 0002–0007（这一版没有：GE-Proton 直接带着原版） | 从 GE-Proton 导入的通用修复和按游戏的兼容处理（没有逐个审代码） | `0024-ge-proton-imports/` |
| wine 0048 | 内置 wintun 报的网卡描述是 Linux 接口名，不是 Windows 的 `<tunnel type> Tunnel`：按描述找自己建的虚拟网卡的程序找不到网卡，网易 UU 的路由模式报 50004 | `0048-wintun-adapter-description/` |
| wine 0049 | GE-Proton 在 win32u 里的托盘实现先于 explorer 接手图标，转图标时只要颜色平面有 alpha 就不看 AND 掩码，WeGame 的托盘图标透明处变成黑色；让 win32u 不接手，交给 0011 | `0049-win32u-tray-to-explorer/` |
| wine 0050 | Wayland 驱动按窗口样式推算全屏，带标题栏或可拖动边框的窗口永远不算全屏，合成器发起的全屏（快捷键）被立刻撤销；交帧的线程用 configure 之前的状态也会撤销。WeGame 窗口、《封印者》的窗口模式按全屏快捷键马上恢复原样 | `0050-winewayland-compositor-fullscreen/` |
| wine 0052 | Wayland 驱动在窗口拿到键盘焦点后还向合成器申请激活，niri 分支的网格概览收到申请就关掉：焦点一移到 Wine 窗口网格就没了 | `0052-winewayland-no-activation-on-focus/` |
| wine 0053 | 网易 UU 加速器的路由模式加速不上：它用旧的 `CreateIpForwardEntry`/`DeleteIpForwardEntry` 加删路由，Wine 里这几个函数什么都不做就返回成功，路由一条也没加上 | `0053-iphlpapi-old-route-functions/` |
| wine 0054 | 没写过的写时复制页，`VirtualProtect` 报的旧属性是 `PAGE_READWRITE`（Windows 是 `PAGE_WRITECOPY`）：GE-Proton 带的 staging `ntdll-WRITECOPY/0010` 一律改写，把 0005 给出的正确结果改错；改写只留给 0005 跟踪不了的情况 | `0054-kernelbase-writecopy-old-protection/` |
| wine 0055 | 释放 Media Foundation 的读取器后马上 `MFShutdown` 会卡死：rtworkq 关系统队列时拿着队列表的锁等回调结束，读取器最后一次释放恰好在回调里、也要这把锁（上游的隐患，GE-Proton 的视频补丁把它触发出来）；等回调时不再拿这把锁 | `0055-rtworkq-shutdown-deadlock/` |
| wine 0056 | 窗口激活时 `WM_ACTIVATEAPP` 的 lParam 不对：跨线程、跨进程激活时原版 Wine 报 0，GE-Proton 的补丁改成收消息的线程自己；Windows 实测报上一个前台窗口的线程 | `0056-win32u-activateapp-lparam/` |
| protonfixes 0051 | 前缀里已有程序在运行时，再用 `proton run` 加入的游戏拿不到 protonfixes 的修正：《战锤40K：暗潮》的修正是绕开启动器，没绕开时启动器把游戏隐藏启动，窗口出不来；设 `PROTONFIXES_RUN_VERB=1` 时 `run` 也执行修正 | `0051-protonfixes-run-verb/` |
| vkd3d-proton 0001 | dxil-spirv 生成非法的结构化控制流，RADV 拒绝后崩溃 | `vkd3d-0001-dxil-spirv-loop-breaks/` |

## 已知、没修的问题

- **Media Foundation 里没有 MPEG-1/2 视频和 MP2 音频的解码器**（DW 底座也一样，Windows 有）：DirectShow 能放
- **wineserver fsync 断言**（`fsync.c:271`，`fsync_free_shm_idx`）：进程在短时间内反复异常退出时触发，wineserver 退出，前缀里的进程卡住。只在一次崩溃循环里见过
- **N 卡渲染、核显显示时交帧卡住**（niri 不用 N 卡，屏幕接在核显上）：D3D11 程序在 N 卡上约 2 fps，渲染线程在等交帧。见 `0006-noactivate-activation/`
- **xwayland-satellite 不执行最小化**：会在失去前台时最小化自己的程序会进入「最小化 → 被恢复」循环
- **自绘边框、不让拖大小的窗口在平铺合成器下被平铺**：这类窗口带 `WS_THICKFRAME`，靠 `WM_NCHITTEST` 不返回边框来禁止拖大小，Wine 只能当成可调大小，niri 会把它平铺、程序按平铺尺寸重新排版。不是 Wine 能判断的，要用合成器的窗口规则。见 `0007-layered-child-windows/`
- **RADV 在 `spirv_to_nir` 失败时解引用 NULL**（Mesa 26.2.4，`radv_shader.c:544`）：本该只是一处渲染失败，结果整个进程崩溃。触发它的非法 SPIR-V 已经由 vkd3d-proton 0001 修掉

## 做法

- 只按精确 PID 结束进程（先核对 `/proc/<pid>/cmdline`），不用 `pkill -f` 之类的模式匹配
- 「Windows 到底怎么做」先在 Windows 上用探测程序实测，再写补丁（`we/tests/*.windows.txt`）
- 先用诊断日志看程序实际在做什么，不凭推测写补丁（0004 第一版就是这样写错的）
- 改 runner 或程序文件前先备份，并能一键还原；装二进制之后比对校验和
