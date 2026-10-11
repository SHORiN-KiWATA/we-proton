# vkd3d-proton 0001：dxil-spirv 生成非法的结构化控制流

补丁：`patches/vkd3d-proton/0001-Iterate-loop-break-rewrites-until-no-frozen-loop-is-.patch`（`subprojects/dxil-spirv/cfg_structurizer.cpp`，约 20 行）。不在 Wine 里：vkd3d-proton 把 D3D12 着色器（DXIL）经 dxil-spirv 翻译成 SPIR-V，再交给 Vulkan 驱动。

## 现象

一个 D3D12 程序加载到某个阶段时退出（退出码 3，程序自己调用 `abort()`）。退出前一刻 Mesa 的 RADV 驱动（`libvulkan_radeon.so`，Mesa 26.2.4）段错误：

```
radv_shader_spirv_to_nir   (src/amd/vulkan/radv_shader.c:544)
```

## 根因

1. dxil-spirv 翻译一个像素着色器时，生成了不合法的结构化控制流。`spirv-val`：`block '%8336' exits the loop headed by '%8329', but not via a structured exit`
2. Mesa 的 `spirv_to_nir` 正确地拒绝了它，返回 NULL；`radv_shader_spirv_to_nir` 没检查就用了，段错误（Mesa 的另一个 bug，这里没修）
3. 渲染失败，程序 `abort()`

dxil-spirv 的结构化分两轮。这个着色器的控制流是「三条不同的分支跳到同一个节点」：

- 第 0 轮把两个选择结构升级成「冻结循环」（frozen loop），借循环的 break 跳出去，但内层冻结循环的正常出口没经过它的 merge 块
- 最后一轮（pass 1）之后，`rewrite_invalid_loop_breaks()` 发现越界的出口，把那些块挪到循环 merge 之后，用一串分派块（dispatcher）链接起来，然后再跑一次 `structurize(1)`
- 这一次 `fixup_broken_selection_merges()` 的 tie-break 又把新建的一个分派块升级成了冻结循环（还因为前面 `find_selection_merges` 里「Mismatch headers in pass 1」改写了它的 selection merge，用错了 merge 块），而这个新循环的出口没有人再检查

## 修复

让「`rewrite_invalid_loop_breaks()` + `structurize(1)`」循环到不再需要改写为止（上限 16 轮，不收敛时打警告）。只修了最后一步漏检；第 0 轮的 merge 选择和 pass 1 的「Mismatch headers」改写没有动，那部分启发式太深，改动风险大。

## 验证

- `we/tests/dxil-spirv/run.sh`：用 dxil-spirv 的 `structurize-test` 跑 `loop-break-dispatch.st`（缩减到 15 行的控制流图，只有形状）。修复后通过校验，`--unpatched` 用未打补丁的源码，不通过
- 出问题的那一轮转储下来的 368 个着色器，补丁前后用 dxil-spirv 命令行逐个对比：只有出问题的那个输出变了，而且变成合法的了，其余字节完全相同
- dxil-spirv 自带测试集（用 DXC 1.9.2609 编译）：vkd3d-proton 这一版带的 dxil-spirv（`c5e5522a`）863 个、当时的上游 master（`b6e310b6`）867 个，补丁前后输出全部相同
- 实机：程序越过原来退出的阶段，Mesa 没有再拒绝任何着色器

## 排查过程

1. `we/diag/0003-ntdll-log-process-termination.patch`（`we/diag/run-term-trace.sh`）记录所有 `NtTerminateProcess`：程序是自己 `abort()` 退出的，没有被别的进程结束。
2. vkd3d-proton 和 Wine 的异常日志指向 RADV，用 Arch 的调试符号解析出 `radv_shader.c:544`。
3. `MESA_SPIRV_FAIL_DUMP_PATH`（`we/diag/run-spirv-fail.sh`）转储被拒绝的 SPIR-V，`spirv-val` 判定非法。
4. `VKD3D_SHADER_DUMP_PATH`（`we/diag/run-shaderdump.sh`）转储全部着色器：368 个里只有这一个非法。用 vkd3d-proton 当时带的 dxil-spirv 和上游 master 分别重新翻译，都非法，说明上游还没修。
5. 编一个带 `-DDXIL_SPV_MISC_CLI=ON` 的 dxil-spirv，用 `DXIL_SPIRV_STRUCTURIZE_TEST_PATH` 导出控制流图（458 行），`misc/reduce-cfg.py` 缩减到 15 行。
6. `DXIL_SPIRV_GRAPHVIZ_PATH` 导出各阶段的图，再在所有把节点升级成循环的地方加日志，定位到上面根因里的那一步。

## 构建上的差异

- 构建出的 `d3d12.dll`、`d3d12core.dll` 和官方的导出表相同，但导入 `api-ms-win-crt-*`（UCRT）而不是 `msvcrt.dll`：Arch 的 mingw-w64 默认 UCRT，它的 libstdc++ 头文件不能配 `-mcrtdll=msvcrt-os` 编译。Wine 两套都有
- vkd3d-proton 用构建编号（`vkd3d_build`）当着色器缓存的兼容键。打了补丁的构建按「脏树」处理（`<hash>0`，版本号带 `+`），不会读到官方构建存下的缓存
