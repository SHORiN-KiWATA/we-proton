# @NAME@

WE-Proton 是 DW-Proton 的一个个人修改版，补了几个 Wine 和 vkd3d-proton 的 bug。**它不是 DW-Proton**，遇到问题不要去 DW-Proton 那边报告。

基于 `@BASE@` 官方发布包，只替换了这些文件：

- Wine：`files/bin/wineserver`、`files/lib/wine/x86_64-unix/ntdll.so`、`files/lib/wine/x86_64-unix/win32u.so`、`files/lib/wine/x86_64-windows/ntoskrnl.exe`
- vkd3d-proton（D3D12）：`files/lib/wine/vkd3d-proton/{x86_64,i386}-windows/` 下的 `d3d12.dll`、`d3d12core.dll`

## 修了什么

| 补丁 | 修之前 |
|---|---|
| wine 0001 | 一些第三方内核驱动加载失败，或运行一段时间后因为调用未实现的函数被结束 |
| wine 0002 | wineserver 崩溃，前缀里的所有程序一起消失，还会留下占着显示连接的残留进程 |
| wine 0003 | 直接执行 `syscall` 指令、并在栈指针下方存数据的代码，返回后数据被改坏而崩溃 |
| wine 0004 | 靠 UDP `connect(0.0.0.0)` 唤醒 `select()` 的程序，每次都要等满超时，网络请求极慢 |
| wine 0005 | 写时复制页被写过之后仍报告 `PAGE_WRITECOPY`，基于 CEF 的程序渲染进程一启动就崩 |
| wine 0006 | 活动窗口消失后，不该被激活的覆盖层窗口（`WS_EX_NOACTIVATE`）被设成前台，全屏程序因失去焦点而反复最小化 |
| wine 0007 | 用 `UpdateLayeredWindow` 绘制的子窗口（`WS_CHILD` + `WS_EX_LAYERED`）变成一个单独的窗口，出现在屏幕左上角或被窗口管理器当成另一个窗口，不跟着主窗口走 |
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
