/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: ppc4xx_pic.h
 *
 *    Description:
 *      Interrupt controller driver for PowerPC 4xx-based processors.
 */

#ifndef	__PPC4XX_PIC_H__
#define	__PPC4XX_PIC_H__

#include <linux/config.h>
#include <linux/irq.h>

/* External Global Variables */

extern struct hw_interrupt_type *ppc4xx_pic;
extern unsigned int ibm4xxPIC_NumInitSenses;
extern unsigned char *ibm4xxPIC_InitSenses;

/* Function Prototypes */

extern void ppc4xx_pic_init(void);
extern int ppc4xx_pic_get_irq(struct pt_regs *regs);

#endif				/* __PPC4XX_PIC_H__ */
