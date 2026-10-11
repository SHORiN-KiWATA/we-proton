# GE-Proton + DW-Proton 合并记录

WE-Proton 从 `we-proton-ge11-7-*` 起以 GE-Proton 为底，合入 DW-Proton 的补丁。这里记录合并时的做法、规则和每一处手工处理，跟进上游新版本时照着做。

- GE-Proton：`GE-Proton11-7`（`c191f35d`）
- DW-Proton：`dwproton-11.0-14`
- 时间：2026-10-11

## 两边的结构

- GE-Proton 的顶层从 2019 年起不再合并 Valve 的历史，Valve 的改动是手工搬的。Wine 子模块是 Valve 的 `46b29104e`，构建前由 `patches/protonprep-valve-staging.sh` 打上 GE-Proton 的补丁、staging（`--all`）和视频改造（`ge-video-rework/`）
- DW-Proton 的顶层合着 Valve 的 `experimental_11.0`（到 09-23）。Wine 是它自己的 fork（wine-dwproton），在同一个 Valve `46b29104e` 之上有 504 个非合并提交

两边改 Wine 的地方大量重叠：Wayland 驱动、视频、驱动模拟、AMD 扩展库。直接三方合并时 77 个文件冲突，而且有几块是两边各做了一套，只能二选一。所以选了以 GE-Proton 为底：

1. 在 Valve `46b29104e` 上跑 GE-Proton 的 protonprep，得到「GE-Proton 打完补丁的树」
2. 把 DW-Proton 的 504 个提交按拓扑序逐个 cherry-pick 上去，按下面的规则取舍
3. 结果导出成 `patches/dwproton/`，由 protonprep 在 GE-Proton 的补丁之后打上

## 规则和数量

| 处理 | 数量 | 说明 |
|---|---|---|
| GE-Proton 的补丁或 staging 里有同名补丁 | 140 | 用 GE-Proton 的。其中 6 个是 review 时才补上的：GE-Proton 补丁的长标题在 `Subject:` 里折了行，第一次只比了第一行；它们已经被干净地打上去了，有一个造成 winewayland 里函数重复定义 |
| 打上后没有改动 | 51 | GE-Proton 已经有了，只是标题不同 |
| 冲突，丢弃 | 136 | 85 个在 GE-Proton 里有同名补丁；其余是 GE-Proton 换了实现（视频、Wayland 的离屏渲染等）、Valve/上游的回移（GE-Proton 跟进 Valve 时会带上）、CachyOS 字样之类 |
| 冲突，移植 | 8 | 二次元游戏相关，GE-Proton 没有，见下 |
| 能打上，但按规则剔除 | 13 | 见「视频」和下面两条 |
| 编译和 review 时发现，剔除 | 7 | 见下 |
| 合入 | 149 | 其中 2 个改过，见下 |

逐个提交怎么处理的见 [`ge-dw-merge-commits.txt`](ge-dw-merge-commits.txt)。

能打上但剔除的 13 个：

- 视频 10 个：7 个撤销 winedmo 里 Valve 的权宜修改（「假定有 AV1 解码器」「缺 PNG 解码器时忽略」等），3 个强制走 GStreamer 的开关。GE-Proton 的视频全走 winedmo，这些打上会弄坏它
- 撤销《神力科莎》字体权宜修改：打上会删掉 GE-Proton 里的对应改动
- rgb 字体平滑：DW-Proton 加了又自己撤了，两个一起去掉

编译和 review 时发现、剔除的 7 个：

- 5 个是 GE-Proton 已有、只是标题不同的实现，打上后变成重复定义或类型冲突（GE-Proton 构建用 `--enable-werror`）：token 的内核对象、`KeRegisterBugCheckReasonCallback`、`KeDeregisterBugCheckReasonCallback`、`KeCapturePersistentThreadState`、`MmGetVirtualForPhysical`
- `d2d1: Forward geometry group methods to the path representing the group.`：依赖丢弃了的「几何组内部建路径」。GE-Proton 用的是 staging 的几何组绘制实现
- `HACK: user32: Also force X11 for launchers using webview2.`：接在「跨进程启动器在 Wine-Wayland 下改用 X11」后面。后者 GE-Proton 有同名补丁但有意没打：GE-Proton 的 Wayland 驱动能直接画跨进程的启动器窗口，强制走 X11 反而会让内嵌网页进程和游戏分属两个驱动。两个都不要

## 视频

