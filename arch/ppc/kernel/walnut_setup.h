/*
 *
 *    Copyright (c) 1999-2000 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: walnut_setup.c
 *
 *    Description:
 *      Architecture- / platform-specific boot-time initialization code for
 *      the IBM PowerPC 405GP "Walnut" evaluation board. Adapted from original
 *      code by Gary Thomas, Cort Dougan <cort@cs.nmt.edu>, and Dan Malek
 *      <dan@netx4.com>.
 *
 */

#ifndef	__WALNUT_SETUP_H__
#define	__WALNUT_SETUP_H__

#include <asm/ptrace.h>
#include <asm/board.h>


#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char	 __res[sizeof(bd_t)];

extern void		 walnut_init(unsigned long r3,
				  unsigned long ird_start,
				  unsigned long ird_end,
				  unsigned long cline_start,
				  unsigned long cline_end);
extern void		 walnut_setup_arch(void);
extern int		 walnut_setup_residual(char *buffer);
extern void		 walnut_init_IRQ(void);
extern int		 walnut_get_irq(struct pt_regs *regs);
extern void		 walnut_restart(char *cmd);
extern void		 walnut_power_off(void);
extern void		 walnut_halt(void);
extern void		 walnut_time_init(void);
extern int		 walnut_set_rtc_time(unsigned long now);
extern unsigned long	 walnut_get_rtc_time(void);
extern void		 walnut_calibrate_decr(void);


#ifdef __cplusplus
}
#endif

#endif /* __WALNUT_SETUP_H__ */
