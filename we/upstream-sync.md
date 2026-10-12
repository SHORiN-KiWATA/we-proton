# 跟进 GE-Proton 和 DW-Proton 的新版本（给 AI 的操作手册）

这份手册写给 AI agent，按顺序照做即可，不需要理解全部背景。规矩：

- **每一步做完都对照「检查」。** 结果和「检查」写的不一样，就停下，把命令和输出告诉用户，不要自己想办法绕过去。
- **看到「停下」两个字，就停下问用户。** 那些情况需要判断，不是照规则能做的。
- 不要推送（`git push`）、不要发 Release、不要改 AUR：这些都要用户明确同意。
- 先读一遍仓库根目录的 `AGENTS.md`。补丁、提交信息里不写游戏名、平台名、反作弊名。
- 不要手改 `patches/dwproton/`、`patches/wine/` 里的补丁文件：在 Wine 开发分支上改，再用脚本导出（见下面的步骤）。
- 所有命令都在仓库根目录运行，除非写明 `cd`。
- `we/build.sh` 和 `we/sync/wine-dev.sh` 都会重置、重新打补丁所有子模块：同一时间只跑一个，构建没结束前不要跑别的。

## 0. 背景：这个仓库是怎么叠起来的

| 层 | 在哪里 | 说明 |
|---|---|---|
| GE-Proton | 整个仓库（顶层历史就是 GE-Proton 的）、`patches/` 下除了下面两个目录之外的全部、`patches/protonprep-valve-staging.sh` | 底座。构建前 protonprep 往 Valve 的 Wine 上打 GE-Proton 的补丁和 wine-staging |
| DW-Proton | `patches/dwproton/`（Wine 补丁）、`patches/protonfixes/0003`–`0005`、`patches/dxvk/dw-hdr-detection-via-displayconfig.patch`、`7zip` 子模块和 `Makefile.in` 里的 7zip 段 | DW-Proton 有、GE-Proton 没有的东西。当前合入的 DW-Proton 版本写在 `we/dw-proton-version` |
| WE-Proton | `patches/wine/`（编号就是 WE-Proton 的补丁号）、`patches/vkd3d-proton/we-*.patch`、`we/` | 自己的修复 |

protonprep 在 `make_requests` 之前依次打 `patches/dwproton/` 和 `patches/wine/`（那一段叫 `(2-8) DW-PROTON AND WE-PROTON PATCHES`）。

合并时的规则和每个 DW-Proton 提交的去留，记录在 `we/ge-dw-merge.md` 和 `we/ge-dw-merge-commits.txt`。

## 1. 工具

| 命令 | 做什么 |
|---|---|
| `we/sync/wine-dev.sh` | 重置并打好所有子模块，在 `wine/` 里建三个分支：`we-base`（GE-Proton 打完补丁，一个提交）、`we-dw`（+ `patches/dwproton/`）、`we-dev`（+ `patches/wine/`，检出这个） |
| `we/sync/export-patches.py [--new NNNN]...` | 把 `we-base..we-dw` 写回 `patches/dwproton/`（从 0001 重新编号），把 `we-dw..we-dev` 写回 `patches/wine/`（按标题沿用原来的编号；新补丁用 `--new` 给编号）。生成文件自动排除。只差 From 行的文件保持不动 |
| `we/sync/dup-titles.py [--remove] <仓库> <范围>` | 列出范围里标题和 GE-Proton 补丁或 wine-staging 补丁相同的提交（GE-Proton 已经带着的）；`--remove` 同时删掉 `patches/dwproton/` 里同标题的补丁文件 |
| `we/build.sh --name <名字>` | 完整构建，输出到 `build/` |
| `we/quick-build.sh --base <runner> --name <名字> [--prep] <wine 目录>...` | 在上次完整构建的 `build/obj` 上只重编几个 Wine 模块，出测试版（不能用于发布） |
| `we/tests/build.sh` | 编测试程序（mingw） |
| `we/tests/regress.sh <runner> <输出目录>` | 在 Steam Linux Runtime 4.0 里跑回归探测；`--compare <旧目录> <新目录>` 比较 |

