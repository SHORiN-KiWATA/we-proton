# 0066–0069：分层子窗口不显示在父窗口的 3D 画面上

补丁（临时补丁：DW-Proton 上游有了自己的修复之后，换成那边的，这四个删掉）：

- `patches/wine/0066-win32u-Let-winex11-blend-layered-child-windows-over-.patch`（`dlls/win32u/window.c`、`dlls/winex11.drv/init.c`、`include/wine/gdi_driver.h`）
- `patches/wine/0067-winex11-Keep-the-last-frame-when-a-client-window-goe.patch`（`dlls/winex11.drv/init.c`）
- `patches/wine/0068-win32u-Let-winewayland-show-layered-child-windows-as.patch`（`dlls/win32u/window.c`）
- `patches/wine/0069-winewayland-Show-a-child-window-with-a-surface-as-a-.patch`（`dlls/winewayland.drv/window.c`、`wayland_surface.c`）

## 现象

《蓝色星原：旅谣》（繁中版 1.0.2.2，2560×1440 无边框全屏，niri + xwayland-satellite，NVIDIA RTX 4060 笔记本）能启动，但进游戏后整屏是黑的，什么也点不了。用 Wine 的 Wayland 驱动也一样。

黑的是游戏自己的画面：它在等发行商 SDK 的「使用者协议」对话框，而对话框没有显示出来。对话框之后的登录框也是同一种窗口。

## 根因

SDK 的界面是 Qt 5 写的，窗口是这样来的（`WINEDEBUG=+win,+x11drv` 的日志）：

1. 建一个隐藏的顶层窗口，`WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN`，640×480，再加上 `WS_EX_LAYERED | WS_EX_TOOLWINDOW`
2. `SetParent` 到游戏主窗口下（样式不改，仍是 `WS_POPUP`）。游戏主窗口是 `WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS`，没有 `WS_CLIPCHILDREN`，画面由 D3D11 交出
3. 调成和主窗口客户区一样大，`ShowWindow(SW_SHOWNOACTIVATE)`
4. 用 `UpdateLayeredWindowIndirect` 提交图像（每像素 alpha，带脏矩形），**整体不透明度是 0**
5. 再用不带图像的 `UpdateLayeredWindow(hwnd, NULL, NULL, NULL, NULL, NULL, 0, &blend, ULW_ALPHA)` 把整体不透明度一步步调到 255（Qt 的窗口淡入动画）

也就是一个分层的子窗口，铺满父窗口，叠在父窗口的 3D 画面上，半透明的部分要透出下面的游戏画面。Windows 8 起支持这种窗口，由系统合成。Wine 里有三处接不上：

**一、X11 驱动没有地方画它。** 0007 之后，分层子窗口没有自己的表面，`UpdateLayeredWindow` 的内容画进父窗口的 GDI 表面。但父窗口的 3D 画面在另一个 X 窗口里（客户区窗口，是顶层 X 窗口的子窗口），盖在 GDI 表面上面，画进去的东西全被挡住。父窗口带 `WS_CLIPCHILDREN` 时客户区窗口会转成离屏、每帧拷到顶层窗口上，这时能看到分层子窗口，但透明的地方是黑的，而且拷帧时被子窗口的区域裁掉，下面的画面不更新。没有 0007 的 DW-Proton 上走的是另一条路（子窗口的 X 窗口在 `SetParent` 时被销毁重建），结果一样是看不到。

**二、只改不透明度的调用被忽略了。** 0007 的 `update_layered_child()` 开头是 `if (!hdc_src) return TRUE`，第 4 步以不透明度 0 画了一张「什么都没有」的图，第 5 步全部被丢掉，图像永远停在不透明度 0。所以即使有地方画，也是一片空白（叠在黑底上就是全黑）。

**三、Wayland 驱动本来能做，被 0007 关掉了。** winewayland 会把有自己表面的子窗口做成顶层窗口的子表面（`wl_subsurface`），由合成器混合，正好是需要的效果。0007 为了 X11 让所有驱动的分层子窗口都不要表面，Wayland 下也就只能画进父窗口的表面：父窗口铺满屏幕时被 3D 画面（另一个子表面）挡住；父窗口带标题栏时驱动把 3D 子表面垫到下面，弹窗露出来了，但透明处是黑的，游戏画面被整个盖住。放开之后驱动里还有两处不对：

