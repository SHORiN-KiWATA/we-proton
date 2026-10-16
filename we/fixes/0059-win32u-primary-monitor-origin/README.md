# 0059：主屏变化时顶层窗口留在原处

补丁：`patches/wine/0059-win32u-Keep-top-level-windows-in-place-when-the-virt.patch`（`dlls/win32u/defwnd.c`）。

## 现象

niri + xwayland-satellite，两块屏缩放不同（内屏 eDP-1 1.5 倍在左，外屏 DP-2 1.0 倍在右）。雷神加速器的窗口从外屏挪到内屏（拖动或快捷键都一样）时会「崩溃」：窗口消失，马上又在外屏出现。反方向（内屏到外屏）没事。进程其实没退出，雷神自己的日志只有 `move to another display`。

不是雷神特有的：记事本也一样（见「验证」）。

## 根因

- xwayland-satellite 把**有焦点的窗口所在的输出**设成 X 的主输出。窗口一换屏，主输出就跟着换
- Wine 的虚拟屏原点是主显示器的左上角，`root_to_virtual_screen()` 把 X 根窗口坐标加上虚拟屏左上角换算成 Windows 坐标。主屏是 DP-2 时，DP-2 在 (0,0)、eDP-1 在 (-1707,0)；主屏换成 eDP-1 后，eDP-1 在 (0,0)、DP-2 在 (1707,0)
- 过程：窗口挪到 eDP-1，ConfigureNotify 给出 X 坐标 (0,0) → Windows 坐标 (-1707,0)（此时主屏还是 DP-2）→ satellite 把主输出改成 eDP-1 → 桌面窗口收到 `WM_DISPLAYCHANGE`，虚拟屏变成 (0,0)-(4267,1440)，对所有顶层窗口 `update_window_state()` → winex11 的 `get_desired_wm_state()` 发现窗口矩形 (-1707,0)-(-507,750) 完全在新虚拟屏外面，`is_window_rect_mapped()` 为假 → `WithdrawnState`，X 窗口被 unmap → 程序随后改位置时重新 map，niri 当成新窗口放回原来的屏
- 窗口矩形没跟着原点平移，而 X 窗口没动，两边从这一刻起对不上
- 反方向不触发：窗口到 DP-2 时 Windows 坐标 (1707,0)，主屏改成 DP-2 后虚拟屏是 (-1707,0)-(2560,1440)，1707 仍在范围内，不会被撤掉（坐标照样是错的，只是没出界）

调试日志（`WINEDEBUG=+win,+x11drv,+event`）里撤窗口那一刻：所有进程的所有顶层窗口都收到一次 `swp_flags 0x181f`（`update_window_state`），雷神窗口紧接着 `requesting WM_STATE 0x1 -> 0`。

## 修复

桌面窗口处理 `WM_DISPLAYCHANGE` 时，如果虚拟屏原点变了，在 `update_children_window_state()` 之前把所有没最小化的顶层窗口平移同样的量（异步 `SetWindowPos`，`SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE`）。窗口留在原来的显示器上，Windows 坐标重新和 X 坐标对上；X 窗口位置不变。

旧原点取自桌面窗口自己的矩形：这时它还是上一次的虚拟屏（explorer 创建桌面后就设成虚拟屏大小）。不能用原来那个静态变量，它第一次是哨兵值，桌面创建后的第一次显示变化会被漏掉（第一版就漏了，见下）。

## 验证

`we/quick-build.sh --base build/we-proton-ge11-7-29 --name test-0059 dlls/win32u`。在真实桌面上用独立前缀开记事本，`niri msg action move-window-to-monitor` 先挪到 DP-2 再挪到 eDP-1，看 niri 的窗口编号：

| 运行器 | 挪到 eDP-1 后 |
|---|---|
| ge11-7-29 | 编号变了（1287 → 1288），窗口回到 DP-2 的工作区；日志里 `requesting WM_STATE 0x1 -> 0` |
| test-0059 | 编号不变（1297），窗口在 eDP-1 的工作区；日志里 `offset_top_level_windows moving ... by 1707,0`，没有撤窗口 |

WE-Proton 的回归探测和 ge11-7-29 相同。

弯路：第一版用 `WM_DISPLAYCHANGE` 里原有的静态 `virtual_rect` 当旧值，跳过哨兵值。测试里记事本那次正是桌面创建后的第一次显示变化，照样被撤掉。

## 没验证的

- Windows 上切换主屏时窗口是否保持在原来的显示器上（虚拟机没有多显示器）。这里按「窗口留在原处」实现
- 用雷神本身验证：它和 WeGame 在同一个前置环境里，要整个会话换到测试版
- 最小化的窗口没有平移，还原时用的旧位置仍是旧坐标系的
- 竞态：程序线程在显示缓存更新后、桌面窗口读它矩形之前处理了一个 ConfigureNotify，会被多平移一次。没见过
