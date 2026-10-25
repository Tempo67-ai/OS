#!/usr/bin/env python3
from pathlib import Path
import subprocess,sys
r=Path(__file__).resolve().parents[1]
name=sys.argv[1] if len(sys.argv)>1 else 'mmu'
assert name in ('mmu','tlb')
args=['xcrun','lldb','--batch','-s',str(r/'debug'/f'{name}.lldb'),'--',str(r/'debug/build/riscv64-softmmu/qemu-system-riscv64'),'-machine','virt','-nographic','-bios',str(r/'debug/qemu-4.1.1/pc-bios/opensbi-riscv64-virt-fw_jump.bin'),'-kernel',str(r/'bin/ucore.img')]
p=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
(r/'report'/'logs'/f'qemu4-{name}-lldb.log').write_bytes(p.stdout)
print(p.stdout.decode());sys.exit(p.returncode)
