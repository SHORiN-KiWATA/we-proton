# 0050：Wayland 驱动不再撤销合成器发起的全屏

补丁：`patches/wine/0050-winewayland-Keep-a-fullscreen-state-the-compositor-a.patch`（`dlls/winewayland.drv/window.c`、`waylanddrv.h`、`wayland_surface.c`）。只影响 Wine 的 Wayland 驱动（`PROTON_ENABLE_WAYLAND=1`，wegame-launcher 的 `wayland on`），X11/XWayland 不受影响。

## 现象

wegame-launcher 打开 Wayland 后，在 niri 里对 WeGame 窗口按全屏快捷键（`fullscreen-window`）：窗口全屏一下立刻恢复原样；或者全屏了，一切换焦点就退出全屏。Wine 自带的记事本一样，任何带标题栏或可拖动边框的窗口都这样。

第一版修复之后：

- 《封印者》（CLOSERS）用窗口模式、2560×1440 运行，按全屏快捷键仍然是全屏一下就退回窗口，第一轮画面还会卡住；再按一次也一样退回，画面恢复。niri 的窗口事件里，窗口进入全屏 13 毫秒后就回到了浮动状态
- 浮动的窗口（嵌套 niri 里的记事本）退出 niri 全屏后停在全屏那么大

## 根因

Wayland 驱动（GE-Proton 带的 winewayland）不单独记录「现在是全屏」，而是在每次窗口更新时，用 `wayland_win_data_get_config()` 从 Win32 窗口的位置和样式推算出该有的 Wayland 状态，再在 `wayland_win_data_update_wayland_state()` 里和合成器当前的状态比，不一致就发 `xdg_toplevel_set_fullscreen` / `unset_fullscreen`。

推算全屏用的 `wayland_win_data_is_fullscreen()` 只认两种窗口：没有 `WS_CAPTION`、`WS_THICKFRAME` 的，或者样式正好是 `WS_POPUP | WS_BORDER` 的。带标题栏或可拖动边框的窗口就算铺满屏幕，也永远不算全屏。

所以合成器自己把窗口设成全屏时（快捷键、合成器的窗口规则）：

1. configure 带全屏状态，驱动按它处理（`processing` 里是 2560×1440、状态 `0xc` = 平铺 + 全屏）
2. 处理完之后的下一次窗口更新，推算结果不含全屏（`window=…,0x4`）
3. 和合成器当前的全屏状态不一致，驱动发 `xdg_toplevel_unset_fullscreen`，niri 把窗口改回平铺尺寸

`+waylanddrv` 日志里就是这个顺序：`processing=(0,0)-(2560,1440),0xc` → `current=…,0xc` → 紧接着 niri 发来 `processing=(0,0)-(1265,1389),0x4`。

Windows 上没有「合成器替窗口决定全屏」这回事，这是 Wayland 驱动自己的状态同步问题，不涉及 Windows 的行为。

第一版修复后剩下的两个问题：

- **渲染线程抢先用了旧状态。** 《封印者》的窗口模式是 `WS_POPUP | WS_BORDER`、客户区 2560×1440（边框在屏幕外，整个窗口 2562×1442），高度又被游戏限制在屏幕以内，所以不算全屏、在 niri 里浮动。全屏快捷键的 configure 到了以后，窗口线程开始处理，同时游戏的渲染线程正在交帧，它走 `ensure_window_surface_contents()` → `wayland_surface_reconfigure()`，把这个 configure 认作当前状态，接着 `wayland_surface_update_state_toplevel()`。这时窗口线程还没更新 `surface->window.state`，它还是 configure 之前的「非全屏」，和当前的全屏一比，驱动就发了 `xdg_toplevel_unset_fullscreen`。日志（嵌套 niri，模拟窗口，线程 0158 是交帧的线程）：

  ```
  00f0:trace:waylanddrv:wayland_configure_window hwnd=0x10056 processing=(0,0)-(2561,1440),0x8
  0158:trace:waylanddrv:wayland_surface_reconfigure hwnd=0x10056 window=(0,0)-(2563,1390),0 processing=(0,0)-(2561,1440),0x8 current=(0,0)-(2563,1390),0
  0158:trace:waylanddrv:wayland_surface_update_state_toplevel hwnd=0x10056 window_state=0 current->state=0x8
  ```

  第一版只在「请求全屏」一边认了合成器给的全屏，「取消全屏」一边没认。窗口一直不交帧（记事本）时碰不到。
- **浮动窗口退出全屏不给尺寸。** niri 结束浮动窗口的全屏时，configure 的尺寸是 0×0（由客户端决定），驱动就保持当前尺寸，也就是全屏的尺寸。

