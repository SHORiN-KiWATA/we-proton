# WE-Proton

中文 | [English](README.en.md)

## 理念和简介

WeGame 客户端在 Linux（Wine / Proton）上会出现无法正常下载游戏、窗口错位、交互异常、黑屏等问题。本项目在 DWProton 的基础上修复导致这些问题的 Wine / Proton / vkd3d-proton 的 bug，目标是让 WeGame 客户端本身，以及不带内核级反作弊的游戏（例如单机游戏）能够正常运行。

配套的启动器：[wegame-launcher](https://github.com/SHORiN-KiWATA/wegame-launcher)，一键安装和运行 WeGame。

## 关于 AI

本项目新增的补丁是在 AI 辅助下编写和调试的，每个补丁的现象、根因和验证过程都写在 [`we/fixes/`](we/fixes/) 里。

## 关于竞技游戏和反作弊

- 本项目不做任何竞技游戏相关的修复和兼容工作，不支持三角洲行动、无畏契约这类竞技游戏，相关 issue 会直接关闭。
- 本项目不对 ACE 等反作弊做任何适配，也不接受任何绕过反作弊或作弊相关的请求和代码。
- Linux 不是 ACE 反作弊支持的平台。即使带 ACE 的游戏能够运行，也可能被判定为环境异常而封号，使用本项目造成的封号等后果请自行承担。

## 演示视频

https://github.com/user-attachments/assets/42438b93-5a36-4557-8a0f-4b65b53d5e07

## 修了什么

在 DWProton 官方发布包的基础上，只替换了这些文件：

- Wine：`files/bin/wineserver`、`files/lib/wine/x86_64-unix/ntdll.so`、`files/lib/wine/x86_64-unix/win32u.so`、`files/lib/wine/x86_64-windows/ntoskrnl.exe`
- vkd3d-proton（D3D12）：`files/lib/wine/vkd3d-proton/{x86_64,i386}-windows/` 下的 `d3d12.dll`、`d3d12core.dll`

| 补丁              | 修之前                                                                                                                                                | 报告                                                      |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------- |
| wine 0001         | 一些第三方内核驱动加载失败，或运行一段时间后因为调用未实现的函数被结束                                                                                | [0001](we/fixes/0001-ntoskrnl-driver-functions/)          |
| wine 0002         | wineserver 崩溃，前缀里的所有程序一起消失，还会留下占着显示连接的残留进程                                                                             | [0002](we/fixes/0002-wineserver-thread-input-uaf/)        |
| wine 0003         | 直接执行 `syscall` 指令、并在栈指针下方存数据的代码，返回后数据被改坏而崩溃                                                                           | [0003](we/fixes/0003-native-syscall-red-zone/)            |
| wine 0004         | 靠 UDP `connect(0.0.0.0)` 唤醒 `select()` 的程序，每次都要等满超时，网络请求极慢                                                                      | [0004](we/fixes/0004-udp-connect-unspecified/)            |
| wine 0005         | 写时复制页被写过之后仍报告 `PAGE_WRITECOPY`，基于 CEF 的程序渲染进程一启动就崩                                                                        | [0005](we/fixes/0005-write-copy-tracking/)                |
| wine 0006         | 活动窗口消失后，不该被激活的覆盖层窗口（`WS_EX_NOACTIVATE`）被设成前台，全屏程序因失去焦点而反复最小化                                                | [0006](we/fixes/0006-noactivate-activation/)              |
| wine 0007         | 用 `UpdateLayeredWindow` 绘制的子窗口（`WS_CHILD` + `WS_EX_LAYERED`）变成一个单独的窗口，出现在屏幕左上角或被窗口管理器当成另一个窗口，不跟着主窗口走 | [0007](we/fixes/0007-layered-child-windows/)              |
| vkd3d-proton 0001 | 部分 D3D12 着色器被翻译成非法的 SPIR-V，AMD 显卡（Mesa RADV）上直接崩溃                                                                               | [vkd3d-0001](we/fixes/vkd3d-0001-dxil-spirv-loop-breaks/) |

## 安装

1. 从 [Releases](https://github.com/SHORiN-KiWATA/we-proton/releases) 下载 `we-proton-<版本>.tar.xz`
2. 解压到启动器的 Proton 目录：
   - wegame-launcher：`~/.local/share/proton/runners/`
   - Steam：`~/.local/share/Steam/compatibilitytools.d/`
   - Lutris、Heroic 等：各自的 Proton / runner 目录
3. 在启动器里选 `we-proton-<版本>`。wegame-launcher 默认自动使用最新的 DWProton，需要手动选择：在图形界面的「设置 → 运行器」里选，或者运行

   ```
   wegame-launcher runner we-proton-<版本>
   ```

说明：

- 前缀版本和所基于的 DWProton 版本相同，两者之间切换不会触发前缀升级
- 0005 依赖 Linux 6.7 及以上内核的 userfaultfd 异步写保护；内核更旧时这一项不生效，其他照常
- 构建出的 `d3d12.dll`、`d3d12core.dll` 使用 UCRT（`api-ms-win-crt-*`），官方版本用 `msvcrt.dll`，Wine 两者都提供

## 从源码构建

```
git clone https://github.com/SHORiN-KiWATA/we-proton.git
cd we-proton
git submodule update --init wine Vulkan-Headers
git submodule update --init --recursive vkd3d-proton
we/overlay-build.sh --release <N>             # 输出到 build/
we/overlay-build.sh --release <N> --install   # 同时安装到 ~/.local/share/proton/runners/WE-Proton
```

脚本的详细步骤、测试程序、诊断工具以及跟进上游新版本的方法见 [`we/README.md`](we/README.md)。

完整构建（`make redist`，在容器里从头编译所有组件）和上游相同（本项目没有实际测试过），见 [DWProton](https://dawn.wine/dawn-winery/dwproton) 和 [Proton](https://github.com/ValveSoftware/Proton) 的说明。

## 仓库结构

- `patches/wine/`、`patches/vkd3d-proton/`：补丁。`wine/`、`vkd3d-proton/` 子模块停在上游 commit，构建时自动打上补丁
- `we/fixes/`：每个补丁一份报告（现象、根因、修复、验证）
- `we/tests/`：测试程序和修复前后的输出
- `we/diag/`：诊断补丁和脚本，不进正式构建
- `we/overlay-build.sh`：快速构建脚本
- 其余是 DWProton / Proton 的原有内容

## 源码与许可证

- Proton 顶层内容：BSD-3-Clause（Valve Corporation，见 [`LICENSE`](LICENSE)、[`LICENSE.proton`](LICENSE.proton)）
- vkd3d-proton：上游 vkd3d-proton 加上 `patches/vkd3d-proton/` 里的补丁，LGPL-2.1；其中 dxil-spirv 子项目以及改它的补丁为 MIT
- 其他组件见各自目录下的 `LICENSE`、`COPYING`，以及发布包里的 `LICENSE`、`LICENSE.OFL`、`PATENTS.AV1`
- `we/` 下的脚本和文档沿用 BSD-3-Clause

## 致谢

- Valve 和 CodeWeavers 的 [Proton](https://github.com/ValveSoftware/Proton)
- Dawn Winery 的 [DWProton](https://dawn.wine/dawn-winery/dwproton)
- [Wine](https://www.winehq.org/)、[DXVK](https://github.com/doitsujin/dxvk)、[vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton) 等项目
