# 0021、0022：VC++ 运行库的版本号

补丁：

- `patches/wine/0021-msvcp140-msvcp140_2-vcruntime140_1-Report-a-version-.patch`（`dlls/msvcp140`、`dlls/msvcp140_2`、`dlls/vcruntime140_1` 的 `version.rc`）
- `patches/wine/0022-wineboot-Keep-the-Visual-C-runtime-version-in-the-re.patch`（`programs/wineboot/wineboot.c`）

## 现象

《诡秘之主》（UE5，用自己的启动器）点开始游戏后弹出：

    The following component(s) are required to run this program:

    Microsoft Visual C++ 2015-2022 Redistributable (x64)

只有“确定”，点了就退出，游戏本体没有启动。

## 根因

### 引导程序查什么

游戏的 `Game\Lord of Mysteries.exe` 是 UE 打包游戏的引导程序（版本资源 5.7.0.4），它检查完运行库再启动资源里写的 `C7\Binaries\Win64\C7-Win64-Shipping.exe`。反汇编得到的检查顺序：

1. 游戏 exe 所在目录里自带的 `msvcp140_2.dll`、`vcruntime140_1.dll`：文件版本都 ≥ 14.44.35211 且能加载，就算通过。这个游戏只带了 `msvcp140.dll`、`msvcp140_1.dll`、`vcruntime140.dll`、`vcruntime140_1.dll`，没带 `msvcp140_2.dll`，这一步不过。
2. 打包应用（`GetCurrentPackageInfo`）的依赖检查，和 Wine 无关。
3. 读 `HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64`（64 位视图）的 `Major`/`Minor`/`Bld`，要 ≥ 14.44.35211；键打不开或版本低，直接判缺失。
4. `%SystemRoot%\system32\msvcp140_2.dll`、`vcruntime140_1.dll` 的文件版本要 ≥ 14.44.35211，且 `LoadLibrary` 成功；不过的话按 DLL 搜索路径再查一遍这两个文件。
5. 判缺失时拼出上面那条消息。如果 `Engine\Extras\Redist\en-us\vc_redist.x64.exe` 存在，会问要不要现在安装；这个游戏把安装包放在了 `Game\` 根目录，所以只有“确定”。

最低版本 14.44.35211 是写死在引导程序里的常量（`cmp ..., 0xe` / `0x2c` / `0x898b`）。

### Wine 给出的版本

- 内置 `msvcp140.dll`、`msvcp140_2.dll`、`vcruntime140_1.dll` 的版本资源是 14.42.34433.0（`version.rc` 里的 `WINE_FILEVERSION`）。
- 注册表键由 `wine.inf` 的 `[VCRuntime.*]` 写成 14.42.34433（64 位视图写 `x64`，32 位视图写 `x64` 和 `x86`），带 `FLG_ADDREG_NOCLOBBER`：已有的值不覆盖。所以前缀里的值一直是第一次建前缀那个 Wine 写的。用户前缀里是 14.51.36247.0（Wine 的格式，2026-08 由别的 Wine 写入），注册表这一项本来能过，卡在了文件版本这一项。

这几个 DLL 没有 prefer-native 标记，按 Wine 默认的加载顺序，system32 里就算有原生副本，加载的也还是内置的（除非设了 DllOverrides）。所以这个版本号只影响这类检查，不影响实际用哪份代码。

### 启动器为什么没装运行库

启动器下载完游戏会装依赖：用 `powershell.exe` 建一个计划任务，以 `/quiet /norestart` 运行 `Game\vc_redist.x64.exe`，返回码不是 0 的话再直接运行一次。Wine 的 `powershell.exe` 是个什么都不做、返回 0 的 stub，启动器的日志里是 `InstallDependenceTools: Installation code : 0`（46 ms），它以为装好了，就没走后备路径。这一层这次没有动，见“没做的”。

## Windows 上怎么样

Windows 上这个检查靠真正装好的运行库通过：官方安装包把四个注册表值写成自己的版本（`Version` 是 `v14.44.35211.00` 这种格式），system32 里的 DLL 也是这个版本。依据是在 Wine 里运行游戏自带的官方 14.44.35211 安装包之后读到的值（`vcrt_version_probe.wine24-redist.txt`，注册表和文件都是微软的 MSI 自己写的）。没有在 Windows 虚拟机上跑探测程序。

## 修复

- **0021**：三个 DLL 的版本资源改成 14.99.65535.0。这个数比任何已经发布的运行库都高（2026 年微软的版本是 14.51.36247），以后的最低版本要求也够不到，不用跟着工具链一版一版改。Wine 自己的 `include/wine/wine_common_ver.rc` 就写着这个原则：“Assign WINE_FILEVERSION and WINE_FILEVERSION_STR high enough number to make sure that programs, relying on the version numbers, will never complain”。
- **0022**：wineboot 每次启动时，对每个支持的架构取 `msvcp140.dll` 的文件版本（x64 读 system32，x86 读 syswow64，arm64 读 system32）。`Runtimes\<arch>` 键在 64 位和 32 位视图里只要存在，并且报告的版本比这个低，就把 `Major`/`Minor`/`Bld`/`Rbld`/`Version` 都提到这个版本。比它高的不动，键不存在的不建。这样旧前缀第一次用新版启动就能跟上（`wine.inf` 不覆盖已有值）；以后要是有安装程序把这个键写低了，下次启动也会恢复。如果装过官方运行库，system32 里是原生 DLL，读到的就是原生的版本，注册表和它一致，不会被改。
- `overlay-build.sh` 的 TARGETS：`dlls/msvcp140`、`dlls/msvcp140_2`（两个架构），`dlls/vcruntime140_1`（只有 x86_64，i386 本来就不编），`programs/wineboot`（只有 x86_64：WoW64 构建里 i386 不编 wineboot，Proton 前缀里运行的是 64 位的）。
- `we/tests/build.sh` 链接时多加了 `-lversion`（探测程序要用 `GetFileVersionInfo`）。

## 验证

`vcrt_version_probe` 打印 `VC\Runtimes` 各键在两个注册表视图里的值、system32/syswow64 里运行库 DLL 的文件版本，再照引导程序的第 3、4 步检查（默认最低 14.44.35211）。都在 Steam Linux Runtime 4.0 里经 shorin-proton-wrapper 运行。

- r23 新前缀：注册表和文件都是 14.42.34433，判缺失（`vcrt_version_probe.wine23.txt`）。
- r24 新前缀：都是 14.99.65535，通过（`vcrt_version_probe.wine24.txt`）。
- r23 建的前缀改用 r24：wineboot 把三个键都提到了 14.99.65535，通过。
- 同一路径把 r23 就地换成 r24（和 “WE-Proton Latest” 更新时一样）：也通过。前缀里复制的 `msvcp140_2.dll` 在磁盘上还是 r23 的 14.42（`msvcp1*.dll` 在 proton 的 `builtin_dll_copy` 里，是复制而不是符号链接；`CURRENT_PREFIX_VERSION` 和 prefix_info 都没变，proton 不刷新它），但 Wine 对内置 DLL 做 `GetFileVersionInfo` 时取的是 runner 里那一份，所以读到 14.99。
- r24 前缀里运行游戏自带的官方 14.44.35211 安装包（`/install /quiet /norestart`）：返回 0；system32 里的四个 DLL 换成了原生 14.44.35211，x64 键被写成 `v14.44.35211.00`，检查仍然通过（`vcrt_version_probe.wine24-redist.txt`）；下次启动时 wineboot 不改（键和 `msvcp140.dll` 一致）。安装包的 MSI 先执行 `RemoveFiles` 删掉这几个文件，再执行 `InstallFiles`，所以内置的版本号报得再高，也不妨碍装原生运行库（和打补丁前一样）。
- 引导程序端到端：把游戏的引导程序单独拷到测试目录，它要启动的 `C7\Binaries\Win64\C7-Win64-Shipping.exe` 换成一个只写标记文件的空程序，游戏本体不参与，在 Xvfb 上运行。r23 弹出同样的对话框（截图里就是这条消息），目标没有启动；r24 直接启动了目标（命令行 `"D:\\C7\Binaries\Win64\C7-Win64-Shipping.exe" C7`）。
- 游戏本体没有用 r24 测，只测了引导程序。
- Wine 自己的测试没跑：0021 只改版本资源，`msvcp140` 的测试不涉及版本号；wineboot 没有测试。上面每次测试前缀都正常启动，说明 wineboot 改动后没有出错。

## 排查过程和弯路

- 一开始怀疑注册表，但用户前缀里 x64 键已经是 14.51，就排除了。再比对文件版本：Wine 内置的是 14.42.34433，游戏自带的是 14.44.35211。反汇编后确认引导程序还查 system32 的文件版本，最低版本是常量 14.44.35211。
- 当时先在用户前缀里运行了游戏自带的官方安装包，绕过了这个问题。之后才写的补丁。
- 担心过版本号报高以后，官方安装包会按 MSI 的文件版本规则保留内置 DLL（Wine msi 的 `calculate_install_state` 遇到磁盘上的版本 ≥ 包里的版本就保留），从而装不上原生运行库，注册表却被写低。实测这个 MSI 先删文件再装，不受这条规则影响。winetricks 的 `vcrun2022` 本来也会手动替换 `msvcp140`、`msvcp140_2`（Wine bug 57518：内置版本号比它的安装包高时，就是靠手动替换处理的）。
- 考虑过只跟着微软最新的版本走（比如 14.51.36247）。但那样工具链每升级一次、引擎每提高一次最低版本，就得再改一次，所以选了一个够不着的数。
- 考虑过直接改 `wine.inf` 里的默认值并去掉 NOCLOBBER。问题有两个：旧前缀要等 `wine.inf` 的 mtime 变了才会重新执行它；新前缀用的是 `default_pfx` 里早就生成好的 `system.reg`，而 `overlay-build.sh` 只换二进制，不重新生成它们。放在 wineboot 里每次启动检查，就不依赖这些。
- 一度以为就地更新 runner 后，前缀里复制的内置 DLL 不刷新会让 0021 失效，给 `overlay-build.sh` 加过 `touch default_pfx/system.reg`（它的 mtime 是 prefix_info 的一部分）。对照测试证明不需要（见“验证”），就撤掉了。磁盘上的副本确实不刷新；直接读文件内容的程序会看到旧副本，这个没有处理。
- `overlay-build.sh` 第一次失败：WoW64 构建里 i386 不编 wineboot（`i386_DISABLED_SUBDIRS`），去掉了 i386 的映射。脚本出错时 `grep -m20 error` 只抓到了 `error.c` 的编译命令，真正的错误在 `build/overlay/make.log` 末尾。

## 没做的、未验证

- 启动器靠 powershell 计划任务安装运行库，Wine 的 powershell stub 返回 0，启动器就以为装好了。这个没改：让 stub 返回非 0，会影响所有调用 powershell 做些可有可无的事、又检查返回码的程序。有了 0021/0022，这个游戏不装运行库也能通过检查。
- 已经装过官方运行库的前缀（比如用户的 `~/.proton`，现在是原生 14.44.35211），检查按原生的版本算。以后要求更高版本的游戏还会提示，和 Windows 上装了旧版运行库一样，要再装一个新的。
- `vcruntime140.dll`、`msvcp140_1.dll`、`concrt140.dll` 等没有版本资源，这次没加。上游只给这三个加了，查这几个文件版本的程序还没遇到过。
- 没在 Windows 上跑探测程序。
- 纯 32 位前缀（非 WoW64）和 arm64 上 wineboot 的这段代码没测。
