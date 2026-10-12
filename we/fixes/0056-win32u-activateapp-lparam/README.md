# 0056：WM_ACTIVATEAPP 的 lParam 报上一个前台线程

补丁：`patches/wine/0056-win32u-Report-the-previous-foreground-thread-in-WM_A.patch`（`dlls/win32u/input.c`）。

## 现象

窗口获得激活时，程序收到 `WM_ACTIVATEAPP(TRUE)`，lParam 应该是失去激活的那个线程。换成 GE-Proton 底座后（`we-proton-ge11-7-25` 起），GE-Proton 的 `game-patches/0001-win32u-Avoid-zero-WM_ACTIVATEAPP-lparam-on-first-for.patch` 对所有程序生效：只要新线程的输入里原来没有活动窗口，就把 lParam 换成**收消息的线程自己**。补丁说明说只改「第一次成为前台」，实际上没有 `AttachThreadInput` 时，每一次跨线程、跨进程的激活都满足这个条件（前台线程丢掉前台时，自己的活动窗口已经清成 0）。在这之前，原版 Wine 在这些情况下报 0。

以前 WE-Proton 导入 GE-Proton 补丁时特意没合这个（见 `0024-ge-proton-imports/`），因为它会改变所有程序的激活消息，包括 WeGame。还没发现因此出问题的程序；Chromium 只在失活（`wParam` 为 0）时比较这个线程号，不受影响。

## Windows 行为

`we/tests/activateapp_probe.c` 在 Windows 11 26200 的交互会话（计划任务，`LogonType Interactive`）里跑，输出 `tests/activateapp_probe.windows.txt`（开头那个前台程序的名字换成了 `another-program.exe`）。`WM_ACTIVATEAPP(TRUE)` 的 lParam：

| 情况 | Windows | ge11-7-28（GE 补丁） | 打 0056 后 |
|---|---|---|---|
| 进程第一个窗口激活 | 之前前台程序的线程 | 自己 | 桌面线程（Wine 里之前没有前台程序） |
| 同一进程两个线程互切 | 对方线程 | 自己 | 对方线程 |
| 两个进程互切 | 对方进程的线程 | 自己 | 对方进程的线程 |
| 从桌面窗口切回 | 桌面线程 | 自己 | 桌面线程 |
| 前台窗口被销毁后激活的窗口 | 被销毁窗口的线程 | 自己 | 被销毁窗口的线程 |
| X11 原生窗口拿走焦点后切回（只有 Wine 有） | — | 自己 | 桌面线程 |

Windows 从不报收消息的线程自己。在 SSH 的非交互会话里（没有前台窗口）第一个窗口报 0，所以 0 只出现在「之前没有前台」的时候。`WM_ACTIVATEAPP(FALSE)` 两边本来就一致（新前台的线程；前台离开 Wine 时是桌面线程）。

## 修复

- 去掉 GE 补丁加的 `get_activateapp_thread_id()`
- `set_foreground_window()` 记下原来前台窗口的线程（原来前台不在 Wine 里或没有前台时，用桌面窗口的线程，和 `WM_ACTIVATEAPP(FALSE)` 离开 Wine 时一样），通过 `set_active_window()` 的最后一个参数传下去（这个参数原来只在失活时用，激活时所有调用者都传 0）；跨线程时走 `WM_WINE_SETACTIVEWINDOW` 的 lParam
- `set_active_window()` 发 `WM_ACTIVATEAPP(TRUE)` 时，新线程的输入里原来有活动窗口就照旧用它的线程，没有就用传下来的前台线程；永远不报收消息的线程自己

GE 补丁要的「lParam 不为 0」照样满足。

## 验证

`we/quick-build.sh --prep --base build/we-proton-ge11-7-28 --name test-0056 dlls/win32u`，`we/tests/activateapp_probe.sh <runner> <输出文件>`（Steam Linux Runtime 4.0、WoW64、私有 Xvfb，`activateapp_x11_helper.py` 当最简单的窗口管理器）：输出 `tests/activateapp_probe.ge28.txt`（打补丁前）和 `tests/activateapp_probe.ge28-0056.txt`。上表的跨线程、跨进程、桌面窗口几种情况和 Windows 逐行一致。

WE-Proton 的回归探测（包括 `noactivate_probe`）和 ge11-7-28 相同。

## 没修的差异（和这个补丁无关，打补丁前后一样）

- 同一进程的工作线程显示第一个窗口时，Windows 不让它抢前台（只在自己线程里激活，lParam 为 0）；Wine 让它成为前台
- 前台窗口被销毁后，Windows 激活另一个进程的窗口，Wine 激活同一进程的另一个线程的窗口
- `AttachThreadInput` 共享输入后切换，Windows 激活一侧报 0，Wine 报对方线程

## 没验证的

- 具体的程序和游戏（包括 GE 补丁原本针对的那个游戏）
- 32 位探测只编了，没在 Windows 上跑（行为不分位数）
