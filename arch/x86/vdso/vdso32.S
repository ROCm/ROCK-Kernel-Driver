#include <linux/init.h>

__INITDATA

	.globl vdso32_default_start, vdso32_default_end
vdso32_default_start:
#ifdef CONFIG_X86_32
	.incbin "arch/x86/vdso/vdso32-int80.so"
#else
	.incbin "arch/x86/vdso/vdso32-syscall.so"
#endif
vdso32_default_end:

	.globl vdso32_sysenter_start, vdso32_sysenter_end
vdso32_sysenter_start:
	.incbin "arch/x86/vdso/vdso32-sysenter.so"
vdso32_sysenter_end:

#if defined(CONFIG_X86_64_XEN) && CONFIG_XEN_COMPAT < 0x030200
	.globl vdso32_int80_start, vdso32_int80_end
vdso32_int80_start:
	.incbin "arch/x86/vdso/vdso32-int80.so"
vdso32_int80_end:
#elif defined(CONFIG_X86_XEN)
	.globl vdso32_syscall_start, vdso32_syscall_end
vdso32_syscall_start:
	.incbin "arch/x86/vdso/vdso32-syscall.so"
vdso32_syscall_end:
#endif

__FINIT
