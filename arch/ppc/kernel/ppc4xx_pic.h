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

#include <asm/ptrace.h>


#ifdef __cplusplus
extern "C" {
#endif

/* External Global Variables */

extern struct hw_interrupt_type *ppc4xx_pic;


/* Function Prototypes */

extern void	 ppc4xx_pic_init(void);
extern int	 ppc4xx_pic_get_irq(struct pt_regs *regs);


#ifdef __cplusplus
}
#endif

#endif /* __PPC4XX_PIC_H__ */
