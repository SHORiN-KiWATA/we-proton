# 0023：XWayland 下，设成点击穿透的普通窗口仍然接收鼠标

补丁：`patches/wine/0023-winex11-Keep-pointer-input-for-click-through-regular.patch`（`dlls/winex11.drv/window.c`）。

## 现象

在 niri + xwayland-satellite 下，《诡秘之主》的启动器（`GMZZLauncher`）在它和别的窗口之间切几次焦点以后就点不动了：鼠标移上去没有悬停效果，点击也没反应。最大化、最小化或者再切几次焦点，有时能恢复。

## 根因

启动器是 Electron 程序（目录里有 `LICENSE.electron.txt`）。`resources/app.asar` 里的逻辑：

```js
// 页面：每次 mousemove，看鼠标下面是不是透明背景 .app-root
window.api.onEnterTransparent(isAppRoot);
// 主进程
electron.ipcMain.on(IpcEvents.ON_ENTER_TRANSPARENT, (event, entered) => {
  if (entered) win?.setIgnoreMouseEvents(true, { forward: true });
  else win?.setIgnoreMouseEvents(false);
});
```

窗口四周有一圈透明背景。鼠标落在上面，程序就把窗口设成鼠标穿透；落回面板上，再取消。

1. Windows 上 `setIgnoreMouseEvents(true, { forward: true })` 给窗口加 `WS_EX_LAYERED | WS_EX_TRANSPARENT`，再装一个低级鼠标钩子（`WH_MOUSE_LL`）。鼠标在窗口范围内移动时，钩子把 `WM_MOUSEMOVE` 转发给页面，页面才知道鼠标回到了面板上。
2. DWProton 的 winex11 对带 `WS_EX_TRANSPARENT` 的窗口做两件事：把 X 输入区域清空（`sync_window_input_shape()`），并在 X 事件掩码里去掉指针事件（`get_window_attributes()`）。
3. XWayland 下，X 服务器只有在指针停在一个接收输入的 X 窗口上时才知道它在哪。窗口的输入区域清空以后，合成器把指针事件交给下面的东西，Xwayland 什么也收不到；Wine 也就没有鼠标输入可以交给钩子。程序一直以为鼠标还在透明背景上，窗口一直穿透，点不动。原生 X11 下 X 服务器总能看到指针，没有这个问题。
4. 切焦点时鼠标往往从窗口边上那圈透明背景划出去，所以「切几次就坏」。

`+win,+event,+cursor` 日志（r25 第二版）对上了：鼠标移到 (1252,58) 时程序把窗口样式设成 `0x80020`（`WS_EX_LAYERED | WS_EX_TRANSPARENT`）。之后的一百多秒里，窗口再没收到 `MotionNotify`，Wine 记的光标位置一直停在 (1252,58)，程序也再没改过样式。焦点来回切了很多次，都没用。

`sync_window_input_shape()` 里原有的注释说的是同一件事：Valve 对 Task Bar Hero 用了只按它的 SteamGameId 生效的特殊处理。

## 修复

新增 `keep_pointer_input()`：XWayland 下（X 服务器有 `XWAYLAND` 扩展），窗口带 `WS_EX_TRANSPARENT`、由窗口管理器管理（`data->managed`）、且不是置顶（没有 `WS_EX_TOPMOST`）时，不清空输入区域，也不去掉指针事件。窗口从非托管变成托管时（`window_set_managed()`），如果已经是透明样式，就重新同步一次。

这样鼠标回到窗口上，X 照常收到，Wine 照常把鼠标输入交给 wineserver，低级鼠标钩子和 `GetCursorPos()` 都能看到光标；程序自己取消穿透。

代价：这类窗口穿透期间，点在它上面的点击不会落到下面的原生 Wayland 窗口上。wineserver 的命中测试照样跳过分层 + 透明的窗口，所以下面是 Wine 窗口时点击还是交给它。置顶窗口和非托管窗口（游戏覆盖层、桌宠、高亮框之类）照旧清空输入区域，鼠标照旧穿透。原生 X11 下什么都不变。

## 验证

- 用户实测（2026-10-11，r25 第三版，niri + xwayland-satellite）：同样的复现步骤（用 shorin-proton-wrapper 跑启动器，在它和别的窗口之间来回切焦点），修复成功。
- `we/tests/input_shape_probe.sh`：在无头 gamescope（XWayland）里跑 `input_shape_probe.exe`。探针模仿 Electron：窗口一秒后加上 `WS_EX_LAYERED | WS_EX_TRANSPARENT`，再由低级鼠标钩子看光标，光标到了窗口上就去掉这两个样式。脚本读出穿透时窗口的 X 输入区域矩形数和有没有选指针移动事件，再用 XTest 把指针从窗口外移到窗口中间。

| | 普通窗口穿透时 | 置顶窗口（`--topmost`）穿透时 |
|---|---|---|
| r24（`input_shape_probe.wine24.txt`） | 0 个矩形，不收指针移动 | 0 个矩形，不收指针移动 |
| r25（`input_shape_probe.wine25.txt`） | 1 个矩形，收指针移动 | 0 个矩形，不收指针移动 |

探针的局限：XTest 是在 X 服务器里移动指针，X 服务器总知道指针在哪，Wine 还能从根窗口的 XInput2 原始移动事件拿到输入。所以 r24 在 gamescope 里也会恢复，复现不出 niri 下的卡死。探针只能证明 X 这一侧的状态变了：普通窗口穿透时仍然收输入、选了指针事件，合成器就会把指针交给它。置顶窗口不受影响。卡死本身靠启动器实测。

## 弯路

- 第一版：DWProton 只看 `WS_EX_TRANSPARENT` 就清空输入区域，而 Windows 和 wineserver 的命中测试要 `WS_EX_LAYERED | WS_EX_TRANSPARENT` 才让鼠标穿透，于是改成两个都有才清空。这个不一致是真的，但启动器加的就是分层 + 透明，r25 第一版上照样复现。用户要求回滚，从补丁序列里拿掉了。
- 第二版：窗口收到 `FocusIn` 时恢复输入区域。用户实测仍然点不动。原因有两个：一是只恢复了输入区域，没恢复事件掩码里的指针事件，X 把指针交给了窗口，Wine 却没选这些事件，还是收不到；二是就算都恢复了，鼠标下次再划过透明背景，窗口又会卡住，要再切一次焦点。也换掉了。
- 当时以为程序靠轮询 `GetCursorPos()` 决定何时恢复（Task Bar Hero 是这样）。这个 Wine 版本的 winex11 已经没有 `GetCursorPos` 驱动入口，光标位置只靠 X 送来的指针事件更新。看了 `app.asar` 才确认这个程序走的是低级鼠标钩子。

## 未验证

- 别的会把普通窗口设成穿透的程序（其他 Electron 程序、靠 `GetCursorPos()` 轮询的程序）没有逐个测
- KDE、GNOME 的 Wayland 会话没测；那里 Xwayland 的窗口管理器不同，但判断只看 `XWAYLAND` 扩展，不看窗口管理器的名字
- 置顶的 Electron 窗口（`alwaysOnTop`）如果也这样用，仍然会卡住
