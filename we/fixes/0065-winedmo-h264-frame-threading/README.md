# 0065：低延迟模式下 H.264 只用一个核解码，4K 60 帧视频跟不上

补丁：`patches/wine/0065-winedmo-Use-frame-threading-for-H.264-also-for-low-l.patch`（`dlls/winedmo/unix_transform.c`）。

## 现象

《影之刃零》（2560×1440，NVIDIA RTX 4060 笔记本，R9 7940H）里预渲染的过场 CG 全程黑屏，声音和字幕照常，放完回到游戏。主菜单的背景视频正常。

已经打了提前移植的 GE-Proton 补丁 `winedmo-disable-d3d11-aware-for-phantom-blade-zero`（`e8c2a561e`，对这个游戏关掉解码器的 `MF_SA_D3D11_AWARE`）。没有它时同一段 CG 是画面冻在某一帧；有了它，画面不冻了，变成全黑。

## 根因

游戏（Unreal 的 Electra 播放器）创建 H.264 解码器时设了 `MF_LOW_LATENCY=1`。winedmo 在低延迟时只开 libavcodec 的切片多线程（`FF_THREAD_SLICE`），不开帧多线程：

```
opened h264 decoder, threads 12, active thread type 0x2, low latency 1
```

切片多线程只能并行一帧里的多个切片。这段 CG 每帧只有一个切片（日志里 8989 个切片 NAL 对应 8989 帧），所以 12 个线程实际只有一个在解码。

| | 主菜单背景 | 过场 CG |
|---|---|---|
| 尺寸、帧率 | 3840×1600，30 fps | 3840×2160，60 fps |
| 修复前实测解码速度 | 够 30 fps | 开头约 55 fps，十几秒后降到 30 左右；开播 13 秒时只解到 9 秒的位置 |

视频越来越落后于音频，播放器丢掉迟到的帧，画面就一直是黑的。主菜单只要 30 fps，单核够用，所以正常。

单核能力在本机上用 ffmpeg 测过：一段参数接近的 4K 60 fps High Profile、每帧一个切片的测试片，单线程 74 fps，切片多线程（12 线程）73 fps，帧多线程（12 线程）407 fps。游戏运行时 CPU 本来就忙，实际的 CG 又比测试片复杂，所以只剩 30 多 fps。

## Windows 行为

没在 Windows 上实测。微软的 H.264 解码器 MFT 在 `MF_LOW_LATENCY` 下的线程方式没有文档；这次的判断依据是：在 Windows 上同一段 CG 能正常播放，而且游戏的播放器本来就能处理解码器晚几帧出图（解码器出图前一直返回 `MF_E_TRANSFORM_NEED_MORE_INPUT`）。

## 修复

H.264 一律打开 `FF_THREAD_SLICE | FF_THREAD_FRAME`，不再因为低延迟只开切片多线程。帧多线程会让第一帧晚出几帧，调用方本来就要处理 `MF_E_TRANSFORM_NEED_MORE_INPUT`。线程数上限不变（64 位 16、32 位 4）。

## 验证

`we/quick-build.sh --prep --base build/we-proton-ge11-12-32 --name test-0065 dlls/winedmo`，装进游戏，`WINEDEBUG=+timestamp,err+all,+mfplat,+dmo` 下打同一段 CG：

- 解码器报 `active thread type 0x1`（帧多线程生效）
- CG 解码稳定在每秒 60 帧（开头先出 72 帧补足缓冲，之后每秒正好 60，直到片尾）
- 画面正常显示，用户确认

## 排查过程和弯路

1. 一开始以为是 GE-Proton #903 的那个问题（D3D11 视频路径在高分辨率下卡住），提前移植了 GE 的 `MF_SA_D3D11_AWARE` 补丁。补丁生效了（日志有 `HACK: setting MF_SA_D3D11_AWARE to FALSE`），但画面从冻住变成全黑。
2. 看到游戏对每一帧输出缓冲都查询 `IMF2DBuffer`/`IMF2DBuffer2` 并失败，游戏程序里又有「`IMF2DBuffer` 不可用时回退到旧 CPU 路径」的开关 `Electra.Win.H264AutoFallbackToOldOutputPath`，以为黑屏是因为缓冲区没有这个接口。后来发现缓冲区是游戏自己用 `MFCreateMemoryBuffer` 建的，Windows 上也没有这个接口；临时打开那个开关后仍然黑屏；主菜单视频也被同样查询失败，却正常显示。排除。
3. 把主菜单和 CG 放在同一局里对比，才发现差别在解码速度。
4. 曾怀疑显卡侧（vkd3d-proton、NV12 纹理、D3D11/D3D12 共享）：开 `VKD3D_DEBUG=warn` 和 `DXVK_LOG_LEVEL=info` 看过，CG 期间没有相关报错；NVIDIA 的 NV12 格式能力也不比 AMD 少。排除。

## 没验证的

- Windows 上微软解码器在低延迟模式下的线程方式
- 真正低延迟的场景（视频通话、串流）里多几帧延迟的影响
- 不带 GE 的 `MF_SA_D3D11_AWARE` 补丁、只打 0065 时，D3D11 路径下 CG 是否也能正常播放（那条路径的冻结可能是另一个问题）
