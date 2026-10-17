# WE-Proton

中文 | [English](README.en.md)

## 理念和简介

WeGame 客户端在 Linux（Wine / Proton）上会出现无法正常下载游戏、窗口错位、交互异常、黑屏等问题。本项目基于 GE-Proton 和 DW-Proton：以 GE-Proton 为底，合入 DW-Proton 的补丁（二次元游戏等的兼容修复），再修复导致这些问题的 Wine / Proton / vkd3d-proton 的 bug，目标是让 WeGame 客户端本身，以及不带内核级反作弊的游戏（例如单机游戏）能够正常运行。

配套的启动器：[wegame-launcher](https://github.com/SHORiN-KiWATA/wegame-launcher)，一键安装和运行 WeGame。

## 关于 AI

本项目新增的补丁是在 AI 辅助下编写和调试的，每个补丁的现象、根因和验证过程都写在 [`we/fixes/`](we/fixes/) 里。

## 关于竞技游戏和反作弊

- 本项目不做任何竞技游戏相关的修复和兼容工作，不支持三角洲行动、无畏契约这类竞技游戏，相关 issue 会直接关闭。
- 本项目不对 ACE 等反作弊做任何适配，也不接受任何绕过反作弊或作弊相关的请求和代码。
- Linux 不是 ACE 反作弊支持的平台。即使带 ACE 的游戏能够运行，也可能被判定为环境异常而封号，使用本项目造成的封号等后果请自行承担。

## 演示视频

https://github.com/user-attachments/assets/42438b93-5a36-4557-8a0f-4b65b53d5e07

### 实际试玩过的游戏

> 仅供参考，版本可能导致结果变化。

>✅ 代表正常游玩；⚠️ 代表能运行游玩但存在异常和风险；❌ 代表无法运行

|         游戏名         | 游玩结果 |                                                          备注                                                          |
| :--------------------: | :------: | :--------------------------------------------------------------------------------------------------------------------: |
|       雷神加速器       |    ✅     | 不是广告。安装`passt`这个包后在`wegame-launcher`设置里开启`独立网络命名空间`，加速时选择`模式三`的节点就可以正常加速。 |
|      网易UU加速器      |    ✅     |                                同雷神加速器，加速时选择`路由模式`的节点就可以正常加速。                                |
|       王者万象棋       |    ✅     |                                                                                                                        |
|      王者荣耀世界      |    ✅     |                                                                                                                        |
|         三国杀         |    ✅     |                                                                                                                        |
|      洛克王国世界      |    ✅     |                                          仅支持中国地区游玩，国外要开加速器。                                          |
|        星痕共鸣        |    ✅     |                                                                                                                        |
|          伊莫          |    ✅     |                                                                                                                        |
|       最终幻想14       |    ✅     |                                                                                                                        |
|      地下城与勇士      |    ✅     |                                                                                                                        |
|     古剑奇谭网络版     |    ✅     |                                                                                                                        |
|       天涯明月刀       |    ✅     |                                       首次启动会卡0%，是在编译着色器不是真的卡了                                       |
|         封印者         |    ✅     |                                                                                                                        |
|        黑色沙漠        |    ✅     |                                    首次启动会卡黑屏，也是在编译着色器，不是真的卡了                                    |
|        命运方舟        |    ✅     |                                                                                                                        |
|         激战2          |    ✅     |                                                                                                                        |
|          鸣潮          |    ✅     |                                                                                                                        |
|         QQ飞车         |    ✅     |                                                    可能遇到窗口问题                                                    |
|        迷你世界        |    ✅     |                                                                                                                        |
|     绿梦：时空之声     |    ✅     |                                                                                                                        |
|     暗区突围：无限     |    ⚠️     |                                                   数据异常、画面异常                                                   |
|        英雄联盟        |    ⚠️     |                                                        数据异常                                                        |
|        无畏契约        |    ⚠️     |                                                        数据异常                                                        |
|       三角洲行动       |    ⚠️     |                                                        数据异常                                                        |
|        穿越火线        |    ⚠️     |                                                        数据异常                                                        |
|     和平精英模拟器     |    ⚠️     |                                                        数据异常                                                        |
| 使命召唤手游高清模拟器 |    ❌     |                                              无法加载 ACE 需要的驱动程序                                               |
|       逆战：未来       |    ❌     |                                              无法加载 ACE 需要的驱动程序                                               |

## 安装

1. 从 [Releases](https://github.com/SHORiN-KiWATA/we-proton/releases) 下载 `we-proton-<版本>.tar.xz`
2. 用 `tar` 解压到启动器的 Proton 目录，以 Steam 为例：

   ```
   mkdir -p ~/.local/share/Steam/compatibilitytools.d
   tar -xf we-proton-<版本>.tar.xz -C ~/.local/share/Steam/compatibilitytools.d/
   ```

   Lutris、Heroic 也能识别 Steam 目录里的 Proton，直接用上面的命令即可；也可以把 `-C` 后面换成它们自己的 Proton 目录。

   > 不要用 7-Zip 解压。包里很多 DLL 是符号链接，新版 7-Zip 会把它们当成危险路径跳过，解出来是空文件，前缀会坏掉，各种程序莫名报错。如果解压时出现过 `Dangerous link path was ignored`，请删掉解压出的目录，用上面的命令重新解压，再把用它建过的前缀也删掉重建。

3. 在启动器里选 `we-proton-<版本>`。wegame-launcher 默认自动使用找到的最新 WE-Proton，也可以手动选择：在图形界面的「设置 → 运行器」里选，或者运行

   ```
   wegame-launcher runner we-proton-<版本>
   ```

## 从源码构建

需要 docker。第一次完整构建要几个小时：

```
git clone https://github.com/SHORiN-KiWATA/we-proton.git
cd we-proton
we/build.sh --name we-proton-<版本>             # 输出到 build/
we/build.sh --name we-proton-<版本> --install   # 同时安装到「~/.local/share/proton/runners/WE-Proton Latest」
```

脚本会拉取子模块、用 `patches/protonprep-valve-staging.sh` 打补丁（GE-Proton 的补丁，然后是 DW-Proton 和 WE-Proton 的），再在 Steam Linux Runtime SDK 容器里 `make redist`。详细步骤、测试程序、诊断工具以及跟进上游新版本的方法见 [`we/README.md`](we/README.md)。

## 仓库结构

- `patches/wine/`、`patches/vkd3d-proton/we-*.patch`：WE-Proton 的补丁
- `patches/dwproton/`：DW-Proton 有、GE-Proton 没有的 Wine 补丁
- `patches/` 下其余目录和 `patches/protonprep-valve-staging.sh`：GE-Proton 的补丁和打补丁脚本（其中 `protonfixes/0003`–`0005` 和 `dxvk/dw-*` 来自 DW-Proton，`protonfixes/0051` 是 WE-Proton 自己的）。子模块停在上游 commit，构建前由脚本打上所有补丁
- `we/fixes/`：每个补丁一份报告（现象、根因、修复、验证）
- `we/tests/`：测试程序和修复前后的输出
- `we/diag/`：诊断补丁和脚本，不进正式构建
- `we/build.sh`：构建脚本
- `we/sync/`、`we/upstream-sync.md`：跟进 GE-Proton、DW-Proton 新版本的脚本和操作手册
- 其余是 GE-Proton / Proton 的原有内容

## 源码与许可证

- Proton 顶层内容：BSD-3-Clause（Valve Corporation，见 [`LICENSE`](LICENSE)、[`LICENSE.proton`](LICENSE.proton)）
- Wine：Valve 的 Wine 加上 `patches/` 里的补丁，LGPL-2.1
- vkd3d-proton：上游 vkd3d-proton 加上 `patches/vkd3d-proton/` 里的补丁，LGPL-2.1；其中 dxil-spirv 子项目以及改它的补丁为 MIT
- 其他组件见各自目录下的 `LICENSE`、`COPYING`，以及发布包里的 `LICENSE`、`LICENSE.OFL`、`PATENTS.AV1`
- `we/` 下的脚本和文档沿用 BSD-3-Clause

## 致谢

- Valve 和 CodeWeavers 的 [Proton](https://github.com/ValveSoftware/Proton)
- GloriousEggroll 的 [GE-Proton](https://github.com/GloriousEggroll/proton-ge-custom)
- Dawn Winery 的 [DW-Proton](https://dawn.wine/dawn-winery/dwproton)
- [Wine](https://www.winehq.org/)、[DXVK](https://github.com/doitsujin/dxvk)、[vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton) 等项目
