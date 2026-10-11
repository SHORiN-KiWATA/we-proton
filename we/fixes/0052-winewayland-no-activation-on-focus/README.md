# 0052：Wayland 驱动不再为已经有键盘焦点的窗口申请激活

补丁：`patches/wine/0052-winewayland-Don-t-ask-for-activation-of-a-window-tha.patch`（`dlls/winewayland.drv/window.c`）。只影响 Wine 的 Wayland 驱动（`PROTON_ENABLE_WAYLAND=1`）。

## 现象

用户的 niri 分支有网格概览（grid overview）：打开后用方向键在窗口之间移动焦点。焦点一移到用 Wine 的 Wayland 驱动运行的窗口（WeGame、记事本都一样），网格就直接关掉，那个窗口被激活。原生 Wayland 程序和 XWayland 下的 Wine 窗口没有这个问题。

## 根因

1. 网格把键盘焦点给 Wine 窗口，驱动收到 `wl_keyboard.enter`，把这个窗口设成 Win32 的前台窗口
2. win32u 激活窗口时调用驱动的 `WAYLAND_ActivateWindow()`，它调 `wayland_surface_activate()`，用 `xdg_activation_v1` 向合成器申请激活这个窗口（带着刚才那次输入的 serial，合成器认为合法）
3. niri 分支收到激活请求时走 `activate_window_from_activation()`：网格开着、窗口又不是网格打开后新出现的，就先关掉网格再激活窗口

窗口这时已经有键盘焦点，这次申请是多余的；它原本是给程序自己调 `SetForegroundWindow` 时用的。

Windows 上没有对应的东西，这是 Wayland 驱动和合成器之间的问题。

## 修复

`WAYLAND_ActivateWindow()`：要激活的窗口正是当前有键盘焦点的窗口（`process_wayland.keyboard.focused_hwnd`）时不再申请激活。程序激活一个还没有焦点的窗口（比如从最小化恢复后 `SetForegroundWindow`）照旧申请。

## 验证

嵌套 niri（和用户会话同一个 niri 程序，见 `we/fixes/0050-winewayland-compositor-fullscreen/README.md` 的测试环境），两个 Wine 窗口，打开网格后用 `focus-column-left`、`focus-column-right` 移动焦点，每步截屏：

| | 修复前 | 修复后 |
|---|---|---|
| 打开网格 | 网格 | 网格 |
| 焦点左移 | 网格关掉，回到普通布局 | 网格，左边的窗口高亮 |
| 焦点右移 | — | 网格，右边的窗口高亮 |

`we/tests/wayland_fullscreen.sh` 的第 4 步做同样的事，截图写到 `$WAYLAND_FULLSCREEN_SHOTS`。

## 未验证

- 用户真机上的网格概览（测试构建已经给 WeGame 换上，等用户确认）
- 其他合成器：大多数合成器对已经有焦点的窗口的激活请求不做什么，改动对它们应该没有影响
