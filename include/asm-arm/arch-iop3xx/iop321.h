/*
 * linux/include/asm/arch-iop3xx/iop321.h
 *
 * Intel IOP321 Chip definitions
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IOP321_HW_H_
#define _IOP321_HW_H_

/*
 * IOP321 I/O and Mem space regions for PCI autoconfiguration
 */
#define	IOP321_PCI_IO_BASE		0x90000000
#define	IOP321_PCI_IO_SIZE		0x00010000
#define IOP321_PCI_MEM_BASE		0x40000000
#define IOP321_PCI_MEM_SIZE		0x40000000

/*
 * IOP321 chipset registers
 */
#define IOP321_VIRT_MEM_BASE 0xfff00000  /* chip virtual mem address*/
#define IOP321_PHY_MEM_BASE  0xffffe000  /* chip physical memory address */
#define IOP321_REG_ADDR(reg) (IOP321_VIRT_MEM_BASE | (reg))

/* Reserved 0x00000000 through 0x000000FF */

/* Address Translation Unit 0x00000100 through 0x000001FF */
#define IOP321_ATUVID     (volatile u16 *)IOP321_REG_ADDR(0x00000100)
#define IOP321_ATUDID     (volatile u16 *)IOP321_REG_ADDR(0x00000102)
#define IOP321_ATUCMD     (volatile u16 *)IOP321_REG_ADDR(0x00000104)
#define IOP321_ATUSR      (volatile u16 *)IOP321_REG_ADDR(0x00000106)
#define IOP321_ATURID     (volatile u8  *)IOP321_REG_ADDR(0x00000108)
#define IOP321_ATUCCR     (volatile u32 *)IOP321_REG_ADDR(0x00000109)
#define IOP321_ATUCLSR    (volatile u8  *)IOP321_REG_ADDR(0x0000010C)
#define IOP321_ATULT      (volatile u8  *)IOP321_REG_ADDR(0x0000010D)
#define IOP321_ATUHTR     (volatile u8  *)IOP321_REG_ADDR(0x0000010E)
#define IOP321_ATUBIST    (volatile u8  *)IOP321_REG_ADDR(0x0000010F)
#define IOP321_IABAR0     (volatile u32 *)IOP321_REG_ADDR(0x00000110)
#define IOP321_IAUBAR0    (volatile u32 *)IOP321_REG_ADDR(0x00000114)
#define IOP321_IABAR1     (volatile u32 *)IOP321_REG_ADDR(0x00000118)
#define IOP321_IAUBAR1    (volatile u32 *)IOP321_REG_ADDR(0x0000011C)
#define IOP321_IABAR2     (volatile u32 *)IOP321_REG_ADDR(0x00000120)
#define IOP321_IAUBAR2    (volatile u32 *)IOP321_REG_ADDR(0x00000124)
#define IOP321_ASVIR      (volatile u16 *)IOP321_REG_ADDR(0x0000012C)
#define IOP321_ASIR       (volatile u16 *)IOP321_REG_ADDR(0x0000012E)
#define IOP321_ERBAR      (volatile u32 *)IOP321_REG_ADDR(0x00000130)
/* Reserved 0x00000134 through 0x0000013B */
#define IOP321_ATUILR     (volatile u8  *)IOP321_REG_ADDR(0x0000013C)
#define IOP321_ATUIPR     (volatile u8  *)IOP321_REG_ADDR(0x0000013D)
#define IOP321_ATUMGNT    (volatile u8  *)IOP321_REG_ADDR(0x0000013E)
#define IOP321_ATUMLAT    (volatile u8  *)IOP321_REG_ADDR(0x0000013F)
#define IOP321_IALR0      (volatile u32 *)IOP321_REG_ADDR(0x00000140)
#define IOP321_IATVR0     (volatile u32 *)IOP321_REG_ADDR(0x00000144)
#define IOP321_ERLR       (volatile u32 *)IOP321_REG_ADDR(0x00000148)
#define IOP321_ERTVR      (volatile u32 *)IOP321_REG_ADDR(0x0000014C)
#define IOP321_IALR1      (volatile u32 *)IOP321_REG_ADDR(0x00000150)
#define IOP321_IALR2      (volatile u32 *)IOP321_REG_ADDR(0x00000154)
#define IOP321_IATVR2     (volatile u32 *)IOP321_REG_ADDR(0x00000158)
#define IOP321_OIOWTVR    (volatile u32 *)IOP321_REG_ADDR(0x0000015C)
#define IOP321_OMWTVR0    (volatile u32 *)IOP321_REG_ADDR(0x00000160)
#define IOP321_OUMWTVR0   (volatile u32 *)IOP321_REG_ADDR(0x00000164)
#define IOP321_OMWTVR1    (volatile u32 *)IOP321_REG_ADDR(0x00000168)
#define IOP321_OUMWTVR1   (volatile u32 *)IOP321_REG_ADDR(0x0000016C)
/* Reserved 0x00000170 through 0x00000177*/
#define IOP321_OUDWTVR    (volatile u32 *)IOP321_REG_ADDR(0x00000178)
/* Reserved 0x0000017C through 0x0000017F*/
#define IOP321_ATUCR      (volatile u32 *)IOP321_REG_ADDR(0x00000180)
#define IOP321_PCSR       (volatile u32 *)IOP321_REG_ADDR(0x00000184)
#define IOP321_ATUISR     (volatile u32 *)IOP321_REG_ADDR(0x00000188)
#define IOP321_ATUIMR     (volatile u32 *)IOP321_REG_ADDR(0x0000018C)
#define IOP321_IABAR3     (volatile u32 *)IOP321_REG_ADDR(0x00000190)
#define IOP321_IAUBAR3    (volatile u32 *)IOP321_REG_ADDR(0x00000194)
#define IOP321_IALR3      (volatile u32 *)IOP321_REG_ADDR(0x00000198)
#define IOP321_IATVR3     (volatile u32 *)IOP321_REG_ADDR(0x0000019C)
/* Reserved 0x000001A0 through 0x000001A3*/
#define IOP321_OCCAR      (volatile u32 *)IOP321_REG_ADDR(0x000001A4)
/* Reserved 0x000001A8 through 0x000001AB*/
#define IOP321_OCCDR      (volatile u32 *)IOP321_REG_ADDR(0x000001AC)
/* Reserved 0x000001B0 through 0x000001BB*/
#define IOP321_PDSCR      (volatile u32 *)IOP321_REG_ADDR(0x000001BC)
#define IOP321_PMCAPID    (volatile u8  *)IOP321_REG_ADDR(0x000001C0)
#define IOP321_PMNEXT     (volatile u8  *)IOP321_REG_ADDR(0x000001C1)
#define IOP321_APMCR      (volatile u16 *)IOP321_REG_ADDR(0x000001C2)
#define IOP321_APMCSR     (volatile u16 *)IOP321_REG_ADDR(0x000001C4)
/* Reserved 0x000001C6 through 0x000001DF */
#define IOP321_PCIXCAPID  (volatile u8  *)IOP321_REG_ADDR(0x000001E0)
#define IOP321_PCIXNEXT   (volatile u8  *)IOP321_REG_ADDR(0x000001E1)
#define IOP321_PCIXCMD    (volatile u16 *)IOP321_REG_ADDR(0x000001E2)
#define IOP321_PCIXSR     (volatile u32 *)IOP321_REG_ADDR(0x000001E4)
#define IOP321_PCIIRSR    (volatile u32 *)IOP321_REG_ADDR(0x000001EC)

