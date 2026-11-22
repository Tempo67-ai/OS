# Lab4 实验报告 虚拟内存页操作与内核线程

学号标记：2411020。实现方式：AI辅助，按提供的2026指导书与本仓库Lab3重建。已有Lab3汇编代码中原作者标记保留，不将复用部分声称为本次独立编写。实际组内分工由成员另行如实填写。

## 1 实验材料与完成范围

本次依据`os26guidebook.zip`中Lab4目的、练习、项目组成、页表原理、PCB、idle/init创建和调度说明完成代码。离线手册包含结构和部分源码，但未包含可独立构建的Lab4源文件包；基线仓库main只有Lab1～Lab3，因此本次为重建版本，不是课程原始框架补丁。

实现了手册列出的三个模块和12个函数：

| 模块 | 函数 | 所在文件 |
| --- | --- | --- |
| 页映射 | page_insert、page_remove | kern/mm/pmm.c |
| 创建与初始化 | alloc_proc、proc_init、kernel_thread、do_fork、copy_thread、copy_mm | kern/process/proc.c |
| 调度与切换 | cpu_idle、schedule、proc_run、switch_to | kern/process/proc.c、kern/schedule/sched.c、kern/process/switch.S |

补齐这些接口依赖的get_pte/get_page、TLB管理、slab堆、VMA描述、PID索引、线程入口和首次返回链。单hart、仅内核线程；copy_mm只接受本实验的NULL用户mm；没有按需分页、换页、用户进程或抢占式调度。

## 2 整体逻辑线

```text
OpenSBI → kern_entry → kern_init
  → idt_init / dtb_init
  → pmm_init：物理页分配器、三级页表、内核4KiB映射
  → kmalloc_init / vmm_init：对象分配与地址区间描述
  → proc_init：idle包装启动现场，创建init
  → clock_init / intr_enable
  → cpu_idle → schedule → proc_run → switch_to
  → 新线程首次：forkret → forkrets → __trapret → sret
  → kernel_thread_entry → fn(arg) → do_exit
  → 切换后由idle回收退出线程的栈和PCB
```

页映射是地址访问和栈对象正常工作的基础；PCB将身份、状态和执行现场组合起来；调度器选择可运行对象，底层切换才能让它真正占用CPU。只创建PCB而没有构造正确的返回现场，不能启动线程；只保存寄存器而没有选择策略，也不构成调度。

## 3 模块一 页映射与引用计数

### 3.1 get_pte与页表层级

采用Sv39的VPN2/VPN1/VPN0三级、每级512项、每项8字节，页内偏移12位。get_pte逐层定位，create为真才分配缺失的中间表。新表清零、引用置1，父项仅设V，避免非法的非叶U/A/D位。

如果第二张中间表分配失败，第一张不能遗留。实现记录本次创建的父槽和Page，失败倒序清槽、释放Page，已有中间表不动。对非规范Sv39地址和已有高层叶子返回NULL；不实现自动拆分大页。

内核启动时先沿用Lab3的1GiB过渡映射，再建立并切到新根页表，以4KiB叶子映射可管理内存的高地址区域。直接映射用于访问物理页、PCB和内核栈，不参与普通可释放页面的引用所有权。

### 3.2 page_insert

先调用get_pte(create=1)，失败返回-E_NO_MEM而不改目标页引用。成功后先增加目标页引用；若旧项指向同页，抵消刚增加的引用，仅更新权限；若指向另一页，撤销旧映射并减少旧页引用，归零时释放。最后写入新PTE并使对应TLB翻译失效。

权限处理保留U/W/X/R请求，对缺少R/X的perm=0补R，对仅W的请求也补R，以形成基础Sv39有效叶子；设置A/D以避免本实验依赖额外的访问/脏位故障处理。此行为及与手册示例的差异详见PROVENANCE.md。

### 3.3 page_remove

get_pte(create=0)不产生新表；无映射静默返回。有效映射先清PTE、失效TLB，再减物理页引用，归零才释放。重复删除不再减引用。中间表不会在每次删除时隐式回收，私有树由page_table_destroy集中递归释放，避免留下其他映射的中间表被误删。

### 3.4 实际验证

