/*
*
*   (c) 1999 by Computone Corporation
*
********************************************************************************
*
*   PACKAGE:     Linux tty Device Driver for IntelliPort family of multiport
*                serial I/O controllers.
*
*   DESCRIPTION: Mainline code for the device driver
*
*******************************************************************************/
// ToDo:
//
// Done:
//
// 1.2.9
// Four box EX was barfing on >128k kmalloc, made structure smaller by
// reducing output buffer size
//
// 1.2.8
// Device file system support (MHW)
//
// 1.2.7 
// Fixed
// Reload of ip2 without unloading ip2main hangs system on cat of /proc/modules
//
// 1.2.6
//Fixes DCD problems
//	DCD was not reported when CLOCAL was set on call to TIOCMGET
//
//Enhancements:
//	TIOCMGET requests and waits for status return
//	No DSS interrupts enabled except for DCD when needed
//
// For internal use only
//
//#define IP2DEBUG_INIT
//#define IP2DEBUG_OPEN
//#define IP2DEBUG_WRITE
//#define IP2DEBUG_READ
//#define IP2DEBUG_IOCTL
//#define IP2DEBUG_IPL

//#define IP2DEBUG_TRACE
//#define DEBUG_FIFO

/************/
/* Includes */
/************/
#include <linux/config.h>
// Uncomment the following if you want it compiled with modversions
#ifdef MODULE
#	if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#		define MODVERSIONS
#	endif
#	ifdef MODVERSIONS
#		include <linux/modversions.h>
#	endif
#endif

#include <linux/version.h>

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#ifdef	CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/wait.h>

#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/termios.h>
#include <linux/tty_driver.h>
#include <linux/serial.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>

#include <linux/cdk.h>
#include <linux/comstats.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(ver,rel,seq) (((ver)<<16) | ((rel)<<8) | (seq))
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
#	include <linux/vmalloc.h>
#	include <linux/init.h>
#	include <asm/serial.h>
#else
#	include <linux/bios32.h>
#endif

// These VERSION switches maybe inexact because I simply don't know
// when the various features appeared in the 2.1.XX kernels.
// They are good enough for 2.0 vs 2.2 and if you are fooling with
// the 2.1.XX stuff then it would be trivial for you to fix.
// Most of these macros were stolen from some other drivers
// so blame them.

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,4)
#	include <asm/segment.h>
#	define GET_USER(error,value,addr) error = get_user(value,addr)
#	define COPY_FROM_USER(error,dest,src,size) error = copy_from_user(dest,src,size) ? -EFAULT : 0
#	define PUT_USER(error,value,addr) error = put_user(value,addr)
#	define COPY_TO_USER(error,dest,src,size) error = copy_to_user(dest,src,size) ? -EFAULT : 0

#	if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,5)
#		include <asm/uaccess.h>
#		define		pcibios_strerror(status)	\
					printk( KERN_ERR "IP2: PCI error 0x%x \n", status );
#	endif

#else  /* 2.0.x and 2.1.x before 2.1.4 */

#	define		proc_register_dynamic(a,b) proc_register(a,b) 

#	define GET_USER(error,value,addr)					  \
	do {									  \
		error = verify_area (VERIFY_READ, (void *) addr, sizeof (value)); \
		if (error == 0)							  \
			value = get_user(addr);					  \
	} while (0)

#	define COPY_FROM_USER(error,dest,src,size)				  \
	do {									  \
		error = verify_area (VERIFY_READ, (void *) src, size);		  \
		if (error == 0)							  \
			memcpy_fromfs (dest, src, size);			  \
	} while (0)

#	define PUT_USER(error,value,addr)					   \
	do {									   \
		error = verify_area (VERIFY_WRITE, (void *) addr, sizeof (value)); \
		if (error == 0)							   \
			put_user (value, addr);					   \
	} while (0)

#	define COPY_TO_USER(error,dest,src,size)				  \
	do {									  \
		error = verify_area (VERIFY_WRITE, (void *) dest, size);		  \
		if (error == 0)							  \
			memcpy_tofs (dest, src, size);				  \
	} while (0)

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
#define __init
#define __initfunc(a) a
#define __initdata
#define ioremap(a,b) vremap((a),(b))
#define iounmap(a) vfree((a))
#define SERIAL_TYPE_NORMAL	1
#define SERIAL_TYPE_CALLOUT	2
#define schedule_timeout(a){current->timeout = jiffies + (a); schedule();}
#define signal_pending(a) ((a)->signal & ~(a)->blocked)
#define in_interrupt()	intr_count
#endif

#include "./ip2/ip2types.h"
#include "./ip2/ip2trace.h"
#include "./ip2/ip2ioctl.h"
#include "./ip2/ip2.h"
#include "./ip2/i2ellis.h"
#include "./ip2/i2lib.h"

/*****************
 * /proc/ip2mem  *
 *****************/

#include <linux/proc_fs.h>

static int ip2_read_procmem(char *, char **, off_t, int);
int ip2_read_proc(char *, char **, off_t, int, int *, void * );

/********************/
/* Type Definitions */
/********************/

/*************/
/* Constants */
/*************/

/* String constants to identify ourselves */
static char *pcName    = "Computone IntelliPort Plus multiport driver";
static char *pcVersion = "1.2.9";

/* String constants for port names */
static char *pcDriver_name   = "ip2";
#ifdef	CONFIG_DEVFS_FS
static char *pcTty    		 = "ttf/%d";
static char *pcCallout		 = "cuf/%d";
#else
static char *pcTty    		 = "ttyF";
static char *pcCallout		 = "cuf";
#endif
static char *pcIpl    		 = "ip2ipl";

/* Serial subtype definitions */
#define SERIAL_TYPE_NORMAL    1
#define SERIAL_TYPE_CALLOUT   2

// cheezy kludge or genius - you decide?
int ip2_loadmain(int *, int *, unsigned char *, int);
static unsigned char *Fip_firmware;
static int Fip_firmware_size;

/***********************/
/* Function Prototypes */
/***********************/

/* Global module entry functions */
#ifdef MODULE
int init_module(void);
void cleanup_module(void);
#endif

int old_ip2_init(void);

/* Private (static) functions */
static int  ip2_open(PTTY, struct file *);
static void ip2_close(PTTY, struct file *);
static int  ip2_write(PTTY, int, const unsigned char *, int);
static void ip2_putchar(PTTY, unsigned char);
static void ip2_flush_chars(PTTY);
static int  ip2_write_room(PTTY);
static int  ip2_chars_in_buf(PTTY);
static void ip2_flush_buffer(PTTY);
static int  ip2_ioctl(PTTY, struct file *, UINT, ULONG);
static void ip2_set_termios(PTTY, struct termios *);
static void ip2_set_line_discipline(PTTY);
static void ip2_throttle(PTTY);
static void ip2_unthrottle(PTTY);
static void ip2_stop(PTTY);
static void ip2_start(PTTY);
static void ip2_hangup(PTTY);

static void set_irq(int, int);
static void ip2_interrupt(int irq, void *dev_id, struct pt_regs * regs);
static void ip2_poll(unsigned long arg);
static inline void service_all_boards(void);
static inline void do_input(i2ChanStrPtr pCh);
static inline void do_status(i2ChanStrPtr pCh);

static void ip2_wait_until_sent(PTTY,int);

static void set_params (i2ChanStrPtr, struct termios *);
static int set_modem_info(i2ChanStrPtr, unsigned int, unsigned int *);
static int get_serial_info(i2ChanStrPtr, struct serial_struct *);
static int set_serial_info(i2ChanStrPtr, struct serial_struct *);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
static int     ip2_ipl_read(struct inode *, char *, size_t , loff_t *);
#else
static ssize_t ip2_ipl_read(struct file *, char *, size_t, loff_t *) ;
#endif
static ssize_t ip2_ipl_write(struct file *, const char *, size_t, loff_t *);
static int ip2_ipl_ioctl(struct inode *, struct file *, UINT, ULONG);
static int ip2_ipl_open(struct inode *, struct file *);

void ip2trace(unsigned short,unsigned char,unsigned char,unsigned long,...);
static int DumpTraceBuffer(char *, int);
static int DumpFifoBuffer( char *, int);

static void ip2_init_board(int);
static unsigned short find_eisa_board(int);

/***************/
/* Static Data */
/***************/

static struct tty_driver ip2_tty_driver;
static struct tty_driver ip2_callout_driver;

static int ref_count;

/* Here, then is a table of board pointers which the interrupt routine should
 * scan through to determine who it must service.
 */
static unsigned short i2nBoards; // Number of boards here

static i2eBordStrPtr i2BoardPtrTable[IP2_MAX_BOARDS];

static i2ChanStrPtr  DevTable[IP2_MAX_PORTS];
//DevTableMem just used to save addresses for kfree
static void  *DevTableMem[IP2_MAX_BOARDS];

static struct tty_struct * TtyTable[IP2_MAX_PORTS];
static struct termios    * Termios[IP2_MAX_PORTS];
static struct termios    * TermiosLocked[IP2_MAX_PORTS];

/* This is the driver descriptor for the ip2ipl device, which is used to
 * download the loadware to the boards.
 */
static struct file_operations ip2_ipl = {
	owner:		THIS_MODULE,
	read:		ip2_ipl_read,
	write:		ip2_ipl_write,
	ioctl:		ip2_ipl_ioctl,
	open:		ip2_ipl_open,
}; 

static long irq_counter;
static long bh_counter;

// Use immediate queue to service interrupts
//#define USE_IQI	// PCI&2.2 needs work
//#define USE_IQ	// PCI&2.2 needs work

/* The timer_list entry for our poll routine. If interrupt operation is not
 * selected, the board is serviced periodically to see if anything needs doing.
 */
#define  POLL_TIMEOUT   (jiffies + 1)
static struct timer_list PollTimer = { function: ip2_poll };
static char  TimerOn;

#ifdef IP2DEBUG_TRACE
/* Trace (debug) buffer data */
#define TRACEMAX  1000
static unsigned long tracebuf[TRACEMAX];
static int tracestuff;
static int tracestrip;
static int tracewrap;
#endif

/**********/
/* Macros */
/**********/

#if defined(MODULE) && defined(IP2DEBUG_OPEN)
#define DBG_CNT(s) printk(KERN_DEBUG "(%s): [%x] refc=%d, ttyc=%d, modc=%x -> %s\n", \
		    kdevname(tty->device),(pCh->flags),ref_count, \
		    tty->count,/*GET_USE_COUNT(module)*/0,s)
#else
#define DBG_CNT(s)
#endif

#define MIN(a,b)	( ( (a) < (b) ) ? (a) : (b) )
#define MAX(a,b)	( ( (a) > (b) ) ? (a) : (b) )

/********/
/* Code */
/********/

#include "./ip2/i2ellis.c"    /* Extremely low-level interface services */
#include "./ip2/i2cmd.c"      /* Standard loadware command definitions */
#include "./ip2/i2lib.c"      /* High level interface services */

/* Configuration area for modprobe */
#ifdef MODULE
#	if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
		MODULE_AUTHOR("Doug McNash");
		MODULE_DESCRIPTION("Computone IntelliPort Plus Driver");
#	endif	/* LINUX_VERSION */
#endif	/* MODULE */

static int poll_only;

static int Eisa_irq;
static int Eisa_slot;

static int iindx;
static char rirqs[IP2_MAX_BOARDS];
static int Valid_Irqs[] = { 3, 4, 5, 7, 10, 11, 12, 15, 0};

/******************************************************************************/
/* Initialisation Section                                                     */
/******************************************************************************/
int 
ip2_loadmain(int *iop, int *irqp, unsigned char *firmware, int firmsize) 
{
	int i;
	/* process command line arguments to modprobe or insmod i.e. iop & irqp */
	/* otherwise ip2config is initialized by what's in ip2/ip2.h */
	/* command line trumps initialization in ip2.h */
	/* first two args are null if builtin to kernel */
	if ((irqp != NULL) || (iop != NULL)) {
		for ( i = 0; i < IP2_MAX_BOARDS; ++i ) {
			if (irqp && irqp[i]) {
				ip2config.irq[i] = irqp[i];
			}
			if (iop && iop[i]) {
				ip2config.addr[i] = iop[i];
			}
		}
	}
	Fip_firmware = firmware;
	Fip_firmware_size = firmsize;
	return old_ip2_init();
}

// Some functions to keep track of what irq's we have

static int __init
is_valid_irq(int irq)
{
	int *i = Valid_Irqs;
	
	while ((*i != 0) && (*i != irq)) {
		i++;
	}
	return (*i);
}

static void __init
mark_requested_irq( char irq )
{
	rirqs[iindx++] = irq;
}

static int __init
clear_requested_irq( char irq )
{
	int i;
	for ( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		if (rirqs[i] == irq) {
			rirqs[i] = 0;
			return 1;
		}
	}
	return 0;
}

static int __init
have_requested_irq( char irq )
{
	// array init to zeros so 0 irq will not be requested as a side effect
	int i;
	for ( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		if (rirqs[i] == irq)
			return 1;
	}
	return 0;
}

/******************************************************************************/
/* Function:   init_module()                                                  */
/* Parameters: None                                                           */
/* Returns:    Success (0)                                                    */
/*                                                                            */
/* Description:                                                               */
/* This is a required entry point for an installable module. It simply calls  */
/* the driver initialisation function and returns what it returns.            */
/******************************************************************************/
#ifdef MODULE
int
init_module(void)
{
#ifdef IP2DEBUG_INIT
	printk (KERN_DEBUG "Loading module ...\n" );
#endif
    //was	return old_ip2_init();
    return 0;
}
#endif /* MODULE */

