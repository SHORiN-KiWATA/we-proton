#!/usr/bin/env python3
"""扫描 32 位 PE DLL 的导出函数开头，找出 SSE2/SSSE3 独有的操作码。

背景：部分加壳器（2017 前后的版本）会把被保护程序导入的 API 开头若干条指令
复制到自己的页里再混淆（code splicing）。它的指令长度表不认识一部分 SSE2 独有的
操作码（例如 66 0F 6C punpcklqdq：MMX 里不存在 0F 6C，于是被当成没有 ModRM 的指令），
复制出来的代码会整体错位 → 崩溃。真 Windows 的系统 DLL 由 MSVC 编译，函数开头几乎
不会出现这些指令；Wine 的 i386 DLL 用 gcc -O2 -march=nocona 编译，SLP 向量化会产生它们。

用法：
    pe-sse-prologue-scan.py --dll <path.dll> [--dll ...] [--bytes 96] [--show]
    pe-sse-prologue-scan.py --dir <i386-windows 目录> [--only user32,gdi32,...]
依赖：capstone（pip install capstone）
"""
import argparse, os, struct, sys

# 只在带 66/F2/F3 前缀的 SSE2+ 中存在、MMX/SSE1 里没有的双字节操作码（0F xx）
RISKY_0F = {0x6C, 0x6D, 0xD6, 0x7C, 0x7D, 0xE6, 0x78, 0x79}
RISKY_3BYTE = {0x38, 0x3A}  # 0F 38 xx / 0F 3A xx（SSSE3/SSE4）


def pe_exports(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', data, pe+4)[0] != 0x14c:
        return None, None
    opt = pe + 24
    ib = struct.unpack_from('<I', data, opt+28)[0]
    exp_rva, exp_size = struct.unpack_from('<II', data, opt+96)
    ns = struct.unpack_from('<H', data, pe+6)[0]
    so = struct.unpack_from('<H', data, pe+20)[0]
    secs = [struct.unpack_from('<8sIIII', data, opt+so+i*40) for i in range(ns)]

    def off(r):
        for _, vs, va, rs, ro in secs:
            if va <= r < va + max(vs, rs):
                return r - va + ro
        return None
    if not exp_rva:
        return ib, []
    nf, nn, af, an, ao = struct.unpack_from('<IIIII', data, off(exp_rva)+20)
    names = {}
    for i in range(nn):
        nr = struct.unpack_from('<I', data, off(an)+4*i)[0]
        o = struct.unpack_from('<H', data, off(ao)+2*i)[0]
        s = off(nr)
        names[o] = data[s:data.index(b'\0', s)].decode()
    out = []
    for i in range(nf):
        r = struct.unpack_from('<I', data, off(af)+4*i)[0]
        if not r or exp_rva <= r < exp_rva + exp_size:  # 空项 / 转发
            continue
        o = off(r)
        if o is not None:
            out.append((names.get(i, '#%d' % i), r, o))
    return ib, out


def scan(path, nbytes, show):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    data = open(path, 'rb').read()
    ib, exps = pe_exports(data)
    if exps is None:
        return 0, 0
    hits = 0
    for name, rva, o in exps:
        found = None
        for ins in md.disasm(data[o:o+nbytes], ib + rva):
            b = bytes(ins.bytes)
            i = 0
            while i < len(b) and b[i] in (0x66, 0xF2, 0xF3):
                i += 1
            if i < len(b) - 1 and b[i] == 0x0F and (b[i+1] in RISKY_0F or b[i+1] in RISKY_3BYTE):
                found = ins
                break
            if ins.mnemonic in ('ret', 'retn', 'jmp'):
                break
        if found:
            hits += 1
            if show:
                print(f'  {os.path.basename(path)}!{name} +{found.address-ib-rva:#x}: '
                      f'{bytes(found.bytes).hex()} {found.mnemonic} {found.op_str}')
    return len(exps), hits


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dll', action='append', default=[])
    ap.add_argument('--dir')
    ap.add_argument('--only', help='逗号分隔的模块名（不带 .dll）')
    ap.add_argument('--bytes', type=int, default=96, help='每个函数最多看多少字节（默认 96）')
    ap.add_argument('--show', action='store_true', help='列出命中的函数')
    a = ap.parse_args()
    paths = list(a.dll)
    if a.dir:
        only = set(a.only.split(',')) if a.only else None
        for f in sorted(os.listdir(a.dir)):
            if f.endswith('.dll') and (not only or f[:-4].lower() in only):
                paths.append(os.path.join(a.dir, f))
    tot = th = 0
    for p in paths:
        n, h = scan(p, a.bytes, a.show)
        tot += n; th += h
        if h:
            print(f'{os.path.basename(p)}: {h}/{n} 个导出函数开头含 SSE2 独有操作码')
    print(f'合计: {th}/{tot}')


if __name__ == '__main__':
    main()
