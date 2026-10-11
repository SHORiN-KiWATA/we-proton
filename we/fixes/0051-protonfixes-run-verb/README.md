# 0051：设了 PROTONFIXES_RUN_VERB=1 时，用 `run` 启动的程序也执行 protonfixes

补丁：`patches/protonfixes/0051-protonfixes-Also-run-fixes-for-run-when-PROTONFIXES_.patch`（`__init__.py`）。`we/overlay-build.sh` 同时学会把比基础版本多出来的 protonfixes 补丁打进发布包。

## 现象

shorin-proton-wrapper 把同一个前缀里的程序当成一个会话：第一个程序用 `proton waitforexitandrun` 启动，之后的程序用 `proton run` 加入正在运行的 wineserver。

在《战锤40K：暗潮》（Warhammer 40,000: Darktide，Steam 1361210）里：

- 前缀里没有别的程序时，从 Steam（经 wrapper）启动，游戏正常出窗口；
- 先在同一个前缀里开着别的程序（这次是游戏加速器），再从 Steam 启动，游戏进程起来了、有声音，但窗口一直不出来。有没有用 pasta 网络命名空间都一样。

## 定位

1. `WINEDEBUG=+win,+x11drv`：游戏主窗口的 `ShowWindow(SW_SHOW)` 被 Wine 换成了隐藏：

   ```
   trace:win:show_window hwnd=0xb033e, cmd=5, was_visible 0
   trace:win:show_window hwnd=0xb033e, using cmd 0 from startup info.
   ```

   X 窗口一直是 `IsUnMapped`，窗口管理器从没接管过它（没有 `WM_STATE`）。
2. 从游戏进程的内存读出进程参数（`RTL_USER_PROCESS_PARAMETERS`，按命令行字符串反找 `CommandLine.Buffer` 定位）：`dwFlags = STARTF_USESHOWWINDOW`、`wShowWindow = SW_HIDE`。游戏是被它的父进程，即游戏自带的 .NET 启动器（在 Wine Mono 里运行），以隐藏方式启动的。Wine 在第一次 `ShowWindow` 时按启动信息改写显示方式（`dlls/win32u/window.c` 的 `show_window()`），这是照 Windows 的规则做的，Wine 这边没有错。
3. 同一个游戏单独启动（能出窗口）时，读到的是 `dwFlags = 0`、`wShowWindow = SW_SHOWNORMAL`，而且启动器的日志里根本没有这一次的记录：启动器没有运行。
4. protonfixes 里这个游戏的修正（`gamefixes-steam/1361210.py`）就是绕开启动器：`util.replace_command('launcher/Launcher.exe', 'binaries/Darktide.exe')`，注释写的正是这个现象（游戏作为后台进程启动、只有声音）。
5. 窗口出不来的每一次，Proton 的输出里都是 `ProtonFixes WARN: Skipping fix execution. We are probably running a unit test.`：`check_conditions()` 要求 `'waitforexitandrun' in sys.argv[1]`，`run` 不满足，修正整个跳过。
6. 加入会话的程序不能改用 `waitforexitandrun`：Proton 处理这个动词时先执行 `wineserver -w`，等当前的 wineserver 退出，也就是等前缀里先开的程序全部关掉。

Windows 上的行为：这是 Proton 一侧决定要不要执行游戏修正的逻辑，和 Windows 无关；启动器在 Windows 上会不会把游戏隐藏启动没有测（修正绕开了启动器，不依赖这一点）。

## 修复

- `check_conditions()`：动词是 `run` 并且 `PROTONFIXES_RUN_VERB=1` 时也执行修正。`PROTONFIXES_DISABLE` 仍然优先；读 `sys.argv[1]` 之前先要求至少两个参数（原来 `len(sys.argv) >= 1` 时取 `sys.argv[1]` 会抛异常）。
- 不设这个变量时行为不变，`run` 照旧不执行修正。这一点要保持：Steam 自己也用 `run` 启动不是游戏本体的程序，这台机器的 Steam 日志里有 38 次 `proton run …/legacycompat/iscriptevaluator.exe`（首次启动前的安装脚本）和 1 次 `proton run …/d3ddriverquery64.exe`（查询显卡驱动）。安装脚本 protonfixes 另有排除，但驱动查询如果也执行游戏的修正，会被追加游戏的启动参数、在那时去装组件。所以只在启动器明确要求时打开。
- shorin-proton-wrapper 在加入正在运行的会话（用 `run`）时设 `PROTONFIXES_RUN_VERB=1`。
- `we/overlay-build.sh`：官方发布包里的 protonfixes 已经打好了基础版本 `patches/protonfixes` 里的补丁（DW 的那些）；比基础版本多出来的补丁用 `patch -Np1` 打到发布包的 `protonfixes/` 上（Python，不用编译），记进 `we-proton-build-info`，并复制到 `we-proton-patches/protonfixes/`。基础版本已有的补丁被改动或删掉时，脚本报错退出，这种情况要走完整构建。

