#ifndef __UM_SOFTIRQ_H
#define __UM_SOFTIRQ_H

#include "linux/smp.h"
#include "asm/system.h"
#include "asm/processor.h"

/* A gratuitous name change */
#define i386_bh_lock um_bh_lock
#include "asm/arch/softirq.h"
#undef i386_bh_lock

#endif
