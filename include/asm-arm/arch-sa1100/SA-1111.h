/*
 * linux/include/asm/arch/SA-1111.h
 *
 * Copyright (C) 2000 John G Dorsey <john+@cs.cmu.edu>
 *
 * This file contains definitions for the SA-1111 Companion Chip.
 * (Structure and naming borrowed from SA-1101.h, by Peter Danielsson.)
 *
 */

#ifndef _ASM_ARCH_SA1111
#define _ASM_ARCH_SA1111

/*
 * Macro that calculates real address for registers in the SA-1111
 */

#define _SA1111( x )    ((x) + SA1111_BASE)

/*
 * System Bus Interface (SBI)
 *
 * Registers
 *    SKCR	Control Register
 *    SMCR	Shared Memory Controller Register
 *    SKID	ID Register
 */

#define _SKCR		_SA1111( 0x0000 )
#define _SMCR		_SA1111( 0x0004 )
#define _SKID		_SA1111( 0x0008 )

#if LANGUAGE == C

#define SKCR		(*((volatile Word *) SA1111_p2v (_SKCR)))
#define SMCR		(*((volatile Word *) SA1111_p2v (_SMCR)))
#define SKID		(*((volatile Word *) SA1111_p2v (_SKID)))

#endif  /* LANGUAGE == C */

#define SKCR_PLL_BYPASS	(1<<0)
#define SKCR_RCLKEN	(1<<1)
#define SKCR_SLEEP	(1<<2)
#define SKCR_DOZE	(1<<3)
#define SKCR_VCO_OFF	(1<<4)
#define SKCR_SCANTSTEN	(1<<5)
#define SKCR_CLKTSTEN	(1<<6)
#define SKCR_RDYEN	(1<<7)
#define SKCR_SELAC	(1<<8)
#define SKCR_OPPC	(1<<9)
#define SKCR_PLLTSTEN	(1<<10)
#define SKCR_USBIOTSTEN	(1<<11)
#define SKCR_OE_EN	(1<<13)

#define SMCR_DTIM	(1<<0)
#define SMCR_MBGE	(1<<1)
#define SMCR_DRAC_0	(1<<2)
#define SMCR_DRAC_1	(1<<3)
#define SMCR_DRAC_2	(1<<4)
#define SMCR_CLAT	(1<<5)

#define SKID_SIREV_MASK	(0x000000f0)
#define SKID_MTREV_MASK (0x0000000f)
#define SKID_ID_MASK	(0xffffff00)
#define SKID_SA1111_ID	(0x690cc200)

/*
 * System Controller
 *
 * Registers
 *    SKPCR	Power Control Register
 *    SKCDR	Clock Divider Register
 *    SKAUD	Audio Clock Divider Register
 *    SKPMC	PS/2 Mouse Clock Divider Register
 *    SKPTC	PS/2 Track Pad Clock Divider Register
 *    SKPEN0	PWM0 Enable Register
 *    SKPWM0	PWM0 Clock Register
 *    SKPEN1	PWM1 Enable Register
 *    SKPWM1	PWM1 Clock Register
 */

#define _SKPCR		_SA1111(0x0200)
#define _SKCDR		_SA1111(0x0204)
#define _SKAUD		_SA1111(0x0208)
#define _SKPMC		_SA1111(0x020c)
#define _SKPTC		_SA1111(0x0210)
#define _SKPEN0		_SA1111(0x0214)
#define _SKPWM0		_SA1111(0x0218)
#define _SKPEN1		_SA1111(0x021c)
#define _SKPWM1		_SA1111(0x0220)

#if LANGUAGE == C

#define SKPCR		(*((volatile Word *) SA1111_p2v (_SKPCR)))
#define SKCDR		(*((volatile Word *) SA1111_p2v (_SKCDR)))
#define SKAUD		(*((volatile Word *) SA1111_p2v (_SKAUD)))
#define SKPMC		(*((volatile Word *) SA1111_p2v (_SKPMC)))
#define SKPTC		(*((volatile Word *) SA1111_p2v (_SKPTC)))
#define SKPEN0		(*((volatile Word *) SA1111_p2v (_SKPEN0)))
#define SKPWM0		(*((volatile Word *) SA1111_p2v (_SKPWM0)))
#define SKPEN1		(*((volatile Word *) SA1111_p2v (_SKPEN1)))
#define SKPWM1		(*((volatile Word *) SA1111_p2v (_SKPWM1)))