GE-Proton 去掉了 GStreamer 后端，视频全走 winedmo（FFmpeg 8.1），WMV 读取器、WMA 解码器、DirectShow 分离器都挪进了 winedmo；DW-Proton 留在 GStreamer 上。DW-Proton 冲突的视频提交逐个对过，GE-Proton 都有对应的修复，或者在它的实现里不会出这个问题：

- B 帧视频时间戳：GE-Proton 用 FFmpeg 的显示时间戳，并保持单调递增
- 媒体读取挑错流导致卡住：GE-Proton 按交给程序的样本挑流
- 解码后音频的块对齐和码率：GE-Proton 只在格式不变时才继承
- WMA 解码器的两个查询接口、EVR 的渲染偏好：GE-Proton 已经实现
- 《洛克人传奇合集 2》WMV 读流串台：GE-Proton 切换压缩/解码时不重建分离器，不会串
- 《空之轨迹》开场视频倒置：GE-Proton 有自己针对这个游戏的修复（没核实是不是同一个症状）
- AV1 转码视频的读缓存、两个 GStreamer 的 OpenGL 开关：GStreamer 专有，不适用
- 媒体转换器：DW-Proton 留在 GStreamer 里，GE-Proton 用 Valve 放在 FFmpeg 那一侧的实现，是设计取向不同

所以 DW-Proton 的视频提交一律不要，只留 6 个媒体时钟、EVR 事件的通用修复。

## 移植的 8 个

| 游戏 | 提交 | 做法 |
|---|---|---|
| 异环 | wdfldr.sys 组件加载器、`WdfRegisterLibrary`、wdf01000.sys 存根、函数表第 201 项（4 个） | 两边从同一个存根各自往下做，都修了第 201 项，DW-Proton 那份是完整的加载器：`wdfldr.sys` 整块换成 DW-Proton 的，加上 `wdf01000.sys`；GE-Proton 的私有头文件没人用了，删掉（`patches/dwproton/0158`） |
| hololive Dreams | 把进程挪到另一个会话、记录已加载的内核模块（2 个） | `server/device.c` 两边新增的声明都留；协议头用 `make_requests` 重新生成 |
| NIKKE | `PsGetContextThread` 改成跳板（1 个） | 用 DW-Proton 的版本：三个参数，和 Windows 一致；GE-Proton 的是两个参数。spec 和 `ntddk.h` 跟着改 |
| 鸣潮 | winex11 跨进程往 ARGB 窗口上画（1 个） | 两处冲突都留两边：GE-Proton 的 `mark_direct_draw_child()` 和 DW-Proton 的 visual。`set_dc_drawable()` 多了一个参数，GE-Proton 的其他调用补上 |

## 改过的 2 个 DW-Proton 提交

- `ntdll: Default to Windows 11`：`RegSetValueExW(..., REG_DWORD, (const BYTE *)win11_maj_ver, ...)` 把数值 10 当成了指针，改成 `&win11_maj_ver`。DW-Proton 的构建不开 `-Werror`，没暴露
- `HACK: ws2_32: Block first internet connection for some games.`：插进去的语句在声明前面，GE-Proton 的 staging 在后面加了声明，`-Werror` 下报错；把声明挪到前面

## 顶层

- 用 GE-Proton 的顶层和构建（`configure.sh` + `make redist`）
- DW-Proton 的 protonfixes 游戏修复：`patches/protonfixes/0003`–`0005`（NIKKE、hololive Dreams、按可执行文件自动套用）
- DW-Proton 的 dxvk HDR 检测：`patches/dxvk/dw-hdr-detection-via-displayconfig.patch`，排在 GE-Proton 的 dxvk 补丁前面；Wine 那一侧（`NtUserDisplayConfigGetDeviceInfo`）已经有了
- 7zip：hololive Dreams 的 protonfix 要用 `files/utils/7zz` 从 Windows 镜像里解出 `version.dll`，GE-Proton 不编它。照 DW-Proton 的 `steamrtdeps/build-sevenzip.mk` 加了 `7zip` 子模块（DW-Proton 用的同一个提交）和 `Makefile.in` 里的构建规则
- 没带过来：DW-Proton 给 protonfixes 的 umu 数据库加 NIKKE 条目的补丁（它的 protonfixes 版本里已经有了，只影响日志里显示的标题）。另外 dwfixes（`patches/protonfixes/0005`）会给终末地等程序设 `UMU_ID`，从 Steam 启动时会走 GE-Proton 的 `umu_without_steam` 分支，这是 DW-Proton 原来的设计
- xalia（手柄操作界面的辅助程序）默认关掉：GE-Proton 的 proton 脚本对所有程序都启动它（`PROTON_USE_XALIA=1` 加 `XALIA_SUPPORTED_ONLY=1`），WeGame 的 Chromium 界面线程回答它的无障碍查询时卡死，窗口点不动（2026-10-12，X11 下；Wayland 下 GE-Proton 本来就关掉它）。DW-Proton 没开它。要用的话设 `PROTON_USE_XALIA=1`，或在 `PROTON_COMPAT_CONFIG` 里加 `xalia`
- 不要：DW-Proton 的额外组件（dxvk-sarek、低延迟版 dxvk/vkd3d、自编 GStreamer 等）、proton 脚本里 DW-Proton 独有的选项（`PROTON_DXVK_SAREK` 等，大多数选项 GE-Proton 有同名的）、8 个 Valve 回移

