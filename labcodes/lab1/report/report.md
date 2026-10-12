# Lab1 实验报告

## 一、实验目的和整体逻辑

本实验构建一个最小可执行内核，在 QEMU 模拟的 64 位 RISC-V 计算机上运行。内核启动后输出 `(THU.CST) os is loading ...`，然后进入无限循环。

我对本章节的整体理解是：先了解 QEMU、OpenSBI 和内核之间的关系，再确定内核的内存布局与入口，随后封装输出功能，最后完成编译、运行和调试。

编译过程是：C 和汇编源码经过交叉编译生成目标文件，链接器按照链接脚本组合目标文件，生成 ELF 文件，再通过 objcopy 转换成原始二进制内核镜像。

启动过程是：QEMU 预先将 OpenSBI 和内核镜像放入模拟的物理内存。CPU 从复位地址 0x1000 开始执行，跳到 0x80000000 的 OpenSBI。OpenSBI 完成初始化后，将控制权交给 0x80200000 的内核。内核入口建立栈，跳到 kern_init，完成初始化和打印后进入循环。

资料前面将加载过程概括为 OpenSBI 加载内核，后面进一步说明本实验实际上由 QEMU 在 CPU 运行前加载镜像，OpenSBI 负责初始化和交接控制权。

## 二、核心函数和功能模块

kern/init/entry.S 是内核最先执行的汇编代码。它设置栈指针，再跳到 C 语言的初始化函数。启动栈由 .space KSTACKSIZE 预留；

kern/init/init.c 中的 kern_init 是 C 初始化入口。memset()清零从 edata 到 end 的区域，保证初值为零。随后调用 cprintf 输出信息，最后保持循环。

tools/kernel.ld 是链接脚本。OUTPUT_ARCH 指定 RISC-V 架构，ENTRY(kern_entry) 设置 ELF 入口，BASE_ADDRESS 将内核布局起点设为 0x80200000。脚本依次安排 .text、.rodata、.data、.sdata、.bss 等部分，并使用 ALIGN(0x1000) 将后续布局对齐到 4096 字节的倍数。

Makefile 定义目标、依赖和生成命令，并使用 tools/function.mk 中的辅助函数。make 根据依赖及文件更新时间判断哪些部分需要重新构建。KOBJS 收集 kernel 和 libs 的目标文件，供链接器生成内核。bin/kernel 是包含符号和调试信息的 ELF 文件，bin/ucore.img 是由 objcopy 转换得到的原始二进制镜像。

cprintf 使用省略号接收可变参数，通过 va_list、va_start 和 va_end 管理参数访问。

libs/sbi.c 封装底层调用。本实验使用的字符输出编号为 1，服务号放在 a7/x17，字符参数放在 a0/x10，随后执行 ecall。内核运行在 S 模式，OpenSBI 运行在 M 模式，ecall 使内核按接口约定请求固件服务。asm volatile 表明汇编有重要副作用，防止编译器仅因结果未使用而删除它。

libs/defs.h 提供类型和宏，libs/riscv.h 封装寄存器操作。本实验使用项目自己的输出函数。

## 三、练习一：理解内核启动中的程序入口操作

问题：la sp, bootstacktop 完成了什么操作，目的是什么？

操作：将符号 bootstacktop 的地址加载到栈指针寄存器 sp 中。

目的：初始化内核的运行栈。C 语言函数调用需要栈，设置 sp 为即将执行的 C 代码准备基础运行环境。栈空间已经由 .space 预留，这条指令负责设置指针，不负责分配空间。由于栈向低地址增长，sp 要指向预留区域的高地址端。

问题：tail kern_init 完成了什么操作，目的是什么？

操作：将控制流跳转到 C 函数 kern_init 的入口，不向返回地址寄存器 ra 写入一个新的返回地址。

目的：将执行权交给 C 语言编写的初始化函数。本实验中的 kern_init 最后进入无限循环，不会返回到汇编入口，因此无需为这次跳转建立返回路径。

## 四、练习二：使用 GDB 验证启动流程

源码目录为 E:\riscv\lab1，在 WSL 中对应 /mnt/e/riscv/lab1。先进入该目录，设置路径，再执行 make：

