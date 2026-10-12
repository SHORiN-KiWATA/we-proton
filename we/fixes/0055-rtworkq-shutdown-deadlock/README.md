# 0055：释放读取器后马上 MFShutdown 会卡死

补丁：`patches/wine/0055-rtworkq-Don-t-hold-the-queue-list-lock-while-shuttin.patch`（`dlls/rtworkq/queue.c`）。

## 现象

从 `we-proton-ge11-7-25` 起（换成 GE-Proton 底座）：程序用 `MFCreateSourceReaderFromURL` 读视频，释放读取器后马上调用 `MFShutdown`，经常卡住不返回。视频测试（`scratchpad` 里的 `mf_probe`，H.264+AAC、VP9+Opus、MPEG、AV1 各一份，64 位和 32 位）一轮 12 个里卡 7–10 个；WMV 不卡；释放后等 2 秒再调就不卡；旧的 DW-Proton 底座一个也不卡。

游戏里的表现会是：放完片头或过场视频、或者退出时，程序停在那里。还没在具体游戏里碰到，是验证视频解码时查出来的。

## 根因

卡住时的调用栈（`winedbg bt all`，32 位，用 PE 的 COFF 符号表解析）：

```
主线程：MFShutdown → RtwqShutdown → shutdown_system_queues → shutdown_queue
        → pool_queue_shutdown → CloseThreadpoolCleanupGroupMembers      （拿着 queues_section）
线程池：serial_queue_finalization_callback → work_item_Release → async_result_Release
        → source_reader_stream_events_callback_Release → source_reader_release
        → RtwqUnlockWorkQueue → unlock_user_queue → EnterCriticalSection(queues_section)
```

Wine 日志里同时有 `queues_section wait timed out in thread 0100, blocked by 00e8`。

- `shutdown_system_queues()` 拿着 `queues_section` 关闭系统队列，关闭线程池队列要等还在跑的回调结束
- 读取器被释放时会关掉媒体源，还没完成的事件回调带着 `MF_E_SHUTDOWN` 投到读取器的工作队列。GE-Proton 的视频补丁（`ge-video-rework/0002`）把读取器的队列改成以多线程系统队列为目标的串行队列，并把 `MFUnlockWorkQueue` 挪到读取器最后一次释放里；`0056` 修了上游串行工作项少释放一次的泄漏。两者叠加后，读取器的最后一个引用在多线程系统队列的回调里释放，这时要拿 `queues_section` 去解锁读取器的队列
- 主线程正拿着这把锁等这个回调结束，于是互相等死

WMV 不卡：GE-Proton 让 WM 的音视频走共享的多线程用户队列，`MFShutdown` 不等那个线程池。旧底座不卡：读取器在那里一直泄漏，根本走不到这次释放。

隐患是上游 Wine 的：上游的 `shutdown_system_queues()` 一模一样。必现程序 `we/tests/mf_shutdown_probe.c`（不经过读取器：在系统队列的回调里调 `MFUnlockWorkQueue`，主线程同时调 `MFShutdown`）在旧的 DW 底座 `we-proton-11.0-14-25` 上也卡。GE-Proton 主线目前也没修；它的 `0003` 已经把 `unlock_user_queue()` 里的 `shutdown_queue` 挪到锁外，但漏了 `shutdown_system_queues()`。

## Windows 行为

没在 Windows 上实测。按 Media Foundation 的文档，读取器释放后就可以调 `MFShutdown`，不会卡。上游 rtworkq 测试（`test_queue_shutdown`）的注释说，Windows 的 `RtwqShutdown` 不等已经排队的工作项，它们在关闭之后照样执行；Wine 会等，这次没改这一点。

## 修复

- `shutdown_system_queues()` 等系统队列的回调时不再拿 `queues_section`
- 新加 `startup_section`，只有系统队列的初始化和关闭会拿，回调从来不拿，用它继续保证 `RtwqStartup` 和 `RtwqShutdown` 不同时进行
- 新加 `system_queues_shutdown` 标志：关闭开始后，`grab_queue()` 不再懒创建系统队列，返回 `RTWQ_E_SHUTDOWN`

顺带去掉了一处加锁顺序相反的问题：串行队列的完成回调先拿 `queue->cs` 再拿 `queues_section`，原来的关闭路径正好反过来。

没有选择撤掉 GE-Proton 的 `0056`：那会让每个读取器泄漏它的串行队列和线程池。

## 验证

`we/quick-build.sh --prep --base build/we-proton-ge11-7-27 --name test-0055 dlls/rtworkq`（只换 `rtworkq.dll`，64 位和 32 位）。

| | we-proton-ge11-7-27 | 打 0055 后 |
|---|---|---|
| `mf_shutdown_probe.exe --queue standard` | 卡死（看门狗 10 秒） | `RESULT ok` |
| `mf_shutdown_probe.exe --queue mt` | 卡死 | `RESULT ok` |
| `mf_shutdown_probe32.exe --queue standard` | 卡死 | `RESULT ok` |
| `mf_shutdown_probe32.exe --queue mt` | 卡死 | `RESULT ok` |

视频测试（同上的 `mf_probe`/`ds_probe` 全套，24 个）连跑三轮：`MFShutdown` 卡死 0 次（ge11-7-27 一轮 8 次），每轮 14 个 Media Foundation 测试都正常关闭；剩下失败的只有 MPEG-1/2，是 Media Foundation 里本来就没有这个解码器。WE-Proton 的回归探测和 ge11-7-27 相同。

## 没验证的

- Windows 上的行为（子代理写了一个 rtworkq 一致性测试，没在 Windows 上跑过，所以没有放进补丁）
- 具体的游戏

## 风险

- 读取器和媒体源现在会在 `MFShutdown` 期间、在线程池线程上真正析构（以前要么泄漏要么卡死）。它们的析构路径里如果还有别的等待，可能暴露新的问题
- 关闭开始后，懒创建系统队列返回 `RTWQ_E_SHUTDOWN`。以前这种情况要么卡死，要么在关闭后又建出一个泄漏的队列
- 在工作队列的回调里调 `MFStartup` 或 `MFShutdown` 仍会卡死，和以前一样，这本身也违反文档的要求

## 排查过程

先是视频测试发现关闭时偶尔卡住，等 2 秒再关就不卡；第一个判断是「解码线程在等锁」。抓调用栈后发现另一方其实是读取器推迟到工作队列线程上的最后一次释放；返回地址正好落在 GE-Proton `0056` 新加的那次 `IUnknown_Release` 后面。再写不依赖读取器的必现程序，确认问题在 rtworkq 本身，旧底座也有。
