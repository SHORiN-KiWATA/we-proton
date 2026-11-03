# 上游来源、版本与许可证

核对日期：2026-11-03。提议评审分支：`proposed-uu-network-fixes`。

本提案面向 [SHORiN-KiWATA/we-proton](https://github.com/SHORiN-KiWATA/we-proton)，提供 **0062、0063 两项 Wine 网络补丁，以及可选的原生 Steam 共享网络入口、测试源码和文档**。提案材料仅包含源码和受控输出，不附带运行器、pasta、UU 或 Steam 二进制。

Wintun 支持、独立网络命名空间、pasta 网络连接及会话加入能力来自上游。提议增加的内容是接口缓存过滤、路由枚举性能修复和启动入口整合，不改变这些能力的作者归属，也不代表网易、Valve、Wine 或其他上游项目的官方支持。

提案作者：**Felix Li，with OpenAI Codex assistance**。该署名针对新增实现、问题排查、测试和资料整理；继承代码的作者及许可证保持不变。

## 直接上游与继承关系

| 项目 | 来源与版本 | 在本方案中的作用 |
| --- | --- | --- |
| WE-Proton / SHORiN-KiWATA | [we-proton-ge11-11-31](https://github.com/SHORiN-KiWATA/we-proton/releases/tag/we-proton-ge11-11-31)，历史基点 [`4617d991e787603f5300c484f54b4bd61c776090`](https://github.com/SHORiN-KiWATA/we-proton/commit/4617d991e787603f5300c484f54b4bd61c776090) | 历史开发与测试的直接基底；提供 Windows 加速器所需的已有兼容实现 |
| GE-Proton / GloriousEggroll | [proton-ge-custom](https://github.com/GloriousEggroll/proton-ge-custom)，`GE-Proton11-11` | WE-Proton 的项目、构建框架与基础补丁集 |
| DW-Proton / Dawn Winery | [dwproton](https://dawn.wine/dawn-winery/dwproton)，`dwproton-11.0-15` | WE-Proton 合入的另一来源补丁集 |
| Valve Proton | [ValveSoftware/Proton](https://github.com/ValveSoftware/Proton) | Proton 运行器及构建体系的上游 |
| Wine / Valve Wine | [Wine 项目](https://www.winehq.org/)、[ValveSoftware/wine](https://github.com/ValveSoftware/wine)，子模块基点 `c53678ab22a85f0fad3588dd34f0452a463bd015` | Windows API 实现；本提案的两项补丁修改其网络接口代码 |
| Steam Linux Runtime / pressure-vessel | [steam-runtime-tools](https://gitlab.steamos.cloud/steamrt/steam-runtime-tools) | Proton 所需的运行环境，由 Valve 及其上游贡献者提供 |

以上版本表描述历史受测来源，不能作为最新上游 `main` 已完成验证的声明。提案沿用项目的子模块和补丁应用结构：保留上游作者、源码和许可证，在构建时按项目顺序应用补丁。本提案固定在上游 `407044f0a8395d6dd0c73318a2f7ae42300860ea`；该基点与历史基点之间的网络代码一致性、补丁应用及入口测试检查见 [提交前检查](VALIDATION.md#本次提交前检查2026-11-03)。

## 提议改动与历史测试基底

| 改动 | 内容 | 原始 Wine 开发提交 |
| --- | --- | --- |
| 0062 | 校验名称与索引的当前对应关系，过滤关系已失效的 Unix 接口缓存项 | `0784ca7162a8532a8587b9c3dcf38668379eb3ce` |
| 0063 | 在一次内部 IPv4 或 IPv6 路由枚举调用内复用成功转换，消除 0062 引入的重复检查开销 | `28db1f7cd049a6f6e4be9fb5f8ce275868b7a4bb` |
| Steam 共享网络入口 | 调用原版 wrapper，让原生 Steam 加入加速器网络；核对已运行 Steam 的网络归属，不额外注入加速器专用运行器变量，保留用户已有环境 | 本提案的独立启动脚本 |

0062 与 0063 作为配套修改提出。0063 将合成路由批量操作恢复到接近原有性能水平，未消除已有的逐条操作及全表扫描算法，也不是所有客户端启动耗时的通用修复。

历史实机和隔离测试曾使用包含 **0061 桌面兼容补丁**的运行器作为基底。0061 的原始开发提交为 `6e1fb3f92cd8da1d8dfbdeb0a37afa6dd39ac4f9`，用于窗口身份与宿主装饰；**本提案不包含该补丁或其桌面辅助组件**。历史测试运行器与本提案源码树并不完全相同，历史结果不能自动扩展为本提案所有环境、所有程序的端到端验证。

以上原始开发提交用于追踪历史补丁来源，不等同于最终 PR 提交号，也不表示上游已经接纳修改。提案仅保留通用源码、补丁和脱敏结果摘要，不包含个人工作目录、原始用户日志、账号数据或既有 Wine prefix。

## shorin-proton-wrapper：未修改的外部依赖

作者与仓库：[SHORiN-KiWATA/proton-wrapper](https://github.com/SHORiN-KiWATA/proton-wrapper)。

| 项目 | 精确标识 |
| --- | --- |
| 受测提交 | [`d8ca9e73bf44d14eadfb29c95e17b57990e0b7f4`](https://github.com/SHORiN-KiWATA/proton-wrapper/commit/d8ca9e73bf44d14eadfb29c95e17b57990e0b7f4) |
| 受测脚本 | [`shorin-proton-wrapper`](https://github.com/SHORiN-KiWATA/proton-wrapper/blob/d8ca9e73bf44d14eadfb29c95e17b57990e0b7f4/shorin-proton-wrapper) |
| Git blob | `350485de888d8dd062afbccd5f3ffe4edeee5daf` |
| SHA256 | `19a5ed00b0571d48bb41c7e38e6f5a254db96e06b6b6370f2680caab265d997c` |

受测脚本与该上游版本逐字节一致。提案入口通过 CLI 调用它，不修改、复制或重新打包 wrapper 源码。

上游 [`nfpm.yaml`](https://github.com/SHORiN-KiWATA/proton-wrapper/blob/d8ca9e73bf44d14eadfb29c95e17b57990e0b7f4/nfpm.yaml) 声明 `license: MIT`；截至核对日期，仓库根目录未提供独立 `LICENSE` 文件。这里准确记录包元数据中的声明，不将本提案许可证套用到 wrapper。

上游在 2026-11-02 增加了 VC++ runtime 安装、工作目录及盘符处理改动。这里的验证对应表中固定版本，不代表当日或未来 `main`、`Latest` 已通过相同测试。

上游还提供通过逐游戏 wrapper 启动参数共享 Wine prefix 的用法。本提案的 Steam 入口通过父进程网络继承接入同一网络，保留 Steam 各游戏原有 prefix。独立启动器或自行创建网络命名空间的程序仍需分别核实，不能将两种配置方式及其验证范围混为一谈。

## passt / pasta：未修改的外部依赖

来源：[passt 项目](https://passt.top/)，[Git 仓库](https://passt.top/passt)。感谢 Stefano Brivio、Red Hat 及项目其他贡献者。

| 项目 | 精确标识 |
| --- | --- |
| 受测源码提交 | `6aa4f88bd9846411edff3ed70460475b538addca` |
| 受测本地构建产物 SHA256 | `1dd758b40ab9a6ba4fdd5fc6792c11bb180e87fdeb1b56eab6babe4d4a4b3ddd` |
| 源码改动 | 无；源码工作树与该提交一致 |

pasta 负责网络命名空间内 TAP 与宿主 socket 之间的连接。它不是本提案编写的驱动，也不是 Wine 子模块的一部分。本提案不捆绑该二进制。其主要源码为 `GPL-2.0-or-later`，其他文件按逐文件声明适用许可证；上游 `LICENSES` 还包含 `BSD-3-Clause`。

## 许可证与 AI 署名

| 范围 | 许可及依据 |
| --- | --- |
| Proton 顶层代码 | `BSD-3-Clause`；保留 [LICENSE](../../LICENSE) 与 [LICENSE.proton](../../LICENSE.proton) |
| 本提案新增启动脚本、集成文档 | 沿用项目 `BSD-3-Clause`，文件另有声明时以该声明为准 |
| 修改 Wine 源码的 0062、0063 补丁 | `LGPL-2.1-or-later`；保留原文件版权及 [Wine LICENSE](https://github.com/ValveSoftware/wine/blob/c53678ab22a85f0fad3588dd34f0452a463bd015/LICENSE)、[COPYING.LIB](https://github.com/ValveSoftware/wine/blob/c53678ab22a85f0fad3588dd34f0452a463bd015/COPYING.LIB) |
| `ndis_interface_lifecycle_probe.c`、`route_perf_probe.c` | 文件头明确为 `LGPL-2.1-or-later`，不因位于 `we/` 而改成 BSD |
| wrapper、pasta 及其他上游组件 | 保持各自许可；不适用本提案的统一重新许可 |

整个 Proton 由多种许可证组成，不能把完整项目或 `we/` 下所有文件一概标为 BSD。提案材料仅包含源码和受控输出；源码、补丁和测试保留原有版权、许可及作者信息。

新增修改和测试由 **Felix Li 在 OpenAI Codex 协助下**编写与整理，相关提交保留 AI 协作的 `Co-Authored-By`。此署名只针对新增工作，不替代上游作者。所有历史验证均有明确边界，不把未执行的原生 Windows 测试或实际游戏测试写成已完成。

## 与上游及客户端的边界

本提案面向 WE-Proton 的代码评审，遵循项目贡献指引；历史开发所依据的版本见 [AGENTS.md](https://github.com/SHORiN-KiWATA/we-proton/blob/4617d991e787603f5300c484f54b4bd61c776090/AGENTS.md)。不涉及向 Wine、GE-Proton、DW-Proton 等项目单独提交修改，也不代表这些项目已认可或接纳提案。

网易 UU、Steam 及其商标属于各自权利人。本提案提供适配与启动代码，不包含这些客户端的安装包、图标、账号数据或游戏内容，不声称提供客户端官方支持或额外服务权益。
