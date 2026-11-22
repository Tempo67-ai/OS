#!/usr/bin/env python3
"""Reconstruction self-checks, NOT an official course grader. No network access."""
from pathlib import Path
import hashlib,json,os,platform,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
LOG=ROOT/'report'/'logs'
LOG.mkdir(parents=True,exist_ok=True)
MAKE=os.environ.get('MAKE') or shutil.which('gmake') or 'make'
PREFIX=os.environ.get('GCCPREFIX','riscv64-elf-')
QEMU=os.environ.get('QEMU','qemu-system-riscv64')
REQUIRED=[
 'Sv39 kernel 4K mappings: PASS',
 'check_pgdir() succeeded! refcounts, replace, remove, hardware TLB: PASS',
 'check_kmalloc(): slab reuse and reclamation PASS',
 'vmm_init(): ordered VMA, cache, gaps, destruction PASS',
 'proc_init(): idle=0 init=1, first-run frame PASS',
 'initproc pid=1 parent=0: Hello kernel thread',
 'thread context + first return + yields + exit/reap: PASS',
 'resource baseline restored (except idle PCB slab): PASS',
 'timer interrupts during threaded kernel ticks=',
 'LAB4 SELF CHECKS PASSED',
]
def cmdlog(cmd,path):
    p=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    path.write_bytes(p.stdout)
    if p.returncode:
        print(p.stdout.decode(errors='replace')[-6000:]);raise RuntimeError(f'command failed: {cmd}')
    return p

def run_boot(label,memory,extra=False):
    cmd=[QEMU,'-machine','virt','-m',f'{memory}M','-smp','1','-nographic','-bios','default',
         '-kernel','bin/ucore.img','-monitor','none','-no-reboot']
    start=time.monotonic()
    try:
        p=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
        data=p.stdout;rc=p.returncode
    except subprocess.TimeoutExpired as exc:
        data=exc.stdout or b'';rc=-1
    (LOG/f'{label}.log').write_bytes(data)
    text=data.decode(errors='replace');required=list(REQUIRED)
    if extra:required+=['Page table allocation failure rollback: PASS','fork failure cleanup and idle fallback: PASS']
    missing=[s for s in required if s not in text]
    good=rc==0 and not missing and 'PANIC' not in text
    result=dict(label=label,ram_mib=memory,returncode=rc,passed=good,
                seconds=round(time.monotonic()-start,3),missing=missing,
                image_sha256=hashlib.sha256((ROOT/'bin/ucore.img').read_bytes()).hexdigest(),command=cmd)
    print(f'{label}: {"PASS" if good else "FAIL"}',flush=True)
    if not good: print(text[-7000:])
    return result

def main():
    env=dict(date=time.strftime('%Y-%m-%d %H:%M:%S %z'),host=platform.platform(),
             compiler=subprocess.check_output([PREFIX+'gcc','--version'],text=True).splitlines()[0],
             qemu=subprocess.check_output([QEMU,'--version'],text=True).splitlines()[0])
    results=[]
    for opt,extra,memories in [('-O2',False,[32,128,256]),('-O2',True,[32,128]),('-O0',True,[128])]:
        tag=('selftest' if extra else 'normal')+'-'+opt[1:]
        cmdlog([MAKE,'clean'],LOG/f'{tag}-clean.log')
        cmdlog([MAKE,'-j4',f'GCCPREFIX={PREFIX}',f'OPT={opt}',f'DEFS={"-DLAB4_SELFTEST" if extra else ""}'],LOG/f'{tag}-build.log')
        for mem in memories: results.append(run_boot(f'{tag}-{mem}M',mem,extra))
    summary=dict(environment=env,official_grader=False,results=results,passed=all(x['passed'] for x in results))
    (LOG/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
    print(f'Self-check matrix: {sum(x["passed"] for x in results)}/{len(results)} passed (not official grading)')
    if not summary['passed']:return 1
    # Leave the ordinary O2 image ready for make qemu; verify that final image too.
    cmdlog([MAKE,'clean'],LOG/'final-clean.log')
    cmdlog([MAKE,'-j4',f'GCCPREFIX={PREFIX}','OPT=-O2','DEFS='],LOG/'final-build.log')
    final=run_boot('final-normal-O2-128M',128)
    summary['final_build']=final;summary['passed'] &= final['passed']
    (LOG/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
    return 0 if summary['passed'] else 1
if __name__=='__main__':sys.exit(main())
