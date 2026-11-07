# Lab3 实验报告：断，都可以断（中断与异常处理）

成员：刘琦（2311764）、李金璇（2312418）、赵非凡（2411020）。

> 实验代码目录：`riscv/lab3`
> 本次修改的文件：`kern/trap/trapentry.S`（功能模块一）、`kern/trap/trap.c`（功能模块二）、`kern/mm/pmm.c` 与 `kern/mm/best_fit_pmm.c`（移植 lab2 代码）；为适配本机 QEMU 10.2.1 / OpenSBI 1.8 环境，另外修改了 `kern/init/entry.S`（启动页表）、`Makefile` 与 `tools/grade.sh`（内核加载方式），改法与 lab2 保持一致

---

## 一、编译与运行

实验在 WSL Ubuntu 中进行，Windows 下的源码目录 `E:\riscv\lab3` 在 WSL 中对应 `/mnt/e/riscv/lab3`。

```bash
# 进入实验目录，并把预编译的 RISC-V 工具链加入 PATH
cd /mnt/e/riscv/lab3
export PATH="/mnt/e/riscv/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin:$PATH"

# 编译
make clean
make

# 在 QEMU 中运行：每秒输出一行 "100 ticks"，输出 10 行后自动关机
make qemu

# 运行评分脚本（断点上下文恢复 / 非法指令 / 持续时钟中断 三个测试）
make grade
```

`make qemu` 的运行结果（关键部分）：

```text
(THU.CST) os is loading ...
...
check_alloc_page() succeeded!
...
++ setup timer interrupts
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
```

输出 10 行 `100 ticks` 后系统调用 `sbi_shutdown()` 关机，QEMU 自动退出。

`make grade` 的运行结果：

```text
  - trap entry, register restore, and breakpoint EPC: PASS (35/35)
  - illegal instruction EPC advance and return: PASS (25/25)
  - re-armed supervisor timer and repeated trap return: PASS (40/40)
Total Score: 100/100
```

---

## 二、对本章整体逻辑线的理解

本章在 lab2（物理内存管理 + 页表）的基础上，让内核第一次具备"被打断、处理、再回来"的能力。整条逻辑线按 **"Trap 上下文保存与恢复 → Trap 入口与返回 → 中断与异常处理"** 展开，可以概括为一个闭环：

```
硬件触发 trap（中断 / 异常）
  └─ 硬件自动：sepc ← 被打断指令地址，scause ← 原因，stval ← 附加信息，
              SPIE ← SIE，SIE ← 0，SPP ← 原特权级，pc ← stvec
       └─ __alltraps（stvec 指向这里，Direct 模式）
            ├─ SAVE_ALL：在内核栈上构造 struct trapframe（32 个通用寄存器 + 4 个 CSR）
            ├─ move a0, sp; jal trap   —— 按调用约定把 trapframe 指针传给 C
            │    └─ trap() → trap_dispatch()
            │         ├─ scause 最高位为 1 → interrupt_handler()（时钟中断：重设定时器、计数、打印、关机）
            │         └─ 最高位为 0     → exception_handler()（非法指令 / 断点：打印并调整 epc）
            └─ __trapret：RESTORE_ALL 从 trapframe 恢复 sstatus、sepc 和通用寄存器
                 └─ sret：pc ← sepc，SIE ← SPIE，回到 SPP 指定的特权级继续执行
```

实现顺序与依赖关系是：

1. **准备阶段（硬件/驱动/初始化）**：`idt_init()` 把 `sscratch` 清零（表示当前在内核态）、把 `stvec` 设为 `__alltraps`；`clock_init()` 打开 `sie.STIE` 并预约第一次时钟事件；`intr_enable()` 置位 `sstatus.SIE` 打开全局中断。
2. **功能模块一（汇编层）**：先保证"进得去、出得来"——`SAVE_ALL` 已提供，补全与之严格对称的 `RESTORE_ALL`，并在 `__alltraps` 中把 trapframe 交给 C 函数 `trap()`。这是后面所有处理的基础：如果现场恢复错一位寄存器，被打断的程序就会出错。
3. **功能模块二（C 层）**：在"能进能出"的基础上决定"进来以后做什么"——按 `scause` 分类，对时钟中断做周期性处理，对非法指令和断点异常输出信息并修正 `epc`，其他情况用 `print_trapframe` 兜底。
4. **验证**：用断点（`ebreak`）验证寄存器现场完整恢复，用非法指令验证 `epc` 前移，用持续的时钟中断验证"中断可以反复进入并正确返回"。
5. **配套**：`kern/sync/sync.h` 提供 `local_intr_save/restore`，在 `alloc_pages/free_pages` 等修改内存管理数据结构的临界区内关中断，保证原子性——这说明一旦引入了中断，之前的代码也要考虑"被打断"的问题。

