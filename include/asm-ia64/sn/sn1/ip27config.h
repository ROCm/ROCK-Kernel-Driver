/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#ifndef _ASM_SN_SN1_IP27CONFIG_H
#define _ASM_SN_SN1_IP27CONFIG_H


/*
 * Structure: 	ip27config_s
 * Typedef:	ip27config_t
 * Purpose: 	Maps out the region of the boot prom used to define
 *		configuration information.
 * Notes:       Corresponds to ip27config structure found in start.s.
 *		Fields are ulong where possible to facilitate IP27 PROM fetches.
 */

#define CONFIG_INFO_OFFSET		0x60

#define IP27CONFIG_ADDR			(LBOOT_BASE	    + \
					 CONFIG_INFO_OFFSET)
#define IP27CONFIG_ADDR_NODE(n)		(NODE_RBOOT_BASE(n) + \
					 CONFIG_INFO_OFFSET)

/* Offset to the config_type field within local ip27config structure */
#define CONFIG_FLAGS_ADDR			(IP27CONFIG_ADDR + 72)
/* Offset to the config_type field in the ip27config structure on 
 * node with nasid n
 */
#define CONFIG_FLAGS_ADDR_NODE(n)		(IP27CONFIG_ADDR_NODE(n) + 72)

/* Meaning of each valid bit in the config flags 
 * None are currently defined
 */

/* Meaning of each mach_type value
 */
#define SN1_MACH_TYPE 0

/*
 * Since 800 ns works well with various HUB frequencies, (such as 360,
 * 380, 390, and 400 MHZ), we now use 800ns rtc cycle time instead of
 * 1 microsec.
 */
#define IP27_RTC_FREQ			1250	/* 800ns cycle time */

#if _LANGUAGE_C

typedef	struct ip27config_s {		/* KEEP IN SYNC w/ start.s & below  */
    uint		time_const;	/* Time constant 		    */
    uint		r10k_mode;	/* R10k boot mode bits 		    */

    uint64_t		magic;		/* CONFIG_MAGIC			    */

    uint64_t		freq_cpu;	/* Hz 				    */
    uint64_t		freq_hub;	/* Hz 				    */
    uint64_t		freq_rtc;	/* Hz 				    */

    uint		ecc_enable;	/* ECC enable flag		    */
    uint		fprom_cyc;	/* FPROM_CYC speed control  	    */

    uint		mach_type;	/* Inidicate IP27 (0) or Sn00 (1)    */

    uint		check_sum_adj;	/* Used after config hdr overlay    */
					/* to make the checksum 0 again     */
    uint		flash_count;	/* Value incr'd on each PROM flash  */
    uint		fprom_wr;	/* FPROM_WR speed control  	    */

    uint		pvers_vers;	/* Prom version number		    */
    uint		pvers_rev;	/* Prom revision number		    */
    uint		config_type;	/* To support special configurations
					 * (none currently defined)
					 */
} ip27config_t;

typedef	struct {
    uint		r10k_mode;	/* R10k boot mode bits 		    */
    uint		freq_cpu;	/* Hz 				    */
    uint		freq_hub;	/* Hz 				    */
    char		fprom_cyc;	/* FPROM_CYC speed control  	    */
    char		mach_type;	/* IP35(0) is only type defined      */
    char		fprom_wr;	/* FPROM_WR speed control  	    */
} config_modifiable_t;

#define IP27CONFIG		(*(ip27config_t *) IP27CONFIG_ADDR)
#define IP27CONFIG_NODE(n)	(*(ip27config_t *) IP27CONFIG_ADDR_NODE(n))
#define SN00			0 /* IP35 has no Speedo equivalent */

/* Get the config flags from local ip27config */
#define CONFIG_FLAGS		(*(uint *) (CONFIG_FLAGS_ADDR))

/* Get the config flags from ip27config on the node
 * with nasid n
 */
#define CONFIG_FLAGS_NODE(n)	(*(uint *) (CONFIG_FLAGS_ADDR_NODE(n)))

/* Macro to check if the local ip27config indicates a config
 * of 12 p 4io
 */
#define CONFIG_12P4I		(0) /* IP35 has no 12p4i equivalent */

/* Macro to check if the ip27config on node with nasid n
 * indicates a config of 12 p 4io
 */
