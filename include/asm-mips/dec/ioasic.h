/*
 *  linux/asm-mips/dec/ioasic.h
 *
 *  Copyright (C) 2000  Maciej W. Rozycki
 *
 *  DEC I/O ASIC access operations.
 */

#ifndef __ASM_DEC_IOASIC_H
#define __ASM_DEC_IOASIC_H

extern volatile unsigned int *ioasic_base;

extern inline void ioasic_write(unsigned int reg, unsigned int v)
{
	ioasic_base[reg / 4] = v;
}

extern inline unsigned int ioasic_read(unsigned int reg)
{
	return ioasic_base[reg / 4];
}

#endif /* __ASM_DEC_IOASIC_H */