---

## 三、对各功能核心函数 / 模块的理解

### 3.1 `struct trapframe`（kern/trap/trap.h）

`trapframe` 是"现场快照"的数据布局约定：前 32 个 `uintptr_t` 依次是 `x0~x31`（`struct pushregs`），后 4 个是 `status/epc/badvaddr/cause`，共 36 × 8 = 288 字节。汇编层（`SAVE_ALL/RESTORE_ALL`）和 C 层（`trap()`、`print_trapframe()`）都只依赖这一个布局，所以**第 i 个通用寄存器必须存在 `i*REGBYTES(sp)`，CSR 必须在 32~35 号槽位**，任何一边擅自改动都会导致读写错位。

### 3.2 `SAVE_ALL`（已提供）

1. `csrw sscratch, sp`：先把 trap 发生时的原始 `sp` 暂存到 `sscratch`，因为马上要修改 `sp` 开辟空间；
2. `addi sp, sp, -36*REGBYTES`：在当前内核栈上开辟一个 trapframe；
3. 依次 `STORE x0,x1,x3~x31`，**跳过 x2(sp)**，因为此时的 `sp` 已经是减过的值；
4. `csrrw s0, sscratch, x0`：把原始 `sp` 从 `sscratch` 取到 `s0`，同时把 `sscratch` 清零（约定 0 表示在内核态，便于将来识别嵌套 trap）；
5. 用 `s1~s4` 读出 `sstatus/sepc/sbadaddr/scause`（RISC-V 不能直接把 CSR 存到内存，必须经过通用寄存器中转），再连同 `s0` 一起存到 2、32~35 号槽位。由于 `s0~s4` 的原值在第 3 步已经保存过，这里覆盖它们是安全的。

### 3.3 `RESTORE_ALL`（功能模块一，本次实现）

```asm
    .macro RESTORE_ALL
    LOAD s1, 32*REGBYTES(sp)      # 保存的 sstatus
    LOAD s2, 33*REGBYTES(sp)      # 保存的（可能被 C 修改过的）sepc
    csrw sstatus, s1
    csrw sepc, s2

    LOAD x1, 1*REGBYTES(sp)
    LOAD x3, 3*REGBYTES(sp)
    ...                           # x4 ~ x30 同理
    LOAD x31, 31*REGBYTES(sp)
    LOAD x2, 2*REGBYTES(sp)       # 最后恢复 sp
    .endm
```

设计要点：

- **先恢复 CSR，再恢复通用寄存器**：写 CSR 需要借用通用寄存器（这里借 `s1/s2`）中转，借完之后它们会在后面被各自保存的值覆盖，因此不会污染现场。
- **只写回 `sstatus` 和 `sepc`**：`sepc` 决定 `sret` 跳回哪里（异常处理时 C 代码会修改 `tf->epc`，必须写回才能生效）；`sstatus` 中的 `SPP/SPIE` 决定 `sret` 返回的特权级和中断使能状态。`badvaddr` 和 `scause` 只是提供给处理程序的"现场证据"，不需要写回。
- **`x0` 不恢复**（硬连线为 0），**`x2(sp)` 最后恢复**：所有 `LOAD` 都以 `sp` 作为基址，若提前恢复 `sp`，后面的读取就会从错误的地址取值。同时 `2*REGBYTES(sp)` 中保存的正是 `SAVE_ALL` 经 `sscratch` 存下的原始 `sp`，所以这最后一条 `LOAD` 同时完成了"释放 trapframe 空间"的工作，不需要再 `addi sp, sp, 36*REGBYTES`。这就是题目强调的"`SAVE_ALL` 中对 `x2(sp)` 的处理方式与其他通用寄存器不完全相同"。

### 3.4 `__alltraps` / `__trapret`（功能模块一，本次实现）

