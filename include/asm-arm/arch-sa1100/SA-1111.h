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

#include <asm/arch/bitfield.h>

/*
 * Macro that calculates real address for registers in the SA-1111
 */

#define _SA1111( x )    ((x) + SA1111_BASE)

/*
 * 26 bits of the SA-1110 address bus are available to the SA-1111.
 * Use these when feeding target addresses to the DMA engines.
 */

#define SA1111_ADDR_WIDTH	(26)
#define SA1111_ADDR_MASK	((1<<SA1111_ADDR_WIDTH)-1)
#define SA1111_DMA_ADDR(x)	((x)&SA1111_ADDR_MASK)

/*
 * Don't ask the (SAC) DMA engines to move less than this amount.
 */

#define SA1111_SAC_DMA_MIN_XFER	(0x800)

/* System Bus Interface (SBI)
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
#define SMCR_DRAC	Fld(3, 2)
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

#define SKPCR_UCLKEN	(1<<0)
#define SKPCR_ACCLKEN	(1<<1)
#define SKPCR_I2SCLKEN	(1<<2)
#define SKPCR_L3CLKEN	(1<<3)
#define SKPCR_SCLKEN	(1<<4)
#define SKPCR_PMCLKEN	(1<<5)
#define SKPCR_PTCLKEN	(1<<6)
#define SKPCR_DCLKEN	(1<<7)
#define SKPCR_PWMCLKEN	(1<<8)

/*
 * Serial Audio Controller
 *
 * Registers
 *    SACR0             Serial Audio Common Control Register
 *    SACR1             Serial Audio Alternate Mode (I2C/MSB) Control Register
 *    SACR2             Serial Audio AC-link Control Register
 *    SASR0             Serial Audio I2S/MSB Interface & FIFO Status Register
 *    SASR1             Serial Audio AC-link Interface & FIFO Status Register
 *    SASCR             Serial Audio Status Clear Register
 *    L3_CAR            L3 Control Bus Address Register
 *    L3_CDR            L3 Control Bus Data Register
 *    ACCAR             AC-link Command Address Register
 *    ACCDR             AC-link Command Data Register
 *    ACSAR             AC-link Status Address Register
 *    ACSDR             AC-link Status Data Register
 *    SADTCS            Serial Audio DMA Transmit Control/Status Register
 *    SADTSA            Serial Audio DMA Transmit Buffer Start Address A
 *    SADTCA            Serial Audio DMA Transmit Buffer Count Register A
 *    SADTSB            Serial Audio DMA Transmit Buffer Start Address B
 *    SADTCB            Serial Audio DMA Transmit Buffer Count Register B
 *    SADRCS            Serial Audio DMA Receive Control/Status Register
 *    SADRSA            Serial Audio DMA Receive Buffer Start Address A
 *    SADRCA            Serial Audio DMA Receive Buffer Count Register A
 *    SADRSB            Serial Audio DMA Receive Buffer Start Address B
 *    SADRCB            Serial Audio DMA Receive Buffer Count Register B
 *    SAITR             Serial Audio Interrupt Test Register
 *    SADR              Serial Audio Data Register (16 x 32-bit)
 */

#define _SACR0          _SA1111( 0x0600 )
#define _SACR1          _SA1111( 0x0604 )
#define _SACR2          _SA1111( 0x0608 )
#define _SASR0          _SA1111( 0x060c )
#define _SASR1          _SA1111( 0x0610 )
#define _SASCR          _SA1111( 0x0618 )
#define _L3_CAR         _SA1111( 0x061c )
#define _L3_CDR         _SA1111( 0x0620 )
#define _ACCAR          _SA1111( 0x0624 )
#define _ACCDR          _SA1111( 0x0628 )
#define _ACSAR          _SA1111( 0x062c )
#define _ACSDR          _SA1111( 0x0630 )
#define _SADTCS         _SA1111( 0x0634 )
#define _SADTSA         _SA1111( 0x0638 )
#define _SADTCA         _SA1111( 0x063c )
#define _SADTSB         _SA1111( 0x0640 )
#define _SADTCB         _SA1111( 0x0644 )
#define _SADRCS         _SA1111( 0x0648 )
#define _SADRSA         _SA1111( 0x064c )
#define _SADRCA         _SA1111( 0x0650 )
#define _SADRSB         _SA1111( 0x0654 )
#define _SADRCB         _SA1111( 0x0658 )
#define _SAITR          _SA1111( 0x065c )
#define _SADR           _SA1111( 0x0680 )

