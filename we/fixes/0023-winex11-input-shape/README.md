# 0023：只有分层且透明的窗口才让鼠标穿透

补丁：`patches/wine/0023-winex11-Only-let-the-mouse-through-windows-that-are-.patch`（`dlls/winex11.drv/window.c`）。

## 现象

Proton 跑的程序窗口有时点不动：鼠标移上去没有悬停效果，点击也没反应；切到别的窗口再切回来，或者最大化、最小化一下，有时能恢复。在 niri + xwayland-satellite 下遇到，现场是《诡秘之主》的启动器（`GMZZLauncher`）窗口。

现场从 X 那边看（显示 `:0`）：

- 窗口 1362×840，色深 24（不是带 alpha 的分层窗口），`_WINE_HWND_EXSTYLE` 属性是 0
- **X 输入区域（SHAPE 扩展的 input shape）是空的，0 个矩形**；边界区域和裁剪区域是整个窗口
- 鼠标在窗口范围内时，`XQueryPointer` 报告鼠标下面没有窗口：点击直接穿透

xwayland-satellite 把 X 窗口的输入区域原样转给合成器，所以合成器把点击交给了下面的窗口，X 和 Wine 都收不到。

## 根因

能把顶层 X 窗口的输入区域清空的只有 `sync_window_input_shape()`（DWProton 加的，不在上游 Wine 里）：

```c
if (ex_style & WS_EX_TRANSPARENT)
    /* set an empty input shape to allow mouse pass-through */
```

而 Windows 和 wineserver 自己的命中测试（`server/window.c` 的 `is_point_in_window()`、`is_window_transparent()`）只把**同时**带 `WS_EX_LAYERED` 和 `WS_EX_TRANSPARENT` 的窗口当成鼠标穿透；只带 `WS_EX_TRANSPARENT` 的普通窗口照样接收鼠标（这个样式本来只影响绘制顺序）。所以一个只带 `WS_EX_TRANSPARENT` 的窗口，在 Wine 内部“能点”，在 X 那边却被设成了穿透。

`_WINE_HWND_EXSTYLE` 只在 `set_wm_hints()` 里更新（建窗口、映射、改边框等时候），扩展样式变了不会跟着改，所以现场看到的 0 不代表窗口当时的真实样式。启动器窗口当时到底带没带 `WS_EX_TRANSPARENT`，没有在游戏的会话里放探测程序去读（游戏可能带反作弊）；但能清空输入区域的只有这一处，而它只在 `WS_EX_TRANSPARENT` 时才清空。

## 修复

- `sync_window_input_shape()` 改用和 wineserver 相同的条件：`WS_EX_LAYERED` 和 `WS_EX_TRANSPARENT` 都有时才设空输入区域，否则恢复成整个窗口
- `X11DRV_SetWindowStyle()` 在这两个位任一变化时都重新同步（原来只看 `WS_EX_TRANSPARENT`）

真正的透明覆盖层（`WS_EX_LAYERED | WS_EX_TRANSPARENT`，比如游戏内覆盖层、xalia 的高亮框）仍然穿透。

## 验证

`we/tests/input_shape_probe.sh`：私有 Xvfb 上跑 `input_shape_probe.exe`，窗口一秒后加上 `WS_EX_TRANSPARENT`（参数 `layered` 时再加 `WS_EX_LAYERED`），打印 `WindowFromPoint(窗口中心)` 是否命中窗口，外面再读 X 输入区域的矩形数。

| | r24（未修，`input_shape_probe.wine24.txt`） | r25（`input_shape_probe.wine25.txt`） |
|---|---|---|
| 只加 `WS_EX_TRANSPARENT` | WindowFromPoint 命中窗口，**X 输入区域 0 个矩形**（两边不一致） | WindowFromPoint 命中窗口，X 输入区域 1 个矩形（整个窗口） |
| `WS_EX_LAYERED` + `WS_EX_TRANSPARENT` | WindowFromPoint 不命中，X 输入区域 0 个矩形 | 同左（仍然穿透） |

都是在私有 Xvfb 上、经 steamrt4 跑的 Proton。

## 未验证

- 没有在 Windows 上实测（这个语义是 wineserver 的现有实现和 Windows 文档的说法）
- 没有确认《诡秘之主》启动器窗口的真实样式，也没有在它上面复测；如果它其实是分层 + 透明、并按鼠标位置自己切换（Wayland 下 `GetCursorPos` 会停在旧位置），这个补丁就帮不上