- 插入、同页重插、跨地址共享、另一页替换、权限与引用计数检查。
- 无映射删除、重复删除不改变状态。
- 在实际活动页表上读虚拟地址取得页A内容，重映射到页B后读到页B内容，再通过虚拟地址写并核对物理直映地址，验证硬件页表遍历与TLB更新。
- 故障注入分别让第一、第二个中间页分配失败，检查返回值、根项、目标页引用和空闲页数。
- 拒绝非规范地址和已有大页下的普通查找，避免当成下级表错误解引用。

## 4 模块二 线程创建与初始化

### 4.1 alloc_proc

用kmalloc分配PCB，清零后设置state=UNINIT、pid=-1、mm=NULL、pgdir=boot_pgdir_pa，初始化链表节点。context、tf、kstack、runs、flags、name等从安全零状态开始，避免后续误读未初始化内存。

### 4.2 proc_init

初始化proc_list与32桶PID哈希。idleproc直接包装启动执行流，PID0，复用bootstack，need_resched=1，current指向idle，nr_process=1。再通过kernel_thread创建PID1的initproc，设置名称。idle不挂普通proc_list，用作调度兜底。

### 4.3 kernel_thread

构造局部Trap帧模板：s0存fn，s1存arg，epc指向kernel_thread_entry。status设置SPP、SPIE并清SIE，使首次sret返回S模式并恢复中断使能。交给do_fork登记实体，不在kernel_thread中直接调用fn。

### 4.4 do_fork

先检查线程数上限，分配PCB、独立两页内核栈，执行copy_mm，构造线程现场。所有可能失败的资源分配成功后，才在关本地中断区域分配PID、加入proc_list与哈希、增加计数和唤醒。失败按栈→PCB倒序释放，不提前发布半成品对象。

### 4.5 copy_mm与copy_thread

copy_mm的职责是保持可扩展的地址空间接口；当前线程共享内核页表，mm=NULL。若current->mm非空，明确返回-E_INVAL，而不是伪装已经复制用户地址空间。

copy_thread将模板复制到新栈顶部的Trap帧位置，设置子线程a0=0和初始sp；context.ra=forkret，context.sp指向该帧。context是普通函数切换现场，Trap帧是sret恢复现场，两者配合而非重复。

### 4.6 辅助内存和VMA

kmalloc采用32～2048字节的幂次大小slab类，在页头记录对象大小、计数、空闲链和位图；对象全部释放时归还整页。大于2048字节的请求使用连续页，设置上限并在释放时回收。检查覆盖小对象跨slab、多种大小、写入内容和空闲页基线。

mm_struct持有有序VMA链和查找缓存。插入要求区间不重叠；find_vma判断半开区间[start,end)，不把空隙视为映射。mm_destroy释放VMA描述符；这些结构不会自动建立页映射，更没有隐含的缺页分配。

## 5 模块三 调度与上下文切换

### 5.1 schedule

关本地中断，清current->need_resched；从当前链表位置的下一项循环找RUNNABLE，idle则从表头开始。未找到则选idle，增加目标runs并调用proc_run。这里是合作式的循环扫描策略：由idle初次调度或thread_yield/do_exit进入，timer不负责抢占。

### 5.2 proc_run

目标必须可运行；与current相同则无需切换。否则保存中断状态，更新current，lsatp切换根页表并刷新TLB，再switch_to保存旧context并恢复新context。普通线程以后恢复时，从自己的调用栈继续执行local_intr_restore，恢复自己保存的中断状态。

### 5.3 switch_to与首次执行

切换保存ra、sp和s0～s11共14个64位寄存器槽。a0、a1分别是from/to指针；临时/参数寄存器遵守普通调用ABI，不应把异步Trap保存需求照搬为一模一样的切换现场。

首次切换恢复ra=forkret，ret进入forkret；forkrets将sp切到current->tf，跳__trapret；RESTORE_ALL恢复帧，sret到kernel_thread_entry；该包装器a0=s1，用jalr调用s0中的fn，最后把返回值传给do_exit。

### 5.4 退出和回收

do_exit把线程置ZOMBIE并记录返回码，切到其他可运行线程。不能在当前栈上先释放自己；idle在另一栈执行时摘除僵尸的全局/哈希链、释放内核栈和PCB。本实现只提供实验所需的idle集中回收，不声称已经实现完整父子wait/reparent语义。

