# 修复记录

每个补丁一份报告：现象、根因、依据（能在 Windows 上实测的都实测过）、修复、验证、排查过程和弯路。

| 补丁 | 问题 | 报告 |
|---|---|---|
| wine 0001 | 第三方 KMDF 驱动加载失败：`ZwQueryValueKey` 不容忍值名末尾的 NUL，另有三个函数未实现 | `0001-ntoskrnl-driver-functions/` |
| wine 0002 | wineserver 在 `attach_thread_input` 里释放后使用，整个前缀一起崩溃 | `0002-wineserver-thread-input-uaf/` |
| wine 0003 | 原生 `syscall` 指令返回时写坏 `[rsp-8]` | `0003-native-syscall-red-zone/` |
| wine 0004 | UDP socket `connect(0.0.0.0)` 的语义和 Windows 不同，靠它唤醒的 `select()` 要等满超时 | `0004-udp-connect-unspecified/` |
| wine 0005 | 写过的写时复制页仍报告 `PAGE_WRITECOPY`，CEF 渲染进程的断言失败 | `0005-write-copy-tracking/` |
| vkd3d-proton 0001 | dxil-spirv 生成非法的结构化控制流，RADV 拒绝后崩溃 | `vkd3d-0001-dxil-spirv-loop-breaks/` |

## 已知、没修的问题

- **wineserver fsync 断言**（`fsync.c:271`，`fsync_free_shm_idx`）：进程在短时间内反复异常退出时触发，wineserver 退出，前缀里的进程卡住。只在一次崩溃循环里见过
- **RADV 在 `spirv_to_nir` 失败时解引用 NULL**（Mesa 26.2.4，`radv_shader.c:544`）：本该只是一处渲染失败，结果整个进程崩溃。触发它的非法 SPIR-V 已经由 vkd3d-proton 0001 修掉

## 做法

- 只按精确 PID 结束进程（先核对 `/proc/<pid>/cmdline`），不用 `pkill -f` 之类的模式匹配
- 「Windows 到底怎么做」先在 Windows 上用探测程序实测，再写补丁（`we/tests/*.windows.txt`）
- 先用诊断日志看程序实际在做什么，不凭推测写补丁（0004 第一版就是这样写错的）
- 改 runner 或程序文件前先备份，并能一键还原；装二进制之后比对校验和
