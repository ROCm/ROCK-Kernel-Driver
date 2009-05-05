#ifndef _ASM_X86_SYSTEM_64_H
#define _ASM_X86_SYSTEM_64_H

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

#endif /* _ASM_X86_SYSTEM_64_H */
