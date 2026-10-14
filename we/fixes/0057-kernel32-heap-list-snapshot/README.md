# 0057：kernel32 的 toolhelp 堆快照（TH32CS_SNAPHEAPLIST）

补丁：`patches/wine/0057-kernel32-Implement-heap-list-snapshots.patch`
（`dlls/kernel32/toolhelp.c`、`dlls/kernel32/kernel32.spec`）

## 现象

加壳的商业客户端（封印者 TW 2018，壳类似 WinLicense/Themida 一类）在 Wine 下启动到
某个固定阶段后**静默退出**（无窗口、无错误对话框、无游戏日志）。

`WINEDEBUG=err+all,warn+all,fixme+all` 下能看到它确实调了：

```
018c:fixme:toolhelp:CreateToolhelp32Snapshot Unimplemented: heap list snapshot
```

## 根因

Wine 的 toolhelp 只实现了 `SNAPPROCESS`/`SNAPTHREAD`/`SNAPMODULE` 三种快照：

- `CreateToolhelp32Snapshot()` 顶部直接拒绝不含这三种 flag 的调用；
- 即使和别的 flag 一起传了 `TH32CS_SNAPHEAPLIST`，也只打一条 `FIXME`，不往快照里写任何堆数据；
- `Heap32ListFirst()` 是 `FIXME(": stub")`，`Heap32ListNext()` 在 `kernel32.spec` 里是 `@ stub`。

壳用「枚举自己的堆」做反调试/完整性检查（Windows 上这是有真实数据的），
拿到空结果/`ERROR_CALL_NOT_IMPLEMENTED` 后就判定环境不对并退出。

## 修复

用 ntdll 已有的 `RtlGetProcessHeaps()` 实现真正的堆列表：

1. `struct snapshot` 增加 `heap_count/heap_pos/heap_offset`（与 thread/module 同构）。
2. `fetch_heap()`：只对**当前进程**枚举（`RtlGetProcessHeaps` 是进程内的堆列表；
   枚举其它进程的堆要读对方进程内部结构，暂不支持，返回 0 个堆）。
   第一个是进程默认堆 → `HF32_DEFAULT`，其余 `dwFlags = 0`。
   注：`HEAPLIST32.dwSize` 是 `SIZE_T`，`WARN` 里要用 `%Iu`（用 `%ld` 过不了 `-Werror`）。
3. `CreateToolhelp32Snapshot()`：放开 flag 检查、按堆数量算段大小、填快照、释放临时数组。
4. `Heap32ListFirst()`/`Heap32ListNext()`：按 thread/module 的范式遍历（`next_heap()`），
   `dwSize` 不够报 `ERROR_INSUFFICIENT_BUFFER`，越界报 `ERROR_NO_MORE_FILES`；
   `kernel32.spec` 里 `Heap32ListNext` 从 `@ stub` 改成 `@ stdcall Heap32ListNext(long ptr)`。

## 验证

- 构建：`we/quick-build.sh --base build/test-privdiag --name test-heap dlls/kernel32`
  （在本机的 `build/we-proton-ge11-7-29` 基础上）。
- 客户端侧：`fixme:toolhelp:*` 不再出现（用 `WINEDEBUG=fixme+toolhelp` 确认）；
  **客户端确实会调用这个 API**（这是本补丁的直接依据）。
- ⚠️ 更正：我最初写「客户端因此能继续往下走到 XIGNCODE 阶段」——**这条没有严格验证**
  （当时的运行后来被证明卡在别处，且 XIGNCODE 45A 至今仍会出现）。本补丁的依据只是
  「Windows 实现了这三个 API、Wine 没有、而该客户端会调用它们」。
- 未做：`Heap32First`/`Heap32Next`（枚举堆内块）仍是 `@ stub`；
  客户端目前没调用，先留着。

## 排查过程

1. 用诊断补丁 `diag/0001-diag-priv-instr-logging.patch` 先排除了「特权指令异常」这条线：
   壳大量执行 `fb`（`sti`）陷阱，Wine 抛 `EXCEPTION_PRIV_INSTRUCTION` 是**对的**——
   `WINEDEBUG='+seh'` 能看到壳自己的 SEH 处理器接手（`handler returned 1`）。
   实验性地让 Wine 跳过这些陷阱，客户端反而卡死（不再出窗口）→ 壳就是要这些异常。
2. 改用 `WINEDEBUG=err+all,warn+all,fixme+all` 复跑，`fixme:` 频道统计里 toolhelp 的
   唯一一条就是堆快照；客户端传的是 `TH32CS_SNAPHEAPLIST`（和其它 flag 组合）。
3. 在 Wine 源码里确认三种相关 API 都还没实现，且 `RtlGetProcessHeaps` 已可用 → 补上。

## 弯路

- 一开始把 `0xFB` 当成「WoW64 的系统调用标记被 Wine 误判」，为此写了一版
  「32 位代码里跳过 `sti`/`in`/`out` 等陷阱指令」的补丁：结果是客户端不再抛异常、
  但也**不再前进**（静默卡住）。已回滚，只留诊断。
- 把「xigncode 缺驱动」直接理解成 `NtLoadDriver`：实测客户端**根本没调** `NtLoadDriver`
  （Wine 里它还是纯 stub），所以这条不是本次失败的直接原因。
