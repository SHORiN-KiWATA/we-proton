# 0060：实现 ShutdownBlockReasonCreate/Query/Destroy

补丁：`patches/wine/0060-user32-Implement-ShutdownBlockReasonCreate-Query-Des.patch`（`dlls/user32/user_main.c`、`user32.spec`、`include/winuser.h`）。

## 现象

WeGame 后台下载好新版本（放在 `WeGame\wegame_update`）之后，每次启动都「闪退」：主界面一闪就没了，进程退出码 0，没有崩溃报告。几分钟前还能正常打开，下载完更新后的下一次启动就不行了。

过程：`wegame.exe` 发现有待安装的更新，启动 `Assistant.exe -m "<wegame_update>"|"<WeGame>" -e wegame.exe -k wegame.exe ...` 来替换文件，然后自己退出。Assistant 读完要结束的进程列表后调用 `ShutdownBlockReasonCreate(自己的窗口, "Please wait for WeGame update")`，失败（`lasterr:120`）就写 `update.tmp`（`OverwriteStatus=7299004`）直接退出，一个文件都不换，也不重新启动 WeGame。下一次启动重复同样的过程。

`log/alog/` 里能看到以前每一次更新（10-04、10-12 的 multi_group、10-17）都在这一步失败。

## 根因

Wine 的 `ShutdownBlockReasonCreate`/`ShutdownBlockReasonDestroy` 是空壳，固定返回 FALSE、错误码 `ERROR_CALL_NOT_IMPLEMENTED`；`ShutdownBlockReasonQuery` 没有导出。

## Windows 行为

`we/tests/shutdown_block_probe.c` 在 Windows 11 26200（SSH 会话）里跑，输出 `tests/shutdown_block_probe.windows.txt`：

- 自己的顶层窗口：Create 成功、不改错误码；再次 Create 替换原因；Query 返回原因
- Query：缓冲区为 NULL 时只返回长度；缓冲区不够时截断（保留结尾的 0），返回 TRUE，长度一律是含结尾 0 的完整长度
- 原因为 NULL：失败，87；空字符串：成功，Query 长度 1
- 没设过原因时 Query/Destroy：失败，87；Destroy 两次：第二次 87
- 子窗口：三个函数都 87；NULL、无效句柄、已销毁的窗口：1400
- 同一进程的另一个线程：可以设置、查询、删除
- 桌面窗口：Create/Destroy 返回值是 5（`ERROR_ACCESS_DENIED`）、错误码不变；Query 87

## 修复

原因存成窗口属性（`__wine_shutdown_block_reason`，进程堆里的副本），按上面的规则检查参数；导出 `ShutdownBlockReasonQuery`。其他进程的窗口一律 `ERROR_ACCESS_DENIED`（没在 Windows 上测）。

## 验证

- `we/quick-build.sh --base build/test-0059 --name test-0060 dlls/user32`
- `we/tests/shutdown_block_probe.sh <runner> <输出文件>`（Steam Linux Runtime 4.0、WoW64、私有 Xvfb）：test-0060 的输出 `tests/shutdown_block_probe.wine-0060.txt` 和 Windows 逐行相同；打补丁前（ge11-7-29，`tests/shutdown_block_probe.ge29.txt`）连 Query 都没有导出
- WeGame：用 test-0060 启动，Assistant 把 6.6.1.9201 更新到 6.7.0.10150（`SmartMoveFile ... success`、`Move Directory success`），然后重新启动 `wegame.exe -s 0 0 1219 54`，主界面正常。更新前 WeGame 目录的 reflink 备份在 `~/.cache/wegame-before-update-20261017`
- WE-Proton 的回归探测和 ge11-7-29 相同

## 没做/没验证的

- 其他进程的窗口（Windows 上的关机界面就是从别的进程查询的）：Wine 没有关机界面，这里直接拒绝
- 窗口销毁时没有释放保存原因的那块内存（每个窗口最多一份，很小）