#endif  /* LANGUAGE == C */

/*
 * General-Purpose I/O Interface
 *
 * Registers
 *    PA_DDR		GPIO Block A Data Direction
 *    PA_DRR/PA_DWR	GPIO Block A Data Value Register (read/write)
 *    PA_SDR		GPIO Block A Sleep Direction
 *    PA_SSR		GPIO Block A Sleep State
 *    PB_DDR		GPIO Block B Data Direction
 *    PB_DRR/PB_DWR	GPIO Block B Data Value Register (read/write)
 *    PB_SDR		GPIO Block B Sleep Direction
 *    PB_SSR		GPIO Block B Sleep State
 *    PC_DDR		GPIO Block C Data Direction
 *    PC_DRR/PC_DWR	GPIO Block C Data Value Register (read/write)
 *    PC_SDR		GPIO Block C Sleep Direction
 *    PC_SSR		GPIO Block C Sleep State
 */

#define _PA_DDR		_SA1111( 0x1000 )
#define _PA_DRR		_SA1111( 0x1004 )
#define _PA_DWR		_SA1111( 0x1004 )
#define _PA_SDR		_SA1111( 0x1008 )
#define _PA_SSR		_SA1111( 0x100c )
#define _PB_DDR		_SA1111( 0x1010 )
#define _PB_DRR		_SA1111( 0x1014 )
#define _PB_DWR		_SA1111( 0x1014 )
#define _PB_SDR		_SA1111( 0x1018 )
#define _PB_SSR		_SA1111( 0x101c )
#define _PC_DDR		_SA1111( 0x1020 )
#define _PC_DRR		_SA1111( 0x1024 )
#define _PC_DWR		_SA1111( 0x1024 )
#define _PC_SDR		_SA1111( 0x1028 )
#define _PC_SSR		_SA1111( 0x102c )

#if LANGUAGE == C

#define PA_DDR		(*((volatile Word *) SA1111_p2v (_PA_DDR)))
#define PA_DRR		(*((volatile Word *) SA1111_p2v (_PA_DRR)))
#define PA_DWR		(*((volatile Word *) SA1111_p2v (_PA_DWR)))
#define PA_SDR		(*((volatile Word *) SA1111_p2v (_PA_SDR)))
#define PA_SSR		(*((volatile Word *) SA1111_p2v (_PA_SSR)))
#define PB_DDR		(*((volatile Word *) SA1111_p2v (_PB_DDR)))
#define PB_DRR		(*((volatile Word *) SA1111_p2v (_PB_DRR)))
#define PB_DWR		(*((volatile Word *) SA1111_p2v (_PB_DWR)))
#define PB_SDR		(*((volatile Word *) SA1111_p2v (_PB_SDR)))
#define PB_SSR		(*((volatile Word *) SA1111_p2v (_PB_SSR)))
#define PC_DDR		(*((volatile Word *) SA1111_p2v (_PC_DDR)))
#define PC_DRR		(*((volatile Word *) SA1111_p2v (_PC_DRR)))
#define PC_DWR		(*((volatile Word *) SA1111_p2v (_PC_DWR)))
#define PC_SDR		(*((volatile Word *) SA1111_p2v (_PC_SDR)))
#define PC_SSR		(*((volatile Word *) SA1111_p2v (_PC_SSR)))

#endif  /* LANGUAGE == C */

/*
 * Interrupt Controller
 *
 * Registers
 *    INTTEST0		Test register 0
 *    INTTEST1		Test register 1
 *    INTEN0		Interrupt Enable register 0
 *    INTEN1		Interrupt Enable register 1
 *    INTPOL0		Interrupt Polarity selection 0
 *    INTPOL1		Interrupt Polarity selection 1
 *    INTTSTSEL		Interrupt source selection
 *    INTSTATCLR0	Interrupt Status/Clear 0
 *    INTSTATCLR1	Interrupt Status/Clear 1
 *    INTSET0		Interrupt source set 0
 *    INTSET1		Interrupt source set 1
 *    WAKE_EN0		Wake-up source enable 0
 *    WAKE_EN1		Wake-up source enable 1
 *    WAKE_POL0		Wake-up polarity selection 0
 *    WAKE_POL1		Wake-up polarity selection 1
 */

