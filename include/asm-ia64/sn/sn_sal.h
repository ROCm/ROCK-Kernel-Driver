#ifndef _ASM_IA64_SN_SAL_H
#define _ASM_IA64_SN_SAL_H

/*
 * System Abstraction Layer definitions for IA64
 *
 *
 * Copyright (C) 2000, Silicon Graphics.
 * Copyright (C) 2000. Jack Steiner (steiner@sgi.com)
 */


#include <asm/sal.h>


// SGI Specific Calls
#define  SN_SAL_POD_MODE                           0x02000001
#define  SN_SAL_SYSTEM_RESET                       0x02000002
#define  SN_SAL_PROBE                              0x02000003


u64 ia64_sn_probe_io_slot(long paddr, long size, void *data_ptr);


#endif /* _ASM_IA64_SN_SN1_SAL_H */