- 子表面的位置是「窗口矩形 − 顶层窗口矩形」，对真正的子窗口来说窗口矩形是相对父窗口客户区的，少加了顶层窗口客户区的偏移，父窗口带标题栏时偏 (4, 30) 像素
- 一个窗口要不要留在顶层窗口的子表面树里，是按「不受管理的顶层窗口」的规则（看样式）定的。`SetParent` 进来的弹窗带着 `WS_POPUP`，第二次显示时被判成不受管理的顶层窗口，从树里摘了出去，窗口矩形还被改成了 (0,0) 起

## Windows 行为，以及依据

**没有在 Windows 上实测**：Windows 虚拟机开机要占走独显，这次用户不在，没有开。依据是文档和这个游戏在 Windows 上的表现：

- *Extended Window Styles* 里 `WS_EX_LAYERED` 的说明：“Windows 8: The WS_EX_LAYERED style is supported for top-level windows and child windows. Previous Windows versions support WS_EX_LAYERED only for top-level windows.”
- *Window Features* 的 Layered Windows 一节：“The system automatically composes and repaints layered windows and the windows of underlying applications.”；“because the system stores the image of a layered window, the system will not ask the window to paint if parts of it are revealed”
- `SetParent` 的 Remarks：“For compatibility reasons, SetParent does not modify the WS_CHILD or WS_POPUP window styles of the window whose parent is being changed.” 所以带着 `WS_POPUP` 的窗口也可以是子窗口，不能按 `WS_CHILD` 样式判断（win32u 的 `is_child()` 就是按样式判断的，第一版因此没生效）
- 这个游戏在 Windows 上对话框叠在游戏画面上、淡入显示，是它正常的样子

探测程序 `we/tests/layered_popup_child_probe.exe selfgrab` 可以在 Windows 的交互会话里直接跑，读屏幕上的颜色，留着以后补测。

## 修复

**0066（win32u + winex11）**

- win32u 给每个分层子窗口存一份图像（预乘的 ARGB）和一个整体不透明度，两者分开：`UpdateLayeredWindow` 带图像时更新图像（只更新脏矩形），不带图像、带 `ULW_ALPHA` 时只改不透明度。色键和不带 alpha 的图像转成 alpha（色键处 0，其余 255）
- 给驱动三个函数：`get_layered_children()` 列出一个窗口下面可见的、有内容的分层子窗口和它们在客户区里的位置；`lock_layered_child_bits()` / `unlock_layered_child_bits()` 取图像和上次之后变了的矩形。图像全透明或不透明度为 0 的窗口不列出来
- winex11：窗口下面有这样的子窗口时，客户区窗口转成离屏（Proton 已有的路径：每帧从客户区窗口拷到顶层窗口）。拷之前先把这一帧拷到一张中间图上，把每个子窗口的图像按不透明度用 XRender 混上去，再把中间图拷到顶层窗口。子窗口的图像存在服务器端的 32 位 pixmap 里，只在变化时上传变了的矩形。拷帧的裁剪区域加上子窗口占的区域（父窗口带 `WS_CLIPCHILDREN` 时原来会把这块裁掉）
- 窗口在 3D 父窗口里面时，不再往父窗口的 GDI 表面上画（否则每次更新会闪一下黑底）；图像、不透明度、位置、显隐变化时，让驱动用最后一帧重新合成一次，不用等下一帧
- 纯 GDI 的父窗口照旧画进父窗口的表面（黑底上混合，和 0007 一样），但现在只改不透明度的调用会用存下的图像重画，隐藏时让父窗口重画被盖住的那块（原来会留下残影）
- 窗口销毁、`SetParent` 回桌面、去掉 `WS_EX_LAYERED`、调用 `SetLayeredWindowAttributes` 时丢掉图像

**0067（winex11）** 客户区窗口转成离屏的一瞬间，它会拿到一张新的空 pixmap，顶层窗口上它原来的位置被清成黑色，而下一帧可能还没到。转之前把它当前的画面存下来，转之后放回客户区窗口和顶层窗口里它原来的位置。没有这个补丁时，对话框每次出现都会闪一两帧黑屏。

**0068（win32u）** 驱动自己没有 GDI 函数（winewayland）时，分层子窗口保留自己的表面，走 Wine 原来的路径，驱动把它做成子表面。另外，分层子窗口丢掉自己的表面时（`SetWindowPos` 改大小之后、下一次 `UpdateLayeredWindow` 之前），不再把旧表面的内容拷进父窗口的表面，否则父窗口上会留下一份带黑底的旧图。

