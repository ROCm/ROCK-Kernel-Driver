/*
 *  PC-9800 machine types.
 *
 *  Copyright (C) 1999	TAKAI Kosuke <tak@kmc.kyoto-u.ac.jp>
 *			(Linux/98 Project)
 */

#ifndef _ASM_PC9800_H_
#define _ASM_PC9800_H_

#include <asm/pc9800_sca.h>
#include <asm/types.h>

#define __PC9800SCA(type, pa)	(*(type *) phys_to_virt(pa))
#define __PC9800SCA_TEST_BIT(pa, n)	\
	((__PC9800SCA(u8, pa) & (1U << (n))) != 0)

#define PC9800_HIGHRESO_P()	__PC9800SCA_TEST_BIT(PC9800SCA_BIOS_FLAG, 3)
#define PC9800_8MHz_P()		__PC9800SCA_TEST_BIT(PC9800SCA_BIOS_FLAG, 7)

				/* 0x2198 is 98 21 on memory... */
#define PC9800_9821_P()		(__PC9800SCA(u16, PC9821SCA_ROM_ID) == 0x2198)

/* Note PC9821_...() are valid only when PC9800_9821_P() was true. */
#define PC9821_IDEIF_DOUBLE_P()	__PC9800SCA_TEST_BIT(PC9821SCA_ROM_FLAG4, 4)

#endif
