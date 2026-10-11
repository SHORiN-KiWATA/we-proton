# @NAME@

WE-Proton 是基于 GE-Proton 和 DW-Proton 的个人 Proton：以 GE-Proton（`@GE@`）为底，合入 DW-Proton（`@DW@`）的补丁，再补几个 Wine 和 vkd3d-proton 的 bug。**它不是 GE-Proton，也不是 DW-Proton**，遇到问题不要去那两边报告。

这是完整构建（`make redist`），不是在别人的发布包上替换文件。DW-Proton 的补丁怎么挑的、丢了哪些，见源码仓库的 `we/ge-dw-merge.md`。

## WE-Proton 修了什么

| 补丁 | 修之前 |
|---|---|
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
| wine 0013 | 用私有命名空间做单实例检测的程序（客户端、加速器等）在 Wine 里无法启动 |
| wine 0014 | 关掉了 IMM、靠 TSF 收输入法文字的程序（例如一些内置 Chromium 的客户端），能看到输入法的候选框，选字后却不上屏 |
| wine 0015 | 需要给网卡配 IP、加路由的程序（VPN、加速器、TUN 类工具）配置失败，表现为连接或加速不上 |
| wine 0016 | 用 wintun 接口创建虚拟网卡的程序（VPN、游戏加速器等）建不了网卡，表现为连接/加速在创建适配器一步失败或超时 |
| wine 0017 | 开 TUN 模式的加速器/VPN 后，程序自己“直连”的连接被虚拟网卡吃掉，整个会话像是断了网 |
| wine 0019 | 《王者万象棋》闪退（驱动调用 `Inbv*` 被中止）；加上这两个空操作 |
| wine 0020 | 《暗区突围：无限》进游戏后闪退（查询窗口层级的 `NtUserGetWindowBand` 没有实现，进程被中止） |
| wine 0021 | 《诡秘之主》等较新的 UE5 游戏启动时提示缺少 Microsoft Visual C++ 2015-2022 Redistributable，点确定就退出（内置运行库的版本号低于游戏要求的最低版本） |
| wine 0022 | 同上，注册表里记的运行库版本停在建前缀时的旧值，旧前缀换新版后仍然提示 |
| wine 0023 | Wayland 桌面下，有的程序（如部分 Electron 启动器）窗口切几次焦点就点不动（程序把窗口设成鼠标穿透后，等不到鼠标回来的消息） |
| wine 0048 | 网易 UU 加速器的路由模式报 50004：内置 wintun 报的网卡描述是 Linux 接口名，程序按 Windows 的描述找不到自己建的虚拟网卡 |
| wine 0049 | 托盘图标透明的地方显示成黑色（GE-Proton 自带的托盘实现转图标时没处理好掩码）；托盘交回给 0011 的实现 |
| wine 0050 | 用 Wine 的 Wayland 驱动时，按合成器的全屏快捷键，窗口全屏一下就立刻恢复，或者一切换焦点就退出全屏（游戏的窗口模式也一样） |
| wine 0052 | 用 Wine 的 Wayland 驱动时，在 niri 分支的网格概览里把焦点移到 Wine 窗口，网格就直接关掉 |
| protonfixes 0051 | 前缀里已有程序在运行时，启动器用 `proton run` 再启动的游戏拿不到 protonfixes 的修正（例如绕开游戏自带的启动器），窗口可能出不来；启动器设 `PROTONFIXES_RUN_VERB=1` 后照常执行 |
| vkd3d-proton 0001 | 部分 D3D12 着色器被翻译成非法的 SPIR-V，AMD 显卡（Mesa RADV）上直接崩溃 |

编号沿用原来的。0001、0012、0018 和 0024–0042 是这一版不再需要的（0043–0047 没有用到）：0012 修的 bug GE-Proton 也修了。原来从 GE-Proton 搬来的 0024–0042 等，现在由 GE-Proton 本身带着。

托盘图标仍由 WE-Proton 的实现（0011）显示，GE-Proton 自带的那套由 0049 关掉。

GE-Proton 对所有程序都开着的 xalia（用手柄操作程序界面的辅助程序）在 WE-Proton 里默认关闭：它的无障碍查询会让 WeGame 的界面卡死。要用的话设 `PROTON_USE_XALIA=1`。

## 安装

解压到启动器的 Proton 目录，例如：

- Steam：`~/.local/share/Steam/compatibilitytools.d/`
- 其他启动器：各自的 Proton / runner 目录

然后在启动器里选 `@NAME@`。

它要在 Steam Linux Runtime 4.0 里运行（`toolmanifest.vdf` 里的 `require_tool_appid 4183110`）。Steam、umu、Lutris、wegame-launcher 会自动这样做；在运行时外面直接执行 `proton` 时，自带的音视频解码库等缺少依赖，视频和音频之类的功能会出问题。

## 说明

- 0005 依赖 Linux 6.7 及以上内核的 userfaultfd 异步写保护；内核更旧时这一项不生效，其他照常
- 前缀版本是 `@GE@`，从基于 DW-Proton 的旧版 WE-Proton 换过来时前缀会升级一次；日志里的 `Prefix has an invalid version?!` 是因为 GE-Proton 的脚本认不出旧的版本号格式，属于正常
- 在 Wine 下运行带反作弊的在线游戏，账号可能被处理，风险自负

## 源码

- 源码仓库：https://github.com/SHORiN-KiWATA/we-proton （构建用的提交和补丁列表见包里的 `we-proton-build-info`）
- 打好补丁的 Wine、vkd3d-proton、dxvk 源码：Release 里的 `@NAME@-source.tar.xz`
- 基于：GE-Proton `@GE@`（https://github.com/GloriousEggroll/proton-ge-custom ）、DW-Proton `@DW@`（https://dawn.wine/dawn-winery/dwproton ）
- Proton 及其组件（包括 Wine、vkd3d-proton）的许可证见包内的 `LICENSE`、`LICENSE.OFL`、`PATENTS.AV1`
