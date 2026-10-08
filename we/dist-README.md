# @NAME@

WE-Proton 是 DW-Proton 的一个个人修改版，补了几个 Wine 和 vkd3d-proton 的 bug。**它不是 DW-Proton**，遇到问题不要去 DW-Proton 那边报告。

基于 `@BASE@` 官方发布包，只替换了这些文件（都在 `files/` 下）：

- `bin/wineserver`
- `lib/wine/x86_64-unix/` 下的 `ntdll.so`、`win32u.so`、`qcap.so`、`nsiproxy.so`，以及新增的 `explorer.so`、`wintun.so`
- `lib/wine/x86_64-windows/` 下的 `ntoskrnl.exe`、`nsiproxy.sys`
- `lib/wine/{x86_64,i386}-windows/` 下的 `ntdll.dll`、`kernelbase.dll`、`kernel32.dll`、`rpcrt4.dll`、`winhttp.dll`、`iphlpapi.dll`、`nsi.dll`、`explorer.exe`、`d3d10.dll`、`d3dcompiler_33`～`43`、`46`、`47`，以及新增的 `wintun.dll`
- vkd3d-proton（D3D12）：`lib/wine/vkd3d-proton/{x86_64,i386}-windows/` 下的 `d3d12.dll`、`d3d12core.dll`

Unix 侧只换了 64 位的库（`x86_64-unix`）。32 位程序要在 WoW64 模式下运行（`PROTON_USE_WOW64=1`，wegame-launcher 默认开启）才用得上 `*.so` 里的修复；不开 WoW64 时，它们用的是官方发布包里没改过的 `i386-unix` 库。

## 修了什么

| 补丁 | 修之前 |
|---|---|
| wine 0001 | 已由 wine 0018 撤回，不再生效 |
| wine 0002 | wineserver 崩溃，前缀里的所有程序一起消失，还会留下占着显示连接的残留进程 |
| wine 0003 | 直接执行 `syscall` 指令、并在栈指针下方存数据的代码，返回后数据被改坏而崩溃 |
| wine 0004 | 靠 UDP `connect(0.0.0.0)` 唤醒 `select()` 的程序，每次都要等满超时，网络请求极慢 |
| wine 0005 | 写时复制页被写过之后仍报告 `PAGE_WRITECOPY`，基于 CEF 的程序渲染进程一启动就崩 |
| wine 0006 | 活动窗口消失后，不该被激活的覆盖层窗口（`WS_EX_NOACTIVATE`）被设成前台，全屏程序因失去焦点而反复最小化 |
| wine 0007 | 用 `UpdateLayeredWindow` 绘制的子窗口（`WS_CHILD` + `WS_EX_LAYERED`）变成一个单独的窗口，出现在屏幕左上角或被窗口管理器当成另一个窗口，不跟着主窗口走 |
| wine 0008 | 通过进程外 COM 服务器拿结果的程序（接口按类型库封送、参数没写 `[in]`/`[out]`）拿不回任何结果，表现为初始化失败、登录或认证报错 |
| wine 0009 | 用到 min16float 等最低精度类型、并按着色器反射结果拼输入布局的 D3D11/D3D12 程序，创建渲染管线失败，常见表现是加载场景时闪退 |
| wine 0010 | 添加 HTTP 请求头时字符串以空行开头的程序（WinHTTP），请求头加不上、请求发不出去，常见表现是登录或认证后报错、崩溃 |
| wine 0011 | 在 waybar 等只支持 StatusNotifierItem 的面板上看不到程序的托盘图标，隐藏到托盘的程序叫不回来 |
| wine 0012 | 接着摄像头时，32 位程序（WoW64 模式）一枚举摄像头就崩溃，常见表现是内嵌浏览器（CEF）的程序窗口一出来就闪退 |
| wine 0013 | 用私有命名空间做单实例检测的程序（客户端、加速器等）在 Wine 里无法启动 |
| wine 0014 | 关掉了 IMM、靠 TSF 收输入法文字的程序（例如一些内置 Chromium 的客户端），能看到输入法的候选框，选字后却不上屏 |
| wine 0015 | 需要给网卡配 IP、加路由的程序（VPN、加速器、TUN 类工具）配置失败，表现为连接或加速不上 |
| wine 0016 | 用 wintun 接口创建虚拟网卡的程序（VPN、游戏加速器等）建不了网卡，表现为连接/加速在创建适配器一步失败或超时 |
| wine 0017 | 开 TUN 模式的加速器/VPN 后，程序自己“直连”的连接被虚拟网卡吃掉，整个会话像是断了网 |
| wine 0018 | 撤回 wine 0001：带上它时有程序报运行环境异常 |
| wine 0019 | 撤回 wine 0001 后《王者万象棋》闪退（驱动调用 `Inbv*` 被中止）；只把这两个空操作加回来 |
| vkd3d-proton 0001 | 部分 D3D12 着色器被翻译成非法的 SPIR-V，AMD 显卡（Mesa RADV）上直接崩溃 |

补丁源码在 `we-proton-patches/` 的 `wine/` 和 `vkd3d-proton/` 下。

## 安装

解压到启动器的 Proton 目录，例如：

- Steam：`~/.local/share/Steam/compatibilitytools.d/`
- 其他启动器：各自的 Proton / runner 目录

然后在启动器里选 `@NAME@`。

它要在 Steam Linux Runtime 4.0 里运行（`toolmanifest.vdf` 里的 `require_tool_appid 4183110`）。Steam、umu、Lutris、wegame-launcher 会自动这样做；在运行时外面直接执行 `proton` 时，自带的 GStreamer 插件等缺少依赖库，视频和音频解码之类的功能会出问题。

## 说明

- 0005 依赖 Linux 6.7 及以上内核的 userfaultfd 异步写保护；内核更旧时这一项不生效，其他照常
- 构建出的 `d3d12.dll`、`d3d12core.dll` 使用 UCRT（`api-ms-win-crt-*`），官方版本用 `msvcrt.dll`，Wine 两者都提供
- 在 Wine 下运行带反作弊的在线游戏，账号可能被处理，风险自负

## 源码

- Proton：dwproton `@BASE@`（https://dawn.wine/dawn-winery/dwproton ）
- Wine：wine-dwproton @WINE@ 加上 `we-proton-patches/wine/` 里的补丁
- vkd3d-proton：@VKD3D@（https://github.com/HansKristian-Work/vkd3d-proton ）加上 `we-proton-patches/vkd3d-proton/` 里的补丁
- Proton 及其组件（包括 Wine、vkd3d-proton）的许可证见包内的 `LICENSE`、`LICENSE.OFL`、`PATENTS.AV1`
