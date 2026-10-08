# 0014：线程关掉 IME 后，宿主输入法提交的文字被丢掉

补丁：`patches/wine/0014-win32u-Deliver-input-method-text-to-threads-with-the.patch`（`dlls/win32u/imm.c`）。

## 现象

游戏平台客户端（32 位）的搜索栏打不了中文，英文可以。输入法是 fcitx5 + rime：切到中文打拼音，rime 的候选框正常出现，但选字（空格或数字键）之后什么也没上屏。

同样在 Steam Linux Runtime 4.0 里经 Proton 运行的 CEF 109 示例浏览器（官方构建的 cefclient），不论窗口模式还是离屏渲染模式，都能正常打中文。

## 根因

客户端在自己的界面线程上关掉了 IMM 输入法。导入表和反汇编显示：

- 主程序调用 `ImmDisableIME(GetCurrentThreadId())`
- CEF 宿主模块在一个对象的构造函数里调用 `ImmDisableIME(0)`

它内置的 Chromium 109 在 Windows 8 以上默认用 TSF 接收输入法的文字：`TSFImeSupport` 默认开启，创建的是 `InputMethodWinTSF`。这个类在 IMM 消息里只处理 `WM_CHAR`、`WM_SYSCHAR` 和 `WM_IME_REQUEST`。

Wine 的 `NtUserDisableThreadIme` 会给线程设上 `disable_ime`，并销毁线程的默认 IME 窗口。

宿主输入法（XIM）提交的文字在 Wine 里是这样送到程序的：

1. winex11 的 `xim_set_result_string()` 调 win32u 的 `post_ime_update()`
2. `post_ime_update()` 记下这次更新，给窗口 post 一个 `WM_WINE_IME_NOTIFY`
3. 窗口线程处理这个消息时，找到线程的默认 IME 窗口，转发成 `WM_IME_NOTIFY`
4. imm32 的 IME 据此生成 `WM_IME_COMPOSITION` 等消息
5. `DefWindowProc` 把结果字符串转成 `WM_IME_CHAR`，再转成 `WM_CHAR`

线程没有默认 IME 窗口时，第 3 步直接返回（`win32u/message.c`），这次更新就丢了。候选框是 fcitx5 自己画的，所以显示正常，但选中的字再也到不了程序。

## Windows 的行为

`ImmDisableIME` 关的是 IMM：线程的窗口没有默认 IME 窗口，也收不到 IMM 消息。TSF 不受影响。程序如果自己激活了 TSF 线程管理器、实现了文本存储（Chromium 的 TSF 模式就是这样），照样能从输入法拿到文字。这个客户端在 Windows 上能打中文，靠的就是 TSF。

这一点没有在 Windows 上实测，因为要在交互会话里真的用输入法打字。依据是：

- Chromium 109 的源码：`ui/base/ime/init/input_method_factory.cc` 在 Windows 8 以上选 `InputMethodWinTSF`；`ui/base/ime/win/input_method_win_tsf.cc` 只处理上面那三个消息
- 客户端在 Windows 上能打中文

## 修复

在 `post_ime_update()` 开头加一个判断：进程的 IME 被禁用（`ImmDisableIME(-1)`），或者当前线程的 IME 被禁用时，不再记录这次更新，而是：

- 把结果字符串逐个字符以 `WM_IME_CHAR` post 给当前的焦点窗口；没有焦点窗口时，发给 XIM 报告的窗口
- 组合中的字符串（预编辑）忽略

`DefWindowProc` 会把 `WM_IME_CHAR` 转成 `WM_CHAR`。不认 IME 的程序和 Chromium 的 TSF 模式都处理 `WM_CHAR`。Wine 没有 TSF 的输入法服务，在 Windows 上这类线程是从 TSF 拿到同样的文字，这是 Wine 能给出的最接近的结果。

## 验证

### 探测程序

`we/tests/ime_edit_probe` 配合 `ime_type.sh` 运行。脚本起一套私有环境：Xvfb、会话总线、fcitx5（XIM 前端，rime 的 luna_pinyin_simp），然后用 xdotool 依次输入 `ctrl+space`、`nihao`、空格、`shijie`、空格。

修复前（release 13，`ime_edit_probe.wine13.txt`）：

- 正常线程拿到「你好世界」
- 调过 `ImmDisableIME(0)` 的线程什么也没收到，和客户端的现象一样

修复后（release 14，`ime_edit_probe.wine14.txt`）：

- 正常线程的消息序列和修复前逐行相同
- 禁用 IME 的线程收到 `WM_IME_CHAR`，再收到 `WM_CHAR`，输入框里是「你好世界」

### Wine 自己的测试

修复前后（release 13 / 14），x86_64，Xvfb：

| 测试 | 结果 |
|---|---|
| `imm32:imm32` | 5538 项，0 失败，前后相同 |

### 实机

niri + xwayland-satellite，fcitx5 + rime，release 14 在 Steam Linux Runtime 4.0 里运行。那个客户端的搜索栏能打中文，选字后正常上屏。重启客户端后再试一次，结果相同。

## 没有验证的部分

- Windows 上 `ImmDisableIME` 之后，TSF 程序照样能收到文字，这一点没有实测
- 禁用 IME 的线程里看不到预编辑（正在组合的拼音），只有 fcitx5 自己的候选框；结果字符串在提交时一次送到
- 32 位程序走的是同一份 `win32u.so`（WoW64），但探测程序只编了 64 位

## 排查过程

1. 用户反馈搜索栏英文能打、中文不行。fcitx5 的 `DebugInfo` 显示，客户端的 XIM 输入上下文拿到了焦点，rime 也处于激活状态，说明按键到了输入法
2. 客户端的 libcef 是 CEF 109，里面有 `TSFImeSupport`。最初猜测是 Chromium 走 TSF，拿不到字
3. 在 Xvfb 里搭了私有的 fcitx5 + rime，用 xdotool 打字：
   - Wine 的普通输入框能上屏
   - CEF 109 的 cefclient（经 Proton，在 steamrt4 里）窗口模式和离屏渲染模式都能上屏

   所以问题和 Chromium 本身无关：Wine 的 `DefWindowProc` 会把 IMM 的结果转成 `WM_CHAR`，TSF 模式的 Chromium 收得到
4. 用户在桌面上对照：cefclient 正常；客户端有候选框，选字后不上屏。问题在客户端自己
5. 扫描客户端所有模块的导入表：主程序、守护进程和 CEF 宿主模块都导入了 `ImmDisableIME`。反汇编看到传的是当前线程
6. 测试程序加上 `ImmDisableIME(0)` 后复现。`+imm,+xim` 日志里能看到 `xim_set_result_string` 和 `post_ime_update`，之后就没有下文了
7. 第一版补丁把 `WM_IME_CHAR` 发给了 XIM 报告的顶层窗口，字落在顶层窗口上，而不是有焦点的输入框。改成发给焦点窗口