```asm
__alltraps:
    SAVE_ALL
    move  a0, sp        # trapframe 的地址就是当前 sp，按调用约定用 a0 传参
    jal trap            # 调用 void trap(struct trapframe *tf)

    .globl __trapret
__trapret:
    RESTORE_ALL
    sret
```

- `SAVE_ALL` 结束后 `sp` 恰好指向 trapframe 的起始地址，即 `struct trapframe *`；RISC-V 调用约定规定第一个参数放在 `a0`，所以 `move a0, sp`。
- `trap()` 是普通 C 函数，遵守调用约定，返回时 `sp` 与调用前相同（`sp` 是 callee-saved），因此返回后 `sp` 仍指向同一个 trapframe，可以直接顺序执行到 `__trapret`。`ra` 被 `jal` 改写也没关系，因为 `RESTORE_ALL` 会从 trapframe 恢复 `x1`。
- `sret` 完成特权级返回：`pc ← sepc`，`SIE ← SPIE`，`SPIE ← 1`，特权级切换为 `SPP`，`SPP ← 0`。

### 3.5 `trap()` / `trap_dispatch()`（已提供）

`trap_dispatch` 把 `tf->cause` 当作有符号数判断：`scause` 最高位（Interrupt 位）为 1 时有符号值为负，说明是中断，交给 `interrupt_handler`；否则是异常，交给 `exception_handler`。

### 3.6 `interrupt_handler()`（功能模块二，本次实现）

```c
void interrupt_handler(struct trapframe *tf) {
    intptr_t cause = (tf->cause << 1) >> 1;   // 去掉最高位的 Interrupt 标志
    switch (cause) {
        case IRQ_U_SOFT:  cprintf("User software interrupt\n"); break;
        case IRQ_S_SOFT:  cprintf("Supervisor software interrupt\n"); break;
        ...                                    // H/M 软件中断、U/H/M 时钟中断、各类外部中断：输出类型提示
        case IRQ_S_TIMER:
            clock_set_next_event();            // (1) 预约下一次时钟中断
            ticks++;                           // (2) 计数
            if (ticks % TICK_NUM == 0) {       // (3) 每 100 次输出 "100 ticks"
                print_ticks();
                num_prints++;
                if (num_prints >= SHUTDOWN_AFTER)  // (4) 输出 10 次后关机
                    sbi_shutdown();
            }
            break;
        default:                               // 无法识别的中断
            cprintf("Unknown interrupt: cause %d\n", (int)cause);
            print_trapframe(tf);
            break;
    }
}
```

- **时钟中断必须先重新设置下一次事件**：OpenSBI 的 `sbi_set_timer()` 一次只能预约一个时间点，`mtimecmp` 一旦到期，不重设就不会再有下一次中断，也会让 STIP 一直挂起。
- `ticks` 定义在 `clock.c` 中且是 `volatile`，因为它在中断上下文里被修改、在主流程（以及测试代码的忙等循环）里被读取，必须阻止编译器把它缓存到寄存器。
- 框架中没有 `SHUTDOWN_AFTER` 的定义，因此在 `trap.c` 的待填写区域内用 `#ifndef` 定义为 10（与 ucore 原版"打印 10 次后关机"一致），并用 `num_prints` 记录打印次数，关机调用框架在 `libs/sbi.h` 中提供的 `sbi_shutdown()`。
- `timebase = 100000`，QEMU 的 `time` 频率是 10 MHz，所以每 10 ms 一次中断，100 次即 1 秒输出一行 `100 ticks`。

### 3.7 `exception_handler()`（功能模块二，本次实现）

```c
static inline uintptr_t trap_inst_len(uintptr_t epc) {
    uint16_t low = *(volatile uint16_t *)epc;
    return ((low & 0x3) == 0x3) ? 4 : 2;    // 低两位 11 → 32 位指令，否则 16 位压缩指令
}

void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
        case CAUSE_ILLEGAL_INSTRUCTION:
            cprintf("Exception type: Illegal instruction\n");
            cprintf("Illegal instruction at 0x%016lx\n", tf->epc);
            tf->epc += trap_inst_len(tf->epc);
            break;
        case CAUSE_BREAKPOINT:
            cprintf("Exception type: breakpoint\n");
            cprintf("Breakpoint at 0x%016lx\n", tf->epc);
            tf->epc += trap_inst_len(tf->epc);
            break;
        default:
            print_trapframe(tf);   // 其他异常兜底
            break;
    }
}
```

