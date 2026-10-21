# Wine-Mono Fixes

The top-level patches apply to Wine-Mono's Mono submodule, not Wine.
The `fna/` patches apply to the bundled FNA source instead.
`patches/protonprep-valve-staging.sh` prepares the official Wine-Mono
source release selected by `source.conf` and applies them. The Proton `Makefile.in` then builds
`libmono-2.0-x86.dll` and `libmono-2.0-x86_64.dll` using the SDK's
MinGW toolchains. Both runtime DLLs replace their prebuilt counterparts in
the distribution. It also rebuilds the Windows `System.Drawing.dll` class
library and `WineMono.FNA.dll`. All other managed libraries, WPF, WinForms, support MSI and native
helpers remain from the matching official binary release.

The Mono patches do not change winewayland, mscoree, executable metadata,
or game prefixes. The separate Wine SQM registry initialization described
below addresses a later failure. The layer/graphics work is unrelated.

## Source and Build

- Upstream: <https://github.com/wine-mono/wine-mono>
- Current release: `wine-mono-11.3.0`, selected by `source.conf`.
- Source artifact: `wine-mono-${WINEMONO_VER}-src.tar.xz` from the official release.
- Earlier Purple investigations used 11.2.0, commit
  `2973e4af02eeec2f2727e88e4559cb149271775d`, with Mono submodule
  `775a29a0d864061a40438e38830c0afaadf9ae79`.
- Native runtime implementation remains upstream Wine-Mono/Mono, with the
  local patches authored by GloriousEggroll. No ReactOS source is used.

The first prep downloads the approximately 307 MB source archive to
`contrib/`. Cached archives are reused without checksum verification.
Every prep removes the generated `wine-mono/` source tree, re-extracts
the pristine source including pinned
submodules, and applies `patches/wine-mono/*.patch` in filename order.
It then applies `patches/wine-mono/fna/*.patch` to `wine-mono/FNA/`.
Re-extraction is the archive equivalent of reset/clean for this component;
there is no Wine-Mono Git submodule. A symlink or Git checkout at that path
is rejected rather than deleted.

Only successful prep publishes `wine-mono/.proton-prepared`. The build
copies that prepared tree into `build/src-wine-mono-${WINEMONO_VER}/` and builds the
native runtimes and class libraries there; it does not apply patches a second
time. Re-running prep invalidates those build outputs. Builds without another prep
reuse them. Re-run prep whenever changing the patch stack. Missing or stale
prep stops the build with an explicit instruction to run protonprep.

The class-library stage uses the release's `mono.make` host-runtime/compiler
bootstrap and `net_4_x` Windows profile. This builds the framework dependencies
needed by `System.Drawing`, but only `System.Drawing.dll` and its PDB (when
present) replace files in the distribution. The GAC copy is also the target of
the release's `lib/mono/4.5/System.Drawing.dll` symlink, so both lookup paths
use the replacement. No application-local DLLs or prefix overrides are used.
The initial class-library build adds work beyond the previous native-only
build; the drawing stamp caches it until the prepared source changes.

No extra LLVM toolchain download or WPF/WinForms rebuild is requested.
The generated source tree is ignored by the main Git repository.
The native runtime link explicitly uses `-static-libgcc`: the SDK's x86
GCC otherwise imports its integer-division helpers from
`libgcc_s_dw2-1.dll`, which is not included in the distribution. Pass this
through `PDB_LDFLAGS_LIBMONO_x86` / `PDB_LDFLAGS_LIBMONO_x86_64`, because
`mono.make` replaces `LDFLAGS` for the final runtime link. These variables
also suppress the unwanted PDB filename when generating DWARF symbols.

When updating Wine-Mono, update the version in `source.conf`
(shared by the prep script and Makefile), rebase the runtime patch, and
keep the binary and source release versions matched.

The agent has not compiled Proton or Mono. The user builds separately.
Source/metadata inspection, patch validation, and diagnostic launches of
the user's rebuilt runtime have been performed. The regression fixtures
have not been assembled or run.

## ARM64 Compiler Delegate Cache Race

