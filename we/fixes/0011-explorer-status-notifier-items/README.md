# 0011：只支持 StatusNotifierItem 的面板上看不到通知区图标

补丁：`patches/wine/0011-explorer-Show-systray-icons-as-StatusNotifierItems.patch`（`programs/explorer/systray.c`，新增 `programs/explorer/sni.c`、`programs/explorer/unixlib.h`。explorer 因此多了一个 unix 库 `x86_64-unix/explorer.so`）。

## 现象

在 niri + waybar 上，游戏平台客户端（32 位）启动游戏后把自己的主窗口隐藏起来，只留一个通知区（托盘）图标，要回到客户端得点这个图标。waybar 的托盘里没有这个图标，客户端窗口叫不回来，只能先退出游戏。

客户端窗口不是最小化，而是隐藏（`ShowWindow(SW_HIDE)`，X 窗口处于 Withdrawn 状态）。会话里没有任何程序拥有 `_NET_SYSTEM_TRAY_S0`。

## 根因

Wine 只用 XEmbed 系统托盘显示通知区图标：explorer 的 `show_icon()` 通过 `WINE_SYSTRAY_DOCK_INSERT` 让 winex11 把图标窗口嵌进 `_NET_SYSTEM_TRAY_S0` 的所有者。Wayland 合成器上的面板（waybar，KDE Plasma，GNOME 加 AppIndicator 扩展等）只实现 StatusNotifierItem（D-Bus 协议），没有 XEmbed 托盘，嵌入失败。

嵌入失败后，图标放进 explorer 自己的独立托盘窗口。启动器为了不在 Wayland 上多出一个白色小窗口，在前缀里关掉了这个窗口（`HKCU\Software\Wine\Explorer` 的 `ShowSystray=0`），所以图标在哪里都看不到。就算开着，它在平铺合成器上也只是一个普通窗口，不在面板里。

上游 Wine master（2026-10-08）同样只有 XEmbed，winewayland 驱动没有实现托盘。

## Windows 的行为

Windows 没有 StatusNotifierItem，要对照的是用户点通知区图标时，图标所有者收到哪些回调。Wine 处理嵌入式图标的代码（`tray_icon_wndproc()`）已经按 Windows 把鼠标消息转成回调：

- 所有图标都收到 `WM_xBUTTONDOWN`/`WM_xBUTTONUP`/`WM_xBUTTONDBLCLK`
- `NOTIFYICON_VERSION_4` 的图标，左键抬起后再收到 `NIN_SELECT`，右键抬起后再收到 `WM_CONTEXTMENU`，`wParam` 带屏幕坐标

补丁让面板报告的点击走同一段代码，回调格式没有改。点击在 Windows 上没有实测：通知区图标没法从 SSH 会话里点。

## 修复

explorer 不用虚拟桌面时（root 模式），启动托盘前先看会话总线上有没有 `org.kde.StatusNotifierWatcher`：

- 有，所有图标都用 StatusNotifierItem 显示
- 没有，和原来一样走 XEmbed

D-Bus 部分放在 explorer 新的 unix 库里。`libdbus-1.so.3` 用 dlopen 加载，和 mountmgr 的做法一样；构建时没有 dbus，就只编进返回 `STATUS_NOT_SUPPORTED` 的空函数。

### 图标

- 每个图标各开一个 D-Bus 连接，占一个名字 `org.kde.StatusNotifierItem-<pid>-<n>`（Qt、libappindicator 也这样做），然后向 watcher 注册。连接一关，图标就从面板上消失
- 属性：
  - `Id`：图标所有者的 exe 文件名
  - `Title`、`ToolTip`：图标的提示文字
  - `IconPixmap`：把 HICON 用 `DrawIconEx` 画成 16、24、32、48 四个尺寸，去掉预乘，转成网络字节序的 ARGB。没有 alpha 的图标按掩码补 alpha，和 `paint_layered_icon()` 的做法一样
- 没有 `Menu` 属性，所以面板在右键时调用 `ContextMenu`，菜单由程序自己弹
- 图标、标题、提示真的变了，才发对应的 `NewIcon`/`NewTitle`/`NewToolTip`：很多程序只改提示时也把同一个图标再设一遍（那个客户端下载时每秒改一次提示），不比较的话面板每秒都要重取一遍图标

### 点击

面板调用 `Activate`、`ContextMenu` 或 `SecondaryActivate` 时，explorer 这样处理：

1. 把光标移到面板给的坐标。Windows 上点托盘图标时光标就在图标上，程序常用 `GetCursorPos` 决定菜单的位置。面板的坐标从整个桌面的左上角算，Wine 的屏幕坐标从主显示器的左上角算，所以要加上虚拟屏幕的原点（`SM_XVIRTUALSCREEN`、`SM_YVIRTUALSCREEN`）
2. 给图标窗口发左键、右键或中键的按下和抬起。此后的回调和嵌入式图标完全一样

和上一次 `Activate` 的间隔在双击时间内的 `Activate` 当作双击，发 `WM_LBUTTONDBLCLK`。waybar 一次双击会调三次 `Activate`：两次按下各一次，GTK 的双击事件再一次。第三次丢掉。

unix 库里有一个专门的线程，poll 所有连接、应答面板的属性查询。收到点击后，它用 `PostMessage` 交给图标窗口，由 explorer 的托盘线程处理。

### 其他

- 气泡提示（`NIF_INFO`）改用桌面通知显示（`org.freedesktop.Notifications.Notify`）
- 面板重启后 watcher 换了所有者。explorer 收到 `NameOwnerChanged` 后，把现有图标重新注册一遍

## 验证

### 探测程序

`we/tests/tray_sni_probe` 和 `sni_host.py` 配合使用：`sni_host.py` 在 `dbus-run-session` 起的私有会话总线上扮演面板。

