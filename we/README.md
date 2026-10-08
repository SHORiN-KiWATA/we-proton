# WE-Proton

[DW-Proton](https://dawn.wine/dawn-winery/dwproton) 的个人 fork，攒一些 Wine（和 vkd3d-proton）的 bug 修复。自己和朋友用，不往上游提。

- 分支 `we-proton`，基于 tag `dwproton-11.0-14`
- 上游远程叫 `dwproton`
- Wine 的修复放在 `patches/wine/`，构建时由 `Makefile.in` 自动打到 wine 源码上（`.wine-post-source`）。`wine/` 子模块保持在上游 commit 不动
- vkd3d-proton 的修复放在 `patches/vkd3d-proton/`（路径相对 vkd3d-proton 根目录），`Makefile.in` 的 `.vkd3d-proton-post-source` 照 dxvk 的方式打上（这一段是 WE-Proton 加的）。`vkd3d-proton/` 子模块同样不动
- `wine/` 子模块和 `vkd3d-proton/subprojects/dxil-spirv` 里各有本地分支 `we-patches`，就是这些补丁对应的提交，rebase 时用

## 补丁

| 补丁 | 修什么 |
|---|---|
| `wine/0001-ntoskrnl-Implement-a-few-functions-needed-by-third-p` | 第三方 KMDF 驱动需要的 `ZwQueryValueKey`（容忍值名末尾的 NUL）、`IoGetBaseFileSystemDeviceObject`、`InbvAcquireDisplayOwnership`/`InbvResetDisplay`、`SeQueryInformationToken` |
| `wine/0002-server-Free-thread-input-attachments-when-either-que` | wineserver 在 `attach_thread_input` 里释放后使用 |
| `wine/0003-ntdll-Return-from-native-syscalls-without-writing-be` | 原生 `syscall` 指令返回时写坏 `[rsp-8]` |
| `wine/0004-server-Follow-Windows-connect-semantics-for-datagram` | UDP socket `connect()` 到全零地址的 Windows 语义 |
| `wine/0005-ntdll-Report-written-write-copy-pages-as-read-write` | 写过的写时复制页报告 `PAGE_READWRITE` |
| `wine/0006-win32u-Don-t-activate-WS_EX_NOACTIVATE-windows-in-pl` | 活动窗口消失后不把 `WS_EX_NOACTIVATE` 窗口设成前台 |
| `wine/0007-win32u-Draw-layered-child-windows-into-their-parent-` | 分层子窗口（`WS_CHILD` + `WS_EX_LAYERED`）画进父窗口，不再变成单独的顶层窗口 |
| `wine/0008-rpcrt4-Marshal-type-library-parameters-without-a-dir` | 类型库封送把没有 `[in]`/`[out]` 的指针参数按 `[in, out]` 处理，和 Windows 一致 |
| `vkd3d-proton/0001-Iterate-loop-break-rewrites-until-no-frozen-loop-is-` | dxil-spirv 生成非法的结构化控制流 |

每个补丁的现象、根因、Windows 实测、验证和排查过程见 `we/fixes/`。

## 测试

`we/tests/build.sh` 用 mingw 静态编译测试程序（也能直接拷到 Windows 上跑），用 runner 的 `files/bin/wine` 加一个空前缀运行：

- `redzone_test`（0003）：在 rsp 下方放标记值，执行原生 syscall，检查标记有没有被改
- `udp_connect_poll`、`udp_wakeup_probe`、`udp_connect_err_probe`、`udp_connect_err_probe2`（0004）：UDP `connect()` 的各种情况。`*.windows.txt` 是 Windows 11 26200 上的实测输出，`*.wine3.txt` 是修复后的输出
- `writecopy_probe`、`writecopy_probe2`（0005）：写时复制页写前写后报告的保护属性、代码页打补丁、区域边界
- `noactivate_probe`（0006）：普通窗口隐藏、最小化、销毁时谁接手前台，`WS_EX_NOACTIVATE` 窗口在同一进程和另一个进程两种情况；要在有显示的环境里跑（Xvfb 即可）
- `layered_child_probe`（0007）：用 `UpdateLayeredWindow` 更新的分层子窗口画在哪里、跟不跟父窗口走。用 `layered_child_grab.py` 在 Xvfb 上跑（`DISPLAY=:N layered_child_grab.py <runner>/files/bin/wine layered_child_probe.exe [pos]`），它从 X 根窗口读像素，需要没有合成器的 X 服务器
- `typelib_noflags_probe`（0008）：参数不带方向的类型库接口跨套间调用时，服务端看到什么、客户端拿回什么。`build.sh` 另编一个 32 位的 `typelib_noflags_probe32.exe`，两个都要跑
- `sta_thread_exit_probe`（0008 排查时的弯路）：客户端 STA 线程不调 `CoUninitialize` 就退出时，代理的引用什么时候释放；Windows 和 Wine 相同
- `quit_filter_probe`（0008 排查时的弯路）：`PostQuitMessage` 和 `PostThreadMessage(WM_QUIT)` 遇到各种 `GetMessage`/`PeekMessage` 过滤条件时返回什么；Windows 和 Wine 相同
- `dxil-spirv/run.sh`（vkd3d-proton 0001）：用 dxil-spirv 的 `structurize-test` 跑 `*.st` 控制流图，检查生成的 SPIR-V 能通过校验。源码取 `build/overlay/src-vkd3d-proton`（先跑 `overlay-build.sh`）；`--unpatched` 用未打补丁的子模块，应该失败

Wine 自己的测试：用 `build/overlay/src-wine` 另配一个 `--enable-tests` 的构建目录，编 `dlls/kernel32/tests`、`dlls/ntdll/tests`、`dlls/user32/tests`（0008 是 `dlls/oleaut32/tests`、`dlls/rpcrt4/tests`），用新旧 runner 各跑一遍、逐条比对失败项（0005 起都这样验证）。

## 诊断

诊断补丁在 `we/diag/`，不进正式构建，打在发布版上构建成 `build/diag-runner`：

- `0001-trace-skip-noisy-syscalls.patch`：`+syscall` 跟踪跳过 QPC、yield、查询系统时间
- `0002-server-log-afd-poll.patch`：wineserver 记录可能阻塞的 AFD poll 和数据报 connect
- `0003-ntdll-log-process-termination.patch`：记录每次 `NtTerminateProcess` 的调用方、退出码和调用栈

启动脚本都是 `<脚本> [--exe NAME.exe] -- <启动命令>`，日志进 `build/logs`：

- `we/run-logged.sh <启动命令>`：只开 Proton 日志
- `we/diag/run-exe-trace.sh`：一个进程的异常、调试输出、fixme/err、DLL 加载
- `we/diag/run-syscall-trace.sh`、`run-poll-trace.sh`、`run-term-trace.sh`：要用诊断 runner，启动命令里要用 `$WE_DIAG_PROTON` 指向的 proton 脚本
- `we/diag/run-shaderdump.sh`、`run-spirv-fail.sh`：vkd3d-proton 着色器转储、Mesa 拒绝的 SPIR-V 转储
- `we/diag/eventselect_rtt.c`：`WSAEventSelect` 循环的往返时间；`we/diag/pdb_publics.py`：用 PDB 公共符号把 RVA 解析成函数名

## 构建

快速构建（约 1–2 分钟，第二次之后只编改动的部分）：

```
we/overlay-build.sh --install
```

脚本做的事：

1. 下载官方 `dwproton-11.0-14` 发布包到 `~/.cache/we-proton/`，校验 sha512
2. 从 `wine/` 导出源码，打 `patches/wine/*.patch`，生成 configure 和 vulkan 头文件
3. 在 `Makefile.in` 指定的 Steam Linux Runtime 4.0 SDK 镜像里（docker）配置、编译 Wine，编译参数照抄 `Makefile.in`（`-march=nocona`、禁用 AVX、`-mcmodel=small` 等），unix 库只依赖运行时里有的库（在宿主机上编会链接宿主机的 `libunwind`，进不了运行时）。只编补丁动到的东西：`dlls/ntdll/unix` → `ntdll.so`，`server` → `wineserver`，`dlls/ntoskrnl.exe` → `ntoskrnl.exe`，`dlls/win32u` → `win32u.so`，`dlls/rpcrt4` → 64 位和 32 位的 `rpcrt4.dll`（所以配置了 i386 和 x86_64 两个 PE 架构）。补丁动到别的目录时脚本会报错，要先在 `TARGETS` 里加映射（`*/tests/*` 不发布，跳过）
4. `patches/vkd3d-proton/` 有补丁时：同步 `vkd3d-proton/` 源码到 `build/overlay/src-vkd3d-proton`，打补丁，用 meson 交叉编译 x86_64 和 i386 的 `d3d12.dll`、`d3d12core.dll`，编译参数照抄 `Makefile.in`（`-march=nocona`、禁用 AVX、`-O3`、静态 libstdc++）。需要 `git submodule update --init --recursive vkd3d-proton`；widl 用 overlay 里 wine 编出来的
5. 替换进发布包，改名为 `we-proton-11.0-14-<N>`，输出到 `build/`，再生成三个发布文件：`<名字>.tar.xz`（runner）、`<名字>.sha512sum`、`<名字>-source.tar.xz`（构建用的、打好补丁的 Wine 和 vkd3d-proton 源码，满足 LGPL 提供源码的要求）
6. `--install` 时复制到 `~/.local/share/proton/runners/WE-Proton`

`--release N` 改发布号，`--jobs N` 改并行数。

完整构建走上游的容器流程（`make redist`，几个小时），结果应该一样，没试过。

## 发布

tag 和 Release 名都用 runner 的名字（`we-proton-11.0-14-<N>`），tag 打在构建用的那个提交上（`we-proton-build-info` 里的 `we-proton:` 一行），三个文件原样上传：

```
gh release create we-proton-11.0-14-<N> --target <提交> --title we-proton-11.0-14-<N> --notes-file <说明> \
    build/we-proton-11.0-14-<N>.tar.xz build/we-proton-11.0-14-<N>.sha512sum build/we-proton-11.0-14-<N>-source.tar.xz
```

[wegame-launcher](https://github.com/SHORiN-KiWATA/wegame-launcher) 从最新的 Release 下载 `<tag>.tar.xz`，用 `<tag>.sha512sum` 校验，所以这两个文件名不能改。

## 用法

runner 名字是 `we-proton-11.0-14-<N>`，在启动器里手动选。`proton` 脚本里的 `CURRENT_PREFIX_VERSION` 没改，还是 `dwproton-11.0-14`，在 dwproton 和 WE-Proton 之间切换不会触发前缀升级。

## 跟进上游新版本

```
git fetch dwproton --tags
git rebase --onto dwproton-11.0-NN dwproton-11.0-14 we-proton
git submodule update wine
cd wine && git rebase --onto <新的 wine commit> b585905 we-patches   # 冲突在这里解决
git format-patch -o ../patches/wine <新的 wine commit>
```

vkd3d-proton 同理：`git submodule update --init --recursive vkd3d-proton` 之后，在 `vkd3d-proton/subprojects/dxil-spirv` 里把 `we-patches` rebase 到新的 dxil-spirv commit，再导出：

```
git format-patch --src-prefix=a/subprojects/dxil-spirv/ --dst-prefix=b/subprojects/dxil-spirv/ -o ../../../patches/vkd3d-proton <新的 dxil-spirv commit>
```

上游修了同一个问题的话，删掉对应的补丁即可（`patches/vkd3d-proton/` 空了 `overlay-build.sh` 就不编 vkd3d-proton）。

然后把 `overlay-build.sh` 里的 `BASE` 改成新 tag，重新构建，再跑一遍 `we/tests` 和 `we/tests/dxil-spirv/run.sh`。

## 注意

- 根目录的 `AGENTS.md` 是给 AI agent 的规定（问题报到哪里、反作弊和竞技游戏、补丁和报告的写法），动手前先看
- 不要以 DWProton 的名义发布，也不要往上游提 issue/PR
