# 0003：原生 syscall 返回时写坏 rsp 下方的数据

补丁：`patches/wine/0003-ntdll-Return-from-native-syscalls-without-writing-be.patch`（`dlls/ntdll/unix/signal_x86_64.c`）。

## 现象

一个程序在运行到某个固定阶段时稳定崩溃，11 份崩溃转储签名完全相同：一条普通的 `.text` 指令 `xchg %al,-0x55(%rbp)` 写只读页触发访问违例。`rbp` 是栈帧指针，崩溃时却等于一条原生 `syscall` 指令（`NtDeviceIoControlFile`，rax=7）的下一条指令地址。

## 根因

Wine 的 syscall 分发器在快速返回路径上借用户栈恢复 eflags 并跳回：

```
movq 0x88(%rcx),%rsp ; movq 0x70(%rcx),%rcx ; pushq %r11 ; popfq ; pushq %rcx ; ret
```

Wine 自己的 syscall 桩是 `call` 进分发器的，`[rsp-8]` 本来就是那个返回地址，写了也没事。程序直接执行的 `syscall` 指令经 seccomp → SIGSYS 进入同一条返回路径，但它不是 call 进来的，这次写会覆盖调用方放在 rsp 下方的数据。Windows 的 sysret 从不写用户栈。

出问题的 syscall 桩恰好把调用方的 rbp 存在 `[rsp-8]`（syscall 时 `rbp = rsp-8`），syscall 之后再读回，结果 rbp 变成了返回地址。上层代码用这个 rbp 写局部变量，写到了只读段。两个相关段的 SEH handler 都返回 1（`ExceptionContinueSearch`），没人处理，进程崩溃退出。

## 修复

`sigsys_handler` 里让 `frame->restore_flags` 带上 `CONTEXT_CONTROL`，SIGSYS 进来的 syscall 一律走 iretq 路径：在内核栈上恢复 rip/rsp/eflags，不写用户栈，rcx=rip、r11=eflags 的语义不变。这条路径原本就用于单步时的 SIGSYS。

## 验证

- `we/tests/redzone_test`：在 `[rsp-8]`、`[rsp-16]` 放标记值，执行原生 syscall，检查标记有没有被改。原版 FAIL（`[rsp-8]` 变成 syscall 下一条指令的地址），修复后 PASS
- 实机：越过了原来崩溃的阶段，运行到主动退出，没有再生成转储

## 排查过程

1. 分析转储：确认崩溃点、rbp 的值、对应的 syscall 指令。
2. `diag/0001-diag-sigsys-rbp-logging+fix.patch`：在 SIGSYS 进入、返回路径、APC、SetContext、instrumentation callback 各处记录 rbp。日志显示没有注册 instrumentation callback，出事的 syscall 和访问违例之间也没有 usr1/SetContext/APC，排除了这几条特殊路径。
3. 反汇编分发器的快速返回路径，发现 `push` 写 `[rsp-8]`，写了上面的 redzone_test 证实。

## 弯路

- 假设「SIGSYS 之后回错了地址（`+0xb` 应为 `+2`）」：改了之后程序 3 秒就崩。用一个只有恢复点正确才会返回 `0x5a` 的程序验证，原版是对的：`sigsys_handler` 里的 `+0xb` 会被分发器的 `subq $0xb,0x70(%rcx)` 抵消。已回滚。
- 改出问题模块的页保护属性：方向错了，已回滚。
- 一度把那次访问违例当成程序故意触发、自己会处理的「写屏障」：把 SEH handler 的返回值 1 读成了「已处理」，其实是 `ExceptionContinueSearch`。
- 早期的寄存器测试只检查寄存器，没在 rsp 下方放数据，所以没发现问题。
- `rejected/ntdll-lookupfunctionentry-dynamic-table-fallback.patch`：给 `RtlLookupFunctionEntry` 加动态函数表回退，情况更糟，已撤回。
- 给驱动接口加「假成功」返回值的实验补丁：干扰变量，已撤回。
