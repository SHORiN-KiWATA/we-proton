# 0020：实现 NtUserGetWindowBand

补丁：`patches/wine/0020-win32u-Implement-NtUserGetWindowBand.patch`（`dlls/win32u/window.c`、`main.c`、`win32u.spec`，`dlls/wow64win/user.c`，`dlls/user32/user32.spec`，`include/ntuser.h`，测试 `dlls/user32/tests/win.c`）。

## 现象

《暗区突围：无限》进游戏两分多钟后闪退，两次都一样。游戏自带的崩溃上报组件留下了 minidump：

- 异常代码 `0x80000100`（`EXCEPTION_WINE_STUB`），标志 `EXCEPTION_NONCONTINUABLE`
- 异常地址 `win32u.dll+0x14494`，即 `NtUserGetWindowBand` 系统调用入口里 `syscall` 的下一条指令
- 两个参数指向 Unix 侧的字符串；按页内偏移在 `x86_64-unix/win32u.so` 里对上了 `"win32u"`（`0x183d89`）和 `"NtUserGetWindowBand"`（`0x184f84`），正是 `dlls/win32u/syscall.c` 里 `stub_syscall()` 抛出的那两个参数

`window_band_probe` 在 r22 上复现同样的结果（`window_band_probe.wine22.txt`）：

    wine: Call from 00006FFFFF444494 to unimplemented function win32u.NtUserGetWindowBand, aborting

## 根因

`win32u.spec` 里是 `@ stub -syscall NtUserGetWindowBand`：系统调用入口存在，Unix 侧对应的是 `stub_syscall()`，一调用就抛出不可继续的 `EXCEPTION_WINE_STUB`，进程被终止。user32 的 `GetWindowBand` 在 `user32.spec` 里是注释掉的，所以调用方是用 `GetProcAddress` 直接取了 win32u 的导出。

出错的线程栈很浅：`BaseThreadInitThunk` 之后就是一段不属于任何已加载模块的代码，栈上只有 `IsWindow`、`NtUserGetWindowBand`、`Sleep` 三个函数指针，是一个轮询窗口层级的循环。具体是游戏里哪个组件发起的，没有继续深究；函数名在游戏目录的文件里也搜不到（ASCII 和 UTF-16 都没有）。

## 修复

Windows 把顶层窗口放在不同的 z 序层级（band）里，普通程序创建的窗口都在桌面层级 `ZBID_DESKTOP`（1）。Wine 只有这一个层级，所以：

- 有效窗口：返回 `TRUE`，层级 1
- 无效句柄（包括 NULL）：返回 `FALSE`，`ERROR_INVALID_WINDOW_HANDLE`
- 输出指针为 NULL：返回 `FALSE`，`ERROR_NOACCESS`

错误码的顺序和取值照搬同形态的 `NtUserGetWindowDisplayAffinity`（上游测试在 Windows 上验证过：空句柄报 `ERROR_INVALID_WINDOW_HANDLE`，空指针报 `ERROR_NOACCESS`）。

另外：`main.c` 补上 PE 侧的系统调用入口（不再由 stub 列表生成），`wow64win` 加 32 位转接，`user32.spec` 导出 `GetWindowBand`，`dlls/user32/tests/win.c` 加 `test_window_band`。`overlay-build.sh` 的 TARGETS 加了 `dlls/user32`（两个架构）、`dlls/wow64win`，`dlls/win32u` 除了 `win32u.so` 也换 PE 的 `win32u.dll`（两个架构，原因见下面的弯路）。

## 验证

- `window_band_probe`（64 位）和 `window_band_probe32`（WoW64）在 r23 上（`window_band_probe.wine23.txt`）：所有有效窗口（隐藏/可见顶层、置顶、分层透明、子窗口、被拥有的弹出窗口、仅消息窗口、桌面、任务栏）都返回 `TRUE`、层级 1；NULL、无效句柄、已销毁的窗口返回 `FALSE`、错误 1400；输出指针为 NULL 返回 `FALSE`、错误 998。user32 的 `GetWindowBand` 和 win32u 的 `NtUserGetWindowBand` 结果相同，32 位和 64 位相同。
- 弯路：第一版只换了 `win32u.so`，64 位正常，32 位第一次调用后栈就乱了。官方包的 i386 `win32u.dll` 里这个入口还是按 stub 生成的，不知道参数个数，返回时不按 stdcall 弹出 8 字节参数。所以 PE 的 `win32u.dll`（两个架构）也要换，TARGETS 里 `dlls/win32u` 加上了它们。
- 游戏实测（2026-10-11，r23）：《暗区突围：无限》不再闪退，可以正常进局游玩（第一次运行了 13 分钟；之前两次都在两分半到五分钟内闪退）。
- 另外的问题，没有处理：墙面上有闪烁的点阵状黑块、间歇卡顿（游戏跑在 NVIDIA 独显上，经 vkd3d-proton 渲染；关掉 DLSS、帧生成和调低画质都没用）。第二次进游戏时，游戏提示检测到非法模块，原因没有查。
- Wine 自己的 `user32:win`（含新加的 `test_window_band`）还没跑。

## 未验证

- 这次没有在 Windows 上实测（虚拟机没用上）。子窗口、仅消息窗口、桌面窗口、别的进程的窗口（任务栏等）在 Windows 上返回什么层级，以及输出指针为 NULL 时 Windows 的错误码，都没确认；Wine 对所有有效窗口一律返回桌面层级。
- Wine 测试只断言了有把握的三种情况：空句柄、无效句柄、普通顶层窗口返回 1。
