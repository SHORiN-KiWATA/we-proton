# 修复记录

每个补丁一份报告：现象、根因、依据（能在 Windows 上实测的都实测过）、修复、验证、排查过程和弯路。

| 补丁 | 问题 | 报告 |
|---|---|---|
| wine 0001 | 第三方 KMDF 驱动加载失败：`ZwQueryValueKey` 不容忍值名末尾的 NUL，另有三个函数未实现 | `0001-ntoskrnl-driver-functions/` |
| wine 0002 | wineserver 在 `attach_thread_input` 里释放后使用，整个前缀一起崩溃 | `0002-wineserver-thread-input-uaf/` |
| wine 0003 | 原生 `syscall` 指令返回时写坏 `[rsp-8]` | `0003-native-syscall-red-zone/` |
| wine 0004 | UDP socket `connect(0.0.0.0)` 的语义和 Windows 不同，靠它唤醒的 `select()` 要等满超时 | `0004-udp-connect-unspecified/` |
| wine 0005 | 写过的写时复制页仍报告 `PAGE_WRITECOPY`，CEF 渲染进程的断言失败 | `0005-write-copy-tracking/` |
| wine 0006 | 活动窗口消失后，Wine 会把 `WS_EX_NOACTIVATE` 窗口设成前台 | `0006-noactivate-activation/` |
| wine 0007 | 分层子窗口被做成单独的 override-redirect 顶层窗口，位置按屏幕原点算、不跟父窗口走 | `0007-layered-child-windows/` |
| wine 0008 | 类型库封送把没有方向的参数只当输入，进程外 COM 服务器通过这类参数返回的结果全部丢失 | `0008-typelib-param-direction/` |
| wine 0009 | d3dcompiler 的反射读不出最低精度签名（ISG1/OSG1/PSG1），按反射结果拼输入布局的程序得到空布局，创建管线失败 | `0009-d3dcompiler-min-precision-signatures/` |
| vkd3d-proton 0001 | dxil-spirv 生成非法的结构化控制流，RADV 拒绝后崩溃 | `vkd3d-0001-dxil-spirv-loop-breaks/` |

## 已知、没修的问题

- **wineserver fsync 断言**（`fsync.c:271`，`fsync_free_shm_idx`）：进程在短时间内反复异常退出时触发，wineserver 退出，前缀里的进程卡住。只在一次崩溃循环里见过
- **N 卡渲染、核显显示时交帧卡住**（niri 不用 N 卡，屏幕接在核显上）：D3D11 程序在 N 卡上约 2 fps，渲染线程在等交帧。见 `0006-noactivate-activation/`
- **xwayland-satellite 不执行最小化**：会在失去前台时最小化自己的程序会进入「最小化 → 被恢复」循环
- **自绘边框、不让拖大小的窗口在平铺合成器下被平铺**：这类窗口带 `WS_THICKFRAME`，靠 `WM_NCHITTEST` 不返回边框来禁止拖大小，Wine 只能当成可调大小，niri 会把它平铺、程序按平铺尺寸重新排版。不是 Wine 能判断的，要用合成器的窗口规则。见 `0007-layered-child-windows/`
- **RADV 在 `spirv_to_nir` 失败时解引用 NULL**（Mesa 26.2.4，`radv_shader.c:544`）：本该只是一处渲染失败，结果整个进程崩溃。触发它的非法 SPIR-V 已经由 vkd3d-proton 0001 修掉

## 做法

- 只按精确 PID 结束进程（先核对 `/proc/<pid>/cmdline`），不用 `pkill -f` 之类的模式匹配
- 「Windows 到底怎么做」先在 Windows 上用探测程序实测，再写补丁（`we/tests/*.windows.txt`）
- 先用诊断日志看程序实际在做什么，不凭推测写补丁（0004 第一版就是这样写错的）
- 改 runner 或程序文件前先备份，并能一键还原；装二进制之后比对校验和
