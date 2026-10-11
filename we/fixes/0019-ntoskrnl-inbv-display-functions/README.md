# 0019：把 Inbv 的显示所有权函数实现回来

补丁：`patches/wine/0019-ntoskrnl-Implement-InbvAcquireDisplayOwnership-and-Inbv.patch`（`dlls/ntoskrnl.exe/ntoskrnl.c`、`ntoskrnl.exe.spec`）。

## 现象

0001 被撤回（0018）之后，《王者万象棋》点开始游戏就闪退，日志里是：

    wine: Call from ... to unimplemented function ntoskrnl.exe.InbvAcquireDisplayOwnership, aborting

## 根因

游戏加载的内核驱动通过 `MmGetSystemRoutineAddress` 解析 `InbvAcquireDisplayOwnership`、`InbvResetDisplay` 并调用；Wine 里这两个函数是 stub，一调用就把驱动所在的进程 abort 掉。0001 原先实现了它们（空操作），但 0001 被 0018 整体撤回后就没有了。

## 修复

0019 只把这两个函数加回来（空操作）。0001 里其余三个函数（`IoGetBaseFileSystemDeviceObject`、`ZwQueryValueKey` 的 NUL 容忍、`SeQueryInformationToken`）保持撤回——带上它们时会有程序报“运行环境异常”（见 0018 的提交说明和 0001 的报告）。

## 验证

- `we/tests/inbv_probe.c`：加载 `ntoskrnl.exe` 并调用这两个函数
  - 撤回状态（r19）：`unimplemented function ntoskrnl.exe.InbvAcquireDisplayOwnership, aborting`（`inbv_probe.wine19.txt`）
  - 加上 0019（r20）：两个函数都 `called ok`（`inbv_probe.wine20.txt`）
- 《王者万象棋》不再闪退；《鸣潮》也正常（2026-10-09，r20）。

## 未验证

- 0001 里那三个函数具体是哪一个/哪几个导致“运行环境异常”，没有继续细分（0018 只验证了“全部撤回则不报”）。