/******************************************************************************/
/* Function:   cleanup_module()                                               */
/* Parameters: None                                                           */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/* This is a required entry point for an installable module. It has to return */
/* the device and the driver to a passive state. It should not be necessary   */
/* to reset the board fully, especially as the loadware is downloaded         */
/* externally rather than in the driver. We just want to disable the board    */
/* and clear the loadware to a reset state. To allow this there has to be a   */
/* way to detect whether the board has the loadware running at init time to   */
/* handle subsequent installations of the driver. All memory allocated by the */
/* driver should be returned since it may be unloaded from memory.            */
/******************************************************************************/
#ifdef MODULE
void
cleanup_module(void)
{
	int err;
	int i;

#ifdef IP2DEBUG_INIT
	printk (KERN_DEBUG "Unloading %s: version %s\n", pcName, pcVersion );
#endif
	/* Stop poll timer if we had one. */
	if ( TimerOn ) {
		del_timer ( &PollTimer );
		TimerOn = 0;
	}

	/* Reset the boards we have. */
	for( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		if ( i2BoardPtrTable[i] ) {
			iiReset( i2BoardPtrTable[i] );
		}
	}

	/* The following is done at most once, if any boards were installed. */
	for ( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		if ( i2BoardPtrTable[i] ) {
			iiResetDelay( i2BoardPtrTable[i] );
			/* free io addresses and Tibet */
			release_region( ip2config.addr[i], 8 );
#ifdef	CONFIG_DEVFS_FS
			devfs_unregister (i2BoardPtrTable[i]->devfs_ipl_handle);
			devfs_unregister (i2BoardPtrTable[i]->devfs_stat_handle);
#endif
		}
		/* Disable and remove interrupt handler. */
		if ( (ip2config.irq[i] > 0) && have_requested_irq(ip2config.irq[i]) ) {	
			free_irq ( ip2config.irq[i], (void *)&pcName);
			clear_requested_irq( ip2config.irq[i]);
		}
	}
	if ( ( err = tty_unregister_driver ( &ip2_tty_driver ) ) ) {
		printk(KERN_ERR "IP2: failed to unregister tty driver (%d)\n", err);
	}
	if ( ( err = tty_unregister_driver ( &ip2_callout_driver ) ) ) {
		printk(KERN_ERR "IP2: failed to unregister callout driver (%d)\n", err);
	}
#ifdef	CONFIG_DEVFS_FS
	if ( ( err = devfs_unregister_chrdev ( IP2_IPL_MAJOR, pcIpl ) ) )
#else
	if ( ( err = unregister_chrdev ( IP2_IPL_MAJOR, pcIpl ) ) )
#endif
	{
		printk(KERN_ERR "IP2: failed to unregister IPL driver (%d)\n", err);
	}
	remove_proc_entry("ip2mem", &proc_root);

	// free memory
	for (i = 0; i < IP2_MAX_BOARDS; i++) {
		void *pB;
		if ((pB = i2BoardPtrTable[i]) != 0 ) {
			kfree ( pB );
			i2BoardPtrTable[i] = NULL;
		}
		if ((DevTableMem[i]) != NULL ) {
			kfree ( DevTableMem[i]  );
			DevTableMem[i] = NULL;
		}
	}

	/* Cleanup the iiEllis subsystem. */
	iiEllisCleanup();
#ifdef IP2DEBUG_INIT
	printk (KERN_DEBUG "IP2 Unloaded\n" );
#endif
}
#endif /* MODULE */

/******************************************************************************/
/* Function:   old_ip2_init()                                                 */
/* Parameters: irq, io from command line of insmod et. al.                    */
/* Returns:    Success (0)                                                    */
/*                                                                            */
/* Description:                                                               */
/* This was the required entry point for all drivers (now in ip2.c)           */
/* It performs all                                                            */
/* initialisation of the devices and driver structures, and registers itself  */
/* with the relevant kernel modules.                                          */
/******************************************************************************/
/* SA_INTERRUPT- if set blocks all interrupts else only this line */
/* SA_SHIRQ    - for shared irq PCI or maybe EISA only */
/* SA_RANDOM   - can be source for cert. random number generators */
#define IP2_SA_FLAGS	0

int __init
old_ip2_init(void)
{
#ifdef	CONFIG_DEVFS_FS
	static devfs_handle_t devfs_handle;
	int j, box;
#endif
	int i;
	int err;
	int status = 0;
	static int loaded;
	i2eBordStrPtr pB = NULL;
	int rc = -1;

#ifdef IP2DEBUG_TRACE
	ip2trace (ITRC_NO_PORT, ITRC_INIT, ITRC_ENTER, 0 );
#endif

	/* Announce our presence */
	printk( KERN_INFO "%s version %s\n", pcName, pcVersion );

	// ip2 can be unloaded and reloaded for no good reason
	// we can't let that happen here or bad things happen
	// second load hoses board but not system - fixme later
	if (loaded) {
		printk( KERN_INFO "Still loaded\n" );
		return 0;
	}
	loaded++;

	/* if all irq config is zero we shall poll_only */
	for ( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		poll_only |= ip2config.irq[i];
	}
	poll_only = !poll_only;

	/* Initialise the iiEllis subsystem. */
	iiEllisInit();

	/* Initialize arrays. */
	memset( i2BoardPtrTable, 0, sizeof i2BoardPtrTable );
	memset( DevTable, 0, sizeof DevTable );
	memset( TtyTable, 0, sizeof TtyTable );
	memset( Termios, 0, sizeof Termios );
	memset( TermiosLocked, 0, sizeof TermiosLocked );

	/* Initialise all the boards we can find (up to the maximum). */
	for ( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		switch ( ip2config.addr[i] ) { 
		case 0:	/* skip this slot even if card is present */
			break;
		default: /* ISA */
		   /* ISA address must be specified */
			if ( (ip2config.addr[i] < 0x100) || (ip2config.addr[i] > 0x3f8) ) {
				printk ( KERN_ERR "IP2: Bad ISA board %d address %x\n",
							 i, ip2config.addr[i] );
				ip2config.addr[i] = 0;
			} else {
				ip2config.type[i] = ISA;

				/* Check for valid irq argument, set for polling if invalid */
				if (ip2config.irq[i] && !is_valid_irq(ip2config.irq[i])) {
					printk(KERN_ERR "IP2: Bad IRQ(%d) specified\n",ip2config.irq[i]);
					ip2config.irq[i] = 0;// 0 is polling and is valid in that sense
				}
			}
			break;
		case PCI:
#ifdef CONFIG_PCI
#if (LINUX_VERSION_CODE < 0x020163) /* 2.1.99 */
			if (pcibios_present()) {
				unsigned char pci_bus, pci_devfn;
				int Pci_index = 0;
				status = pcibios_find_device(PCI_VENDOR_ID_COMPUTONE,
							  PCI_DEVICE_ID_COMPUTONE_IP2EX, Pci_index,
							  &pci_bus, &pci_devfn);
				if (status == 0) {
					unsigned int addr;
					unsigned char pci_irq;

					ip2config.type[i] = PCI;
					/* 
					 * Update Pci_index, so that the next time we go
					 * searching for a PCI board we find a different
					 * one.
					 */
					++Pci_index;

					pcibios_read_config_dword(pci_bus, pci_devfn,
								  PCI_BASE_ADDRESS_1, &addr);
					if ( addr & 1 ) {
						ip2config.addr[i]=(USHORT)(addr&0xfffe);
					} else {
						printk( KERN_ERR "IP2: PCI I/O address error\n");
					}
					pcibios_read_config_byte(pci_bus, pci_devfn,
								  PCI_INTERRUPT_LINE, &pci_irq);

					if (!is_valid_irq(pci_irq)) {
						printk( KERN_ERR "IP2: Bad PCI BIOS IRQ(%d)\n",pci_irq);
						pci_irq = 0;
					}
					ip2config.irq[i] = pci_irq;
				} else {	// ann error
					ip2config.addr[i] = 0;
					if (status == PCIBIOS_DEVICE_NOT_FOUND) {
						printk( KERN_ERR "IP2: PCI board %d not found\n", i );
					} else {
						pcibios_strerror(status);
					}
				} 
			} 
#else /* LINUX_VERSION_CODE > 2.1.99 */
			if (pci_present()) {
				struct pci_dev *pci_dev_i = NULL;
				pci_dev_i = pci_find_device(PCI_VENDOR_ID_COMPUTONE,
							  PCI_DEVICE_ID_COMPUTONE_IP2EX, pci_dev_i);
				if (pci_dev_i != NULL) {
					unsigned int addr;
					unsigned char pci_irq;

					ip2config.type[i] = PCI;
					status =
					pci_read_config_dword(pci_dev_i, PCI_BASE_ADDRESS_1, &addr);
					if ( addr & 1 ) {
						ip2config.addr[i]=(USHORT)(addr&0xfffe);
					} else {
						printk( KERN_ERR "IP2: PCI I/O address error\n");
					}
					status =
					pci_read_config_byte(pci_dev_i, PCI_INTERRUPT_LINE, &pci_irq);

					if (!is_valid_irq(pci_irq)) {
						printk( KERN_ERR "IP2: Bad PCI BIOS IRQ(%d)\n",pci_irq);
						pci_irq = 0;
					}
					ip2config.irq[i] = pci_irq;
				} else {	// ann error
					ip2config.addr[i] = 0;
					if (status == PCIBIOS_DEVICE_NOT_FOUND) {
						printk( KERN_ERR "IP2: PCI board %d not found\n", i );
					} else {
						pcibios_strerror(status);
					}
				} 
			} 
#endif	/* ! 2_0_X */
#else
			printk( KERN_ERR "IP2: PCI card specified but PCI support not\n");
			printk( KERN_ERR "IP2: configured in this kernel.\n");
			printk( KERN_ERR "IP2: Recompile kernel with CONFIG_PCI defined!\n");
#endif /* CONFIG_PCI */
			break;
		case EISA:
			if ( (ip2config.addr[i] = find_eisa_board( Eisa_slot + 1 )) != 0) {
				/* Eisa_irq set as side effect, boo */
				ip2config.type[i] = EISA;
			} 
			ip2config.irq[i] = Eisa_irq;
			break;
		}	/* switch */
	}	/* for */
	for ( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		if ( ip2config.addr[i] ) {
			pB = kmalloc( sizeof(i2eBordStr), GFP_KERNEL);
			if ( pB != NULL ) {
				i2BoardPtrTable[i] = pB;
				memset( pB, 0, sizeof(i2eBordStr) );
				iiSetAddress( pB, ip2config.addr[i], ii2DelayTimer );
				iiReset( pB );
			} else {
				printk(KERN_ERR "IP2: board memory allocation error\n");
			}
		}
	}
	for ( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		if ( ( pB = i2BoardPtrTable[i] ) != NULL ) {
			iiResetDelay( pB );
			break;
		}
	}
	for ( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		if ( i2BoardPtrTable[i] != NULL ) {
			ip2_init_board( i );
		}
	}

#ifdef IP2DEBUG_TRACE
	ip2trace (ITRC_NO_PORT, ITRC_INIT, 2, 0 );
#endif

	/* Zero out the normal tty device structure. */
	memset ( &ip2_tty_driver, 0, sizeof ip2_tty_driver );

	/* Initialise the relevant fields. */
	ip2_tty_driver.magic                = TTY_DRIVER_MAGIC;
	ip2_tty_driver.name                 = pcTty;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,0)
	ip2_tty_driver.driver_name          = pcDriver_name;
	ip2_tty_driver.read_proc          	= ip2_read_proc;
#endif
	ip2_tty_driver.major                = IP2_TTY_MAJOR;
	ip2_tty_driver.minor_start          = 0;
	ip2_tty_driver.num                  = IP2_MAX_PORTS;
	ip2_tty_driver.type                 = TTY_DRIVER_TYPE_SERIAL;
	ip2_tty_driver.subtype              = SERIAL_TYPE_NORMAL;
	ip2_tty_driver.init_termios         = tty_std_termios;
	ip2_tty_driver.init_termios.c_cflag = B9600|CS8|CREAD|HUPCL|CLOCAL;
#ifdef	CONFIG_DEVFS_FS
	ip2_tty_driver.flags                = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
#else
	ip2_tty_driver.flags                = TTY_DRIVER_REAL_RAW;
#endif
	ip2_tty_driver.refcount             = &ref_count;
	ip2_tty_driver.table                = TtyTable;
	ip2_tty_driver.termios              = Termios;
	ip2_tty_driver.termios_locked       = TermiosLocked;

	/* Setup the pointers to the implemented functions. */
	ip2_tty_driver.open            = ip2_open;
	ip2_tty_driver.close           = ip2_close;
	ip2_tty_driver.write           = ip2_write;
	ip2_tty_driver.put_char        = ip2_putchar;
	ip2_tty_driver.flush_chars     = ip2_flush_chars;
	ip2_tty_driver.write_room      = ip2_write_room;
	ip2_tty_driver.chars_in_buffer = ip2_chars_in_buf;
	ip2_tty_driver.flush_buffer    = ip2_flush_buffer;
	ip2_tty_driver.ioctl           = ip2_ioctl;
	ip2_tty_driver.throttle        = ip2_throttle;
	ip2_tty_driver.unthrottle      = ip2_unthrottle;
	ip2_tty_driver.set_termios     = ip2_set_termios;
	ip2_tty_driver.set_ldisc       = ip2_set_line_discipline;
	ip2_tty_driver.stop            = ip2_stop;
	ip2_tty_driver.start           = ip2_start;
	ip2_tty_driver.hangup          = ip2_hangup;

	/* Initialise the callout driver structure from the tty driver, and
	 * make the needed adjustments.
	 */
	ip2_callout_driver         = ip2_tty_driver;
	ip2_callout_driver.name    = pcCallout;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,0)
	ip2_callout_driver.driver_name = pcDriver_name;
	ip2_callout_driver.read_proc  = NULL;
#endif
	ip2_callout_driver.major   = IP2_CALLOUT_MAJOR;
	ip2_callout_driver.subtype = SERIAL_TYPE_CALLOUT;

#ifdef IP2DEBUG_TRACE
	ip2trace (ITRC_NO_PORT, ITRC_INIT, 3, 0 );
#endif

	/* Register the tty devices. */
	if ( ( err = tty_register_driver ( &ip2_tty_driver ) ) ) {
		printk(KERN_ERR "IP2: failed to register tty driver (%d)\n", err);
	} else
	if ( ( err = tty_register_driver ( &ip2_callout_driver ) ) ) {
		printk(KERN_ERR "IP2: failed to register callout driver (%d)\n", err);
	} else
	/* Register the IPL driver. */
#ifdef	CONFIG_DEVFS_FS
	if (( err = devfs_register_chrdev ( IP2_IPL_MAJOR, pcIpl, &ip2_ipl )))
#else
	if ( ( err = register_chrdev ( IP2_IPL_MAJOR, pcIpl, &ip2_ipl ) ) )
