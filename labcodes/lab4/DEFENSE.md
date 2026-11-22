# Lab4 答辩要点

## 一分钟主线

在Lab3“能处理中断”的基础上，先实现页映射的插入与删除，保证PTE、Page引用计数和TLB一致；再用PCB、内核栈、Trap帧和context组织内核线程；最后用合作式调度从idle切换到init。新线程第一次不是返回普通C调用，而是沿预构造的forkret→forkrets→__trapret→sret→kernel_thread_entry进入fn(arg)。

## 页映射为什么不能直接覆盖PTE

旧页可能还有其他映射，需减引用；引用为0才归还物理页。同一物理页重插只更新权限，不应临时释放它。因此先增加新页引用，若旧页就是新页则抵消一次；若是另一页，则移除旧映射，最后写新项并失效相应TLB。

## get_pte失败怎么处理

Sv39要走VPN2、VPN1、VPN0；可能新建两张中间表。记录本次新建的父槽和物理页，后续失败则倒序清父槽、清引用并释放。已有表不能作为“本次分配”误删。测试通过故障预算在第1、第2次分配处失败，并检查PTE/引用/空闲页保持一致。

## PCB中有哪些关键字段

state状态，pid身份，runs被选择次数，kstack独立栈，need_resched调度请求，parent父线程，mm用户地址空间描述（本实验NULL），context普通调用切换现场，tf首次/异常返回现场，pgdir页表物理地址，name名称，以及全局链表和PID哈希节点。

## idle与init有什么区别

idle包住现有启动执行流，PID0、复用bootstack；init通过kernel_thread/do_fork创建，PID1、独立分配内核栈、parent=idle。两者共享内核页表。

## context与trapframe有什么区别

switch_to是普通调用边界，只需按整数ABI保存callee-saved寄存器s0～s11，以及ra/sp。异步Trap要保护被打断现场，保存所有GPR槽及状态。新建线程把context.ra预设为forkret，用普通ret启动恢复链；trapframe.epc预设为kernel_thread_entry，再用sret启动线程函数包装器。

## 为什么移动sp到a0

在__alltraps中a0=sp，把trapframe指针作为C函数第一个参数；而forkrets中sp=a0，是把传入的目标trapframe作为恢复栈顶。方向不同，目的不同。

## schedule与proc_run与switch_to分工

schedule选线程；proc_run更新current与satp并保持切换临界区；switch_to只迁移寄存器，不负责调度策略。本实现循环扫描runnable，找不到则idle；timer只计数，线程使用thread_yield合作让出。不能称为已经实现时间片抢占。

## 为什么退出不能立即释放自己的栈

do_exit仍运行在当前内核栈。若先释放该栈，后面的C调用与切换仍会用到已释放内存。先标记ZOMBIE并切走，再由idle在另一条栈上释放PCB/内核栈。

## 中断保存恢复为什么要记原值

外层关中断，内层必须恢复为仍关闭；如果内层无条件开启会破坏外层临界区。切换后proc_run在目标线程恢复的不是同一个C调用链；各线程栈上的保存变量随自身context继续生效。新线程则从构造好的sstatus经sret恢复中断使能。

## Challenge 分页模式

Sv32通常两级、每级1024个4字节项、10+10+12；Sv39三级、每级512个8字节项、9+9+9+12；Sv48四级、9+9+9+9+12。get_pte中相似代码是每层相同的“定位项→必要时分配→走向下级”，不是两份无意义重复。可以拆成lookup/create接口减少隐式副作用，但创建仍需事务式失败清理；本实验采用create参数并显式回滚。

## 准确描述结果

展示`make grade`和`report/logs/summary.json`，说明这是重建版自检。没有取得原始课程框架，未跑官方评分，不说官方100分；个人分工和AI使用按实际情况回答。