#if LANGUAGE == C

#define SACR0		(*((volatile Word *) SA1111_p2v (_SACR0)))
#define SACR1		(*((volatile Word *) SA1111_p2v (_SACR1)))
#define SACR2		(*((volatile Word *) SA1111_p2v (_SACR2)))
#define SASR0		(*((volatile Word *) SA1111_p2v (_SASR0)))
#define SASR1		(*((volatile Word *) SA1111_p2v (_SASR1)))
#define SASCR		(*((volatile Word *) SA1111_p2v (_SASCR)))
#define L3_CAR		(*((volatile Word *) SA1111_p2v (_L3_CAR)))
#define L3_CDR		(*((volatile Word *) SA1111_p2v (_L3_CDR)))
#define ACCAR		(*((volatile Word *) SA1111_p2v (_ACCAR)))
#define ACCDR		(*((volatile Word *) SA1111_p2v (_ACCDR)))
#define ACSAR		(*((volatile Word *) SA1111_p2v (_ACSAR)))
#define ACSDR		(*((volatile Word *) SA1111_p2v (_ACSDR)))
#define SADTCS		(*((volatile Word *) SA1111_p2v (_SADTCS)))
#define SADTSA		(*((volatile Word *) SA1111_p2v (_SADTSA)))
#define SADTCA		(*((volatile Word *) SA1111_p2v (_SADTCA)))
#define SADTSB		(*((volatile Word *) SA1111_p2v (_SADTSB)))
#define SADTCB		(*((volatile Word *) SA1111_p2v (_SADTCB)))
#define SADRCS		(*((volatile Word *) SA1111_p2v (_SADRCS)))
#define SADRSA		(*((volatile Word *) SA1111_p2v (_SADRSA)))
#define SADRCA		(*((volatile Word *) SA1111_p2v (_SADRCA)))
#define SADRSB		(*((volatile Word *) SA1111_p2v (_SADRSB)))
#define SADRCB		(*((volatile Word *) SA1111_p2v (_SADRCB)))
#define SAITR		(*((volatile Word *) SA1111_p2v (_SAITR)))
#define SADR		(*((volatile Word *) SA1111_p2v (_SADR)))

#endif  /* LANGUAGE == C */

#define SACR0_ENB	(1<<0)
#define SACR0_BCKD	(1<<2)
#define SACR0_RST	(1<<3)

#define SACR1_AMSL	(1<<0)
#define SACR1_L3EN	(1<<1)
#define SACR1_L3MB	(1<<2)
#define SACR1_DREC	(1<<3)
#define SACR1_DRPL	(1<<4)
#define SACR1_ENLBF	(1<<5)

#define SACR2_TS3V	(1<<0)
#define SACR2_TS4V	(1<<1)
#define SACR2_WKUP	(1<<2)
#define SACR2_DREC	(1<<3)
#define SACR2_DRPL	(1<<4)
#define SACR2_ENLBF	(1<<5)
#define SACR2_RESET	(1<<6)

#define SASR0_TNF	(1<<0)
#define SASR0_RNE	(1<<1)
#define SASR0_BSY	(1<<2)
#define SASR0_TFS	(1<<3)
#define SASR0_RFS	(1<<4)
#define SASR0_TUR	(1<<5)
#define SASR0_ROR	(1<<6)
#define SASR0_L3WD	(1<<16)
#define SASR0_L3RD	(1<<17)

#define SASR1_TNF	(1<<0)
#define SASR1_RNE	(1<<1)
#define SASR1_BSY	(1<<2)
#define SASR1_TFS	(1<<3)
#define SASR1_RFS	(1<<4)
#define SASR1_TUR	(1<<5)
#define SASR1_ROR	(1<<6)
#define SASR1_CADT	(1<<16)
#define SASR1_SADR	(1<<17)
#define SASR1_RSTO	(1<<18)
#define SASR1_CLPM	(1<<19)
#define SASR1_CRDY	(1<<20)
#define SASR1_RS3V	(1<<21)
#define SASR1_RS4V	(1<<22)

#define SASCR_TUR	(1<<5)
#define SASCR_ROR	(1<<6)
#define SASCR_DTS	(1<<16)
#define SASCR_RDD	(1<<17)
#define SASCR_STO	(1<<18)