#define CONFIG_12P4I_NODE(n)	(0)

#endif /* _LANGUAGE_C */

#if _LANGUAGE_ASSEMBLY
	.struct		0		/* KEEP IN SYNC WITH C structure */

ip27c_time_const:	.word	0
ip27c_r10k_mode:	.word	0

ip27c_magic:		.dword	0

ip27c_freq_cpu:		.dword	0
ip27c_freq_hub:		.dword	0
ip27c_freq_rtc:		.dword	0

ip27c_ecc_enable:	.word	1
ip27c_fprom_cyc:	.word	0

ip27c_mach_type:	.word	0
ip27c_check_sum_adj:	.word	0

ip27c_flash_count:	.word	0
ip27c_fprom_wr:		.word	0

ip27c_pvers_vers:	.word	0
ip27c_pvers_rev:	.word	0

ip27c_config_type:	.word 	0	/* To recognize special configs */
#endif /* _LANGUAGE_ASSEMBLY */

/*
 * R10000 Configuration Cycle - These define the SYSAD values used
 * during the reset cycle.
 */

#define	IP27C_R10000_KSEG0CA_SHFT	0
#define	IP27C_R10000_KSEG0CA_MASK	(7 << IP27C_R10000_KSEG0CA_SHFT)
#define	IP27C_R10000_KSEG0CA(_B)	 ((_B) << IP27C_R10000_KSEG0CA_SHFT)

#define	IP27C_R10000_DEVNUM_SHFT	3
#define	IP27C_R10000_DEVNUM_MASK	(3 << IP27C_R10000_DEVNUM_SHFT)
#define	IP27C_R10000_DEVNUM(_B)		((_B) << IP27C_R10000_DEVNUM_SHFT)

#define	IP27C_R10000_CRPT_SHFT		5
#define	IP27C_R10000_CRPT_MASK		(1 << IP27C_R10000_CRPT_SHFT)
#define	IP27C_R10000_CPRT(_B)		((_B)<<IP27C_R10000_CRPT_SHFT)

#define	IP27C_R10000_PER_SHFT		6
#define	IP27C_R10000_PER_MASK		(1 << IP27C_R10000_PER_SHFT)
#define	IP27C_R10000_PER(_B)		((_B) << IP27C_R10000_PER_SHFT)

#define	IP27C_R10000_PRM_SHFT		7
#define	IP27C_R10000_PRM_MASK		(3 << IP27C_R10000_PRM_SHFT)
#define	IP27C_R10000_PRM(_B)		((_B) << IP27C_R10000_PRM_SHFT)

#define	IP27C_R10000_SCD_SHFT		9
#define	IP27C_R10000_SCD_MASK		(0xf << IP27C_R10000_SCD_MASK)
#define	IP27C_R10000_SCD(_B)		((_B) << IP27C_R10000_SCD_SHFT)

#define	IP27C_R10000_SCBS_SHFT		13
#define	IP27C_R10000_SCBS_MASK		(1 << IP27C_R10000_SCBS_SHFT)
#define	IP27C_R10000_SCBS(_B)		(((_B)) << IP27C_R10000_SCBS_SHFT)

#define	IP27C_R10000_SCCE_SHFT		14
#define	IP27C_R10000_SCCE_MASK		(1 << IP27C_R10000_SCCE_SHFT)
#define	IP27C_R10000_SCCE(_B)		((_B) << IP27C_R10000_SCCE_SHFT)

#define	IP27C_R10000_ME_SHFT		15
#define	IP27C_R10000_ME_MASK		(1 << IP27C_R10000_ME_SHFT)
#define	IP27C_R10000_ME(_B)		((_B) << IP27C_R10000_ME_SHFT)

#define	IP27C_R10000_SCS_SHFT		16
#define	IP27C_R10000_SCS_MASK		(7 << IP27C_R10000_SCS_SHFT)
#define	IP27C_R10000_SCS(_B)		((_B) << IP27C_R10000_SCS_SHFT)

#define	IP27C_R10000_SCCD_SHFT		19
#define	IP27C_R10000_SCCD_MASK		(7 << IP27C_R10000_SCCD_SHFT)
#define	IP27C_R10000_SCCD(_B)		((_B) << IP27C_R10000_SCCD_SHFT)