#endif
	{
		printk(KERN_ERR "IP2: failed to register IPL device (%d)\n", err );
	} else
	/* Register the read_procmem thing */
	if (!create_proc_info_entry("ip2mem",0,&proc_root,ip2_read_procmem)) {
		printk(KERN_ERR "IP2: failed to register read_procmem\n");
	} else {

#ifdef IP2DEBUG_TRACE
	ip2trace (ITRC_NO_PORT, ITRC_INIT, 4, 0 );
#endif
		/* Register the interrupt handler or poll handler, depending upon the
		 * specified interrupt.
		 */
#ifdef	CONFIG_DEVFS_FS
		if (!devfs_handle)
			devfs_handle = devfs_mk_dir (NULL, "ip2", NULL);
#endif

		for( i = 0; i < IP2_MAX_BOARDS; ++i ) {
#ifdef	CONFIG_DEVFS_FS
			char name[16];
#endif

			if ( 0 == ip2config.addr[i] ) {
				continue;
			}

#ifdef	CONFIG_DEVFS_FS
			sprintf( name, "ipl%d", i );
			i2BoardPtrTable[i]->devfs_ipl_handle =
				devfs_register (devfs_handle, name,
					DEVFS_FL_DEFAULT,
					IP2_IPL_MAJOR, 4 * i,
					S_IRUSR | S_IWUSR | S_IRGRP | S_IFCHR,
					&ip2_ipl, NULL);

			sprintf( name, "stat%d", i );
			i2BoardPtrTable[i]->devfs_stat_handle =
				devfs_register (devfs_handle, name,
					DEVFS_FL_DEFAULT,
					IP2_IPL_MAJOR, 4 * i + 1,
					S_IRUSR | S_IWUSR | S_IRGRP | S_IFCHR,
					&ip2_ipl, NULL);

			for ( box = 0; box < ABS_MAX_BOXES; ++box )
			{
			    for ( j = 0; j < ABS_BIGGEST_BOX; ++j )
			    {
				if ( pB->i2eChannelMap[box] & (1 << j) )
				{
				    tty_register_devfs(&ip2_tty_driver,
					0, j + ABS_BIGGEST_BOX *
						(box+i*ABS_MAX_BOXES));
				    tty_register_devfs(&ip2_callout_driver,
					0, j + ABS_BIGGEST_BOX *
						(box+i*ABS_MAX_BOXES));
				}
			    }
			}
#endif

			if (poll_only) {
					ip2config.irq[i] = CIR_POLL;
			}
			if ( ip2config.irq[i] == CIR_POLL ) {
retry:
				if (!TimerOn) {
					PollTimer.expires = POLL_TIMEOUT;
					add_timer ( &PollTimer );
					TimerOn = 1;
					printk( KERN_INFO "IP2: polling\n");
				}
			} else {
				if (have_requested_irq(ip2config.irq[i]))
					continue;
				rc = request_irq( ip2config.irq[i], ip2_interrupt,
					IP2_SA_FLAGS | (ip2config.type[i] == PCI ? SA_SHIRQ : 0),
					pcName, (void *)&pcName);
				if (rc) {
					printk(KERN_ERR "IP2: an request_irq failed: error %d\n",rc);
					ip2config.irq[i] = CIR_POLL;
					printk( KERN_INFO "IP2: Polling %ld/sec.\n",
							(POLL_TIMEOUT - jiffies));
					goto retry;
				} 
				mark_requested_irq(ip2config.irq[i]);
				/* Initialise the interrupt handler bottom half (aka slih). */
			}
		}
		for( i = 0; i < IP2_MAX_BOARDS; ++i ) {
			if ( i2BoardPtrTable[i] ) {
				set_irq( i, ip2config.irq[i] ); /* set and enable board interrupt */
			}
		}
	}
#ifdef IP2DEBUG_TRACE
	ip2trace (ITRC_NO_PORT, ITRC_INIT, ITRC_RETURN, 0 );
#endif

	return 0;
}

/******************************************************************************/
/* Function:   ip2_init_board()                                               */
/* Parameters: Index of board in configuration structure                      */
/* Returns:    Success (0)                                                    */
/*                                                                            */
/* Description:                                                               */
/* This function initializes the specified board. The loadware is copied to   */
/* the board, the channel structures are initialized, and the board details   */
/* are reported on the console.                                               */
/******************************************************************************/
static void __init
ip2_init_board( int boardnum )
{
	int i,rc;
	int nports = 0, nboxes = 0;
	i2ChanStrPtr pCh;
	i2eBordStrPtr pB = i2BoardPtrTable[boardnum];

	if ( !iiInitialize ( pB ) ) {
		printk ( KERN_ERR "IP2: Failed to initialize board at 0x%x, error %d\n",
			 pB->i2eBase, pB->i2eError );
		kfree ( pB );
		i2BoardPtrTable[boardnum] = NULL;
		return;
	}
	printk(KERN_INFO "Board %d: addr=0x%x irq=%d ", boardnum + 1,
	       ip2config.addr[boardnum], ip2config.irq[boardnum] );

	if (0 != ( rc = check_region( ip2config.addr[boardnum], 8))) {
		i2BoardPtrTable[boardnum] = NULL;
		printk(KERN_ERR "bad addr=0x%x rc = %d\n",
				ip2config.addr[boardnum], rc );
		return;
	}
	request_region( ip2config.addr[boardnum], 8, pcName );

	if ( iiDownloadAll ( pB, (loadHdrStrPtr)Fip_firmware, 1, Fip_firmware_size )
	    != II_DOWN_GOOD ) {
		printk ( KERN_ERR "IP2:failed to download loadware " );
	} else {
		printk ( KERN_INFO "fv=%d.%d.%d lv=%d.%d.%d\n",
			 pB->i2ePom.e.porVersion,
			 pB->i2ePom.e.porRevision,
			 pB->i2ePom.e.porSubRev, pB->i2eLVersion,
			 pB->i2eLRevision, pB->i2eLSub );
	}

	switch ( pB->i2ePom.e.porID & ~POR_ID_RESERVED ) {

	default:
		printk( KERN_ERR "IP2: Unknown board type, ID = %x",
				pB->i2ePom.e.porID );
		nports = 0;
		goto ex_exit;
		break;

	case POR_ID_II_4: /* IntelliPort-II, ISA-4 (4xRJ45) */
		printk ( KERN_INFO "ISA-4" );
		nports = 4;
		break;

	case POR_ID_II_8: /* IntelliPort-II, 8-port using standard brick. */
		printk ( KERN_INFO "ISA-8 std" );
		nports = 8;
		break;

	case POR_ID_II_8R: /* IntelliPort-II, 8-port using RJ11's (no CTS) */
		printk ( KERN_INFO "ISA-8 RJ11" );
		nports = 8;
		break;

	case POR_ID_FIIEX: /* IntelliPort IIEX */
	{
		int portnum = IP2_PORTS_PER_BOARD * boardnum;
		int            box;

		for( box = 0; box < ABS_MAX_BOXES; ++box ) {
			if ( pB->i2eChannelMap[box] != 0 ) {
				++nboxes;
			}
			for( i = 0; i < ABS_BIGGEST_BOX; ++i ) {
				if ( pB->i2eChannelMap[box] & 1<< i ) {
					++nports;
				}
			}
		}
		DevTableMem[boardnum] = pCh =
			kmalloc( sizeof(i2ChanStr) * nports, GFP_KERNEL );
		if ( !i2InitChannels( pB, nports, pCh ) ) {
			printk(KERN_ERR "i2InitChannels failed: %d\n",pB->i2eError);
		}
		pB->i2eChannelPtr = &DevTable[portnum];
		pB->i2eChannelCnt = ABS_MOST_PORTS;

		for( box = 0; box < ABS_MAX_BOXES; ++box, portnum += ABS_BIGGEST_BOX ) {
			for( i = 0; i < ABS_BIGGEST_BOX; ++i ) {
				if ( pB->i2eChannelMap[box] & (1 << i) ) {
					DevTable[portnum + i] = pCh;
					pCh->port_index = portnum + i;
					pCh++;
				}
			}
		}
		printk(KERN_INFO "IP2: EX box=%d ports=%d %d bit",
			nboxes, nports, pB->i2eDataWidth16 ? 16 : 8 );
		}
		goto ex_exit;
		break;
	}
	DevTableMem[boardnum] = pCh =
		kmalloc ( sizeof (i2ChanStr) * nports, GFP_KERNEL );
	pB->i2eChannelPtr = pCh;
	pB->i2eChannelCnt = nports;
	i2InitChannels ( pB, pB->i2eChannelCnt, pCh );
	pB->i2eChannelPtr = &DevTable[IP2_PORTS_PER_BOARD * boardnum];

	for( i = 0; i < pB->i2eChannelCnt; ++i ) {
		DevTable[IP2_PORTS_PER_BOARD * boardnum + i] = pCh;
		pCh->port_index = (IP2_PORTS_PER_BOARD * boardnum) + i;
		pCh++;
	}
ex_exit:
	printk ( KERN_INFO "\n" );
}

/******************************************************************************/
/* Function:   find_eisa_board ( int start_slot )                             */
/* Parameters: First slot to check                                            */
/* Returns:    Address of EISA IntelliPort II controller                      */
/*                                                                            */
/* Description:                                                               */
/* This function searches for an EISA IntelliPort controller, starting        */
/* from the specified slot number. If the motherboard is not identified as an */
/* EISA motherboard, or no valid board ID is selected it returns 0. Otherwise */
/* it returns the base address of the controller.                             */
/******************************************************************************/
static unsigned short __init
find_eisa_board( int start_slot )
{
	int i, j;
	unsigned int idm = 0;
	unsigned int idp = 0;
	unsigned int base = 0;
	unsigned int value;
	int setup_address;
	int setup_irq;
	int ismine = 0;

	/*
	 * First a check for an EISA motherboard, which we do by comparing the
	 * EISA ID registers for the system board and the first couple of slots.
	 * No slot ID should match the system board ID, but on an ISA or PCI
	 * machine the odds are that an empty bus will return similar values for
	 * each slot.
	 */
	i = 0x0c80;
	value = (inb(i) << 24) + (inb(i+1) << 16) + (inb(i+2) << 8) + inb(i+3);
	for( i = 0x1c80; i <= 0x4c80; i += 0x1000 ) {
		j = (inb(i)<<24)+(inb(i+1)<<16)+(inb(i+2)<<8)+inb(i+3);
		if ( value == j )
			return 0;
	}

	/*
	 * OK, so we are inclined to believe that this is an EISA machine. Find
	 * an IntelliPort controller.
	 */
	for( i = start_slot; i < 16; i++ ) {
		base = i << 12;
		idm = (inb(base + 0xc80) << 8) | (inb(base + 0xc81) & 0xff);
		idp = (inb(base + 0xc82) << 8) | (inb(base + 0xc83) & 0xff);
		ismine = 0;
		if ( idm == 0x0e8e ) {
			if ( idp == 0x0281 || idp == 0x0218 ) {
				ismine = 1;
			} else if ( idp == 0x0282 || idp == 0x0283 ) {
				ismine = 3;	/* Can do edge-trigger */
			}
			if ( ismine ) {
				Eisa_slot = i;
				break;
			}
		}
	}
	if ( !ismine )
		return 0;

	/* It's some sort of EISA card, but at what address is it configured? */

	setup_address = base + 0xc88;
	value = inb(base + 0xc86);
	setup_irq = (value & 8) ? Valid_Irqs[value & 7] : 0;

	if ( (ismine & 2) && !(value & 0x10) ) {
		ismine = 1;	/* Could be edging, but not */
	}

	if ( Eisa_irq == 0 ) {
		Eisa_irq = setup_irq;
	} else if ( Eisa_irq != setup_irq ) {
		printk ( KERN_ERR "IP2: EISA irq mismatch between EISA controllers\n" );
	}

#ifdef IP2DEBUG_INIT
printk(KERN_DEBUG "Computone EISA board in slot %d, I.D. 0x%x%x, Address 0x%x",
	       base >> 12, idm, idp, setup_address);
	if ( Eisa_irq ) {
		printk(KERN_DEBUG ", Interrupt %d %s\n",
		       setup_irq, (ismine & 2) ? "(edge)" : "(level)");
	} else {
		printk(KERN_DEBUG ", (polled)\n");
	}
#endif
	return setup_address;
}

/******************************************************************************/
/* Function:   set_irq()                                                      */
/* Parameters: index to board in board table                                  */
/*             IRQ to use                                                     */
/* Returns:    Success (0)                                                    */
/*                                                                            */
/* Description:                                                               */
/******************************************************************************/
static void
set_irq( int boardnum, int boardIrq )
{
	unsigned char tempCommand[16];
	i2eBordStrPtr pB = i2BoardPtrTable[boardnum];
	unsigned long flags;

	/*
	 * Notify the boards they may generate interrupts. This is done by
	 * sending an in-line command to channel 0 on each board. This is why
	 * the channels have to be defined already. For each board, if the
	 * interrupt has never been defined, we must do so NOW, directly, since
	 * board will not send flow control or even give an interrupt until this
	 * is done.  If polling we must send 0 as the interrupt parameter.
	 */

	// We will get an interrupt here at the end of this function

	iiDisableMailIrq(pB);

	/* We build up the entire packet header. */
	CHANNEL_OF(tempCommand) = 0;
	PTYPE_OF(tempCommand) = PTYPE_INLINE;
	CMD_COUNT_OF(tempCommand) = 2;
	(CMD_OF(tempCommand))[0] = CMDVALUE_IRQ;
	(CMD_OF(tempCommand))[1] = boardIrq;
	/*
	 * Write to FIFO; don't bother to adjust fifo capacity for this, since
	 * board will respond almost immediately after SendMail hit.
	 */
	WRITE_LOCK_IRQSAVE(&pB->write_fifo_spinlock,flags);
	iiWriteBuf(pB, tempCommand, 4);
	WRITE_UNLOCK_IRQRESTORE(&pB->write_fifo_spinlock,flags);
	pB->i2eUsingIrq = boardIrq;
	pB->i2eOutMailWaiting |= MB_OUT_STUFFED;

	/* Need to update number of boards before you enable mailbox int */
	++i2nBoards;

	CHANNEL_OF(tempCommand) = 0;
	PTYPE_OF(tempCommand) = PTYPE_BYPASS;
	CMD_COUNT_OF(tempCommand) = 6;
	(CMD_OF(tempCommand))[0] = 88;	// SILO
	(CMD_OF(tempCommand))[1] = 64;	// chars
	(CMD_OF(tempCommand))[2] = 32;	// ms

	(CMD_OF(tempCommand))[3] = 28;	// MAX_BLOCK
	(CMD_OF(tempCommand))[4] = 64;	// chars

	(CMD_OF(tempCommand))[5] = 87;	// HW_TEST
	WRITE_LOCK_IRQSAVE(&pB->write_fifo_spinlock,flags);
	iiWriteBuf(pB, tempCommand, 8);
	WRITE_UNLOCK_IRQRESTORE(&pB->write_fifo_spinlock,flags);

	CHANNEL_OF(tempCommand) = 0;
	PTYPE_OF(tempCommand) = PTYPE_BYPASS;
	CMD_COUNT_OF(tempCommand) = 1;
	(CMD_OF(tempCommand))[0] = 84;	/* get BOX_IDS */
	iiWriteBuf(pB, tempCommand, 3);

#ifdef XXX
	// enable heartbeat for test porpoises
	CHANNEL_OF(tempCommand) = 0;
	PTYPE_OF(tempCommand) = PTYPE_BYPASS;
	CMD_COUNT_OF(tempCommand) = 2;
	(CMD_OF(tempCommand))[0] = 44;	/* get ping */
	(CMD_OF(tempCommand))[1] = 200;	/* 200 ms */
	WRITE_LOCK_IRQSAVE(&pB->write_fifo_spinlock,flags);
	iiWriteBuf(pB, tempCommand, 4);
	WRITE_UNLOCK_IRQRESTORE(&pB->write_fifo_spinlock,flags);
#endif

	iiEnableMailIrq(pB);
	iiSendPendingMail(pB);
}

/******************************************************************************/
/* Interrupt Handler Section                                                  */
/******************************************************************************/

static inline void
service_all_boards()
{
	int i;
	i2eBordStrPtr  pB;

	/* Service every board on the list */
	for( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		pB = i2BoardPtrTable[i];
		if ( pB ) {
			i2ServiceBoard( pB );
		}
	}
}