#define SADTCS_TDEN	(1<<0)
#define SADTCS_TDIE	(1<<1)
#define SADTCS_TDBDA	(1<<3)
#define SADTCS_TDSTA	(1<<4)
#define SADTCS_TDBDB	(1<<5)
#define SADTCS_TDSTB	(1<<6)
#define SADTCS_TBIU	(1<<7)

#define SADRCS_RDEN	(1<<0)
#define SADRCS_RDIE	(1<<1)
#define SADRCS_RDBDA	(1<<3)
#define SADRCS_RDSTA	(1<<4)
#define SADRCS_RDBDB	(1<<5)
#define SADRCS_RDSTB	(1<<6)
#define SADRCS_RBIU	(1<<7)

#define SAD_CS_DEN	(1<<0)
#define SAD_CS_DIE	(1<<1)	/* Not functional on metal 1 */
#define SAD_CS_DBDA	(1<<3)	/* Not functional on metal 1 */
#define SAD_CS_DSTA	(1<<4)
#define SAD_CS_DBDB	(1<<5)	/* Not functional on metal 1 */
#define SAD_CS_DSTB	(1<<6)
#define SAD_CS_BIU	(1<<7)	/* Not functional on metal 1 */

#define SAITR_TFS	(1<<0)
#define SAITR_RFS	(1<<1)
#define SAITR_TUR	(1<<2)
#define SAITR_ROR	(1<<3)
#define SAITR_CADT	(1<<4)
#define SAITR_SADR	(1<<5)
#define SAITR_RSTO	(1<<6)
#define SAITR_TDBDA	(1<<8)
#define SAITR_TDBDB	(1<<9)
#define SAITR_RDBDA	(1<<10)
#define SAITR_RDBDB	(1<<11)

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
 * PS/2 Trackpad and Mouse Interfaces
 *
 * Registers   (prefix kbd applies to trackpad interface, mse to mouse)
 *    KBDCR     Control Register
 *    KBDSTAT       Status Register
 *    KBDDATA       Transmit/Receive Data register
 *    KBDCLKDIV     Clock Division Register
 *    KBDPRECNT     Clock Precount Register
 *    KBDTEST1      Test register 1
 *    KBDTEST2      Test register 2
 *    KBDTEST3      Test register 3
 *    KBDTEST4      Test register 4
 *    MSECR
 *    MSESTAT
 *    MSEDATA
 *    MSECLKDIV
 *    MSEPRECNT
 *    MSETEST1
 *    MSETEST2
 *    MSETEST3
 *    MSETEST4
 *
 */

#define _KBD( x )   _SA1111( 0x0A00 )
#define _MSE( x )   _SA1111( 0x0C00 )

#define _KBDCR      _SA1111( 0x0A00 )
#define _KBDSTAT    _SA1111( 0x0A04 )
#define _KBDDATA    _SA1111( 0x0A08 )
#define _KBDCLKDIV  _SA1111( 0x0A0C )
#define _KBDPRECNT  _SA1111( 0x0A10 )
#define _MSECR      _SA1111( 0x0C00 )
#define _MSESTAT    _SA1111( 0x0C04 )
#define _MSEDATA    _SA1111( 0x0C08 )
#define _MSECLKDIV  _SA1111( 0x0C0C )
#define _MSEPRECNT  _SA1111( 0x0C10 )

#if ( LANGUAGE == C )

#define KBDCR       (*((volatile Word *) SA1111_p2v (_KBDCR)))
#define KBDSTAT     (*((volatile Word *) SA1111_p2v (_KBDSTAT)))
#define KBDDATA     (*((volatile Word *) SA1111_p2v (_KBDDATA)))
#define KBDCLKDIV   (*((volatile Word *) SA1111_p2v (_KBDCLKDIV)))
#define KBDPRECNT   (*((volatile Word *) SA1111_p2v (_KBDPRECNT)))
#define KBDTEST1    (*((volatile Word *) SA1111_p2v (_KBDTEST1)))
#define KBDTEST2    (*((volatile Word *) SA1111_p2v (_KBDTEST2)))
#define KBDTEST3    (*((volatile Word *) SA1111_p2v (_KBDTEST3)))
#define KBDTEST4    (*((volatile Word *) SA1111_p2v (_KBDTEST4)))
#define MSECR       (*((volatile Word *) SA1111_p2v (_MSECR)))
#define MSESTAT     (*((volatile Word *) SA1111_p2v (_MSESTAT)))
#define MSEDATA     (*((volatile Word *) SA1111_p2v (_MSEDATA)))
#define MSECLKDIV   (*((volatile Word *) SA1111_p2v (_MSECLKDIV)))
#define MSEPRECNT   (*((volatile Word *) SA1111_p2v (_MSEPRECNT)))
#define MSETEST1    (*((volatile Word *) SA1111_p2v (_MSETEST1)))
#define MSETEST2    (*((volatile Word *) SA1111_p2v (_MSETEST2)))
#define MSETEST3    (*((volatile Word *) SA1111_p2v (_MSETEST3)))
#define MSETEST4    (*((volatile Word *) SA1111_p2v (_MSETEST4)))

