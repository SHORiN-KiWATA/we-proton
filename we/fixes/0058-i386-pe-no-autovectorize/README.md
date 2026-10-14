# 0058：加壳器搬移 API 开头时解不了 SSE2 指令；DXVK d3d9 采样器状态越界

两个补丁，同一个程序（封印者 TW 2018-01-02 客户端 `CW.EXE`，32 位、加壳）先后撞上：

- `patches/wine/0058-configure-Disable-auto-vectorization-for-i386-PE-code.patch`（`configure.ac`）
- `patches/dxvk/d3d9-ignore-out-of-range-sampler-state-types.patch`（`src/d3d9/d3d9_device.cpp`，登记在 `patches/protonprep-valve-staging.sh`）

## 现象

1. WE-Proton（GE 底座）下客户端启动约 0.5 秒就崩：`Unhandled page fault on read access to FFFFFFFF at address 097Cxxxx`，地址落在一页动态分配的 rwx 内存里，每次运行地址都不同；不开调试通道时进程直接以 `0xC0000005` 退出、不打印。同一前缀、同一客户端，Proton 10.0 能过。负载高、低，Xvfb、gamescope 无头、真桌面 `:0`，全新前缀、旧前缀，结果都一样（WE-Proton 0/15）。
2. 解决 1 之后，过了壳、建好 D3D9 设备，约 6 秒后进程以退出码 3（`abort()`）退出，还没走到客户端的下一阶段。

## 根因

### 1. 加壳器的 API 代码搬移（wine 0058）

这个壳会把被保护程序导入的 API 开头若干条指令复制到自己分配的页里，插入花指令后执行，最后跳回原函数中段（导入表重定向桩）。它的指令长度表不认识 SSE2 独有的操作码：`66 0F 6C`（`punpcklqdq`）在 MMX 里不存在，被当成没有 ModRM 的 3 字节指令，后面的字节整体错位。

崩溃那次搬的是 `user32.SetRect`。Wine 的 i386 PE 代码用 `-O2 -march=nocona` 编译，GCC 12 起 -O2 默认开 SLP 向量化，`SetRect` 的四次存储被编成：

```
movd xmm0,[ebp+18h] / movd xmm1,[ebp+14h] / movd xmm2,[ebp+10h] / mov eax,[ebp+8]
punpckldq xmm1,xmm0 / movd xmm0,[ebp+0Ch] / punpckldq xmm0,xmm2
punpcklqdq xmm0,xmm1        ; 66 0F 6C C1
test eax,eax / je ... / movups [eax],xmm0 ...
```

桩里复制出来的却是 `punpcklqdq xmm2,[eax+52h]`（`66 0F 6C` + 花指令字节 `50 52`），随后的 `c1 85 c0 74 16 0f 11` 被拼成 `rol dword ptr [ebp+0F1674C0h],11h`。`[eax+52h]` 不是 16 字节对齐 → #GP，Wine 照 Windows 惯例报成读 `FFFFFFFF` 的访问违例；SEH 链上只有游戏自己的 handler，返回 `ExceptionContinueSearch`，进程崩溃。

真 Windows 的系统 DLL 是 MSVC 编译的，函数开头不会出现这种向量化代码。扫描（`pe-sse-prologue-scan.py`）统计各 runner 的 i386 系统 DLL 导出函数开头（到第一个 ret/jmp，最多 96 字节）含 SSE2 独有操作码的个数：

| runner | user32 | gdi32 | kernel32 | kernelbase | ntdll | 合计（20 个常用 DLL） |
|---|---|---|---|---|---|---|
| Proton 10.0 | 0 | 0 | 0 | 0 | 0 | 16（msvcrt/oleaut32 的 double 数学函数） |
| GE-Proton11-7 / Experimental / WE ge11-7-29 | 25 | 3 | 13 | 10 | 19–20 | 125–127 |
| 关自动向量化后 | 0 | 0 | 0 | 0 | 0 | 20（同样只剩 double 数学函数） |

剩下的是 `-mfpmath=sse` 搬 double 用的 `movq`（`66 0F D6`），Proton 10.0 也有，属于正常代码生成。

### 2. DXVK d3d9 采样器状态越界（dxvk 补丁）

游戏启动时对采样器 0–3 读取状态类型 1..14，最后一个比 `D3DSAMP_DMAPOFFSET`（13）大 1。DXVK 的 `GetSamplerState`/`SetSamplerState` 直接用 `Type` 下标访问 `std::array<DWORD, 14>`，不校验。WE-Proton 的 DXVK 用 meson `buildtype=plain`、`b_ndebug=false` 构建，meson 1.4 起这会带上 `-D_GLIBCXX_ASSERTIONS=1`，越界访问触发 libstdc++ 断言：

```
.../c++/array:208: ... std::array<_Tp, _Nm>::operator[](size_type) [with _Tp = long unsigned int;
unsigned int _Nm = 14; ...]: Assertion '__n < this->size()' failed.
```

