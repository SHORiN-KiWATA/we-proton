# 0009：d3dcompiler 的反射读不出最低精度签名（ISG1/OSG1/PSG1）

补丁：`patches/wine/0009-d3dcompiler-Handle-the-ISG1-OSG1-and-PSG1-signature-.patch`（`dlls/d3dcompiler_43/reflection.c`、`blob.c`、`d3dcompiler_private.h`，测试在 `dlls/d3dcompiler_43/tests/reflection.c`、`blob.c`）。这份源码同时编进 `d3d10` 和 `d3dcompiler_33`～`43`、`46`、`47`。

## 现象

《天涯明月刀》的 DX12 客户端（64 位）启动后加载登录场景时闪退，大约在启动后 20～30 秒。每次崩在同一个地址，崩溃报告的转储显示：

```
movq (%rcx), %rax        rcx = 0, rax = 0x80070057 (E_INVALIDARG)
```

也就是说，前一个调用返回了 `E_INVALIDARG`，拿到的对象是空的，程序没检查就去取了它的虚表。N 卡（RTX 4060，nvidia 615 驱动）和 780M（RADV）上都一样。同一个游戏的 DX11 客户端没有问题。

## 根因

1. 以 `VKD3D_DEBUG=warn` 运行，vkd3d-proton 在崩溃前报：`vkd3d_validate_vertex_input_signature: No input layout element found for VS input semantic POSITION0`，`CreateGraphicsPipelineState` 返回 `E_INVALIDARG`
2. 在校验失败的地方打日志，并把着色器字节码存下来：游戏传进来的输入布局有 **0 个元素**；顶点着色器的输入签名有 6 个元素（`POSITION0`、`TEXCOORD0`～`3`、`SV_InstanceID0`）
3. 这个顶点着色器是游戏自带的预编译着色器（SpeedTree 树木用的，由微软的编译器编译），用了 `min16float`/`min16uint`，所以签名段是 `ISG1`/`OSG1`，而不是 `ISGN`/`OSGN`。游戏用 `D3DReflect` 读顶点着色器的输入签名来拼输入布局
4. Wine 的 d3dcompiler 只认 `ISGN`/`OSGN`/`OSG5`/`PCSG`。`d3dcompiler_shader_reflection_init()` 遇到 `ISG1`/`OSG1`/`PSG1` 时直接跳过，于是反射报告 0 个输入、0 个输出。`D3DGetBlobPart()` 取签名也一样拿不到这几个段，返回 `E_FAIL`
5. 结果是游戏拼出一个空布局，vkd3d-proton 拒绝创建管线（Windows 也会拒绝，见下面），游戏拿到空 PSO 后崩溃

`ISG1`/`OSG1`/`PSG1` 的每个元素是 8 个 DWORD，比 `ISGN` 多两个字段：名字偏移前面多一个 stream 索引（同 `OSG5`），掩码后面多一个最低精度。最低精度的数值就是 `D3D_MIN_PRECISION` 的值（1 = `FLOAT_16`，4 = `SINT_16`，5 = `UINT_16`）。微软的编译器只要整个着色器里有一处用了最低精度类型，就会把所有签名都写成这种格式：下面那个 HS 只有 patch constant 里有 `min16float`，三个签名却都是 `*1` 格式。

## Windows 的行为（实测）

Windows 11 26200 的 `d3dcompiler_47`（虚拟机直通了 RTX 4060，用的是真驱动）。

**`we/tests/minprec_sig_probe`**：对 `minprec_vs.dxbc`、`minprec_hs.dxbc` 调 `D3DGetBlobPart` 取各种签名，再用 `ID3D11ShaderReflection` 和 `ID3D12ShaderReflection` 列出参数。两个着色器是在 Windows 上用 `D3DCompile` 编译的，源码在同名的 `.hlsl` 文件里。输出见 `minprec_sig_probe.windows.txt`。

