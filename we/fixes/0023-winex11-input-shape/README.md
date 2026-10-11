# 0023：XWayland 下，点击穿透的窗口拿到焦点时恢复输入

补丁：`patches/wine/0023-winex11-Let-click-through-windows-take-input-again-w.patch`（`dlls/winex11.drv/window.c`、`event.c`、`x11drv.h`）。

## 现象

在 niri + xwayland-satellite 下，《诡秘之主》的启动器（`GMZZLauncher`）窗口切到别的窗口再切回来以后，就点不动了：鼠标移上去没有悬停效果，点击也没反应。最大化、最小化或者再切几次焦点，有时能恢复。

## 根因

现场（`:0`，用 r25 的第一版跑）读到的：

```
21:28:53.586  焦点离开启动器窗口
21:28:56.610  窗口的 X 输入区域（SHAPE 的 input shape）变成空的，0 个矩形
21:28:57.114  焦点回到窗口，输入区域仍然是空的
```

之后把鼠标在窗口上来回移动了 8 秒，`XQueryPointer` 读到的指针位置一直是 (2989, 66)，一次都没变，指针下面的窗口也是空的。

1. 把顶层 X 窗口的输入区域清空的只有 `sync_window_input_shape()`（DWProton 的改动，不在上游 Wine 里）：窗口带 `WS_EX_TRANSPARENT` 时设空输入区域，让鼠标穿透。所以是启动器在失去焦点后自己给窗口加了透明样式。
2. 在 Windows 上，这类程序靠轮询 `GetCursorPos()`：鼠标回到窗口上就去掉透明样式。
3. XWayland 下，X 服务器只有在指针停在一个接收输入的 X 窗口上时才知道它在哪。窗口的输入区域是空的，合成器就把指针事件交给下面的东西，X 收不到任何移动，Wine 报给程序的位置一直停在鼠标离开时的那一点。程序认为鼠标还在外面，就一直不去掉透明样式，窗口一直点不动；只有样式碰巧又变了，输入区域才会重新同步。原生 X11 下 X 服务器总能看到指针，没有这个问题。

`sync_window_input_shape()` 里原有的注释说的是同一件事：Valve 对 Task Bar Hero 用了只按它的 SteamGameId 生效的特殊处理。

## 修复

- 记下窗口的输入区域是不是因为 `WS_EX_TRANSPARENT` 而清空的（`input_transparent`）
- XWayland 下（X 服务器有 `XWAYLAND` 扩展），这样的窗口收到 `FocusIn` 时恢复它的输入区域；之后样式再变，照常同步

恢复以后指针移到窗口上，X 就能收到，`GetCursorPos()` 更新，程序看到鼠标回来，自己去掉透明样式。没拿到焦点的透明窗口（覆盖层、高亮框之类）不受影响；原生 X11 下也不做这件事。

## 验证

`we/tests/input_shape_probe.sh`：在无头 gamescope（XWayland）里跑 `input_shape_probe.exe`，窗口加上 `WS_EX_TRANSPARENT` 后读一次输入区域，再从外面把 X 焦点移走、还给它，再读一次。

| | 加上 `WS_EX_TRANSPARENT` 后 | 重新拿到焦点后 |
|---|---|---|
| r24（`input_shape_probe.wine24.txt`） | 0 个矩形 | 0 个矩形（点不动） |
| r25（`input_shape_probe.wine25.txt`） | 0 个矩形 | 1 个矩形（整个窗口） |

两次都在 XWayland 下（有 `XWAYLAND` 扩展），Proton 经 steamrt4。

## 弯路

第一版的判断是：DWProton 只看 `WS_EX_TRANSPARENT` 就清空输入区域，而 Windows 和 wineserver 的命中测试要 `WS_EX_LAYERED | WS_EX_TRANSPARENT` 才让鼠标穿透，于是改成两个都有才清空。这个不一致是真的，探测程序也证实了，但它修不了这个启动器：r25 第一版上照样复现，窗口的输入区域照样被清空，说明启动器加的就是分层 + 透明。用户要求回滚，没有发布过，就从补丁序列里拿掉了，0023 换成现在的修复。

## 未验证

- 启动器窗口在新版上的实测还没做
- 只是收到焦点时恢复；如果用户不切焦点、只把鼠标移回去，窗口仍然点不动（这时 X 确实收不到指针）
