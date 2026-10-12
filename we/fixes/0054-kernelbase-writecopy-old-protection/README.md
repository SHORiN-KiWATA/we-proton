# 0054：没写过的写时复制页，VirtualProtect 报的旧属性和 Windows 不同

补丁：`patches/wine/0054-kernelbase-Don-t-report-unwritten-write-copy-pages-a.patch`（`dlls/kernelbase/memory.c`、`dlls/ntdll/unix/virtual.c`）。依赖 0005（用到它的 `VPROT_COPY_TRACKED`），要排在 0005 之后。

## 现象

从 `we-proton-ge11-7-25` 起（换成 GE-Proton 底座），`we/tests/writecopy_probe` 比 Windows 多出一处不同：镜像数据段里**没写过**的页，`VirtualProtect(PAGE_READONLY)` 报的旧属性是 `PAGE_READWRITE`（`0x04`），Windows 是 `PAGE_WRITECOPY`（`0x08`）。同一页 `VirtualQuery` 报的是 `0x08`，两个 API 互相矛盾。

```
                       query       VirtualProtect(RO) 旧属性
.data not written      0x08        0x04   ← ge11-7-25/26
.data not written      0x08        0x08   ← Windows
```

（`tests/writecopy_probe.ge25.txt`、`tests/regress-we-proton-ge11-7-26/writecopy_probe.txt`、`tests/writecopy_probe.windows.txt`）

还没发现因此出问题的程序，是回归探测查出来的。

## 根因

GE-Proton 打 wine-staging 时带上了 `ntdll-WRITECOPY` 补丁集。这个补丁集现在只剩两个补丁，都在 kernelbase：

- `0010-kernelbase-Correct-return-value-in-VirtualProtect-fo.patch`：`VirtualProtect()` 调完 `VirtualProtectEx()` 后，旧属性是 `PAGE_WRITECOPY` 就一律改成 `PAGE_READWRITE`
- `0011`：给上面那行加 `old_prot` 的空指针检查（原版对传 NULL 的程序会崩）

打完以后的 `wine/dlls/kernelbase/memory.c`：

```c
BOOL ret = VirtualProtectEx( GetCurrentProcess(), addr, size, new_prot, old_prot );
if (old_prot && *old_prot == PAGE_WRITECOPY) *old_prot = PAGE_READWRITE;
return ret;
```

### staging 为什么这么做

和 0005 是同一个问题：Chromium 把写过的全局变量改成只读，并 `CHECK` 旧属性是 `PAGE_READWRITE`；Wine 不知道哪些写时复制页写过，一律报 `PAGE_WRITECOPY`，基于 Chromium/CEF 的程序就崩。staging 的历史（`wine-staging` 子模块的 git log）：

| 时间 | 提交 | 内容 |
|---|---|---|
| 2024-04 | `9d59c4b2` | 加入 0010，说明是让一个基于 CEF 的客户端重新能用 |
| 2024-08 | `c901884a` | Elizabeth Figura 删掉 0010：这是给上游准备的 hack，只对 CEF 有用，而且会让补丁集其余部分修好的测试重新失败 |
| 2024-09 | `e68e4dbb` | 又加回来，说明是那个客户端需要 |
| 2024-09 | `16dce521` | 删掉补丁集里真正跟踪写时复制的 8 个补丁（信号处理 + 只读映射 + 缺页时标记已复制），只留下 0010/0011，说明里说留着它们是为了让基于 Chrome 的程序能跑 |

也就是说，0010 是在 Wine 分不清页写没写过的前提下，按「改保护属性的页多半写过」猜一个结果：写过的页猜对，没写过的页猜错。

### 和 0005 叠加后的效果

0005 在 ntdll 里用 userfaultfd 异步写保护 + `PAGEMAP_SCAN` 跟踪镜像页写没写过，`NtProtectVirtualMemory` 和 `NtQueryVirtualMemory` 都直接报对的结果。0010 在它上面一层再改一遍，结果：

