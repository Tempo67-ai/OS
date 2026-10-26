# 三窗口、双 GDB 调试：从零照做

已在 2026-10-26 本机实际验证进入 QEMU 4.1.1 的 get_physical_address，日志见 ../logs/qemu-walk-verified.txt。
调试版 QEMU 位于 WSL：
/var/tmp/oslab-qemu-debug/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64
没有替换系统 QEMU。该路径在 Linux 文件系统中，不是 Windows 文件夹。
源码已编译带调试信息，无需再次编译。根页表和入口地址随内核重新编译可能变化。

## 三个窗口的角色

A：运行模拟器。B：宿主 gdb，查看 QEMU 的 C 源码。
C：RISC-V gdb，控制被模拟的内核。
B 停住 QEMU 时，C 也会等待，这是正常的。必须回 B 输入 continue 才能使 C 继续响应。
B 中的 env/addr/base/pte 是模拟器变量，C 中的 $pc/$satp 是被模拟 CPU 的寄存器。

## 第一步：窗口 A

打开 PowerShell，执行：
wsl -d Ubuntu
cd /mnt/d/GPT浏览器下载/lab2/submission/code
make -j4
使用下面已实际验证的启动命令（参数与 make debug 的 -s -S 对应）：
/var/tmp/oslab-qemu-debug/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64 -machine virt -nographic -bios /usr/lib/riscv64-linux-gnu/opensbi/generic/fw_jump.bin -kernel bin/ucore.img -s -S


保持 A 打开，没输出是 -S 正在等。

## 第二步：窗口 B，先控制模拟器

另开 PowerShell：
wsl -d Ubuntu
pgrep -af qemu-system

找到命令行包含 /var/tmp/oslab-qemu-debug 的那一行，记下最左边 PID。
假设看到 335，下面用 335；你自己的数字很可能不同：
sudo gdb -q /var/tmp/oslab-qemu-debug/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64 -p 335

如果提示下载 debuginfo，输入 n。
如果 sudo 询问 Linux 密码，由你本人输入，密码输入时无显示。
在 (gdb) 后逐行输入：
set pagination off
set style enabled off
handle SIGPIPE nostop noprint
break get_physical_address if addr == 0xffffffffc02000d6
continue

这个地址来自当前内核的 kern_init。若已改内核，应先用基础调试的 p/x &kern_init 查新地址。
现在 B 显示 Continuing，转到 C。

## 第三步：窗口 C，启动客体

另开 PowerShell：
wsl -d Ubuntu
cd /mnt/d/GPT浏览器下载/lab2/submission/code
make gdb

在 (gdb) 后：
continue

此时 B 应停在 get_physical_address。C 暂时没有返回是正常的。
取 kern_init 的指令也是一次内存访问，access_type=MMU_INST_FETCH，不是数据 load/store。
指导书也允许以 C 入口这一必经地址作为观察对象。

## 第四步：回到 B，先证明调用路径

p/x addr
p/x env->satp
bt 8

本次实测：
addr=0xffffffffc02000d6
env->satp=0x8000000000080205
调用栈为 get_page_addr_code -> tlb_fill -> riscv_cpu_tlb_fill -> get_physical_address。
bt 打印从当前函数往调用者看，因此显示顺序反过来。

解释：当前首次取高地址指令，没有可用 TLB 项，因此走慢路径查询页表。

## 第五步：B 中看根页表和叶子

break cpu_helper.c:268 if addr == 0xffffffffc02000d6
continue
p/x base
p levels
p ptshift
p idx
p/x pte_addr
p/x pte
p/x ppn

本次实测：
base=0x80205000
levels=3
ptshift=18
idx=511
pte_addr=0x80205ff8
pte=0x200000cf
ppn=0x80000

268 行在这个官方版本中位于读出 PTE 并做了初步叶子检查之后。
解释：
1. base 是根页表物理地址。
2. idx=VPN2=511。
3. pte_addr=base+511*8，因为一项占8字节。
4. ldq_phys 从模拟机的物理内存读出该项。
5. 0xcf 含 R/W/X，所以是叶子；根层叶子表示1GiB大页。
6. ptshift=18 表示物理页号还要拼接低两级VPN，共18位。它不是虚拟地址直接右移18位，也不是页大小只有2^18字节。