#ifdef USE_IQI
static struct tq_struct 
senior_service =
{	// it's the death that worse than fate
	NULL,
	0,
	(void(*)(void*)) service_all_boards,
	NULL,	//later - board address XXX
};
#endif

/******************************************************************************/
/* Function:   ip2_interrupt(int irq, void *dev_id, struct pt_regs * regs)    */
/* Parameters: irq - interrupt number                                         */
/*             pointer to optional device ID structure                        */
/*             pointer to register structure                                  */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static void
ip2_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	int i;
	i2eBordStrPtr  pB;

#ifdef IP2DEBUG_TRACE
	ip2trace (ITRC_NO_PORT, ITRC_INTR, 99, 1, irq );
#endif

#ifdef USE_IQI

	queue_task(&senior_service, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

#else
	/* Service just the boards on the list using this irq */
	for( i = 0; i < i2nBoards; ++i ) {
		pB = i2BoardPtrTable[i];
		if ( pB && (pB->i2eUsingIrq == irq) ) {
			i2ServiceBoard( pB );
		}
	}

#endif /* USE_IQI */

	++irq_counter;

#ifdef IP2DEBUG_TRACE
	ip2trace (ITRC_NO_PORT, ITRC_INTR, ITRC_RETURN, 0 );
#endif
}

/******************************************************************************/
/* Function:   ip2_poll(unsigned long arg)                                    */
/* Parameters: ?                                                              */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/* This function calls the library routine i2ServiceBoard for each board in   */
/* the board table. This is used instead of the interrupt routine when polled */
/* mode is specified.                                                         */
/******************************************************************************/
static void
ip2_poll(unsigned long arg)
{
#ifdef IP2DEBUG_TRACE
	ip2trace (ITRC_NO_PORT, ITRC_INTR, 100, 0 );
#endif
	TimerOn = 0; // it's the truth but not checked in service

	bh_counter++; 

#ifdef USE_IQI

	queue_task(&senior_service, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

#else
	// Just polled boards, service_all might be better
	ip2_interrupt(0, NULL, NULL);

#endif /* USE_IQI */

	PollTimer.expires = POLL_TIMEOUT;
	add_timer( &PollTimer );
	TimerOn = 1;

#ifdef IP2DEBUG_TRACE
	ip2trace (ITRC_NO_PORT, ITRC_INTR, ITRC_RETURN, 0 );
#endif
}

static inline void 
do_input( i2ChanStrPtr pCh )
{
	unsigned long flags;

#ifdef IP2DEBUG_TRACE
	ip2trace(CHANN, ITRC_INPUT, 21, 0 );
#endif
	// Data input
	if ( pCh->pTTY != NULL ) {
		READ_LOCK_IRQSAVE(&pCh->Ibuf_spinlock,flags)
		if (!pCh->throttled && (pCh->Ibuf_stuff != pCh->Ibuf_strip)) {
			READ_UNLOCK_IRQRESTORE(&pCh->Ibuf_spinlock,flags)
			i2Input( pCh );
		} else
			READ_UNLOCK_IRQRESTORE(&pCh->Ibuf_spinlock,flags)
	} else {
#ifdef IP2DEBUG_TRACE
		ip2trace(CHANN, ITRC_INPUT, 22, 0 );
#endif
		i2InputFlush( pCh );
	}
}

// code duplicated from n_tty (ldisc)
static inline void 
isig(int sig, struct tty_struct *tty, int flush)
{
	if (tty->pgrp > 0)
		kill_pg(tty->pgrp, sig, 1);
	if (flush || !L_NOFLSH(tty)) {
		if ( tty->ldisc.flush_buffer )  
			tty->ldisc.flush_buffer(tty);
		i2InputFlush( tty->driver_data );
	}
}

static inline void
do_status( i2ChanStrPtr pCh )
{
	int status;

	status =  i2GetStatus( pCh, (I2_BRK|I2_PAR|I2_FRA|I2_OVR) );

#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_STATUS, 21, 1, status );
#endif

	if (pCh->pTTY && (status & (I2_BRK|I2_PAR|I2_FRA|I2_OVR)) ) {
		if ( (status & I2_BRK) ) {
			// code duplicated from n_tty (ldisc)
			if (I_IGNBRK(pCh->pTTY))
				goto skip_this;
			if (I_BRKINT(pCh->pTTY)) {
				isig(SIGINT, pCh->pTTY, 1);
				goto skip_this;
			}
			wake_up_interruptible(&pCh->pTTY->read_wait);
		}
#ifdef NEVER_HAPPENS_AS_SETUP_XXX
	// and can't work because we don't know the_char
	// as the_char is reported on a seperate path
	// The intelligent board does this stuff as setup
	{
	char brkf = TTY_NORMAL;
	unsigned char brkc = '\0';
	unsigned char tmp;
		if ( (status & I2_BRK) ) {
			brkf = TTY_BREAK;
			brkc = '\0';
		} 
		else if (status & I2_PAR) {
			brkf = TTY_PARITY;
			brkc = the_char;
		} else if (status & I2_FRA) {
			brkf = TTY_FRAME;
			brkc = the_char;
		} else if (status & I2_OVR) {
			brkf = TTY_OVERRUN;
			brkc = the_char;
		}
		tmp = pCh->pTTY->real_raw;
		pCh->pTTY->real_raw = 0;
		pCh->pTTY->ldisc.receive_buf( pCh->pTTY, &brkc, &brkf, 1 );
		pCh->pTTY->real_raw = tmp;
	}
#endif /* NEVER_HAPPENS_AS_SETUP_XXX */
	}
skip_this:

	if ( status & (I2_DDCD | I2_DDSR | I2_DCTS | I2_DRI) ) {
		wake_up_interruptible(&pCh->delta_msr_wait);

		if ( (pCh->flags & ASYNC_CHECK_CD) && (status & I2_DDCD) ) {
			if ( status & I2_DCD ) {
				if ( pCh->wopen ) {
					wake_up_interruptible ( &pCh->open_wait );
				}
			} else if ( !(pCh->flags & ASYNC_CALLOUT_ACTIVE) ) {
				if (pCh->pTTY &&  (!(pCh->pTTY->termios->c_cflag & CLOCAL)) ) {
					tty_hangup( pCh->pTTY );
				}
			}
		}
	}

#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_STATUS, 26, 0 );
#endif
}

/******************************************************************************/
/* Device Open/Close/Ioctl Entry Point Section                                */
/******************************************************************************/

/******************************************************************************/
/* Function:   open_sanity_check()                                            */
/* Parameters: Pointer to tty structure                                       */
/*             Pointer to file structure                                      */
/* Returns:    Success or failure                                             */
/*                                                                            */
/* Description:                                                               */
/* Verifies the structure magic numbers and cross links.                      */
/******************************************************************************/
#ifdef IP2DEBUG_OPEN
static void 
open_sanity_check( i2ChanStrPtr pCh, i2eBordStrPtr pBrd )
{
	if ( pBrd->i2eValid != I2E_MAGIC ) {
		printk(KERN_ERR "IP2: invalid board structure\n" );
	} else if ( pBrd != pCh->pMyBord ) {
		printk(KERN_ERR "IP2: board structure pointer mismatch (%p)\n",
			 pCh->pMyBord );
	} else if ( pBrd->i2eChannelCnt < pCh->port_index ) {
		printk(KERN_ERR "IP2: bad device index (%d)\n", pCh->port_index );
	} else if (&((i2ChanStrPtr)pBrd->i2eChannelPtr)[pCh->port_index] != pCh) {
	} else {
		printk(KERN_INFO "IP2: all pointers check out!\n" );
	}
}
#endif


/******************************************************************************/
/* Function:   ip2_open()                                                     */
/* Parameters: Pointer to tty structure                                       */
/*             Pointer to file structure                                      */
/* Returns:    Success or failure                                             */
/*                                                                            */
/* Description: (MANDATORY)                                                   */
/* A successful device open has to run a gauntlet of checks before it         */
/* completes. After some sanity checking and pointer setup, the function      */
/* blocks until all conditions are satisfied. It then initialises the port to */
/* the default characteristics and returns.                                   */
/******************************************************************************/
static int
ip2_open( PTTY tty, struct file *pFile )
{
	int rc = 0;
	int do_clocal = 0;
	i2ChanStrPtr  pCh = DevTable[MINOR(tty->device)];

#ifdef IP2DEBUG_TRACE
	ip2trace (MINOR(tty->device), ITRC_OPEN, ITRC_ENTER, 0 );
#endif

	if ( pCh == NULL ) {
		return -ENODEV;
	}
	/* Setup pointer links in device and tty structures */
	pCh->pTTY = tty;
	tty->driver_data = pCh;
	MOD_INC_USE_COUNT;

#ifdef IP2DEBUG_OPEN
	printk(KERN_DEBUG \
			"IP2:open(tty=%p,pFile=%p):dev=%x,maj=%d,min=%d,ch=%d,idx=%d\n",
	       tty, pFile, tty->device, MAJOR(tty->device), MINOR(tty->device),
			 pCh->infl.hd.i2sChannel, pCh->port_index);
	open_sanity_check ( pCh, pCh->pMyBord );
#endif

	i2QueueCommands(PTYPE_INLINE, pCh, 100, 3, CMD_DTRUP,CMD_RTSUP,CMD_DCD_REP);
	pCh->dataSetOut |= (I2_DTR | I2_RTS);
	serviceOutgoingFifo( pCh->pMyBord );

	/* Block here until the port is ready (per serial and istallion) */
	/*
	 * 1. If the port is in the middle of closing wait for the completion
	 *    and then return the appropriate error.
	 */
	if ( tty_hung_up_p(pFile) || ( pCh->flags & ASYNC_CLOSING )) {
		if ( pCh->flags & ASYNC_CLOSING ) {
			interruptible_sleep_on( &pCh->close_wait);
		}
		if ( tty_hung_up_p(pFile) ) {
			return( pCh->flags & ASYNC_HUP_NOTIFY ) ? -EAGAIN : -ERESTARTSYS;
		}
	}
	/*
	 * 2. If this is a callout device, make sure the normal port is not in
	 *    use, and that someone else doesn't have the callout device locked.
	 *    (These are the only tests the standard serial driver makes for
	 *    callout devices.)
	 */
	if ( tty->driver.subtype == SERIAL_TYPE_CALLOUT ) {
		if ( pCh->flags & ASYNC_NORMAL_ACTIVE ) {
			return -EBUSY;
		}
		if ( ( pCh->flags & ASYNC_CALLOUT_ACTIVE )  &&
		    ( pCh->flags & ASYNC_SESSION_LOCKOUT ) &&
		    ( pCh->session != current->session ) ) {
			return -EBUSY;
		}
		if ( ( pCh->flags & ASYNC_CALLOUT_ACTIVE ) &&
		    ( pCh->flags & ASYNC_PGRP_LOCKOUT )   &&
		    ( pCh->pgrp != current->pgrp ) ) {
			return -EBUSY;
		}
		pCh->flags |= ASYNC_CALLOUT_ACTIVE;
		goto noblock;
	}
	/*
	 * 3. Handle a non-blocking open of a normal port.
	 */
	if ( (pFile->f_flags & O_NONBLOCK) || (tty->flags & (1<<TTY_IO_ERROR) )) {
		if ( pCh->flags & ASYNC_CALLOUT_ACTIVE ) {
			return -EBUSY;
		}
		pCh->flags |= ASYNC_NORMAL_ACTIVE;
		goto noblock;
	}
	/*
	 * 4. Now loop waiting for the port to be free and carrier present
	 *    (if required).
	 */
	if ( pCh->flags & ASYNC_CALLOUT_ACTIVE ) {
		if ( pCh->NormalTermios.c_cflag & CLOCAL ) {
			do_clocal = 1;
		}
	} else {
		if ( tty->termios->c_cflag & CLOCAL ) {
			do_clocal = 1;
		}
	}

#ifdef IP2DEBUG_OPEN
	printk(KERN_DEBUG "OpenBlock: do_clocal = %d\n", do_clocal);
#endif

	++pCh->wopen;
	for(;;) {
		if ( !(pCh->flags & ASYNC_CALLOUT_ACTIVE)) {
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 2, CMD_DTRUP, CMD_RTSUP);
			pCh->dataSetOut |= (I2_DTR | I2_RTS);
			serviceOutgoingFifo( pCh->pMyBord );
		}
		if ( tty_hung_up_p(pFile) ) {
			return ( pCh->flags & ASYNC_HUP_NOTIFY ) ? -EBUSY : -ERESTARTSYS;
		}
		if ( !(pCh->flags & ASYNC_CALLOUT_ACTIVE) &&
				!(pCh->flags & ASYNC_CLOSING) && 
				(do_clocal || (pCh->dataSetIn & I2_DCD) )) {
			rc = 0;
			break;
		}

#ifdef IP2DEBUG_OPEN
		printk(KERN_DEBUG "ASYNC_CALLOUT_ACTIVE = %s\n",
			(pCh->flags & ASYNC_CALLOUT_ACTIVE)?"True":"False");
		printk(KERN_DEBUG "ASYNC_CLOSING = %s\n",
			(pCh->flags & ASYNC_CLOSING)?"True":"False");
		printk(KERN_DEBUG "OpenBlock: waiting for CD or signal\n");
#endif
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_OPEN, 3, 2, (pCh->flags & ASYNC_CALLOUT_ACTIVE),
								(pCh->flags & ASYNC_CLOSING) );
#endif
		/* check for signal */
		if (signal_pending(current)) {
			rc = (( pCh->flags & ASYNC_HUP_NOTIFY ) ? -EAGAIN : -ERESTARTSYS);
			break;
		}
		interruptible_sleep_on(&pCh->open_wait);
	}
	--pCh->wopen; //why count?
#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_OPEN, 4, 0 );
#endif
	if (rc != 0 ) {
		return rc;
	}
	pCh->flags |= ASYNC_NORMAL_ACTIVE;

noblock:

	/* first open - Assign termios structure to port */
	if ( tty->count == 1 ) {
		i2QueueCommands(PTYPE_INLINE, pCh, 0, 2, CMD_CTSFL_DSAB, CMD_RTSFL_DSAB);
		if ( pCh->flags & ASYNC_SPLIT_TERMIOS ) {
			if ( tty->driver.subtype == SERIAL_TYPE_NORMAL ) {
				*tty->termios = pCh->NormalTermios;
			} else {
				*tty->termios = pCh->CalloutTermios;
			}
		}
		/* Now we must send the termios settings to the loadware */
		set_params( pCh, NULL );
	}

	/* override previous and never reset ??? */
	pCh->session = current->session;
	pCh->pgrp = current->pgrp;

	/*
	 * Now set any i2lib options. These may go away if the i2lib code ends
	 * up rolled into the mainline.
	 */
	pCh->channelOptions |= CO_NBLOCK_WRITE;