#define	IP27C_R10000_SCCT_SHFT		25
#define	IP27C_R10000_SCCT_MASK		(0xf << IP27C_R10000_SCCT_SHFT)
#define	IP27C_R10000_SCCT(_B)		((_B) << IP27C_R10000_SCCT_SHFT)

#define	IP27C_R10000_ODSC_SHFT		29
#define IP27C_R10000_ODSC_MASK		(1 << IP27C_R10000_ODSC_SHFT)
#define	IP27C_R10000_ODSC(_B)		((_B) << IP27C_R10000_ODSC_SHFT)

#define	IP27C_R10000_ODSYS_SHFT		30
#define	IP27C_R10000_ODSYS_MASK		(1 << IP27C_R10000_ODSYS_SHFT)
#define	IP27C_R10000_ODSYS(_B)		((_B) << IP27C_R10000_ODSYS_SHFT)

#define	IP27C_R10000_CTM_SHFT		31
#define	IP27C_R10000_CTM_MASK		(1 << IP27C_R10000_CTM_SHFT)
#define	IP27C_R10000_CTM(_B)		((_B) << IP27C_R10000_CTM_SHFT)

#define IP27C_MHZ(x)			(1000000 * (x))
#define IP27C_KHZ(x)			(1000 * (x))
#define IP27C_MB(x)			((x) << 20)

/*
 * PROM Configurations
 */

#define CONFIG_MAGIC		0x69703237636f6e66

/* The high 32 bits of the "mode bits".  Bits 7..0 contain one more
 * than the number of 5ms clocks in the 100ms "long delay" intervals
 * of the TRex reset sequence.  Bit 8 is the "synergy mode" bit.
 */
#define CONFIG_TIME_CONST	0x15

#define CONFIG_ECC_ENABLE	1
#define CONFIG_CHECK_SUM_ADJ	0
#define CONFIG_DEFAULT_FLASH_COUNT    0

/*
 * Some promICEs have trouble if CONFIG_FPROM_SETUP is too low.
 * The nominal value for 100 MHz hub is 5, for 200MHz bedrock is 16.
 * any update to the below should also reflected in the logic in
 *   IO7prom/flashprom.c function _verify_config_info and _fill_in_config_info
 */

/* default junk bus timing values to use */
#define CONFIG_SYNERGY_ENABLE	0xff
#define CONFIG_SYNERGY_SETUP	0xff
#define CONFIG_UART_ENABLE	0x0c
#define CONFIG_UART_SETUP	0x02
#define CONFIG_FPROM_ENABLE	0x10
#define CONFIG_FPROM_SETUP	0x10

#define CONFIG_FREQ_RTC	IP27C_KHZ(IP27_RTC_FREQ)

#if _LANGUAGE_C

/* we are going to define all the known configs is a table
 * for building hex images we will pull out the particular
 * slice we care about by using the IP27_CONFIG_XX_XX as
 * entries into the table
 * to keep the table of reasonable size we only include the
 * values that differ across configurations
 * please note then that this makes assumptions about what
 * will and will not change across configurations
 */

/* these numbers are as the are ordered in the table below */
#define	IP27_CONFIG_UNKNOWN -1
#define IP27_CONFIG_SN1_1MB_200_400_200_TABLE 0
#define IP27_CONFIG_SN00_4MB_100_200_133_TABLE 1
#define IP27_CONFIG_SN1_4MB_200_400_267_TABLE 2
#define IP27_CONFIG_SN1_8MB_200_500_250_TABLE 3
#define IP27_CONFIG_SN1_8MB_200_400_267_TABLE 4
#define IP27_CONFIG_SN1_4MB_180_360_240_TABLE 5
#define NUMB_IP_CONFIGS 6

#ifdef DEF_IP_CONFIG_TABLE
/*
 * N.B.: A new entry needs to be added here everytime a new config is added
 * The table is indexed by the PIMM PSC value
 */