- 异常是**同步**事件，`sepc` 指向**触发异常的那条指令本身**；若不修改 `epc`，`sret` 后会再次执行同一条指令，立刻再次陷入，形成死循环。中断则不同，`sepc` 指向被打断但尚未执行的指令，返回后应当继续执行它，所以中断处理不修改 `epc`。
- **不能假定所有异常指令都是 4 字节**：RISC-V 指令长度由最低两位编码决定，`0b11` 是 32 位标准指令，其他是 16 位压缩指令（C 扩展）。本工程编译时启用了 C 扩展，`ebreak` 被汇编成 2 字节的 `c.ebreak`（机器码 `0x9002`），而测试中的非法指令 `.word 0x0000007b` 低两位是 `11`，是 4 字节。因此通过读取 `epc` 处的低 16 位动态判断长度。

### 3.8 lab2 代码移植（`pmm.c` / `best_fit_pmm.c`）

本实验依赖 lab2 的物理内存管理：把 lab2 中实现的 `page_init()`（根据 DTB 探测的内存范围建立 `Page` 数组并把空闲区交给 `init_memmap`）填入 `pmm.c` 中 `//lab2 请补充你在lab2中的代码` 处，把 Best-Fit 分配器的 `best_fit_init / init_memmap / alloc_pages / free_pages / nr_free_pages` 填入 `best_fit_pmm.c` 对应位置。lab3 的 `pmm.c` 在 `alloc_pages/free_pages/nr_free_pages` 外面包了 `local_intr_save/restore`，这正是本章"断亦有断"一节的内容。

---

## 四、每个练习的 spec 与 prompt

### 4.1 功能模块一：Trap 上下文恢复与 Trap 入口

**spec（功能规格）**

| 项目 | 内容 |
| --- | --- |
| 位置 | `kern/trap/trapentry.S`：`RESTORE_ALL` 宏、`__alltraps` 中 `SAVE_ALL` 之后的空位 |
| 输入 | `SAVE_ALL` 在当前栈顶构造好的 trapframe（36 × REGBYTES，x0~x31 依次在 0~31 号槽，sstatus/sepc/sbadaddr/scause 在 32~35 号槽，2 号槽存的是 trap 前的原始 sp） |
| `__alltraps` 输出 | 以 `struct trapframe *tf = sp` 调用 `void trap(struct trapframe *tf)`，满足 RISC-V 调用约定（参数放 a0） |
| `RESTORE_ALL` 输出 | 处理器状态恢复为 trapframe 中记录的状态：sstatus、sepc（可能已被 C 修改）、x1、x3~x31，最后 sp 恢复为 trap 前的原值 |
| 约束 | 不能修改 `SAVE_ALL`、`struct trapframe`、`trap()` 签名；必须与 `SAVE_ALL` 的布局严格对称；x0 不恢复；x2 必须最后恢复且从 2 号槽恢复；不要求写回 badvaddr/scause；`jal trap` 前后 sp 相同；最终由 `__trapret` 中的 `sret` 返回 |
| 验收 | 断点测试中 t0~t6、s0~s11、a1~a7 在 `ebreak` 前后保持不变；时钟中断可以反复进入和返回 |

**prompt（提示词）**