修复前（release 11，`tray_sni_probe.wine11.txt`）：面板一直收不到注册。

修复后（release 13，`tray_sni_probe.wine13.txt` 和 `tray_sni_probe.v0.wine13.txt`），图标正常注册，各项都符合预期：

- **属性**：`Id`、`Title`、`ToolTip` 正确，包括中文
- **图标像素**：四个尺寸的颜色都对
  - 带 alpha 的图标，50% 透明的绿色还原成 `8000ff00`
  - 只有掩码的图标，透明部分的 alpha 是 0
- **改提示**：程序把同一个图标连同新提示再设一遍，面板只收到 `NewTitle`/`NewToolTip`，再读到的是新提示
- **气泡**：变成一条桌面通知
- **面板重启**：watcher 重启后图标重新注册
- **点击**：左键、右键、双击（按 waybar 的方式调三次 `Activate`）、中键，程序收到的回调序列、回调里的坐标、光标位置都对。版本 4 和旧格式都测了
- **删除**：程序删除图标后，它的名字从总线上消失

同一个测试经 Proton 在 Steam Linux Runtime 4.0 容器里跑（pressure-vessel 转发会话总线），面板这边的输出和直接运行时一样。Proton 不转发程序的标准输出，所以只比较了面板这边。

会话里没有 watcher 时，explorer 记一行 `no StatusNotifierWatcher on the session bus`，图标照旧走 XEmbed。

### Wine 自己的测试

修复前后（release 11 / 12 / 13），x86_64，Xvfb，有和没有 watcher 各跑一遍：

| 测试 | 结果 |
|---|---|
| `shell32:systray` | 10 项，0 失败，各种情况相同 |

有 watcher 时，release 12、13 都向 watcher 注册了这个测试的 2 个图标。

### 实机

niri + waybar，release 12（第一版补丁），那个客户端：

- 图标出现在 waybar 的托盘里，`Id` 是客户端的 exe 名，提示随下载进度更新
- 右键弹出客户端自己的菜单

但菜单是一个普通窗口，出现在屏幕中间，带窗口边框，见下面「另外看到的问题」。提示每秒更新、每次都连带发 `NewIcon` 也是这次看到的，release 13 的补丁改成了只发真正变了的信号。

## 另外看到的问题

客户端的菜单是它自己画的窗口，不是 `TrackPopupMenu`。它弹出时会激活这个窗口，Wine 的 winex11 因此把它当成普通的顶层窗口交给窗口管理器（不是 override-redirect，`_NET_WM_WINDOW_TYPE_NORMAL`，没有 owner）。niri 通过 xwayland-satellite 管理 X 窗口，Wayland 的顶层窗口不能由程序指定位置，于是 niri 把它当浮动窗口放在屏幕中间。这和托盘图标怎么显示无关，菜单从任何地方弹出来都一样，不在这个补丁里处理。

另外，在 XWayland 上鼠标停在面板上时，X 的指针位置不会更新，`SetCursorPos` 的指针移动也不一定生效。Wine 在 `SetCursorPos` 之后 100 ms 内用自己记的位置，之后改问 X，所以超过 100 ms 才调 `GetCursorPos` 的程序拿到的是旧位置。

## 没有验证的部分

- 释放版（release 13）的补丁还没在实机上试过；坐标换算只在单显示器的 Xvfb 上跑过，那里虚拟屏幕的原点是 (0, 0)
- 只按协议和 waybar 的实现测了，KDE Plasma、GNOME 的面板没有测
- 显示器有缩放时，面板的坐标（逻辑像素）和 X 根窗口的坐标可能对不上
- 动画图标（频繁 `NIM_MODIFY`）每次都要重画四个尺寸，开销没有测
- i386 的 `explorer.exe` 走 unix 调用的 WoW64 表，没有单独跑过；Proton 里桌面进程是 64 位的 explorer

## 排查过程

1. 用户启动游戏后，客户端窗口消失。它的 X 窗口处于 Withdrawn 状态；根窗口上没有 `_NET_SYSTEM_TRAY_S0` 的所有者；会话总线上 waybar 拥有 `org.kde.StatusNotifierWatcher`。也就是说，客户端是隐藏到托盘，而托盘图标没地方显示
2. 考虑过的办法：
   - 打开 Wine 的独立托盘窗口（`ShowSystray=1`）：多一个白色小窗口，在 niri 上被平铺，启动器当初正是为了这个才关掉它
   - 外部的 XEmbed→SNI 桥（snixembed、xembedsniproxy）：要用户另跑一个守护进程，xembedsniproxy 还依赖 KDE
   - 在 explorer 里直接实现 StatusNotifierItem：用户选了这个
3. 第一版（release 12）实机：用户右键托盘图标，菜单弹出来了，但成了屏幕中间的一个窗口。用 D-Bus 调一次 `ContextMenu`，同时用 python-xlib 抓新映射的 X 窗口、用 `niri msg event-stream` 看 niri 怎么处理：菜单窗口 250×280，Wine 把它放在 X 坐标 (1707, 0)，也就是主显示器的左上角（Win32 的 (0, 0)）；niri 把它当浮动窗口放到屏幕中间。窗口一失去焦点，客户端就把它关掉
4. 这台机器的主显示器在 X 坐标 1707 处，面板给的坐标和 Wine 的屏幕坐标差了这么多，第一版没有换算。xwayland-satellite 只把 override-redirect、菜单/提示类窗口类型，以及「工具窗口 + 无装饰 + 固定大小」的窗口当成弹出层，其他窗口都是顶层窗口，位置由合成器决定
5. 同时看到客户端下载时每秒改一次提示，每次都带着同一个图标；第一版每次都发 `NewIcon`，面板每秒重取一遍图标