| | VS（`ISG1`、`OSG1`） | HS（`ISG1`、`OSG1`、`PSG1`） |
|---|---|---|
| 输入签名 | `ISG1` | `ISG1` |
| 输出签名 | `OSG1` | `OSG1` |
| 输入加输出 | `ISG1` + `OSG1` | `ISG1` + `OSG1` |
| patch constant 签名 | `E_FAIL` | `PSG1` |
| 全部签名 | `E_FAIL`（只有两个段） | `ISG1` + `OSG1` + `PSG1` |
| 反射 | 4 个输入、3 个输出，`MinPrecision` 是 1/5/0/0、0/1/4 | 1 个输入、1 个输出、5 个 patch constant，`TEXCOORD0` 的 `MinPrecision` 是 1 |

游戏那个顶点着色器：Windows 上 `D3DReflect` 报告 6 个输入（`MinPrecision` 分别是 1、1、1、5、5、0），Wine 上是 0 个。

**输入布局和 PSO 的关系**（另写了一个 `CreateGraphicsPipelineState` 的小程序，没有收进仓库）：只要布局缺了 VS 签名里的任何一个元素，Windows 都返回 `E_INVALIDARG`，哪怕这个输入没被读（读写掩码为 0）、布局完全为空，或者用 `SV_Position` 顶替 `POSITION`；语义名比较不区分大小写。这和 vkd3d-proton 的校验逐项一致，所以 vkd3d-proton 没有问题。

## 修复

- `reflection.c`：新增 8 个字段的元素格式。`ISG1`/`OSG1`/`PSG1` 按这个格式解析，并在 `d3dcompiler_shader_reflection_init()` 里分别当作输入、输出、patch constant 签名。元素格式只要不是 6 个字段，就先读 stream；8 个字段的格式在掩码之后再读最低精度，`D3D_COMPILER_VERSION >= 46` 时写进 `MinPrecision`。`ISGN`/`OSGN`/`PCSG` 仍然报 `D3D_MIN_PRECISION_DEFAULT`（Windows 也是这样）
- 同一处顺带修了一个问题：在 `d3d10`（`D3D_COMPILER_VERSION` 为 0）里，结构体没有 `Stream` 成员，原来遇到 `OSG5` 时也不跳过 stream 字段，后面的字段会整体错位。现在这种情况也会跳过
- `blob.c`：`check_blob_part()` 把 `ISG1`/`OSG1`/`PSG1` 算进对应的签名部分。「全部签名」原本就要求正好 3 个段，所以 VS 失败、HS 成功，和 Windows 一致
- 新增 Wine 测试 `test_reflection_min_precision`、`test_get_blob_part_min_precision`：用 Windows 编出来的 VS/HS 字节码，期望值取自 Windows 的实测结果，只在 `D3D_COMPILER_VERSION >= 46` 时运行

## 验证

- Wine 测试在 Windows 上：同一个 `d3dcompiler_47_test.exe` 配 Windows 自带的 `d3dcompiler_47`，`reflection` 1500 项、`blob` 291 项，都是 0 失败。期望值和 Windows 的行为一致
- Wine 测试，修复前后（x86_64）：

  | 测试 | 修复前 | 修复后 |
  |---|---|---|
  | `d3dcompiler_47:reflection` | 1360 项，5 失败（新测试） | 1500 项，0 失败 |
  | `d3dcompiler_47:blob` | 263 项，5 失败（新测试） | 291 项，0 失败 |
  | `d3dcompiler_47:asm`、`hlsl_d3d11`、`hlsl_d3d9` | — | 前后相同 |
  | `d3dcompiler_43:asm`、`blob`、`hlsl_d3d11`、`hlsl_d3d9`、`reflection` | — | 前后相同 |
  | `d3d10:device`、`effect`、`reflection` | — | 前后相同 |

  `hlsl_d3d9` 修复前后都有 1 项失败，和这次的改动无关
- `minprec_sig_probe`：修复前（release 10，`minprec_sig_probe.wine10.txt`）取签名返回 `E_FAIL`，反射报告 0 个参数；修复后（release 10，只换了 x86_64 的 `d3dcompiler_47.dll`，`minprec_sig_probe.wine10-0009.txt`）和 Windows 的输出逐行相同
- 游戏：同样是「release 10 只换 `d3dcompiler_47.dll`」，在 Steam Linux Runtime 4.0 容器里运行。登录、创建角色、进入世界都正常。第一次进入世界时会花较长时间编译着色器（加载条停在 0%，CPU 占满），之后正常

