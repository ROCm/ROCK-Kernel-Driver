/*
 *  include/asm-s390/delay.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/delay.h"
 *    Copyright (C) 1993 Linus Torvalds
 *
 *  Delay routines calling functions in arch/i386/lib/delay.c
 */
 
#ifndef _S390_DELAY_H
#define _S390_DELAY_H

extern void __udelay(unsigned long usecs);
extern void __const_udelay(unsigned long usecs);
extern void __delay(unsigned long loops);

#define udelay(n) (__builtin_constant_p(n) ? \
	__const_udelay((n) * 0x10c6ul) : \
	__udelay(n))

#endif /* defined(_S390_DELAY_H) */
