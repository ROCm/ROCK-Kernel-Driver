/****************************************************************************/

/*
 *	nap.h -- Marconi/NAP support.
 *
 * 	(C) Copyright 2001, SnapGear (www.snapgear.com) 
 */

/****************************************************************************/
#ifndef	nap_h
#define	nap_h
/****************************************************************************/

#include <linux/config.h>

/****************************************************************************/
#ifdef CONFIG_MARCONINAP
/****************************************************************************/

#ifdef CONFIG_COLDFIRE
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#endif

/*
 *	Command to support selecting RS232 or RS422 mode on the
 *	second COM port.
 */
#define	TIOCSET422	0x4d01		/* Set port mode 232 or 422 */
#define	TIOCGET422	0x4d02		/* Get current port mode */

/*
 *	Low level control of the RS232/422 enable.
 */
#define	MCFPP_PA11	0x0800

#ifndef __ASSEMBLY__
/*
 *	RS232/422 control is via the single PA11 line. Low is the RS422
 *	enable, high is RS232 mode.
 */
static __inline__ unsigned int mcf_getpa(void)
{
	volatile unsigned short *pp;
	pp = (volatile unsigned short *) (MCF_MBAR + MCFSIM_PADAT);
	return((unsigned int) *pp);
}

static __inline__ void mcf_setpa(unsigned int mask, unsigned int bits)
{
	volatile unsigned short *pp;
	unsigned long		flags;

	pp = (volatile unsigned short *) (MCF_MBAR + MCFSIM_PADAT);
	save_flags(flags); cli();
	*pp = (*pp & ~mask) | bits;
	restore_flags(flags);
}
#endif /* __ASSEMBLY__ */

/****************************************************************************/

#if defined(CONFIG_M5272)
/*
 *	Marconi/NAP based hardware. DTR/DCD lines are wired to GPB lines.
 */
#define	MCFPP_DCD0	0x0080
#define	MCFPP_DCD1	0x0020
#define	MCFPP_DTR0	0x0040
#define	MCFPP_DTR1	0x0010

#ifndef __ASSEMBLY__
/*
 *	These functions defined to give quasi generic access to the
 *	PPIO bits used for DTR/DCD.
 */
static __inline__ unsigned int mcf_getppdata(void)
{
	volatile unsigned short *pp;
	pp = (volatile unsigned short *) (MCF_MBAR + MCFSIM_PBDAT);
	return((unsigned int) *pp);
}

static __inline__ void mcf_setppdata(unsigned int mask, unsigned int bits)
{
	volatile unsigned short *pp;
	pp = (volatile unsigned short *) (MCF_MBAR + MCFSIM_PBDAT);
	*pp = (*pp & ~mask) | bits;
}
#endif /* __ASSEMBLY__ */
#endif /* CONFIG_M5272 */

/****************************************************************************/
#endif /* CONFIG_MARCONINAP */
/****************************************************************************/
#endif	/* nap_h */
