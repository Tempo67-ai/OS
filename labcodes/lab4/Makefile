GCCPREFIX ?= riscv64-elf-
QEMU ?= qemu-system-riscv64
OPT ?= -O2
DEFS ?=
CC = $(GCCPREFIX)gcc
LD = $(GCCPREFIX)ld
OBJCOPY = $(GCCPREFIX)objcopy
OBJDUMP = $(GCCPREFIX)objdump
INCLUDES = -Ilibs $(addprefix -I,$(wildcard kern/*))
CFLAGS = -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -std=gnu99 $(OPT) -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -msmall-data-limit=0 -nostdinc $(INCLUDES) $(DEFS)
SOURCES = $(wildcard libs/*.c kern/*/*.c)
ASMS = kern/init/entry.S $(filter-out kern/init/entry.S,$(wildcard kern/*/*.S))
OBJECTS = $(addprefix obj/,$(ASMS:.S=.o)) $(addprefix obj/,$(SOURCES:.c=.o))
.PHONY: all clean qemu debug gdb grade
all: bin/ucore.img
obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
obj/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
bin/kernel: $(OBJECTS) tools/kernel.ld
	@mkdir -p bin
	$(LD) -m elf64lriscv -nostdlib -T tools/kernel.ld -o $@ $(OBJECTS)
	$(OBJDUMP) -d $@ > bin/kernel.asm
bin/ucore.img: bin/kernel
	$(OBJCOPY) -O binary $< $@
qemu: all
	$(QEMU) -machine virt -m 128M -smp 1 -nographic -bios default -kernel bin/ucore.img -monitor none
debug: all
	$(QEMU) -machine virt -m 128M -smp 1 -nographic -bios default -kernel bin/ucore.img -monitor none -s -S
gdb:
	$(GCCPREFIX)gdb bin/kernel -ex 'target remote localhost:1234'
grade:
	python3 tools/grade.py
clean:
	rm -rf obj bin
-include $(OBJECTS:.o=.d)