static int psc_to_flash_config[] = {
        IP27_CONFIG_SN1_4MB_200_400_267_TABLE,	/* 0x0 */
        IP27_CONFIG_SN1_8MB_200_500_250_TABLE,	/* 0x1 */
        IP27_CONFIG_SN1_8MB_200_400_267_TABLE,	/* 0x2 */
        IP27_CONFIG_UNKNOWN,	/* 0x3 */
        IP27_CONFIG_UNKNOWN,	/* 0x4 */
        IP27_CONFIG_UNKNOWN,	/* 0x5 */
        IP27_CONFIG_UNKNOWN,	/* 0x6 */
        IP27_CONFIG_UNKNOWN,	/* 0x7 */
        IP27_CONFIG_SN1_4MB_180_360_240_TABLE,	/* 0x8 */
        IP27_CONFIG_UNKNOWN,	/* 0x9 */
        IP27_CONFIG_UNKNOWN,	/* 0xa */
        IP27_CONFIG_UNKNOWN,	/* 0xb */
        IP27_CONFIG_UNKNOWN,	/* 0xc */
        IP27_CONFIG_UNKNOWN,	/* 0xd */
        IP27_CONFIG_SN00_4MB_100_200_133_TABLE, /* 0xe  O200 PIMM for bringup */
        IP27_CONFIG_UNKNOWN	/* 0xf == PIMM not installed */
};

static config_modifiable_t ip_config_table[NUMB_IP_CONFIGS] = {
/* the 1MB_200_400_200 values (Generic settings, will work for any config.) */
{
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(3)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(1)	 + \
	 IP27C_R10000_SCCD(3)	 + \
	 IP27C_R10000_SCCT(9)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(1)	 + \
	 IP27C_R10000_CTM(0)),
	IP27C_MHZ(400),
	IP27C_MHZ(200),
	CONFIG_FPROM_SETUP,
	SN1_MACH_TYPE,
	CONFIG_FPROM_ENABLE
},

/* the 4MB_100_200_133 values (O200 PIMM w/translation board, PSC 0xe)
 * (SysAD at 100MHz (SCD=3), and bedrock core at 200 MHz) */
{
 /* ODSYS == 0 means HSTL1 on SysAD bus; other PIMMs use HSTL2 */
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(3)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(3)	 + \
	 IP27C_R10000_SCCD(2)	 + \
	 IP27C_R10000_SCCT(9)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(0)	 + \
	 IP27C_R10000_CTM(0)),
	IP27C_MHZ(200),
	IP27C_MHZ(200),
	CONFIG_FPROM_SETUP,
	SN1_MACH_TYPE,
	CONFIG_FPROM_ENABLE
},

/* 4MB_200_400_267 values (R12KS, 3.7ns, LWR, 030-1602-001, PSC 0x0) */
{
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(3)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(3)	 + \
	 IP27C_R10000_SCCD(2)	 + \
	 IP27C_R10000_SCCT(0xa)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(1)	 + \
	 IP27C_R10000_CTM(0)),
	IP27C_MHZ(400),
	IP27C_MHZ(200),
	CONFIG_FPROM_SETUP,
	SN1_MACH_TYPE,
	CONFIG_FPROM_ENABLE
},

/* 8MB_200_500_250 values (R14K, 4.0ns, DDR1, 030-1520-001, PSC 0x1) */
{
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(4)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(4)	 + \
	 IP27C_R10000_SCCD(3)	 + \
	 IP27C_R10000_SCCT(0xa)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(1)	 + \
	 IP27C_R10000_CTM(0)),
	IP27C_MHZ(500),
	IP27C_MHZ(200),
	CONFIG_FPROM_SETUP,
	SN1_MACH_TYPE,
	CONFIG_FPROM_ENABLE
},

/* 8MB_200_400_267 values (R12KS, 3.7ns, LWR, 030-1616-001, PSC 0x2) */
{
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(3)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(4)	 + \
	 IP27C_R10000_SCCD(2)	 + \
	 IP27C_R10000_SCCT(0xa)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(1)	 + \
	 IP27C_R10000_CTM(0)),
	IP27C_MHZ(400),
	IP27C_MHZ(200),
	CONFIG_FPROM_SETUP,
	SN1_MACH_TYPE,
	CONFIG_FPROM_ENABLE
},

