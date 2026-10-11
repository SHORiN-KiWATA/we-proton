# 0004：UDP socket 的 connect() 语义和 Windows 不同，select() 唤醒失效

补丁：`patches/wine/0004-server-Follow-Windows-connect-semantics-for-datagram.patch`（`server/sock.c`，以及 `dlls/ws2_32/tests/afd.c` 里一处 `todo_wine`）。

## 现象

一个由多个进程组成的客户端，启动时后端要通过本机 WebSocket 发上百个请求，每个请求都恰好花整秒数（1.00、2.00、5.02 秒……）。118 个插件初始化要三分半钟，前端等 2 分钟就放弃退出。几个网络线程同时在空转：perf 显示一个主线程 70% 的时间在 `QueryPerformanceCounter`，另一个线程 33 秒里调了 180 万次 `WSAPoll`。

## 根因

几个进程的网络线程都用 `select()` 等待，1 秒超时，`exceptfds` 里只放一个 UDP socket（wineserver 看到的事件掩码是 `AFD_POLL_OOB | AFD_POLL_CONNECT_ERR`，即 `0x102`）。别的线程要唤醒它时，就对这个 UDP socket 执行 `connect(0.0.0.0:0)`。

Windows 11 26200 实测：

| | Windows | Wine 原来 |
|---|---|---|
| 没有对端的 UDP socket 连 `0.0.0.0` / `::` | 失败（`WSAEADDRNOTAVAIL`，10049），之后 poll 一直报告 `AFD_POLL_CONNECT_ERR`（状态 `STATUS_INVALID_ADDRESS_COMPONENT`），直到下一次成功的 connect | 成功，没有任何事件 |
| 等在 `exceptfds` 上的 `select` | 立即返回 | 等满超时 |
| 已连接的 UDP socket 连全零地址 | 断开 | 成功 |
| 已连接的 UDP socket 的 poll | 一直报告 `AFD_POLL_CONNECT` | 不报告 |
| 上面这些情况下的 `SO_ERROR` | 0 | 0 |

Linux 上 `connect(0.0.0.0)` 会成功，于是每次唤醒都落空，线程只能等 1 秒超时才处理下一个请求。那些空转是程序自己的写法，它们只是在等迟到的回复。

## 修复

数据报 socket 连全零地址时：已连接的就断开（`connect(AF_UNSPEC)`）；没连接的就记下 `CONNECT_ERR`（`EADDRNOTAVAIL`），完成等这个事件的 poll，返回 `STATUS_INVALID_ADDRESS_COMPONENT`。成功的数据报 connect 清掉这个错误，并完成等 `AFD_POLL_CONNECT` 的 poll。已连接的数据报 socket 报告 `AFD_POLL_CONNECT`。`SO_ERROR` 不受 `CONNECT_ERR` 影响。顺带让 Wine 自己 `afd.c` 测试里对应的 `todo_wine` 通过了。

## 验证

- `we/tests/udp_wakeup_probe`、`udp_connect_err_probe`、`udp_connect_err_probe2`、`udp_connect_poll`：`*.windows.txt` 是 Windows 实测输出，`*.wine3.txt` 是修复后的输出。除了向关闭端口 `sendto` 后 Windows 因为 ICMP 多报一次 `READ` 以外，返回值、错误码、事件逐行一致
- 实机：后端 20 秒初始化完成（原来三分多钟），前端第一次尝试就连上

## 排查过程

1. 打开 Wine 日志对照：日志开销不是原因，不开日志一样慢。
2. perf：主线程在空转等待，把 QPC 做快没用。另外测了单次 QPC 的耗时（测试程序没有保留），时钟源是 TSC，都正常。
3. 只跟踪其中一个进程的 syscall（`we/diag/run-syscall-trace.sh`，诊断补丁 `we/diag/0001` 让 `+syscall` 跳过 QPC/yield/查询时间）：本地 HTTP 服务端被唤醒很快，「每秒推进一次」看起来发生在另一个进程里。这个结论片面，第 5 步才发现两边的网络线程是同一个问题。
4. 怀疑 Wine 漏发 `WSAEventSelect` 事件：`we/diag/eventselect_rtt.c` 测往返时间，正常，排除。
5. 诊断版 wineserver（`we/diag/0002-server-log-afd-poll.patch`，`we/diag/run-poll-trace.sh`）记录每个可能阻塞的 poll 和每次数据报 connect：等的是 `0x102`，唤醒方式是 `connect(0.0.0.0:0)`，一轮里 1400 多次，每次都有一个 select 在等。
6. 文档一处说全零地址非法，一处说对 UDP 是断开。在 Windows 上实测（第 2 节的表），按结果实现。

## 弯路

第一版 0004 只在 UDP connect 成功时完成等 `AFD_POLL_CONNECT` 的 poll：Wine 的测试刚好说明 UDP connect 会触发这个事件，就假设程序等的也是它，没有验证。实机毫无变化。之后的规矩：先用诊断日志看程序实际等什么，「Windows 到底怎么做」先在 Windows 上实测，再写补丁。
