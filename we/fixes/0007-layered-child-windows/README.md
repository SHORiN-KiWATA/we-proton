# 0007：分层子窗口被做成了单独的顶层窗口

补丁：`patches/wine/0007-win32u-Draw-layered-child-windows-into-their-parent-.patch`（`dlls/win32u/window.c`）。

## 现象

一个程序启动时的闪屏/登录窗口，在 niri + xwayland-satellite 下变成两个窗口：一个是空白（白色）的窗口，被平铺；画面在另一个单独浮动的窗口里。画面不跟着前一个窗口走，窗口交互也不正常。

Win32 这边只有一个顶层窗口，结构是：

- 宿主：`WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN`，自己去掉了非客户区，960×540，居中
- 画面：宿主的子窗口，`WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN`，扩展样式 `WS_EX_LAYERED | WS_EX_NOACTIVATE`，铺满宿主客户区，用 `UpdateLayeredWindow` 更新

X11 上却有两个顶层窗口：宿主是普通的受管窗口；画面子窗口有一个自己的 override-redirect 窗口，挂在根窗口下，位置是 Win32 屏幕坐标的原点（主显示器左上角）。在普通 X11 窗口管理器下也一样是两个，只是画面出现在屏幕左上角；在 xwayland-satellite 下，没有父窗口的 override-redirect 窗口成了一个独立的 Wayland 顶层，被 niri 当成另一个浮动窗口居中摆放。

## 根因

Windows 8 开始，`WS_EX_LAYERED` 可以用在子窗口上（文档 *Extended Window Styles* 里 `WS_EX_LAYERED` 的说明：“Windows 8: The WS_EX_LAYERED style is supported for top-level windows and child windows. Previous Windows versions support WS_EX_LAYERED only for top-level windows.”）。Wine 处理它的链路：

1. `NtUserUpdateLayeredWindow()` 对任何窗口都用 `create_layered = TRUE` 调 `get_window_surface()`。子窗口本来没有自己的表面（画在父窗口的表面上），但 `create_layered` 让 `needs_surface` 变成 TRUE，于是给子窗口建了一个 layered 表面
2. winex11 的 `X11DRV_CreateWindowSurface()` 对 layered 表面调 `set_window_visual( data, &argb_visual, TRUE )`
3. `set_window_visual()` 在 visual 不同时 `destroy_whole_window()` + `create_whole_window()`，不检查窗口是不是顶层。子窗口本来没有 whole window，这一步在根窗口下给它建了一个：`is_window_managed()` 对子窗口返回 FALSE，所以是 override-redirect；位置取 `data->rects.visible`，对子窗口这是相对父窗口客户区的坐标，被当成了屏幕坐标
4. 之后子窗口的移动、改大小都按这套坐标同步到这个 X 窗口，所以它永远不跟着父窗口

上游 Wine master（2026-10-08）这几处代码相同。

## 修复

- `NtUserUpdateLayeredWindow()` 对子窗口不再请求 layered 表面。子窗口和普通子窗口一样没有自己的表面，`get_window_surface()` 返回 NULL，绘制落在它和父窗口共用的表面上
- 新增 `update_layered_child()`：用子窗口的 DC（`DCX_CACHE | DCX_WINDOW | DCX_CLIPSIBLINGS | DCX_CLIPCHILDREN`）把源 DC 的内容画上去，再 flush 顶层窗口的表面：
  - `ULW_ALPHA`：先涂黑，再按调用方给的 `BLENDFUNCTION` 做 `AlphaBlend`（每像素 alpha 和整体透明度都生效）
  - `ULW_COLORKEY`：`TransparentBlt`
  - 其他：`BitBlt`
- 用 `pptDst`/`psize` 移动或改变子窗口大小时，服务器对还没设过 layered 属性的 `WS_EX_LAYERED` 窗口强制 `SWP_NOREDRAW`，被它让出来的那块父窗口不会重画，所以手动让父窗口重画这块区域（旧矩形减新矩形）

顶层窗口的路径没有改。

## 依据，以及没有实测的部分

