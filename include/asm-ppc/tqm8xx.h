/*
 * A collection of structures, addresses, and values associated with
 * the TQ Systems TQM8xx(L) modules.  This was originally created for the
 * MBX860, and probably needs revisions for other boards (like the 821).
 * When this file gets out of control, we can split it up into more
 * meaningful pieces.
 *
 * Based on mbx.h, Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * Copyright (c) 1999,2000 Wolfgang Denk (wd@denx.de)
 */
#ifndef __MACH_TQM8xx_DEFS
#define __MACH_TQM8xx_DEFS

#include <linux/config.h>

#ifndef __ASSEMBLY__

typedef	void (interrupt_handler_t)(void *);

typedef struct serial_io {
	int	(*getc)(void);
	int	(*tstc)(void);
	void	(*putc)(const char c);
	void	(*printf)(const char *fmt, ...);
} serial_io_t;

typedef struct intr_util {
	void	(*install_hdlr)(int, interrupt_handler_t *, void *);
	void	(*free_hdlr)(int);
} intr_util_t;


/* A Board Information structure that is given to a program when
 * ppcboot starts it up.
 */
typedef struct bd_info {
	 unsigned long	bi_memstart;	/* start of  DRAM memory		*/
	 unsigned long	bi_memsize;	/* size	 of  DRAM memory in bytes	*/
	 unsigned long	bi_flashstart;	/* start of FLASH memory		*/
	 unsigned long	bi_flashsize;	/* size	 of FLASH memory		*/
	 unsigned long	bi_flashoffset; /* reserved area for startup monitor	*/
	 unsigned long	bi_sramstart;	/* start of  SRAM memory		*/
	 unsigned long	bi_sramsize;	/* size	 of  SRAM memory		*/
	 unsigned long	bi_immr_base;	/* base of IMMR register		*/
	 unsigned long	bi_bootflags;	/* boot / reboot flag (for LynxOS)	*/
	 unsigned long	bi_ip_addr;	/* IP Address				*/
	 unsigned char	bi_enetaddr[6]; /* Ethernet adress			*/
	 unsigned char	bi_reserved[2]; /* -- just for alignment --		*/
	 unsigned long	bi_intfreq;	/* Internal Freq, in MHz		*/
	 unsigned long	bi_busfreq;	/* Bus Freq, in MHz			*/
	 unsigned long	bi_baudrate;	/* Console Baudrate			*/
	 serial_io_t	bi_serial_io;	/* Addr of monitor fnc for Console I/O	*/
	 intr_util_t	bi_interrupt;	/* Addr of monitor fnc for Interrupts	*/
} bd_t;

#endif /* __ASSEMBLY__ */

#define	TQM_IMMR_BASE	0xFFF00000	/* phys. addr of IMMR			*/
#define	TQM_IMAP_SIZE	(64 * 1024)	/* size of mapped area			*/

#define	IMAP_ADDR	TQM_IMMR_BASE	/* physical base address of IMMR area	*/
#define IMAP_SIZE	TQM_IMAP_SIZE	/* mapped size of IMMR area		*/

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

/* Generic 8xx type
*/
#if defined(CONFIG_TQM8xxL)
#define _MACH_8xx (_MACH_tqm8xxL)
#endif
#if defined(CONFIG_TQM860)
#define _MACH_8xx (_MACH_tqm860)
#endif

#endif	/* __MACH_TQM8xx_DEFS */
