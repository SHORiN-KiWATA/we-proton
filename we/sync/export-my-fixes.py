#!/usr/bin/env python3
"""把本次的两处 Wine 改动导出成 WE-Proton 规范里的补丁文件。

wine/ 子模块处于“已打 GE/dw/WE 补丁但未提交”的状态，直接 git diff 会把别人的
改动一起带出来。这里用“反向移除自己插入的片段”得到改动前的版本，再 diff。

产物：
  patches/wine/0057-kernel32-Implement-heap-list-snapshots.patch      （正式修复）
  we/fixes/0057-kernel32-heap-list-snapshot/diag/0001-diag-priv-instr-logging.patch （诊断）
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # we/
REPO = os.path.dirname(ROOT)
WINE = os.path.join(REPO, "wine")

# ---------------------------------------------------------------- toolhelp.c
TOOLHELP_ADDED = [
    # 1) struct snapshot 里的堆字段
    """    int         heap_count;
    int         heap_pos;
    int         heap_offset;
""",
    # 2) fetch_heap + fill_heap + CreateToolhelp32Snapshot 头部改动
    """/* 取指定进程的堆列表。RtlGetProcessHeaps 只能枚举当前进程的堆，
 * 枚举其它进程的堆需要读取对方进程内部结构，这里暂不支持（返回 0 个堆）。 */
static ULONG fetch_heap( DWORD process, HANDLE **heaps )
{
    ULONG   count;
    HANDLE  *local;

    *heaps = NULL;
    if (process && process != GetCurrentProcessId()) return 0;

    count = RtlGetProcessHeaps( 0, NULL );
    if (!count) return 0;
    local = HeapAlloc( GetProcessHeap(), 0, count * sizeof(HANDLE) );
    if (!local) return 0;
    count = RtlGetProcessHeaps( count, local );
    *heaps = local;
    return count;
}

static void fill_heap( struct snapshot* snap, ULONG* offset, HANDLE* heaps, ULONG num )
{
    HEAPLIST32*         hl;
    ULONG               i;

    snap->heap_count = num;
    snap->heap_pos = 0;
    if (!num) return;
    snap->heap_offset = *offset;

    hl = (HEAPLIST32*)&snap->data[*offset];
    for (i = 0; i < num; i++)
    {
        hl->dwSize = sizeof(HEAPLIST32);
        hl->th32ProcessID = GetCurrentProcessId();
        hl->th32HeapID = (ULONG_PTR)heaps[i];
        /* RtlGetProcessHeaps 把进程默认堆放在第一个 */
        hl->dwFlags = (i == 0) ? HF32_DEFAULT : 0;
        hl++;
    }
    *offset += num * sizeof(HEAPLIST32);
}

""",
]

# (改动后 -> 改动前) 的逐处替换，顺序执行
TOOLHELP_REWRITES = [
    # 允许 SNAPHEAPLIST
    ("|TH32CS_SNAPMODULE|TH32CS_SNAPHEAPLIST)))", "|TH32CS_SNAPMODULE)))"),
    # 声明 heaps / num_heap
    ("""    LDR_DATA_TABLE_ENTRY *mod = NULL;
    HANDLE              *heaps = NULL;
    ULONG               num_pcs, num_thd, num_mod, num_heap = 0;""",
     """    LDR_DATA_TABLE_ENTRY *mod = NULL;
    ULONG               num_pcs, num_thd, num_mod;"""),
    # 段大小
    ("""        if (flags & TH32CS_SNAPHEAPLIST)
            sect_size += (num_heap = fetch_heap( process, &heaps )) * sizeof(HEAPLIST32);""",
     """        if (flags & TH32CS_SNAPHEAPLIST)FIXME("Unimplemented: heap list snapshot\\n");"""),
    # 填充
    ("""            fill_thread( snap, &offset, spi, num_thd );
            fill_heap( snap, &offset, heaps, num_heap );""",
     """            fill_thread( snap, &offset, spi, num_thd );"""),
    # 释放
    ("""    }
    HeapFree( GetProcessHeap(), 0, heaps );

    while (num_mod--)""",
     """    }

    while (num_mod--)"""),
    # next_heap + Heap32ListFirst/Next 实现 -> 原来的 stub
    ("""static BOOL next_heap( HANDLE hSnapShot, LPHEAPLIST32 lphl, BOOL first )
{
    struct snapshot*    snap;
    BOOL                ret = FALSE;

    if (lphl->dwSize < sizeof(HEAPLIST32))
    {
        SetLastError( ERROR_INSUFFICIENT_BUFFER );
        WARN("Result buffer too small (%Iu)\\n", lphl->dwSize);
        return FALSE;
    }
    if ((snap = MapViewOfFile( hSnapShot, FILE_MAP_ALL_ACCESS, 0, 0, 0 )))
    {
        if (first) snap->heap_pos = 0;
        if (snap->heap_pos < snap->heap_count)
        {
            LPHEAPLIST32 hl = (LPHEAPLIST32)&snap->data[snap->heap_offset];
            *lphl = hl[snap->heap_pos++];
            ret = TRUE;
        }
        else SetLastError( ERROR_NO_MORE_FILES );
        UnmapViewOfFile( snap );
    }
    return ret;
}

