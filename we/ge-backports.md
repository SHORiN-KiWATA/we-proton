# 提前移植的 GE-Proton 补丁

GE-Proton 主线在正式版之前已经加上、对 WE-Proton 用法有用的修复，先搬过来。

**做法**：文件从 GE-Proton 主线原样复制到相同路径，`patches/protonprep-valve-staging.sh` 里加的行也和 GE-Proton 主线一字不差。跟进下一个 GE-Proton 版本时，这些文件和 GE 的完全相同，合并不会冲突；protonprep 里一字不差的行 git 会合成一份，只有 GE 之后改过、挪过的行要手工去重（见文末）。

## 现在移植着的

无。

## 跟进下一个 GE-Proton 版本时

1. 合并后这些文件应该没有冲突（内容相同）
2. `patches/protonprep-valve-staging.sh` 里搜上表的补丁文件名，各应只剩一处；GE 把它们挪了位置时会出现两处，删掉我们这份
3. 新版本已经全部带上时，把上面的表清空，写成「无」

## 以前的

- GE-Proton11-12 之后搬的一个（`winedmo-disable-d3d11-aware-for-phantom-blade-zero`，GE 的 `e8c2a561e`）已经在 GE-Proton11-14 里
- GE-Proton11-11 之后搬的三个（em-fixups `0073`、`win32u-release-images-presented-to-hidden-windows`、`ntdll-take-in-flight-alert-when-wait-on-address-times-out`）已经随 GE-Proton11-12 进了正式版
- GE-Proton11-7 之后搬的一批（em-fixups `0024`、`0051`–`0067`，wineland-child-rendering `0041`，`server-dont-touch-freed-handle-table-in-close-handle`、`ntdll-block-apcs-until-process-exit-state-is-set`、winedmo 的 wow64 修复，FFmpeg 链接 dav1d）已经随 GE-Proton11-8 进了正式版。winedmo 那个是上游 Wine 的 2110c64d，GE-Proton11-8 起的 Valve Wine 已经包含，补丁文件和 protonprep 里的那一行跟进 GE-Proton11-11 时删掉了