#ifdef IP2DEBUG_OPEN
	printk (KERN_DEBUG "IP2: open completed\n" );
#endif
	serviceOutgoingFifo( pCh->pMyBord );

#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_OPEN, ITRC_RETURN, 0 );
#endif
	return 0;
}

/******************************************************************************/
/* Function:   ip2_close()                                                    */
/* Parameters: Pointer to tty structure                                       */
/*             Pointer to file structure                                      */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static void
ip2_close( PTTY tty, struct file *pFile )
{
	i2ChanStrPtr  pCh = tty->driver_data;

	if ( !pCh ) {
		return;
	}

#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_CLOSE, ITRC_ENTER, 0 );
#endif

#ifdef IP2DEBUG_OPEN
	printk(KERN_DEBUG "IP2:close ttyF%02X:\n",MINOR(tty->device));
#endif

	if ( tty_hung_up_p ( pFile ) ) {
		MOD_DEC_USE_COUNT;

#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_CLOSE, 2, 1, 2 );
#endif
		return;
	}
	if ( tty->count > 1 ) { /* not the last close */
		MOD_DEC_USE_COUNT;
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_CLOSE, 2, 1, 3 );
#endif
		return;
	}
	pCh->flags |= ASYNC_CLOSING;	// last close actually

	/*
	 * Save the termios structure, since this port may have separate termios
	 * for callout and dialin.
	 */
	if (pCh->flags & ASYNC_NORMAL_ACTIVE)
		pCh->NormalTermios = *tty->termios;
	if (pCh->flags & ASYNC_CALLOUT_ACTIVE)
		pCh->CalloutTermios = *tty->termios;

	tty->closing = 1;

	if (pCh->ClosingWaitTime != ASYNC_CLOSING_WAIT_NONE) {
		/*
		 * Before we drop DTR, make sure the transmitter has completely drained.
		 * This uses an timeout, after which the close
		 * completes.
		 */
		ip2_wait_until_sent(tty, pCh->ClosingWaitTime );
	}
	/*
	 * At this point we stop accepting input. Here we flush the channel
	 * input buffer which will allow the board to send up more data. Any
	 * additional input is tossed at interrupt/poll time.
	 */
	i2InputFlush( pCh );

	/* disable DSS reporting */
	i2QueueCommands(PTYPE_INLINE, pCh, 100, 4,
				CMD_DCD_NREP, CMD_CTS_NREP, CMD_DSR_NREP, CMD_RI_NREP);
	if ( !tty || (tty->termios->c_cflag & HUPCL) ) {
		i2QueueCommands(PTYPE_INLINE, pCh, 100, 2, CMD_RTSDN, CMD_DTRDN);
		pCh->dataSetOut &= ~(I2_DTR | I2_RTS);
		i2QueueCommands( PTYPE_INLINE, pCh, 100, 1, CMD_PAUSE(25));
	}

	serviceOutgoingFifo ( pCh->pMyBord );

	if ( tty->driver.flush_buffer ) 
		tty->driver.flush_buffer(tty);
	if ( tty->ldisc.flush_buffer )  
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
	
	pCh->pTTY = NULL;

	if (pCh->wopen) {
		if (pCh->ClosingDelay) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(pCh->ClosingDelay);
		}
		wake_up_interruptible(&pCh->open_wait);
	}

	pCh->flags &=~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE|ASYNC_CLOSING);
	wake_up_interruptible(&pCh->close_wait);

#ifdef IP2DEBUG_OPEN
	DBG_CNT("ip2_close: after wakeups--");
#endif

	MOD_DEC_USE_COUNT;

#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_CLOSE, ITRC_RETURN, 1, 1 );
#endif
	return;
}

/******************************************************************************/
/* Function:   ip2_hangup()                                                   */
/* Parameters: Pointer to tty structure                                       */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static void
ip2_hangup ( PTTY tty )
{
	i2ChanStrPtr  pCh = tty->driver_data;

#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_HANGUP, ITRC_ENTER, 0 );
#endif

	ip2_flush_buffer(tty);

	/* disable DSS reporting */

	i2QueueCommands(PTYPE_BYPASS, pCh, 0, 1, CMD_DCD_NREP);
	i2QueueCommands(PTYPE_INLINE, pCh, 0, 2, CMD_CTSFL_DSAB, CMD_RTSFL_DSAB);
	if ( !tty || (tty->termios->c_cflag & HUPCL) ) {
		i2QueueCommands(PTYPE_BYPASS, pCh, 0, 2, CMD_RTSDN, CMD_DTRDN);
		pCh->dataSetOut &= ~(I2_DTR | I2_RTS);
		i2QueueCommands( PTYPE_INLINE, pCh, 100, 1, CMD_PAUSE(25));
	}
	i2QueueCommands(PTYPE_INLINE, pCh, 1, 3, 
				CMD_CTS_NREP, CMD_DSR_NREP, CMD_RI_NREP);
	serviceOutgoingFifo ( pCh->pMyBord );

	wake_up_interruptible ( &pCh->delta_msr_wait );

	pCh->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
	pCh->pTTY = NULL;
	wake_up_interruptible ( &pCh->open_wait );

#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_HANGUP, ITRC_RETURN, 0 );
#endif
}

/******************************************************************************/
/******************************************************************************/
/* Device Output Section                                                      */
/******************************************************************************/
/******************************************************************************/

/******************************************************************************/
/* Function:   ip2_write()                                                    */
/* Parameters: Pointer to tty structure                                       */
/*             Flag denoting data is in user (1) or kernel (0) space          */
/*             Pointer to data                                                */
/*             Number of bytes to write                                       */
/* Returns:    Number of bytes actually written                               */
/*                                                                            */
/* Description: (MANDATORY)                                                   */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static int
ip2_write( PTTY tty, int user, const unsigned char *pData, int count)
{
	i2ChanStrPtr  pCh = tty->driver_data;
	int bytesSent = 0;
	unsigned long flags;

#ifdef IP2DEBUG_TRACE
   ip2trace (CHANN, ITRC_WRITE, ITRC_ENTER, 2, count, -1 );
#endif

	/* Flush out any buffered data left over from ip2_putchar() calls. */
	ip2_flush_chars( tty );

	/* This is the actual move bit. Make sure it does what we need!!!!! */
	WRITE_LOCK_IRQSAVE(&pCh->Pbuf_spinlock,flags);
	bytesSent = i2Output( pCh, pData, count, user );
	WRITE_UNLOCK_IRQRESTORE(&pCh->Pbuf_spinlock,flags);

#ifdef IP2DEBUG_TRACE
   ip2trace (CHANN, ITRC_WRITE, ITRC_RETURN, 1, bytesSent );
#endif
	return bytesSent > 0 ? bytesSent : 0;
}

/******************************************************************************/
/* Function:   ip2_putchar()                                                  */
/* Parameters: Pointer to tty structure                                       */
/*             Character to write                                             */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static void
ip2_putchar( PTTY tty, unsigned char ch )
{
	i2ChanStrPtr  pCh = tty->driver_data;
	unsigned long flags;

#ifdef IP2DEBUG_TRACE
//	ip2trace (CHANN, ITRC_PUTC, ITRC_ENTER, 1, ch );
#endif

	WRITE_LOCK_IRQSAVE(&pCh->Pbuf_spinlock,flags);
	pCh->Pbuf[pCh->Pbuf_stuff++] = ch;
	if ( pCh->Pbuf_stuff == sizeof pCh->Pbuf ) {
		WRITE_UNLOCK_IRQRESTORE(&pCh->Pbuf_spinlock,flags);
		ip2_flush_chars( tty );
	} else
		WRITE_UNLOCK_IRQRESTORE(&pCh->Pbuf_spinlock,flags);

#ifdef IP2DEBUG_TRACE
//	ip2trace (CHANN, ITRC_PUTC, ITRC_RETURN, 1, ch );
#endif
}

/******************************************************************************/
/* Function:   ip2_flush_chars()                                              */
/* Parameters: Pointer to tty structure                                       */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/******************************************************************************/
static void
ip2_flush_chars( PTTY tty )
{
	int   strip;
	i2ChanStrPtr  pCh = tty->driver_data;
	unsigned long flags;

	WRITE_LOCK_IRQSAVE(&pCh->Pbuf_spinlock,flags);
	if ( pCh->Pbuf_stuff ) {
#ifdef IP2DEBUG_TRACE
//	ip2trace (CHANN, ITRC_PUTC, 10, 1, strip );
#endif
		//
		// We may need to restart i2Output if it does not fullfill this request
		//
		strip = i2Output( pCh, pCh->Pbuf, pCh->Pbuf_stuff, 0 );
		if ( strip != pCh->Pbuf_stuff ) {
			memmove( pCh->Pbuf, &pCh->Pbuf[strip], pCh->Pbuf_stuff - strip );
		}
		pCh->Pbuf_stuff -= strip;
	}
	WRITE_UNLOCK_IRQRESTORE(&pCh->Pbuf_spinlock,flags);
}

/******************************************************************************/
/* Function:   ip2_write_room()                                               */
/* Parameters: Pointer to tty structure                                       */
/* Returns:    Number of bytes that the driver can accept                     */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/******************************************************************************/
static int
ip2_write_room ( PTTY tty )
{
	int bytesFree;
	i2ChanStrPtr  pCh = tty->driver_data;
	unsigned long flags;

	READ_LOCK_IRQSAVE(&pCh->Pbuf_spinlock,flags);
	bytesFree = i2OutputFree( pCh ) - pCh->Pbuf_stuff;
	READ_UNLOCK_IRQRESTORE(&pCh->Pbuf_spinlock,flags);

#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_WRITE, 11, 1, bytesFree );
#endif

	return ((bytesFree > 0) ? bytesFree : 0);
}

/******************************************************************************/
/* Function:   ip2_chars_in_buf()                                             */
/* Parameters: Pointer to tty structure                                       */
/* Returns:    Number of bytes queued for transmission                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static int
ip2_chars_in_buf ( PTTY tty )
{
	i2ChanStrPtr  pCh = tty->driver_data;
	int rc;
	unsigned long flags;
#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_WRITE, 12, 1, pCh->Obuf_char_count + pCh->Pbuf_stuff );
#endif
#ifdef IP2DEBUG_WRITE
	printk (KERN_DEBUG "IP2: chars in buffer = %d (%d,%d)\n",
				 pCh->Obuf_char_count + pCh->Pbuf_stuff,
				 pCh->Obuf_char_count, pCh->Pbuf_stuff );
#endif
	READ_LOCK_IRQSAVE(&pCh->Obuf_spinlock,flags);
	rc =  pCh->Obuf_char_count;
	READ_UNLOCK_IRQRESTORE(&pCh->Obuf_spinlock,flags);
	READ_LOCK_IRQSAVE(&pCh->Pbuf_spinlock,flags);
	rc +=  pCh->Pbuf_stuff;
	READ_UNLOCK_IRQRESTORE(&pCh->Pbuf_spinlock,flags);
	return rc;
}

/******************************************************************************/
/* Function:   ip2_flush_buffer()                                             */
/* Parameters: Pointer to tty structure                                       */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static void
ip2_flush_buffer( PTTY tty )
{
	i2ChanStrPtr  pCh = tty->driver_data;
	unsigned long flags;

#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_FLUSH, ITRC_ENTER, 0 );
#endif
#ifdef IP2DEBUG_WRITE
	printk (KERN_DEBUG "IP2: flush buffer\n" );
#endif
	WRITE_LOCK_IRQSAVE(&pCh->Pbuf_spinlock,flags);
	pCh->Pbuf_stuff = 0;
	WRITE_UNLOCK_IRQRESTORE(&pCh->Pbuf_spinlock,flags);
	i2FlushOutput( pCh );
	ip2_owake(tty);
#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_FLUSH, ITRC_RETURN, 0 );
#endif
}

/******************************************************************************/
/* Function:   ip2_wait_until_sent()                                          */
/* Parameters: Pointer to tty structure                                       */
/*             Timeout for wait.                                              */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/* This function is used in place of the normal tty_wait_until_sent, which    */
/* only waits for the driver buffers to be empty (or rather, those buffers    */
/* reported by chars_in_buffer) which doesn't work for IP2 due to the         */
/* indeterminate number of bytes buffered on the board.                       */
/******************************************************************************/
static void
ip2_wait_until_sent ( PTTY tty, int timeout )
{
	int i = jiffies;
	i2ChanStrPtr  pCh = tty->driver_data;

	tty_wait_until_sent(tty, timeout );
	if ( (i = timeout - (jiffies -i)) > 0)
		i2DrainOutput( pCh, i );
}

/******************************************************************************/
/******************************************************************************/
/* Device Input Section                                                       */
/******************************************************************************/
/******************************************************************************/

/******************************************************************************/
/* Function:   ip2_throttle()                                                 */
/* Parameters: Pointer to tty structure                                       */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static void
ip2_throttle ( PTTY tty )
{
	i2ChanStrPtr  pCh = tty->driver_data;

#ifdef IP2DEBUG_READ
	printk (KERN_DEBUG "IP2: throttle\n" );
#endif
	/*
	 * Signal the poll/interrupt handlers not to forward incoming data to
	 * the line discipline. This will cause the buffers to fill up in the
	 * library and thus cause the library routines to send the flow control
	 * stuff.
	 */
	pCh->throttled = 1;
}

/******************************************************************************/
/* Function:   ip2_unthrottle()                                               */
/* Parameters: Pointer to tty structure                                       */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static void
ip2_unthrottle ( PTTY tty )
{
	i2ChanStrPtr  pCh = tty->driver_data;
	unsigned long flags;

#ifdef IP2DEBUG_READ
	printk (KERN_DEBUG "IP2: unthrottle\n" );
#endif

	/* Pass incoming data up to the line discipline again. */
	pCh->throttled = 0;
 	i2QueueCommands(PTYPE_BYPASS, pCh, 0, 1, CMD_RESUME);
	serviceOutgoingFifo( pCh->pMyBord );
	READ_LOCK_IRQSAVE(&pCh->Ibuf_spinlock,flags)
	if ( pCh->Ibuf_stuff != pCh->Ibuf_strip ) {
		READ_UNLOCK_IRQRESTORE(&pCh->Ibuf_spinlock,flags)
#ifdef IP2DEBUG_READ
		printk (KERN_DEBUG "i2Input called from unthrottle\n" );
#endif
		i2Input( pCh );
	} else
		READ_UNLOCK_IRQRESTORE(&pCh->Ibuf_spinlock,flags)
}

static void
ip2_start ( PTTY tty )
{
 	i2ChanStrPtr  pCh = DevTable[MINOR(tty->device)];

 	i2QueueCommands(PTYPE_BYPASS, pCh, 0, 1, CMD_RESUME);
 	i2QueueCommands(PTYPE_BYPASS, pCh, 100, 1, CMD_UNSUSPEND);
 	i2QueueCommands(PTYPE_BYPASS, pCh, 100, 1, CMD_RESUME);
#ifdef IP2DEBUG_WRITE
	printk (KERN_DEBUG "IP2: start tx\n" );
#endif
}

