/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5307/config.c
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2000, Lineo (www.lineo.com)
 */

/***************************************************************************/

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/init.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcftimer.h>
#include <asm/mcfsim.h>
#include <asm/mcfdma.h>
#include <asm/delay.h>

#if defined(CONFIG_eLIA)
#include <asm/elia.h>
#endif

#include <asm/mcfwdebug.h>

/***************************************************************************/

void	reset_setupbutton(void);
void	coldfire_profile_init(void);

/***************************************************************************/

/*
 *	DMA channel base address table.
 */
unsigned int   dma_base_addr[MAX_M68K_DMA_CHANNELS] = {
        MCF_MBAR + MCFDMA_BASE0,
        MCF_MBAR + MCFDMA_BASE1,
        MCF_MBAR + MCFDMA_BASE2,
        MCF_MBAR + MCFDMA_BASE3,
};

unsigned int dma_device_address[MAX_M68K_DMA_CHANNELS];

/***************************************************************************/

void coldfire_tick(void)
{
	volatile unsigned char	*timerp;

	/* Reset the ColdFire timer */
	timerp = (volatile unsigned char *) (MCF_MBAR + MCFTIMER_BASE1);
	timerp[MCFTIMER_TER] = MCFTIMER_TER_CAP | MCFTIMER_TER_REF;
}

/***************************************************************************/

void coldfire_timer_init(void (*handler)(int, void *, struct pt_regs *))
{
	volatile unsigned short	*timerp;
	volatile unsigned char	*icrp;

	/* Set up TIMER 1 as poll clock */
	timerp = (volatile unsigned short *) (MCF_MBAR + MCFTIMER_BASE1);
	timerp[MCFTIMER_TMR] = MCFTIMER_TMR_DISABLE;

	timerp[MCFTIMER_TRR] = (unsigned short) ((MCF_CLK / 16) / HZ);
	timerp[MCFTIMER_TMR] = MCFTIMER_TMR_ENORI | MCFTIMER_TMR_CLK16 |
		MCFTIMER_TMR_RESTART | MCFTIMER_TMR_ENABLE;

	icrp = (volatile unsigned char *) (MCF_MBAR + MCFSIM_TIMER1ICR);

#if defined(CONFIG_NETtel) || defined(CONFIG_eLIA) || \
		defined(CONFIG_DISKtel) || defined(CONFIG_SECUREEDGEMP3) || \
		defined(CONFIG_CLEOPATRA) 
	*icrp = MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI3;
	request_irq(30, handler, SA_INTERRUPT, "ColdFire Timer", NULL);
#else
	*icrp = MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL5 | MCFSIM_ICR_PRI3;
	request_irq(29, handler, SA_INTERRUPT, "ColdFire Timer", NULL);
#endif

#if defined(CONFIG_NETtel) || defined(CONFIG_eLIA) || \
		defined(CONFIG_DISKtel) || defined(CONFIG_SECUREEDGEMP3)
	/* This is not really the right place to do this... */
	reset_setupbutton();
#endif
#ifdef CONFIG_HIGHPROFILE
	coldfire_profile_init();
#endif
	mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_TIMER1);
}

/***************************************************************************/
#ifdef CONFIG_HIGHPROFILE
/***************************************************************************/

#define	PROFILEHZ	1013

/*
 *	Use the other timer to provide high accuracy profiling info.
 */

void coldfire_profile_tick(int irq, void *dummy, struct pt_regs *regs)
{
	volatile unsigned char	*timerp;

	/* Reset the ColdFire timer2 */
	timerp = (volatile unsigned char *) (MCF_MBAR + MCFTIMER_BASE2);
	timerp[MCFTIMER_TER] = MCFTIMER_TER_CAP | MCFTIMER_TER_REF;

        if (!user_mode(regs)) {
                if (prof_buffer && current->pid) {
                        extern int _stext;
                        unsigned long ip = instruction_pointer(regs);
                        ip -= (unsigned long) &_stext;
                        ip >>= prof_shift;
                        if (ip < prof_len)
                                prof_buffer[ip]++;
                }
        }
}

void coldfire_profile_init(void)
{
	volatile unsigned short	*timerp;
	volatile unsigned char	*icrp;

	printk("PROFILE: lodging timer2=%d as profile timer\n", PROFILEHZ);

	/* Set up TIMER 2 as poll clock */
	timerp = (volatile unsigned short *) (MCF_MBAR + MCFTIMER_BASE2);
	timerp[MCFTIMER_TMR] = MCFTIMER_TMR_DISABLE;

	timerp[MCFTIMER_TRR] = (unsigned short) ((MCF_CLK / 16) / PROFILEHZ);
	timerp[MCFTIMER_TMR] = MCFTIMER_TMR_ENORI | MCFTIMER_TMR_CLK16 |
		MCFTIMER_TMR_RESTART | MCFTIMER_TMR_ENABLE;

	icrp = (volatile unsigned char *) (MCF_MBAR + MCFSIM_TIMER2ICR);

	*icrp = MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL7 | MCFSIM_ICR_PRI3;
	request_irq(31, coldfire_profile_tick, (SA_INTERRUPT | IRQ_FLG_FAST),
		"Profile Timer", NULL);
	mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_TIMER2);
}

/***************************************************************************/
#endif	/* CONFIG_HIGHPROFILE */
/***************************************************************************/

