#!/usr/bin/env python3
from pathlib import Path
import os, subprocess
root = Path(__file__).resolve().parents[1]
os.chdir(root)
ev = root/'report'/'logs'
subprocess.run(['make','GCCPREFIX='+os.environ.get('GCCPREFIX','riscv64-elf-'),'-j4'],check=True)
with (ev/'gdb-serial.log').open('w') as serial:
 p = subprocess.Popen(['qemu-system-riscv64','-machine','virt','-nographic','-bios','default','-kernel','bin/ucore.img','-S','-gdb','tcp:127.0.0.1:12348'],stdout=serial,stderr=subprocess.STDOUT)
 cmds=['set pagination off','set confirm off','file bin/kernel','target remote 127.0.0.1:12348','hbreak kern_init','continue','info registers pc satp','x/1gx (char *)&boot_page_table_sv39+16','x/1gx (char *)&boot_page_table_sv39+4088','disassemble /r kern_entry','hbreak slub_check','continue','info registers pc satp','p/x va_pa_offset','p/x npage','p/x pages','detach','quit']
 args=[os.environ.get('GCCPREFIX','riscv64-elf-')+'gdb','-q','-batch']
 for c in cmds: args += ['-ex',c]
 try:
  r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
  (ev/'guest-gdb.log').write_bytes(r.stdout)
  print(r.stdout.decode())
  assert r.returncode == 0 and b'Breakpoint 2, slub_check' in r.stdout
 finally: p.terminate();p.wait()