/* 4MB_180_360_240 values (R12KS, 3.7ns, LWR, 030-1627-001, PSC 0x8)
 * (SysAD at 180 MHz (SCD=3, the fastest possible), bedrock core at 200MHz) */
{
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(3)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(3)	 + \
	 IP27C_R10000_SCCD(2)	 + \
	 IP27C_R10000_SCCT(9)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(1)	 + \
	 IP27C_R10000_CTM(0)),
	IP27C_MHZ(360),
	IP27C_MHZ(200),
	CONFIG_FPROM_SETUP,
	SN1_MACH_TYPE,
	CONFIG_FPROM_ENABLE
},

};
#else
extern	config_modifiable_t	ip_config_table[];
#endif /* DEF_IP27_CONFIG_TABLE */

#ifdef IP27_CONFIG_SN00_4MB_100_200_133
#define CONFIG_CPU_MODE	ip_config_table[IP27_CONFIG_SN00_4MB_100_200_133_TABLE].r10k_mode
#define CONFIG_FREQ_CPU	ip_config_table[IP27_CONFIG_SN00_4MB_100_200_133_TABLE].freq_cpu
#define CONFIG_FREQ_HUB	ip_config_table[IP27_CONFIG_SN00_4MB_100_200_133_TABLE].freq_hub
#define CONFIG_FPROM_CYC ip_config_table[IP27_CONFIG_SN00_4MB_100_200_133_TABLE].fprom_cyc
#define CONFIG_MACH_TYPE ip_config_table[IP27_CONFIG_SN00_4MB_100_200_133_TABLE].mach_type
#define CONFIG_FPROM_WR	ip_config_table[IP27_CONFIG_SN00_4MB_100_200_133_TABLE].fprom_wr
#endif /* IP27_CONFIG_SN00_4MB_100_200_133 */

#ifdef IP27_CONFIG_SN1_1MB_200_400_200
#define CONFIG_CPU_MODE	ip_config_table[IP27_CONFIG_SN1_1MB_200_400_200_TABLE].r10k_mode
#define CONFIG_FREQ_CPU	ip_config_table[IP27_CONFIG_SN1_1MB_200_400_200_TABLE].freq_cpu
#define CONFIG_FREQ_HUB	ip_config_table[IP27_CONFIG_SN1_1MB_200_400_200_TABLE].freq_hub
#define CONFIG_FPROM_CYC ip_config_table[IP27_CONFIG_SN1_1MB_200_400_200_TABLE].fprom_cyc
#define CONFIG_MACH_TYPE ip_config_table[IP27_CONFIG_SN1_1MB_200_400_200_TABLE].mach_type
#define CONFIG_FPROM_WR	ip_config_table[IP27_CONFIG_SN1_1MB_200_400_200_TABLE].fprom_wr
#endif /* IP27_CONFIG_SN1_1MB_200_400_200 */

#ifdef IP27_CONFIG_SN1_4MB_200_400_267
#define CONFIG_CPU_MODE	ip_config_table[IP27_CONFIG_SN1_4MB_200_400_267_TABLE].r10k_mode
#define CONFIG_FREQ_CPU	ip_config_table[IP27_CONFIG_SN1_4MB_200_400_267_TABLE].freq_cpu
#define CONFIG_FREQ_HUB	ip_config_table[IP27_CONFIG_SN1_4MB_200_400_267_TABLE].freq_hub
#define CONFIG_FPROM_CYC ip_config_table[IP27_CONFIG_SN1_4MB_200_400_267_TABLE].fprom_cyc
#define CONFIG_MACH_TYPE ip_config_table[IP27_CONFIG_SN1_4MB_200_400_267_TABLE].mach_type
#define CONFIG_FPROM_WR	ip_config_table[IP27_CONFIG_SN1_4MB_200_400_267_TABLE].fprom_wr
#endif /* IP27_CONFIG_SN1_4MB_200_400_267 */

#ifdef IP27_CONFIG_SN1_8MB_200_500_250
#define CONFIG_CPU_MODE	ip_config_table[IP27_CONFIG_SN1_8MB_200_500_250_TABLE].r10k_mode
#define CONFIG_FREQ_CPU	ip_config_table[IP27_CONFIG_SN1_8MB_200_500_250_TABLE].freq_cpu
#define CONFIG_FREQ_HUB	ip_config_table[IP27_CONFIG_SN1_8MB_200_500_250_TABLE].freq_hub
#define CONFIG_FPROM_CYC ip_config_table[IP27_CONFIG_SN1_8MB_200_500_250_TABLE].fprom_cyc
#define CONFIG_MACH_TYPE ip_config_table[IP27_CONFIG_SN1_8MB_200_500_250_TABLE].mach_type
#define CONFIG_FPROM_WR	ip_config_table[IP27_CONFIG_SN1_8MB_200_500_250_TABLE].fprom_wr
#endif /* IP27_CONFIG_SN1_8MB_200_500_250 */

