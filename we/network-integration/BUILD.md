# 构建、探针与回退

本提案提供源码、探针和说明，不附带预编译运行器。直接使用历史上游二进制不会自动包含提议中的 0062/0063。历史受测运行器包含额外桌面补丁，详见 [VALIDATION](VALIDATION.md)；不能把新构建产物直接视为已通过那轮客户端测试。

## 代码基点与全量构建

历史开发与测试基点是 WE-Proton `4617d991e787603f5300c484f54b4bd61c776090`（`we-proton-ge11-11-31`），Wine 子模块为 `c53678ab22a85f0fad3588dd34f0452a463bd015`。本提案的提交基点为上游 `main` 的 `407044f0a8395d6dd0c73318a2f7ae42300860ea`。相比历史基点，Wine gitlink、补丁树及构建入口未改变；两项补丁已在导出的既有补丁后网络源码上顺序检查并应用，结果与历史受测源码逐字节一致。提案目录中的 45 项入口测试已重跑通过；完整运行器没有重新构建，详见 [本次提交前检查](VALIDATION.md#本次提交前检查2026-11-03)。

使用独立的源码目录，不要在当前正在使用的运行器或包含客户端数据的目录内构建。上游构建脚本会重置子模块并重新创建构建输出目录。

```sh
git clone --branch proposed-uu-network-fixes https://github.com/liyaomeng202114981-ops/we-proton.git we-proton-uu-network
cd we-proton-uu-network
we/build.sh --name we-proton-uu-network-review --jobs 4
```

`proposed-uu-network-fixes` 是本提案的评审分支。`we-proton-uu-network-review` 只是独立输出目录的名称，不表示某个已发布运行器版本。

继承的 `we/build.sh` 需要 Docker、下载子模块及 SDK，首次全量构建可能需要数小时。沿用上游的准备顺序：GE/Staging → DW-Proton → WE-Proton → 提议的两项网络补丁。详细依赖见[上游构建说明](../README.md)。文档准备没有重新执行全量构建，也不宣称新整包已受测。

输出包括运行器与对应源码归档。把新产物解压到一个独立目录，再将启动脚本配置中的 `UU_RUNNER` 指向其中的 `proton`；不要覆盖原版本。切换运行器应在该 UU 前缀中的程序完全正常退出之后进行。

历史 SDK 固定镜像为：

```text
registry.gitlab.steamos.cloud/proton/steamrt4/sdk/x86_64@sha256:6c1789cad862fd8ed9d46d71bbf8752bb7cb2336ab5e9680c0f0e3e7c88d77f1
```

继承的全量 builder 默认使用其 Makefile 中的镜像配置。若要做严格二进制复现，必须另外固定工具链与全部构建参数；本提案不承诺上述全量命令重建出历史模块的逐字节相同结果。

## Wine 探针源码

公开三份历史探针，保留其代码与许可证：

| 探针 | 用途与前提 |
| --- | --- |
| [ndis_interface_lifecycle_probe.c](../tests/ndis_interface_lifecycle_probe.c) | `--isolated` 模式下建立、关闭、同名重建临时接口，查询 Windows API；需要当前 WE-Proton 内置 Wintun |
| [route_perf_probe.c](../tests/route_perf_probe.c) | `--isolated --routes 3000` 合成路由循环；需外层 Linux fixture 在 `Z:\test\output` 的 ready/configured 握手后配置临时接口地址和 198.18.0.0/15 测试路由 |
| [route_interface_enumeration_probe.c](../tests/route_interface_enumeration_probe.c) | 持续查询受控 IPv4/IPv6 路由；需外层 fixture 在独立网络内配置两张临时 TAP，执行删除/同名重建并比较输出 |

这些 C 文件不是在真实 UU 网络中直接运行的一键测试。历史 90 项边界检查和 194 项路由语义断言包含外层 Linux fixture；仅编译或运行一个 C 探针不能声称重现了全部检查。本提案未包含完整历史容器调度器，因其依赖本机专属产物路径。提供的输出与方法用于审查，完整重新测量需按同一 fixture 条件另建隔离环境。

例如，用 MinGW 编译路由读取探针：

```sh
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -static \
  we/tests/route_interface_enumeration_probe.c -o route-probe.exe \
  -liphlpapi -lws2_32
```

应在无外网的临时容器/网络命名空间和新的 Wine 前缀里执行网络探针；不要连接实际 UU 会话、Steam 或真实游戏。构建产物、临时 prefix 和完整日志无需提交到仓库。

## 历史受测网络模块指纹

| 文件 | SHA256 |
| --- | --- |
| x86_64 Unix nsiproxy.so | `d5c538f1eda8d9bbb11c79afbc5f4e01303d5c0ee18a1173ec52c8356202e807` |
| i386 Unix nsiproxy.so | `81dd705bd950bc8bb58fe5676265579522a7c0a789c21af4d79d970621323f3a` |

这些是既有本地测试产物的追踪信息，不是本仓库可下载二进制的校验声明。若未来分发修改后的二进制，须同时提供与该资产相对应的完整修改源码、构建方法与各组件许可证。

## 回退

启动脚本可以从仓库独立运行，不自动接管系统 Steam 图标或安装目录。停用时正常退出使用该入口的客户端，再使用原来的启动入口。仅当自行添加了专用快捷方式时删除自己的快捷方式。

运行器版本通过配置切换，保留原版即可回退。回退到没有 0062 的版本也会恢复旧接口身份缺陷；0062 单独使用又有已知性能回退，因此不应把只安装其中一个网络补丁作为推荐状态。