| 镜像数据段里的页 | Windows（实测） | 0005 单独（DW 底座，实测） | 0005 + staging 0010（ge11-7-25/26，实测） |
|---|---|---|---|
| 写过，`VirtualQuery` | `0x04` | `0x04` | `0x04` |
| 写过，`VirtualProtect` 旧属性 | `0x04` | `0x04` | `0x04`（0005 已经给出 `0x04`，0010 不起作用） |
| 没写过，`VirtualQuery` | `0x08` | `0x08` | `0x08` |
| 没写过，`VirtualProtect` 旧属性 | `0x08` | `0x08` | **`0x04`**（0010 把对的结果改错） |
| 没写过，`VirtualProtectEx`/`NtProtectVirtualMemory` 旧属性 | `0x08` | `0x08` | `0x08`（0010 只在 `VirtualProtect` 里，Windows 上三者一致） |

这一行是按代码推算的，探测只调 `VirtualProtect`。

内核支持跟踪时，0010 对写过的页是多余的，对没写过的页是错的。

0010 只在内核不支持 userfaultfd 异步写保护时（Linux 6.7 以前，或者沙箱的 seccomp 不让用 `userfaultfd`，这时 0005 什么都不做）才有用：这时 CEF 那个检查靠它才能过。

还有一处副作用：用 `FILE_MAP_COPY` 映射的普通文件视图（不是镜像），没写过的页 0010 也报 `PAGE_READWRITE`。可是这种视图上 `VirtualProtect(PAGE_READWRITE)` 会失败，Windows 和 Wine 都是这样（`kernel32/tests/virtual.c` 的 `test_mapping`，`is_compatible_protection(PAGE_WRITECOPY, PAGE_READWRITE)` 为假，期望 `ERROR_INVALID_PARAMETER`）。先保存旧属性、再拿它恢复的程序，在 Windows 上能成功，在这里恢复会失败，页面停在只读。

## Windows 行为

- `writecopy_probe.windows.txt`：写过的镜像页 `VirtualQuery` 和 `VirtualProtect` 旧属性都是 `0x04`；没写过的都是 `0x08`；写过的页改成只读再改回来仍是 `0x04`
- `VirtualProtect` 转调 `VirtualProtectEx`，再转调 `NtProtectVirtualMemory`，中间不改旧属性，所以三者结果一致（没有单独实测后两个）
- `FILE_MAP_COPY` 视图上不能设 `PAGE_READWRITE`（Wine 的 kernel32 测试，在 Windows 上通过）

## 修复

- **kernelbase**：`VirtualProtect()` 改回上游的样子，直接返回 `VirtualProtectEx()` 的结果，去掉 0010/0011 的改写
- **ntdll**（`NtProtectVirtualMemory`）：0010 的猜测只留给它还有用的情况：镜像视图（`SEC_IMAGE`）**且**没被 0005 注册跟踪（没有 `VPROT_COPY_TRACKED`）时，旧属性 `PAGE_WRITECOPY` 报成 `PAGE_READWRITE`。和 0010 一样只改 `PAGE_WRITECOPY`，不改 `PAGE_EXECUTE_WRITECOPY`

修复后：

| 情况 | 写过的镜像页 | 没写过的镜像页 | `FILE_MAP_COPY` 视图（没写过） |
|---|---|---|---|
| 内核支持跟踪 | `0x04`（0005） | `0x08`，和 Windows 一致 | `0x08`，和 Windows 一致，能拿来恢复 |
| 内核不支持 | `0x04`（保留 0010 的猜测，CEF 照样能过） | `0x04`（和现在一样，猜错） | `0x08`（`FILE_MAP_COPY` 视图不再套用猜测） |

Proton 自己的 `simulate_writecopy`（按程序名/AppID 打开，或 `WINE_SIMULATE_WRITECOPY=1`）不受影响。

