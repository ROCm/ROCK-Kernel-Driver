/*
 * A collection of structures, addresses, and values associated with
 * Speech Design SPD8xxTS boards.
 *
 * Copyright (c) 2000 Wolfgang Denk (wd@denx.de)
 */
#ifndef __MACH_SPD8xx_DEFS
#define __MACH_SPD8xx_DEFS

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

#define	SPD_IMMR_BASE	0xFFF00000	/* phys. addr of IMMR			*/
#define	SPD_IMAP_SIZE	(64 * 1024)	/* size of mapped area			*/

#define	IMAP_ADDR	SPD_IMMR_BASE	/* physical base address of IMMR area	*/
#define IMAP_SIZE	SPD_IMAP_SIZE	/* mapped size of IMMR area		*/

#define PCMCIA_MEM_ADDR	((uint)0xFE100000)
#define PCMCIA_MEM_SIZE	((uint)(64 * 1024))

#define IDE0_INTERRUPT	10		/* = IRQ5				*/
#define	IDE1_INTERRUPT	12		/* = IRQ6				*/
#define	CPM_INTERRUPT	13		/* = SIU_LEVEL6 (was: SIU_LEVEL2)	*/

#define	MAX_HWIFS	2	/* overwrite default in include/asm-ppc/ide.h	*/

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

/*
 * Definitions for IDE1 Interface
 */
#define IDE1_BASE_OFFSET		0x0C00	/* Offset in PCMCIA memory	*/
#define IDE1_DATA_REG_OFFSET		0x0000
#define IDE1_ERROR_REG_OFFSET		0x0081
#define IDE1_NSECTOR_REG_OFFSET		0x0082
#define IDE1_SECTOR_REG_OFFSET		0x0083
#define IDE1_LCYL_REG_OFFSET		0x0084
#define IDE1_HCYL_REG_OFFSET		0x0085
#define IDE1_SELECT_REG_OFFSET		0x0086
#define IDE1_STATUS_REG_OFFSET		0x0087
#define IDE1_CONTROL_REG_OFFSET		0x0106
#define IDE1_IRQ_REG_OFFSET		0x000A	/* not used			*/

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

/* Generic 8xx type
*/
#define _MACH_8xx (_MACH_spd8xx)

#endif	/* __MACH_SPD8xx_DEFS */