**0069（winewayland）** 有父窗口的窗口，不管样式是什么，都留在顶层窗口的子表面树里（也不会被当成通知窗口或全屏垫底窗口）；它的子表面位置按「相对父窗口客户区的坐标 → 顶层窗口客户区 → 加上顶层窗口客户区在表面里的偏移」算。

## 验证

探测程序 `we/tests/layered_popup_child_probe.c`：父窗口用 D3D11 一直交纯色帧（先蓝后青），弹窗按上面第 1–3 步建出来，图像分三段：不透明绿、50% 红、全透明。在父窗口宽度的 1/12、3/12 … 11/12 处读颜色。变体：`clip`（父窗口带 `WS_CLIPCHILDREN`）、`caption`（带标题栏）、`full`（铺满显示器）、`flip`（flip 模型交换链）、`indirect`（带脏矩形）、`fade`（按第 4、5 步淡入）、`gdi`（父窗口用 GDI 画）。

结果是 `we/tests/layered_popup_child_probe.ge32.txt`（修复前，`we-proton-ge11-12-32`）和 `.ge33.txt`（修复后，`we-proton-ge11-14-33`），X11 驱动、无头 gamescope 里读父窗口的 X 窗口；Wayland 驱动的是 `.wayland.ge32.txt` / `.wayland.ge33.txt`，在 niri 里截窗口取色。六个取样点的颜色（弹窗铺满时，前两点在绿色段，中间两点在 50% 红段，后两点在全透明段；父窗口的画面是蓝 `0000ff`、之后青 `008080`）：

X11，3D 父窗口（默认、`clip`、`caption`、`flip`、`full indirect fade` 的结果相同，除了修复前的两处差别）：

| 步骤 | 修复前 | 修复后 | 应该是 |
|---|---|---|---|
| 弹窗显示 | `0000ff` ×6（看不到弹窗）；`clip`：`00ff00 00ff00 800000 800000 000000 000000`（透明处是黑的） | `00ff00 00ff00 80007f 80007f 0000ff 0000ff` | 绿、绿、红叠蓝、红叠蓝、蓝、蓝 |
| 父窗口换成青色 | `008080` ×6；`clip`：和上一步一样（下面的画面不更新） | `00ff00 00ff00 804040 804040 008080 008080` | 透明处跟着变 |
| 弹窗只盖右半边 | `008080` ×6；`clip`：`… 00ff00 800000 000000` | `008080 008080 008080 00ff00 804040 008080` | |
| 隐藏 | `008080` ×6 | `008080` ×6 | |
| `fade`：不透明度 128 | `0000ff` ×6 | `00807f 00807f 4000bf 4000bf 0000ff 0000ff` | 50% 的绿叠蓝、25% 的红叠蓝 |
| `fade`：不透明度 255 | `0000ff` ×6（`clip` 和 GDI 父窗口下是全黑） | 同「弹窗显示」 | |

X11，GDI 父窗口（`gdi`）：显示时修复前后都是 `00ff00 00ff00 800000 800000 000000 000000`（黑底，没变）；隐藏后修复前留着残影（`… 00ff00 800000 000000`），修复后是父窗口自己的颜色；`gdi fade` 修复前全黑，修复后和 `gdi` 一样。

Wayland（`fade`、`caption`、`gdi`）：修复前 `fade` 下弹窗始终不出现；`caption` 下出现了但在黑底上（`00ff00 00ff00 800000 800000 000000 000000`，父窗口的画面被盖住）；修复后三种都是 `00ff00 00ff00 80007f 80007f 0000ff 0000ff` → `… 804040 804040 008080 008080` → 右半边 → 隐藏 → 再显示 → 销毁，每一步都对。`caption` 下弹窗的左上角在客户区原点（修 0069 之前在 (−4, −30)）。

在和游戏相同的环境里（xwayland-satellite，2560×1440，`full indirect fade`）连续截图（每秒约 30 张）：弹窗出现、移动、再显示时没有黑帧（修 0067 之前每次切换有一张全黑或「弹窗叠在黑底上」的截图）。弹窗改了大小、新图像还没提交的那一帧，旧图像按原尺寸画在新位置上（一张截图，约 30 毫秒）。