为什么不用 protonprep 里加 `-W ntdll-WRITECOPY` 的办法：那样也能去掉 0010/0011，但内核不支持时的退路就没了，还得另外补；放进 WE 补丁可以一处做完。代价是 GE 以后改动或去掉这两个 staging 补丁时，kernelbase 那段会打不上（会直接报错，不会悄悄出错），到时再改。

## 验证

测试版：`test-ge-backport`（提前移植 GE 补丁后的完整构建，见 `we/ge-backports.md`）上用 `we/quick-build.sh --prep … dlls/kernelbase dlls/ntdll` 编出 `test-ge-0054`。

### 1. 回归探测

`we/tests/regress.sh --compare <test-ge-backport 的结果> <test-ge-0054 的结果>`：只有 `writecopy_probe` 不同，只差一行：

```
< .data not written      query 0x08 type 0x1000000 | VirtualProtect(RO) ok 1 old 0x04 | back to RW old 0x02, now 0x08
> .data not written      query 0x08 type 0x1000000 | VirtualProtect(RO) ok 1 old 0x08 | back to RW old 0x02, now 0x08
```

和 `writecopy_probe.windows.txt` 比，除第一行（`wine yes/no`）外完全相同（`tests/writecopy_probe.ge26-0054.txt`）。其余 12 个探测相同。

### 2. writecopy_probe2

`data section, middle page written` 下 `pages 0-2 -> READONLY` 的旧属性从 `0x04` 变成 `0x08`（第 0 页没写过），其余不变；整份输出和 DW 底座时 0005 单独起作用的 `writecopy_probe2.wine6.txt` 逐行相同（`tests/writecopy_probe2.ge26-0054.txt`）。

### 3. 内核不支持跟踪时的退路

`systemd-run --user -p SystemCallFilter=~userfaultfd -p SystemCallErrorNumber=EPERM` 下分别跑两个构建的回归：`writecopy_probe` 两边完全相同，三行都是 `query 0x08 … old 0x04 … now 0x08`，和打 0054 之前一样，CEF 检查的那种情况照样能过。

### 4. 实机：WeGame（基于 CEF）

用 `test-ge-0054` 启动 WeGame（X11），主界面正常，3 分半里两个渲染进程一直是启动时的那两个，没有重启，也没有崩溃转储。之后又在嵌套 niri 里用 Wayland 驱动跑了一轮，同样正常。

### 没做的

- Wine 自己的 `kernel32:virtual`、`ntdll:virtual` 测试
- 其他基于 CEF/Chromium 的程序

## 风险

- **CEF 改成只靠 0005**：见验证第 4 步，WeGame 上没有出现渲染进程崩溃。0005 只跟踪镜像视图映射完成之后的写入，`PAGEMAP_SCAN` 出错时退回报 `PAGE_WRITECOPY`
- **退路的范围比 0010 大**：0010 只在 `VirtualProtect` 里，现在放在 `NtProtectVirtualMemory`，内核不支持时 `VirtualProtectEx`、直接调 `NtProtectVirtualMemory`，以及别的进程对本进程的 `VirtualProtectEx`（在本进程里执行），也会把没跟踪的镜像页报成 `PAGE_READWRITE`。只限镜像视图，在镜像上用 `PAGE_READWRITE` 恢复和用 `PAGE_WRITECOPY` 效果一样，所以不影响「保存再恢复」
- **`FILE_MAP_COPY` 视图里写过的页**：`VirtualProtect` 改回报 `PAGE_WRITECOPY`（Windows 报 `PAGE_READWRITE`，0010 碰巧报对了）。0005 不跟踪这种视图，和上游 Wine、DW 底座时的 WE-Proton 一样。真要对上，得把 0005 的跟踪扩展到 `FILE_MAP_COPY` 视图
- **依赖 staging 补丁的原文**：见「修复」最后一段