## 2. 一次性准备

1. 远程仓库：

   ```
   git remote -v
   ```

   **检查**：有 `ge`（`https://github.com/GloriousEggroll/proton-ge-custom.git`）和 `dwproton`（`https://dawn.wine/dawn-winery/dwproton.git`）。缺哪个就加：

   ```
   git remote add ge https://github.com/GloriousEggroll/proton-ge-custom.git
   git remote add dwproton https://dawn.wine/dawn-winery/dwproton.git
   ```

2. DW-Proton 的 Wine 仓库（只有跟进 DW-Proton 时要用；dawn.wine 下载慢，要等很久）：

   ```
   git clone --filter=blob:none https://dawn.wine/dawn-winery/wine-dwproton.git ~/src/wine-dwproton
   ```

3. `git config user.name` 和 `git config user.email` 都要有值。
4. 要有：docker（构建用的 SDK 镜像会自动拉）、`~/.local/share/umu/steamrt4/_v2-entry-point`（umu 或 wegame-launcher 装的，测试用）、`x86_64-w64-mingw32-gcc` 和 `i686-w64-mingw32-gcc`、`Xvfb`、`python3`。

## A. 跟进 GE-Proton 的新版本

### A1. 看有没有新版本

```
cat VERSION
git ls-remote --tags ge 'GE-Proton*' | grep -v '\^{}' | awk -F/ '{print $3}' | sort -V | tail -3
```

**检查**：最后一行比 `VERSION` 里的新，就是要跟进的版本，下面记作 `<新GE>`（例如 `GE-Proton11-8`）。一样就不用做。

### A2. 开分支并合并

先还原构建时 protonprep 在顶层目录里改过的文件，再看工作区：

```
git checkout -- lsteamclient steam_helper umu_helper wineopenxr
git status --short
```

**检查**：`git status --short` 的每一行都是子模块（行首是 ` m` 或 ` M`，后面是 `wine`、`dxvk` 这样的子模块路径）。有别的文件：**停下**（工作区有没提交的改动）。

```
git checkout -b sync-<新GE>
git fetch ge tag <新GE> --no-tags
git merge <新GE>
```

合并出现冲突时，`git diff --name-only --diff-filter=U` 列出冲突的文件，按下表处理。表里没有的文件冲突：**停下**。

| 冲突的文件 | 怎么处理 |
|---|---|
| `README.md`、`AGENTS.md` | `git checkout --ours <文件>`（用我们的） |
| `VERSION` | `git checkout --theirs VERSION`（用 GE-Proton 的） |
| `patches/protonprep-valve-staging.sh` | `git checkout --theirs` 这个文件，然后把下面「protonprep 里我们的三处改动」重新加回去 |
| `Makefile.in` | `git checkout --theirs Makefile.in`，然后把下面「Makefile.in 里的 7zip 段」重新加回去 |
| `.gitmodules` | 打开文件，保留两边的内容（我们的 `[submodule "7zip"]` 一段 + GE-Proton 的改动），删掉 `<<<<<<<`、`=======`、`>>>>>>>` 行 |
| 子模块 `dxvk`、`vkd3d-proton`、`protonfixes` | 见下面「子模块版本」 |
| `patches/protonfixes/` 里的文件 | 用 GE-Proton 的；我们的 `0003`–`0005` 要排在 GE-Proton 的补丁后面，GE-Proton 新加了 `0003` 之类同号文件时，把我们的三个顺延改名 |
| `patches/dxvk/`、`patches/vkd3d-proton/` 里 GE-Proton 的文件 | `git checkout --theirs <文件>` |
| `patches/wine-hotfixes/` 里 `we/ge-backports.md` 列出的文件（提前从 GE-Proton 主线搬来的） | `git checkout --theirs <文件>`（GE-Proton 之后又改过它们）；合并完照 `we/ge-backports.md` 最后一节收尾 |

**protonprep 里我们的三处改动**（`git checkout --theirs` 之后都会丢，逐条加回）：