## 修复

- `wayland_configure_window()` 处理 configure 时：configure 带全屏、而上一次从窗口推算出的状态（`surface->window.state`）不带全屏，就是合成器自己发起的，记在 `surface->compositor_fullscreen`；configure 不带全屏时清掉
- `wayland_win_data_get_config()`：不在处理 configure、有这个标记时，状态里保留全屏，不再按窗口样式推翻
- `wayland_win_data_update_wayland_state()`：有这个标记时不再主动请求全屏（否则会因为 `requested_output` 对不上先 unset 再 set）
- `wayland_surface_update_state_toplevel()`：有这个标记时也不主动取消全屏，只等合成器发不带全屏的 configure 来结束它
- 合成器结束这种全屏时，按它给的尺寸改回窗口大小：原来的「窗口已经全屏、尺寸兼容就不改尺寸」（照顾坚持自己全屏分辨率的游戏）不适用于这种情况，否则窗口会留在全屏尺寸。configure 没给尺寸时（浮动窗口），恢复进入全屏前的尺寸（进入时记下）
- 窗口角色清掉时（`wayland_surface_clear_role`）一并清掉标记

窗口自己要求的全屏（游戏把窗口改成 `WS_POPUP` 铺满屏幕）走原来的路，不受影响。

## 验证

`we/tests/wayland_fullscreen.sh <runner>`（需要 niri 会话），niri 报告的窗口尺寸，主屏 2560×1440，平铺尺寸 1265×1389：

| 步骤 | 修复前 | 修复后 |
|---|---|---|
| 记事本，niri 全屏 | 1265x1389（0.3 秒内就被撤销） | 2560x1440 |
| 焦点切到别的窗口 | — | 2560x1440 |
| 焦点切回来 | — | 2560x1440 |
| niri 再按一次（结束全屏） | — | 1265x1389 |
| 再来一轮 | — | 2560x1440 → 1265x1389 |

窗口自己全屏（`wayland_fullscreen_probe.exe`：2 秒时改成 `WS_POPUP` 铺满屏幕，5 秒时改回 800×600 的普通窗口），修复前后都是 平铺尺寸 → 2560x1440 → 平铺尺寸。

用户在 WeGame 里确认第一版修复生效（2026-10-12）。

### 第二版（交帧线程、浮动窗口的尺寸）

测试环境：嵌套 niri，避免模拟输入和全屏窗口打扰正在用的桌面。

```
cp /proc/<会话 niri 的 pid>/exe niri-session      # 和会话同一个 niri 程序
printf 'output "winit" { scale 1.0; }\n' > nested.kdl
WAYLAND_DISPLAY=wayland-1 niri-session -c nested.kdl &
```

然后在外面的 niri 里把嵌套窗口挪到笔记本屏幕（2560×1440 物理像素，缩放 1.5）上全屏，嵌套 niri 的输出就是约 2560×1440、缩放 1；里面再开一个 waybar，和用户的桌面一样。测试时把 `NIRI_SOCKET`、`WAYLAND_DISPLAY` 指向嵌套的 niri。

`we/tests/wayland_fullscreen.sh`，输出 2561×1440（`wayland_fullscreen.ge25-test2.txt` 是第一版，`wayland_fullscreen.ge25-0052.txt` 是第二版）：

| 步骤 | 第一版 | 第二版 |
|---|---|---|
| 记事本（浮动），niri 全屏 | 2561x1440 | 2561x1440 |
| niri 结束全屏 | 2561x1440（停在全屏尺寸） | 1368x1026（原来的尺寸） |
| D3D9 游戏窗口（窗口模式，比屏幕矮 50 像素，被改尺寸就改回去），niri 全屏 | 2563x1390 floating（立刻被撤销） | 2561x1389，全屏 |
| 2 秒后 | 2563x1390 floating | 2561x1389，全屏 |
| niri 结束全屏 | — | 2563x1390 floating |

D3D9 窗口用的是发布包里的 DXVK。客户区正好等于屏幕的窗口驱动本来就当它全屏（样式加尺寸），按快捷键是退出全屏、再按一次进入，第二版也正常。独占全屏（`Windowed = FALSE`）的窗口失去焦点会被 DXVK 最小化，这和 Windows 上一样，从 niri 恢复后接着渲染，没变。

## 未验证

- niri 以外的合成器（KDE、GNOME 等）
- 游戏自己全屏、同时用户又用合成器的全屏快捷键切换的组合
- 《封印者》本身（第二版的测试构建已经给 WeGame 换上，等用户确认）；它第一轮按全屏时画面卡住，模拟窗口没有复现，看第二版后是否还在
