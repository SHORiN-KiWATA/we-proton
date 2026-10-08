# 0012：WoW64 下视频采集的媒体类型带着未初始化的 pUnk

补丁：`patches/wine/0012-qcap-Clear-pUnk-in-media-types-returned-to-32-bit-ca.patch`（`dlls/qcap/v4l.c`，测试在 `dlls/qcap/tests/videocapture.c`）。

## 现象

《三国杀十周年》（`Sgsc10th.exe`，32 位，界面是 CEF 87.1.14 / Chromium 87.0.4280.141）启动后窗口出来，随即闪退。每次都一样。

平台客户端的崩溃上报目录里有这次闪退的转储和 `crashrpt.json`：

- `EXCEPTION_ACCESS_VIOLATION`，读地址 `0x50545450`，位置 `libcef.dll+0x36ead64`，崩溃线程不是主线程
- 进程里加载了 `devenum`、`qcap`、`quartz`、`msdmo`、`avicap32`、`mfplat`/`mf`/`mfreadwrite`：Chromium 正在枚举摄像头

机器上接着一个 USB UVC 摄像头（`/dev/video0`）。

## 根因

反汇编崩溃点（`libcef.dll` 不带符号，按结构偏移认）：

```
libcef+0x36ead3c:            ; 参数是 AM_MEDIA_TYPE *mt（32 位布局）
  cmp  [esi+0x40], 0         ; mt->cbFormat
  je   1f
  push [esi+0x44]            ; mt->pbFormat
  call [CoTaskMemFree]
  ...
1:mov  eax, [esi+0x3c]       ; mt->pUnk
  test eax, eax
  je   2f
  mov  ecx, [eax]            ; pUnk->lpVtbl
  push eax
  call [ecx+8]               ; IUnknown::Release   <-- 崩在这里
```

这就是 `DeleteMediaType()`／`FreeMediaType()` 释放 `pUnk` 的那一步。上一层是一个循环：每次迭代对一个媒体类型调这个函数——Chromium 的 DirectShow 采集代码（按 Chromium 87 的源码）对每个设备循环 `IAMStreamConfig::GetStreamCaps(i, &mt, ...)`，用完就 `DeleteMediaType(mt)`。崩溃时 `pUnk` 是一个堆地址，那里的内容是一段字符串（`"HTTP..."`），被当成虚表读，于是 `[ecx+8]` = `0x50545450`。

Wine 的 qcap 在 PE 侧用 `CoTaskMemAlloc` 分配 `AM_MEDIA_TYPE`（不清零），再调 unix 侧的 v4l 代码填内容：

- 64 位调用方：`v4l_device_get_caps()` 等直接整个结构赋值 `*params->mt = caps->media_type`，`pUnk` 是 `fill_caps()` 设的 NULL
- 32 位调用方（WoW64）：走 `wow64_v4l_device_get_caps()`／`_get_format()`／`_get_media_type()`，在 64 位的临时结构里拿到结果后用 `put_media_type()` 转成 32 位布局。`put_media_type()` 只写了 GUID、标志和 `lSampleSize`，**没写 `pUnk`**（`cbFormat`/`pbFormat` 由 PE 侧随后写）

所以 32 位进程拿到的 `pUnk` 是这块堆内存原来的内容。`IAMStreamConfig::GetFormat`、`GetStreamCaps` 和输出引脚的 `IEnumMediaTypes::Next` 三条路径都受影响。反方向的 `get_media_type()`（32 位转 64 位）是会把 `pUnk` 设成 NULL 的，只有这一个方向漏了。上游 Wine master（2026-10-08）这段代码相同。

只有 WoW64 模式会走这里：非 WoW64 的 32 位进程用的是 `i386-unix/qcap.so`，直接整个结构赋值。我们用的启动器默认开 WoW64（`PROTON_USE_WOW64=1`）。另外，宿主机没有 32 位 `libv4l2` 时，非 WoW64 模式下 UVC 摄像头根本打不开（不支持 `read()`，要靠 libv4l2 模拟）；Steam Linux Runtime 4.0 里两个架构的 `libv4l2.so.0` 都有。

## Windows 的行为

没有实测：gaming 虚拟机没有直通摄像头，Windows 上没有视频采集设备可测。依据是：

- DirectShow 文档对 `AM_MEDIA_TYPE::pUnk` 的说明是「Not used. Set to NULL.」，SDK 的 `DeleteMediaType()`／`FreeMediaType()` 会释放非 NULL 的 `pUnk`，所以调用方拿到的媒体类型里 `pUnk` 必须是 NULL 或有效指针
- Wine 自己的 64 位路径和非 WoW64 的 32 位路径都返回 NULL

## 修复

`put_media_type()` 里加上 `mt32->pUnk = 0`。unix 侧的媒体类型里 `pUnk` 总是 NULL，32 位调用方也没法拿到 64 位指针，所以直接写 0。只改 `x86_64-unix/qcap.so`。

