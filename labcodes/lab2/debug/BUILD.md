# QEMU 4.1.1 调试构建记录（macOS arm64）

源： https://download.qemu.org/qemu-4.1.1.tar.xz ，原始压缩包保存在本机工作目录；本提交包不含QEMU源码和构建产物。复现时请从官方地址下载，解压到debug/qemu-4.1.1，在debug/build中构建，并先应用tci-buffer.patch。

本机原有glib/pixman/dtc等依赖；本次新增Homebrew pkgconf用于配置依赖发现。编译目录为 `debug/build`。不执行make install，不替换系统QEMU。

原生TCG版构建成功，但运行报 `Could not allocate dynamic translator buffer`，日志见 `../report/logs/qemu4-kernel.log`。为避开旧版QEMU对现代Apple Silicon可执行内存的兼容问题，改用QEMU自带TCG Interpreter（TCI），并仅在 `CONFIG_TCG_INTERPRETER` 下移除缓冲区的PROT_EXEC；这是解释器字节码缓冲区，不会在其中执行宿主机器码。MMU和TLB逻辑保持原样。

最终配置命令（从本目录的build子目录执行）：

```sh
../qemu-4.1.1/configure \
 --target-list=riscv64-softmmu --enable-debug --enable-tcg-interpreter \
 --disable-werror --disable-cocoa --disable-sdl --disable-gtk \
 --disable-vnc --disable-curses --disable-user --disable-tools \
 --disable-nettle --disable-gnutls --disable-capstone --disable-libssh \
 --disable-hvf --disable-hax --disable-libusb --disable-curl \
 --disable-slirp --disable-guest-agent --audio-drv-list= --enable-fdt \
 --extra-cflags=-I/opt/homebrew/include --extra-ldflags=-L/opt/homebrew/lib \
 --python=/opt/homebrew/bin/python3.12
make -j6
```

早期配置先缺少pkg-config，之后未发现系统libfdt而错误尝试空的内置dtc目录；增加Homebrew include/lib目录修正。旧版不接受 `--enable-fdt=system`，应使用 `--enable-fdt` 让它探测系统libfdt。Python3.12有旧脚本语法警告，但最终构建成功，原始输出在evidence。

宿主为arm64 macOS，源码调试使用系统LLDB；客体RISC-V调试使用riscv64-elf-gdb。按指导书给出的条件断点替代方案，宿主调试可以单独完成，不必同时启两个调试器。

复现：

```sh
python3 debug/run_lldb.py mmu
python3 debug/run_lldb.py tlb
```

脚本采用当前内核入口VA条件及QEMU4.1.1源码行号。更换内核布局或QEMU版本应重新确认断点，不能机械照搬。