1. 在 `pushd dxvk` 那一段里，`patch -Np1 < ../patches/dxvk/layered-overlay-dxvk.patch` 这一行的前面加：

   ```
       # DW-Proton: report HDR support through NtUserDisplayConfigGetDeviceInfo.
       apply_patch "../patches/dxvk/dw-hdr-detection-via-displayconfig.patch"
   ```

2. 在 `pushd vkd3d-proton` 那一段里，`git clean -xdf` 的下一行加：

   ```
       # WE-Proton's we-* patch changes the nested dxil-spirv submodule, which the
       # reset above doesn't touch
       git submodule foreach --recursive --quiet 'git reset --hard -q HEAD && git clean -xdfq'
   ```

3. 在 `echo "WINE: RUN AUTOCONF TOOLS/MAKE_REQUESTS"` 这一行的前面加：

   ```
   ### (2-8) DW-PROTON AND WE-PROTON PATCHES ###

       # DW-Proton commits that GE-Proton doesn't carry. Generated files
       # (server protocol, syscall tables) are left out of these patches and
       # regenerated below and by Makefile.in.
       echo "WINE: -DW-PROTON- apply DW-Proton patches"
       apply_all_in_dir "../patches/dwproton/"

       echo "WINE: -WE-PROTON- apply WE-Proton patches"
       apply_all_in_dir "../patches/wine/"

   ### END DW-PROTON AND WE-PROTON PATCHES ###

   ```

**检查**：

```
grep -c 'dw-hdr-detection-via-displayconfig' patches/protonprep-valve-staging.sh       # 1
grep -c 'git submodule foreach --recursive --quiet' patches/protonprep-valve-staging.sh # 1
grep -c 'DW-PROTON AND WE-PROTON PATCHES' patches/protonprep-valve-staging.sh          # 2
grep -n 'DW-PROTON AND WE-PROTON\|RUN AUTOCONF' patches/protonprep-valve-staging.sh
```

前三条分别输出 `1`、`1`、`2`。第四条输出三行，形如 `475:### (2-8) DW-PROTON…`、`486:### END DW-PROTON…`、`488:    echo "WINE: RUN AUTOCONF…"`：前两行的行号都要比 `RUN AUTOCONF` 那一行小。

GE-Proton 已经没有 `layered-overlay-dxvk.patch` 这一行、没有 `pushd vkd3d-proton`、或者没有 `RUN AUTOCONF` 这一行时：**停下**。

**Makefile.in 里的 7zip 段**：从合并前的版本取回来，放回 `## PipeWire ALSA PCM plugin` 那一段的前面：

```
git show HEAD:Makefile.in | sed -n '/^## 7zip (from DW-Proton/,/^all-dist: sevenzip$/p'
```

把输出（前面再补一行 `##`，后面补一个空行）插进去。**检查**：`grep -c 'sevenzip' Makefile.in` 和合并前（`git show HEAD:Makefile.in | grep -c sevenzip`）一样。

**子模块版本**（`dxvk`、`vkd3d-proton`、`protonfixes`；合并冲突时，或者合并后 GE-Proton 改了它们的版本时都要看）：

```
git ls-tree HEAD <子模块>          # 我们原来用的提交，记作 OURS
git ls-tree <新GE> <子模块>        # GE-Proton 新版本用的，记作 GE
git -C <子模块> fetch -q origin OURS GE
git -C <子模块> merge-base --is-ancestor OURS GE && echo GE更新
git -C <子模块> merge-base --is-ancestor GE OURS && echo 我们更新
```

- 输出 `GE更新`：用 GE-Proton 的：`git update-index --cacheinfo 160000,GE,<子模块>`
- 输出 `我们更新`：用我们的：`git update-index --cacheinfo 160000,OURS,<子模块>`
- 都没输出（两边分叉）：**停下**

全部冲突解决后：

```
git diff --name-only --diff-filter=U     # 应该没有输出
git commit --no-edit
```

### A3. 重建 Wine 开发分支

```
we/sync/wine-dev.sh
```

几分钟。可能的结果：

