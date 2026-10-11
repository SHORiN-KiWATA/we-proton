# 0024–0042、vkd3d-proton 0002、dxvk 0002–0007：从 GE-Proton 导入的补丁

来源：GE-Proton11-7（2026-09-16）的 `patches/`，msxml3 那个来自 wine-staging v11.0。原作者保留（`git am`）；没有作者信息的裸 diff 以导入者提交。每个补丁的提交说明最后一行写了它在 GE 里的路径。

按用户的决定（2026-10-11）：GE 是被广泛使用的开源项目，这些补丁**没有逐个审代码**，只处理打不上的部分，然后编译、跑回归。挑选标准见 GE 调研：DWProton 11.0-14 没有、对我们有用的通用修复，加上 GE 里按游戏判断的兼容处理；和反作弊有关的补丁不合。

## 通用修复

| 补丁 | GE 里的路径 | 修什么 |
|---|---|---|
| wine 0024 | `wine-hotfixes/pending/ntdll-retry-native-view-allocation-with-effective-range.patch` | 地址空间满后清掉 native view 重试分配时，上下限传的是 0，重试必然失败；32 位进程（WeGame 的 CEF、32 位游戏）后期内存不足时用得上 |
| wine 0025 | wine-staging `msxml3_encode_gb2312` | 内置 msxml3 认 `encoding="GB2312"` 的 XML（Wine bug 58673） |
| wine 0026 | `wine-hotfixes/pending/ntdll-reserve-top-down-space-for-large-address-aware-wow64.patch` | 新 WoW64 下大地址感知（LAA）的 32 位程序拿到整块高地址后出问题，保留顶部一段（上游 f4c5b041，bug 58698） |
| wine 0027 | `wine-hotfixes/em-fixups/0043-winex11-poll-resize-completion-with-a-busy-message-queue.patch` | winex11 拖动/缩放窗口时消息队列一直忙（定时器渲染），等不到鼠标松开而卡在 SC_MOVE/SC_SIZE |
| wine 0028 | `wine-hotfixes/pending/urlmon-pump-thread-user-messages-during-synchronous-bind.patch` | 同步 bind 时只泵 urlmon 自己窗口的消息，嵌入的 IE 控件等到超时 |
| wine 0029 | `wine-hotfixes/em-fixups/0041-ntdll-terminate-on-contended-srw-locks-during-process-exit.patch` | 进程退出时在已退出线程持有的 SRW 锁上永久等待，进程退不掉 |
| wine 0030 | `game-patches/maplestory-spi-stickykeys-filterkeys.patch` | `SPI_SETSTICKYKEYS`/`SPI_SETFILTERKEYS` 返回成功（忽略） |
| wine 0031 | `game-patches/maplestory-kernelbase-charprev-null.patch` | `CharPrevA`/`CharPrevExA` 遇到 NULL 不崩 |
| wine 0032 | `wine-hotfixes/wine-wayland/0018-server-Fix-numpad-vkey-handling.patch` | 小键盘的 vkey 处理 |
| wine 0033 | `proton/0001-ntdll-Read-QueryPerformanceCounter-from-the-TSC-in-us.patch` | QueryPerformanceCounter 在用户态直接读 TSC；`WINE_DISABLE_QPC_TSC=1` 关闭 |
| vkd3d-proton 0002 | `vkd3d-proton/0001-vkd3d-bound-present-waits-during-swapchain-drain.patch` | 窗口先被撤掉时，销毁交换链卡在无限期的 `vkWaitForPresent`（D3D12 游戏退出、切换时卡住） |

## 按游戏判断的兼容处理

只在对应的游戏（按 exe 名、SteamGameId 或环境变量判断）里生效。

