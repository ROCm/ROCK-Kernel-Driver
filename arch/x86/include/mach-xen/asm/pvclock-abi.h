#ifndef _ASM_X86_PVCLOCK_ABI_H
#define _ASM_X86_PVCLOCK_ABI_H
#ifndef __ASSEMBLY__

#include <xen/interface/xen.h>

#define pvclock_vcpu_time_info vcpu_time_info
struct pvclock_wall_clock; /* not used */

#define PVCLOCK_TSC_STABLE_BIT	(1 << 0)
#endif /* __ASSEMBLY__ */
#endif /* _ASM_X86_PVCLOCK_ABI_H */