static void
ip2_stop ( PTTY tty )
{
 	i2ChanStrPtr  pCh = DevTable[MINOR(tty->device)];

 	i2QueueCommands(PTYPE_BYPASS, pCh, 100, 1, CMD_SUSPEND);
#ifdef IP2DEBUG_WRITE
	printk (KERN_DEBUG "IP2: stop tx\n" );
#endif
}

/******************************************************************************/
/* Device Ioctl Section                                                       */
/******************************************************************************/

/******************************************************************************/
/* Function:   ip2_ioctl()                                                    */
/* Parameters: Pointer to tty structure                                       */
/*             Pointer to file structure                                      */
/*             Command                                                        */
/*             Argument                                                       */
/* Returns:    Success or failure                                             */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static int
ip2_ioctl ( PTTY tty, struct file *pFile, UINT cmd, ULONG arg )
{
	i2ChanStrPtr pCh = DevTable[MINOR(tty->device)];
	struct async_icount cprev, cnow;	/* kernel counter temps */
	struct serial_icounter_struct *p_cuser;	/* user space */
	int rc = 0;
	unsigned long flags;

	if ( pCh == NULL ) {
		return -ENODEV;
	}

#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_IOCTL, ITRC_ENTER, 2, cmd, arg );
#endif

#ifdef IP2DEBUG_IOCTL
	printk(KERN_DEBUG "IP2: ioctl cmd (%x), arg (%lx)\n", cmd, arg );
#endif

	switch(cmd) {
	case TIOCGSERIAL:
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_IOCTL, 2, 1, rc );
#endif
		rc = get_serial_info(pCh, (struct serial_struct *) arg);
		if (rc)
			return rc;
		break;

	case TIOCSSERIAL:
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_IOCTL, 3, 1, rc );
#endif
		rc = set_serial_info(pCh, (struct serial_struct *) arg);
		if (rc)
			return rc;
		break;

	case TCXONC:
		rc = tty_check_change(tty);
		if (rc)
			return rc;
		switch (arg) {
		case TCOOFF:
			//return  -ENOIOCTLCMD;
			break;
		case TCOON:
			//return  -ENOIOCTLCMD;
			break;
		case TCIOFF:
			if (STOP_CHAR(tty) != __DISABLED_CHAR) {
				i2QueueCommands( PTYPE_BYPASS, pCh, 100, 1,
						CMD_XMIT_NOW(STOP_CHAR(tty)));
			}
			break;
		case TCION:
			if (START_CHAR(tty) != __DISABLED_CHAR) {
				i2QueueCommands( PTYPE_BYPASS, pCh, 100, 1,
						CMD_XMIT_NOW(START_CHAR(tty)));
			}
			break;
		default:
			return -EINVAL;
		}
		return 0;

	case TCSBRK:   /* SVID version: non-zero arg --> no break */
		rc = tty_check_change(tty);
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_IOCTL, 4, 1, rc );
#endif
		if (!rc) {
			ip2_wait_until_sent(tty,0);
			if (!arg) {
				i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_SEND_BRK(250));
				serviceOutgoingFifo( pCh->pMyBord );
			}
		}
		break;

	case TCSBRKP:  /* support for POSIX tcsendbreak() */
		rc = tty_check_change(tty);
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_IOCTL, 5, 1, rc );
#endif
		if (!rc) {
			ip2_wait_until_sent(tty,0);
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1,
				CMD_SEND_BRK(arg ? arg*100 : 250));
			serviceOutgoingFifo ( pCh->pMyBord );	
		}
		break;

	case TIOCGSOFTCAR:
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_IOCTL, 6, 1, rc );
#endif
			PUT_USER(rc,C_CLOCAL(tty) ? 1 : 0, (unsigned long *) arg);
		if (rc)	
			return rc;
	break;

	case TIOCSSOFTCAR:
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_IOCTL, 7, 1, rc );
#endif
		GET_USER(rc,arg,(unsigned long *) arg);
		if (rc) 
			return rc;
		tty->termios->c_cflag = ((tty->termios->c_cflag & ~CLOCAL)
					 | (arg ? CLOCAL : 0));
		
		break;

	case TIOCMGET:
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_IOCTL, 8, 1, rc );
#endif
/*
	FIXME - the following code is causing a NULL pointer dereference in
	2.3.51 in an interrupt handler.  It's suppose to prompt the board
	to return the DSS signal status immediately.  Why doesn't it do
	the same thing in 2.2.14?
*/
/*
		i2QueueCommands(PTYPE_BYPASS, pCh, 100, 1, CMD_DSS_NOW);
		serviceOutgoingFifo( pCh->pMyBord );
		interruptible_sleep_on(&pCh->dss_now_wait);
		if (signal_pending(current)) {
			return -EINTR;
		}
*/
		PUT_USER(rc,
				    ((pCh->dataSetOut & I2_RTS) ? TIOCM_RTS : 0)
				  | ((pCh->dataSetOut & I2_DTR) ? TIOCM_DTR : 0)
				  | ((pCh->dataSetIn  & I2_DCD) ? TIOCM_CAR : 0)
				  | ((pCh->dataSetIn  & I2_RI)  ? TIOCM_RNG : 0)
				  | ((pCh->dataSetIn  & I2_DSR) ? TIOCM_DSR : 0)
				  | ((pCh->dataSetIn  & I2_CTS) ? TIOCM_CTS : 0),
				(unsigned int *) arg);
		break;

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_IOCTL, 9, 0 );
#endif
		rc = set_modem_info(pCh, cmd, (unsigned int *) arg);
		break;

	/*
	 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change - mask
	 * passed in arg for lines of interest (use |'ed TIOCM_RNG/DSR/CD/CTS
	 * for masking). Caller should use TIOCGICOUNT to see which one it was
	 */
	case TIOCMIWAIT:
		save_flags(flags);cli();
		cprev = pCh->icount;	 /* note the counters on entry */
		restore_flags(flags);
		i2QueueCommands(PTYPE_BYPASS, pCh, 100, 4, 
						CMD_DCD_REP, CMD_CTS_REP, CMD_DSR_REP, CMD_RI_REP);
		serviceOutgoingFifo( pCh->pMyBord );
		for(;;) {
#ifdef IP2DEBUG_TRACE
			ip2trace (CHANN, ITRC_IOCTL, 10, 0 );
#endif
			interruptible_sleep_on(&pCh->delta_msr_wait);
#ifdef IP2DEBUG_TRACE
			ip2trace (CHANN, ITRC_IOCTL, 11, 0 );
#endif
			/* see if a signal did it */
			if (signal_pending(current)) {
				rc = -ERESTARTSYS;
				break;
			}
			save_flags(flags);cli();
			cnow = pCh->icount; /* atomic copy */
			restore_flags(flags);
			if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
				cnow.dcd == cprev.dcd && cnow.cts == cprev.cts) {
				rc =  -EIO; /* no change => rc */
				break;
			}
			if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
			    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
			    ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
			    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts)) ) {
				rc =  0;
				break;
			}
			cprev = cnow;
		}
		i2QueueCommands(PTYPE_BYPASS, pCh, 100, 3, 
						 CMD_CTS_NREP, CMD_DSR_NREP, CMD_RI_NREP);
		if ( ! (pCh->flags	& ASYNC_CHECK_CD)) {
			i2QueueCommands(PTYPE_BYPASS, pCh, 100, 1, CMD_DCD_NREP);
		}
		serviceOutgoingFifo( pCh->pMyBord );
		return rc;
		break;

	/*
	 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
	 * Return: write counters to the user passed counter struct
	 * NB: both 1->0 and 0->1 transitions are counted except for RI where
	 * only 0->1 is counted. The controller is quite capable of counting
	 * both, but this done to preserve compatibility with the standard
	 * serial driver.
	 */
	case TIOCGICOUNT:
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_IOCTL, 11, 1, rc );
#endif
		save_flags(flags);cli();
		cnow = pCh->icount;
		restore_flags(flags);
		p_cuser = (struct serial_icounter_struct *) arg;
		PUT_USER(rc,cnow.cts, &p_cuser->cts);
		PUT_USER(rc,cnow.dsr, &p_cuser->dsr);
		PUT_USER(rc,cnow.rng, &p_cuser->rng);
		PUT_USER(rc,cnow.dcd, &p_cuser->dcd);
		PUT_USER(rc,cnow.rx, &p_cuser->rx);
		PUT_USER(rc,cnow.tx, &p_cuser->tx);
		PUT_USER(rc,cnow.frame, &p_cuser->frame);
		PUT_USER(rc,cnow.overrun, &p_cuser->overrun);
		PUT_USER(rc,cnow.parity, &p_cuser->parity);
		PUT_USER(rc,cnow.brk, &p_cuser->brk);
		PUT_USER(rc,cnow.buf_overrun, &p_cuser->buf_overrun);
		break;

	/*
	 * The rest are not supported by this driver. By returning -ENOIOCTLCMD they
	 * will be passed to the line discipline for it to handle.
	 */
	case TIOCSERCONFIG:
	case TIOCSERGWILD:
	case TIOCSERGETLSR:
	case TIOCSERSWILD:
	case TIOCSERGSTRUCT:
	case TIOCSERGETMULTI:
	case TIOCSERSETMULTI:

	default:
#ifdef IP2DEBUG_TRACE
		ip2trace (CHANN, ITRC_IOCTL, 12, 0 );
#endif
		rc =  -ENOIOCTLCMD;
		break;
	}
#ifdef IP2DEBUG_TRACE
	ip2trace (CHANN, ITRC_IOCTL, ITRC_RETURN, 0 );
#endif
	return rc;
}

/******************************************************************************/
/* Function:   set_modem_info()                                               */
/* Parameters: Pointer to channel structure                                   */
/*             Specific ioctl command                                         */
/*             Pointer to source for new settings                             */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/* This returns the current settings of the dataset signal inputs to the user */
/* program.                                                                   */
/******************************************************************************/
static int
set_modem_info(i2ChanStrPtr pCh, unsigned cmd, unsigned int *value)
{
	int rc;
	unsigned int arg;

	GET_USER(rc,arg,value);
	if (rc)
		return rc;
	switch(cmd) {
	case TIOCMBIS:
		if (arg & TIOCM_RTS) {
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_RTSUP);
			pCh->dataSetOut |= I2_RTS;
		}
		if (arg & TIOCM_DTR) {
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_DTRUP);
			pCh->dataSetOut |= I2_DTR;
		}
		break;
	case TIOCMBIC:
		if (arg & TIOCM_RTS) {
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_RTSDN);
			pCh->dataSetOut &= ~I2_RTS;
		}
		if (arg & TIOCM_DTR) {
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_DTRDN);
			pCh->dataSetOut &= ~I2_DTR;
		}
		break;
	case TIOCMSET:
		if ( (arg & TIOCM_RTS) && !(pCh->dataSetOut & I2_RTS) ) {
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_RTSUP);
			pCh->dataSetOut |= I2_RTS;
		} else if ( !(arg & TIOCM_RTS) && (pCh->dataSetOut & I2_RTS) ) {
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_RTSDN);
			pCh->dataSetOut &= ~I2_RTS;
		}
		if ( (arg & TIOCM_DTR) && !(pCh->dataSetOut & I2_DTR) ) {
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_DTRUP);
			pCh->dataSetOut |= I2_DTR;
		} else if ( !(arg & TIOCM_DTR) && (pCh->dataSetOut & I2_DTR) ) {
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_DTRDN);
			pCh->dataSetOut &= ~I2_DTR;
		}
		break;
	default:
		return -EINVAL;
	}
	serviceOutgoingFifo( pCh->pMyBord );
	return 0;
}

/******************************************************************************/
/* Function:   GetSerialInfo()                                                */
/* Parameters: Pointer to channel structure                                   */
/*             Pointer to old termios structure                               */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/* This is to support the setserial command, and requires processing of the   */
/* standard Linux serial structure.                                           */
/******************************************************************************/
static int
get_serial_info ( i2ChanStrPtr pCh, struct serial_struct *retinfo )
{
	struct serial_struct tmp;
	int rc;

	if ( !retinfo ) {
		return -EFAULT;
	}

	memset ( &tmp, 0, sizeof(tmp) );
	tmp.type = pCh->pMyBord->channelBtypes.bid_value[(pCh->port_index & (IP2_PORTS_PER_BOARD-1))/16];
	if (BID_HAS_654(tmp.type)) {
		tmp.type = PORT_16650;
	} else {
		tmp.type = PORT_CIRRUS;
	}
	tmp.line = pCh->port_index;
	tmp.port = pCh->pMyBord->i2eBase;
	tmp.irq  = ip2config.irq[pCh->port_index/64];
	tmp.flags = pCh->flags;
	tmp.baud_base = pCh->BaudBase;
	tmp.close_delay = pCh->ClosingDelay;
	tmp.closing_wait = pCh->ClosingWaitTime;
	tmp.custom_divisor = pCh->BaudDivisor;
   	COPY_TO_USER(rc,retinfo,&tmp,sizeof(*retinfo));
   return rc;
}

/******************************************************************************/
/* Function:   SetSerialInfo()                                                */
/* Parameters: Pointer to channel structure                                   */
/*             Pointer to old termios structure                               */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/* This function provides support for setserial, which uses the TIOCSSERIAL   */
/* ioctl. Not all setserial parameters are relevant. If the user attempts to  */
/* change the IRQ, address or type of the port the ioctl fails.               */
/******************************************************************************/
static int
set_serial_info( i2ChanStrPtr pCh, struct serial_struct *new_info )
{
	struct serial_struct ns;
	int   old_flags, old_baud_divisor;
	int     rc = 0;

	if ( !new_info ) {
		return -EFAULT;
	}
	COPY_FROM_USER(rc, &ns, new_info, sizeof (ns) );
	if (rc) {
		return rc;
	}
	/*
	 * We don't allow setserial to change IRQ, board address, type or baud
	 * base. Also line nunber as such is meaningless but we use it for our
	 * array index so it is fixed also.
	 */
	if ( (ns.irq  	    != ip2config.irq[pCh->port_index])
	    || ((int) ns.port      != ((int) (pCh->pMyBord->i2eBase)))
	    || (ns.baud_base != pCh->BaudBase)
	    || (ns.line      != pCh->port_index) ) {
		return -EINVAL;
	}

	old_flags = pCh->flags;
	old_baud_divisor = pCh->BaudDivisor;

	if ( !suser() ) {
		if ( ( ns.close_delay != pCh->ClosingDelay ) ||
		    ( (ns.flags & ~ASYNC_USR_MASK) !=
		      (pCh->flags & ~ASYNC_USR_MASK) ) ) {
			return -EPERM;
		}

		pCh->flags = (pCh->flags & ~ASYNC_USR_MASK) |
			       (ns.flags & ASYNC_USR_MASK);
		pCh->BaudDivisor = ns.custom_divisor;
	} else {
		pCh->flags = (pCh->flags & ~ASYNC_FLAGS) |
			       (ns.flags & ASYNC_FLAGS);
		pCh->BaudDivisor = ns.custom_divisor;
		pCh->ClosingDelay = ns.close_delay * HZ/100;
		pCh->ClosingWaitTime = ns.closing_wait * HZ/100;
	}

	if ( ( (old_flags & ASYNC_SPD_MASK) != (pCh->flags & ASYNC_SPD_MASK) )
	    || (old_baud_divisor != pCh->BaudDivisor) ) {
		// Invalidate speed and reset parameters
		set_params( pCh, NULL );
	}

	return rc;
}