- 「分层子窗口的内容显示在父窗口里、随父窗口移动」：上面引的文档
- **透明像素下面是什么，没在 Windows 上实测**。Windows 由 DWM 合成；父窗口带 `WS_CLIPCHILDREN` 时自己不会画到子窗口下面。补丁在黑色上混合，和 Wine 给新建的顶层 layered 表面的初始内容一致；内容完全不透明时没有区别。这次没开 Windows 虚拟机（开机会占走独显和蓝牙网卡）
- **`pptDst` 对子窗口按父窗口客户区坐标解释**：沿用 Wine 原来的解释，没实测

## 验证

- `we/tests/layered_child_probe`：宿主画蓝色、带 `WS_CLIPCHILDREN`；分层子窗口铺满宿主，左半不透明绿、右上 50% 红、右下全透明。`layered_child_grab.py` 在 Xvfb 上运行它，每一步从 X 根窗口读像素、列出顶层 X 窗口
  - 修复前（`layered_child_probe.wine7.txt`）：子窗口有自己的 override-redirect 窗口，在 `+0+0`；宿主所在位置是空白（白色），画面在屏幕原点；宿主移动后画面留在原处；用 `pptDst` 把子窗口右移半个宽度后，那个 X 窗口移到了屏幕上的 `+160+0`
  - 修复后（`layered_child_probe.wine8.txt`，带 `pptDst` 的是 `layered_child_probe.pos.wine8.txt`）：只有宿主一个 X 窗口，宿主位置上依次是绿、128 红（50% 红叠在黑上）、黑；宿主移动后、子窗口再次更新后都一样；子窗口右移半个宽度后，让出来的左半边是宿主自己画的蓝色，右半边是子窗口的绿色
- 原来出问题的程序（niri + xwayland-satellite）：修复前启动时 X11 上有两个顶层窗口（宿主 + override-redirect 画面窗口），niri 里是一个平铺、一个浮动；修复后只有宿主一个，闪屏和登录画面都在宿主里
- Wine 自己的测试，Xvfb 无窗口管理器，修复前后（release 7 / 8）：

  | 测试 | 结果 |
  |---|---|
  | `user32:win` | 68344 项，3 失败，前后相同 |
  | `user32:msg` | 30620 项，35 失败，前后逐条相同 |
  | `user32:input` | 6 失败，前后相同 |
  | `user32:dce` | 212 项，0 失败 |

  机器负载高时（load average 50 以上）跑的三次 release 8 的 `user32:msg`，各多出 1～2 个失败，都是鼠标悬停、`TrackMouseEvent`、IME 的消息序列里插进了别的消息，每次不同；负载低时重跑，和 release 7 逐条相同。`user32` 的测试里没有分层子窗口的用例（`msg.c` 的 `UpdateLayeredWindow` 测试只用顶层窗口），所以这些测试只能说明顶层窗口的路径没有被改坏

## 排查过程

1. 录 X11 根窗口的 SubstructureNotify（创建、映射、配置、属性）和 niri 的 event-stream，同时每 0.6 秒截屏：确认「两个窗口」是同一个进程的一个受管窗口加一个 override-redirect 窗口，后者没有 `WM_TRANSIENT_FOR`，比受管窗口早映射
2. 在同一个 wineserver 里跑一个枚举程序（`EnumWindows` + `EnumChildWindows`，读 `__wine_x11_whole_window` 属性对应 X 窗口）：override-redirect 窗口属于一个 `WS_CHILD | WS_EX_LAYERED` 的子窗口——子窗口本来不该有 X 窗口
3. 顺着 `UpdateLayeredWindow` → `get_window_surface` → `X11DRV_CreateWindowSurface` → `set_window_visual` 找到建窗口的地方

## 另外看到的问题（没修）

- **宿主被平铺、界面被拉伸**：宿主带 `WS_THICKFRAME`，Wine 认为它可以调大小，不设固定大小提示（`WM_NORMAL_HINTS` 没有最小/最大尺寸），niri 就把它平铺，程序再按平铺后的尺寸重新排版。这个程序在 Windows 上靠 `WM_NCHITTEST` 不返回边框来禁止拖大小，`WM_GETMINMAXINFO` 也没有给出固定尺寸（最小 0×0、最大为默认的屏幕尺寸），Wine 没有依据把它当成固定大小。要在合成器的窗口规则里处理
