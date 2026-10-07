# 0006：活动窗口消失后，Wine 会激活 WS_EX_NOACTIVATE 窗口

补丁：`patches/wine/0006-win32u-Don-t-activate-WS_EX_NOACTIVATE-windows-in-pl.patch`（`dlls/win32u/window.c`，一行）。

## 现象

一个独占全屏的程序在 niri + xwayland-satellite 下黑屏，声音正常，切焦点或截屏时能闪出一帧画面。

诊断日志（`+x11drv`）显示，程序窗口在不停地「被恢复 → 0.05～0.1 秒后失去前台 → 自己最小化」：

- 独占全屏的程序在失去前台时会最小化自己（`WindowPosChanged` 到 `(-32000,-32000)`，`WM_STATE → Iconic`，渲染用的子窗口缩成 1x1）
- satellite 不执行最小化，Wine 看到 `mismatch WM_STATE 0x1, expected 0x3` 又把窗口恢复，画一帧，然后又失去前台
- 每次最小化时，前台都是同一个别的进程的窗口：一个全屏大小的 `WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TOOLWINDOW` 覆盖层弹窗

## 根因

活动窗口被隐藏、最小化或销毁时，`activate_other_window()` 要挑一个窗口接手。Windows 文档对 `WS_EX_NOACTIVATE` 的说明是：

> The system does not bring this window to the foreground when the user minimizes or closes the foreground window.

Wine 的 `can_activate_window()`（win32u）不检查这个样式，所以会把这种窗口挑出来设成前台。覆盖层在另一个进程时更糟：前台到了那个进程，原来的程序再调用 `SetForegroundWindow()` 也拿不回来（`we/tests/noactivate_probe` 实测）。

## 修复

`can_activate_window()` 跳过 `WS_EX_NOACTIVATE` 窗口。这个函数只被 `activate_other_window()` 调用，所以只影响「活动窗口消失后选谁接手」这一种情况，也就是文档说的那一种。

`SW_SHOW`、不带 `SWP_NOACTIVATE` 的 `SetWindowPos`、`SetForegroundWindow` 这些显式激活路径，以及窗口管理器把 `_NET_ACTIVE_WINDOW` 设到这种窗口时的处理，都没有改：这些情况下 Windows 怎么做没有实测过。

## 验证

- `we/tests/noactivate_probe`（`*.wine6.txt` 修复前，`*.wine7.txt` 修复后）：A 被隐藏、销毁时，修复前前台交给 `WS_EX_NOACTIVATE` 窗口，修复后为空；对照组（不带 `WS_EX_NOACTIVATE` 的弹窗）和显式激活路径完全不变
- Wine 自己的测试，Xvfb 无窗口管理器，修复前后的失败项逐条相同（失败都是这个环境本来就有的）：

  | 测试 | 结果 |
  |---|---|
  | `user32:win` | 68344 项，3 失败，前后相同 |
  | `user32:msg` | 30620 项，35 失败，前后相同 |
  | `user32:input` | 6 失败，前后相同 |

- 其他补丁的回归测试（redzone、writecopy、udp）在 release 7 上都通过

## 没有验证的

- **Windows 实测**：没有在 Windows 上跑 `noactivate_probe`，依据只有文档
- **实际程序**：没有在原来出问题的程序上复测。日志能证明前台被那个覆盖层拿走，但没有抓到它是经哪条路径拿走的（这一步的 `+win` 跟踪没跑成）。如果是经显式激活或窗口管理器激活，这个补丁不够

## 另外看到的问题（没修）

- **N 卡渲染、核显显示时交帧卡住**：niri 不使用 N 卡、屏幕都接在核显上，D3D11 程序在 N 卡上渲染时只有约 2 fps，N 卡处于空闲功耗档，渲染线程在等交帧。强制用核显后正常出帧
- **satellite 不执行最小化**：Wine 请求 `IconicState` 后窗口仍是 `NormalState`，Wine 会把它当成被恢复。对会在失去前台时最小化自己的程序，这会变成循环