/******************************************************************************/
/* Function:   ip2_set_termios()                                              */
/* Parameters: Pointer to tty structure                                       */
/*             Pointer to old termios structure                               */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static void
ip2_set_termios( PTTY tty, struct termios *old_termios )
{
	i2ChanStrPtr pCh = (i2ChanStrPtr)tty->driver_data;

#ifdef IP2DEBUG_IOCTL
	printk (KERN_DEBUG "IP2: set termios %p\n", old_termios );
#endif

	set_params( pCh, old_termios );
}

/******************************************************************************/
/* Function:   ip2_set_line_discipline()                                      */
/* Parameters: Pointer to tty structure                                       */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:  Does nothing                                                 */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static void
ip2_set_line_discipline ( PTTY tty )
{
#ifdef IP2DEBUG_IOCTL
	printk (KERN_DEBUG "IP2: set line discipline\n" );
#endif
#ifdef IP2DEBUG_TRACE
	ip2trace (((i2ChanStrPtr)tty->driver_data)->port_index, ITRC_IOCTL, 16, 0 );
#endif
}

/******************************************************************************/
/* Function:   SetLine Characteristics()                                      */
/* Parameters: Pointer to channel structure                                   */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/* This routine is called to update the channel structure with the new line   */
/* characteristics, and send the appropriate commands to the board when they  */
/* change.                                                                    */
/******************************************************************************/
static void
set_params( i2ChanStrPtr pCh, struct termios *o_tios )
{
	tcflag_t cflag, iflag, lflag;
	char stop_char, start_char;
	struct termios dummy;

	lflag = pCh->pTTY->termios->c_lflag;
	cflag = pCh->pTTY->termios->c_cflag;
	iflag = pCh->pTTY->termios->c_iflag;

	if (o_tios == NULL) {
		dummy.c_lflag = ~lflag;
		dummy.c_cflag = ~cflag;
		dummy.c_iflag = ~iflag;
		o_tios = &dummy;
	}

	{
		switch ( cflag & CBAUD ) {
		case B0:
			i2QueueCommands( PTYPE_BYPASS, pCh, 100, 2, CMD_RTSDN, CMD_DTRDN);
			pCh->dataSetOut &= ~(I2_DTR | I2_RTS);
			i2QueueCommands( PTYPE_INLINE, pCh, 100, 1, CMD_PAUSE(25));
			pCh->pTTY->termios->c_cflag |= (CBAUD & o_tios->c_cflag);
			goto service_it;
			break;
		case B38400:
			/*
			 * This is the speed that is overloaded with all the other high
			 * speeds, depending upon the flag settings.
			 */
			if ( ( pCh->flags & ASYNC_SPD_MASK ) == ASYNC_SPD_HI ) {
				pCh->speed = CBR_57600;
			} else if ( (pCh->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI ) {
				pCh->speed = CBR_115200;
			} else if ( (pCh->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST ) {
				pCh->speed = CBR_C1;
			} else {
				pCh->speed = CBR_38400;
			}
			break;
		case B50:      pCh->speed = CBR_50;      break;
		case B75:      pCh->speed = CBR_75;      break;
		case B110:     pCh->speed = CBR_110;     break;
		case B134:     pCh->speed = CBR_134;     break;
		case B150:     pCh->speed = CBR_150;     break;
		case B200:     pCh->speed = CBR_200;     break;
		case B300:     pCh->speed = CBR_300;     break;
		case B600:     pCh->speed = CBR_600;     break;
		case B1200:    pCh->speed = CBR_1200;    break;
		case B1800:    pCh->speed = CBR_1800;    break;
		case B2400:    pCh->speed = CBR_2400;    break;
		case B4800:    pCh->speed = CBR_4800;    break;
		case B9600:    pCh->speed = CBR_9600;    break;
		case B19200:   pCh->speed = CBR_19200;   break;
		case B57600:   pCh->speed = CBR_57600;   break;
		case B115200:  pCh->speed = CBR_115200;  break;
		case B153600:  pCh->speed = CBR_153600;  break;
		case B230400:  pCh->speed = CBR_230400;  break;
		case B307200:  pCh->speed = CBR_307200;  break;
		case B460800:  pCh->speed = CBR_460800;  break;
		case B921600:  pCh->speed = CBR_921600;  break;
		default:       pCh->speed = CBR_9600;    break;
		}
		if ( pCh->speed == CBR_C1 ) {
			// Process the custom speed parameters.
			int bps = pCh->BaudBase / pCh->BaudDivisor;
			if ( bps == 921600 ) {
				pCh->speed = CBR_921600;
			} else {
				bps = bps/10;
				i2QueueCommands( PTYPE_INLINE, pCh, 100, 1, CMD_BAUD_DEF1(bps) );
			}
		}
		i2QueueCommands( PTYPE_INLINE, pCh, 100, 1, CMD_SETBAUD(pCh->speed));
		
		i2QueueCommands ( PTYPE_INLINE, pCh, 100, 2, CMD_DTRUP, CMD_RTSUP);
		pCh->dataSetOut |= (I2_DTR | I2_RTS);
	}
	if ( (CSTOPB & cflag) ^ (CSTOPB & o_tios->c_cflag)) 
	{
		i2QueueCommands ( PTYPE_INLINE, pCh, 100, 1, 
			CMD_SETSTOP( ( cflag & CSTOPB ) ? CST_2 : CST_1));
	}
	if (((PARENB|PARODD) & cflag) ^ ((PARENB|PARODD) & o_tios->c_cflag)) 
	{
		i2QueueCommands ( PTYPE_INLINE, pCh, 100, 1,
			CMD_SETPAR( 
				(cflag & PARENB ?  (cflag & PARODD ? CSP_OD : CSP_EV) : CSP_NP)
			)
		);
	}
	/* byte size and parity */
	if ( (CSIZE & cflag)^(CSIZE & o_tios->c_cflag)) 
	{
		int datasize;
		switch ( cflag & CSIZE ) {
		case CS5: datasize = CSZ_5; break;
		case CS6: datasize = CSZ_6; break;
		case CS7: datasize = CSZ_7; break;
		case CS8: datasize = CSZ_8; break;
		default:  datasize = CSZ_5; break;	/* as per serial.c */
		}
		i2QueueCommands ( PTYPE_INLINE, pCh, 100, 1, CMD_SETBITS(datasize) );
	}
	/* Process CTS flow control flag setting */
	if ( (cflag & CRTSCTS) ) {
		i2QueueCommands(PTYPE_INLINE, pCh, 100,
						2, CMD_CTSFL_ENAB, CMD_RTSFL_ENAB);
	} else {
		i2QueueCommands(PTYPE_INLINE, pCh, 100,
						2, CMD_CTSFL_DSAB, CMD_RTSFL_DSAB);
	}
	//
	// Process XON/XOFF flow control flags settings
	//
	stop_char = STOP_CHAR(pCh->pTTY);
	start_char = START_CHAR(pCh->pTTY);

	//////////// can't be \000
	if (stop_char == __DISABLED_CHAR ) 
	{
		stop_char = ~__DISABLED_CHAR; 
	}
	if (start_char == __DISABLED_CHAR ) 
	{
		start_char = ~__DISABLED_CHAR;
	}
	/////////////////////////////////

	if ( o_tios->c_cc[VSTART] != start_char ) 
	{
		i2QueueCommands(PTYPE_BYPASS, pCh, 100, 1, CMD_DEF_IXON(start_char));
		i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_DEF_OXON(start_char));
	}
	if ( o_tios->c_cc[VSTOP] != stop_char ) 
	{
		 i2QueueCommands(PTYPE_BYPASS, pCh, 100, 1, CMD_DEF_IXOFF(stop_char));
		 i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_DEF_OXOFF(stop_char));
	}
	if (stop_char == __DISABLED_CHAR ) 
	{
		stop_char = ~__DISABLED_CHAR;  //TEST123
		goto no_xoff;
	}
	if ((iflag & (IXOFF))^(o_tios->c_iflag & (IXOFF))) 
	{
		if ( iflag & IXOFF ) {	// Enable XOFF output flow control
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_OXON_OPT(COX_XON));
		} else {	// Disable XOFF output flow control
no_xoff:
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_OXON_OPT(COX_NONE));
		}
	}
	if (start_char == __DISABLED_CHAR ) 
	{
		goto no_xon;
	}
	if ((iflag & (IXON|IXANY)) ^ (o_tios->c_iflag & (IXON|IXANY))) 
	{
		if ( iflag & IXON ) {
			if ( iflag & IXANY ) { // Enable XON/XANY output flow control
				i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_IXON_OPT(CIX_XANY));
			} else { // Enable XON output flow control
				i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_IXON_OPT(CIX_XON));
			}
		} else { // Disable XON output flow control
no_xon:
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_IXON_OPT(CIX_NONE));
		}
	}
	if ( (iflag & ISTRIP) ^ ( o_tios->c_iflag & (ISTRIP)) ) 
	{
		i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, 
				CMD_ISTRIP_OPT((iflag & ISTRIP ? 1 : 0)));
	}
	if ( (iflag & INPCK) ^ ( o_tios->c_iflag & (INPCK)) ) 
	{
		i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, 
				CMD_PARCHK((iflag & INPCK) ? CPK_ENAB : CPK_DSAB));
	}

	if ( (iflag & (IGNBRK|PARMRK|BRKINT|IGNPAR)) 
			^	( o_tios->c_iflag & (IGNBRK|PARMRK|BRKINT|IGNPAR)) ) 
	{
		char brkrpt = 0;
		char parrpt = 0;

		if ( iflag & IGNBRK ) { /* Ignore breaks altogether */
			/* Ignore breaks altogether */
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_BRK_NREP);
		} else {
			if ( iflag & BRKINT ) {
				if ( iflag & PARMRK ) {
					brkrpt = 0x0a;	// exception an inline triple
				} else {
					brkrpt = 0x1a;	// exception and NULL
				}
				brkrpt |= 0x04;	// flush input
			} else {
				if ( iflag & PARMRK ) {
					brkrpt = 0x0b;	//POSIX triple \0377 \0 \0
				} else {
					brkrpt = 0x01;	// Null only
				}
			}
			i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_BRK_REP(brkrpt));
		} 

		if (iflag & IGNPAR) {
			parrpt = 0x20;
													/* would be 2 for not cirrus bug */
													/* would be 0x20 cept for cirrus bug */
		} else {
			if ( iflag & PARMRK ) {
				/*
				 * Replace error characters with 3-byte sequence (\0377,\0,char)
				 */
				parrpt = 0x04 ;
				i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_ISTRIP_OPT((char)0));
			} else {
				parrpt = 0x03;
			} 
		}
		i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_SET_ERROR(parrpt));
	}
	if (cflag & CLOCAL) {
		// Status reporting fails for DCD if this is off
		i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_DCD_NREP);
		pCh->flags &= ~ASYNC_CHECK_CD;
	} else {
		i2QueueCommands(PTYPE_INLINE, pCh, 100, 1, CMD_DCD_REP);
		pCh->flags	|= ASYNC_CHECK_CD;
	}

#ifdef XXX
do_flags_thing:	// This is a test, we don't do the flags thing
	
	if ( (cflag & CRTSCTS) ) {
		cflag |= 014000000000;
	}
	i2QueueCommands(PTYPE_BYPASS, pCh, 100, 1, 
				CMD_UNIX_FLAGS(iflag,cflag,lflag));
#endif
		
service_it:
	i2DrainOutput( pCh, 100 );		
}

/******************************************************************************/
/* IPL Device Section                                                         */
/******************************************************************************/

/******************************************************************************/
/* Function:   ip2_ipl_read()                                                  */
/* Parameters: Pointer to device inode                                        */
/*             Pointer to file structure                                      */
/*             Pointer to data                                                */
/*             Number of bytes to read                                        */
/* Returns:    Success or failure                                             */
/*                                                                            */
/* Description:   Ugly                                                        */
/*                                                                            */
/*                                                                            */
/******************************************************************************/

static 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
int
ip2_ipl_read(struct inode *pInode, char *pData, size_t count, loff_t *off )
	unsigned int minor = MINOR( pInode->i_rdev );
#else
ssize_t
ip2_ipl_read(struct file *pFile, char *pData, size_t count, loff_t *off )
{
	unsigned int minor = MINOR( pFile->f_dentry->d_inode->i_rdev );
#endif
	int rc = 0;

#ifdef IP2DEBUG_IPL
	printk (KERN_DEBUG "IP2IPL: read %p, %d bytes\n", pData, count );
#endif

	switch( minor ) {
	case 0:	    // IPL device
		rc = -EINVAL;
		break;
	case 1:	    // Status dump
		rc = -EINVAL;
		break;
	case 2:	    // Ping device
		rc = -EINVAL;
		break;
	case 3:	    // Trace device
		rc = DumpTraceBuffer ( pData, count );
		break;
	case 4:	    // Trace device
		rc = DumpFifoBuffer ( pData, count );
		break;
	default:
		rc = -ENODEV;
		break;
	}
	return rc;
}

static int
DumpFifoBuffer ( char *pData, int count )
{
#ifdef DEBUG_FIFO
	int rc;
	COPY_TO_USER(rc, pData, DBGBuf, count);

	printk(KERN_DEBUG "Last index %d\n", I );

	return count;
#endif	/* DEBUG_FIFO */
	return 0;
}

static int
DumpTraceBuffer ( char *pData, int count )
{
#ifdef IP2DEBUG_TRACE
	int rc;
	int dumpcount;
	int chunk;
	int *pIndex = (int*)pData;

	if ( count < (sizeof(int) * 6) ) {
		return -EIO;
	}
	PUT_USER(rc, tracewrap, pIndex );
	PUT_USER(rc, TRACEMAX, ++pIndex );
	PUT_USER(rc, tracestrip, ++pIndex );
	PUT_USER(rc, tracestuff, ++pIndex );
	pData += sizeof(int) * 6;
	count -= sizeof(int) * 6;

	dumpcount = tracestuff - tracestrip;
	if ( dumpcount < 0 ) {
		dumpcount += TRACEMAX;
	}
	if ( dumpcount > count ) {
		dumpcount = count;
	}
	chunk = TRACEMAX - tracestrip;
	if ( dumpcount > chunk ) {
		COPY_TO_USER(rc, pData, &tracebuf[tracestrip],
			      chunk * sizeof(tracebuf[0]) );
		pData += chunk * sizeof(tracebuf[0]);
		tracestrip = 0;
		chunk = dumpcount - chunk;
	} else {
		chunk = dumpcount;
	}
	COPY_TO_USER(rc, pData, &tracebuf[tracestrip],
		      chunk * sizeof(tracebuf[0]) );
	tracestrip += chunk;
	tracewrap = 0;

	PUT_USER(rc, tracestrip, ++pIndex );
	PUT_USER(rc, tracestuff, ++pIndex );

	return dumpcount;
#else
	return 0;
#endif
}

