#ifndef _ASM_M32R_DELAY_H
#define _ASM_M32R_DELAY_H

/* $Id$ */

/*
 * Copyright (C) 1993 Linus Torvalds
 *
 * Delay routines calling functions in arch/m32r/lib/delay.c
 */

extern void __bad_udelay(void);

extern void __udelay(unsigned long usecs);
extern void __const_udelay(unsigned long usecs);
extern void __delay(unsigned long loops);

#define udelay(n) (__builtin_constant_p(n) ? \
	((n) > 20000 ? __bad_udelay() : __const_udelay((n) * 4294UL)) : \
	__udelay(n))

#endif /* _ASM_M32R_DELAY_H */