#ifdef IP27_CONFIG_SN1_8MB_200_400_267
#define CONFIG_CPU_MODE	ip_config_table[IP27_CONFIG_SN1_8MB_200_400_267_TABLE].r10k_mode
#define CONFIG_FREQ_CPU	ip_config_table[IP27_CONFIG_SN1_8MB_200_400_267_TABLE].freq_cpu
#define CONFIG_FREQ_HUB	ip_config_table[IP27_CONFIG_SN1_8MB_200_400_267_TABLE].freq_hub
#define CONFIG_FPROM_CYC ip_config_table[IP27_CONFIG_SN1_8MB_200_400_267_TABLE].fprom_cyc
#define CONFIG_MACH_TYPE ip_config_table[IP27_CONFIG_SN1_8MB_200_400_267_TABLE].mach_type
#define CONFIG_FPROM_WR	ip_config_table[IP27_CONFIG_SN1_8MB_200_400_267_TABLE].fprom_wr
#endif /* IP27_CONFIG_SN1_8MB_200_400_267 */

#ifdef IP27_CONFIG_SN1_4MB_180_360_240
#define CONFIG_CPU_MODE	ip_config_table[IP27_CONFIG_SN1_4MB_180_360_240_TABLE].r10k_mode
#define CONFIG_FREQ_CPU	ip_config_table[IP27_CONFIG_SN1_4MB_180_360_240_TABLE].freq_cpu
#define CONFIG_FREQ_HUB	ip_config_table[IP27_CONFIG_SN1_4MB_180_360_240_TABLE].freq_hub
#define CONFIG_FPROM_CYC ip_config_table[IP27_CONFIG_SN1_4MB_180_360_240_TABLE].fprom_cyc
#define CONFIG_MACH_TYPE ip_config_table[IP27_CONFIG_SN1_4MB_180_360_240_TABLE].mach_type
#define CONFIG_FPROM_WR	ip_config_table[IP27_CONFIG_SN1_4MB_180_360_240_TABLE].fprom_wr
#endif /* IP27_CONFIG_SN1_4MB_180_360_240 */

#endif /* _LANGUAGE_C */

#if _LANGUAGE_ASSEMBLY

/* these need to be in here since we need assembly definitions
 * for building hex images (as required by start.s)
 */
#ifdef IP27_CONFIG_SN00_4MB_100_200_133
#ifdef IRIX
/* Set PrcReqMax to 0 to reduce memory problems */
#define	BRINGUP_PRM_VAL	0
#else
#define	BRINGUP_PRM_VAL	3
#endif
#define CONFIG_CPU_MODE \
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(BRINGUP_PRM_VAL)	 + \
	 IP27C_R10000_SCD(3)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(3)	 + \
	 IP27C_R10000_SCCD(2)	 + \
	 IP27C_R10000_SCCT(9)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(0)	 + \
	 IP27C_R10000_CTM(0))
#define CONFIG_FREQ_CPU IP27C_MHZ(200)
#define CONFIG_FREQ_HUB IP27C_MHZ(200)
#define CONFIG_FPROM_CYC CONFIG_FPROM_SETUP
#define CONFIG_MACH_TYPE SN1_MACH_TYPE
#define CONFIG_FPROM_WR CONFIG_FPROM_ENABLE
#endif /* IP27_CONFIG_SN00_4MB_100_200_133 */

#ifdef IP27_CONFIG_SN1_1MB_200_400_200
#define CONFIG_CPU_MODE \
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(3)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(1)	 + \
	 IP27C_R10000_SCCD(3)	 + \
	 IP27C_R10000_SCCT(9)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(1)	 + \
	 IP27C_R10000_CTM(0))
