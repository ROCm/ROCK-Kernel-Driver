/*
 * A collection of structures, addresses, and values associated with
 * Speech Design Integrated Voicemail Systems (IVMS8) boards.
 *
 * Copyright (c) 2000 Wolfgang Denk (wd@denx.de)
 */
#ifndef __MACH_IVMS8_DEFS
#define __MACH_IVMS8_DEFS

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

#define	IVMS_IMMR_BASE	0xFFF00000	/* phys. addr of IMMR			*/
#define	IVMS_IMAP_SIZE	(64 * 1024)	/* size of mapped area			*/

#define	IMAP_ADDR	IVMS_IMMR_BASE	/* physical base address of IMMR area	*/
#define IMAP_SIZE	IVMS_IMAP_SIZE	/* mapped size of IMMR area		*/

#define PCMCIA_MEM_ADDR	((uint)0xFE100000)
#define PCMCIA_MEM_SIZE	((uint)(64 * 1024))

#define	FEC_INTERRUPT	 9		/* = SIU_LEVEL4				*/
#define IDE0_INTERRUPT	10		/* = IRQ5				*/
#define	CPM_INTERRUPT	11		/* = SIU_LEVEL5 (was: SIU_LEVEL2)	*/
#define PHY_INTERRUPT	12		/* = IRQ6				*/

#define	MAX_HWIFS	1	/* overwrite default in include/asm-ppc/ide.h	*/

/*
 * Definitions for IDE0 Interface
 */
#define IDE0_BASE_OFFSET		0x0000	/* Offset in PCMCIA memory	*/
#define IDE0_DATA_REG_OFFSET		0x0000
#define IDE0_ERROR_REG_OFFSET		0x0081
#define IDE0_NSECTOR_REG_OFFSET		0x0082
#define IDE0_SECTOR_REG_OFFSET		0x0083
#define IDE0_LCYL_REG_OFFSET		0x0084
#define IDE0_HCYL_REG_OFFSET		0x0085
#define IDE0_SELECT_REG_OFFSET		0x0086
#define IDE0_STATUS_REG_OFFSET		0x0087
#define IDE0_CONTROL_REG_OFFSET		0x0106
#define IDE0_IRQ_REG_OFFSET		0x000A	/* not used			*/

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

/* Generic 8xx type
*/
#define _MACH_8xx (_MACH_ivms8)

#endif	/* __MACH_IVMS8_DEFS */

