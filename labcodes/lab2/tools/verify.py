#!/usr/bin/env python3
"""Build and boot the real RISC-V kernel; timeout is expected for its idle loop."""
from pathlib import Path
import os, subprocess, sys, zipfile, hashlib
root = Path(__file__).resolve().parents[1]
evidence = root / 'report' / 'logs'
evidence.mkdir(exist_ok=True)
os.chdir(root)
prefix = os.environ.get('GCCPREFIX', 'riscv64-elf-')
build = subprocess.run(['make', 'GCCPREFIX='+prefix, '-j4'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(evidence/'build.log').write_bytes(build.stdout)
if build.returncode: sys.exit(build.returncode)
for ram in (8, 32, 64, 128, 256):
    cmd = ['qemu-system-riscv64','-machine','virt','-m',str(ram),'-nographic','-bios','default','-kernel','bin/ucore.img']
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try: output = p.communicate(timeout=2)[0]
    except subprocess.TimeoutExpired:
        p.terminate(); output = p.communicate()[0]
    (evidence/f'kernel-{ram}M.log').write_bytes(output)
    for marker in (b'check_alloc_page() succeeded!', b'[Buddy]', b'[SLUB]', b'[Lab2] all kernel checks completed'):
        assert marker in output, (ram, marker, output.decode(errors='replace'))
    assert b'kernel panic' not in output
    print(f'{ram} MiB: actual QEMU boot and all kernel self-checks PASS')
archive = root/'lab2-original.zip'
with zipfile.ZipFile(archive) as z:
    original = z.read('lab2_new/kern/mm/best_fit_pmm.c')
current = (root/'kern/mm/best_fit_pmm.c').read_bytes()
marker = b'// Framework tests:'
assert original[original.index(marker):] == current[current.index(marker):]
for file in ('tools/test_lab2.c', 'tools/test_lab2.h', 'tools/inject.py'):
    with zipfile.ZipFile(archive) as z: assert z.read('lab2_new/'+file) == (root/file).read_bytes()
print('Official Best-Fit test functions, grading assertions and injector: unchanged')
(evidence/'original-sha256.txt').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  lab2-original.zip\n')