| 输出 | 怎么办 |
|---|---|
| `:: done: wine/ is on we-dev (...)` | 继续 A4 |
| `GE-Proton's own patches did not apply` | protonprep 合并时改坏了，或者 GE-Proton 自己有问题。**停下** |
| `git am stopped in patches/dwproton` 或 `git am stopped in patches/wine` | 按下面「git am 停下怎么办」处理，处理完继续 A4 |

### A4. 去掉 GE-Proton 新版本已经带上的 DW-Proton 补丁

```
we/sync/dup-titles.py wine we-base..we-dw
```

**检查**：最后一行是 `0 of N commits ...` 就继续 A5。

有输出时（最后一行不是 `0 of …`），按顺序做：

1. 删掉这些补丁文件（脚本按标题找到 `patches/dwproton/` 里对应的文件并 `git rm`，打印 `removed …`）：

   ```
   we/sync/dup-titles.py --remove wine we-base..we-dw
   ```

2. 对上一步打印的每个标题，运行 `grep -nF -- '<标题>' we/ge-dw-merge-commits.txt` 找到对应的行（行首是 `合入:<四位数>` 或 `移植:<四位数>`），只把行首的 `合入:<四位数>` 或 `移植:<四位数>` 换成 `跳过:GE有同名补丁（<新GE>起）`，后面的哈希和标题不动。
3. 再运行 `we/sync/wine-dev.sh`（和 A3 同一条命令，结果的处理也按 A3 的表）。
4. 再运行 `we/sync/dup-titles.py wine we-base..we-dw`（不带 `--remove`）。最后一行是 `0 of N commits …` 就继续 A5；不是的话，从本节第 1 步起把 1–4 再做一遍，直到是 `0 of N commits …`。

### A5. 导出补丁

```
we/sync/export-patches.py
```

**检查**：

1. 第一行形如 `patches/dwproton: N patches, patches/wine: M patches`。`git -C wine rev-list --count we-base..we-dw` 输出的数等于 N，`git -C wine rev-list --count we-dw..we-dev` 输出的数等于 M。
2. 下面列出的 `removed`、`new`：`patches/dwproton/` 删掉了补丁之后，后面的补丁编号都会往前挪，所以会看到成对的 `removed 0040-xxx` / `new 0039-xxx`（标题相同、编号小了），这是正常的。除此之外，`removed` 的标题只能是 A4 删掉的、或者 `git am --skip` 跳过的补丁；`patches/wine/` 下不应该有 `removed`，`new` 只能是你用 `--new` 加的。不符合就**停下**。

### A6. 构建

名字的格式是 `we-proton-ge<X>-<N>`：

- `<X>`：新 GE 版本号去掉开头的 `GE-Proton`。例如 `GE-Proton11-8` → `11-8`
- `<N>`：上一次发布的号加 1。上一次发布的名字用 `git tag -l 'we-proton-*' | sort -V | tail -1` 看（例如 `we-proton-ge11-7-25` → 上一次是 25，这次是 26）；看不到 tag 就问用户

例：`we-proton-ge11-8-26`。下面的 `<名字>` 都指这个。

```
we/build.sh --name <名字> --jobs 8
```

一个多小时。失败时看最后的输出和 `build/make.log`（`grep -n ': error: \|\*\*\* \[' build/make.log | head`），按下面「构建报错对照」处理。

### A7. 测试

`<名字>` 是 A6 用的名字（例如 `we-proton-ge11-8-26`）。

```
we/tests/build.sh
we/tests/regress.sh build/<名字> build/regress-<名字>
ls -d we/tests/regress-we-proton-* | sort -V | tail -1        # 上一版的基线，记作 <基线>
we/tests/regress.sh --compare <基线> build/regress-<名字>
```

**检查**：全是 `same`。有 `different`：把差异贴给用户，**停下**。

通过之后，把这次的结果存成新的基线：

```
mkdir -p we/tests/regress-<名字> && cp build/regress-<名字>/*.txt we/tests/regress-<名字>/ && rm -f we/tests/regress-<名字>/stderr.log
```

### A8. 更新文档并提交

