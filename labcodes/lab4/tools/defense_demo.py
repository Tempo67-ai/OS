#!/usr/bin/env python3
"""One-command Lab4 defense demo. Uses real QEMU/GDB; no fabricated transcripts."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PASS = 'LAB4 SELF CHECKS PASSED'


def banner(title, explanation=''):
    print('\n' + '=' * 72 + '\n' + title + '\n' + '=' * 72, flush=True)
    if explanation:
        print(explanation, flush=True)


def pause(auto):
    if not auto and sys.stdin.isatty():
        input('\n按回车进入下一步；Ctrl+C 退出演示……')


def tool(name):
    found = shutil.which(name)
    if not found:
        raise RuntimeError('未找到工具：' + name)
    return found


def run(cmd, cwd, log, timeout=90, echo=True):
    """Capture exact output and propagate nonzero exit or timeout."""
    print('$ ' + ' '.join(str(x) for x in cmd), flush=True)
    try:
        result = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        log.write_bytes(exc.stdout or b'')
        raise RuntimeError('命令超时，输出已保存：' + str(log)) from exc
    log.write_bytes(result.stdout)
    output = result.stdout.decode('utf-8', errors='replace')
    if echo or result.returncode:
        print(output, end='' if output.endswith('\n') else '\n', flush=True)
    if result.returncode:
        raise RuntimeError(f'命令退出码 {result.returncode}，日志：{log}')
    return output


def check_serial(text):
    if PASS not in text or 'PANIC' in text:
        raise RuntimeError('客体内核没有完成自检，或出现 PANIC；请查看串口日志。')


def show_function(relative, name):
    """Show the selected complete function with real source line numbers."""
    text = (ROOT / relative).read_text()
    # These names are defined before any later call in the selected C files.
    match = re.search(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', text)
    if not match:
        raise RuntimeError('源码中找不到函数：' + name)
    start = text.rfind('\n', 0, match.start()) + 1
    brace = text.find('{', match.start(), match.end())
    # Ignore comments and strings so braces in explanation cannot truncate code.
    token = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    end = None
    for item in token.finditer(text, brace):
        if item.group() == '{':
            depth += 1
        elif item.group() == '}':
            depth -= 1
            if depth == 0:
                end = item.end()
                break
    if end is None:
        raise RuntimeError('无法定位函数结尾：' + name)
    line = text[:start].count('\n') + 1
    print(f'\n--- {relative} / {name} ---')
    for offset, content in enumerate(text[start:end].splitlines()):
        print(f'{line + offset:4d}  {content}')


def gdb_demo(work, qemu, gdb, logs):
    # Bind the debug server only to loopback and avoid assuming port 1234 is free.
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    commands = f'''set pagination off
set confirm off
set print pretty on
set tcp auto-retry on
set tcp connect-timeout 8
file bin/kernel
target remote 127.0.0.1:{port}
tbreak cpu_idle
continue
printf "\\n[1] Created threads before first scheduling\\n"
p current->pid
p idleproc->pid
p initproc->pid
p nr_process
p/x initproc->context.ra
p/x initproc->context.sp
p/x initproc->tf->epc
info symbol initproc->context.ra
info symbol initproc->tf->epc
tbreak *switch_to
continue
printf "\\n[2] switch_to entry: a0=old context, a1=new context\\n"
info registers a0 a1 sp ra
x/14gx $a0
x/14gx $a1
disassemble switch_to
tbreak *forkret
continue
printf "\\n[3] First return reached forkret\\n"
x/i $pc
tbreak *forkrets
continue
printf "\\n[4] forkrets receives the new trapframe in a0\\n"
p/x current->tf
p/x $a0
x/36gx $a0
tbreak *__trapret
continue
printf "\\n[5] Restore trapframe before sret\\n"
x/5i $pc
tbreak *kernel_thread_entry
continue
printf "\\n[6] sret reached kernel_thread_entry in S-mode\\n"
info registers sp s0 s1 sstatus
p current->pid
x/3i $pc
printf "\\nGDB_FIRST_RUN_CHAIN_PASS\\n"
detach
quit
'''
    script = work / 'demo.gdb'
    script.write_text(commands)
    shutil.copy2(script, logs / 'demo.gdb')
    serial = logs / 'gdb-serial.log'
    command = [qemu, '-machine', 'virt', '-m', '128M', '-smp', '1',
               '-nographic', '-bios', 'default', '-kernel', 'bin/ucore.img',
               '-monitor', 'none', '-S', '-gdb', f'tcp:127.0.0.1:{port}']
    with serial.open('wb') as stream:
        process = subprocess.Popen(command, cwd=work, stdout=stream, stderr=subprocess.STDOUT)
        try:
            output = run([gdb, '-q', '-nx', '-batch', '-x', 'demo.gdb'], work,
                         logs / 'gdb.log', timeout=35)
            if 'GDB_FIRST_RUN_CHAIN_PASS' not in output or 'Error in sourced command file' in output:
                raise RuntimeError('GDB 演示链未完成。')
            if process.wait(timeout=15) != 0:
                raise RuntimeError('GDB detach 后 QEMU 异常退出。')
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    check_serial(serial.read_text(errors='replace'))
    print('GDB 首次切换链已实际走通；detach 后内核继续运行并完成自检。')


def main():
    parser = argparse.ArgumentParser(description='Lab4 一键答辩演示：真实运行、源码、GDB、本地自检')
    parser.add_argument('--auto', action='store_true', help='不等待回车，适合预演或录屏')
    parser.add_argument('--quick', action='store_true', help='跳过完整矩阵，保留正常运行、代码与GDB')
    parser.add_argument('--skip-gdb', action='store_true', help='明确跳过GDB，不把跳过记录成通过')
    args = parser.parse_args()
    prefix = os.environ.get('GCCPREFIX')
    if not prefix:
        prefix = next((p for p in ('riscv64-elf-', 'riscv64-unknown-elf-')
                       if shutil.which(p + 'gcc')), None)
    if not prefix:
        raise RuntimeError('找不到RISC-V交叉编译器，请安装或设置GCCPREFIX。')
    make = tool(os.environ.get('MAKE') or ('gmake' if shutil.which('gmake') else 'make'))
    qemu = tool(os.environ.get('QEMU', 'qemu-system-riscv64'))
    gdb = None if args.skip_gdb else tool(os.environ.get('GDB', prefix + 'gdb'))
    for name in ('gcc', 'ld', 'objcopy', 'objdump'):
        tool(prefix + name)
    logs = ROOT / 'demo_logs' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    logs.mkdir(parents=True)
    manifest = {'official_grading': False, 'gdb': 'skipped' if args.skip_gdb else 'pending',
                'matrix': 'skipped' if args.quick else 'pending', 'passed': False}
    try:
        with tempfile.TemporaryDirectory(prefix='lab4-defense-') as temporary:
            work = Path(temporary) / 'lab4'
            shutil.copytree(ROOT, work, ignore=shutil.ignore_patterns(
                'bin', 'obj', 'report', 'demo_logs', '__pycache__', '.git'))
            banner('Lab4 一键答辩演示',
                   '这是按手册重建版的本地演示，不是课程官方评分。\n'
                   '在临时副本构建运行，不覆盖原工程或原始评分日志。\n日志：' + str(logs))
            pause(args.auto)
            banner('第一步 编译并正常运行', '观察页映射、init线程输出、100 ticks和退出回收。')
            run([make, '-j4', 'GCCPREFIX=' + prefix, 'OPT=-O2', 'DEFS='], work,
                logs / 'build.log', echo=False)
            text = run([qemu, '-machine', 'virt', '-m', '128M', '-smp', '1',
                        '-nographic', '-bios', 'default', '-kernel', 'bin/ucore.img',
                        '-monitor', 'none'], work, logs / 'normal-serial.log', timeout=20)
            check_serial(text)
            manifest['normal_boot'] = 'passed'
            pause(args.auto)
            banner('第二步 对照核心代码解释',
                   '页映射不能直接覆盖旧PTE；调度选择与寄存器迁移是不同职责。')
            for relative, name in [('kern/mm/pmm.c', 'page_insert'),
                                   ('kern/mm/pmm.c', 'page_remove'),
                                   ('kern/process/proc.c', 'copy_thread'),
                                   ('kern/schedule/sched.c', 'schedule'),
                                   ('kern/process/proc.c', 'proc_run')]:
                show_function(relative, name)
            print('\n--- kern/process/switch.S ---\n' + (ROOT / 'kern/process/switch.S').read_text())
            print('讲解：context保存ra/sp/s0～s11；首次运行依赖预设的forkret和Trap帧。')
            pause(args.auto)
            if not args.skip_gdb:
                banner('第三步 GDB 自动展示首次运行链',
                       '真实断点依次停在idle、switch_to、forkret、forkrets、__trapret、kernel_thread_entry。')
                gdb_demo(work, qemu, gdb, logs)
                manifest['gdb'] = 'passed'
                pause(args.auto)
            if not args.quick:
                banner('第四步 完整本地自检矩阵', '32/128/256 MiB、O2/O0、分配失败回滚；不是官方分数。')
                env = os.environ.copy()
                env.update(GCCPREFIX=prefix, QEMU=qemu, MAKE=make)
                result = subprocess.run([sys.executable, 'tools/grade.py'], cwd=work,
                                        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        timeout=120)
                (logs / 'matrix.log').write_bytes(result.stdout)
                print(result.stdout.decode(errors='replace'))
                if result.returncode:
                    raise RuntimeError('完整自检失败，请查看matrix.log。')
                summary = json.loads((work / 'report/logs/summary.json').read_text())
                if not summary['passed']:
                    raise RuntimeError('矩阵结果标记为失败。')
                shutil.copytree(work / 'report/logs', logs / 'matrix-evidence')
                manifest['matrix'] = 'passed'
            manifest['passed'] = True
            banner('演示完成', '所选阶段已实际执行成功。日志保存在：' + str(logs))
    finally:
        (logs / 'demo-summary.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print('\n已停止演示。', file=sys.stderr)
        sys.exit(130)
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        print('\n演示失败：' + str(exc), file=sys.stderr)
        sys.exit(1)