两个工作线程使用不同seed执行寄存器哨兵测试，至少跨过三个实际timer事件；汇编探针自身也保存调用者的s0～s11/ra，避免测试代码违反被调用者保存约定。最终检查线程完成数、退出码、空链表、nr_process=1、当前仍为idle和资源基线。

## 6 Spec与实际使用的AI提示词

以下为本次任务中落实的规格与给AI的实现指令整理，不是另一个平台原生对话截图，也不是伪造的历史聊天。

### 页操作Spec

输入：pgdir、目标Page、线性地址和权限。输出：插入成功返回0，失败返回-E_NO_MEM；删除幂等。约束：同页重插不释放、旧页引用正确、TLB同步、非叶项合法、分配失败清理完整。

提示词要点：

> 基于现有Lab3 Page与First-Fit接口，按提供手册重建Sv39 get_pte/page_insert/page_remove；保留手册函数签名。新建中间表要记录所有权并失败回滚。同页重插不得释放。以实际活动页表读写验证TLB替换，不以打印字符串替代检查。解释手册非叶PTE_U示例与实际架构规则的差异。

### 线程创建Spec

输入：fn、arg、clone_flags以及fork模板。输出：新PID或负错误。约束：安全PCB默认值、独立栈、mm占位、构造完整首次返回链、失败清理、发布顺序、PID索引一致。

提示词要点：

> 实现alloc_proc/proc_init/kernel_thread/do_fork/copy_thread/copy_mm。idle复用启动栈而非fork；init为PID1。用s0/s1传函数与参数，context.ra预置forkret；不实现用户mm复制，不支持时返回错误。分别测试PCB、栈分配失败和copy_mm拒绝后的页数与进程计数。

### 调度Spec

输入：全局进程链和当前进程，或from/to context。输出：runnable线程实际执行、未找到回idle。约束：切换临界区、current/pgdir/寄存器一致、普通ABI现场、退出后延迟回收。

提示词要点：

> 按单hart合作式循环扫描实现cpu_idle/schedule/proc_run/switch_to。switch只迁移ra/sp/s0～s11，不混入策略。用独立线程不同哨兵反复yield并跨timer事件，检查被调用者寄存器、SP、返回链和最后资源回收。明确本地自检不是官方grade。

## 7 实际遇到的问题与修正

| 问题 | 实际依据 | 处理 |
| --- | --- | --- |
| 现代汇编器不识别旧CSR名称sbadaddr | 首次编译报unknown CSR，原始日志build-first-failure.log | Lab4入口改用stval，布局与槽位不变 |
| 新启用-Wextra后继承代码符号类型比较报错 | kmonitor的循环索引、default_check总页数触发sign-compare | 调整索引和总数类型，不关闭-Werror、不删除断言 |
| Lab3打印十次后关机不适合Lab4生命周期 | 代码审阅确认该策略属于前一实验 | Lab4由线程结束、资源检查与ticks阈值共同决定关机 |
| 初轮线程可能在第一个timer前就完成 | 首轮路径分析发现测试组合覆盖不足 | 工作线程至少运行到ticks>=3，期间反复yield并检查哨兵 |
| 缺失官方Lab4框架 | 压缩包逐项和仓库树检查 | 在来源文件和报告明确重建范围，不捏造官方得分 |

页表回滚、同页重插与PCB失败清理为实现时预先设计并通过故障注入验证的逻辑；没有把“可能发生的错误”伪装成已经复现的bug。

## 8 编译与运行证据

实际环境与时间见`report/logs/summary.json`；本次工具输出为macOS arm64、RISC-V GCC 16.2.0、QEMU 11.1.2、QEMU默认OpenSBI 1.8.1。代码编译显式选择RV64整数ABI，避免未保存浮点状态。

```sh
cd labcodes/lab4
make grade
```

| 构建 | QEMU RAM | 故障注入 | 结果 |
| --- | --- | --- | --- |
| -O2 普通 | 32 MiB | 无 | PASS |
| -O2 普通 | 128 MiB | 无 | PASS |
| -O2 普通 | 256 MiB | 无，仅管理前128 MiB | PASS |
| -O2 LAB4_SELFTEST | 32 MiB | 页表、PCB、栈、copy_mm拒绝 | PASS |
| -O2 LAB4_SELFTEST | 128 MiB | 同上 | PASS |
| -O0 LAB4_SELFTEST | 128 MiB | 同上 | PASS |
| 最终重建-O2普通版本 | 128 MiB | 无 | PASS |