```
grep -rn '<旧GE>' README.md README.en.md we/README.md we/dist-README.md
```

把这些地方说的「当前基于的 GE-Proton 版本」改成 `<新GE>`（`we/ge-dw-merge.md` 是当时的记录，不要改）。然后：

```
git add -A patches we README.md README.en.md
git status --short | grep -v '^ [mM] '     # 去掉子模块的行（行首 " m"/" M"），看一遍要提交的文件
git commit -m "Follow <新GE>."
```

提交信息按 `AGENTS.md`：英文，结尾加 `Co-Authored-By` 行。之后告诉用户：构建结果在 `build/`、测试结果、哪些 DW-Proton 补丁被去掉了。发布等用户决定（见 `we/README.md` 的「发布」）。

## B. 跟进 DW-Proton 的新版本

### B1. 看有没有新版本

```
cat we/dw-proton-version
git ls-remote --tags dwproton 'dwproton-*' | grep -v '\^{}' | awk -F/ '{print $3}' | sort -V | tail -3
```

**检查**：有比 `we/dw-proton-version` 新的版本，记作 `<新DW>`，旧的记作 `<旧DW>`。

### B2. 找出 DW-Proton 新增的 Wine 提交

```
git fetch dwproton tag <旧DW> --no-tags
git fetch dwproton tag <新DW> --no-tags
OLD=$(git ls-tree <旧DW> wine | awk '{print $3}')
NEW=$(git ls-tree <新DW> wine | awk '{print $3}')
git -C ~/src/wine-dwproton fetch origin
git -C ~/src/wine-dwproton merge-base --is-ancestor $OLD $NEW && echo 可以
```

**检查**：输出 `可以`。没有输出（DW-Proton 换了 Wine 的底座，历史不连续）：**停下**。

```
git -C ~/src/wine-dwproton log --reverse --topo-order --no-merges --format='%h %s' $OLD..$NEW > build/dw-new.txt
wc -l build/dw-new.txt
we/sync/dup-titles.py ~/src/wine-dwproton $OLD..$NEW > build/dw-dup.txt; tail -1 build/dw-dup.txt
```

### B3. 逐个决定要不要

先运行 `we/sync/wine-dev.sh`，得到 `wine/` 的 `we-base` 分支（下面「撤销提交怎么查」要用它）。

对 `build/dw-new.txt` 的每一行（按顺序），按表格的顺序一行一行检查，采用**第一条符合**的规则，后面的行不再看。改了哪些文件用这条命令看：

```
git -C ~/src/wine-dwproton show --stat --format=%s <提交>
```

| 情况 | 处理 |
|---|---|
| 这个提交出现在 `build/dw-dup.txt` 里 | 跳过（GE-Proton 已有） |
| 标题里有 `CachyOS` | 跳过 |
| 标题以 `Revert "` 开头 | 见表下面的「撤销提交怎么查」 |
| 上面那条 `show --stat` 列出的文件里有 `dlls/winegstreamer/` 或 `dlls/winedmo/` 开头的，或者标题里有 `media-converter` | 跳过（视频用 GE-Proton 的方案） |
| 列出的文件里有 `dlls/mf/`、`dlls/mfreadwrite/`、`dlls/evr/`、`dlls/mfplat/`、`dlls/quartz/` 开头的 | **停下**（视频相关，要判断） |
| 其他 | 要（B4 打上） |

撤销提交怎么查：标题形如 `Revert "<原标题>"`。`<原标题>` 是 `Revert "` 后面、到最后一个 `"` 为止的部分，例如 `Revert "win32u: Clear WS_EX_TOOLWINDOW."` 的原标题是 `win32u: Clear WS_EX_TOOLWINDOW.`。运行

```
git -C wine log --oneline --fixed-strings --grep='<原标题>' we-base
```

有输出：GE-Proton 带着被撤销的那个提交，撤销它会删掉 GE-Proton 的东西，跳过。没有输出：**停下**。

把决定写进 `build/dw-plan.txt`，每行 `要|跳过:<原因> <提交> <标题>`。

### B4. 打上要的提交

