# 提前移植的 GE-Proton 补丁

GE-Proton11-7 发布后，GE-Proton 主线又加了一批 Wine Wayland 驱动、托盘和退出流程的修复。GE-Proton11-8 还没发布，这里把其中和 WE-Proton 用法相关的一部分先搬过来。

**做法**：文件从 GE-Proton 主线原样复制到相同路径，`patches/protonprep-valve-staging.sh` 里加的行也和 GE-Proton 主线一字不差。跟进 GE-Proton11-8 时，这些文件和 GE 的完全相同，合并不会冲突；protonprep 里会多出重复的 `apply_patch` 行，删掉我们这份即可（见文末）。

## 移植了什么

来源：`ge` 远端的 master（2026-10-11，`8d7a8cd29`）。

| 文件 | GE 提交 | 作用 |
|---|---|---|
| `wine-hotfixes/em-fixups/0024-…recover-client-presentation-after-surface-transitions.patch` | `18ec61659`（更新版） | 窗口表面切换后恢复客户端的画面提交 |
| `wine-hotfixes/em-fixups/0051-…set-client-surface-opaque-region-from-swapchain-alpha.patch` | `a9e98fb81` | 按交换链的 alpha 设置不透明区域 |
| `wine-hotfixes/em-fixups/0052-…dispatch-clipboard-work-on-windows-thread.patch` | `e57ff17bb` | 剪贴板处理挪到窗口线程，修 Wayland 下叠加层窗口的崩溃 |
| `wine-hotfixes/em-fixups/0053-…preserve-overlay-cursor-ownership.patch` | `8c3c59f70` | 叠加层的光标归属 |
| `wine-hotfixes/em-fixups/0054-…commit-pointer-constraint-updates.patch` | `3a3d180bc` | 指针约束的更新单独提交，修光标消失 |
| `wine-hotfixes/em-fixups/0055-…reject-owner-hints-from-owned-dialogs.patch` | `444dee993` | 有所有者的对话框不再给弹出窗口当定位父窗口，修设置窗口不出现、光标丢失 |
| `wine-hotfixes/em-fixups/0056`–`0063` | `7f6728197` | 托盘：SNI 菜单属性、托盘菜单的键盘焦点和层级、XEmbed 托盘重建、表面反馈、光标保留、托盘点击把前台权限转给程序 |
| `wine-hotfixes/em-fixups/0064-…preserve-fixed-size-configure-geometry.patch` | `a60807eb0` | 固定尺寸窗口的 configure 几何，修崩溃 |
| `wine-hotfixes/em-fixups/0065-…keep-game-windows-out-of-tray-menu-layers.patch` | `b144dea11` | 普通窗口不进托盘菜单的层 |
| `wine-hotfixes/em-fixups/0066-…respect-output-hdr-state.patch` | `58448e1e9` | 输出口没开 HDR 时不强行报 HDR |
| `wine-hotfixes/em-fixups/0067-…preserve-renderer-subsurfaces-when-minimized.patch` | `ad73744bc` | 最小化时不摘掉渲染用的子表面，修全屏窗口最小化后恢复时卡死（交帧线程等合成器的帧回调，窗口线程等交帧） |
| `wine-hotfixes/wineland-child-rendering/0041-…Handle-managed-Vulkan-swapchain-extensions.patch` | `18ec61659`（更新版） | 跨进程子窗口渲染里托管的 Vulkan 交换链扩展 |
| `wine-hotfixes/pending/server-dont-touch-freed-handle-table-in-close-handle.patch` | `a1a8366fd` | wineserver：进程关掉自己所在的作业对象时不再访问已释放的句柄表 |
| `wine-hotfixes/pending/ntdll-block-apcs-until-process-exit-state-is-set.patch` | `8473e3d22` | 进程退出状态设好之前不执行 I/O 的 APC，避免退出时卡在孤儿锁上 |
| `wine-hotfixes/pending/winedmo-use-correct-parameter-structure-in-wow64-demuxer-destroy.patch` | `885a18afe` | 32 位程序停止 DirectShow 视频时 winedmo 释放了错误的指针而中止（上游 Wine 2110c64d；GE 主线换了更新的 Wine 后把它挪进了 disabled，我们的 Wine 还需要） |

另外 `Makefile.in` 的 `FFMPEG_CONFIGURE_ARGS` 加了 `--enable-libdav1d`（GE `57d3b58ca`，cherry-pick）：dav1d 本来就编进了包里，但 FFmpeg 没链接它，只剩要硬件加速的原生 AV1 解码器，AV1 视频解不出来。

## 没有移植的

- `dxvk/dxgi-respect-output-hdr-state.patch`：针对 GE 自己那版 DXVK。我们的 DXVK 带 DW-Proton 的 HDR 自动检测，已经按输出口是否开了 HDR（`getAdvancedColorEnabled`）决定上报，补丁也打不上
- 视频（`ge-video-rework/0062`–`0090`）、手柄、蓝牙、Steam 叠加层、针对单个游戏的修复，以及子模块和 Wine 本身的版本更新：等 GE-Proton11-8 一起跟进

## 对 WE 补丁的影响

- `patches/wine/0011`（explorer 的 SNI 托盘）：`hide_icon()` 那一段的上下文改成了 `0062` 之后的代码（`0062` 改了停靠图标移除的条件），内容不变
- `0063` 在 explorer 的托盘图标窗口收到鼠标消息时把前台权限（explorer 线程上次输入的时间）转给程序。0011 的 SNI 点击在 explorer 里也变成发给图标窗口的鼠标消息（`handle_sni_event()`），所以同样经过这里；只是 SNI 点击来自 D-Bus，不是 explorer 收到的真实输入，转出去的是它更早的输入时间，实际给不了程序前台权限

## 跟进 GE-Proton11-8 时

1. 合并后 `git status` 里这些文件应该没有冲突（内容相同）
2. `patches/protonprep-valve-staging.sh` 里搜下面这几行，GE 的版本里会各有一份，删掉重复的：
   - `winedmo-use-correct-parameter-structure-in-wow64-demuxer-destroy.patch`（GE 的新 Wine 已经包含，GE 那边可能已经去掉了这一行；那我们这行也删掉）
   - `ntdll-block-apcs-until-process-exit-state-is-set.patch`
   - `server-dont-touch-freed-handle-table-in-close-handle.patch`
3. 删掉这份文件，或者把它改成「已随 GE-Proton11-8 合入」