```text
你是一名熟悉 RISC-V 特权架构和 ucore 的操作系统助教。请为 ucore lab3 补全
kern/trap/trapentry.S 中的 RESTORE_ALL 宏和 __alltraps 中调用 C 函数的部分。

【已有代码】
（此处粘贴完整的 SAVE_ALL 宏、__alltraps/__trapret 骨架，以及 trap.h 中的
 struct pushregs 和 struct trapframe 定义）

【接口】
- C 处理函数：void trap(struct trapframe *tf);
- 宏 STORE/LOAD/REGBYTES 来自 riscv.h，64 位下分别是 sd/ld/8。

【要求】
1. RESTORE_ALL 必须与 SAVE_ALL 严格对称，使用同一个 trapframe 布局：
   通用寄存器 xi 位于 i*REGBYTES(sp)，sstatus/sepc/sbadaddr/scause 位于 32~35 号槽。
2. 需要写回 sstatus 和 sepc（sepc 可能被 C 代码修改过，sret 依赖它们），
   说明为什么 sbadaddr 和 scause 不需要写回。写 CSR 时可以借用一个之后会被恢复的
   通用寄存器做中转。
3. 注意 SAVE_ALL 对 x2(sp) 的特殊处理：2 号槽保存的是 trap 前的原始 sp。
   请说明 sp 应该在什么时候恢复、为什么，以及恢复 sp 之后是否还需要 addi 回收栈空间。
   x0 不需要恢复。
4. __alltraps 中 SAVE_ALL 之后，按 RISC-V 调用约定把当前 trapframe 地址作为
   第一个参数传给 trap()，并保证 trap 返回后 sp 不变、能顺序进入 __trapret。
5. 不得修改 SAVE_ALL、struct trapframe、trap() 的签名，保留原有的
   "#lab3 function1 YOUR CODE" 注释。
6. 输出完整的汇编代码，每一组指令加中文注释，最后用 3~5 句话解释整个
   "保存 → 调用 trap → 恢复 → sret" 的闭环。
```

### 4.2 功能模块二：中断与异常处理

**spec（功能规格）**

| 项目 | 内容 |
| --- | --- |
| 位置 | `kern/trap/trap.c` 中 `//lab3 function2 YOUR CODE` 处（位于 `trap_dispatch` 之前） |
| 函数 | `void interrupt_handler(struct trapframe *tf)`、`void exception_handler(struct trapframe *tf)`，必须是非 static 的外部函数（测试代码通过 extern 调用） |
| 中断 | 去掉 scause 最高位得到编号；`IRQ_S_TIMER`：先 `clock_set_next_event()`，再 `ticks++`，`ticks` 为 `TICK_NUM` 整数倍时调用 `print_ticks()` 输出 `100 ticks`，累计输出 `SHUTDOWN_AFTER` 次后调用 `sbi_shutdown()`；其他已知中断（U/S/H/M 软件、时钟、外部中断）输出类型提示，如 `Supervisor software interrupt`；无法识别的中断输出 `Unknown interrupt: cause <n>` 并 `print_trapframe(tf)` |
| 异常 | `CAUSE_ILLEGAL_INSTRUCTION`：输出 `Illegal instruction at 0x<epc>`；`CAUSE_BREAKPOINT`：输出 `Breakpoint at 0x<epc>`；两者都要把 `tf->epc` 增加**该指令的实际长度**（按低两位编码判断 2 或 4 字节）；其他异常 `print_trapframe(tf)` 兜底 |
| 约束 | 不修改函数签名，不修改 trap.c 中规定以外的函数；使用框架已有的宏（`TICK_NUM`、`IRQ_*`、`CAUSE_*`）；框架没有定义 `SHUTDOWN_AFTER`，需在待填写区域内自行定义 |
| 验收 | 非法指令后能继续执行（epc 前移 4）；c.ebreak 后寄存器不变且继续执行（epc 前移 2）；时钟中断持续产生，ticks 能达到 200 以上，每秒一行 `100 ticks`，10 行后关机 |

**prompt（提示词）**