```
we/sync/wine-dev.sh                     # 先得到干净的 we-base/we-dw/we-dev
git -C wine fetch ~/src/wine-dwproton $NEW
git -C wine checkout we-dw
git -C wine cherry-pick <提交>          # 对 build/dw-plan.txt 里每个「要」的提交，按顺序
```

`cherry-pick` 冲突时（`git -C wine status` 看冲突的文件）：

| 冲突在哪里 | 处理 |
|---|---|
| 只在 `include/wine/server_protocol.h`、`server/request_handlers.h`、`server/request_trace.h` | 运行下面这一整行（括号里先 `cd wine`，所以 `tools/make_requests` 是 `wine/tools/make_requests`）：`(cd wine && git checkout --ours include/wine/server_protocol.h server/request_handlers.h server/request_trace.h && tools/make_requests && git add -A && git cherry-pick --continue --no-edit)` |
| 在 `dlls/winewayland.drv/` 里 | `git -C wine cherry-pick --abort`，这个提交改记成 `跳过:冲突（Wayland 用 GE-Proton 的）` |
| 其他 | `git -C wine cherry-pick --abort`，**停下**（要移植，需要判断）。把提交和冲突的文件告诉用户 |

没有冲突的 `cherry-pick` 会自己完成提交，直接运行下一个提交的 `cherry-pick`。所有「要」的提交都处理完后，把 WE-Proton 的补丁接上去：

```
git -C wine checkout -B we-dev we-dw
git -C wine am -3 ../patches/wine/*.patch
```

`git am` 停下：**停下**（WE-Proton 的补丁和新的 DW-Proton 提交冲突，要判断）。

### B5. DW-Proton 顶层的变化

```
git diff --stat <旧DW> <新DW> -- patches/protonfixes patches/dxvk
for s in dxvk vkd3d-proton protonfixes; do echo "$s $(git ls-tree <旧DW> $s | awk '{print $3}') $(git ls-tree <新DW> $s | awk '{print $3}')"; done
```

- `patches/protonfixes/` 有变化：**停下**（要看是不是游戏修复，再决定怎么放进 `patches/protonfixes/0003` 之后）
- `patches/dxvk/0001-dxvk-Implement-support-for-HDR-detection-via-NtUserD.patch` 有变化：`git show <新DW>:patches/dxvk/0001-dxvk-Implement-support-for-HDR-detection-via-NtUserD.patch > patches/dxvk/dw-hdr-detection-via-displayconfig.patch`。DW-Proton 的这个文件改了名字或删掉了：**停下**
- 子模块版本变了（某一行的两个哈希不同）：拿 DW-Proton 新版本用的提交（第二个哈希）和我们现在用的（`git ls-tree HEAD <子模块>`）比，做法和 A2 的「子模块版本」一样，只是把那里的 GE 换成这里的 DW-Proton 新提交：DW-Proton 的更新（我们的提交是它的祖先）就换成它的；我们的更新就不动；分叉就**停下**

### B6. 导出、构建、测试

和 A5、A6、A7 一样。

### B7. 记录并提交

1. `echo <新DW> > we/dw-proton-version`
2. 在 `we/ge-dw-merge-commits.txt` 末尾加一段：

   ```
   # <新DW>：<OLD 前 11 位>..<NEW 前 11 位>
   合入:<编号> <提交> <标题>          # 每个打上的
   跳过:<原因> <提交> <标题>          # 每个跳过的
   ```

   编号就是 `patches/dwproton/` 里的新编号（导出后 `ls patches/dwproton | tail` 能看到）。
3. 在 `we/ge-dw-merge.md` 开头的「DW-Proton：」那一行后面补上 `，之后跟进到 <新DW>`。
4. 提交（同 A8）。

## C. 加 WE-Proton 自己的新补丁

在 `wine/` 的 `we-dev` 分支上改代码、提交（Wine 的提交风格，见 `AGENTS.md`），然后：

```
we/sync/export-patches.py --new <编号>
```

编号要先和用户（以及别的会话）确认没人占用。报告、测试、三张表照 `AGENTS.md` 写。

## git am 停下怎么办

