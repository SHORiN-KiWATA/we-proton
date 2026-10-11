# 0010：WinHttpAddRequestHeaders 遇到开头的空行就失败

补丁：`patches/wine/0010-winhttp-Skip-empty-lines-in-front-of-the-first-reque.patch`（`dlls/winhttp/request.c`，测试在 `dlls/winhttp/tests/winhttp.c`）。

## 现象

《黑色沙漠》国服客户端（64 位）点「开始游戏」后显示「处理中」，十几秒后崩溃。每次都一样，崩溃报告的转储显示：主线程往 exe 自己的一个只读节写数据，触发访问违例；调用栈里只有游戏自己的代码。

游戏日志（`Log/Client_<时间>.json`，UTF-16）里，崩溃前的顺序每次都相同：

1. 连接认证服务器，成功
2. 为一个身份查询的 HTTPS 请求设置请求头：`WinHttpAddRequestHeaders 이어받기 설정 실패`（设置失败）
3. 处理认证服务器的回包
4. 2～9 秒后崩溃

## 根因

只开 `WINEDEBUG=+winhttp` 运行一次，抓到游戏的调用：

```
WinHttpAddRequestHeaders(request, L"\r\n<名字1>:<值1>\r\n<名字2>:<值2>", 535, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)
warn:winhttp:parse_header invalid character in field name L"\r\n<名字1>:..."
```

游戏一次加两个自定义请求头，整串**以一个空行 `\r\n` 开头**。

Wine 的 `add_request_headers()` 把字符串按行拆开：第一行取的是「从开头到第一个 CR/LF 为止」的部分。开头就是 CR/LF 时，第一行是空的，`parse_header()` 拒绝空行，整个调用返回 `ERROR_WINHTTP_INVALID_HEADER`（12153），后面的头一个也没加。（行与行之间多出来的空行会被拆分循环一起跳过，所以只有开头的空行有问题。）

游戏拿到失败后关掉请求句柄，这个请求没有发出去，随后进入错误分支并崩溃。

## Windows 的行为（实测）

Windows 11 26200，`we/tests/winhttp_crlf_probe`：每个用例新开一个请求句柄，调 `WinHttpAddRequestHeaders`，再用 `WinHttpQueryHeaders(WINHTTP_QUERY_RAW_HEADERS_CRLF | WINHTTP_QUERY_FLAG_REQUEST_HEADERS)` 看实际加上了什么。flag 测了 ADD|REPLACE、ADD 和 0，长度测了 -1 和准确长度，结果都一样。输出见 `winhttp_crlf_probe.windows.txt`。

| 参数 | Windows | Wine（修复前） |
|---|---|---|
| `\r\nX-A:one\r\nX-B:...`（开头有空行，结尾有没有 CRLF 都一样） | 成功，两个头都加上 | 12153，什么都没加 |
| `\r\n\r\nX-A:one`、`\nX-A:one`、`\rX-A:one` | 成功 | 12153 |
| `\r\n`（只有空行） | 成功，什么都不加 | 12153 |
| `X-A:one\r\n\r\nX-B:two`（中间有空行）、`X-A:one\r\n\r\n` | 成功 | 成功 |
| `""`，长度 -1 | 87（`ERROR_INVALID_PARAMETER`） | 成功 |
| ` \r\nX-A:one`（第一行只有空格） | 87 | 12153 |

## 修复

`add_request_headers()` 在逐行解析之前，先跳过开头的 `\r` 和 `\n`；跳完如果已经到了结尾（整串都是空行），就返回成功。`WinHttpAddRequestHeaders()` 和 `WinHttpSendRequest()` 的附加请求头都会经过这个函数。

表里最后两行的差异（空字符串配长度 -1，以及只有空格的第一行）跟这次的问题无关，没有改。

新增的 Wine 测试在 `test_WinHttpAddHeaders()` 末尾：开头是 `\r\n`、`\n`、`\r\n\r\n` 时头都能加上，只有 `\r\n` 时请求头不变。

## 验证

- `winhttp_crlf_probe`：修复后（release 11 只换了 x86_64 的 `winhttp.dll`，`winhttp_crlf_probe.wine11-0010.txt`），所有开头有空行的用例都和 Windows 相同。剩下的差异只有表里最后两行，修复前后一样
- Wine 测试 `winhttp:winhttp`（x86_64）：

  | | 结果 |
  |---|---|
  | Windows 11 26200 | 4443 项，3 失败（第 3662～3664 行，本地服务器那部分，和环境有关）；新加的测试全部通过 |
  | Wine 修复前（release 11） | 4443 项，17 失败：新加的测试 12 项，另外 5 项（3662～3664、6310～6311） |
  | Wine 修复后 | 4443 项，5 失败，和修复前那 5 项完全相同 |
- 游戏：release 11 只换 `winhttp.dll`，在 Steam Linux Runtime 4.0 容器里运行。游戏日志不再出现请求头设置失败，越过了认证环节，可以创建角色、进入游戏。进入游戏后画面是黑的，那是另一个问题（见下面）

## 排查过程和弯路

1. **弯路一**：exe 加了壳，所有节都被改名，被写的那个节在节头里标为只读，于是一开始以为是保护壳在 Wine 下运行时修改内存权限没成功。细看转储后排除了：崩溃线程是主线程，调用栈上只有游戏自己的代码（夹着 `msvcp140`、`dxgi`）。被写的地址里是「函数指针 + 数据指针」对，紧挨着 MSVC 正则库的元字符常量，是普通的只读常量，更像是坏指针碰巧指了过去
2. 游戏日志里，两次崩溃前都有同一条 `WinHttpAddRequestHeaders` 失败
3. **弯路二**：先不运行游戏，猜参数做离线对照：
   - `we/tests/winhttp_hdr_probe`：`Range` 类请求头 × 各种长度（-1、准确长度、多算一个 NUL、误用字节数、0）× 各种 flag × 是否已有同名头
   - `we/tests/winhttp_state_probe`：发送前、收到响应后、读完数据后、发送失败后、异步请求进行中，在这些状态下加请求头

   两组都没有出现「Windows 成功、Wine 失败」，猜不中游戏的参数。顺带发现的差异方向都是 Windows 更严格：长度里含 NUL 时 Windows 返回 87、Wine 接受；有些情况下两边错误码不同；COALESCE 加空值时 Windows 不加这个头、Wine 会加。这些和本问题无关，没有改，输出见 `winhttp_hdr_probe.*.txt`
4. 只开 `+winhttp` 运行一次游戏，抓到实际参数（开头有空行），按同样的结构（内容换成假的）写了 `winhttp_crlf_probe`，Windows 接受、Wine 拒绝。抓到的参数里含登录凭据，日志副本已打码，报告里也不写出原文

## 没有验证的部分

- 没有测 `WinHttpSendRequest()` 的附加请求头开头带空行时 Windows 的行为。补丁让它和 `WinHttpAddRequestHeaders()` 走同一套逻辑
- Wine 测试只跑了 x86_64；游戏只验证了 x86_64 的 `winhttp.dll`

## 另外看到的问题（不是这次修的）

进入游戏后画面全黑，但有背景音乐、能操作角色，屏幕左侧只看得到平台游戏内工具栏的收起按钮。正在排查，怀疑是这个工具栏叠加在游戏画面上时，背景没有画成透明。