#define KBDCR_ENA        0x08
#define KBDCR_FKD        0x02
#define KBDCR_FKC        0x01

#define KBDSTAT_TXE      0x80
#define KBDSTAT_TXB      0x40
#define KBDSTAT_RXF      0x20
#define KBDSTAT_RXB      0x10
#define KBDSTAT_ENA      0x08
#define KBDSTAT_RXP      0x04
#define KBDSTAT_KBD      0x02
#define KBDSTAT_KBC      0x01

#define KBDCLKDIV_DivVal     Fld(4,0)

#define MSECR_ENA        0x08
#define MSECR_FKD        0x02
#define MSECR_FKC        0x01

#define MSESTAT_TXE      0x80
#define MSESTAT_TXB      0x40
#define MSESTAT_RXF      0x20
#define MSESTAT_RXB      0x10
#define MSESTAT_ENA      0x08
#define MSESTAT_RXP      0x04
#define MSESTAT_MSD      0x02
#define MSESTAT_MSC      0x01

#define MSECLKDIV_DivVal     Fld(4,0)

#define KBDTEST1_CD      0x80
#define KBDTEST1_RC1         0x40
#define KBDTEST1_MC      0x20
#define KBDTEST1_C       Fld(2,3)
#define KBDTEST1_T2      0x40
#define KBDTEST1_T1      0x20
#define KBDTEST1_T0      0x10
#define KBDTEST2_TICBnRES    0x08
#define KBDTEST2_RKC         0x04
#define KBDTEST2_RKD         0x02
#define KBDTEST2_SEL         0x01
#define KBDTEST3_ms_16       0x80
#define KBDTEST3_us_64       0x40
#define KBDTEST3_us_16       0x20
#define KBDTEST3_DIV8        0x10
#define KBDTEST3_DIn         0x08
#define KBDTEST3_CIn         0x04
#define KBDTEST3_KD      0x02
#define KBDTEST3_KC      0x01
#define KBDTEST4_BC12        0x80
#define KBDTEST4_BC11        0x40
#define KBDTEST4_TRES        0x20
#define KBDTEST4_CLKOE       0x10
#define KBDTEST4_CRES        0x08
#define KBDTEST4_RXB         0x04
#define KBDTEST4_TXB         0x02
#define KBDTEST4_SRX         0x01

#define MSETEST1_CD      0x80
#define MSETEST1_RC1         0x40
#define MSETEST1_MC      0x20
#define MSETEST1_C       Fld(2,3)
#define MSETEST1_T2      0x40
#define MSETEST1_T1      0x20
#define MSETEST1_T0      0x10
#define MSETEST2_TICBnRES    0x08
#define MSETEST2_RKC         0x04
#define MSETEST2_RKD         0x02
#define MSETEST2_SEL         0x01
#define MSETEST3_ms_16       0x80
#define MSETEST3_us_64       0x40
#define MSETEST3_us_16       0x20
#define MSETEST3_DIV8        0x10
#define MSETEST3_DIn         0x08
#define MSETEST3_CIn         0x04
#define MSETEST3_KD      0x02
#define MSETEST3_KC      0x01
#define MSETEST4_BC12        0x80
#define MSETEST4_BC11        0x40
#define MSETEST4_TRES        0x20
#define MSETEST4_CLKOE       0x10
#define MSETEST4_CRES        0x08
#define MSETEST4_RXB         0x04
#define MSETEST4_TXB         0x02
#define MSETEST4_SRX         0x01

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