/*
 *	Program the vector to be an auto-vectored.
 */

void mcf_autovector(unsigned int vec)
{
	volatile unsigned char  *mbar;

	if ((vec >= 25) && (vec <= 31)) {
		mbar = (volatile unsigned char *) MCF_MBAR;
		vec = 0x1 << (vec - 24);
		*(mbar + MCFSIM_AVR) |= vec;
		mcf_setimr(mcf_getimr() & ~vec);
	}
}

/***************************************************************************/

extern e_vector	*_ramvec;

void set_evector(int vecnum, void (*handler)(void))
{
	if (vecnum >= 0 && vecnum <= 255)
		_ramvec[vecnum] = handler;
}

/***************************************************************************/

/* assembler routines */
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage void system_call(void);
asmlinkage void inthandler(void);

#ifdef TRAP_DBG_INTERRUPT
asmlinkage void dbginterrupt(void);
#endif

void __init coldfire_trap_init(void)
{
	int i;

#ifndef ENABLE_dBUG
	mcf_setimr(MCFSIM_IMR_MASKALL);
#endif

	/*
	 *	There is a common trap handler and common interrupt
	 *	handler that handle almost every vector. We treat
	 *	the system call and bus error special, they get their
	 *	own first level handlers.
	 */
#ifndef ENABLE_dBUG
	for (i = 3; (i <= 23); i++)
		_ramvec[i] = trap;
	for (i = 33; (i <= 63); i++)
		_ramvec[i] = trap;
#endif
#ifdef TRAP_DBG_INTERRUPT
	_ramvec[12] = dbginterrupt;
#endif

	for (i = 24; (i <= 30); i++)
		_ramvec[i] = inthandler;
#ifndef ENABLE_dBUG
	_ramvec[31] = inthandler;  // Disables the IRQ7 button
#endif

	for (i = 64; (i < 255); i++)
		_ramvec[i] = inthandler;
	_ramvec[255] = 0;

	_ramvec[2] = buserr;
	_ramvec[32] = system_call;
#ifdef MCF_BDM_DISABLE
	/* Disable the BDM clocking.  This also turns off most of the rest of
	 * the BDM device.  This is good for EMC reasons.  This option is not
	 * incompatible with the memory protection option.
	 */
	wdebug(MCFDEBUG_CSR, MCFDEBUG_CSR_PSTCLK);
#endif

}

/***************************************************************************/

#if defined(CONFIG_NETtel) || defined(CONFIG_eLIA) || \
	defined(CONFIG_DISKtel) || defined(CONFIG_SECUREEDGEMP3)

/*
 *	Routines to support the NETtel software reset button.
 */
void reset_button(int irq, void *dev_id, struct pt_regs *regs)
{
	extern void	flash_eraseconfig(void);
	static int	inbutton = 0;

	/*
	 *	IRQ7 is not maskable by the CPU core. It is possible
	 *	that switch bounce mey get us back here before we have
	 *	really serviced the interrupt.
	 */
	if (inbutton)
		return;
	inbutton = 1;
	/* Disable interrupt at SIM - best we can do... */
	mcf_setimr(mcf_getimr() | MCFSIM_IMR_EINT7);
	/* Try and de-bounce the switch a little... */
	udelay(10000);

	flash_eraseconfig();

	/* Don't leave here 'till button is no longer pushed! */
	for (;;) {
		if ((mcf_getipr() & MCFSIM_IMR_EINT7) == 0)
			break;
	}

	HARD_RESET_NOW();
	/* Should never get here... */

	inbutton = 0;
	/* Interrupt service done, enable it again */
	mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_EINT7);
}

/***************************************************************************/

void reset_setupbutton(void)
{
	volatile unsigned char	*mbar;

	mbar = (volatile unsigned char *) MCF_MBAR;
	*(mbar + MCFSIM_AVR) |= MCFSIM_IMR_EINT7;
	mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_EINT7);
	request_irq(31, reset_button, (SA_INTERRUPT | IRQ_FLG_FAST),
		"Reset Button", NULL);
}

#endif /* CONFIG_NETtel || CONFIG_eLIA || CONFIG_DISKtel || CONFIG_SECUREEDGEMP3 */

/***************************************************************************/

void coldfire_reset(void)
{
	HARD_RESET_NOW();
}

/***************************************************************************/

void config_BSP(char *commandp, int size)
{
#if defined(CONFIG_NETtel) || defined(CONFIG_eLIA) || \
    defined(CONFIG_DISKtel) || defined(CONFIG_SECUREEDGEMP3)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0004000, size);
	commandp[size-1] = 0;
#else
	memset(commandp, 0, size);
#endif /* CONFIG_NETtel || CONFIG_eLIA || CONFIG_DISKtel || CONFIG_SECUREEDGEMP3 */

	mach_sched_init = coldfire_timer_init;
	mach_tick = coldfire_tick;
	mach_trap_init = coldfire_trap_init;
	mach_reset = coldfire_reset;
}

/***************************************************************************/
#ifdef TRAP_DBG_INTERRUPT

asmlinkage void dbginterrupt_c(struct frame *fp)
{
	extern void dump(struct pt_regs *fp);
	printk("%s(%d): BUS ERROR TRAP\n", __FILE__, __LINE__);
	dump((struct pt_regs *) fp);
	asm("halt");
}

#endif
/***************************************************************************/
