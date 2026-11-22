# Lab4 虚拟内存页操作与内核线程

基于本仓库 `main@30ab53c` 的 Lab3 和 `os26guidebook` 的 Lab4 要求重建的可运行版本。原始课程 Lab4 代码包未取得，**不是对课程原始框架的原位补全**，不能用本地自检替代官方评分或教师验收。Lab1～Lab3 保持原样。

## 实现范围

- Sv39 三级页表、4 KiB 内核静态映射；`page_insert/page_remove` 维护引用、权限和 TLB，`get_pte` 在中间页分配失败时回滚。
- 单 hart 内核线程的 PCB、PID/哈希索引、独立内核栈、首次运行 Trap 帧、创建失败清理。
- `cpu_idle/schedule/proc_run/switch_to`；按进程链表循环选取 runnable 线程，合作式调度，idle 兜底。
- VMA 创建、按地址有序插入、缓存查找与销毁；小对象 slab 堆分配，空 slab 释放。
- 保留 Lab3 的中断上下文保存恢复；通过两个工作线程、不同寄存器哨兵、实际时钟事件与资源基线断言验证。

未实现用户程序装载、用户态系统调用、按需分页/换页、多 hart 同步或抢占式调度。VMA 是描述结构，不会按访问自动分配页面。内核线程共享 `boot_pgdir_va`，`copy_mm` 遇到非空用户 mm 明确返回错误。

## 构建与运行

需要 GNU Make、RISC-V bare-metal GCC/binutils、QEMU RISC-V（含支持 legacy SBI 的默认 OpenSBI）、Python 3。实际验证版本见 `report/logs/summary.json`。

```sh
cd labcodes/lab4
make
make qemu
```

默认工具前缀 `riscv64-elf-`。如果课程环境使用另一前缀：

```sh
make clean
make GCCPREFIX=riscv64-unknown-elf-
make qemu GCCPREFIX=riscv64-unknown-elf-
```

正常内核也带场景断言：完成线程运行、退出回收且 ticks 至少 100 后打印 `LAB4 SELF CHECKS PASSED` 并请求关机。遇到断言失败打印 `PANIC` 后关机；QEMU 退出码为 0 本身不构成通过证据。

## 重现自检

```sh
make grade
# 或 GCCPREFIX=riscv64-unknown-elf- QEMU=qemu-system-riscv64 make grade
```

这是本项目自己的重建版检查脚本，**没有“官方100分”含义**。脚本记录编译和 QEMU 原始输出到 `report/logs/`，验证成功标记、失败输出与退出码。每个构建配置先 clean，避免宏和优化选项混用旧对象；最后重建并验证普通 `-O2` 版本。

测试包含：同页重插、共享引用、替换与重复删除、活动页表下实际读写和 TLB 替换、页表分配失败回滚、slab跨页和回收、VMA边界与空隙、idle兜底、PCB/栈分配失败、copy_mm拒绝路径、线程首次进入、合作让出、s0～s11的不同哨兵与普通ABI恢复、退出码、内核栈/PCB回收、时钟持续到100次。

## 调试

```sh
# 终端一
make debug
# 终端二
make gdb
```

建议断点：`kern_init`、`check_pgdir`、`proc_init`、`schedule`、`proc_run`、`switch_to`、`forkret`、`forkrets`、`kernel_thread_entry`、`do_exit`。

在 `switch_to` 的确切入口，a0 指向旧 context，a1 指向新 context；在 `forkrets`，a0 指向新线程 trapframe。`context` 保存 ra、sp、s0～s11（112 字节），Trap 帧保存32个GPR槽位加4个CSR槽位（288字节）。`sret` 根据SPP返回，本实验回到S态。

## 文件与证据

- [实验报告](REPORT.md)：设计、spec、prompt、实际问题、验证与 Challenge。
- [来源与重建差异](PROVENANCE.md)：复用范围、手册差异、实现边界。
- [机器可读结果](report/logs/summary.json)：环境、测试命令、退出码、镜像SHA-256。
- [答辩要点](DEFENSE.md)：页表引用、线程创建、调度与首次运行链路。

当前支持单 hart，物理内存上限沿用 Lab3 的 128 MiB；QEMU 分配256 MiB时只管理前128 MiB。启动用1GiB过渡大页，进入C后切换为4KiB叶子页；不提供对已存在大页的自动拆分。内核直接映射不参与可释放页面的引用所有权，不应通过page_remove撤销正在使用的内核直接映射。