`0004-mini-order-delegate-trampoline-cache-publication.patch` adds matching
publication/consumption barriers to the JIT delegate trampoline cache.
The 2026-10-13 Ampere `GE-Proton11-8-aarch64` build aborted with SIGSEGV
while Roslyn compiled `net_4_x-linux/mscorlib.dll` during the System.Drawing
bootstrap (`Makefile.in`'s drawing stamp, `mono.make:181`, error 134).
This is the native ARM64 host Mono, not an emulated Windows compiler.

Offline inspection of the saved core found an `Action` for Roslyn's
`<CompileNamedTypeAsync>b__0` with a valid optimized `invoke_impl` and a
NULL `method_ptr`. Its shared `MonoDelegateTrampInfo` had both valid pointers.
Disassembly confirmed that the constructor copies `invoke_impl` and then
`method_ptr` without an acquire barrier, while the runtime publishes those
fields without a release barrier. The optimized ARM64 invoke stub loads
the NULL target and branches to address zero. Ordering the cache accesses
prevents this mismatched snapshot; a barrier also precedes publication of
the delegate's own optimized entry point. No compiler parallelism, profile,
game overrides, or graphics code is changed.

The existing prep loop picks up patch 0004 automatically. Re-run prep before
the next build so the host runtime is rebuilt as well as the Windows DLLs.
Do not apply it only to the copied build tree: that tree is regenerated.
The core and source establish the missing synchronization; the fixed ARM64
build still requires validation by the user.

`tests/delegate-cache-publication.cs` stresses simultaneous construction
and invocation of instance, shared-generic, and static delegates, creating
fresh AppDomains to exercise cold caches. It is probabilistic and a single
successful run does not establish that an unpatched runtime is safe.
After building the patched host Mono, run it manually using that runtime
and its matching class libraries, for example from the Wine-Mono source root:

```sh
build/mono-unix/runtime/mono-wrapper mono/mcs/class/lib/net_4_x-linux/mcs.exe \
    -out:/tmp/delegate-cache-publication.exe /path/to/patches/wine-mono/tests/delegate-cache-publication.cs
MONO_PATH="$PWD/mono/mcs/class/lib/net_4_x-linux" \
    build/mono-unix/runtime/mono-wrapper /tmp/delegate-cache-publication.exe
```

The agent has not compiled or run this fixture, nor restarted the Ampere build.

## FNA Stencil Reference Ordering

`fna/0001-graphicsdevice-preserve-stencil-reference-order.patch` corrects
an ordering mismatch between deferred `DepthStencilState` assignment and
immediate `GraphicsDevice.ReferenceStencil` access. Without the fix,
assigning a new state, setting a reference, then drawing resets the
reference to the state's original value. The getter can also return a
stale value before drawing. Only the pending depth/stencil state is flushed;
the supplied state object is not modified and other state remains deferred.

For [Meridian: New World, issue 430](https://github.com/GloriousEggroll/proton-ge-custom/issues/430),
read-only inspection of SunBurn's forward renderer found this exact call
sequence in its direct-light passes: stencil `NotEqual`, a nonzero dynamic
reference, and a stencil buffer cleared to zero. The game requests
`Depth24Stencil8`. Overwriting the reference with zero rejects the lighting
pass. The user's rebuilt version partially improved the lighting, but
terrain darkness and scrolling flicker remained. Both SDL_GPU/Vulkan
and D3D11/DXVK were dark, and disabling post-processing did not help.
The native-XNA comparison instead failed on a separate Mono COM reference
count assertion and is not a valid visual baseline.

The current source is Wine-Mono 11.3.0 (see `source.conf`), with FNA
26.08.0.0. The 11.2.0 runtime investigations elsewhere in this document
are historical. The build now uses Wine-Mono's own `WineMono.FNA.dll`
target, reusing the host compiler bootstrap. Packaging requires the same
GAC assembly identity as the binary release and replaces only that DLL.
FNA3D, XNA ABI stubs, NetStub, assembly configuration and native helpers
remain from the release. No game DLLs or prefix overrides are changed.

Re-run normal protonprep before rebuilding. Prep and build prerequisites
include the FNA patch directory, so reapplying the stack retains the fix.
Retest the demo with its normal graphics settings and without
`WINE_MONO_OVERRIDES` or `FNA3D_FORCE_DRIVER`. Then compare the D3D11 backend.

`tests/fna-reference-stencil.cs` is a standalone graphics regression
fixture for baseline and patched FNA. Compile against `WineMono.FNA.dll`,
then run through Wine-Mono with its normal native helpers available. It
checks a pending state followed by an explicit reference, actual stencil-
tested pixel output, another reference update without a state change,
the getter before drawing, and rebinding a state after a dynamic override.
It also exercises the internal effect-state merge through reflection,
checking preservation across depth-write changes, cache reuse, explicit
zero/nonzero effect references, and a pending state before a draw.
The fixture has not been compiled or run; the agent does not build here.
The FNA patch applies to the pristine 11.3.0 archive without fuzz or
offsets, and its result matches the prepared source edit. Prep-script
syntax and Git whitespace checks passed. No full prep, compilation or
game retest was run by the agent for this change.

### Effect-State Stencil Inheritance

`fna/0002-pipelinecache-preserve-dynamic-stencil-reference.patch` fixes a
second way the reference was lost. `PipelineCache.BeginApplyDepthStencil`
copied `DepthStencilState.ReferenceStencil`, which still contains the
original value, rather than the current `GraphicsDevice.ReferenceStencil`.
An effect changing only depth writes could therefore reset the reference
to zero. Seed the effect's state merge with the device value instead;
an explicit effect `STENCILREF` continues to override it. The caller's
state remains immutable and the cache key already includes the reference.
This depends on 0001 to resolve pending depth/stencil state before the
device reference is read. Both are GE-only downstream patches.

The October 11 OpenGL core-profile RenderDoc capture confirms the failure:

- `/tmp/meridian-gl-capture/frame_frame8191.rdc`, draw 2010: terrain lighting
  produces RGB approximately `(0.2245, 0.2158, 0.0868)` but fails the stencil
  test, leaving the ambient-only color approximately `(0.0353, 0.0706, 0.0471)`.
- The active comparison is `NotEqual`; the reference changed from 220 to
  zero while the stencil buffer at that pixel is zero. The depth test passes.
- Changing only three reference-reset commands in a temporary copy of the
  capture to preserve references 218, 219 and 220 restores the missing
  lighting in replay. Before/after images and pixel history are in
  `/tmp/meridian-gl-analysis/`. No game or installed runtime files were changed.

This is replay-verified evidence for the remaining darkness, not a live
verification of the rebuilt patch or scrolling flicker. Rebuild normally
after protonprep, then test the default backend and D3D11 with no capture
options. No prep-script change is needed: the existing FNA glob applies
0001 then 0002 and the Makefile tracks both as inputs.
Both patches were applied in order to the pristine 11.3.0 source files
without fuzz or offsets, and both results byte-match the prepared tree.
Git whitespace checks passed. No compilation or regression-fixture run
was performed for 0002.

Capture caveats: the diagnostic OpenGL run also reproduced the darkness.
Use `FNA3D_OPENGL_FORCE_CORE_PROFILE=1` with OpenGL capture to avoid FNA's
compatibility-profile `glTexEnvi` call disabling RenderDoc captures.
WOW64 alone starts; WOW64 plus Vulkan capture failed because the captured
device lacked usable low-address mapping support, leading to mappings
outside the 32-bit address space. That capture startup failure is separate
from the lighting bug and is not a reason to change the game's renderer.

The user's first rebuilt capture (September 7, 23:48,
`GE-Proton11-6-27-g80365ed3c`) failed earlier than the managed initializer:
`mscoree` could not load Mono because `libgcc_s_dw2-1.dll` was missing.
Inspecting the installed x86 DLL confirmed that import; the x86_64 DLL did
not have it. The September 8 rebuilt capture confirms that the static-link
correction fixed this: Mono now loads, and the installed x86 DLL no longer
imports libgcc. Import-table inspection is not an archive checksum check.

That capture also contains a separate `explorer.exe` heap-corruption abort
before Purple starts, followed by repeated exception handling failures.
Its cause has not been established; do not attribute it to Mono, which
had not loaded. That earlier exception loop was not present in the new
capture.

## Settings.exe Icon Failure

`0003-drawing-resolve-default-icon-dimensions.patch` fixes the immediate
startup failure of SP Football Life 2026's `Settings.exe`, without an
executable-name check. The September 10 capture at `~/steam-spfl26.log`
shows `System.Drawing.Icon.BuildBitmapOnWin32()` throwing
`Unexpected number of bits: 0` from `Form.UpdateWindowIcon()`, followed by
`CorExitProcess(1)`. A later native fault in `Settings_b.dll` occurs during
termination; it is not the first failure.

Read-only inspection of the executable's serialized icon resource confirmed:

- `IconSize` contains width and height zero (unspecified).
- `IconData` contains seven frames in a 52,968-byte array.
- Mono's old unspecified-size fallback chooses the largest non-ignored
  frame by byte count: a 48x48, nominally 24-bpp entry with an all-zero DIB
  header. Valid 32x32 entries exist in the same icon.

Zero dimensions should first resolve to the system icon metrics, as in
[Microsoft's Icon implementation](https://github.com/dotnet/winforms/blob/main/src/System.Drawing.Common/src/System/Drawing/Icon.cs).
The patch reuses Mono's existing User32 binding on Windows and its 32-pixel
default on Unix, for stream loading (including deserialization) and cloning.
It does not suppress bitmap-decoding exceptions, repair malformed image data,
or change selection for explicitly specified nonzero dimensions.

The upstream `System.Drawing` NUnit tests now include generated multi-frame
fixtures covering zero dimensions, cloning, missing/empty serialized sizes,
a damaged unselected frame, explicit larger sizes, and bitmap/handle creation.
The new tests require the Windows backend (Wine or native Windows); no game
assets are included. Patch application was checked without fuzz. No build or
runtime tests have been run by the agent for this change.

After normal protonprep and rebuild, retest the original `Settings.exe`
command without installing native .NET or altering the executable. The log
must no longer contain the fatal `Unexpected number of bits: 0` stack.
Successful end-to-end application startup still needs user verification.

## Purple Failure

Observed in `Purple.exe` 2.26.907.19 with Wine-Mono 11.2.0:

- `9637AABA.B71B1EA0` is a static readonly byte with HasFieldRVA.
- The FieldRVA table contains matching rows with RVA **zero**. The earlier
  protonfix description saying the row is missing is not correct for this
  version. A zero RVA and a missing row are distinct cases.
- `9637AABA::0B3E3192` takes this field's address (`ldsflda`).
- Mono treats it as absent data, emits the "should have RVA data" warning,
  and uses ordinary zero-initialized static storage. That is not the PE
  image base that an explicit zero RVA represents in a mapped module.
- The initializer itself contains the `newobj BadImageFormatException` /
  `throw` sequence. The crash log shows that exception followed by
  `TypeInitializationException` and module-constructor failure.

Microsoft's public implementation uses `GetRvaData(field, NULL_OK)` in
[PEFile::GetRvaField](https://github.com/dotnet/coreclr/blob/master/src/vm/pefile.inl).
[PEDecoder::GetRvaData](https://github.com/dotnet/runtime/blob/main/src/coreclr/utilcode/pedecoder.cpp)
returns the image base plus the RVA in that case, including RVA zero.
The patch implements that distinction only for Win32 module-backed field
data. It does not make the generic RVA mapper accept zero for other uses,
suppress exceptions, or change missing-row/static-storage behavior.

This is a concrete compatibility mismatch, but fixing it is not yet proof
that all of Purple's WPF/CLR requirements are satisfied. Retain the existing
`l2` protonfix's `dotnet48` fallback until end-to-end testing succeeds.

For runtime validation, use the existing Mono-only `purple` prefix and a
neutral GAMEID such as `umu-purple`, not `umu-l2` (which automatically
installs dotnet48). Do not uninstall .NET from the working prefix. Confirm
the log loads the rebuilt `libmono-2.0-x86.dll`, and capture managed stacks
if another initializer fails:

```sh
WINE_MONO_TRACE=E:System.BadImageFormatException,E:System.TypeInitializationException \
WINEDEBUG=+timestamp,+pid,+tid,+seh,+mscoree,+loaddll \
WINEPREFIX="$HOME/Games/purple" GAMEID=umu-purple \
PROTONPATH=GE-Proton11-6-x86_64 PROTON_LOG=1 PROTON_ENABLE_WAYLAND=1 \
umu-run "$HOME/Games/purple/drive_c/Program Files (x86)/NC/Purple/PurpleLauncher.exe"
```

The captures inspected were `/home/tcrider/steam-purple.log` (Mono crash)
and `/home/tcrider/steam-0.log` (the `purple-dotnet` prefix using native CLR
and WPF). The originally supplied `steam-purple-dotnet.log` path did not
exist during inspection.

## Reflection Follow-Up

The September 8 capture gets past `9637AABA` with patch 0001. A diagnostic
JIT trace confirms `0B3E3192` returns the actual module base, `0x00400000`.
The next failure is `EEB42305..cctor`: it calls
`Marshal.GetDelegateForFunctionPointer(IntPtr.Zero, ...)`, resulting in
`ArgumentNullException("ptr")`. Keep that validation intact.

The program trace and read-only metadata inspection identify an earlier
type resolution error:

- TypeSpec `0x1b000993` has signature `10 05`, meaning `System.Byte&`.
- The runtime resolves it as `System.Byte`. Purple's reference conversion
  consequently truncates an address to a byte instead of dereferencing it.
- For the first byte, address `0x0076e8ea` becomes `0xea`; the actual byte
  there is `0xf3`. Following the logged arithmetic with the actual byte
  instead of the truncated address reconstructs `ntdll.dll`, whereas the
  failing run produces an invalid module name.
- `module_resolve_type_token()` converts every TypeSpec to `MonoClass`
  and returns its `byval_arg`, discarding the byref bit. Both
  `Module.ResolveType` and `Module.ResolveMember` use that helper.

Patch 0002 uses the existing `mono_type_get_checked()` path for loaded
TypeSpecs, preserving the full signature. It also preserves byref when
that helper canonicalizes inflated generic types. TypeDef/TypeRef paths,
dynamic images, token validation, and exception handling remain unchanged.
This is a generic Mono fix, with no Purple executable-name checks.
The API must return the type identified by the token, including its
generic context; see Microsoft's
[Module.ResolveType documentation](https://learn.microsoft.com/en-us/dotnet/api/system.reflection.module.resolvetype?view=netframework-4.8.1).

Diagnostic captures are in `/tmp/purple-mono-{lookup,pointer,startup}/`.
The full `startup/steam-purple.log` is approximately 59 MB. Those runs
exited during initialization; no game was launched or modified. Do not
enable `WINE_MONO_TRACE=program` for an ordinary user retest: it is much
heavier than exception tracing. The user's `~/steam-purple.log` was kept
separate from these diagnostic captures.

An interpreter-only comparison (`WINE_MONO_AOT=interp`) still hit the
earlier RVA initializer failure. It did not reach the new reflection
failure and does not establish whether patch 0002 fixes interpreter mode.
Normal JIT startup is the current target. The user's next rebuilt-runtime
test gets past both initializers and opens the client. Full Purple/WPF
compatibility is not yet verified. Retain the dotnet48 fallback for now.

After rebuilding, a suitable lightweight capture is:

```sh
WINE_MONO_TRACE=E:System.ArgumentNullException,E:System.TypeInitializationException,E:System.BadImageFormatException \
WINEDEBUG=+timestamp,+pid,+tid,+mscoree,+loaddll \
WINEPREFIX="$HOME/Games/purple" GAMEID=umu-purple \
PROTONPATH=GE-Proton11-6-x86_64 PROTON_LOG=1 PROTON_ENABLE_WAYLAND=1 \
umu-run "$HOME/Games/purple/drive_c/Program Files (x86)/NC/Purple/PurpleLauncher.exe"
```

## Missing SQM Machine Identifier

The September 8 `01:33` capture in `~/steam-purple.log` gets past both
initializers. The next popup is `ArgumentNullException("machineId")` in
`Formula.Infrastructure.Logis.LogisService`, also reached from
`SignInWebViewController.WriteResultLog` after sign-in.

Read-only inspection of the running factory and `Formula.Core.dll` shows
that Purple calls `RegistryHelper.GetMachineId`, which reads
`HKLM\Software\Microsoft\SQMClient\MachineId` using `Registry64` on a
64-bit OS. This key is absent in the Mono prefix but present in the
working `purple-dotnet` prefix. The installed `mscorlib.dll` uses CoreFX's
registry implementation, which preserves and forwards the registry view.
The legacy `Microsoft.Win32/RegistryKey.cs` implementation that ignores
views is excluded from this Wine-Mono profile; do not patch it for this
failure.

This is missing Wine environment data, not a further Mono runtime bug.
`patches/wine-hotfixes/pending/wineboot-create-sqm-machine-id.patch` adds
generic boot-time initialization with `UuidCreate`, following Microsoft's
[documented SQM registry behavior](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-sqmcs2/0442f736-8670-4ae4-8651-5732f3ba18dd).
It preserves an existing value and neither opts into telemetry nor starts
any telemetry service. The pending patch is explicitly applied by
`protonprep-valve-staging.sh`. `default_pfx.py` strips only the template's
SQM `MachineId`, so new prefixes get separate IDs instead of inheriting
one shared build-machine value. User prefixes are not filtered.

The same log has a later `ArgumentNullException("type")` / platform-not-
supported exception in WPF's WinRT `InputPane` activation while focusing
the error dialog's textbox. That is downstream of the logging-service
failure, not evidence that touch-keyboard support caused the first popup.
It has not been patched here.

After rebuilding, fully exit Purple and relaunch the same Mono-only prefix
so the new wineboot runs. No prefix recreation or dotnet48 installation
should be needed for the missing key. Verify sign-in, navigation, and game
launch separately; end-to-end success is still pending. Registry validation
should check a nonempty braced GUID in the native view, preservation after
another boot, different values in two fresh prefixes, and no SQM MachineId
in the packaged default prefix. The Wine patch has not been compiled or
runtime-tested by the agent. No-fuzz patch applicability, Git whitespace
checks, and prep-script syntax checks passed. Three in-memory Python tests
also passed: SQM-only filtering (including idempotence and preservation of
other values), existing font filtering, and preservation of the separate
Cryptography `MachineGuid`. These checks did not run a build or alter a
user prefix. The currently open client was briefly attached
for a read-only method inspection and detached, not restarted or modified.

### TypeSpec Regression Fixture

`tests/resolve-typespec.il` is independent of Purple. It obtains tokens
from method bodies instead of assuming their assigned row numbers, and
compares `ResolveType` / `ResolveMember` results with the corresponding
method return types. Cases cover byte/int byrefs, generic type/method
byrefs, pointers, arrays, a plain value type, and a TypeDef control.

```sh
ilasm /exe /output:ResolveTypeSpec.exe tests/resolve-typespec.il
```

Run with both baseline and patched Mono, and native .NET Framework as a
comparison. No executable metadata rewriting is necessary for this test.
Expected success is exit 0; exit 1 indicates ResolveType mismatch, exit 2
ResolveMember mismatch, and exit 3 a malformed fixture. Test names print
before each comparison. This fixture has not yet been assembled or run.

## Standalone Regression Fixture

The test does not use or modify Purple. Assemble `tests/rva-zero.il` with
Mono or Microsoft `ilasm`, then use the `dnfile` parser to set the test's
Zero field to an explicit RVA of zero and mark Missing as HasFieldRVA
without a row. Ordinary nonzero RVA data stays unchanged.

Example preparation (development tools only, not Proton build dependencies):

```sh
ilasm /exe /output:MonoRvaZero-template.exe tests/rva-zero.il
python3 -m venv /tmp/mono-rva-tools
/tmp/mono-rva-tools/bin/pip install dnfile==0.18.0
/tmp/mono-rva-tools/bin/python tests/prepare-rva-fixture.py \
    MonoRvaZero-template.exe MonoRvaZero.exe
```

Run the prepared EXE directly through Wine/mscoree in a separate Mono-only
prefix, not via `mono.exe`, so GetModuleHandle(NULL) identifies the test
assembly. Test x86 and x86_64 builds separately. Exit codes:

- 0: passed.
- 1: ldsflda did not return the loaded executable's image base.
- 2: ldsfld did not read the DOS signature.
- 3: ordinary nonzero RVA data changed.
- 4: RuntimeHelpers.InitializeArray did not read the DOS signature.
- 5: optional missing-row static-storage control failed.

Pass any argument to additionally test Mono's existing missing-row
fallback. Without that argument, the same test can be used as a .NET
Framework comparison. Baseline and patched runtime results are still to
be collected; do not report this fixture as having passed yet.