```text
你是一名熟悉 RISC-V 特权架构和 ucore 的操作系统助教。请为 ucore lab3 在
kern/trap/trap.c 的 "//lab3 function2 YOUR CODE" 处实现
void interrupt_handler(struct trapframe *tf) 和 void exception_handler(struct trapframe *tf)。

【已有代码与接口】
（粘贴 trap.c 全文、trap.h、clock.h、sbi.h，以及 riscv.h 中 IRQ_* 和 CAUSE_* 的定义）
- trap_dispatch 已按 (intptr_t)tf->cause < 0 区分中断和异常，两个函数会在它之前被定义。
- ticks 是 clock.c 中的 volatile size_t；clock_set_next_event() 预约下一次时钟中断；
  TICK_NUM = 100；print_ticks() 已提供；sbi_shutdown() 用于关机。

【interrupt_handler 要求】
1. 先清除 scause 最高位的 Interrupt 标志得到中断编号再 switch。
2. IRQ_S_TIMER：必须先设置下一次时钟事件，再 ticks++；ticks 为 TICK_NUM 的
   整数倍时调用 print_ticks()；累计打印次数达到 SHUTDOWN_AFTER（框架中没有定义，
   请用 #ifndef 定义为 10）后调用 sbi_shutdown()。
3. 其他已知中断（用户/监管/超级/机器 的软件、时钟、外部中断）只输出类型提示，
   监管软件中断输出 "Supervisor software interrupt"。
4. 无法识别的中断输出 "Unknown interrupt: cause %d"，再调用 print_trapframe(tf)。

【exception_handler 要求】
1. CAUSE_ILLEGAL_INSTRUCTION：输出 "Exception type: Illegal instruction" 和
   "Illegal instruction at 0x%016lx"（epc）。
2. CAUSE_BREAKPOINT：输出 "Exception type: breakpoint" 和 "Breakpoint at 0x%016lx"。
3. 上述两种异常都要更新 tf->epc 跳过触发异常的指令，避免 sret 后重复执行。
   注意工程启用了 C 扩展，不能假定所有指令都是 4 字节：请根据 RISC-V 指令编码规则
   （低两位为 0b11 的是 32 位指令，否则是 16 位压缩指令）读取 epc 处的指令计算长度。
4. 其他异常调用 print_trapframe(tf) 兜底。

【通用约束】
- 两个函数必须是非 static 的（测试代码会 extern 调用），定义在 trap_dispatch 之前；
  编译选项含 -Werror，不能有隐式声明和未使用变量警告。
- 不得修改函数签名和 trap.c 中其他函数，保留 "//lab3 function2 YOUR CODE" 注释。
- 输出完整 C 代码并加中文注释，最后说明为什么时钟中断不需要修改 epc 而异常需要。
```

### 4.3 lab2 代码移植

**spec**：把 lab2 中已完成的 `page_init()` 放入 lab3 `pmm.c` 的 `//lab2` 标记处，把 Best-Fit 分配器的实现放入 `best_fit_pmm.c` 的 `//lab2` 标记处；不能修改 `basic_check`、`best_fit_check` 等检查函数；需适配 lab3 头文件的差异，保证 `-Werror` 下编译通过。

**prompt**：

```text
下面是我在 lab2 中完成的 page_init() 和 best_fit 分配器代码，以及 lab3 的
pmm.c、best_fit_pmm.c、memlayout.h、dtb.h。请把 lab2 的实现填入 lab3 中
"//lab2 请补充你在lab2中的代码" 的位置，不要修改 basic_check / best_fit_check，
并逐一检查 lab3 头文件相对 lab2 的变化（宏是否被删除、Page 标志位宏是否改成了
原子操作等），指出需要适配的地方，保证在 -Werror 下编译通过。
```

---

## 五、使用提示词让 AI 生成代码过程中出现的 bug 及解决