### 子模块

Valve 在 09-23 用的 dxvk、vkd3d-proton、vkd3d、FEX、dxvk-nvapi 比两边都旧，两边都是各自从上游往前升的。

| 子模块 | GE-Proton | DW-Proton | 用哪个 | 理由 |
|---|---|---|---|---|
| dxvk | v3.1-19 | v3.1.1-27 | DW-Proton | 包含 GE-Proton 的版本；多 3.1.1 正式版、VK_EXT_present_timing（可用配置关掉）、几个修复；GE-Proton 的 dxvk 补丁都能打上 |
| vkd3d-proton | v3.0.1-402 | v3.0.1-424 | DW-Proton | 包含 GE-Proton 的版本；多 GPU 信号顺序竞争修复、Agility SDK 619.5 紧凑对齐等；GE-Proton 和 WE-Proton 的补丁都能打上 |
| protonfixes | 27f5c70 | 73c5586 | DW-Proton | 包含 GE-Proton 的版本，多 2 个游戏修复和 1 个子模块地址修正 |
| vkd3d | 2.1-26 | 2.1-64 | GE-Proton | 多的主要是着色器编译器内部重构和测试 |
| ffmpeg | n8.1 | n7.1.3 | GE-Proton | GE-Proton 的构建配置和视频改造按 8.1 做的 |
| OpenXR-SDK | 1.1.36 | 1.1.63 | GE-Proton | 和 wineopenxr 的版本绑定，GE-Proton 的是旧版 wineopenxr |
| libsoup | 3.6.4-142 | 3.6.6 | GE-Proton | GE-Proton 不再编 GStreamer，基本用不到 |
| FEX、dxvk-nvapi、低延迟层，以及路径不同的 xz、libpcap、d7vk 等 | | | GE-Proton | 用不到或没有实质区别 |

## WE-Proton 自己的补丁

- 去掉：0001 和撤回它的 0018；0012（GE-Proton 的 `qcap-dshow-fixes` 修了同一个 bug）；0024–0042、dxvk 0002–0007、vkd3d-proton 0002（都是从 GE-Proton 搬的，GE-Proton 直接带着原版）
- 改过：0003（GE-Proton 换了一套系统调用拦截，新的 SIGSYS 处理函数里有同样的问题，改在那里）、0016（格式串在 `-Werror` 下报错）、0023（和 GE-Proton 覆盖层窗口的条件合并）
- 编号沿用原来的，`patches/wine/` 里的空号就是去掉的
- 合并之后 WE-Proton 又加了 0048（wintun 报 Windows 风格的网卡描述，网易 UU 的路由模式要用），直接接在 WE 层后面
- 托盘：一开始打算用 GE-Proton 在 win32u 里的 StatusNotifierItem 实现、去掉 0011。实机上 WeGame 的托盘图标透明处成了黑色（它转图标时只要颜色平面有 alpha 就不看 AND 掩码），于是 0011 留着，0049 让 win32u 不接手托盘
- 0050、0052（实机测试时发现）：Wayland 驱动撤销合成器发起的全屏、窗口拿到焦点后还申请激活（niri 分支的网格概览因此关掉），见 `fixes/0050-winewayland-compositor-fullscreen/`、`fixes/0052-winewayland-no-activation-on-focus/`
- 0051：protonfixes 在 `run` 时也执行修正（`PROTONFIXES_RUN_VERB=1`），在 DW-Proton 底座的 WE 仓库里写的，搬过来

## 换底座带来的行为变化

这些不是合并时的选择，是换成 GE-Proton 底座后随之而来的，要实测：