| 补丁 | GE 里的路径 | 说明 |
|---|---|---|
| wine 0034 | `game-patches/ai-limit-dx12-compute-shader-fallback.patch` | AI LIMIT：DX12 计算着色器的回退 |
| wine 0035 | `game-patches/assettocorsa-hud.patch` | 撤回 dwrite 的“建集合时释放文件流”（Assetto Corsa 的 HUD 字体），对所有程序生效 |
| wine 0036 | `game-patches/max-payne-cpu-detection.patch` | Max Payne 的 CPUID 叶数 |
| wine 0037 | `game-patches/silence-starcitizen-unsupported-os.patch` | Star Citizen 的“不支持的 Windows”弹窗不再弹出 |
| wine 0038 | `wine-hotfixes/pending/winex11-keep-forza-background-windows-unmapped-on-wlroots.patch` | Forza 的后台窗口在 wlroots 合成器上保持不映射 |
| wine 0039 | `wine-hotfixes/pending/unity_crash_hotfix.patch` | wined3d 路径下 dxgi 的一处 Unity 崩溃 |
| wine 0040 | `game-patches/return-to-krondor-text-bitmap-readback.patch` | Return to Krondor 的文字位图读回。**手工合并**：它改的条件在上游多了“显示设备”和 8 位色的判断，保留这两个判断再加上它的例外 |
| wine 0041 | `game-patches/black-desert-keep-fullscreen-on-focus-loss.patch` | 黑色沙漠失去焦点时保持全屏（`WINE_BLACK_DESERT_KEEP_FULLSCREEN=0` 关闭）。**手工合并**：辅助函数原来跟在一个我们没有的函数后面，按 fuzz 会被插进 `NtUserReleaseCapture` 里，改放在 `set_active_window()` 前面 |
| wine 0042 | `game-patches/layered-overlay-wine.patch` | 默认关闭；`WINE_LAYERED_OVERLAY_SHAPE=1`/`WINE_LAYERED_OVERLAY_ALPHA=1` 给桌面覆盖类游戏的分层窗口做形状或真透明。**手工合并**：它改的输入区域条件用 0023 的（分层 + 透明），声明挪到 `needs_offscreen_rendering()` 的声明旁边 |
| dxvk 0002 | `dxvk/layered-overlay-dxvk.patch` | 0042 的 DXVK 一半 |
| dxvk 0003 | `dxvk/dxvk-preserve-swapchain-on-acquire-backpressure.patch` | 获取交换链图像受阻时保留交换链 |
| dxvk 0004–0006 | `dxvk/dxgi-defer-initial-fullscreen-for-probe-swapchains.patch`、`dxgi-follow-d3d12-fullscreen-client-resizes.patch`、`dxgi-keep-fullscreen-on-focus-loss.patch` | 《最终幻想14》（`ffxiv_dx11.exe`）多屏时焦点移到另一块屏幕，从独占全屏缩回 1280×720；三个有上下文依赖，按 GE 的顺序打 |
| dxvk 0007 | `dxvk/black-desert-keep-fullscreen-on-focus-loss.patch` | 0041 的 DXVK 一半 |

DXVK 的补丁接在 DWProton 自己的 `patches/dxvk/0001`（HDR 检测）后面。overlay-build 从这一版起会在 `patches/dxvk` 比基础版本多出补丁时重编 DXVK，换掉 `lib/wine/dxvk/` 下两个架构的 `d3d8`、`d3d9`、`d3d10core`、`d3d11`、`dxgi`（改到的 `src/util`、`src/dxvk` 每个 DLL 都链接）。

## 没有合的

| GE 里的补丁 | 原因 |
|---|---|
| `game-patches/0001-win32u-Avoid-zero-WM_ACTIVATEAPP-lparam-on-first-for.patch` | DWProton 已有只对一个游戏生效的原版；GE 这份把它推广到所有程序，会改变所有程序（包括 WeGame）的激活消息，且和现有代码冲突 |
| `game-patches/pso2_hack.patch`、`game-patches/vgsoh.patch` | DWProton 里已经有了（`WINE_NO_OPEN_FILE_SEARCH`、SteamGameId 218210 的 `SetCurrentDirectoryW`） |
| `game-patches/multi-process-launcher-x11-fallback.patch` | 依赖 GE 自己的 Wayland 驱动改动 |
| `wine-hotfixes/pending/win32u-limit-extra-swapchain-image-to-doom.patch` 和 Hades、Path of Exile 的两个 | 调整的是 GE 从 wineland-child-rendering 0083 引入的交换链处理，我们的树里没有那段代码 |
| 两个和反作弊有关的 game-patches | 不合 |

## 验证

见 0023 的验证和本版的回归测试（`we/tests` 的探测程序在 steamrt4 里跑）。没有针对上面每个游戏实测。
