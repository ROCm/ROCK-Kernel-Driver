#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <asm/segment.h>
#include <asm/cmpxchg.h>


static inline unsigned long read_cr8(void)
{
	return 0;
}

static inline void write_cr8(unsigned long val)
{
	BUG_ON(val);
}

#include <linux/irqflags.h>

#endif
