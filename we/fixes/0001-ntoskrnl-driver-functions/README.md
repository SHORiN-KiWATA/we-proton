# 0001：第三方 KMDF 驱动在 Wine 的 ntoskrnl 下加载失败

**2026-10-09 已撤回。** 《鸣潮》在 WE-Proton 下报运行环境异常，排查结果：

- 官方 `dwproton-11.0-14`：不报
- 官方包只加这个补丁（只有 `ntoskrnl.exe` 不同，在 SDK 里用同一套源码编译）：报，问题复现

据此撤回：`patches/wine/0018-Revert-ntoskrnl-Implement-a-few-functions-needed-by-.patch` 把这个补丁的改动反过来，补丁文件本身保留。之后 0019 又只把其中两个空操作函数（`InbvAcquireDisplayOwnership`、`InbvResetDisplay`）加了回来，见 `0019-ntoskrnl-inbv-display-functions/`。

没有验证的：

- 「release 17 去掉这个补丁」没有单独测过
- 这个补丁当初要修的那个驱动，撤回后是否又加载失败

下面是当初的记录。

补丁：`patches/wine/0001-ntoskrnl-Implement-a-few-functions-needed-by-third-p.patch`（`dlls/ntoskrnl.exe/ntoskrnl.c`、`ntoskrnl.exe.spec`，约 100 行）。2026-10-04 修复。

## 现象

一个第三方 KMDF 内核驱动（导入 `WDFLDR.SYS`，绑定 `KmdfLibrary 1.9`，函数表 396 项）在 Wine 里 `ZwLoadDriver` 失败，返回 `STATUS_OBJECT_NAME_NOT_FOUND`（`c0000034`）。依赖这个驱动的程序随即报错退出。

## 根因

一共四处缺口，修好一处才暴露下一处：

1. **`ZwQueryValueKey` 不容忍值名末尾的 NUL**：驱动读自己服务键下的 `ImagePath`、`DisplayName` 时，传入的 `UNICODE_STRING.Length` 把末尾的 NUL 也算了进去（分别是 10 和 13 个字符，即 9+1、11+2）。Wine 的注册表按计数长度精确匹配，于是找不到值；驱动重试 3 次（每次 10ms）后判定安装无效，`DriverEntry` 返回 `c0000034`。
2. **`IoGetBaseFileSystemDeviceObject` 未实现**：驱动直接导入它，调用即 abort。
3. **`InbvAcquireDisplayOwnership`、`InbvResetDisplay` 未实现**：驱动通过 `MmGetSystemRoutineAddress` 动态解析这两个名字，按顺序调用。
4. **`SeQueryInformationToken` 未实现**：驱动加载完成、运行一两分钟后调用（`TokenStatistics`），abort 掉驱动的宿主进程。

## 修复

| 函数 | 实现 |
|---|---|
| `ZwQueryValueKey` | 在 ntoskrnl 里本地实现（原来是转发给 ntdll）：去掉值名末尾的 NUL 再查询。只影响内核驱动，用户态的 `NtQueryValueKey` 不变 |
| `IoGetBaseFileSystemDeviceObject` | Windows 的回退顺序：`FileObject->Vpb->DeviceObject` → `FileObject->DeviceObject->Vpb->DeviceObject` → `FileObject->DeviceObject` |
| `InbvAcquireDisplayOwnership`、`InbvResetDisplay` | 空操作。其余 10 个 `Inbv*` 保持 stub：驱动既不导入，也没有动态解析过它们 |
| `SeQueryInformationToken` | `ObOpenObjectByPointer` 打开令牌，`NtQueryInformationToken` 查询，结果用 `ExAllocatePool` 分配（调用方用 `ExFreePool` 释放） |

补丁是按「被调用过的才保留」做过减法的最小集合：完整日志里调用次数为 0 的改动（`IoEnumerateDeviceObjectList`、`ExRaiseStatus`、`MmIsAddressValid` 的分支、其余 `Inbv*`）都删掉了，删完重跑结果不变。

## 验证

驱动完整加载并持续运行，依赖它的程序正常工作。补丁前后各函数的调用次数和失败点见上面的根因。

## 排查过程

1. `+ntoskrnl` 日志：WDF 绑定成功，但驱动在自己的初始化阶段重试三次后 `WdfVersionUnbind` 退出，所以问题在 `DriverEntry` 里，不在绑定。
2. 编了一个全插桩的 `wdf01000.sys`（`diag/diagnostics-wdf01000-instrumentation.patch`，函数表 1024 项全换成带日志的桩）：驱动失败前一次都没调用函数表。排除「缺 WDF API」。
3. gdb 抓到的唯一一次取指异常，地址是 Wine 故意给 `DriverEntry` 返回地址加上的内核地址前缀（`call_driver_init` 用它模仿内核地址），属于预期行为，不是崩溃原因。
4. 关键一步：Wine 的 `Nt*` 调用按源文件分在 `reg`、`file`、`sync` 等调试通道，只开 `+ntdll` 时注册表访问根本不出现在日志里。改用 `+server`（所有命名空间操作都要经过 wineserver 请求层），才看到 `get_key_value(name=L"ImagePath\0")` 返回 `OBJECT_NAME_NOT_FOUND`。
5. 反汇编驱动里读注册表字符串的辅助函数：`Length = 字符数 × 2`，字符数由调用方传入且包含 NUL。调用方在驱动的混淆代码段里，没有继续追。
6. 修好第 1 处之后，依次撞上第 2、3、4 处的 `unimplemented function ... aborting`。

## 弯路

- 最早在 ntdll 的注册表代码里容忍末尾 NUL：影响所有 Wine 进程。驱动从 ntoskrnl 导入 `ZwQueryValueKey`，所以收窄到了 ntoskrnl。
- 曾在 `signal_x86_64.c` 加内核别名地址的翻译：和 Wine 已有的 vectored handler 重复，去掉后一样能跑通。
- 曾按 Windows 语义完整实现 `IoEnumerateDeviceObjectList`、`ExRaiseStatus` 等：从没被调用过，删掉。

## 未解决的问题

为什么同样的驱动在 Windows 上能读到值，没有在 Windows 上实测。可能是安装程序在 Windows 上写入的就是带 NUL 后缀的值名（原生 API 允许，注册表编辑器看不出来），驱动再按同样的名字读回。
