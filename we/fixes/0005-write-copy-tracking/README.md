# 0005：写过的写时复制页仍报告 PAGE_WRITECOPY

补丁：`patches/wine/0005-ntdll-Report-written-write-copy-pages-as-read-write.patch`（`dlls/ntdll/unix/virtual.c`，以及 `dlls/kernel32/tests` 里对应的 `todo_wine`）。

## 现象

基于 CEF（Chromium Embedded Framework）的渲染进程启动 1 到 7 秒后触发 `int3`（`0x80000003`，Chromium 的 `CHECK` 失败），位置在 `libcef.dll` 里，主进程重试几次后放弃。

## 根因

反汇编崩溃点：一个小函数调用 `VirtualProtect(addr, size, PAGE_READONLY, &old)`，调用成功，但 `old != PAGE_READWRITE` 时故意崩溃。`addr` 是 `libcef.dll` 数据段里一个已经写过的全局变量（Chromium 启动后把它改成只读）。

Windows 记录每个写时复制页是否已被私有复制过。`we/tests/writecopy_probe` 的实测：

| 镜像数据段里的页 | Windows | Wine 原来 |
|---|---|---|
| 写过的 | `PAGE_READWRITE`（`0x04`） | `PAGE_WRITECOPY`（`0x08`） |
| 没写过的 | `PAGE_WRITECOPY` | `PAGE_WRITECOPY` |
| 写过的页改成只读再改回可写 | 仍是 `PAGE_READWRITE` | `PAGE_WRITECOPY` |

Wine 把写时复制页映射成 `MAP_PRIVATE` 可写（段在文件里没按页对齐时，干脆读进匿名内存），复制由内核完成，Wine 不知道哪些页被写过，所以一律报告 `PAGE_WRITECOPY`。Wine 自己的 `kernel32` 测试检查了这个行为（`virtual.c` 两处检查在循环里共执行 525 次，`loader.c` 一处执行 12 次），都标着 `todo_wine`。

## 修复

用内核的 userfaultfd 异步写保护（Linux 6.7+，Wine 的写监视已经在用同一套机制）记录镜像映射之后的写入：

- `map_image_into_view()` 结束时（段已映射、重定位已完成）把整个镜像 view 注册为 `UFFDIO_REGISTER_MODE_WP` 并写保护，标记 `VPROT_COPY_TRACKED`。Wine 自己在加载阶段对页面的写（读入段内容、清零、重定位）都发生在这之前，不算数
- `NtProtectVirtualMemory` 和 `NtQueryVirtualMemory`（`MemoryBasicInformation`）报告保护属性之前，用 `PAGEMAP_SCAN` 查 `PAGE_IS_WRITTEN`，给写过的写时复制页设上 `VPROT_COPIED`；`get_win32_prot()` 本来就会把 `VPROT_COPIED` 的写时复制页报告成 `PAGE_READWRITE` / `PAGE_EXECUTE_READWRITE`
- 内核不支持时（没有 uffd 异步写保护）什么都不变，和原来一样

所有进程都生效，不需要按程序名开启。Proton 原有的 `simulate_writecopy`（对少数程序把所有写时复制页都当成已复制）保留不动。

## 验证

- `we/tests/writecopy_probe`：输出和 Windows 逐行一致（写过的 `0x04`，没写过的 `0x08`，改回可写后保持）
- `we/tests/writecopy_probe2`：代码页打补丁（`EXECUTE_READ` → `EXECUTE_READWRITE` 未写时报告 `EXECUTE_WRITECOPY`，写后 `EXECUTE_READWRITE`，改回 `EXECUTE_READ` 再打开仍是 `EXECUTE_READWRITE`）、只打开不写的代码页、数据段中间一页写过时的区域边界。Wine 下的输出在 `writecopy_probe2.wine6.txt`；Windows 对照还没跑
- Wine 自己的测试（`build/wine-tests`，`--enable-tests` 单独构建）：

  | 测试 | 修复前 | 修复后 |
  |---|---|---|
  | `ntdll:virtual` | 2340 项，0 失败 | 2340 项，0 失败 |
  | `kernel32:virtual` | 30936 项，0 失败 | 31461 项，0 失败；那两处检查对镜像映射的 525 次执行现在和 Windows 一致，补丁里把它们的 `todo_wine` 改成只对非镜像映射生效 |
  | `kernel32:loader` | 17803 项，17 个 todo | 17803 项，5 个 todo；「镜像段写入后 WRITECOPY 变成 WRITE」的 12 处检查通过，补丁里去掉了这处 `todo_wine_if` |

- 实机：CEF 渲染进程不再崩溃（用第一版做法验证过；现在这版还需要实机复测）

## 排查过程和弯路

1. 第一版：把出问题的进程名加进 Proton `simulate_writecopy` 的名单。能用，但只是按进程名打开一个近似模式（所有写时复制页都报告成已复制，连没写过的也是），已经换掉。
2. 第二版：读 `/proc/self/pagemap`，把「匿名、独占映射」的写时复制页当成已复制。实测不行：段没按页对齐时 Wine 把它读进匿名内存，所有页一开始就是匿名的，没写过的页也报告 `PAGE_READWRITE`，和 Windows 反方向出错，而且对所有进程生效。带完整性检查的程序会把「没写过的页看起来被写过」当成被篡改。
3. 第三版（现在的）：只记录加载完成之后的写入，用 uffd 异步写保护 + `PAGEMAP_SCAN`，和 Windows 一致。

## 已知限制

- 只跟踪镜像 view。带 `FILE_MAP_COPY` 的普通文件映射仍然按原来的方式报告（Wine 测试里这部分的 `todo_wine` 保留）
- 依赖内核支持 uffd 异步写保护（Linux 6.7+）；不支持时退回原来的行为
- 每个镜像页第一次被写时多一次内核内部处理的缺页（不经过用户态）