- 以前 WE-Proton 导入 GE-Proton 的补丁时特意没合的几个，现在随 GE-Proton 一起生效：「WM_ACTIVATEAPP 的 lparam 不为零」的通用版（原来只对一个游戏生效，现在对所有程序生效，包括 WeGame）；和反作弊有关的几个 GE-Proton 补丁（环境变量开关，或只对特定游戏生效）
- DW-Proton 的 dxvk HDR 检测：宿主机开了 HDR 时，dxvk 自动报告 HDR10，不用再设 `DXVK_HDR=1`（`DXVK_NO_HDR=1` 可以关掉）
- 视频走 GE-Proton 的 winedmo + FFmpeg
- staging 的 `ntdll-WRITECOPY/0010`（GE-Proton 带着）在 kernelbase 的 `VirtualProtect` 里把返回的旧属性 `PAGE_WRITECOPY` 一律改报成 `PAGE_READWRITE`。WE 0005 管的「写过的写时复制页报告成可读写」照样成立；没写过的页，Windows 报 `PAGE_WRITECOPY`（0x08），这一版报 `PAGE_READWRITE`（0x04），见 `tests/writecopy_probe.ge25.txt`。没有去掉这个 staging 补丁
- 鸣潮那个跨进程画 ARGB 窗口的提交顺带改了 xrender：从没有 alpha 的源画到 32 位目标时，不再把 alpha 清零（所有程序都受影响）。DW-Proton 本来就这样，相对旧版 WE-Proton 不算变化，但和 GE-Proton 原来的行为不同
- DW-Proton 的「不按分层父窗口裁剪其他进程的子窗口」（server）和 GE-Proton 在 winex11 里裁剪客户区（`mark_direct_draw_child()`）同时作用；《Return to Krondor》的文字位图两边各有一个修复。都没有冲突，但没有实测

## 验证

- 在干净的检出上跑 protonprep：所有补丁都打上，没有 `.rej`；结果和 Wine 开发分支逐文件相同，只差 `SERVER_PROTOCOL_VERSION`（`make_requests` 跑的次数不同）和 `dlls/wineopenxr`（建开发分支的起点时只跑了 protonprep 的 Wine 部分，wineopenxr 没打 GE-Proton 的补丁；DW-Proton 和 WE-Proton 的补丁都不碰它）
- 单独编 Wine（SDK 镜像，`--enable-werror`，**不带 Wayland、FFmpeg、OpenXR**）：第一次编出的错误来自 9 个提交，即编译时剔除的 6 个和改过的 3 个（2 个 DW-Proton 提交和 WE 0016）。改完后编译错误全部消失，只剩缺 FFmpeg 和 OpenXR 加载器库的链接错误。因为没编 Wayland 驱动，winewayland 里的重复定义没抓到
- review：逐个比对完整标题，又找出 6 个和 GE-Proton 同名、已经打上的提交，以及上面那个强制 X11 的提交，都去掉了
- 完整构建：`we/build.sh --name we-proton-ge11-7-25`，从干净的 `build/obj` 开始，在同一个检出上第二次打补丁（验证子模块重置），67 分钟编完。打出来的 Wine 源码和开发分支一致（差别同第一条）
- 回归探测（Steam Linux Runtime 4.0 + WoW64，和 wegame-launcher 相同的启动方式），和 we-proton-11.0-14-24 比：
  - inbv、noactivate、private_ns（64/32 位）、typelib_noflags（64/32 位）、vcrt_version、winhttp_crlf 输出相同
  - layered_child、window_band（64/32 位）只差窗口句柄和函数地址
  - minprec_sig（0009）和修复后、Windows 上的输出相同
  - writecopy 差一行，原因见上一节
- input_shape_probe（0023，无头 gamescope）：两种模式的输出和 we-proton-11.0-14-25 相同
- D3D11：DXVK v3.1.1-27 建设备成功（这次是核显 780M，独显没绑驱动）
- 托盘：`tray_sni_probe` 在 GE-Proton 的 StatusNotifierItem 实现上能注册、显示图标和提示、发通知，单击、右键、双击、删除都正常（`tests/tray_sni_probe.ge25.txt`）。和 0011 的不同：图标只给 32×32 一种尺寸、通知的应用名是 "Wine"、多发 `NIN_SELECT`、`WM_CONTEXTMENU` 回调；waybar 双击时连发三次 Activate，程序收到一次双击外加一次单击
- 0048（合并后加的）：`wintun_probe32.exe` 在无特权的用户+网络命名空间里，网卡描述报 `Probe Tunnel`，和 Windows 一致；12 个回归探测和加 0048 之前的构建相同
- 回归结果存成了基线 `tests/regress-we-proton-ge11-7-25/`，以后用 `tests/regress.sh --compare` 比较
- 没测：WeGame 和游戏的实机运行、WeGame 自绘托盘菜单的位置、Wayland 驱动下的程序
