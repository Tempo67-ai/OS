# Lab2：物理内存管理与页表

成员：刘琦（2311764）、李金璇（2312418）、赵非凡（2411020）。

- [实验报告（含8张截图）](report/实验报告_含截图.md)
- [离线HTML报告](report/实验报告_含截图.html)
- [原始运行日志](report/logs/)
- [截图来源与校验值](report/screenshot_manifest.json)
- [Spec与提示词复现材料](PROMPTS.md)
- [QEMU4.1.1源码调试说明](debug/BUILD.md)

```sh
cd labcodes/lab2
python3 tools/verify.py
make GCCPREFIX=riscv64-elf- grade
python3 tools/debug_guest.py
```

默认使用Best-Fit。初始化与Best-Fit官方评分20/20，Buddy与简化SLUB另有内核自测。已验证8/32/64/128/256 MiB启动，框架的内存管理上限为128 MiB。截图为真实运行日志查看页的浏览器截图，不是交互式终端截图。

QEMU源码与构建产物未纳入Git；源码调试采用QEMU4.1.1/TCI/LLDB，按debug/BUILD.md准备后，从本目录运行python3 debug/run_lldb.py mmu或tlb。
