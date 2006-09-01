#include <linux/init.h>

__INITDATA

	.globl vsyscall_int80_start, vsyscall_int80_end
vsyscall_int80_start:
	.incbin "arch/i386/kernel/vsyscall-int80.so"
vsyscall_int80_end:

	.globl vsyscall_sysenter_start, vsyscall_sysenter_end
vsyscall_sysenter_start:
	.incbin "arch/i386/kernel/vsyscall-sysenter.so"
vsyscall_sysenter_end:

#if defined(CONFIG_XEN) && defined(CONFIG_COMPAT_VDSO)

	.align 4

	.globl vdso_rel_int80_start, vdso_rel_int80_end
vdso_rel_int80_start:
	.include "arch/i386/kernel/vsyscall-int80.rel"
vdso_rel_int80_end:

	.globl vdso_rel_sysenter_start, vdso_rel_sysenter_end
vdso_rel_sysenter_start:
	.include "arch/i386/kernel/vsyscall-sysenter.rel"
vdso_rel_sysenter_end:

#endif

__FINIT