#define _INTTEST0	_SA1111( 0x1600 )
#define _INTTEST1	_SA1111( 0x1604 )
#define _INTEN0		_SA1111( 0x1608 )
#define _INTEN1		_SA1111( 0x160c )
#define _INTPOL0	_SA1111( 0x1610 )
#define _INTPOL1	_SA1111( 0x1614 )
#define _INTTSTSEL	_SA1111( 0x1618 )
#define _INTSTATCLR0	_SA1111( 0x161c )
#define _INTSTATCLR1	_SA1111( 0x1620 )
#define _INTSET0	_SA1111( 0x1624 )
#define _INTSET1	_SA1111( 0x1628 )
#define _WAKE_EN0	_SA1111( 0x162c )
#define _WAKE_EN1	_SA1111( 0x1630 )
#define _WAKE_POL0	_SA1111( 0x1634 )
#define _WAKE_POL1	_SA1111( 0x1638 )

#if LANGUAGE == C

#define INTTEST0	(*((volatile Word *) SA1111_p2v (_INTTEST0)))
#define INTTEST1	(*((volatile Word *) SA1111_p2v (_INTTEST1)))
#define INTEN0		(*((volatile Word *) SA1111_p2v (_INTEN0)))
#define INTEN1		(*((volatile Word *) SA1111_p2v (_INTEN1)))
#define INTPOL0		(*((volatile Word *) SA1111_p2v (_INTPOL0)))
#define INTPOL1		(*((volatile Word *) SA1111_p2v (_INTPOL1)))
#define INTTSTSEL	(*((volatile Word *) SA1111_p2v (_INTTSTSEL)))
#define INTSTATCLR0	(*((volatile Word *) SA1111_p2v (_INTSTATCLR0)))
#define INTSTATCLR1	(*((volatile Word *) SA1111_p2v (_INTSTATCLR1)))
#define INTSET0		(*((volatile Word *) SA1111_p2v (_INTSET0)))
#define INTSET1		(*((volatile Word *) SA1111_p2v (_INTSET1)))
#define WAKE_EN0	(*((volatile Word *) SA1111_p2v (_WAKE_EN0)))
#define WAKE_EN1	(*((volatile Word *) SA1111_p2v (_WAKE_EN1)))
#define WAKE_POL0	(*((volatile Word *) SA1111_p2v (_WAKE_POL0)))
#define WAKE_POL1	(*((volatile Word *) SA1111_p2v (_WAKE_POL1)))

#endif  /* LANGUAGE == C */

/*
 * PCMCIA Interface
 *
 * Registers
 *    PCSR	Status Register
 *    PCCR	Control Register
 *    PCSSR	Sleep State Register
 */

#define _PCCR		_SA1111( 0x1800 )
#define _PCSSR		_SA1111( 0x1804 )
#define _PCSR		_SA1111( 0x1808 )

#if LANGUAGE == C

#define PCCR		(*((volatile Word *) SA1111_p2v (_PCCR)))
#define PCSSR		(*((volatile Word *) SA1111_p2v (_PCSSR)))
#define PCSR		(*((volatile Word *) SA1111_p2v (_PCSR)))

#endif  /* LANGUAGE == C */

#define PCSR_S0_READY	(1<<0)
#define PCSR_S1_READY	(1<<1)
#define PCSR_S0_DETECT	(1<<2)
#define PCSR_S1_DETECT	(1<<3)
#define PCSR_S0_VS1	(1<<4)
#define PCSR_S0_VS2	(1<<5)
#define PCSR_S1_VS1	(1<<6)
#define PCSR_S1_VS2	(1<<7)
#define PCSR_S0_WP	(1<<8)
#define PCSR_S1_WP	(1<<9)
#define PCSR_S0_BVD1	(1<<10)
#define PCSR_S0_BVD2	(1<<11)
#define PCSR_S1_BVD1	(1<<12)
#define PCSR_S1_BVD2	(1<<13)

#define PCCR_S0_RST	(1<<0)
#define PCCR_S1_RST	(1<<1)
#define PCCR_S0_FLT	(1<<2)
#define PCCR_S1_FLT	(1<<3)
#define PCCR_S0_PWAITEN	(1<<4)
#define PCCR_S1_PWAITEN	(1<<5)
#define PCCR_S0_PSE	(1<<6)
#define PCCR_S1_PSE	(1<<7)

#define PCSSR_S0_SLEEP	(1<<0)
#define PCSSR_S1_SLEEP	(1<<1)

#endif  /* _ASM_ARCH_SA1111 */
