# WE-Proton

基于 [GE-Proton](https://github.com/GloriousEggroll/proton-ge-custom) 和 [DW-Proton](https://dawn.wine/dawn-winery/dwproton) 的个人 Proton，攒一些 Wine（和 vkd3d-proton）的 bug 修复。自己和朋友用，不往上游提。

- 顶层是 GE-Proton 的仓库，从 tag `GE-Proton11-7` 起的分支。子模块版本用 GE-Proton 的，只有 dxvk、vkd3d-proton、protonfixes 换成了 DW-Proton 用的更新版本（比较见 [`ge-dw-merge.md`](ge-dw-merge.md)）
- Wine 子模块不改：`patches/protonprep-valve-staging.sh`（GE-Proton 的脚本）在构建前往 Valve 的 Wine 上打 GE-Proton 的补丁和 staging，最后打 `patches/dwproton/` 和 `patches/wine/`
- `patches/dwproton/`：DW-Proton（`dwproton-11.0-14`）的 Wine 提交里 GE-Proton 没有的那些，冲突解决过。怎么挑的、丢了哪些、冲突怎么解的，见 [`ge-dw-merge.md`](ge-dw-merge.md)
- `patches/wine/`：WE-Proton 自己的 Wine 补丁，编号沿用原来的（中间的空号是这一版不再需要的，见下表）
- vkd3d-proton 的修复是 `patches/vkd3d-proton/we-*.patch`（路径相对 vkd3d-proton 根目录），排在 GE-Proton 的补丁后面
- DW-Proton 的 protonfixes 游戏修复是 `patches/protonfixes/0003`–`0005`（WE-Proton 自己的 protonfixes 补丁是 `0051`），dxvk 的 HDR 检测是 `patches/dxvk/dw-hdr-detection-via-displayconfig.patch`

## 补丁

| 补丁 | 修什么 |
|---|---|
| `wine/0001-ntoskrnl-Implement-a-few-functions-needed-by-third-p` | （这一版没有：和撤回它的 0018 一起去掉了）第三方 KMDF 驱动需要的 `ZwQueryValueKey`（容忍值名末尾的 NUL）、`IoGetBaseFileSystemDeviceObject`、`InbvAcquireDisplayOwnership`/`InbvResetDisplay`、`SeQueryInformationToken` |
| `wine/0002-server-Free-thread-input-attachments-when-either-que` | wineserver 在 `attach_thread_input` 里释放后使用 |
| `wine/0003-ntdll-Return-from-native-syscalls-without-writing-be` | 原生 `syscall` 指令返回时写坏 `[rsp-8]` |
| `wine/0004-server-Follow-Windows-connect-semantics-for-datagram` | UDP socket `connect()` 到全零地址的 Windows 语义 |
| `wine/0005-ntdll-Report-written-write-copy-pages-as-read-write` | 写过的写时复制页报告 `PAGE_READWRITE` |
| `wine/0006-win32u-Don-t-activate-WS_EX_NOACTIVATE-windows-in-pl` | 活动窗口消失后不把 `WS_EX_NOACTIVATE` 窗口设成前台 |
| `wine/0007-win32u-Draw-layered-child-windows-into-their-parent-` | 分层子窗口（`WS_CHILD` + `WS_EX_LAYERED`）画进父窗口，不再变成单独的顶层窗口 |
| `wine/0008-rpcrt4-Marshal-type-library-parameters-without-a-dir` | 类型库封送把没有 `[in]`/`[out]` 的指针参数按 `[in, out]` 处理，和 Windows 一致 |
| `wine/0009-d3dcompiler-Handle-the-ISG1-OSG1-and-PSG1-signature-` | d3dcompiler 的反射和 `D3DGetBlobPart` 能读最低精度签名段（ISG1/OSG1/PSG1），和 Windows 一致 |
| `wine/0010-winhttp-Skip-empty-lines-in-front-of-the-first-reque` | WinHttpAddRequestHeaders 跳过开头的空行（`\r\n` 等），和 Windows 一致 |
| `wine/0011-explorer-Show-systray-icons-as-StatusNotifierItems` | 会话里有 StatusNotifierWatcher 时，通知区图标用 StatusNotifierItem（D-Bus）显示，面板上的点击转成和嵌入式图标相同的回调，气泡变成桌面通知 |
| `wine/0012-qcap-Clear-pUnk-in-media-types-returned-to-32-bit-ca` | （这一版没有：GE-Proton 的 `qcap-dshow-fixes` 修了同一个 bug）WoW64 下视频采集返回给 32 位程序的媒体类型把 `pUnk` 设成 NULL，和 64 位一致 |
| `wine/0013-kernelbase-Implement-private-namespaces` | 实现私有命名空间：边界描述符、`Create/Open/ClosePrivateNamespace`，以及 `<别名>\<名字>` 对象名的解析；用它们做单实例检测的程序不再一启动就崩 |
| `wine/0014-win32u-Deliver-input-method-text-to-threads-with-the` | 线程被 `ImmDisableIME` 关掉 IME 后，宿主输入法提交的文字以 `WM_IME_CHAR` 送到焦点窗口，不再丢掉 |
| `wine/0015-iphlpapi-Implement-setting-interfaces-unicast-addres` | 实现 iphlpapi/NSI 的设置通路（改接口参数、加/删地址和路由），底层用 netlink 真正改 Linux 的网络配置 |
| `wine/0016-wintun-Implement-the-Wintun-userspace-tunnel-driver-` | 用 wintun 接口建虚拟网卡的程序在 Wine 里建不了网卡；内置 wintun.dll 把接口接到 Linux 的 `/dev/net/tun`，出口、LUID 和会话语义按 Windows 实测对齐 |
| `wine/0017-ntdll-Make-IP_UNICAST_IF-work-for-TCP-sockets-as-wel` | 程序用 `IP_UNICAST_IF` 指定出口网卡时，TCP 也真的从该网卡出去（Linux 的选项只管 UDP）；TUN 类程序“直连”的连接不再被自己的隧道吃掉 |
| `wine/0018-Revert-ntoskrnl-Implement-a-few-functions-needed-by-` | （这一版没有）撤回 0001：带上它时有程序报运行环境异常，见 0001 的报告 |
| `wine/0019-ntoskrnl-Implement-InbvAcquireDisplayOwnership-and-Inbv` | 把 0018 一起撤掉的 `InbvAcquireDisplayOwnership`/`InbvResetDisplay` 加回来（空操作）：驱动调用它们时不再 abort |
| `wine/0020-win32u-Implement-NtUserGetWindowBand` | 实现 `NtUserGetWindowBand`（以及 user32 的 `GetWindowBand`）：程序查询窗口所在的 z 序层级时不再被当成未实现函数中止；Wine 只有桌面层级，有效窗口都返回它 |
| `wine/0021-msvcp140-msvcp140_2-vcruntime140_1-Report-a-version-` | 内置 VC++ 运行库（`msvcp140`、`msvcp140_2`、`vcruntime140_1`）的版本号报 14.99.65535，比任何已发布的运行库都高：按最低版本检查运行库的程序不再判它缺失 |
| `wine/0022-wineboot-Keep-the-Visual-C-runtime-version-in-the-re` | wineboot 每次启动时，把 `VC\Runtimes\<arch>` 注册表键里比 `msvcp140.dll` 旧的版本提到它的版本：旧前缀（`wine.inf` 不覆盖已有值）和被写低的键也能跟上 |
| `wine/0023-winex11-Keep-pointer-input-for-click-through-regular` | XWayland 下，带 `WS_EX_TRANSPARENT` 的普通窗口（托管、非置顶）不清空输入区域、不去掉指针事件：X 收不到穿透窗口上的指针，靠低级鼠标钩子或 `GetCursorPos()` 看光标、决定何时恢复输入的程序永远等不到鼠标回来。置顶和非托管窗口照旧穿透 |
| `wine/0024-ntdll-Reuse-the-effective-range-after-clearing-nativ` | （这一版没有：GE-Proton 直接带着原版）（GE）清掉 native view 后重试分配用有效的地址范围，不再必然失败 |
| `wine/0025-msxml3-Add-GB2312-support-for-builtin-character-conv` | （这一版没有：GE-Proton 直接带着原版）（staging）内置 msxml3 认 GB2312 编码 |
| `wine/0026-ntdll-Reserve-some-space-for-top-down-allocation-in-` | （这一版没有：GE-Proton 直接带着原版）（GE，上游 f4c5b041）大地址感知的 32 位程序在 WoW64 下保留顶部地址空间 |
| `wine/0027-winex11-Poll-resize-completion-with-a-busy-message-q` | （这一版没有：GE-Proton 直接带着原版）（GE）拖动/缩放窗口时消息队列一直忙也能结束 |
| `wine/0028-urlmon-Pump-thread-user-messages-during-synchronous-` | （这一版没有：GE-Proton 直接带着原版）（GE）同步 bind 时泵线程的所有消息 |
| `wine/0029-ntdll-Finish-process-exit-instead-of-waiting-on-orph` | （这一版没有：GE-Proton 直接带着原版）（GE）进程退出时不在已退出线程持有的 SRW 锁上永久等待 |
| `wine/0030-win32u-Accept-SPI_SETSTICKYKEYS-SPI_SETFILTERKEYS-as` | （这一版没有：GE-Proton 直接带着原版）（GE）`SPI_SETSTICKYKEYS`/`SPI_SETFILTERKEYS` 返回成功 |
| `wine/0031-kernelbase-Don-t-dereference-NULL-in-CharPrevA-CharP` | （这一版没有：GE-Proton 直接带着原版）（GE）`CharPrevA`/`CharPrevExA` 遇到 NULL 不崩 |
| `wine/0032-server-Fix-numpad-vkey-handling` | （这一版没有：GE-Proton 直接带着原版）（GE）小键盘的 vkey 处理 |
| `wine/0033-ntdll-Read-QueryPerformanceCounter-from-the-TSC-in-u` | （这一版没有：GE-Proton 直接带着原版）（GE）QueryPerformanceCounter 在用户态读 TSC（`WINE_DISABLE_QPC_TSC=1` 关闭） |
| `wine/0034-ntdll-Allow-AI-LIMIT-to-reuse-DXBC-compute-shaders-i` | （这一版没有：GE-Proton 直接带着原版）（GE，按游戏）AI LIMIT 的 DX12 计算着色器回退 |
| `wine/0035-Revert-dwrite-Release-file-streams-when-building-col` | （这一版没有：GE-Proton 直接带着原版）（GE）撤回 dwrite 的“建集合时释放文件流”（Assetto Corsa 的 HUD） |
| `wine/0036-ntdll-Fix-Max-Payne-s-truncated-CPUID-leaf-count` | （这一版没有：GE-Proton 直接带着原版）（GE，按游戏）Max Payne 的 CPUID 叶数 |
| `wine/0037-Import-silence-starcitizen-unsupported-os-from-GE-Pr` | （这一版没有：GE-Proton 直接带着原版）（GE，按游戏）Star Citizen 的“不支持的 Windows”弹窗 |
| `wine/0038-winex11-Keep-Forza-backing-windows-unmapped-on-wlroo` | （这一版没有：GE-Proton 直接带着原版）（GE，按游戏）Forza 的后台窗口在 wlroots 上保持不映射 |
| `wine/0039-Import-unity_crash_hotfix-from-GE-Proton` | （这一版没有：GE-Proton 直接带着原版）（GE）wined3d 路径下 dxgi 的一处 Unity 崩溃 |
| `wine/0040-win32u-Restore-Return-to-Krondor-s-text-bitmap-readb` | （这一版没有：GE-Proton 直接带着原版）（GE，按游戏）Return to Krondor 的文字位图读回 |
| `wine/0041-win32u-Keep-Black-Desert-fullscreen-across-applicati` | （这一版没有：GE-Proton 直接带着原版）（GE，按游戏）黑色沙漠失去焦点时保持全屏 |
| `wine/0042-winex11-Shaped-and-real-alpha-transparency-for-layer` | （这一版没有：GE-Proton 直接带着原版）（GE，默认关闭）桌面覆盖类游戏的分层窗口做形状或真透明 |
| `wine/0048-wintun-Report-the-adapter-description-Windows-prog` | 内置 wintun 把 Windows 风格的网卡描述（`<tunnel type> Tunnel`）写进接口 alias，nsiproxy 报给 `GetAdaptersInfo`/`GetAdaptersAddresses`；按描述找自己建的虚拟网卡的程序（网易 UU 的路由模式，报 50004）不再找不到网卡 |
| `wine/0049-win32u-Leave-notification-icons-to-explorer` | GE-Proton 在 win32u 里另有一套 StatusNotifierItem 托盘实现，会先于 explorer 接手通知区图标；它转图标时只要颜色平面有 alpha 就不看 AND 掩码，靠掩码挖空形状的图标（WeGame 的托盘图标）透明处变成不透明的黑色。让 win32u 不接手，图标交给 0011 的 explorer 实现 |
| `wine/0050-winewayland-Keep-a-fullscreen-state-the-compositor-a` | Wayland 驱动：合成器自己发起的全屏（全屏快捷键）不再被立刻撤销。驱动原来按窗口样式推算全屏，带标题栏或可拖动边框的窗口永远不算全屏，于是马上发 `unset_fullscreen`；交帧的线程拿着 configure 之前的窗口状态更新时也会发。这种全屏只由合成器结束，结束时没给尺寸就恢复进入前的尺寸 |
| `wine/0052-winewayland-Don-t-ask-for-activation-of-a-window-tha` | Wayland 驱动：窗口因为合成器给了键盘焦点而被激活时，不再用 xdg_activation 向合成器再申请一次激活（niri 分支的网格概览收到申请就关掉） |
| `wine/0053-iphlpapi-Implement-CreateIpForwardEntry-SetIpForward` | 实现旧的路由函数 `CreateIpForwardEntry`/`SetIpForwardEntry`/`DeleteIpForwardEntry`（原来是什么都不做、返回成功的 stub，网易 UU 的路由模式靠它们加路由）：换成 `MIB_IPFORWARD_ROW2` 交给 0015 的新函数，metric（减去接口 metric）、协议、下一跳按 Windows 实测处理；`GetIpForwardTable` 读回的 metric 加上接口 metric |
| `wine/0054-kernelbase-Don-t-report-unwritten-write-copy-pages-a` | 去掉 staging `ntdll-WRITECOPY/0010` 在 `VirtualProtect` 里把旧属性 `PAGE_WRITECOPY` 一律改成 `PAGE_READWRITE` 的做法（0005 已经在 ntdll 里报对，0010 把没写过的页改错）；只在 0005 跟踪不了的镜像视图（内核不支持 userfaultfd 异步写保护）上保留这个猜测。依赖 0005 |
| `wine/0055-rtworkq-Don-t-hold-the-queue-list-lock-while-shuttin` | `shutdown_system_queues()` 等系统队列的回调时不再拿 `queues_section`：回调里释放读取器的最后一个引用要拿这把锁去解锁读取器的队列，原来互相等死。另用 `startup_section` 保证初始化和关闭不同时进行，关闭开始后不再懒创建系统队列 |
| `wine/0056-win32u-Report-the-previous-foreground-thread-in-WM_A` | `WM_ACTIVATEAPP(TRUE)` 的 lParam 报上一个前台窗口的线程（前台原来不在 Wine 里时报桌面线程），取代 GE-Proton `game-patches/0001` 报收消息线程自己的做法；`set_foreground_window()` 经 `set_active_window()` 的最后一个参数传下来 |
| `wine/0059-win32u-Keep-top-level-windows-in-place-when-the-virt` | 虚拟屏原点（主显示器左上角）变化时，桌面窗口把所有没最小化的顶层窗口平移同样的量，旧原点取自桌面窗口自己的矩形；原来窗口矩形不动、X 窗口也不动，左边屏上的窗口会被算到虚拟屏外面而撤掉 |
| `protonfixes/0051-protonfixes-Also-run-fixes-for-run-when-PROTONFIXES_` | 设了 `PROTONFIXES_RUN_VERB=1` 时，用 `proton run` 启动的程序也执行 protonfixes（`waitforexitandrun` 要先等 wineserver 退出，往运行中的会话里加程序只能用 `run`）；`PROTONFIXES_DISABLE` 仍然优先 |
| `vkd3d-proton/we-0001-Iterate-loop-break-rewrites-until-no-frozen-loop-is-` | dxil-spirv 生成非法的结构化控制流 |
| `vkd3d-proton/0002-vkd3d-bound-present-waits-during-swapchain-drain` | （这一版没有：GE-Proton 直接带着原版）（GE）销毁交换链时等待呈现有上限，不再卡死 |
| `dxvk/0002`–`0007` | （这一版没有：GE-Proton 直接带着原版）（GE）分层覆盖窗口的 DXVK 一半、获取图像受阻时保留交换链、《最终幻想14》多屏失焦不缩窗（三个）、黑色沙漠的 DXVK 一半 |

每个补丁的现象、根因、Windows 实测、验证和排查过程见 `we/fixes/`。

GE-Proton 主线在 GE-Proton11-7 之后的一部分修复提前搬了过来，放在 GE 自己的补丁目录里，见 `we/ge-backports.md`；0011 为此改了一处上下文。

0003、0016、0023 在换到 GE-Proton 底座时改过（0049、0050、0052–0056、0059 是换底座之后新加的）：0003 改到 GE-Proton 新的 SIGSYS 处理函数里；0016 修了一处 `-Werror` 下报错的格式串；0023 和 GE-Proton 覆盖层窗口的条件合并。

## 测试

`we/tests/build.sh` 用 mingw 静态编译测试程序（也能直接拷到 Windows 上跑），用 runner 的 `files/bin/wine` 加一个空前缀运行：

- `redzone_test`（0003）：在 rsp 下方放标记值，执行原生 syscall，检查标记有没有被改
- `udp_connect_poll`、`udp_wakeup_probe`、`udp_connect_err_probe`、`udp_connect_err_probe2`（0004）：UDP `connect()` 的各种情况。`*.windows.txt` 是 Windows 11 26200 上的实测输出，`*.wine3.txt` 是修复后的输出
- `writecopy_probe`、`writecopy_probe2`（0005）：写时复制页写前写后报告的保护属性、代码页打补丁、区域边界
- `noactivate_probe`（0006）：普通窗口隐藏、最小化、销毁时谁接手前台，`WS_EX_NOACTIVATE` 窗口在同一进程和另一个进程两种情况；要在有显示的环境里跑（Xvfb 即可）
- `layered_child_probe`（0007）：用 `UpdateLayeredWindow` 更新的分层子窗口画在哪里、跟不跟父窗口走。用 `layered_child_grab.py` 在 Xvfb 上跑（`DISPLAY=:N layered_child_grab.py <runner>/files/bin/wine layered_child_probe.exe [pos]`），它从 X 根窗口读像素，需要没有合成器的 X 服务器
- `typelib_noflags_probe`（0008）：参数不带方向的类型库接口跨套间调用时，服务端看到什么、客户端拿回什么。`build.sh` 另编一个 32 位的 `typelib_noflags_probe32.exe`，两个都要跑
- `minprec_sig_probe`（0009）：用到最低精度类型的着色器，`D3DGetBlobPart` 取各种签名时返回哪些段，`D3DReflect`（D3D11/D3D12 接口）报告哪些参数，包括 `MinPrecision`。参数是 `minprec_vs.dxbc minprec_hs.dxbc`，这两个是在 Windows 上用 d3dcompiler_47 编的，源码在同名的 `.hlsl` 里
- `sta_thread_exit_probe`（0008 排查时的弯路）：客户端 STA 线程不调 `CoUninitialize` 就退出时，代理的引用什么时候释放；Windows 和 Wine 相同
- `quit_filter_probe`（0008 排查时的弯路）：`PostQuitMessage` 和 `PostThreadMessage(WM_QUIT)` 遇到各种 `GetMessage`/`PeekMessage` 过滤条件时返回什么；Windows 和 Wine 相同
- `winhttp_crlf_probe`（0010）：请求头字符串开头或中间有空行时，WinHttpAddRequestHeaders 返回什么、实际加上了哪些头
- `winhttp_hdr_probe`、`winhttp_state_probe`（0010 排查时的弯路）：Range 类请求头配各种长度和 flag，以及请求句柄处于各种状态时添加请求头的结果。没有找到 Windows 成功而 Wine 失败的情况，但有几处 Windows 比 Wine 更严格的差异，见 0010 报告
- `tray_sni_probe`（0011）：通知区图标由 StatusNotifierItem 面板显示时，面板读到的属性、图标像素、提示和气泡，以及点击后程序收到的回调。用 `sni_host.py` 跑（`dbus-run-session -- env DISPLAY=:N WINEPREFIX=<新前缀> sni_host.py <runner>/files/bin/wine tray_sni_probe.exe [v0]`），它在私有的会话总线上扮演面板。explorer 启动时才找 watcher，所以前缀的 wineserver 不能已经在运行
- `capture_mt_probe`（0012）：照列摄像头格式的程序那样枚举视频采集设备，打印 `GetStreamCaps`、`GetFormat`、`IEnumMediaTypes::Next` 返回的 `pUnk`；`--delete` 改用 `DeleteMediaType()` 释放。要接着摄像头。`build.sh` 另编一个 32 位的 `capture_mt_probe32.exe`，要在 `WINEARCH=wow64` 下跑
- `ime_edit_probe`（0014）：用输入法往输入框里打字时，窗口收到哪些 IME 消息和字符消息、最后得到什么文字；参数 `disable` 先调 `ImmDisableIME(0)`。用 `ime_type.sh` 跑（`WINEPREFIX=<新前缀> ime_type.sh ime_edit_probe ctrl+space TYPE:nihao space -- <runner>/files/bin/wine ime_edit_probe.exe 20 disable`），它起私有的 Xvfb、会话总线和 fcitx5（XIM 前端 + rime），用 xdotool 打拼音，不碰桌面的输入法。需要 fcitx5、fcitx5-rime、xdotool
- `inbv_probe`（0019）：从 `ntoskrnl.exe` 取 `InbvAcquireDisplayOwnership`、`InbvResetDisplay` 并调用；是 stub 时进程被 abort，实现了就打印 `called ok`
- `window_band_probe`（0020，另有 32 位的 `window_band_probe32.exe`）：对各种窗口调用 `GetWindowBand`/`NtUserGetWindowBand`，打印返回值、层级和错误码；参数 `nullptr`、`nullhwnd` 测输出指针为空。是 stub 时进程被 abort
- `input_shape_probe`（0023）：模仿 Electron 的 `setIgnoreMouseEvents(true, { forward: true })`，窗口一秒后加上 `WS_EX_LAYERED | WS_EX_TRANSPARENT`，低级鼠标钩子看到光标到了窗口上就去掉这两个样式，最后打印钩子看到的移动和窗口是否恢复；参数 `topmost` 让窗口置顶，像覆盖层。`input_shape_probe.sh [--topmost]` 在无头 gamescope（XWayland）里跑它，读出穿透时 X 输入区域的矩形数（0 = 点击穿透）和是否选了指针移动事件，再用 XTest 把指针从窗口外移进来。XTest 下 X 服务器总知道指针在哪，复现不出 niri 下的卡死，只能看 X 这一侧的状态
- `vcrt_version_probe`（0021、0022）：打印 `VC\Runtimes\<arch>` 在两个注册表视图里的值和 system32、syswow64 里运行库 DLL 的文件版本，再按“注册表和 `msvcp140_2.dll`、`vcruntime140_1.dll` 的文件版本都不低于最低版本”检查（默认 14.44.35211，参数可改）。`wine24-redist` 是装了官方 14.44.35211 运行库之后的输出
- `iphlp_forward1_probe`（0053）：旧的路由函数 `CreateIpForwardEntry`/`SetIpForwardEntry`/`DeleteIpForwardEntry` 的返回码，以及 `MIB_IPFORWARDROW` 的 metric、下一跳、type、协议怎样变成实际的路由、经 `GetIpForwardTable`/`GetIpForwardTable2` 怎样读回。用 TEST-NET-2 `198.51.100.0/24`，加在通往 8.8.8.8 的网卡上，跑完删掉；地址和 metric 写成相对这块网卡的值，不同机器的输出能逐行比较。要管理员权限；在 Wine 上会真的改路由，只在网络命名空间里跑（例如 shorin-proton-wrapper 开了独立网络的前缀，`shorin-proton-wrapper -p 前缀 --runner <runner>/proton iphlp_forward1_probe.exe Z:\输出.txt`）；参数是输出文件
- `activateapp_probe`（0056，另有 32 位的 `activateapp_probe32.exe`）：进程第一个窗口、同一进程两个线程、两个进程、桌面窗口、销毁前台窗口、`AttachThreadInput` 几种前台变化时，窗口收到的 `WM_ACTIVATEAPP`/`WM_NCACTIVATE`/`WM_ACTIVATE` 和 lParam 对应的线程。Windows 上要在交互会话里跑（SSH 会话没有前台）；Wine 上用 `activateapp_probe.sh <runner> <输出文件>`，它起私有 Xvfb，用 `activateapp_x11_helper.py` 当最简单的窗口管理器，另外测 X11 原生窗口拿走焦点再切回
- `mf_shutdown_probe`（0055，另有 32 位的 `mf_shutdown_probe32.exe`）：系统工作队列的回调里调 `MFUnlockWorkQueue`，主线程同时调 `MFShutdown`；参数 `--queue standard|mt` 选标准队列或多线程队列。卡死时看门狗 10 秒后以退出码 3 结束，正常打印 `RESULT ok`
- `minimize_restore_probe` + `wayland_minimize.sh`（提前移植的 GE em-fixups 0067，见 `we/ge-backports.md`）：游戏式窗口在消息循环里带垂直同步交 D3D11 帧，3 秒时自己最小化，`wayland_minimize.sh <runner> [normal|popup]` 在第 6 秒左右用 niri 分支的 `unminimize-window` 恢复它；看门狗线程每半秒打印帧数，恢复后帧数停住就是卡死在 Present 里。用 Wine 的 Wayland 驱动和 runner 的 DXVK，要在 niri（嵌套的也行）里跑
- `wayland_fullscreen.sh`（0050、0052）：Wayland 驱动下 niri 的全屏快捷键、窗口自己全屏、D3D9 窗口模式全屏、网格概览，见 0050 报告
- `dxil-spirv/run.sh`（vkd3d-proton 0001）：用 dxil-spirv 的 `structurize-test` 跑 `*.st` 控制流图，检查生成的 SPIR-V 能通过校验。源码取打过补丁的 `vkd3d-proton/`（先跑 `we/build.sh` 或 protonprep）；`--unpatched` 用未打补丁的子模块，应该失败

`regress.sh <runner> <输出目录>` 一次跑完上面大部分探测（在 Steam Linux Runtime 4.0 里，和 wegame-launcher 相同的方式）；`regress.sh --compare <旧> <新>` 屏蔽掉句柄和地址后比较。每次发布的结果存在 `regress-<名字>/`，下次和它比。

Wine 自己的测试：用打过补丁的 `wine/`（跑过 protonprep 之后）另配一个 `--enable-tests` 的构建目录，编 `dlls/kernel32/tests`、`dlls/ntdll/tests`、`dlls/user32/tests`（0008 是 `dlls/oleaut32/tests`、`dlls/rpcrt4/tests`，0009 是 `dlls/d3dcompiler_47/tests`、`dlls/d3dcompiler_43/tests`、`dlls/d3d10/tests`，0010 是 `dlls/winhttp/tests`（winhttp:winhttp），0011 是 `dlls/shell32/tests`（shell32:systray，有和没有 watcher 各跑一遍），0012 是 `dlls/qcap/tests`（i386 和 x86_64 都编，`WINEARCH=wow64`，要接着摄像头），0014 是 `dlls/imm32/tests`（imm32:imm32）），用新旧 runner 各跑一遍、逐条比对失败项（0005 起都这样验证）。

## 诊断

诊断补丁在 `we/diag/`，不进正式构建。要用时临时复制进 `patches/wine/`，构建一个单独命名的 runner（`we/build.sh --name we-proton-diag`），用完删掉：

- `0001-trace-skip-noisy-syscalls.patch`：`+syscall` 跟踪跳过 QPC、yield、查询系统时间
- `0002-server-log-afd-poll.patch`：wineserver 记录可能阻塞的 AFD poll 和数据报 connect
- `0003-ntdll-log-process-termination.patch`：记录每次 `NtTerminateProcess` 的调用方、退出码和调用栈

启动脚本都是 `<脚本> [--exe NAME.exe] -- <启动命令>`，日志进 `build/logs`：

- `we/run-logged.sh <启动命令>`：只开 Proton 日志
- `we/diag/run-exe-trace.sh`：一个进程的异常、调试输出、fixme/err、DLL 加载
- `we/diag/run-syscall-trace.sh`、`run-poll-trace.sh`、`run-term-trace.sh`：要用诊断 runner，启动命令里要用 `$WE_DIAG_PROTON` 指向的 proton 脚本
- `we/diag/run-shaderdump.sh`、`run-spirv-fail.sh`：vkd3d-proton 着色器转储、Mesa 拒绝的 SPIR-V 转储
- `we/diag/eventselect_rtt.c`：`WSAEventSelect` 循环的往返时间；`we/diag/pdb_publics.py`：用 PDB 公共符号把 RVA 解析成函数名

## 构建

```
we/build.sh --name we-proton-<版本> [--install] [--jobs N] [--skip-prep]
```

需要 docker。第一次要几个小时，之后靠 ccache（`~/.ccache`）快一些。脚本做的事：

1. `git submodule update --init --force --filter=tree:0 --recursive`：子模块回到这棵树钉住的提交。子模块里没提交的改动会丢，要改 Wine 在开发分支上改（见「跟进上游新版本」）
2. 跑 `patches/protonprep-valve-staging.sh`（日志 `build/protonprep.log`，`LC_ALL=C`）：重置并打补丁 dxvk、vkd3d-proton（包括它嵌套的 dxil-spirv）、protonfixes 等子模块，准备 wine-mono 源码，往 Valve 的 Wine 上打 GE-Proton 的补丁、staging、`patches/dwproton/`、`patches/wine/`，最后 `autoreconf` 和 `make_requests`。脚本本身不会因为补丁打不上而停下，`build.sh` 在日志里找 patch、`git apply`、`git revert` 的报错（`FAILED`、`error:`、`fatal:`、`Failed to apply`、文件不存在等），以及打补丁的目录里有没有 `.rej`，有就报错退出
3. 在 `build/obj` 里 `configure.sh --build-name=<名字>`，`make redist`（日志 `build/make.log`），全部在 `Makefile.in` 指定的 Steam Linux Runtime 4.0 SDK 镜像里编
4. 把结果移到 `build/<名字>`，`version` 文件改成 `<时间> <名字>`（`Makefile.in` 写的是 `git describe`，启动器从这里读 runner 名字），加上 `we-proton-build-info` 和 `README-WE-Proton.md`
5. 生成三个发布文件：`<名字>.tar.xz`（runner）、`<名字>.sha512sum`、`<名字>-source.tar.xz`（打好补丁的 Wine、vkd3d-proton、dxvk 源码和 7-Zip 源码，满足 LGPL 提供源码的要求）。`files/utils/7zz` 的许可证复制成 `LICENSE.7zip`、`COPYING.7zip`
6. `--install` 时复制到 `~/.local/share/proton/runners/WE-Proton Latest`

每次都清空 `build/obj` 从头编：`Makefile.in` 对 xz 这种在源码副本里生成 configure 的组件做不了第二次构建（下次同步时发现源码副本多了生成的文件，重新同步、把它们删掉，configure 的完成标记却还在，接着编就找不到 `build-aux/missing`）。有 ccache，从头编一次大约一个多小时。想试增量，可以自己在 `build/obj` 里跑 `make redist`，碰到上面的错误再清空。

`--skip-prep` 跳过第 1、2 步，用在补丁已经打好、不想重新打一遍的时候。

`--prep-only` 只做第 1、2 步（重置并打补丁），给下面的快速构建用。

### 快速构建（测试版）

```
we/quick-build.sh --base build/<上一个完整构建> --name <测试名> [--prep] <wine 目录>...
```

在上一次完整构建留下的 `build/obj` 上只重编指定的 Wine 模块（`dlls/winewayland.drv`、`dlls/win32u`、`programs/explorer`、`server` 等），把结果放进 `--base` 的一份 reflink 副本 `build/<测试名>`，几分钟出一个测试版。

- `--prep` 先重新打补丁（同 `we/build.sh --prep-only`），用在改了 `patches/` 下的文件之后；不带它就直接编 `wine/` 现在的样子（在 `wine/` 里直接改代码时）
- 只把 `wine/` 里比上次同步新、内容又确实变了的文件复制进 `build/obj/src-wine`，不带时间戳，所以重新打补丁时没变的文件不会触发重编；configure 不同步（它在容器里重新生成过）
- x86_64（64 位 PE 和 unix 部分）和 i386（32 位 PE 和旧式 32 位 unix 部分）都编，按发布版的方式去掉调试信息，只替换 runner 里已有的文件；不改代码时出来的文件和完整构建逐字节相同
- 前缀里的内置 DLL 是指向 runner 的符号链接，已经用过 `build/<测试名>` 的前缀下次启动就用上新文件；替换时先写新文件再改名，正在运行的进程不受影响，但换之前最好先关掉
- 新加 DLL、改 `configure.ac` 这种要重新跑 configure 的改动不行，用 `we/build.sh`；发布也必须用 `we/build.sh`
- `build/obj` 被下一次完整构建清空重建；没有它时脚本会提示先跑一次 `we/build.sh`

只改了 Wine 补丁、想快点验证时，可以先单独编 Wine：在 SDK 镜像里对打好补丁的 `wine/` 跑 `dlls/winevulkan/make_vulkan -x vk.xml -X video.xml`、`tools/make_specfiles`，再用 `Makefile.in` 里 `WINE_AUTOCONF_ARGS` 的参数（`--enable-werror` 等）配置编译。这一步只查编译错误，不出 runner。

## 发布

名字用 `we-proton-ge<GE-Proton 版本>-<N>`，例如 `we-proton-ge11-7-25`。`N` 接着之前基于 DW-Proton 的 `we-proton-11.0-14-<N>` 往下编。wegame-launcher 把名字里的数字依次比大小来挑最新的 runner（`11.0-14-24` 是 (11, 0, 14, 24)，`ge11-7-25` 是 (11, 7, 25)），所以新名字排在旧的后面。

tag 和 Release 名都用 runner 的名字，tag 打在构建用的那个提交上（`we-proton-build-info` 里的 `we-proton:` 一行），三个文件原样上传：

```
gh release create <名字> --target <提交> --title <名字> --notes-file <说明> \
    build/<名字>.tar.xz build/<名字>.sha512sum build/<名字>-source.tar.xz
```

[wegame-launcher](https://github.com/SHORiN-KiWATA/wegame-launcher) 从最新的 Release 下载 `<tag>.tar.xz`，用 `<tag>.sha512sum` 校验，所以这两个文件名不能改。

## 用法

runner 名字就是 `we-proton-ge…`，wegame-launcher 自动选找到的最新 WE-Proton，其他启动器里手动选。`proton` 脚本里的 `CURRENT_PREFIX_VERSION` 是 GE-Proton 的（`GE-Proton11-7`），从基于 DW-Proton 的旧版换过来时前缀会升级一次。

## 跟进上游新版本

照 [`upstream-sync.md`](upstream-sync.md) 一步一步做（写给 AI agent 的操作手册，每一步有命令和检查，判断不了的地方会让你停下问用户）。大致是：

- GE-Proton 出新版：合并 GE-Proton 的新 tag，解决顶层冲突（protonprep 里我们的三处改动、7zip 段、子模块版本）；`we/sync/wine-dev.sh` 在 `wine/` 里重建开发分支（GE-Proton 打完补丁的基底 + `patches/dwproton/` + `patches/wine/`）；`we/sync/dup-titles.py` 找出 GE-Proton 新版本已经带上的 DW-Proton 补丁并删掉；`we/sync/export-patches.py` 导出；构建，`we/tests/regress.sh` 和上一版的基线比较
- DW-Proton 出新版：在 DW-Proton 的 Wine 仓库里列出新增的提交，按规则（GE-Proton 已有的不要，视频相关的不要，其余能打上的要）cherry-pick 到 `we-dw` 上，再接上 WE-Proton 的补丁，导出、构建、测试，记进 `ge-dw-merge-commits.txt` 和 `dw-proton-version`
- 上游修了同一个问题：删掉对应的补丁

## 注意

- 根目录的 `AGENTS.md` 是给 AI agent 的规定（问题报到哪里、反作弊和竞技游戏、补丁和报告的写法），动手前先看
- 不要以 GE-Proton 或 DW-Proton 的名义发布，也不要往上游提 issue/PR
