/*
 *  include/asm-i386/mach-pc9800/mach_traps.h
 *
 *  Machine specific NMI handling for PC-9800.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef _MACH_TRAPS_H
#define _MACH_TRAPS_H

static inline void clear_mem_error(unsigned char reason)
{
	outb(0x08, 0x37);
	outb(0x09, 0x37);
}

static inline unsigned char get_nmi_reason(void)
{
	return (inb(0x33) & 6) ? 0x80 : 0;
}

static inline void reassert_nmi(void)
{
	outb(0x09, 0x50);	/* disable NMI once */
	outb(0x09, 0x52);	/* re-enable it */
}

#endif /* !_MACH_TRAPS_H */