/******************************************************************************/
/* Function:   ip2_ipl_write()                                                 */
/* Parameters:                                                                */
/*             Pointer to file structure                                      */
/*             Pointer to data                                                */
/*             Number of bytes to write                                       */
/* Returns:    Success or failure                                             */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static ssize_t
ip2_ipl_write(struct file *pFile, const char *pData, size_t count, loff_t *off)
{
#ifdef IP2DEBUG_IPL
	printk (KERN_DEBUG "IP2IPL: write %p, %d bytes\n", pData, count );
#endif
	return 0;
}

/******************************************************************************/
/* Function:   ip2_ipl_ioctl()                                                */
/* Parameters: Pointer to device inode                                        */
/*             Pointer to file structure                                      */
/*             Command                                                        */
/*             Argument                                                       */
/* Returns:    Success or failure                                             */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static int
ip2_ipl_ioctl ( struct inode *pInode, struct file *pFile, UINT cmd, ULONG arg )
{
	unsigned int iplminor = MINOR(pInode->i_rdev);
	int rc = 0;
	ULONG *pIndex = (ULONG*)arg;
	i2eBordStrPtr pB = i2BoardPtrTable[iplminor / 4];
	i2ChanStrPtr pCh;

#ifdef IP2DEBUG_IPL
	printk (KERN_DEBUG "IP2IPL: ioctl cmd %d, arg %ld\n", cmd, arg );
#endif

	switch ( iplminor ) {
	case 0:	    // IPL device
		rc = -EINVAL;
		break;
	case 1:	    // Status dump
	case 5:
	case 9:
	case 13:
		switch ( cmd ) {
		case 64:	/* Driver - ip2stat */
			PUT_USER(rc, ref_count, pIndex++ );
			PUT_USER(rc, irq_counter, pIndex++  );
			PUT_USER(rc, bh_counter, pIndex++  );
			break;

		case 65:	/* Board  - ip2stat */
			if ( pB ) {
				COPY_TO_USER(rc, (char*)arg, (char*)pB, sizeof(i2eBordStr) );
				PUT_USER(rc, INB(pB->i2eStatus),
					(ULONG*)(arg + (ULONG)(&pB->i2eStatus) - (ULONG)pB ) );
			} else {
				rc = -ENODEV;
			}
			break;

		default:
			pCh = DevTable[cmd];
			if ( pCh )
			{
				COPY_TO_USER(rc, (char*)arg, (char*)pCh, sizeof(i2ChanStr) );
			} else {
				rc = cmd < 64 ? -ENODEV : -EINVAL;
			}
		}
		break;

	case 2:	    // Ping device
		rc = -EINVAL;
		break;
	case 3:	    // Trace device
		if ( cmd == 1 ) {
			PUT_USER(rc, iiSendPendingMail, pIndex++ );
			PUT_USER(rc, i2InitChannels, pIndex++ );
			PUT_USER(rc, i2QueueNeeds, pIndex++ );
			PUT_USER(rc, i2QueueCommands, pIndex++ );
			PUT_USER(rc, i2GetStatus, pIndex++ );
			PUT_USER(rc, i2Input, pIndex++ );
			PUT_USER(rc, i2InputFlush, pIndex++ );
			PUT_USER(rc, i2Output, pIndex++ );
			PUT_USER(rc, i2FlushOutput, pIndex++ );
			PUT_USER(rc, i2DrainWakeup, pIndex++ );
			PUT_USER(rc, i2DrainOutput, pIndex++ );
			PUT_USER(rc, i2OutputFree, pIndex++ );
			PUT_USER(rc, i2StripFifo, pIndex++ );
			PUT_USER(rc, i2StuffFifoBypass, pIndex++ );
			PUT_USER(rc, i2StuffFifoFlow, pIndex++ );
			PUT_USER(rc, i2StuffFifoInline, pIndex++ );
			PUT_USER(rc, i2ServiceBoard, pIndex++ );
			PUT_USER(rc, serviceOutgoingFifo, pIndex++ );
			// PUT_USER(rc, ip2_init, pIndex++ );
			PUT_USER(rc, ip2_init_board, pIndex++ );
			PUT_USER(rc, find_eisa_board, pIndex++ );
			PUT_USER(rc, set_irq, pIndex++ );
			PUT_USER(rc, ip2_interrupt, pIndex++ );
			PUT_USER(rc, ip2_poll, pIndex++ );
			PUT_USER(rc, service_all_boards, pIndex++ );
			PUT_USER(rc, do_input, pIndex++ );
			PUT_USER(rc, do_status, pIndex++ );
#ifndef IP2DEBUG_OPEN
			PUT_USER(rc, 0, pIndex++ );
#else
			PUT_USER(rc, open_sanity_check, pIndex++ );
#endif
			PUT_USER(rc, ip2_open, pIndex++ );
			PUT_USER(rc, ip2_close, pIndex++ );
			PUT_USER(rc, ip2_hangup, pIndex++ );
			PUT_USER(rc, ip2_write, pIndex++ );
			PUT_USER(rc, ip2_putchar, pIndex++ );
			PUT_USER(rc, ip2_flush_chars, pIndex++ );
			PUT_USER(rc, ip2_write_room, pIndex++ );
			PUT_USER(rc, ip2_chars_in_buf, pIndex++ );
			PUT_USER(rc, ip2_flush_buffer, pIndex++ );

			//PUT_USER(rc, ip2_wait_until_sent, pIndex++ );
			PUT_USER(rc, 0, pIndex++ );

			PUT_USER(rc, ip2_throttle, pIndex++ );
			PUT_USER(rc, ip2_unthrottle, pIndex++ );
			PUT_USER(rc, ip2_ioctl, pIndex++ );
			PUT_USER(rc, set_modem_info, pIndex++ );
			PUT_USER(rc, get_serial_info, pIndex++ );
			PUT_USER(rc, set_serial_info, pIndex++ );
			PUT_USER(rc, ip2_set_termios, pIndex++ );
			PUT_USER(rc, ip2_set_line_discipline, pIndex++ );
			PUT_USER(rc, set_params, pIndex++ );
		} else {
			rc = -EINVAL;
		}

		break;

	default:
		rc = -ENODEV;
		break;
	}
	return rc;
}

/******************************************************************************/
/* Function:   ip2_ipl_open()                                                 */
/* Parameters: Pointer to device inode                                        */
/*             Pointer to file structure                                      */
/* Returns:    Success or failure                                             */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
static int
ip2_ipl_open( struct inode *pInode, struct file *pFile )
{
	unsigned int iplminor = MINOR(pInode->i_rdev);
	i2eBordStrPtr pB;
	i2ChanStrPtr  pCh;

#ifdef IP2DEBUG_IPL
	printk (KERN_DEBUG "IP2IPL: open\n" );
#endif

	switch(iplminor) {
	// These are the IPL devices
	case 0:
	case 4:
	case 8:
	case 12:
		break;

	// These are the status devices
	case 1:
	case 5:
	case 9:
	case 13:
		break;

	// These are the debug devices
	case 2:
	case 6:
	case 10:
	case 14:
		pB = i2BoardPtrTable[iplminor / 4];
		pCh = (i2ChanStrPtr) pB->i2eChannelPtr;
		break;

	// This is the trace device
	case 3:
		break;
	}
	return 0;
}
/******************************************************************************/
/* Function:   ip2_read_procmem                                               */
/* Parameters:                                                                */
/*                                                                            */
/* Returns: Length of output                                                  */
/*                                                                            */
/* Description:                                                               */
/*   Supplies some driver operating parameters                                */
/*	Not real useful unless your debugging the fifo							  */
/*                                                                            */
/******************************************************************************/

#define LIMIT  (PAGE_SIZE - 120)

static int
ip2_read_procmem(char *buf, char **start, off_t offset, int len)
{
	i2eBordStrPtr  pB;
	i2ChanStrPtr  pCh;
	PTTY tty;
	int i;

	len = 0;

#define FMTLINE	"%3d: 0x%08x 0x%08x 0%011o 0%011o\n"
#define FMTLIN2	"     0x%04x 0x%04x tx flow 0x%x\n"
#define FMTLIN3	"     0x%04x 0x%04x rc flow\n"

	len += sprintf(buf+len,"\n");

	for( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		pB = i2BoardPtrTable[i];
		if ( pB ) {
			len += sprintf(buf+len,"board %d:\n",i);
			len += sprintf(buf+len,"\tFifo rem: %d mty: %x outM %x\n",
				pB->i2eFifoRemains,pB->i2eWaitingForEmptyFifo,pB->i2eOutMailWaiting);
		}
	}

	len += sprintf(buf+len,"#: tty flags, port flags,     cflags,     iflags\n");
	for (i=0; i < IP2_MAX_PORTS; i++) {
		if (len > LIMIT)
			break;
		pCh = DevTable[i];
		if (pCh) {
			tty = pCh->pTTY;
			if (tty && tty->count) {
				len += sprintf(buf+len,FMTLINE,i,(int)tty->flags,pCh->flags,
									tty->termios->c_cflag,tty->termios->c_iflag);

				len += sprintf(buf+len,FMTLIN2,
						pCh->outfl.asof,pCh->outfl.room,pCh->channelNeeds);
				len += sprintf(buf+len,FMTLIN3,pCh->infl.asof,pCh->infl.room);
			}
		}
	}
	return len;
}

/*
 * This is the handler for /proc/tty/driver/ip2
 *
 * This stretch of code has been largely plagerized from at least three
 * different sources including ip2mkdev.c and a couple of other drivers.
 * The bugs are all mine.  :-)	=mhw=
 */
int ip2_read_proc(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	int	i, j, box;
	int	len = 0;
	int	boxes = 0;
	int	ports = 0;
	int	tports = 0;
	off_t	begin = 0;
	i2eBordStrPtr  pB;

	len += sprintf(page, "ip2info: 1.0 driver: %s\n", pcVersion );
	len += sprintf(page+len, "Driver: SMajor=%d CMajor=%d IMajor=%d MaxBoards=%d MaxBoxes=%d MaxPorts=%d\n",
			IP2_TTY_MAJOR, IP2_CALLOUT_MAJOR, IP2_IPL_MAJOR,
			IP2_MAX_BOARDS, ABS_MAX_BOXES, ABS_BIGGEST_BOX);

	for( i = 0; i < IP2_MAX_BOARDS; ++i ) {
		/* This need to be reset for a board by board count... */
		boxes = 0;
		pB = i2BoardPtrTable[i];
		if( pB ) {
			switch( pB->i2ePom.e.porID & ~POR_ID_RESERVED ) 
			{
			case POR_ID_FIIEX:
				len += sprintf( page+len, "Board %d: EX ports=", i );
				for( box = 0; box < ABS_MAX_BOXES; ++box )
				{
					ports = 0;

					if( pB->i2eChannelMap[box] != 0 ) ++boxes;
					for( j = 0; j < ABS_BIGGEST_BOX; ++j ) 
					{
						if( pB->i2eChannelMap[box] & 1<< j ) {
							++ports;
						}
					}
					len += sprintf( page+len, "%d,", ports );
					tports += ports;
				}

				--len;	/* Backup over that last comma */

				len += sprintf( page+len, " boxes=%d width=%d", boxes, pB->i2eDataWidth16 ? 16 : 8 );
				break;

			case POR_ID_II_4:
				len += sprintf(page+len, "Board %d: ISA-4 ports=4 boxes=1", i );
				tports = ports = 4;
				break;

			case POR_ID_II_8:
				len += sprintf(page+len, "Board %d: ISA-8-std ports=8 boxes=1", i );
				tports = ports = 8;
				break;

			case POR_ID_II_8R:
				len += sprintf(page+len, "Board %d: ISA-8-RJ11 ports=8 boxes=1", i );
				tports = ports = 8;
				break;

			default:
				len += sprintf(page+len, "Board %d: unknown", i );
				/* Don't try and probe for minor numbers */
				tports = ports = 0;
			}

		} else {
			/* Don't try and probe for minor numbers */
			len += sprintf(page+len, "Board %d: vacant", i );
			tports = ports = 0;
		}

		if( tports ) {
			len += sprintf(page+len, " minors=" );

			for ( box = 0; box < ABS_MAX_BOXES; ++box )
			{
				for ( j = 0; j < ABS_BIGGEST_BOX; ++j )
				{
					if ( pB->i2eChannelMap[box] & (1 << j) )
					{
						len += sprintf (page+len,"%d,",
							j + ABS_BIGGEST_BOX *
							(box+i*ABS_MAX_BOXES));
					}
				}
			}

			page[ len - 1 ] = '\n';	/* Overwrite that last comma */
		} else {
			len += sprintf (page+len,"\n" );
		}

		if (len+begin > off+count)
			break;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}

	if (i >= IP2_MAX_BOARDS)
		*eof = 1;
	if (off >= len+begin)
		return 0;

	*start = page + (off-begin);
	return ((count < begin+len-off) ? count : begin+len-off);
 }
 
/******************************************************************************/
/* Function:   ip2trace()                                                     */
/* Parameters: Value to add to trace buffer                                   */
/* Returns:    Nothing                                                        */
/*                                                                            */
/* Description:                                                               */
/*                                                                            */
/*                                                                            */
/******************************************************************************/
void
ip2trace (unsigned short pn, unsigned char cat, unsigned char label, unsigned long codes, ...)
{
#ifdef IP2DEBUG_TRACE
	long flags;
	unsigned long *pCode = &codes;
	union ip2breadcrumb bc;
	i2ChanStrPtr  pCh;


	tracebuf[tracestuff++] = jiffies;
	if ( tracestuff == TRACEMAX ) {
		tracestuff = 0;
	}
	if ( tracestuff == tracestrip ) {
		if ( ++tracestrip == TRACEMAX ) {
			tracestrip = 0;
		}
		++tracewrap;
	}

	bc.hdr.port  = 0xff & pn;
	bc.hdr.cat   = cat;
	bc.hdr.codes = (unsigned char)( codes & 0xff );
	bc.hdr.label = label;
	tracebuf[tracestuff++] = bc.value;

	for (;;) {
		if ( tracestuff == TRACEMAX ) {
			tracestuff = 0;
		}
		if ( tracestuff == tracestrip ) {
			if ( ++tracestrip == TRACEMAX ) {
				tracestrip = 0;
			}
			++tracewrap;
		}

		if ( !codes-- )
			break;

		tracebuf[tracestuff++] = *++pCode;
	}
#endif
}


