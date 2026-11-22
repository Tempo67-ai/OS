# 一键答辩演示

## 启动

macOS：在Finder中打开Lab4目录，双击 `一键答辩演示.command`。如果系统没有关联该文件，打开终端执行：

```sh
cd labcodes/lab4
python3 tools/defense_demo.py
```

Linux/WSL使用同一条Python命令。脚本仅使用Python标准库，仍需已安装RISC-V GCC/binutils、RISC-V GDB、QEMU和GNU Make。优先自动识别riscv64-elf-，其次riscv64-unknown-elf-；不自动安装工具。

每一阶段结束后按回车继续，便于停下来讲解；Ctrl+C停止。所有步骤连续自动执行（预演/录屏）：

```sh
python3 tools/defense_demo.py --auto
```

## 演示内容与讲解

1. **正常构建与启动**：看页表、VMA、init线程、100 ticks和资源回收输出。解释“创建对象”与“真的切换执行”的区别。
2. **核心代码**：自动输出page_insert/page_remove/copy_thread/schedule/proc_run及switch.S，带真实源码行号。说明引用计数、首次现场和策略/机制分工。
3. **GDB断点链**：脚本自动启用仅绑定127.0.0.1的临时调试端口，不需要自己开第二个终端；真实显示idle/init的PID、context与Trap帧，a0/a1、汇编及首次sret入口。结束detach后内核继续完成运行。
4. **完整本地矩阵**：运行6组配置和最终普通镜像复测，显示真实结果；明确不是官方评分。

第一次建议提前跑完整流程。若答辩现场时间短，可跳过完整矩阵：

```sh
python3 tools/defense_demo.py --quick
```

明确不演示GDB时可使用 `--skip-gdb`。这种情况会在日志中标为skipped，不声称GDB通过。默认模式要求存在GDB，不会默默跳过。

工具名称不一致时可设置：

```sh
GCCPREFIX=riscv64-unknown-elf- GDB=riscv64-unknown-elf-gdb python3 tools/defense_demo.py
```

## 日志与退出

脚本在临时源码副本中构建，不改原工程的bin/obj，不覆盖report/logs原有结果。当前演示记录保存在原Lab4目录的 `demo_logs/日期时间/`，已加入gitignore。

关键文件：build.log、normal-serial.log、gdb.log、gdb-serial.log、demo.gdb、matrix.log、matrix-evidence和demo-summary.json。保存的demo.gdb仅用于核对该次命令，其端口是动态端口，不应用它直接连接另一场演示。

正常阶段要求QEMU返回0、包含成功标记且没有PANIC；GDB必须完成实际断点链，detach后客体也要完成自检；矩阵要求结果passed。失败或超时返回非零，不用固定文本冒充运行结果。macOS启动器最后保留窗口，供阅读退出码；--auto建议直接调用Python以避免启动器末尾的关闭提示。

## 本机验证记录

本次已在macOS arm64环境实际执行 `python3 tools/defense_demo.py --auto`，正常运行、源码展示、GDB首次切换链与完整矩阵均成功。相关原始输出的副本见 `report/demo-validation/`。工具版本沿用实际环境中的RISC-V GCC 16.2.0、QEMU 11.1.2；其他机器应先预演，不能从这次结果推断所有版本都兼容。