`dlls/qcap/tests/videocapture.c` 的 `test_stream_config()` 在 `GetFormat`、`GetStreamCaps` 和 `IEnumMediaTypes::Next` 之后各加一条 `ok(!pUnk)`。只有接着视频采集设备时这个测试才会跑到这里。

## 验证

`we/tests/capture_mt_probe`：照程序的做法枚举 `CLSID_VideoInputDeviceCategory`，绑定采集过滤器，对输出引脚调 `GetStreamCaps`（每个索引）、`GetFormat`、`IEnumMediaTypes::Next`，打印 `pUnk`。每次调用前先把一批 `sizeof(AM_MEDIA_TYPE)` 的 `CoTaskMemAlloc` 块填成 `0xcc` 再释放，让没写的字段显出来。`--delete` 改用 `DeleteMediaType()` 的方式释放。测试前缀，`WINEARCH=wow64`，release 11 和 release 11 + 0012：

| | 32 位 | 32 位 `--delete` | 64 位 |
|---|---|---|---|
| release 11 | 6 个 `GetStreamCaps` 和 `GetFormat` 的 `pUnk` 都是 `CCCCCCCC`；随后 `IPin::EnumMediaTypes` 在 qcap 自己的 `FreeMediaType()` 里崩溃（见下） | 第一个 `GetStreamCaps` 之后就崩：`page fault on read access to 0xcccccccc` | 全是 NULL |
| release 11 + 0012 | 全是 NULL | 正常跑完 | 全是 NULL |

（`capture_mt_probe.wine11.txt`、`capture_mt_probe.wine11-0012.txt`。不设 `WINEARCH=wow64` 时，32 位探测程序在宿主机上停在打开设备那一步，见上面「根因」最后一段。）

不经过调用方，Wine 自己也会崩：strmbase 创建 `IEnumMediaTypes` 时，把引脚的媒体类型逐个取到栈上的 `AM_MEDIA_TYPE` 里数一遍，再 `FreeMediaType()`；WoW64 下栈上的 `pUnk` 没被写过，这次是 `0x58`，在 `qcap+0x4af7`（`FreeMediaType`）读 `0x00000058` 崩溃。所以 32 位程序在 WoW64 下对视频采集引脚调 `EnumMediaTypes` 就会崩，哪怕它自己不释放任何东西。

Wine 自己的测试（`dlls/qcap/tests`，含新加的三条检查，i386 和 x86_64 各编一份），测试前缀，`WINEARCH=wow64`，接着摄像头，release 11 / release 11 + 0012：

| 测试 | release 11 | release 11 + 0012 |
|---|---|---|
| `qcap:videocapture`（i386） | 测到第一个设备就崩溃，没有输出汇总 | 363 项，0 失败 |
| `qcap:videocapture`（x86_64） | 362 项，0 失败 | 相同 |
| `qcap:avico`、`avimux`、`capturegraph`、`filewriter`、`qcap`、`smartteefilter`（两个架构） | 0 失败 | 相同 |
| `qcap:inftee`（两个架构） | 23 项失败 | 相同的 23 项 |
| `qcap:audiorecord`（两个架构） | 120 秒超时 | 相同 |

`inftee` 和 `audiorecord` 的问题修复前后一样，和这次的改动无关，没有查。

原来的程序：用 release 12（0001～0011）加 0012 的 `qcap.so`，在 Steam Linux Runtime 4.0 里、WoW64 模式运行，启动后不再闪退，能正常进游戏（2026-10-08，用户实测）。没有产生新的崩溃上报。

## 没有验证的部分

- Windows 上没有实测（没有摄像头可用），见上面「Windows 的行为」
- 只测了一种摄像头（USB UVC，`libv4l2` 转成 RGB24 的 6 种分辨率）
- 非 WoW64 模式（`i386-unix/qcap.so`，在运行时里有 32 位 `libv4l2`）没测；那条路径不经过 `put_media_type()`

## 排查过程

1. 游戏目录下的日志是加密的，看不出东西。平台客户端的崩溃上报目录里有这次闪退的转储和 `crashrpt.json`，崩在 `libcef.dll`
2. 自己写了一个最小的转储解析脚本（线程、模块、异常上下文），用 capstone 反汇编崩溃点和上面几层返回地址：被释放的结构在 +0x3c/+0x40/+0x44 处正好是 `AM_MEDIA_TYPE` 的 `pUnk`/`cbFormat`/`pbFormat`，再加上进程里加载的 DirectShow 模块，确定是摄像头枚举
3. 看 Wine 的 qcap：`AMStreamConfig_GetStreamCaps()` 不清零分配的结构，64 位路径整个赋值，WoW64 路径的 `put_media_type()` 漏了 `pUnk`
4. 第一次跑探测程序时 32 位版本在 `BindToObject` 就失败了（`Reading from /dev/video0 requires libv4l2, but it could not be loaded`）：测试前缀没有用 WoW64 模式，32 位进程加载的是 `i386-unix/qcap.so`，宿主机没有 32 位 libv4l2。查了启动器运行时的环境变量，它设了 `PROTON_USE_WOW64=1`，加上 `WINEARCH=wow64` 后复现
