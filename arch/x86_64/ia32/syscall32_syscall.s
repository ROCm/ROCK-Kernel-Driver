/* 32bit VDSOs mapped into user space. */

.section ".init.data","aw"

syscall32_syscall:
.incbin "arch/x86_64/ia32/vsyscall-syscall.so"
syscall32_syscall_end:

syscall32_sysenter:
.incbin "arch/x86_64/ia32/vsyscall-sysenter.so"
syscall32_sysenter_end:

.previous