/* Messaging Unit 0x00000300 through 0x000003FF */
/* DMA Controller 0x00000400 through 0x000004FF */
/* Memory controller 0x00000500 through 0x0005FF */
/* Peripheral bus interface unit 0x00000680 through 0x0006FF */
/* Peripheral performance monitoring unit 0x00000700 through 0x00077F */
/* Internal arbitration unit 0x00000780 through 0x0007BF */

/* Interrupt Controller */
#define IOP321_INTCTL     (volatile u32 *)IOP321_REG_ADDR(0x000007D0)
#define IOP321_INTSTR     (volatile u32 *)IOP321_REG_ADDR(0x000007D4)
#define IOP321_IINTSRC    (volatile u32 *)IOP321_REG_ADDR(0x000007D8)
#define IOP321_FINTSRC    (volatile u32 *)IOP321_REG_ADDR(0x000007DC)

/* Timers */

#define IOP321_TU_TMR0		(volatile u32 *)IOP321_REG_ADDR(0x000007E0)
#define IOP321_TU_TMR1		(volatile u32 *)IOP321_REG_ADDR(0x000007E4)

#define IOP321_TMR_TC		0x01
#define	IOP321_TMR_EN		0x02
#define IOP321_TMR_RELOAD	0x04
#define	IOP321_TMR_PRIVILEGED	0x09

#define	IOP321_TMR_RATIO_1_1	0x00
#define	IOP321_TMR_RATIO_4_1	0x10
#define	IOP321_TMR_RATIO_8_1	0x20
#define	IOP321_TMR_RATIO_16_1	0x30

#define IOP321_TU_TCR0    (volatile u32 *)IOP321_REG_ADDR(0x000007E8)
#define IOP321_TU_TCR1    (volatile u32 *)IOP321_REG_ADDR(0x000007EC)
#define IOP321_TU_TRR0    (volatile u32 *)IOP321_REG_ADDR(0x000007F0)
#define IOP321_TU_TRR1    (volatile u32 *)IOP321_REG_ADDR(0x000007F4)
#define IOP321_TU_TISR    (volatile u32 *)IOP321_REG_ADDR(0x000007F8)
#define IOP321_TU_WDTCR   (volatile u32 *)IOP321_REG_ADDR(0x000007FC)

/* Application accelerator unit 0x00000800 - 0x000008FF */
#define IOP321_AAUACR     (volatile u32 *)IOP321_REG_ADDR(0x00000800)
#define IOP321_AAUASR     (volatile u32 *)IOP321_REG_ADDR(0x00000804)
#define IOP321_AAUANDAR   (volatile u32 *)IOP321_REG_ADDR(0x0000080C)

/* SSP serial port unit 0x00001600 - 0x0000167F */
/* I2C bus interface unit 0x00001680 - 0x000016FF */
#endif // _IOP321_HW_H_
