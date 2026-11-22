.text
.globl kernel_thread_entry
kernel_thread_entry:
    mv a0, s1
    jalr s0
    # a0 contains fn(arg)'s return code.
    call do_exit
1:  j 1b
