# 0008：类型库封送把没有方向的参数只当输入

补丁：`patches/wine/0008-rpcrt4-Marshal-type-library-parameters-without-a-dir.patch`（`dlls/rpcrt4/ndr_typelib.c`）。

## 现象

《最终幻想14》国服客户端（64 位）点「开始游戏」后报账号认证错误（5003）。它取登录结果的路径是：

1. 加载一个 64 位的小入口 DLL，入口 DLL 用 `CoCreateInstance(CLSCTX_LOCAL_SERVER)` 创建一个 32 位进程外 COM 服务器（ATL EXE）的对象
2. 接口是双接口，注册的 `ProxyStubClsid32` 是 `{00020424-0000-0000-C000-000000000046}`，也就是按类型库封送
3. 入口 DLL 调 `Initialize(long appid, long areaid, long *ret)`，`ret` 的初值是 -1。入口 DLL 不看 HRESULT，只把 `ret` 原样返回给程序；程序看到非 0 就当作初始化失败，调用终止接口，报错
4. 初始化成功后，程序再调 `GetModule(BSTR *, BSTR *)`，从这两个参数里拿回两个字符串

在 Wine 上，调用本身成功（HRESULT 是 `S_OK`，服务端的方法确实执行了，它自己的日志里初始化也是成功的），但客户端拿到的 `ret` 仍然是 -1。同一接口里一个只做 `*ret = a + b` 的方法也一样：`Add(2, 3, &ret)` 返回 `S_OK`，`ret` 还是 -1。客户端是 32 位时结果相同，所以和跨位数无关。

## 根因

这个服务器的类型库里，这几个方法的参数 `wParamFlags` 都是 0，既没有 `PARAMFLAG_FIN` 也没有 `PARAMFLAG_FOUT`（用 `ICreateTypeInfo` 生成、或者老的 ODL 编译器编出来的类型库会这样；MIDL/widl 总会写上方向）：

```
Add            num1 VT_I4 flags 0, num2 VT_I4 flags 0, bRet VT_PTR→VT_I4 flags 0
Initialize     Appid VT_I4 flags 0, Areaid VT_I4 flags 0, bRet VT_PTR→VT_I4 flags 0
GetModule      p1 VT_PTR→VT_BSTR flags 0, p2 VT_PTR→VT_BSTR flags 0
```

Wine 的类型库封送在 rpcrt4 里按类型库现场生成 NDR 格式串，`write_param_fs()` 这样决定方向：

```c
is_out = param_flags & PARAMFLAG_FOUT;
is_in = (param_flags & PARAMFLAG_FIN) || (!is_out && !is_return);
```

没有方向的参数只当 `[in]`：值会传给服务端，服务端写回的值不会发回客户端。代理和存根两端都按这个格式串工作，所以调用本身不报错，只是结果丢了。上游 Wine master（2026-10-08）这段代码相同。

## Windows 的行为（实测）

`we/tests/typelib_noflags_probe`：用 `ICreateTypeLib2` 生成一个参数不带方向的类型库（`TYPEFLAG_FOLEAUTOMATION`，按类型库封送），按当前用户注册，把对象封送到另一个套间，经类型库代理调用，打印服务端进入时看到的值和客户端拿回的值。Windows 11 26200 上 64 位和 32 位的结果相同（`typelib_noflags_probe.windows.txt`）：

| 参数（都没有方向） | 服务端看到 | 客户端拿回 |
|---|---|---|
| `long *`，传 -1，服务端写 105 | -1 | 105 |
| `long *`，传 NULL | 服务端没被调用 | `RPC_X_NULL_REF_POINTER`（0x800706f4） |
| `BSTR *`，传 `"client"` / NULL，服务端写 `"server"` | `"client"` / NULL | `"server"` |
| 两个 `BSTR *`，都传 NULL | NULL、NULL | 服务端写的两个字符串 |
| `VARIANT *`，传 `VT_I4 -1`，服务端改成 42 | `VT_I4 -1` | `VT_I4 42` |
| `IUnknown **`，传 NULL / 一个对象，服务端换成自己的对象 | NULL / 对象 | 服务端的对象 |
| `IUnknown *`（`VT_UNKNOWN`）、指向接口类型的 `VT_PTR`（按值传的接口指针） | 对象 | （没有可回传的） |
| 对照：`[in] long *`、`[out] long *` | -1 / 0 | -1 / 7 |

也就是说：没有方向的参数按 `[in, out]` 封送，指针按 ref 指针处理（不能是 NULL）；按值传的参数和按值传的接口指针照常只是输入。在 Windows 上，原来那个 32 位服务器的 `Add(2, 3, &ret)` 也实测过：64 位、32 位客户端都拿回 5。

## 修复

`write_param_fs()` 里，参数既没有 `PARAMFLAG_FIN` 也没有 `PARAMFLAG_FOUT`（且不是返回值）时，如果它是被调方能写入的指针——`VT_PTR` 且不指向接口类型，别名展开后再判断——就同时设 `is_out`。其他情况不变。新增的 `type_is_ref_pointer()` 复用已有的 `type_pointer_is_iface()`。

格式串由代理和存根两端各自生成，所以 64 位和 32 位的 `rpcrt4.dll` 都要换。为此 `we/overlay-build.sh` 改成配置 `--enable-archs=i386,x86_64`（配置参数变了会重新配置构建目录），`TARGETS` 支持一个源码目录对应多个产物，`dlls/rpcrt4` 对应 `x86_64-windows/rpcrt4.dll` 和 `i386-windows/rpcrt4.dll`。

## 验证