#define CONFIG_FREQ_CPU IP27C_MHZ(400)
#define CONFIG_FREQ_HUB IP27C_MHZ(200)
#define CONFIG_FPROM_CYC CONFIG_FPROM_SETUP
#define CONFIG_MACH_TYPE SN1_MACH_TYPE
#define CONFIG_FPROM_WR CONFIG_FPROM_ENABLE
#endif /* IP27_CONFIG_SN1_1MB_200_400_200 */

#ifdef IP27_CONFIG_SN1_4MB_200_400_267
#define CONFIG_CPU_MODE \
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(3)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(3)	 + \
	 IP27C_R10000_SCCD(2)	 + \
	 IP27C_R10000_SCCT(0xa)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(1)	 + \
	 IP27C_R10000_CTM(0))
#define CONFIG_FREQ_CPU IP27C_MHZ(400)
#define CONFIG_FREQ_HUB IP27C_MHZ(200)
#define CONFIG_FPROM_CYC CONFIG_FPROM_SETUP
#define CONFIG_MACH_TYPE SN1_MACH_TYPE
#define CONFIG_FPROM_WR CONFIG_FPROM_ENABLE
#endif /* IP27_CONFIG_SN1_4MB_200_400_267 */

#ifdef IP27_CONFIG_SN1_8MB_200_500_250
#define CONFIG_CPU_MODE \
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(4)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(4)	 + \
	 IP27C_R10000_SCCD(3)	 + \
	 IP27C_R10000_SCCT(0xa)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(1)	 + \
	 IP27C_R10000_CTM(0))
#define CONFIG_FREQ_CPU IP27C_MHZ(500)
#define CONFIG_FREQ_HUB IP27C_MHZ(200)
#define CONFIG_FPROM_CYC CONFIG_FPROM_SETUP
#define CONFIG_MACH_TYPE SN1_MACH_TYPE
#define CONFIG_FPROM_WR CONFIG_FPROM_ENABLE
#endif /* IP27_CONFIG_SN1_8MB_200_500_250 */

#ifdef IP27_CONFIG_SN1_8MB_200_400_267
#define CONFIG_CPU_MODE \
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(3)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(4)	 + \
	 IP27C_R10000_SCCD(2)	 + \
	 IP27C_R10000_SCCT(0xa)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(1)	 + \
	 IP27C_R10000_CTM(0))
#define CONFIG_FREQ_CPU IP27C_MHZ(400)
#define CONFIG_FREQ_HUB IP27C_MHZ(200)
#define CONFIG_FPROM_CYC CONFIG_FPROM_SETUP
#define CONFIG_MACH_TYPE SN1_MACH_TYPE
#define CONFIG_FPROM_WR CONFIG_FPROM_ENABLE
#endif /* IP27_CONFIG_SN1_8MB_200_400_267 */

#ifdef IP27_CONFIG_SN1_4MB_180_360_240
#define CONFIG_CPU_MODE \
	(IP27C_R10000_KSEG0CA(5) + \
	 IP27C_R10000_DEVNUM(0)	 + \
	 IP27C_R10000_CPRT(0)	 + \
	 IP27C_R10000_PER(0)	 + \
	 IP27C_R10000_PRM(3)	 + \
	 IP27C_R10000_SCD(3)	 + \
	 IP27C_R10000_SCBS(1)	 + \
	 IP27C_R10000_SCCE(0)	 + \
	 IP27C_R10000_ME(1)	 + \
	 IP27C_R10000_SCS(3)	 + \
	 IP27C_R10000_SCCD(2)	 + \
	 IP27C_R10000_SCCT(9)	 + \
	 IP27C_R10000_ODSC(0)	 + \
	 IP27C_R10000_ODSYS(1)	 + \
	 IP27C_R10000_CTM(0))
#define CONFIG_FREQ_CPU IP27C_MHZ(360)
#define CONFIG_FREQ_HUB IP27C_MHZ(200)
#define CONFIG_FPROM_CYC CONFIG_FPROM_SETUP
#define CONFIG_MACH_TYPE SN1_MACH_TYPE
#define CONFIG_FPROM_WR CONFIG_FPROM_ENABLE
#endif /* IP27_CONFIG_SN1_4MB_180_360_240 */

#endif /* _LANGUAGE_C */

#endif /* _ASM_SN_SN1_IP27CONFIG_H */
