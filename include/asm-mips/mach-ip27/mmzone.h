#ifndef _ASM_MACH_MMZONE_H
#define _ASM_MACH_MMZONE_H

#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>

#define pa_to_nid(addr)		NASID_TO_COMPACT_NODEID(NASID_GET(addr))

#endif /* _ASM_MACH_MMZONE_H */