`we/sync/wine-dev.sh` 用 `git am -3` 打补丁，打不上会停下。先看：

```
git -C wine am --show-current-patch=diff | head -20      # 哪个补丁
git -C wine status --short | grep '^UU\|^AA\|^DU\|^UD'   # 冲突的文件
```

先看停在哪一层：输出是 `git am stopped in patches/dwproton`，`wine/` 在 `we-dw` 分支上；输出是 `git am stopped in patches/wine`，`wine/` 在 `we-dev` 分支上。

**停在 `patches/dwproton`**：

| 情况 | 处理 |
|---|---|
| GE-Proton 已经有同名补丁：用下面两条命令查，第二条有输出就是 | `git -C wine am --skip` |
| 冲突全在 `dlls/winewayland.drv/` 里 | `git -C wine am --skip`（Wayland 用 GE-Proton 的） |
| 其他 | `git -C wine am --abort`，**停下** |

查同名补丁的两条命令：

```
git -C wine am --show-current-patch=raw | sed -n '/^Subject:/{N;p}'       # 标题（可能折成两行）
grep -rlF -- '<标题里冒号后面的前 30 个字>' patches wine-staging/patches | grep -v 'patches/dwproton/\|patches/wine/'
```

跳过的补丁在 `we/ge-dw-merge-commits.txt` 里改成 `跳过:<原因>（<新GE>起）`。

`git am --skip` 之后它会接着打剩下的；又停下就再按上表处理。全部打完后：

```
git -C wine checkout -B we-dev we-dw
git -C wine am -3 ../patches/wine/*.patch
```

**停在 `patches/wine`**：`git -C wine am --abort`，**停下**。WE-Proton 的补丁不能跳过，要移植。

## 构建报错对照

先找出错的地方：`grep -n ': error: \|\*\*\* \[\|Error [0-9]' build/make.log | head`。

| 报错 | 原因 | 处理 |
|---|---|---|
| `redefinition of '<名字>'`、`conflicting types for '<名字>'`、`duplicate member '<名字>'` | 某个 DW-Proton 补丁和 GE-Proton 的实现重复 | `git -C wine log --format='%h %s' -S'<名字>' we-base..we-dw` 找出是哪个 DW-Proton 提交加的；在 `patches/dwproton/` 里按标题找到它的文件，`git rm` 掉，记进 `we/ge-dw-merge-commits.txt`，回到 A3 |
| `-Werror` 引起的错误（`[-Werror=...]`），在 DW-Proton 或 WE-Proton 补丁加的代码里 | 原补丁有警告，GE-Proton 的构建开了 `--enable-werror` | 见下面「修 -Werror 错误」。不确定怎么改就**停下** |
| `src-xz/build-aux/missing: No such file or directory` | 在用过的 `build/obj` 里又编了一次 | `we/build.sh` 每次都会清空 `build/obj`；手动 `make` 时才会碰到。删掉 `build/obj` 重来 |
| `some patches did not apply` | 打补丁失败 | 看 `build/protonprep.log` 里 `FAILED`、`error:` 附近是哪个补丁。属于 `patches/dwproton` 或 `patches/wine`：先跑 A3（`we/sync/wine-dev.sh`）看能不能复现。其他：**停下** |
| 其他 | | **停下** |

### 修 -Werror 错误

只改最小的地方（例如格式串换成和类型对应的、把声明挪到语句前面），不改逻辑。

```
git -C wine log --format='%h %s' -S'<出错那一行里的一段代码>' we-base..we-dev   # 找出是哪个提交加的，记作 <提交>
# 在 wine/ 里改好文件（wine/ 此时在 we-dev 上）
git -C wine commit -a --fixup=<提交>
(cd wine && GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash we-base)
n=$(ls patches/dwproton/*.patch | wc -l)
git -C wine branch -f we-dw $(git -C wine rev-list --reverse we-base..we-dev | sed -n "${n}p")
we/sync/export-patches.py
```

**检查**：`export-patches.py` 只报告 `changed` 了那一个补丁。然后回到 A6 重新构建。