| # | 现象 | 原因 | 解决方法 |
| --- | --- | --- | --- |
| 1 | 移植 lab2 代码后编译报错：`'PHYSICAL_MEMORY_END' undeclared (first use in this function); did you mean 'PHYSICAL_MEMORY_OFFSET'?` | lab3 的 `memlayout.h` 相比 lab2 删除了 `PHYSICAL_MEMORY_END`（以及 `KERNEL_BEGIN_PADDR/VADDR`），而 lab2 的 `page_init()` 用它截断可管理的物理内存上界 | 在 `pmm.c` 的 lab2 代码块前用 `#ifndef PHYSICAL_MEMORY_END` 补充与 lab2 一致的定义 `0x88000000`（QEMU 默认 128 MB）；不改动公共头文件，避免影响其他模块 |
| 2 | 原始框架直接编译报错：`implicit declaration of function 'interrupt_handler' [-Werror=implicit-function-declaration]` | `trap_dispatch` 调用了这两个函数，但框架中没有定义；Makefile 带 `-Werror` | 在 `trap_dispatch` 之前的待填写区域实现两个函数；同时它们不能声明为 `static`，因为评分注入的 `test_lab3.c` 通过 `extern` 直接调用它们 |
| 3 | 断点异常处理若照搬"ecall/异常返回地址为 `sepc + 4`"的写法，`ebreak` 之后会跳到下一条指令的中间，执行错乱 | 用 `objdump -d bin/kernel` 查看，测试代码中的 `ebreak` 被汇编器压缩成了 2 字节的 `c.ebreak`（`9002`），而非法指令 `.word 0x7b` 是 4 字节。固定 `+4` 只对后者正确 | 在提示词中明确"不能假定指令长度都是 4 字节"，让 AI 按 RISC-V 编码规则读取 `epc` 处低 16 位判断：低两位 `11` 为 4 字节，否则为 2 字节 |
| 4 | `RESTORE_ALL` 若按 x1~x31 顺序依次恢复，在恢复 x2 之后，后面的 `LOAD x3...` 会以错误的 sp 为基址读取，寄存器全部错乱 | sp 既是要恢复的寄存器，又是所有 `LOAD` 的基址寄存器 | 在提示词中强调 `SAVE_ALL` 对 x2 的特殊处理，要求"sp 最后恢复"，并说明恢复 sp 后栈空间自动回收，不需要再 `addi` |
| 5 | 测试代码以 `tf.cause = 15`（没有设置最高位）直接调用 `interrupt_handler`，要求输出 `Unknown interrupt: cause 15`；若直接用 `tf->cause` 的原值 switch 并打印，打印出的编号会带上最高位 | 正常中断的 scause 最高位为 1，需要先去掉 Interrupt 位才能和 `IRQ_*` 比较 | 统一用 `(tf->cause << 1) >> 1` 得到中断编号再 switch 和打印，这样无论最高位是否置位结果都一致 |
| 6 | 框架中没有 `SHUTDOWN_AFTER` 宏，直接使用会编译失败 | 指导书要求"以实验框架中已有定义为准"，但该宏缺失 | 在 trap.c 的待填写区域用 `#ifndef SHUTDOWN_AFTER` 定义为 10，既不破坏可能存在的外部定义，又保证 ticks 至少能到 1000，满足评分脚本"ticks 到 200"的要求 |
| 7 | 第一次运行 `make grade` 三个测试全部 0 分。单独运行发现：用评分脚本的 `-device loader` 方式启动时 OpenSBI 打印 `Domain0 Next Address: 0x0000000000000000`，根本没有跳到内核；改用 `-kernel` 后 `Next Address` 变为 `0x80200000`，但内核仍然一行都没有输出 | 两个与新版 QEMU 10.2.1 / OpenSBI 1.8 相关的兼容性问题：① 只用 `-device loader` 放入镜像时，固件不知道内核入口，`Next Address` 为 0；② lab3 的启动页表只映射了高地址 `0xffffffffc0000000`，`entry.S` 执行 `csrw satp` 开启分页后，紧接着的指令仍按物理地址 `0x8020xxxx` 取指，这段地址没有映射，触发取指页异常，而此时 `stvec` 还没有设置，内核卡死在启动阶段 | 与 lab2 中的处理一致：① `Makefile` 的 `qemu/debug/test` 目标和 `tools/grade.sh` 改为 `-kernel` 加载；② 在 `entry.S` 的启动页表第 2 项加入 `0x80000000 → 0x80000000` 的 1 GiB 恒等映射，保证开启分页到跳转高地址之间的几条指令可以正常取指。用 `objdump` 确认页表第 2 项和第 511 项均为 `0x200000cf` |

---

## 六、本实验中重要的知识点与对应的 OS 原理

