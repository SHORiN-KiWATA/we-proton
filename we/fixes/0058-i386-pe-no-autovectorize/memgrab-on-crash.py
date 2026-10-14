#!/usr/bin/env python3
"""作为祖先进程拉起 gamescope+客户端；客户端崩溃并被 ping 调试器挂住后，读它的内存。"""
import os, re, subprocess, sys, time
S = os.path.dirname(os.path.abspath(__file__))
inner = os.path.join(S, 'inner.sh')
log = os.path.expanduser('~/Projects/closers-emu/run/steam-0.log')
gs = subprocess.Popen(['gamescope', '--backend', 'headless', '-W', '1920', '-H', '1080', '--', inner],
                      stdout=open(os.path.join(S, 'gs.log'), 'w'), stderr=subprocess.STDOUT)
def find_cw():
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try: c = open(f'/proc/{p}/cmdline', 'rb').read()
        except OSError: continue
        if c.startswith(b'./CW.EXE'): return int(p)
crash = None
for _ in range(int(sys.argv[1]) if len(sys.argv) > 1 else 60):
    time.sleep(1)
    try: t = open(log, 'rb').read()
    except OSError: continue
    m = re.search(rb'Unhandled page fault on (\w+) access to ([0-9A-F]+) at address ([0-9A-F]+)', t)
    if m: crash = m; break
pid = find_cw()
print('crash:', crash and crash.group(0).decode(), 'pid:', pid)
if crash and pid:
    time.sleep(1)
    eip = int(crash.group(3), 16)
    maps = open(f'/proc/{pid}/maps').read().splitlines()
    open(os.path.join(S, 'maps.txt'), 'w').write('\n'.join(maps))
    mem = open(f'/proc/{pid}/mem', 'rb')
    out = {}
    for line in maps:
        a, b = [int(x, 16) for x in line.split()[0].split('-')]
        perm = line.split()[1]
        if b > 0x100000000 or 'r' not in perm: continue
        if a <= eip < b or (b - a) <= 0x400000 and 0x02000000 <= a < 0x10000000:
            try:
                mem.seek(a); out[a] = mem.read(b - a)
            except OSError as e: pass
    import pickle
    pickle.dump({'eip': eip, 'regions': out}, open(os.path.join(S, 'dump.pkl'), 'wb'))
    print('dumped', len(out), 'regions', sum(map(len, out.values())), 'bytes')
subprocess.run([os.path.expanduser('~/Documents/github/we-proton/build/test-heap/files/bin/wineserver'), '-k'],
               env=dict(os.environ, WINEPREFIX=os.path.expanduser('~/Projects/closers-emu/run/prefix/pfx')))
time.sleep(2); gs.kill()