```bash
cd /mnt/e/riscv/lab1
export PATH=/mnt/e/riscv/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin:$PATH
make
```

图片使用 make qemu 运行，make debug 和 make gdb 调试。当前新版环境需要补充 -kernel，为默认固件提供内核启动信息，因此可分别使用：

```bash
make qemu QEMU='qemu-system-riscv64 -kernel bin/ucore.img'
make debug QEMU='qemu-system-riscv64 -kernel bin/ucore.img'
```

普通运行和调试运行分开进行。debug 中的 -S 暂停 CPU，-s 开启默认 1234 调试端口。另一个终端进入同一实验目录，设置工具链路径后执行 make gdb。它读取 bin/kernel 的符号，设置 RV64 架构，并连接 QEMU。

问题：RISC-V 硬件加电后最初执行的几条指令位于什么地址？主要完成哪些功能？

连接后，info registers pc 显示 PC 为 0x1000。使用 x/6i 0x1000，看到以下六条指令：

```asm
0x1000: auipc t0,0x0
0x1004: addi  a2,t0,40
0x1008: csrr  a0,mhartid
0x100c: ld    a1,32(t0)
0x1010: ld    t0,24(t0)
0x1014: jr    t0
```

第一条将当前地址作为基址，t0 变成 0x1000。第二条使 a2 变成 0x1028，指向当前启动方式中的固件启动信息。第三条读取硬件执行线程编号，本次 a0 为 0。第四条读取设备树地址，a1 变成 0x87e00000。第五条读取固件入口，t0 变成 0x80000000。第六条跳转到该入口，开始执行 OpenSBI。

因此，最初指令位于 0x1000 到 0x1014，主要负责准备固件启动参数并跳转。0x1000 的短复位代码由 QEMU 提供，OpenSBI 主体从 0x80000000 开始执行。

调试过程与观察结果：先交替使用 si 和 info registers pc t0 a0 a1 a2 单步执行六条复位指令，观察上述参数变化，最终 PC 到达 0x80000000。随后设置内核入口断点并继续：

```gdb
break *0x80200000
continue
info registers pc sp
p/x &bootstack
p/x &bootstacktop
x/4i $pc
```

断点命中 kern_entry，PC 为 0x80200000。bootstack 为 0x80201000，bootstacktop 为 0x80203000。此时入口指令还未执行，sp 仍是固件留下的值。

入口处的机器指令是：

```asm
0x80200000: auipc sp,0x3
0x80200004: mv    sp,sp
0x80200008: j     0x8020000a
```

执行两次 si 后，sp 变成 0x80203000，等于 bootstacktop。再执行一次 si，PC 到达 0x8020000a 的 kern_init。说明 la 和 tail 完成了栈初始化与跳转，也说明源码伪指令可能对应多条机器指令。

进一步在 cprintf 设置断点并 continue，实际命中输出函数，调用栈显示它由 kern_init 调用。本次 edata 和 end 都为 0x80203008，清零区域长度为零，但初始化代码仍为后续出现 BSS 数据保留了正确处理。

普通运行核对中，OpenSBI 显示下一跳地址为 0x80200000、下一模式为 S-mode，随后输出：

```text
(THU.CST) os is loading ...
```

输出后内核按照源码进入无限循环。

## 五、实验知识点与 OS 原理的关系和差异

启动与初始化：OS 原理关注系统如何建立运行环境。本实验体现了复位代码、固件和内核之间的交接，并通过栈初始化使 C 代码能够运行；尚未完成整个操作系统的初始化。

特权级与权限隔离：OS 原理通过权限区分系统和应用。本实验体现 S 模式内核与 M 模式固件的分工，U 模式仅在资料中介绍。

内存布局与内存管理：链接脚本安排代码、只读数据、初始化数据和 BSS，启动栈提供函数调用空间。这是静态布局和初始空间准备，不等于实现动态分配、分页或虚拟内存管理。

## 六、本实验尚未涉及的重要 OS 知识

进程与线程：尚未建立多个执行任务，也没有进程控制块和任务创建功能。

调度与上下文切换：尚未实现任务选择、时间片和执行状态切换。

虚拟内存与页表：尚未实现地址空间管理。