副作用：需要安装 winetricks 组件的修正，第一次安装时 protonfixes 自己会把动词改成 `waitforexitandrun` 并执行 `wineserver -w`（winetricks 需要前缀空闲）。所以这类修正在加入会话的启动里，第一次会等到会话结束才继续；装好之后不再等。这是 protonfixes 原有的行为，没有改。

## 验证

1. `check_conditions()` 单独测试（不导入 protonfixes，避免导入时的副作用），9 种组合全部符合预期（变量改名后重跑过）：`waitforexitandrun` 执行；`run` 只有在 `PROTONFIXES_RUN_VERB=1` 时执行，`PROTONFIXES_RUN_VERB=0` 或同时设了 `PROTONFIXES_DISABLE` 时不执行；`runinprefix`、`getcompatpath` 不执行；只有一个参数时返回 False，不再抛异常；改名前的 `PROTONFIXES_FORCE=1` 不再生效。
2. 补丁按构建顺序（`find patches/protonfixes -name '*.patch' | sort`）用 `patch -Np1` 打在 DW 的补丁后面，干净应用，结果和 `we-patches` 分支一致。
3. `we/overlay-build.sh --release 26-0051b`：发布包的 `protonfixes/__init__.py` 带上了改动，`we-proton-build-info` 里有 `patch: protonfixes/0051-…`（`26-0051` 是改名前的同一补丁）。
4. 实机（2026-10-12，`~/.proton`，用测试 runner `26-0051` 启动加速器，再从 Steam 启动游戏）。这一轮做在变量改名之前，当时叫 `PROTONFIXES_FORCE`，判断逻辑和现在相同：
   - 不开网络命名空间：wrapper 输出 `This prefix is running with we-proton-11.0-14-26-0051; this program uses it too.`、`Session: verb run`；protonfixes 输出 `Changed "launcher/Launcher.exe" to "binaries/Darktide.exe"` 和追加参数；启动器没有运行，游戏窗口正常显示。
   - 开 pasta 网络命名空间（加速器建了 `utun233`）：同样执行了修正；窗口显示（`Window => active`）；`auth_platform = steam`，登录 Fatshark 后端成功（服务发现、时间同步、版本检查之后进入 `loading_packages`）；命名空间里对外的 IPv4 连接按路由走 `utun233`，隧道有收发流量。
5. 对照：同样的步骤用不带这个补丁的 release 26，几次都是 `Skipping fix execution`、启动器运行、游戏被隐藏启动、窗口不出来。

## 排查过程和弯路

最开始以为是网络命名空间的问题，先后查过：

- 游戏在命名空间里连不上 Steam 客户端（`SteamAPI_InitEx error: Cannot create IPC pipe`）：Steam 的 IPC 走 `127.0.0.1:57343`，命名空间有自己的回环，pasta 的 `-T auto` 不转发临时端口范围里的端口。这是真问题，在 wrapper 里给 pasta 加了 `-T 57343` 解决，和本补丁无关。
- 以为 Steam 按用户 ID 命名的共享内存（命名空间里用户是 0）也要改名，做过一个预加载库；后来证明不需要，已撤掉。
- 以为是 Steam 覆盖层、Fossilize 层卡住了窗口（线程栈上有它们）、窗口太大、显示相关的环境变量不对，逐一排除。
- 关掉网络命名空间后、只要先开着别的程序再启动游戏，问题照样出现，才确认和命名空间无关，转而对比「游戏先开」与「游戏后加入」两种情况，找到启动信息和 protonfixes。

## 未验证

- GE-Proton、DW-Proton 等不带这个补丁的 runner：加入会话的程序仍然不执行 protonfixes。
- 需要安装 winetricks 组件的修正在加入会话时的表现（见上面的副作用），没有拿实际游戏测过。
- 启动器在 Windows 上启动游戏时传的显示方式没有测。