- `typelib_noflags_probe`：修复前（release 8，`typelib_noflags_probe.wine8.txt`）所有没有方向的指针参数都拿不回值；修复后（release 9，`typelib_noflags_probe.wine9.txt`）和 Windows 的输出逐行相同，64 位、32 位都是
- 原来那个 32 位服务器，在测试前缀里（不涉及任何账号）：
  - `Add(2, 3, &ret)`：修复前 64 位、32 位客户端都是 `ret -1`，修复后都是 5
  - 一个按程序的调用顺序走一遍入口 DLL 的测试客户端：修复前 `Initialize` 返回 -1，服务端紧接着被终止；修复后返回 0，`GetModule` 正常返回（测试前缀里没有登录，两个字符串是空的），服务端的日志里没有错误
- 原来的程序（release 9，之后 release 10）：不再报认证错误，拿到的结果被服务器接受，能继续往下走。再往下卡住的那一步是另一个问题，和 Wine 无关，见下面「另外看到的问题」
- release 10 的 Wine 改在 Steam Linux Runtime 4.0 的 SDK 镜像里编，在运行时容器里重跑 `typelib_noflags_probe`（64 位、32 位）：和 Windows 逐行相同
- Wine 自己的测试，修复前后（release 8 / 9），x86_64，Xvfb：

  | 测试 | 结果 |
  |---|---|
  | `oleaut32:tmarshal` | 754 项，0 失败，前后相同 |
  | `oleaut32:typelib` | 7690 项，0 失败，前后相同 |
  | `rpcrt4:cstub` | 213 项，0 失败，前后相同 |
  | `rpcrt4:ndr_marshall` | 31859 项，0 失败，前后相同 |

  `tmarshal` 的接口都由 widl 生成、参数都有方向，走到的是同一段代码但不覆盖这次改的分支；这次改的分支只由 `typelib_noflags_probe` 覆盖

## 没有验证的部分

- 没有方向的数组（`VT_CARRAY`、`VT_SAFEARRAY *`）、结构体指针、枚举指针在 Windows 上怎么处理没有实测，补丁按「能写入的指针就是 `[in, out]`」一并处理
- Wine 的测试只跑了 x86_64

## 排查过程

1. 程序自己的日志：登录组件里负责和登录模块通信的那一侧，刚初始化完（十几毫秒内）就收到了「关闭」，等待中的消息循环拿到 `WM_QUIT`，于是记成超时、放弃。开始以为是 Wine 的问题让 `WM_QUIT` 提前出现
2. **弯路一**：怀疑某个环境差异（隐藏 Wine 导出）让程序走了别的分支。改了之后重现，现象不变，排除
3. **弯路二**：怀疑 `GetMessage` 带窗口过滤时，Wine 返回 `PostQuitMessage` 的退出标志、或 `PostThreadMessage(WM_QUIT)` 的方式和 Windows 不同。写了 `we/tests/quit_filter_probe`（各种 hwnd 过滤、消息范围过滤、`(HWND)-1`、别的线程的窗口），Windows 和 Wine 的输出相同（`quit_filter_probe.windows.txt` / `.wine8.txt`），排除
4. 反汇编登录组件：那个 `WM_QUIT` 是组件的「终止」处理函数投递给自己工作窗口的；「终止」只在程序调用终止接口时触发；程序只在初始化失败时才调用终止接口。而服务端的日志里初始化是成功的——值在半路丢了
5. 在测试前缀里写一个 64 位小程序按同样的顺序调用入口 DLL，复现 `Initialize` 返回 -1。再直接用 COM 调服务器的 `Add`：`S_OK` 但 `ret` 不变。导出类型库，看到参数都没有方向标志，对上 `write_param_fs()`
6. 复现时遇到的环境问题（不是 Wine 的问题）：测试前缀里少拷了服务器旁边的一个版本文件，它的更新检查走进了强制更新、卡住；补上文件后正常
7. **弯路三**（修好之后）：服务器进程在登录完成几秒后就退出了，怀疑 Wine 在客户端 STA 线程不调 `CoUninitialize` 就退出时提前释放了代理。`we/tests/sta_thread_exit_probe` 在 Windows 上的结果和 Wine 相同（线程退出时代理的引用立即释放，`sta_thread_exit_probe.windows.txt` / `.wine9.txt`）；原来那个服务器在 Windows 上也同样约 6 秒后退出。排除

## 另外看到的问题（不是 Wine 的问题）

修好之后，游戏在进入角色选择时卡住：黑屏转圈，不读盘、不联网。它在这一步用 `WMCreateReader` 创建异步读取器，通过 `IWMReaderAdvanced2::OpenStream` 打开一段 ASF（WMA）数据，等 `WMT_OPENED` 回调。Proton 自带的 GStreamer 插件（解码 WMA/WMV、H.264 的 `gst-libav` 等）依赖 Steam Linux Runtime 里的库（`libvpx.so.9`、`libjpeg.so.62`、`libnettle.so.8` 等）；在运行时外面直接执行 `proton` 时这些插件加载失败，`OpenStream` 返回 `E_FAIL`。同一个读取器的测试程序在运行时容器里能正常打开、播放到结尾。Proton 要在 `toolmanifest.vdf` 要求的运行时（DWProton 是 SLR 4.0，app id 4183110）里运行。

这也暴露了 release 9 及以前的一个构建问题：`overlay-build.sh` 在宿主机上编的 `ntdll.so` 链接了宿主机的 `libunwind.so.8`，运行时里没有这个库，Wine 在容器里起不来。release 10 起 Wine 在 `Makefile.in` 用的 SDK 镜像里、用同样的编译参数编，`ntdll.so` 和官方一样只依赖 `libgcc_s` 和 `libc`。