/************************************************************************
 *              Heap32ListFirst (KERNEL32.@)
 *
 * Return info about the "first" heap in a toolhelp32 snapshot
 */
BOOL WINAPI Heap32ListFirst(HANDLE hSnapshot, LPHEAPLIST32 lphl)
{
    return next_heap( hSnapshot, lphl, TRUE );
}

/************************************************************************
 *              Heap32ListNext (KERNEL32.@)
 *
 * Return info about the "next" heap in a toolhelp32 snapshot
 */
BOOL WINAPI Heap32ListNext(HANDLE hSnapshot, LPHEAPLIST32 lphl)
{
    return next_heap( hSnapshot, lphl, FALSE );
}""",
     """/************************************************************************
 *              Heap32ListFirst (KERNEL32.@)
 *
 */
BOOL WINAPI Heap32ListFirst(HANDLE hSnapshot, LPHEAPLIST32 lphl)
{
    FIXME(": stub\\n");
    return FALSE;
}"""),
]

SPEC_CHANGES = [("@ stdcall Heap32ListNext(long ptr)", "@ stub Heap32ListNext")]

# ------------------------------------------------------- signal_x86_64.c（诊断）
SIGNAL_REWRITES = [
    ("static inline DWORD is_privileged_instr_raw( CONTEXT *context )",
     "static inline DWORD is_privileged_instr( CONTEXT *context )"),
    ("""/* diag: 判定为特权指令时打印实际字节（排查加壳/反作弊代码的模拟缺口）
 * 注意：本函数在信号处理器里被调用，不能用 ERR()/fprintf 这类带锁的参数，
 * 只能用 write() 这类异步信号安全的调用。 */
static inline DWORD is_privileged_instr( CONTEXT *context )
{
    DWORD ret = is_privileged_instr_raw( context );
    if (ret == EXCEPTION_PRIV_INSTRUCTION)
    {
        static const char hex[] = "0123456789abcdef";
        BYTE instr[16];
        char buf[160];
        int n = 0;
        unsigned int i, len = virtual_uninterrupted_read_memory( (BYTE *)context->Rip, instr, sizeof(instr) );
        n += sprintf( buf + n, "priv-instr diag: eip=%08x len=%u bytes: ", (unsigned int)context->Rip, len );
        for (i = 0; i < len && n < 140; i++)
        {
            buf[n++] = hex[instr[i] >> 4];
            buf[n++] = hex[instr[i] & 15];
            buf[n++] = ' ';
        }
        buf[n++] = '\\n';
        write( 2, buf, n );
    }
    return ret;
}

""", ""),
]


def reverse_transform(path, rewrites, removes=()):
    """返回 (原文件内容, 改动后内容)"""
    with open(path, encoding="utf-8") as f:
        new = f.read()
    old = new
    for added in removes:
        if added not in old:
            sys.exit(f"找不到要移除的片段（{path}）：\n{added[:80]}...")
        old = old.replace(added, "", 1)
    for a, b in rewrites:
        if a not in old:
            sys.exit(f"找不到要还原的片段（{path}）：\n{a[:120]}...")
        old = old.replace(a, b, 1)
    return old, new


def make_patch(files, out):
    """files: [(相对路径, 改动前内容, 改动后内容)]"""
    tmp = tempfile.mkdtemp()
    chunks = []
    for rel, old, new in files:
        a = os.path.join(tmp, "a")
        b = os.path.join(tmp, "b")
        for p, txt in ((a, old), (b, new)):
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8") as f:
                f.write(txt)
        d = subprocess.run(["diff", "-u", "--label", f"a/{rel}", "--label", f"b/{rel}", a, b],
                           capture_output=True, text=True).stdout
        chunks.append(d)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write("".join(chunks))
    print(f"写入 {out}（{sum(c.count(chr(10)) for c in chunks)} 行）")


def main():
    th_path = os.path.join(WINE, "dlls/kernel32/toolhelp.c")
    spec_path = os.path.join(WINE, "dlls/kernel32/kernel32.spec")
    sig_path = os.path.join(WINE, "dlls/ntdll/unix/signal_x86_64.c")

    th_old, th_new = reverse_transform(th_path, TOOLHELP_REWRITES, TOOLHELP_ADDED)

    with open(spec_path, encoding="utf-8") as f:
        spec_new = f.read()
    spec_old = spec_new
    for a, b in SPEC_CHANGES:
        if a not in spec_old:
            sys.exit("spec 里找不到 Heap32ListNext 的改动")
        spec_old = spec_old.replace(a, b, 1)

    make_patch(
        [("dlls/kernel32/toolhelp.c", th_old, th_new),
         ("dlls/kernel32/kernel32.spec", spec_old, spec_new)],
        os.path.join(REPO, "patches/wine/0057-kernel32-Implement-heap-list-snapshots.patch"),
    )

    sig_old, sig_new = reverse_transform(sig_path, SIGNAL_REWRITES)
    make_patch(
        [("dlls/ntdll/unix/signal_x86_64.c", sig_old, sig_new)],
        os.path.join(ROOT, "fixes/0057-kernel32-heap-list-snapshot/diag/0001-diag-priv-instr-logging.patch"),
    )


if __name__ == "__main__":
    main()
