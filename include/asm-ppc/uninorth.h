/*
 * uninorth.h: definitions for using the "UniNorth" host bridge chip
 *             from Apple. This chip is used on "Core99" machines
 *
 */
#ifdef __KERNEL__


/*
 * Uni-N config space reg. definitions
 * 
 * (Little endian)
 */

/* Address ranges selection. This one should work with Bandit too */
#define UNI_N_ADDR_SELECT		0x48
#define UNI_N_ADDR_COARSE_MASK		0xffff0000	/* 256Mb regions at *0000000 */
#define UNI_N_ADDR_FINE_MASK		0x0000ffff	/*  16Mb regions at f*000000 */

/* AGP registers */
#define UNI_N_CFG_GART_BASE		0x8c
#define UNI_N_CFG_AGP_BASE		0x90
#define UNI_N_CFG_GART_CTRL		0x94
#define UNI_N_CFG_INTERNAL_STATUS	0x98

/* UNI_N_CFG_GART_CTRL bits definitions */
#define UNI_N_CFG_GART_INVAL		0x00000001
#define UNI_N_CFG_GART_ENABLE		0x00000100
#define UNI_N_CFG_GART_2xRESET		0x00010000


/* 
 * Uni-N memory mapped reg. definitions
 * 
 * Those registers are Big-Endian !!
 *
 * Their meaning come from either Darwin and/or from experiments I made with
 * the bootrom, I'm not sure about their exact meaning yet
 *
 */

/* Version of the UniNorth chip */
#define UNI_N_VERSION			0x0000		/* Known versions: 3,7 and 8 */
 
/* This register is used to enable/disable various parts */
#define UNI_N_CLOCK_CNTL		0x0020
#define UNI_N_CLOCK_CNTL_PCI		0x00000001	/* guess ? */
#define UNI_N_CLOCK_CNTL_GMAC		0x00000002
#define UNI_N_CLOCK_CNTL_FW		0x00000004	/* guess ? */

/* Power Management control ? (from Darwin) */
#define UNI_N_POWER_MGT			0x0030
#define UNI_N_POWER_MGT_NORMAL		0x00
#define UNI_N_POWER_MGT_IDLE2		0x01
#define UNI_N_POWER_MGT_SLEEP		0x02

/* This register is configured by Darwin depending on the UniN
 * revision
 */
#define UNI_N_ARB_CTRL			0x0040
#define UNI_N_ARB_CTRL_QACK_DELAY_SHIFT	15
#define UNI_N_ARB_CTRL_QACK_DELAY_MASK	0x0e1f8000
#define UNI_N_ARB_CTRL_QACK_DELAY	0x30
#define UNI_N_ARB_CTRL_QACK_DELAY105	0x00

/* This one _might_ return the CPU number of the CPU reading it;
 * the bootROM decides wether to boot or to sleep/spinloop depending
 * on this register beeing 0 or not
 */
#define UNI_N_CPU_NUMBER		0x0050

/* This register appear to be read by the bootROM to decide what
 *  to do on a non-recoverable reset (powerup or wakeup)
 */
#define UNI_N_HWINIT_STATE		0x0070
#define UNI_N_HWINIT_STATE_SLEEPING	0x01
#define UNI_N_HWINIT_STATE_RUNNING	0x02
/* This last bit appear to be used by the bootROM to know the second
 * CPU has started and will enter it's sleep loop with IP=0
 */
#define UNI_N_HWINIT_STATE_CPU1_FLAG	0x10000000

#endif /* __KERNEL__ */