**游戏**（修复后的 runner，X11 驱动和 Wayland 驱动各跑一次，没有登录）：进游戏后协议对话框显示在画面中央，能勾选「我已详阅并同意」、点「同意」；之后是游戏的标志画面，再之后登录框（邮箱、密码、Google/Apple）叠在实时的 3D 场景上，圆角和阴影的半透明边缘正常，背景在动（隔 0.7 秒的两张截图有两成到八成的像素不同），登录框本身不变。最终构建 `we-proton-ge11-14-33` 上两种驱动又各跑了一次到登录框。

**没有被改坏的**：

- 0007 的探测程序 `layered_child_probe`（`WS_CHILD` 的分层子窗口在 GDI 父窗口里），Xvfb 上读屏幕像素：新基底不带这四个补丁、带这四个补丁、0007 当时的记录（`layered_child_probe.wine8.txt`、`.pos.wine8.txt`）三者相同
- `we/tests/regress.sh`（13 个探测程序）：带补丁和不带补丁的结果全部 `same`，和上一版 `we-proton-ge11-12-32` 的基线也全部 `same`
- Wine 自己的测试（`user32_test.exe`，Xvfb，没有窗口管理器），新基底不带补丁和带补丁各跑一遍，失败的行逐条相同：

  | 测试 | 结果 |
  |---|---|
  | `user32:win` | 68391 项，3 失败，前后相同 |
  | `user32:msg` | 30617 项，33 失败，前后逐条相同 |
  | `user32:input` | 6 失败，前后相同 |
  | `user32:dce` | 212 项，0 失败 |

  `user32:win` 的三个失败里有两个靠真实鼠标输入（`win.c:10624`、`10625`），带补丁跑的四次（三次是最终构建，一次是定稿前的版本）里有一次这两项碰巧通过了，另外三次和不带补丁的两次一样是 3 个失败。测试程序是以前编好的（`build/wine-tests`，10 月的 Wine 11.0 树），新旧 runner 用的是同一个。这些测试里没有分层子窗口的用例，只能说明别的路径没有被改坏

## 排查过程和弯路

1. 游戏目录里的 SDK 日志（`pcsdk_log.txt`）显示协议窗口已经建好、`sdk window exists: true`，画面上却没有，确定是窗口显示的问题而不是游戏卡住
2. 带 `+win,+x11drv` 跑了一次游戏（只到协议界面，没有登录），拿到窗口的样式和调用顺序；X 窗口树里游戏窗口只有一个子窗口（客户区窗口），弹窗没有 X 窗口
3. 一开始以为屏幕上的黑色是 0007 的「透明处涂黑」。实际上父窗口没有 `WS_CLIPCHILDREN`，弹窗根本没露出来，黑的是游戏自己的画面
4. 第一版在探测程序上通过，在游戏里没反应。原因是探测程序一次就以不透明度 255 提交图像，游戏是淡入的（上面的第二条根因）。带跟踪又跑了一次游戏才看出来：图像更新了两次，都是「全透明」，之后的四次调用没有进到存图像的地方。探测程序加了 `fade` 之后能复现
5. 第一版里用 `is_child()` 判断父子关系，它要求 `WS_CHILD` 样式，这种弹窗是 `WS_POPUP`，一个也找不到
6. 网上有人为这个游戏做了一个 DW-Proton 的临时版：在 `SetParent` 里认 `UnityWndClass` 类名，把「设父窗口」换成「设 owner」，让弹窗继续当顶层窗口。它只认这一种组合，弹窗的坐标也从「相对父窗口」变成了屏幕坐标（窗口化时位置不对），没有采用

## 没验证的

- Windows 上的实际表现，尤其是：父窗口带 `WS_CLIPCHILDREN` 时分层子窗口下面露出什么（这里按「露出父窗口的画面」做）；`SetWindowPos` 改了大小、还没提交新图像时旧图像怎么画（这里不拉伸，按原尺寸画、超出部分裁掉）；全透明的像素是否接收鼠标（Wine 里接收）
- 登录之后的游戏内容：没有登录账号，只验证到登录框
- 纯 GDI 父窗口下透明处仍是黑底，父窗口自己重画（不带 `WS_CLIPCHILDREN`）会盖掉子窗口的图像，直到它下次更新。0007 就是这样，这次没改
- 分层子窗口叠在**兄弟**窗口的 3D 画面上（不是父子关系）的情况不处理
- Wayland 驱动下弹窗没有淡入的中间态，一出现就是完全不透明（niri 下驱动对子表面的整体不透明度没有生效），最终画面是对的
- 32 位程序、OpenGL 的父窗口：代码路径相同，没有单独测