随后 `abort()` → 退出码 3。Proton 10.0 的 DXVK 不带断言，越界读到的是下一个采样器的第 0 项（未使用），所以没事。上游 DXVK（master）同样没有校验。Wine 的 wined3d 也不校验类型。

## 修复

- wine 0058：`configure.ac` 里 i386 PE 的编译参数（和 `-fno-omit-frame-pointer`、`-mstack-alignment=4` 同一处，都是为了让代码生成更像 Windows）加 `-fno-tree-vectorize -fno-tree-slp-vectorize`（GCC/clang 都认）。只影响 32 位 PE 模块；x86_64 不动（x64 MSVC 代码本来就满是 SSE，保护壳必须能处理）。
- dxvk：`GetSamplerState`/`SetSamplerState` 入口遇到 `Type >= D3DSAMP_DMAPOFFSET + 1` 时，Get 返回 0、Set 忽略，都返回 `D3D_OK`。Windows 上的确切返回值没有实测（需要能建 D3D9 设备的桌面会话）；这个行为和 Proton 10.0 实际表现等价，且不再越界。

## 验证

测试 runner `build/test-novec`：`test-heap` 的副本，替换 11 个按 0058 参数重编的 i386 DLL（user32 gdi32 kernel32 kernelbase msvcrt ntdll ole32 oleaut32 shell32 shlwapi ws2_32）和打了 dxvk 补丁的 i386 `d3d9.dll`。gamescope 无头后端、负载 8–20：

| 构建 | 结果 |
|---|---|
| test-heap（修复前） | 0/15，全部在搬移桩里崩 |
| 只换 user32 | 越过 `SetRect`，又在另一个搬移桩里崩（`Unhandled privileged instruction`） |
| 换 user32 + kernelbase | 搬移桩里崩（kernel32 的 `lstrcpyA` 等带 `movq [esp+..],xmm0`） |
| 换全部 11 个 | 过壳，6 秒后 DXVK 断言 abort |
| 换全部 11 个 + dxvk 补丁 | **3/3** 走到下一阶段的错误弹窗（约 30 秒），与 Proton 10.0 一致 |

**没做的**：完整的 `we/build.sh`（configure 要在容器里重新生成，quick-build 不管）。上面的 DLL 是在 `build/obj/obj-wine-i386` 里用 `i386_CFLAGS=... -fno-tree-vectorize -fno-tree-slp-vectorize` 覆盖重编的，与补丁生效后的参数相同。注意：这 11 个模块的 `.o` 现在就是关向量化编的，下次 quick-build 不会重编它们。

## 排查过程

1. 交接时以为卡点是后面的 45A 错误；照交接里的命令加 relay 过滤（注意：`RelayInclude` 是**分号**分隔，交接里写的空格是错的，见 `dlls/ntdll/relay.c`），结果每次都在 0.5 秒内崩，根本到不了后面的阶段。
2. 排除显示环境（Xvfb / gamescope 无头 / `:0`）、前缀状态（全新前缀）、调试通道（`-all` 时也是 `exit_group(0xC0000005)`，strace 看到的）。
3. 冻结崩溃进程：把 AeDebug 调试器换成 `cmd /c ping -n 600 127.0.0.1`，崩溃后进程停在等调试器。winedbg 附加不上（JIT 附加 error 5，主动附加后进程直接退出）；`/proc/pid/mem` 受 ptrace_scope=1 限制，改用一个**祖先进程**（`memgrab-on-crash.py`，拉起 gamescope 再拉起客户端）读内存，拿到崩溃页。
4. 按控制流（跟随 jmp/call）反汇编崩溃页，看到其中引用 `user32` 内部地址和 IAT 槽位，确认是搬移桩；对照 Wine `SetRect` 的机器码找到错位点。
5. Proton 10.0 无此问题 → 扫描各 runner 的 DLL，确认是编译器向量化的差别。按模块二分出需要换的范围。
6. 第二个问题：relay `msvcrt.*` 且 `RelayFromInclude=lua5.1.5`（原以为是 Lua 抛错），看到 abort 前在用 `fputc` 往 stderr 写断言消息，调用方在 DXVK 的 d3d9.dll；诊断版 d3d9 打印出越界的 `Type`。

## 弯路

- 以为是负载/计时反调试（壳里遍布 `rdtsc`）：负载 8 和 20 结果一样，Proton 10.0 同负载能过。
- 以为是 `GetCursorPos`（崩溃时 ecx 指向 `NtUserGetCursorPos` 桩的返回点）：只是上一次系统调用留下的值。
- 以为 abort 是 Lua panic（之前有 `longjmp`），其实是 DXVK 的断言。
- 以为关 kernel32 的向量化本身引起 abort（按模块二分时 kernel32 一换就 abort）：其实是 kernel32 修好后才走到了 DXVK 那一步。