共6组配置测试，加1次最终普通镜像重建与启动。每次记录QEMU退出码、缺失成功标记、运行时长和镜像哈希；PANIC或超时均判失败。**这不是课程官方评分，不能写成“官方100分”。**

典型串口片段（完整原始输出在logs）：

```text
Sv39 kernel 4K mappings: PASS
check_pgdir() succeeded! refcounts, replace, remove, hardware TLB: PASS
check_kmalloc(): slab reuse and reclamation PASS
vmm_init(): ordered VMA, cache, gaps, destruction PASS
proc_init(): idle=0 init=1, first-run frame PASS
initproc pid=1 parent=0: Hello kernel thread
100 ticks
thread context + first return + yields + exit/reap: PASS
resource baseline restored (except idle PCB slab): PASS
timer interrupts during threaded kernel ticks=100: PASS
LAB4 SELF CHECKS PASSED
```

最终保留的idle PCB占用一个非空slab页，因此回收基线比较明确扣除了这一页；内核页表本身是系统常驻资源，不在每次线程退出时释放。

没有伪造交互截图。日志是本次命令实际生成的文本，未证明教师接受、平台提交成功或真机运行。

## 9 Challenge与OS原理联系

### 中断保存与恢复

local_intr_save读取SIE，若原开启则关并记录1，原关闭则记录0；restore只在标志1时开启。由此嵌套内层不会意外开启外层临界区。只有本hart的屏蔽范围受到影响，其他hart不能因此互斥；多核还需要锁和内存序保证。

### 不同分页模式与重复遍历代码

Sv32两级，每级10位索引和4字节PTE；Sv39三级，每级9位索引、8字节PTE；Sv48四级，每级同为9位。多级get_pte反复做相同的项定位、有效性判断、必要分配和下级指针转换，所以形式相似。本实现用level循环而非复制两段代码。

查找和创建合并方便调用，但create=1有分配副作用，错误清理更复杂。拆分lookup和ensure接口可以更清晰表达意图；无论是否拆分，跨多级的部分成功都必须管理所有权和回滚。当前保留手册get_pte(create)形式，内部显式事务回滚。

### 重要知识点

| 实验概念 | OS原理联系 | 差异或边界 |
| --- | --- | --- |
| 页表映射、TLB失效 | 地址转换、缓存一致性 | 单hart本地sfence，不含跨核shootdown |
| 引用计数 | 共享物理页生命周期 | 当前不包含COW和换页引用 |
| VMA | 逻辑地址空间分区与访问属性 | 描述存在不等于页表已映射或按需分页已实现 |
| PCB/context/Trap帧 | 执行实体管理与现场恢复 | 普通调用上下文与异步Trap现场不同 |
| idle/init | 从启动执行流建立调度系统 | 仅内核态，不是用户init程序 |
| runnable循环选择 | 就绪队列与调度 | 合作式，不是抢占时间片 |
| ZOMBIE后回收 | 进程资源生命周期 | idle简化回收，不是完整wait语义 |
| slab对象分配 | 物理页之上的小对象管理 | 简化大小类，无多核缓存和完整SLUB机制 |

未对应的重要知识点：用户进程隔离与装载、系统调用权限校验、按需分页与页面置换、COW、抢占式调度及优先级、完整阻塞唤醒/等待语义、多核锁与调度、文件系统、设备中断处理和I/O。

## 10 来源与复现位置

- 指导书Lab4练习：`lab4_2_1_exercises.html`；项目结构：`lab4_2_2_files.html`；报告要求：`lab4_5_labs_requirement.html`。
- 继承基线：本仓库`30ab53c`的`labcodes/lab3`。
- 具体差异：[PROVENANCE.md](PROVENANCE.md)。
- 自检入口：[tools/grade.py](tools/grade.py)，结果：[report/logs/summary.json](report/logs/summary.json)。
- 答辩提纲：[DEFENSE.md](DEFENSE.md)。