## 排查过程和弯路

1. 游戏本体加了壳，代码段是加密的，静态反汇编没有用。从崩溃转储的栈上读到正在加载的是 SpeedTree 树木（`.srt`）和它的着色器（`.fx12obj`），以及崩溃前一个调用返回的 `E_INVALIDARG`
2. **弯路一**：第一次以 `VKD3D_DEBUG=info` 运行，在崩溃线程上看到 `d3d12_state_object_parse_subobject: Failed to parse DXIL library`（DXR 的 state object），于是推测是基础光追管线创建失败，后来 `AddToStateObject` 拿到空父对象，返回 `E_INVALIDARG`。用加了日志的 vkd3d-proton 记录每次 `CreateStateObject` 后发现：128 个 collection 全部成功，也没有调用 `AddToStateObject`，照样崩溃。推测不成立。那条 DXIL 解析错误只在 N 卡上出现，跟这次崩溃无关，没有继续查
3. **踩过的坑**：vkd3d-proton 的日志级别顺序是 `NONE < ERR < INFO < FIXME < WARN < TRACE`，设成 `info` 会把 WARN 全部过滤掉，前两次运行就是因此漏掉了那条输入布局的警告。另外，vkd3d-shader 那一路的日志要单独设 `VKD3D_SHADER_DEBUG`（Proton 默认把它设成 `none`）
4. **弯路二**：游戏运行时会用 Wine 的 `D3DCompile` 编约 300 个 HLSL 着色器，其中 4 个预处理失败（`#if a||||b`；`#define X 1;` 之后写 `#if X`）。在 Windows 上对照：微软的 `D3DPreprocess` 会报错，但仍返回 `S_OK`（`||||` 那行 `#if` 被丢掉，`1;` 按前面的 1 求值）；不过 `D3DCompile` 遇到这两种写法在 Windows 上同样会失败，所以这几个着色器在 Windows 上也编不过，跟崩溃无关。Wine 另外有一处行为确实和 Windows 不同：被 `#if 0` 跳过的块里写坏的 `#if` 表达式，Wine 也会去求值并报错，Windows 不会。这次没有遇到它，也没有修
5. 给 vkd3d-proton 的所有 `E_INVALIDARG` 出口都加上日志，再以 `VKD3D_DEBUG=warn` 运行：定位到 `d3d12_pipeline_state_init_graphics_create_info` 里的顶点输入校验
6. 在 Windows 上对照输入布局的规则：和 vkd3d-proton 一致（见上面）。所以问题在于游戏在 Wine 下拿到的布局不一样
7. 最初以为那个 VS 是 Wine 现场编的，但 Wine 预处理过的 303 个着色器里一个 `: POSITION` 语义都没有，所以它是游戏自带的预编译着色器。再在校验失败处把布局、签名和字节码都存下来：布局是空的，字节码里是 `ISG1`。拿这份字节码分别在 Wine 和 Windows 上做 `D3DReflect`，得到 0 个和 6 个输入，问题落到 d3dcompiler

## 没有验证的部分

- `d3dcompiler_33`～`43` 遇到最低精度签名时在 Windows 上怎么处理没有实测（最低精度是从 `d3dcompiler_46` 才有的），补丁对所有版本都按同样的格式解析；新测试只在 46 及以上运行
- `d3d10` 遇到 `OSG5`/`ISG1` 时在 Windows 上的行为没有实测
- Wine 测试只跑了 x86_64；游戏只验证了 x86_64 的 `d3dcompiler_47`

## 另外看到的问题（不是这次修的）

- N 卡上，DXR 的某个 state object 报 `Failed to parse DXIL library`，在 780M 上不出现，也不影响游戏运行，没有继续查
- 第一次进入世界时，着色器编译要花一段时间（见「验证」）