| 实验中的知识点 | 对应 OS 原理 | 含义、关系与差异 |
| --- | --- | --- |
| RISC-V trap = 中断 + 异常；scause 最高位区分 | 中断（外部/硬件中断）与异常（内部中断、陷阱）的分类 | 原理课把中断分为外中断和内中断（异常、陷阱/系统调用）；RISC-V 把所有控制流转移统称 trap，用 scause 的 Interrupt 位区分异步中断和同步异常，ecall/ebreak 属于异常。术语不同，本质相同 |
| `stvec` 中断向量基址，Direct 模式 | 中断向量表 / 中断描述符表（IDT） | 原理中由中断向量号查表找到处理程序；本实验采用 Direct 模式，所有 trap 都跳到唯一入口 `__alltraps`，再由软件按 scause 分发，相当于"软件实现的向量表"。x86 的 IDT 由硬件直接分派 |
| `SAVE_ALL / RESTORE_ALL`、`struct trapframe` | 中断现场保护与恢复、上下文切换 | 原理中硬件保存 PC/PSW，软件保存其余寄存器。RISC-V 硬件只保存 sepc、scause、stval 和 sstatus 中的 SPIE/SPP，其余 32 个通用寄存器全部由软件保存，trapframe 就是"现场"的数据结构；将来进程切换时也会以 trapframe 为基础 |
| `sepc`、`sret`、`tf->epc` 的调整 | 中断返回、断点地址、"精确异常" | 中断返回到被打断的指令继续执行；异常（如系统调用、断点）通常要返回下一条指令。原理中由 iret/中断返回指令恢复 PC/PSW，RISC-V 对应 sret；差别在于 RISC-V 的"下一条"需要考虑压缩指令的 2 字节长度 |
| `sstatus.SIE/SPIE/SPP`、`sie`、`intr_enable/disable` | 中断屏蔽、开关中断、中断优先级 | 原理中的"关中断"对应清 `sstatus.SIE`（全局屏蔽），`sie` 寄存器对应屏蔽某一类中断源；进入 trap 时硬件自动关中断（SIE→SPIE，SIE=0），实现了"中断处理期间不被同级中断打断"。本实验没有多级中断优先级与中断嵌套 |
| M/S/U 三个特权级、`mideleg/medeleg` 中断委托、OpenSBI | 用户态与内核态（管态/目态）、特权指令 | 原理中通常只有两态；RISC-V 多了 M 态，由固件 OpenSBI 运行。内核运行在 S 态，通过 ecall 请求 M 态服务（如 `sbi_set_timer`、`sbi_shutdown`），这和用户程序通过系统调用请求内核服务是同一模式在不同层次的复现 |
| 时钟中断、`ticks`、`sbi_set_timer` 一次性定时器 | 时钟中断、时钟管理、时间片 | 原理中时钟中断是分时系统和进程调度的基础（时间片轮转、计时）；本实验只实现了计数和打印，"每次中断重设下一次事件"对应可编程定时器的一次性模式（one-shot），而不是固定周期模式 |
| `local_intr_save/restore`、`atomic.h` | 临界区、互斥、原子操作 | 单处理器上可以用"关中断"实现临界区互斥，原理课中的硬件方法之一；`local_intr_save` 会保存原来的中断状态再恢复，支持嵌套使用。多处理器下仅关本地中断不足以互斥，还需要锁 |
| `volatile size_t ticks` | 并发访问共享变量、内存可见性 | 中断处理程序和主流程并发访问 ticks，`volatile` 防止编译器优化掉读取，相当于最基础的"共享变量可见性"问题 |
| `do { } while (0)` 宏 | — | 让多语句宏在 `if/else` 中像一条普通语句一样使用，并强制调用处带分号，避免宏展开后语法或逻辑错误 |

---

## 七、OS 原理中很重要但本实验没有对应上的知识点

1. **中断嵌套与中断优先级**：本实验进入 trap 后硬件自动关闭 S 态中断，处理过程中不会再响应新中断，没有涉及多级优先级、可抢占的中断处理以及中断嵌套时的栈管理。
2. **用户态到内核态的完整切换**：目前内核一直运行在 S 态，`sscratch` 只是被约定为 0，没有用户程序，也没有在 trap 入口根据 `sscratch` 切换到内核栈，用户栈与内核栈分离、U→S→U 的完整流程没有真正实践。
3. **系统调用**：没有实现基于 `ecall` 的系统调用分发表、参数传递与返回值约定。
4. **基于时钟中断的进程调度**：时钟中断只做了计数，没有时间片轮转、进程切换、调度算法。
5. **外部设备中断与 I/O 处理**：没有涉及 PLIC 外部中断控制器、设备驱动的中断服务程序、中断下半部（软中断/tasklet/工作队列）、DMA 等。
6. **缺页异常处理与虚拟内存**：缺页异常（Instruction/Load/Store page fault）在本实验中只会被 `print_trapframe` 兜底，没有实现按需调页、页面置换。
7. **多处理器下的中断与同步**：核间中断（IPI）、多核下的锁与中断亲和性都没有涉及；关本地中断在多核下不足以保证互斥。
8. **信号 / 异常向用户程序的传递**：原理中用户程序的除零、非法访存等异常最终可能以信号形式交给用户处理或终止进程，本实验没有对应机制。
