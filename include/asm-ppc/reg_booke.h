/*
 * Contains register definitions common to the Book E PowerPC
 * specification.  Notice that while the IBM-40x series of CPUs
 * are not true Book E PowerPCs, they borrowed a number of features
 * before Book E was finalized, and are included here as well.  Unfortunatly,
 * they sometimes used different locations than true Book E CPUs did.
 */
#ifdef __KERNEL__
#ifndef __ASM_PPC_REG_BOOKE_H__
#define __ASM_PPC_REG_BOOKE_H__

#ifndef __ASSEMBLY__
/* Device Control Registers */
void __mtdcr(int reg, unsigned int val);
unsigned int __mfdcr(int reg);
#define mfdcr(rn)						\
	({unsigned int rval;					\
	if (__builtin_constant_p(rn))				\
		asm volatile("mfdcr %0," __stringify(rn)	\
		              : "=r" (rval));			\
	else							\
		rval = __mfdcr(rn);				\
	rval;})

#define mtdcr(rn, v)						\
do {								\
	if (__builtin_constant_p(rn))				\
		asm volatile("mtdcr " __stringify(rn) ",%0"	\
			      : : "r" (v)); 			\
	else							\
		__mtdcr(rn, v);					\
} while (0)

/* R/W of indirect DCRs make use of standard naming conventions for DCRs */
#define mfdcri(base, reg)			\
({						\
	mtdcr(base ## _CFGADDR, base ## _ ## reg);	\
	mfdcr(base ## _CFGDATA);			\
})

#define mtdcri(base, reg, data)			\
do {						\
	mtdcr(base ## _CFGADDR, base ## _ ## reg);	\
	mtdcr(base ## _CFGDATA, data);		\
} while (0)
#endif /* __ASSEMBLY__ */


/* Machine State Register (MSR) Fields */
#define MSR_DWE		(1<<10)	/* Debug Wait Enable */
#define MSR_IS		MSR_IR	/* Instruction Space */
#define MSR_DS		MSR_DR	/* Data Space */

/* Default MSR for kernel mode. */
#if defined (CONFIG_40x)
#define MSR_KERNEL	(MSR_ME|MSR_RI|MSR_IR|MSR_DR|MSR_CE|MSR_DE)
#elif defined(CONFIG_BOOKE)
#define MSR_KERNEL	(MSR_ME|MSR_RI|MSR_CE|MSR_DE)
#endif

/* Special Purpose Registers (SPRNs)*/
#define SPRN_DECAR	0x036	/* Decrementer Auto Reload Register */
#define SPRN_IVPR	0x03F	/* Interrupt Vector Prefix Register */
#define SPRN_USPRG0	0x100	/* User Special Purpose Register General 0 */
#define SPRN_SPRG4R	0x104	/* Special Purpose Register General 4 Read */
#define SPRN_SPRG5R	0x105	/* Special Purpose Register General 5 Read */
#define SPRN_SPRG6R	0x106	/* Special Purpose Register General 6 Read */
#define SPRN_SPRG7R	0x107	/* Special Purpose Register General 7 Read */
#define SPRN_SPRG4W	0x114	/* Special Purpose Register General 4 Write */
#define SPRN_SPRG5W	0x115	/* Special Purpose Register General 5 Write */
#define SPRN_SPRG6W	0x116	/* Special Purpose Register General 6 Write */
#define SPRN_SPRG7W	0x117	/* Special Purpose Register General 7 Write */
#define SPRN_DBCR2	0x136	/* Debug Control Register 2 */
#define SPRN_IAC3	0x13A	/* Instruction Address Compare 3 */
#define SPRN_IAC4	0x13B	/* Instruction Address Compare 4 */
#define SPRN_DVC1	0x13E	/* Data Value Compare Register 1 */
#define SPRN_DVC2	0x13F	/* Data Value Compare Register 2 */
#define SPRN_IVOR0	0x190	/* Interrupt Vector Offset Register 0 */
#define SPRN_IVOR1	0x191	/* Interrupt Vector Offset Register 1 */
#define SPRN_IVOR2	0x192	/* Interrupt Vector Offset Register 2 */
#define SPRN_IVOR3	0x193	/* Interrupt Vector Offset Register 3 */
#define SPRN_IVOR4	0x194	/* Interrupt Vector Offset Register 4 */
#define SPRN_IVOR5	0x195	/* Interrupt Vector Offset Register 5 */
#define SPRN_IVOR6	0x196	/* Interrupt Vector Offset Register 6 */
#define SPRN_IVOR7	0x197	/* Interrupt Vector Offset Register 7 */
#define SPRN_IVOR8	0x198	/* Interrupt Vector Offset Register 8 */
#define SPRN_IVOR9	0x199	/* Interrupt Vector Offset Register 9 */
#define SPRN_IVOR10	0x19A	/* Interrupt Vector Offset Register 10 */
#define SPRN_IVOR11	0x19B	/* Interrupt Vector Offset Register 11 */
#define SPRN_IVOR12	0x19C	/* Interrupt Vector Offset Register 12 */
#define SPRN_IVOR13	0x19D	/* Interrupt Vector Offset Register 13 */
#define SPRN_IVOR14	0x19E	/* Interrupt Vector Offset Register 14 */
#define SPRN_IVOR15	0x19F	/* Interrupt Vector Offset Register 15 */
#define SPRN_MCSRR0	0x23A	/* Machine Check Save and Restore Register 0 */
#define SPRN_MCSRR1	0x23B	/* Machine Check Save and Restore Register 1 */
#define SPRN_MCSR	0x23C	/* Machine Check Status Register */
#ifdef CONFIG_440A
#define  MCSR_MCS	0x80000000 /* Machine Check Summary */
#define  MCSR_IB	0x40000000 /* Instruction PLB Error */
#define  MCSR_DRB	0x20000000 /* Data Read PLB Error */
#define  MCSR_DWB	0x10000000 /* Data Write PLB Error */
#define  MCSR_TLBP	0x08000000 /* TLB Parity Error */
#define  MCSR_ICP	0x04000000 /* I-Cache Parity Error */
#define  MCSR_DCSP	0x02000000 /* D-Cache Search Parity Error */
#define  MCSR_DCFP	0x01000000 /* D-Cache Flush Parity Error */
#define  MCSR_IMPE	0x00800000 /* Imprecise Machine Check Exception */
#endif
#define SPRN_ZPR	0x3B0	/* Zone Protection Register (40x) */
#define SPRN_MMUCR	0x3B2	/* MMU Control Register */
#define SPRN_CCR0	0x3B3	/* Core Configuration Register */
#define SPRN_SGR	0x3B9	/* Storage Guarded Register */
#define SPRN_DCWR	0x3BA	/* Data Cache Write-thru Register */
#define SPRN_SLER	0x3BB	/* Little-endian real mode */
#define SPRN_SU0R	0x3BC	/* "User 0" real mode (40x) */
#define SPRN_DCMP	0x3D1	/* Data TLB Compare Register */
#define SPRN_ICDBDR	0x3D3	/* Instruction Cache Debug Data Register */
#define SPRN_EVPR	0x3D6	/* Exception Vector Prefix Register */
#define SPRN_PIT	0x3DB	/* Programmable Interval Timer */
#define SPRN_DCCR	0x3FA	/* Data Cache Cacheability Register */
#define SPRN_ICCR	0x3FB	/* Instruction Cache Cacheability Register */

/*
 * SPRs which have conflicting definitions on true Book E versus classic,
 * or IBM 40x.
 */
#ifdef CONFIG_BOOKE
#define SPRN_PID	0x030	/* Process ID */
#define SPRN_CSRR0	0x03A	/* Critical Save and Restore Register 0 */
#define SPRN_CSRR1	0x03B	/* Critical Save and Restore Register 1 */
#define SPRN_DEAR	0x03D	/* Data Error Address Register */
#define SPRN_ESR	0x03E	/* Exception Syndrome Register */
#define SPRN_PIR	0x11E	/* Processor Identification Register */
#define SPRN_DBSR	0x130	/* Debug Status Register */
#define SPRN_DBCR0	0x134	/* Debug Control Register 0 */
#define SPRN_DBCR1	0x135	/* Debug Control Register 1 */
#define SPRN_IAC1	0x138	/* Instruction Address Compare 1 */
#define SPRN_IAC2	0x139	/* Instruction Address Compare 2 */
#define SPRN_DAC1	0x13C	/* Data Address Compare 1 */
#define SPRN_DAC2	0x13D	/* Data Address Compare 2 */
#define SPRN_TSR	0x150	/* Timer Status Register */
#define SPRN_TCR	0x154	/* Timer Control Register */
#endif /* Book E */
#ifdef CONFIG_40x
#define SPRN_PID	0x3B1	/* Process ID */
#define SPRN_DBCR1	0x3BD	/* Debug Control Register 1 */		
#define SPRN_ESR	0x3D4	/* Exception Syndrome Register */
#define SPRN_DEAR	0x3D5	/* Data Error Address Register */
#define SPRN_TSR	0x3D8	/* Timer Status Register */
#define SPRN_TCR	0x3DA	/* Timer Control Register */
#define SPRN_SRR2	0x3DE	/* Save/Restore Register 2 */
#define SPRN_SRR3	0x3DF	/* Save/Restore Register 3 */
#define SPRN_DBSR	0x3F0	/* Debug Status Register */		
#define SPRN_DBCR0	0x3F2	/* Debug Control Register 0 */
#define SPRN_DAC1	0x3F6	/* Data Address Compare 1 */
#define SPRN_DAC2	0x3F7	/* Data Address Compare 2 */
#define SPRN_CSRR0	SPRN_SRR2 /* Critical Save and Restore Register 0 */
#define SPRN_CSRR1	SPRN_SRR3 /* Critical Save and Restore Register 1 */
#endif

/* Bit definitions for the DBSR. */
/*
 * DBSR bits which have conflicting definitions on true Book E versus IBM 40x.
 */
#ifdef CONFIG_BOOKE
#define DBSR_IC		0x08000000	/* Instruction Completion */
#define DBSR_BT		0x04000000	/* Branch Taken */
#define DBSR_TIE	0x01000000	/* Trap Instruction Event */
#endif
#ifdef CONFIG_40x
#define DBSR_IC		0x80000000	/* Instruction Completion */
#define DBSR_BT		0x40000000	/* Branch taken */
#define DBSR_TIE	0x10000000	/* Trap Instruction debug Event */
#endif

/* Bit definitions related to the ESR. */
#define ESR_MCI		0x80000000	/* Machine Check - Instruction */
#define ESR_IMCP	0x80000000	/* Instr. Machine Check - Protection */
#define ESR_IMCN	0x40000000	/* Instr. Machine Check - Non-config */
#define ESR_IMCB	0x20000000	/* Instr. Machine Check - Bus error */
#define ESR_IMCT	0x10000000	/* Instr. Machine Check - Timeout */
#define ESR_PIL		0x08000000	/* Program Exception - Illegal */
#define ESR_PPR		0x04000000	/* Program Exception - Priveleged */
#define ESR_PTR		0x02000000	/* Program Exception - Trap */
#define ESR_DST		0x00800000	/* Storage Exception - Data miss */
#define ESR_DIZ		0x00400000	/* Storage Exception - Zone fault */
#define ESR_ST		0x00800000	/* Store Operation */

/* Bit definitions related to the DBCR0. */
#define DBCR0_EDM	0x80000000	/* External Debug Mode */
#define DBCR0_IDM	0x40000000	/* Internal Debug Mode */
#define DBCR0_RST	0x30000000	/* all the bits in the RST field */
#define DBCR0_RST_SYSTEM 0x30000000	/* System Reset */
#define DBCR0_RST_CHIP	0x20000000	/* Chip Reset */
#define DBCR0_RST_CORE	0x10000000	/* Core Reset */
#define DBCR0_RST_NONE	0x00000000	/* No Reset */
#define DBCR0_IC	0x08000000	/* Instruction Completion */
#define DBCR0_BT	0x04000000	/* Branch Taken */
#define DBCR0_EDE	0x02000000	/* Exception Debug Event */
#define DBCR0_TDE	0x01000000	/* TRAP Debug Event */
#define DBCR0_IA1	0x00800000	/* Instr Addr compare 1 enable */
#define DBCR0_IA2	0x00400000	/* Instr Addr compare 2 enable */
#define DBCR0_IA12	0x00200000	/* Instr Addr 1-2 range enable */
#define DBCR0_IA12X	0x00100000	/* Instr Addr 1-2 range eXclusive */
#define DBCR0_IA3	0x00080000	/* Instr Addr compare 3 enable */
#define DBCR0_IA4	0x00040000	/* Instr Addr compare 4 enable */
#define DBCR0_IA34	0x00020000	/* Instr Addr 3-4 range Enable */
#define DBCR0_IA34X	0x00010000	/* Instr Addr 3-4 range eXclusive */
#define DBCR0_IA12T	0x00008000	/* Instr Addr 1-2 range Toggle */
#define DBCR0_IA34T	0x00004000	/* Instr Addr 3-4 range Toggle */
#define DBCR0_FT	0x00000001	/* Freeze Timers on debug event */

/* Bit definitions related to the TCR. */
#define TCR_WP(x)	(((x)&0x3)<<30)	/* WDT Period */
#define TCR_WP_MASK	TCR_WP(3)
#define WP_2_17		0		/* 2^17 clocks */
#define WP_2_21		1		/* 2^21 clocks */
#define WP_2_25		2		/* 2^25 clocks */
#define WP_2_29		3		/* 2^29 clocks */
#define TCR_WRC(x)	(((x)&0x3)<<28)	/* WDT Reset Control */
#define TCR_WRC_MASK	TCR_WRC(3)
#define WRC_NONE	0		/* No reset will occur */
#define WRC_CORE	1		/* Core reset will occur */
#define WRC_CHIP	2		/* Chip reset will occur */
#define WRC_SYSTEM	3		/* System reset will occur */
#define TCR_WIE		0x08000000	/* WDT Interrupt Enable */
#define TCR_PIE		0x04000000	/* PIT Interrupt Enable */
#define TCR_DIE		TCR_PIE		/* DEC Interrupt Enable */
#define TCR_FP(x)	(((x)&0x3)<<24)	/* FIT Period */
#define TCR_FP_MASK	TCR_FP(3)
#define FP_2_9		0		/* 2^9 clocks */
#define FP_2_13		1		/* 2^13 clocks */
#define FP_2_17		2		/* 2^17 clocks */
#define FP_2_21		3		/* 2^21 clocks */
#define TCR_FIE		0x00800000	/* FIT Interrupt Enable */
#define TCR_ARE		0x00400000	/* Auto Reload Enable */

/* Bit definitions for the TSR. */
#define TSR_ENW		0x80000000	/* Enable Next Watchdog */
#define TSR_WIS		0x40000000	/* WDT Interrupt Status */
#define TSR_WRS(x)	(((x)&0x3)<<28)	/* WDT Reset Status */
#define WRS_NONE	0		/* No WDT reset occurred */
#define WRS_CORE	1		/* WDT forced core reset */
#define WRS_CHIP	2		/* WDT forced chip reset */
#define WRS_SYSTEM	3		/* WDT forced system reset */
#define TSR_PIS		0x08000000	/* PIT Interrupt Status */
#define TSR_DIS		TSR_PIS		/* DEC Interrupt Status */
#define TSR_FIS		0x04000000	/* FIT Interrupt Status */

/* Bit definitions for the DCCR. */
#define DCCR_NOCACHE	0		/* Noncacheable */
#define DCCR_CACHE	1		/* Cacheable */

/* Bit definitions for DCWR. */
#define DCWR_COPY	0		/* Copy-back */
#define DCWR_WRITE	1		/* Write-through */

/* Bit definitions for ICCR. */
#define ICCR_NOCACHE	0		/* Noncacheable */
#define ICCR_CACHE	1		/* Cacheable */

/* Bit definitions for SGR. */
#define SGR_NORMAL	0		/* Speculative fetching allowed. */
#define SGR_GUARDED	1		/* Speculative fetching disallowed. */

/* Short-hand for various SPRs. */
#ifdef CONFIG_BOOKE
#define CSRR0	SPRN_CSRR0	/* Critical Save and Restore Register 0 */
#define CSRR1	SPRN_CSRR1	/* Critical Save and Restore Register 1 */
#else
#define CSRR0	SPRN_SRR2	/* Logically and functionally equivalent. */
#define CSRR1	SPRN_SRR3	/* Logically and functionally equivalent. */
#endif
#define MCSRR0	SPRN_MCSRR0	/* Machine Check Save and Restore Register 0 */
#define MCSRR1	SPRN_MCSRR1	/* Machine Check Save and Restore Register 1 */
#define DCMP	SPRN_DCMP	/* Data TLB Compare Register */
#define SPRG4R	SPRN_SPRG4R	/* Supervisor Private Registers */
#define SPRG5R	SPRN_SPRG5R
#define SPRG6R	SPRN_SPRG6R
#define SPRG7R	SPRN_SPRG7R
#define SPRG4W	SPRN_SPRG4W
#define SPRG5W	SPRN_SPRG5W
#define SPRG6W	SPRN_SPRG6W
#define SPRG7W	SPRN_SPRG7W

/*
 * The IBM-403 is an even more odd special case, as it is much
 * older than the IBM-405 series.  We put these down here incase someone
 * wishes to support these machines again.
 */
#ifdef CONFIG_403GCX
/* Special Purpose Registers (SPRNs)*/
#define SPRN_TBHU	0x3CC	/* Time Base High User-mode */
#define SPRN_TBLU	0x3CD	/* Time Base Low User-mode */
#define SPRN_CDBCR	0x3D7	/* Cache Debug Control Register */
#define SPRN_TBHI	0x3DC	/* Time Base High */
#define SPRN_TBLO	0x3DD	/* Time Base Low */
#define SPRN_DBCR	0x3F2	/* Debug Control Regsiter */
#define SPRN_PBL1	0x3FC	/* Protection Bound Lower 1 */
#define SPRN_PBL2	0x3FE	/* Protection Bound Lower 2 */
#define SPRN_PBU1	0x3FD	/* Protection Bound Upper 1 */
#define SPRN_PBU2	0x3FF	/* Protection Bound Upper 2 */


/* Bit definitions for the DBCR. */
#define DBCR_EDM	DBCR0_EDM
#define DBCR_IDM	DBCR0_IDM
#define DBCR_RST(x)	(((x) & 0x3) << 28)
#define DBCR_RST_NONE	0
#define DBCR_RST_CORE	1
#define DBCR_RST_CHIP	2
#define DBCR_RST_SYSTEM	3
#define DBCR_IC		DBCR0_IC	/* Instruction Completion Debug Evnt */
#define DBCR_BT		DBCR0_BT	/* Branch Taken Debug Event */
#define DBCR_EDE	DBCR0_EDE	/* Exception Debug Event */
#define DBCR_TDE	DBCR0_TDE	/* TRAP Debug Event */
#define DBCR_FER	0x00F80000	/* First Events Remaining Mask */
#define DBCR_FT		0x00040000	/* Freeze Timers on Debug Event */
#define DBCR_IA1	0x00020000	/* Instr. Addr. Compare 1 Enable */
#define DBCR_IA2	0x00010000	/* Instr. Addr. Compare 2 Enable */
#define DBCR_D1R	0x00008000	/* Data Addr. Compare 1 Read Enable */
#define DBCR_D1W	0x00004000	/* Data Addr. Compare 1 Write Enable */
#define DBCR_D1S(x)	(((x) & 0x3) << 12)	/* Data Adrr. Compare 1 Size */
#define DAC_BYTE	0
#define DAC_HALF	1
#define DAC_WORD	2
#define DAC_QUAD	3
#define DBCR_D2R	0x00000800	/* Data Addr. Compare 2 Read Enable */
#define DBCR_D2W	0x00000400	/* Data Addr. Compare 2 Write Enable */
#define DBCR_D2S(x)	(((x) & 0x3) << 8)	/* Data Addr. Compare 2 Size */
#define DBCR_SBT	0x00000040	/* Second Branch Taken Debug Event */
#define DBCR_SED	0x00000020	/* Second Exception Debug Event */
#define DBCR_STD	0x00000010	/* Second Trap Debug Event */
#define DBCR_SIA	0x00000008	/* Second IAC Enable */
#define DBCR_SDA	0x00000004	/* Second DAC Enable */
#define DBCR_JOI	0x00000002	/* JTAG Serial Outbound Int. Enable */
#define DBCR_JII	0x00000001	/* JTAG Serial Inbound Int. Enable */
#endif /* 403GCX */
#endif /* __ASM_PPC_REG_BOOKE_H__ */
#endif /* __KERNEL__ */
