
/*
 * A collection of structures, addresses, and values associated with
 * the TQ Systems TQM860 modules.  This was originally created for the
 * MBX860, and probably needs revisions for other boards (like the 821).
 * When this file gets out of control, we can split it up into more
 * meaningful pieces.
 *
 * Based on mbx.h, Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * Copyright (c) 1999 Wolfgang Denk (wd@denx.de)
 */
#ifdef __KERNEL__
#ifndef __MACH_TQM860_DEFS
#define __MACH_TQM860_DEFS

/* A Board Information structure that is given to a program when
 * EPPC-Bug starts it up.
 */
typedef struct bd_info {
	 unsigned long	bi_memstart;	/* start of  DRAM memory */
	 unsigned long	bi_memsize;	/* size  of  DRAM memory in bytes */
	 unsigned long	bi_flashstart;	/* start of FLASH memory */
	 unsigned long	bi_flashsize;	/* size  of FLASH memory */
	 unsigned long	bi_flashoffset;	/* reserved area for startup monitor */
	 unsigned long	bi_sramstart;	/* start of  SRAM memory */
	 unsigned long	bi_sramsize;	/* size  of  SRAM memory */
	 unsigned long	bi_immr_base;	/* base of IMMR register */
	 unsigned long	bi_bootflags;	/* boot / reboot flag (for LynxOS) */
	 unsigned long	bi_ip_addr;	/* IP Address */
	 unsigned char	bi_enetaddr[6];	/* Ethernet adress */
	 unsigned char	bi_reserved[2];	/* -- just for alignment -- */
	 unsigned long	bi_putchar;	/* Addr of monitor putchar() to Console	*/
	 unsigned long	bi_intfreq;	/* Internal Freq, in MHz */
	 unsigned long	bi_busfreq;	/* Bus Freq, in MHz */
	 unsigned long	bi_baudrate;	/* Console Baudrate */
} bd_t;

/* Configuration options for TQ Systems TQM860 mini module
 */

#define	TQM_RESET_ADDR	0x40000100	/* Monitor Reset Entry */

#define	TQM_IMMR_BASE	0xFFF00000	/* phys. addr of IMMR */
#define TQM_IMAP_SIZE	(64 * 1024)	/* size of mapped area */

#define	TQM_CLOCKRATE	50		/* 50 MHz Clock */
#define	TQM_BAUDRATE	115200		/* Console baud rate */
#define	TQM_IP_ADDR	0x0A000063	/* IP addr: 10.0.0.99 */

#define	TQM_SERVER_IP	"10.0.0.3"	/* NFS server IP addr */
#define	TQM_SERVER_DIR	"/LinuxPPC"	/* NFS exported root */

#define	IMAP_ADDR	TQM_IMMR_BASE	/* physical base address of IMMR area */
#define IMAP_SIZE	TQM_IMAP_SIZE	/* mapped size of IMMR area */

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

/* Generic 8xx type
*/
#define _MACH_8xx (_MACH_tqm860)

#endif	/* __MACH_TQM860_DEFS */

#endif /* __KERNEL__ */