若想从 for 循环逐行看，可在新一轮启动中把断点放到 cpu_helper.c:236，随后用 n 前进；list 先确认当前版本对应源码行，不要把不同 QEMU 版本的行号混用。

## 第六步：B 中看翻译结果

break cpu_helper.c:345 if addr == 0xffffffffc02000d6
continue
p/x *physical
p/x *physical | (addr & 0xfff)

本次：*physical=0x80200000，补低12位后=0x802000d6。
这个 QEMU 版本的 get_physical_address 返回物理4KiB页基址，用于 TLB 填充；
完整访问地址还需要原始虚拟地址低12位。不能把页基址误当成完整字节地址。

解释源码：
vpn=addr>>12
*physical=(ppn | (vpn & ((1L<<ptshift)-1)))<<12
根层大页需要把 VPN1/VPN0 拼进物理页号；本例低层VPN对应0x200000这一大页内偏移。

## 第七步：B 中证明 TLB 回填

finish
p/x pa
list
break tlb_set_page
continue
p/x vaddr
p/x paddr
p size
bt 5

finish 返回 riscv_cpu_tlb_fill 后，pa 为查表得到的物理页基址。
tlb_set_page 中本次预期 vaddr=0xffffffffc0200000、paddr=0x80200000、size=4096。
若读到其他地址，应先 bt 并核实该次调用；可使用条件断点：
break tlb_set_page if vaddr == 0xffffffffc0200000

QEMU 即使客体页表用1GiB大页，本版本该路径也按 TARGET_PAGE_SIZE 维护软件TLB项，不能把两者粒度混淆。
finish 和下一次 tlb_set_page 的详细命令建议先自行演练；本次已直接验证到 *physical，调用源码确认随后回填。

## 第八步：TLB 查询代码在哪？

B：
list /var/tmp/oslab-qemu-debug/qemu-4.1.1/accel/tcg/cputlb.c:1024

查看 get_page_addr_code 中：
if (!tlb_hit(entry->addr_code, addr))
若 victim TLB 也未命中，才调用 tlb_fill。

数据访问慢路径在同文件 load_helper（约1248行）和 store_helper（约1501行），也检查 tlb_hit。
有些命中快路径由 TCG 直接生成宿主机器码，因此不能说“每次访存都进入这些 C 函数”。

## 结束

B：
disable breakpoints
detach
quit

C 如果仍在运行，Ctrl+C，然后：
monitor quit
quit

A 应退出。若 C 连接已失效，可在 B detach 之后到 A 按 Ctrl+A 再 X。
不要把 gdb 的命令输入 Linux 的 $ 提示符；也不要把 make 输入 (gdb)。

## 常见卡住

- Connection refused：A 没启动 make debug，或已退出；先 A 后 C。
- Address already in use：另一个 QEMU 已占1234，退出原来那一个，不要重复启动多个。
- B停住、C没反应：回B continue。
- 断点从不触发：确认用调试版、地址是当前kern_init、内核尚未运行过该地址；最好全退出后重来。
- No symbol：系统发布版QEMU通常无完整符号，改用本文 /var/tmp 下的调试版。
- <optimized out>：变量被优化，或尚未赋值；当前 --enable-debug 版本多数可读；不能随便把内核 -O2 改掉后仍套用旧地址。
- SIGPIPE：按上述 handle 命令让GDB不因该信号停住，信号仍交给程序。

## 在其他电脑重建调试版

本文的 /var/tmp 路径仅表示已在本机准备的环境，Git 仓库不包含 QEMU 二进制或源码压缩包。在其他 Ubuntu 环境可下载官方源码并单独构建：

    sudo apt-get update
    sudo apt-get install -y build-essential python3 pkg-config libglib2.0-dev libpixman-1-dev zlib1g-dev bzip2 gdb gdb-multiarch
    wget https://download.qemu.org/qemu-4.1.1.tar.xz
    tar -xJf qemu-4.1.1.tar.xz
    cd qemu-4.1.1
    ./configure --target-list=riscv64-softmmu --enable-debug --disable-werror --disable-docs --disable-gtk --disable-sdl --disable-vnc --disable-capstone --disable-slirp
    make -j8

使用实际编译路径替换本文 /var/tmp 路径。不要执行 make install 来覆盖系统 QEMU。
固件路径也需按本机 OpenSBI 安装位置调整；本文使用 fw_jump.bin 的方式已在本机实测。