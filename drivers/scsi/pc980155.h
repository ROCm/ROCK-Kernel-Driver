/*
 *
 *  drivers/scsi/pc980155.h
 *
 *  PC-9801-55 SCSI host adapter driver
 *
 *  Copyright (C) 1997-2003  Kyoto University Microcomputer Club
 *			     (Linux/98 project)
 *			     Tomoharu Ugawa <ohirune@kmc.gr.jp>
 *
 */

#ifndef __PC980155_H
#define __PC980155_H

#include "wd33c93.h"

#define REG_ADDRST (base_io)
#define REG_CONTRL (base_io + 2)
#define REG_CWRITE (base_io + 4)
#define REG_STATRD (base_io + 4)

#define WD_MEMORYBANK	0x30
#define WD_RESETINT	0x33

static inline uchar read_pc980155(const wd33c93_regs regs, uchar reg_num)
{
	outb(reg_num, regs.SASR);
	return (uchar)inb(regs.SCMD);
}

static inline void write_memorybank(const wd33c93_regs regs, uchar value)
{
      outb(WD_MEMORYBANK, regs.SASR);
      outb(value, regs.SCMD);
}

#define read_pc980155_resetint(regs) \
	read_pc980155((regs), WD_RESETINT)
#define pc980155_int_enable(regs) \
	write_memorybank((regs), read_pc980155((regs), WD_MEMORYBANK) | 0x04)

#define pc980155_int_disable(regs) \
	write_memorybank((regs), read_pc980155((regs), WD_MEMORYBANK) & ~0x04)

#define pc980155_assert_bus_reset(regs) \
	write_memorybank((regs), read_pc980155((regs), WD_MEMORYBANK) | 0x02)

#define pc980155_negate_bus_reset(regs) \
	write_memorybank((regs), read_pc980155((regs), WD_MEMORYBANK) & ~0x02)

#endif /* __PC980155_H */
