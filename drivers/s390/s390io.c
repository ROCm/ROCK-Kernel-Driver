/*
 *  drivers/s390/s390io.c
 *   S/390 common I/O routines
 *
 *  S390 version
 *    Copyright (C) 1999, 2000 IBM Deutschland Entwicklung GmbH,
 *                             IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *    ChangeLog: 01/07/2001 Blacklist cleanup (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *               01/04/2001 Holger Smolinski (smolinsk@de.ibm.com)
 *                          Fixed lost interrupts and do_adapter_IO
 *               xx/xx/xxxx nnn          multiple changes not reflected
 *               03/12/2001 Ingo Adlung  blacklist= - changed to cio_ignore=  
 *               03/14/2001 Ingo Adlung  disable interrupts before start_IO
 *                                        in Path Group processing 
 *                                       decrease retry2 on busy while 
 *                                        disabling sync_isc; reset isc_cnt
 *                                        on io error during sync_isc enablement
 *               05/09/2001 Cornelia Huck added exploitation of debug feature
 *               05/16/2001 Cornelia Huck added /proc/deviceinfo/<devno>/
 *               05/22/2001 Cornelia Huck added /proc/cio_ignore
 *                                        un-ignore blacklisted devices by piping 
 *                                        to /proc/cio_ignore
 *               xx/xx/xxxx some bugfixes & cleanups
 *               08/02/2001 Cornelia Huck not already known devices can be blacklisted
 *                                        by piping to /proc/cio_ignore
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif 
#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/smp.h>
#include <asm/pgtable.h>
#include <asm/delay.h>
#include <asm/processor.h>
#include <asm/lowcore.h>
#include <asm/idals.h>
#include <asm/uaccess.h> 
#include <asm/cpcmd.h>

#include <asm/s390io.h>
#include <asm/s390dyn.h>
#include <asm/s390mach.h>
#include <asm/debug.h>
#include <asm/queue.h>

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#define SANITY_CHECK(irq) do { \
if (irq > highest_subchannel || irq < 0) \
		return (-ENODEV); \
	if (ioinfo[irq] == INVALID_STORAGE_AREA) \
		return (-ENODEV); \
	} while(0) 

#undef  CONFIG_DEBUG_IO
#define CONFIG_DEBUG_CRW

unsigned int           highest_subchannel;
ioinfo_t              *ioinfo_head = NULL;
ioinfo_t              *ioinfo_tail = NULL;
ioinfo_t              *ioinfo[__MAX_SUBCHANNELS] = {
	[0 ... (__MAX_SUBCHANNELS-1)] = INVALID_STORAGE_AREA
};

static atomic_t    sync_isc     = ATOMIC_INIT(-1);
static int         sync_isc_cnt = 0;      // synchronous irq processing lock

static spinlock_t  adapter_lock = SPIN_LOCK_UNLOCKED;
                                          // adapter interrupt lock
static psw_t      io_sync_wait;           // wait PSW for sync IO, prot. by sync_isc
static int        cons_dev          = -1; // identify console device
static int        init_IRQ_complete = 0;
static int        cio_show_msg      = 0;
static schib_t   *p_init_schib      = NULL;
static irb_t     *p_init_irb        = NULL;
static __u64      irq_IPL_TOD;
static adapter_int_handler_t adapter_handler   = NULL;

/* for use of debug feature */
debug_info_t *cio_debug_msg_id = NULL;  
debug_info_t *cio_debug_trace_id = NULL;
debug_info_t *cio_debug_crw_id = NULL;
int cio_debug_initialized = 0;

static void init_IRQ_handler( int irq, void *dev_id, struct pt_regs *regs);
static void s390_process_subchannels( void);
static void s390_device_recognition_all( void);
static void s390_device_recognition_irq( int irq);
static void s390_redo_validation(void);
static int  s390_validate_subchannel( int irq, int enable);
static int  s390_SenseID( int irq, senseid_t *sid, __u8 lpm);
static int  s390_SetPGID( int irq, __u8 lpm, pgid_t *pgid);
static int  s390_SensePGID( int irq, __u8 lpm, pgid_t *pgid);
static int  s390_process_IRQ( unsigned int irq );
static int  enable_subchannel( unsigned int irq);
static int  disable_subchannel( unsigned int irq);

static int chan_proc_init( void );

static inline void do_adapter_IO( __u32 intparm );

int  s390_DevicePathVerification( int irq, __u8 domask );
int  s390_register_adapter_interrupt( adapter_int_handler_t handler );
int  s390_unregister_adapter_interrupt( adapter_int_handler_t handler );

extern int  do_none(unsigned int irq, int cpu, struct pt_regs * regs);
extern int  enable_none(unsigned int irq);
extern int  disable_none(unsigned int irq);

asmlinkage void do_IRQ( struct pt_regs regs );

#define MAX_CIO_PROCFS_ENTRIES 0x300
/* magic number; we want to have some room to spare */

int cio_procfs_device_create(int devno);
int cio_procfs_device_remove(int devno);
int cio_procfs_device_purge(void);

int cio_notoper_msg = 1;
int cio_proc_devinfo = 0; /* switch off the /proc/deviceinfo/ stuff by default
			     until problems are dealt with */

unsigned long s390_irq_count[NR_CPUS]; /* trace how many irqs have occured per cpu... */
int cio_count_irqs = 1;       /* toggle use here... */

/* 
 * "Blacklisting" of certain devices:
 * Device numbers given in the commandline as blacklist=... won't be known to Linux
 * These can be single devices or ranges of devices
 *
 * Introduced by Cornelia Huck <cohuck@de.ibm.com>
 * Most of it shamelessly taken from dasd.c 
 */

typedef struct dev_blacklist_range_t {
     struct dev_blacklist_range_t *next;  /* next range in list */
     unsigned int from;                   /* beginning of range */
     unsigned int to;                     /* end of range */
     int  kmalloced;	
    
} dev_blacklist_range_t;

static dev_blacklist_range_t *dev_blacklist_range_head = NULL; /* Anchor for list of ranges */
static dev_blacklist_range_t *dev_blacklist_unused_head = NULL;

static spinlock_t blacklist_lock = SPIN_LOCK_UNLOCKED;
static int nr_blacklisted_ranges = 0;

/* Handling of the blacklist ranges */

static inline void blacklist_range_destroy( dev_blacklist_range_t *range,int locked )
{
	 long flags;
	 
	 if(!locked)
		 spin_lock_irqsave( &blacklist_lock, flags ); 
	 if(!remove_from_list((list **)&dev_blacklist_range_head,(list *)range))
		 BUG();
	 nr_blacklisted_ranges--;
	 if(range->kmalloced)
		 kfree(range);
	 else
		 add_to_list((list **)&dev_blacklist_unused_head,(list *)range); 
	 if(!locked)
		 spin_unlock_irqrestore( &blacklist_lock, flags );
}



/* 
 * Function: blacklist_range_add
 * Creates a range from the specified arguments and appends it to the list of
 * blacklisted devices
 */

static inline dev_blacklist_range_t *blacklist_range_add( int from, int to,int locked)
{
 
     dev_blacklist_range_t *range = NULL;
     unsigned long flags;

     if (to && (from>to)) {
	     printk(KERN_WARNING "Invalid blacklist range %x to %x, skipping\n", from, to);
	     return NULL;
     }
     if(!locked)
	     spin_lock_irqsave( &blacklist_lock, flags );
     if(dev_blacklist_unused_head)
	     range=(dev_blacklist_range_t *)
		     remove_listhead((list **)&dev_blacklist_unused_head);
     else if (init_IRQ_complete) {
	     if((range = ( dev_blacklist_range_t *) 
		 kmalloc( sizeof( dev_blacklist_range_t ), GFP_KERNEL)))
		     range->kmalloced=1;
     } else {
	     if((range = ( dev_blacklist_range_t *) 
		 alloc_bootmem( sizeof( dev_blacklist_range_t ) )))
		     range->kmalloced=0;
     }
     if (range)
     {
	     add_to_list((list **)&dev_blacklist_range_head,(list *)range);
	     range->from = from;
	     if (to == 0) {           /* only a single device is given */
		     range->to = from;
	     } else {
		     range->to = to;
	     }
	     nr_blacklisted_ranges++;
     }
     if(!locked)
	     spin_unlock_irqrestore( &blacklist_lock, flags );
     return range;
}

/* 
 * Function: blacklist_range_remove
 * Removes a range from the blacklist chain
 */

static inline void blacklist_range_remove( int from, int to )
{
     dev_blacklist_range_t *temp;
     long flags;

     spin_lock_irqsave( &blacklist_lock, flags );
     for ( temp = dev_blacklist_range_head; 
	   (temp->from != from) && (temp->to != to);
	   temp = temp->next );
     blacklist_range_destroy( temp,1 );
     spin_unlock_irqrestore( &blacklist_lock, flags );
}

/* Parsing the commandline for blacklist parameters */

/* 
 * Variable to hold the blacklisted devices given by the parameter line
 * blacklist=...
 */
char *blacklist[256] = {NULL, };

/*
 * Get the blacklist=... items from the parameter line
 */

static void blacklist_split_parm_string (char *str)
{
	char *tmp = str;
	int count = 0;
	do {
		char *end;
		int len;
		end = strchr (tmp, ',');
		if (end == NULL) {
			len = strlen (tmp) + 1;
		} else {
			len = (long) end - (long) tmp + 1;
			*end = '\0';
			end++;
		}
		blacklist[count] = alloc_bootmem (len * sizeof (char) );
		if (blacklist == NULL) {
			printk (KERN_WARNING "can't store blacklist= parameter no %d\n", count + 1);
			break;
		}
		memset (blacklist[count], 0, len * sizeof (char));
		memcpy (blacklist[count], tmp, len * sizeof (char));
		count++;
		tmp = end;
	} while (tmp != NULL && *tmp != '\0');
}

/*
 * The blacklist parameters as one concatenated string
 */

static char blacklist_parm_string[1024] __initdata = {0,};


/* 
 * function: blacklist_strtoul
 * Strip leading '0x' and interpret the values as Hex
 */
static inline int blacklist_strtoul (char *str, char **stra)
{
	char *temp = str;
	int val;
	if (*temp == '0') {
		temp++;		                /* strip leading zero */
		if (*temp == 'x')
			temp++;	                /* strip leading x */
	}
	val = simple_strtoul (temp, &temp, 16);	/* interpret anything as hex */
	*stra = temp;
	return val;
}

/*
 * Function: blacklist_parse
 * Parse the parameters given to blacklist=... 
 * Add the blacklisted devices to the blacklist chain
 */

static inline void blacklist_parse( char **str )
{
     char *temp;
     int from, to;

     while (*str) {
	  temp = *str;
	  from = 0;
	  to = 0;
	  
	  from = blacklist_strtoul( temp, &temp );
	  if (*temp == '-') {
	       temp++;
	       to = blacklist_strtoul( temp, &temp );
	  }
	  if (!blacklist_range_add( from, to,0 )) {
	       printk( KERN_WARNING "Blacklisting range from %X to %X failed!\n", from, to);
	  }
#ifdef CONFIG_DEBUG_IO
	  printk( "Blacklisted range from %X to %X\n", from, to );
#endif
	  str++;
     }
}


/*
 * Initialisation of blacklist 
 */

void __init blacklist_init( void )
{
#ifdef CONFIG_DEBUG_IO     
     printk( "Reading blacklist...\n");
#endif
     if (cio_debug_initialized)
	     debug_sprintf_event(cio_debug_msg_id, 6,
				 "Reading blacklist\n");
     blacklist_split_parm_string( blacklist_parm_string );
     blacklist_parse( blacklist );
}


/*
 * Get all the blacklist parameters from parameter line
 */

void __init blacklist_setup (char *str, int *ints)
{
	int len = strlen (blacklist_parm_string);
	if (len != 0) {
		strcat (blacklist_parm_string, ",");
	}
	strcat (blacklist_parm_string, str);
}

int __init blacklist_call_setup (char *str)
{
        int dummy;
#ifdef CONFIG_DEBUG_IO
	printk( "Reading blacklist parameters...\n" );
#endif
	if (cio_debug_initialized)
		debug_sprintf_event(cio_debug_msg_id, 6,
				    "Reading blacklist parameters\n");
        blacklist_setup(str,&dummy);
	
	blacklist_init(); /* Blacklist ranges must be ready when device recognition starts */
       
	return 1;
}

__setup ("cio_ignore=", blacklist_call_setup);

/* Checking if devices are blacklisted */

/*
 * Function: is_blacklisted
 * Returns 1 if the given devicenumber can be found in the blacklist, otherwise 0.
 */

static inline int is_blacklisted( int devno )
{
     dev_blacklist_range_t *temp;
     long flags;
     int retval=0;

     if (dev_blacklist_range_head == NULL) {  
	  /* no blacklist */
	  return 0;
     }

     spin_lock_irqsave( &blacklist_lock, flags ); 
     temp = dev_blacklist_range_head;
     while (temp) {
	  if ((temp->from <= devno) && (temp->to >= devno)) {
		  retval=1;                      /* Deviceno is blacklisted */
		  break;
	  }
	  temp = temp->next;
     }
     spin_unlock_irqrestore( &blacklist_lock, flags );
     return retval;
}

/*
 * Function: blacklist_free_all_ranges
 * set all blacklisted devices free...
 */

void blacklist_free_all_ranges(void) 
{
	dev_blacklist_range_t *tmp = dev_blacklist_range_head;
	unsigned long flags;

	spin_lock_irqsave( &blacklist_lock, flags ); 
	while (tmp) {
		blacklist_range_destroy(tmp,1);
		tmp = dev_blacklist_range_head;
	}
	spin_unlock_irqrestore( &blacklist_lock, flags );
}

/*
 * Function: blacklist_parse_proc_parameters
 * parse the stuff which is piped to /proc/cio_ignore
 */
void blacklist_parse_proc_parameters(char *buf)
{
	char *tmp;
	int i;
	char *end;
	int len = -1;
	char *param;
	int from = 0;
	int to = 0;
	int changed = 0;
	dev_blacklist_range_t *range, *temp;
	long flags;
	int err = 0;

	tmp = buf;
	if (strstr(tmp, "free ")) {
		for (i=0; i<5; i++) {
			tmp++;
		}
		if (strstr(tmp, "all")) {
			blacklist_free_all_ranges();
			s390_redo_validation();
		} else {
			while (tmp != NULL) {
				end = strchr(tmp, ',');
				if (end == NULL) {
					len = strlen(tmp) + 1;
				} else {
					len = (long)end - (long) tmp + 1;
					*end = '\0';
					end++;
				}
				param =  (char*) kmalloc(len * sizeof(char) + 1, GFP_KERNEL);
				strncpy(param, (const char *) tmp, len);
				tmp = end;
				from = blacklist_strtoul(param, &param);
				if (*param == '-') {
					param++;
					to = blacklist_strtoul(param, &param);
				} else {
					to = from;
				}
				spin_lock_irqsave( &blacklist_lock, flags ); 
				range = dev_blacklist_range_head;
				while (range != NULL) {
					temp = range->next;
					if ((from <= range->from) && (to >= range->to)) {
						blacklist_range_destroy(range,1);
						changed = 1;
					} else if ((from <= range->from) && (to>=range->from) && (to < range->to)) {
						blacklist_range_add(to+1, range->to,1);
						blacklist_range_destroy(range,1);
						changed = 1;
					} else if ((from > range->from) && (from<=range->to) && (to >= range->to)) {
						blacklist_range_add(range->from, from-1,1);
						blacklist_range_destroy(range,1);
						changed = 1;
					} else if ((from > range->from) && (to < range->to)) {
						blacklist_range_add(range->from, from-1,1);
						blacklist_range_add(to+1, range->to,1);
						blacklist_range_destroy(range,1);
						changed = 1;
					}
					range = temp;
				}
				spin_unlock_irqrestore( &blacklist_lock, flags );
				kfree(param);
			}
			if (changed)
				s390_redo_validation();
		}
	} else if (strstr(tmp, "add ")) {
		for (i=0;i<4;i++){
			tmp++;
		}
		while (tmp != NULL) {
			end = strchr(tmp, ',');
			if (end == NULL) {
				len = strlen(tmp) + 1;
			} else {
				len = (long)end - (long) tmp + 1;
				*end = '\0';
				end++;
			}
			param =  (char*) kmalloc(len * sizeof(char) + 1, GFP_KERNEL);
			strncpy(param, (const char *) tmp, len);
			tmp = end;
			from = blacklist_strtoul(param, &param);
			if (*param == '-') {
				param++;
				to = blacklist_strtoul(param, &param);
			} else {
				to = from;
			}
			spin_lock_irqsave( &blacklist_lock, flags ); 
			
			/*
			 * Don't allow for already known devices to be
			 * blacklisted
			 * The criterion is a bit dumb, devices which once were
			 * there but are already gone are also caught...
			 */
			
			err = 0;
			for (i=0; i<=highest_subchannel; i++) {
				if (ioinfo[i]!=INVALID_STORAGE_AREA) {
					if (   (ioinfo[i]->schib.pmcw.dev >= from)
					    && (ioinfo[i]->schib.pmcw.dev <= to)  ) {
						printk(KERN_WARNING "cio_ignore: Won't blacklist "
						       "already known devices, skipping range "
						       "%x to %x\n", from, to);
						err = 1;
						break;
					}
				}
			}

			/*
			 * Note: We allow for overlapping ranges here, 
			 * since the user might specify overlapping ranges
			 * and we walk through all ranges when freeing anyway.
			 */

			if (!err) 
				blacklist_range_add(from, to, 1);
			
			spin_unlock_irqrestore( &blacklist_lock, flags );
			kfree(param);
		}

	} else {
		printk("cio_ignore: Parse error; try using 'free all|<devno-range>,<devno-range>,...'\n");
		printk("or 'add <devno-range>,<devno-range>,...'\n");
	}
}

/* End of blacklist handling */


void s390_displayhex(char *str,void *ptr,s32 cnt);
void s390_displayhex2(char *str, void *ptr, s32 cnt, int level);

void s390_displayhex(char *str,void *ptr,s32 cnt)
{
	s32	cnt1,cnt2,maxcnt2;
	u32	*currptr=(__u32 *)ptr;

	printk("\n%s\n",str);

	for(cnt1=0;cnt1<cnt;cnt1+=16)
	{
		printk("%08lX ",(unsigned long)currptr);
		maxcnt2=cnt-cnt1;
		if(maxcnt2>16)
			maxcnt2=16;
		for(cnt2=0;cnt2<maxcnt2;cnt2+=4)
			printk("%08X ",*currptr++);
		printk("\n");
	}
}

void s390_displayhex2(char *str, void *ptr, s32 cnt, int level)
{
	s32 cnt1, cnt2, maxcnt2;
	u32 *currptr = (__u32 *)ptr;
	char buffer[cnt*12];

	debug_sprintf_event(cio_debug_msg_id, level, "%s\n", str);

	for (cnt1 = 0; cnt1<cnt; cnt1+=16) {
		sprintf(buffer, "%08lX ", (unsigned long)currptr);
		maxcnt2 = cnt - cnt1;
		if (maxcnt2 > 16)
			maxcnt2 = 16;
		for (cnt2 = 0; cnt2 < maxcnt2; cnt2 += 4)
			sprintf(buffer, "%08X ", *currptr++);
	}
	debug_sprintf_event(cio_debug_msg_id, level, "%s\n",buffer);
}

static int __init cio_setup( char *parm )
{
	if ( !strcmp( parm, "yes") )
	{
		cio_show_msg = 1;
	}
	else if ( !strcmp( parm, "no") )
	{
		cio_show_msg = 0;
	}
	else
	{
		printk( "cio_setup : invalid cio_msg parameter '%s'", parm);

	} /* endif */

	return 1;
}

__setup("cio_msg=", cio_setup);

static int __init cio_notoper_setup(char *parm)
{
	if (!strcmp(parm, "yes")) {
		cio_notoper_msg = 1;
	} else if (!strcmp(parm, "no")) {
		cio_notoper_msg = 0;
	} else {
		printk("cio_notoper_setup: invalid cio_notoper_msg parameter '%s'", parm);
	}

	return 1;
}
	
__setup("cio_notoper_msg=", cio_notoper_setup);

static int __init cio_proc_devinfo_setup(char *parm)
{
	if (!strcmp(parm, "yes")) {
		cio_proc_devinfo = 1;
	} else if (!strcmp(parm, "no")) {
		cio_proc_devinfo = 0;
	} else {
		printk("cio_proc_devinfo_setup: invalid parameter '%s'\n",parm);
	}

	return 1;
}

__setup("cio_proc_devinfo=", cio_proc_devinfo_setup);

/*
 * register for adapter interrupts
 *
 * With HiperSockets the zSeries architecture provides for
 *  means of adapter interrups, pseudo I/O interrupts that are
 *  not tied to an I/O subchannel, but to an adapter. However,
 *  it doesn't disclose the info how to enable/disable them, but
 *  to recognize them only. Perhaps we should consider them
 *  being shared interrupts, and thus build a linked list
 *  of adapter handlers ... to be evaluated ...
 */
int  s390_register_adapter_interrupt( adapter_int_handler_t handler )
{
	int ret = 0;
	
	if (cio_debug_initialized)
		debug_text_event(cio_debug_trace_id, 4, "rgaint");

	spin_lock( &adapter_lock );

	if ( handler == NULL )
		ret = -EINVAL;
	else if ( adapter_handler )
		ret = -EBUSY;
	else
		adapter_handler = handler;
  	
	spin_unlock( &adapter_lock ); 	

	return( ret);
}


int  s390_unregister_adapter_interrupt( adapter_int_handler_t handler )
{
	int ret = 0;
	
	if (cio_debug_initialized)
		debug_text_event(cio_debug_trace_id, 4, "urgaint");

	spin_lock( &adapter_lock ); 	

	if ( handler == NULL )
		ret = -EINVAL;
	else if ( handler != adapter_handler )
		ret = -EINVAL;
	else
		adapter_handler = NULL;
  	
	spin_unlock( &adapter_lock ); 	

	return( ret);
}

static inline void do_adapter_IO( __u32 intparm )
{
	if (cio_debug_initialized)
		debug_text_event(cio_debug_trace_id, 4, "doaio");
	
	spin_lock( &adapter_lock ); 	

	if ( adapter_handler )
		(*adapter_handler)( intparm );

	spin_unlock( &adapter_lock );

	return;  	
}

/*
 * Note : internal use of irqflags SA_PROBE for NOT path grouping 
 *
 */
int s390_request_irq_special( int                      irq,
                              io_handler_func_t        io_handler,
                              not_oper_handler_func_t  not_oper_handler,
                              unsigned long            irqflags,
                              const char              *devname,
                              void                    *dev_id)
{
	int		retval = 0;
	unsigned long	flags;
	char            dbf_txt[15];
	int             retry;

	if (irq >= __MAX_SUBCHANNELS)
		return -EINVAL;

	if ( !io_handler || !dev_id )
		return -EINVAL;

	if ( ioinfo[irq] == INVALID_STORAGE_AREA )
		return -ENODEV;


	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "reqsp");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	/*
	 * The following block of code has to be executed atomically
	 */
	s390irq_spin_lock_irqsave( irq, flags);

	if ( !ioinfo[irq]->ui.flags.ready )
	{
		retry = 5;
		
		ioinfo[irq]->irq_desc.handler = io_handler;
		ioinfo[irq]->irq_desc.name    = devname;
		ioinfo[irq]->irq_desc.dev_id  = dev_id;
		ioinfo[irq]->ui.flags.ready   = 1;
		
		
		do {
			retval = enable_subchannel(irq);
			if (retval) {
				ioinfo[irq]->ui.flags.ready = 0;
				break;
			}
			
			stsch(irq,&ioinfo[irq]->schib);
			if (ioinfo[irq]->schib.pmcw.ena) 
				retry = 0;
			else
				retry--;

		} while (retry);
	}
	else
	{
		/*
		 *  interrupt already owned, and shared interrupts
		 *   aren't supported on S/390.
		 */
		retval = -EBUSY;

	} /* endif */

	s390irq_spin_unlock_irqrestore(irq,flags);



	if ( retval == 0 )
	{
		if ( !(irqflags & SA_PROBE))
			s390_DevicePathVerification( irq, 0 );

		ioinfo[irq]->ui.flags.newreq = 1;
		ioinfo[irq]->nopfunc         = not_oper_handler;  	
	}

	return retval;
}


int s390_request_irq( unsigned int   irq,
                      void           (*handler)(int, void *, struct pt_regs *),
                      unsigned long  irqflags,
                      const char    *devname,
                      void          *dev_id)
{
	int ret;

	ret = s390_request_irq_special( irq,
                                   (io_handler_func_t)handler,
                                   NULL,
                                   irqflags,
                                   devname,
                                   dev_id);

	if ( ret == 0 )
	{
		ioinfo[irq]->ui.flags.newreq = 0;

	} /* endif */

	return( ret);
}

void s390_free_irq(unsigned int irq, void *dev_id)
{
	unsigned long flags;
	int          ret;

	unsigned int count = 0;
	char dbf_txt[15];

	if ( irq >= __MAX_SUBCHANNELS || ioinfo[irq] == INVALID_STORAGE_AREA )
	{
		return;

	} /* endif */

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 2, "free");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 2, dbf_txt);
	}

	s390irq_spin_lock_irqsave( irq, flags);

#ifdef  CONFIG_KERNEL_DEBUG
	if ( irq != cons_dev )
	{
		printk("Trying to free IRQ%d\n",irq);

	} /* endif */
#endif
	if (cio_debug_initialized)
		debug_sprintf_event(cio_debug_msg_id, 2, "Trying to free IRQ %d\n", irq);

	/*
	 * disable the device and reset all IRQ info if
	 *  the IRQ is actually owned by the handler ...
	 */
	if ( ioinfo[irq]->ui.flags.ready )
	{
		if ( dev_id == ioinfo[irq]->irq_desc.dev_id  )
		{
			/* start deregister */
			ioinfo[irq]->ui.flags.unready = 1;

			/*
			 * Try to stop IO first...
			 * ... it seems disable_subchannel is sometimes
			 * successfully called with IO still pending.
			 */
			halt_IO( irq,
				 0xC8C1D3E3,
				 DOIO_WAIT_FOR_INTERRUPT );

			do
			{
				ret = disable_subchannel( irq);

				count++;

				if ( ret == -EBUSY )
				{
					int iret;

					/*
					 * kill it !
					 * ... we first try sync and eventually
					 *  try terminating the current I/O by
					 *  an async request, twice halt, then
					 *  clear.
					 */
					if ( count < 2 )
					{              	
						iret = halt_IO( irq,
						                0xC8C1D3E3,
						                DOIO_WAIT_FOR_INTERRUPT );
   	
						if ( iret == -EBUSY )
						{
							halt_IO( irq, 0xC8C1D3E3, 0);
							s390irq_spin_unlock_irqrestore( irq, flags);
							udelay( 200000 ); /* 200 ms */
							s390irq_spin_lock_irqsave( irq, flags);

						} /* endif */
					}
					else
					{
						iret = clear_IO( irq,
						                 0x40C3D3D9,
						                 DOIO_WAIT_FOR_INTERRUPT );
   	
						if ( iret == -EBUSY )
						{
							clear_IO( irq, 0xC8C1D3E3, 0);
							s390irq_spin_unlock_irqrestore( irq, flags);
							udelay( 1000000 ); /* 1000 ms */
							s390irq_spin_lock_irqsave( irq, flags);

						} /* endif */

					} /* endif */

					if ( count == 2 )
					{
						/* give it a very last try ... */
						disable_subchannel( irq);

						if ( ioinfo[irq]->ui.flags.busy )
					        {
							printk( KERN_CRIT"free_irq(%04X) "
							       "- device %04X busy, retry "
							       "count exceeded\n",
								irq,
								ioinfo[irq]->devstat.devno);
							if (cio_debug_initialized)
								debug_sprintf_event(cio_debug_msg_id, 0,
										    "free_irq(%04X) - device %04X busy, retry count exceeded\n",
										    irq, ioinfo[irq]->devstat.devno);

						} /* endif */
						
						break; /* sigh, let's give up ... */

					} /* endif */

				} /* endif */
		
			} while ( ret == -EBUSY );

			ioinfo[irq]->ui.flags.ready   = 0;
			ioinfo[irq]->ui.flags.unready = 0; /* deregister ended */

			ioinfo[irq]->nopfunc = NULL;

			s390irq_spin_unlock_irqrestore( irq, flags);
		}
		else
		{
			s390irq_spin_unlock_irqrestore( irq, flags);

			printk( "free_irq(%04X) : error, "
			        "dev_id does not match !\n", irq);
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_msg_id, 0, 
						    "free_irq(%04X) : error, dev_id does not match !\n", irq);

		} /* endif */

	}
	else
	{
		s390irq_spin_unlock_irqrestore( irq, flags);

		printk( "free_irq(%04X) : error, "
		        "no action block ... !\n", irq);
		if (cio_debug_initialized)
			debug_sprintf_event(cio_debug_msg_id, 0,
					    "free_irq(%04X) : error, no action block ... !\n", irq);

	} /* endif */

}

/*
 * Generic enable/disable code
 */
int disable_irq(unsigned int irq)
{
	unsigned long flags;
	int           ret;
	char dbf_txt[15];

	SANITY_CHECK(irq);

	if ( !ioinfo[irq]->ui.flags.ready )
		return -ENODEV;

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "disirq");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	s390irq_spin_lock_irqsave(irq, flags);
	ret = disable_subchannel(irq);
	s390irq_spin_unlock_irqrestore(irq, flags);

	synchronize_irq();

	return( ret);
}

int enable_irq(unsigned int irq)
{
	unsigned long flags;
	int           ret;
	char dbf_txt[15];

	SANITY_CHECK(irq);

	if ( !ioinfo[irq]->ui.flags.ready )
		return -ENODEV;

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "enirq");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	s390irq_spin_lock_irqsave(irq, flags);
	ret = enable_subchannel(irq);
	s390irq_spin_unlock_irqrestore(irq, flags);

	return(ret);
}

/*
 * Enable IRQ by modifying the subchannel
 */
static int enable_subchannel( unsigned int irq)
{
	int   ret = 0;
	int   ccode;
	int   retry = 5;
	char dbf_txt[15];

	SANITY_CHECK(irq);

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 2, "ensch");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 2, dbf_txt);
	}

	/*
	 * If a previous disable request is pending we reset it. However, this
	 *  status implies that the device may (still) be not-operational.
	 */
	if (  ioinfo[irq]->ui.flags.d_disable )
	{
		ioinfo[irq]->ui.flags.d_disable = 0;
		ret                             = 0;
	}
	else
	{
		ccode = stsch(irq, &(ioinfo[irq]->schib) );

		if ( ccode )
		{
			ret = -ENODEV;
		}
		else
		{
			ioinfo[irq]->schib.pmcw.ena = 1;

			if ( irq == cons_dev )
			{
				ioinfo[irq]->schib.pmcw.isc = 7;
			}
			else	
			{
				ioinfo[irq]->schib.pmcw.isc = 3;

			} /* endif */

			do
			{
				ccode = msch( irq, &(ioinfo[irq]->schib) );

				switch (ccode) {
				case 0: /* ok */
					ret = 0;
					retry = 0;
					break;

				case 1: /* status pending */

					ioinfo[irq]->ui.flags.s_pend = 1;
					s390_process_IRQ( irq );
					ioinfo[irq]->ui.flags.s_pend = 0;

					ret = -EIO;    /* might be overwritten */
					               /* ... on re-driving    */
					               /* ... the msch()       */
					retry--;
					break;

				case 2: /* busy */
					udelay(100);	/* allow for recovery */
					ret = -EBUSY;
					retry--;
					break;

				case 3: /* not oper */
					ioinfo[irq]->ui.flags.oper = 0;
					retry = 0;
					ret = -ENODEV;
					break;
				}

			} while ( retry );

		} /* endif */

	}  /* endif */

	return( ret );
}


/*
 * Disable IRQ by modifying the subchannel
 */
static int disable_subchannel( unsigned int irq)
{
	int  cc;          /* condition code */
	int  ret = 0;     /* function return value */
	int  retry = 5;
	char dbf_txt[15];

	SANITY_CHECK(irq);
	
	if ( ioinfo[irq]->ui.flags.busy )
	{
		/*
		 * the disable function must not be called while there are
		 *  requests pending for completion !
		 */
		ret = -EBUSY;
	}
	else
	{
		if (cio_debug_initialized) {
			debug_text_event(cio_debug_trace_id, 2, "dissch");
			sprintf(dbf_txt, "%x", irq);
			debug_text_event(cio_debug_trace_id, 2, dbf_txt);
		}
		
		/*
		 * If device isn't operational we have to perform delayed
		 *  disabling when the next interrupt occurs - unless the
		 *  irq is re-requested prior to the interrupt to occur.
		 */
		cc = stsch(irq, &(ioinfo[irq]->schib) );

		if ( cc == 3 )
		{
			ioinfo[irq]->ui.flags.oper      = 0;
			ioinfo[irq]->ui.flags.d_disable = 1;

			ret = 0;
		}
		else // cc == 0
		{
			ioinfo[irq]->schib.pmcw.ena = 0;

			do
			{
				cc = msch( irq, &(ioinfo[irq]->schib) );

				switch (cc) {
				case 0: /* ok */
					retry = 0;
					ret = 0;   
					break;

				case 1: /* status pending */
					ioinfo[irq]->ui.flags.s_pend = 1;
					s390_process_IRQ( irq );
					ioinfo[irq]->ui.flags.s_pend = 0;

					ret = -EIO; /* might be overwritten  */
					            /* ... on re-driving the */
					            /* ... msch() call       */
					retry--;
					break;

				case 2: /* busy; this should not happen! */
					printk( KERN_CRIT"disable_subchannel(%04X) "
					        "- unexpected busy condition for "
						"device %04X received !\n",
					        irq,
					        ioinfo[irq]->devstat.devno);
					if (cio_debug_initialized)
						debug_sprintf_event(cio_debug_msg_id, 0, 
								    "disable_subchannel(%04X) - unexpected busy condition for device %04X received !\n",
								    irq, ioinfo[irq]->devstat.devno);
					retry = 0;
					ret = -EBUSY;
					break;

				case 3: /* not oper */
					/*
					 * should hardly occur ?!
					 */
					ioinfo[irq]->ui.flags.oper      = 0;
					ioinfo[irq]->ui.flags.d_disable = 1;
					retry = 0;

					ret = 0; /* if the device has gone we */
					         /* ... don't need to disable */
					         /* ... it anymore !          */
					break;

				} /* endswitch */

			} while ( retry );

		} /* endif */

	} /* endif */

	return( ret);
}


void s390_init_IRQ( void )
{
	unsigned long flags;     /* PSW flags */
	long          cr6 __attribute__ ((aligned (8)));

	asm volatile ("STCK %0" : "=m" (irq_IPL_TOD));

	p_init_schib = alloc_bootmem_low( sizeof(schib_t));
	p_init_irb   = alloc_bootmem_low( sizeof(irb_t));
	
	
	/*
	 * As we don't know about the calling environment
	 *  we assure running disabled. Before leaving the
	 *  function we resestablish the old environment.
	 *
	 * Note : as we don't need a system wide lock, therefore
	 *        we shouldn't use cli(), but __cli() as this
	 *        affects the current CPU only.
	 */
	__save_flags(flags);
	__cli();

	/*
	 * disable all interrupts
	 */
	cr6 = 0;
	__ctl_load( cr6, 6, 6);

	s390_process_subchannels();

	if (cio_count_irqs) {
		int i;
		for (i=0; i<NR_CPUS; i++) 
			s390_irq_count[i]=0;
	}

	/*
	 * enable default I/O-interrupt sublass 3
	 */
	cr6 = 0x10000000;
	__ctl_load( cr6, 6, 6);

	s390_device_recognition_all();

	init_IRQ_complete = 1;

	__restore_flags(flags);

	return;
}


/*
 * dummy handler, used during init_IRQ() processing for compatibility only
 */
void  init_IRQ_handler( int irq, void *dev_id, struct pt_regs *regs)
{
   /* this is a dummy handler only ... */
}


int s390_start_IO( int            irq,      /* IRQ */
                   ccw1_t        *cpa,      /* logical channel prog addr */
                   unsigned long  user_intparm,  /* interruption parameter */
                   __u8           lpm,      /* logical path mask */
                   unsigned long  flag)     /* flags */
{
	int            ccode;
	int            ret = 0;
	char buffer[80];
	char dbf_txt[15];

	SANITY_CHECK(irq);

	/*
	 * The flag usage is mutal exclusive ...
	 */
	if (    (flag & DOIO_EARLY_NOTIFICATION)
	     && (flag & DOIO_REPORT_ALL     ) )
	{
		return( -EINVAL );

	} /* endif */

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "stIO");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	/*
	 * setup ORB
	 */
  	ioinfo[irq]->orb.intparm = (__u32)(long)&ioinfo[irq]->u_intparm;
	ioinfo[irq]->orb.fmt     = 1;

	ioinfo[irq]->orb.pfch = !(flag & DOIO_DENY_PREFETCH);
	ioinfo[irq]->orb.spnd =  (flag & DOIO_ALLOW_SUSPEND ? TRUE : FALSE);
	ioinfo[irq]->orb.ssic =  (    (flag & DOIO_ALLOW_SUSPEND )
	                           && (flag & DOIO_SUPPRESS_INTER) );

	if ( flag & DOIO_VALID_LPM )
	{
		ioinfo[irq]->orb.lpm = lpm;
	}
	else
	{
		ioinfo[irq]->orb.lpm = ioinfo[irq]->opm;

	} /* endif */

#ifdef CONFIG_ARCH_S390X
	/* 
	 * for 64 bit we always support 64 bit IDAWs with 4k page size only
	 */
	ioinfo[irq]->orb.c64 = 1;
	ioinfo[irq]->orb.i2k = 0;
#endif

	ioinfo[irq]->orb.cpa = (__u32)virt_to_phys( cpa);

	/*
	 * If sync processing was requested we lock the sync ISC, modify the
	 *  device to present interrupts for this ISC only and switch the
	 *  CPU to handle this ISC + the console ISC exclusively.
	 */
	if ( flag & DOIO_WAIT_FOR_INTERRUPT )
	{
		ret = enable_cpu_sync_isc( irq);
	
		if ( ret )
		{
			return( ret);
		}

	} /* endif */

	if ( flag & DOIO_DONT_CALL_INTHDLR )
	{
		ioinfo[irq]->ui.flags.repnone = 1;

	} /* endif */

	/*
	 * Issue "Start subchannel" and process condition code
	 */
	ccode = ssch( irq, &(ioinfo[irq]->orb) );

	switch ( ccode ) {
	case 0:

		if ( !ioinfo[irq]->ui.flags.w4sense )
		{
			/*
			 * init the device driver specific devstat irb area
			 *
			 * Note : don´t clear saved irb info in case of sense !
			 */
			memset( &((devstat_t *)ioinfo[irq]->irq_desc.dev_id)->ii.irb,
				'\0', sizeof( irb_t) );
		} /* endif */

		memset( &ioinfo[irq]->devstat.ii.irb,
		        '\0',
		        sizeof( irb_t) );

		/*
		 * initialize device status information
		 */
		ioinfo[irq]->ui.flags.busy   = 1;
		ioinfo[irq]->ui.flags.doio   = 1;

		ioinfo[irq]->u_intparm       = user_intparm;
		ioinfo[irq]->devstat.cstat   = 0;
		ioinfo[irq]->devstat.dstat   = 0;
		ioinfo[irq]->devstat.lpum    = 0;
		ioinfo[irq]->devstat.flag    = DEVSTAT_START_FUNCTION;
		ioinfo[irq]->devstat.scnt    = 0;

		ioinfo[irq]->ui.flags.fast   = 0;
		ioinfo[irq]->ui.flags.repall = 0;

		/*
		 * Check for either early (FAST) notification requests
		 *  or if we are to return all interrupt info.
		 * Default is to call IRQ handler at secondary status only
		 */
		if ( flag & DOIO_EARLY_NOTIFICATION )
		{
			ioinfo[irq]->ui.flags.fast = 1;
		}
		else if ( flag & DOIO_REPORT_ALL )
		{
			ioinfo[irq]->ui.flags.repall = 1;

		} /* endif */

		ioinfo[irq]->ulpm = ioinfo[irq]->orb.lpm;

		/*
		 * If synchronous I/O processing is requested, we have
		 *  to wait for the corresponding interrupt to occur by
		 *  polling the interrupt condition. However, as multiple
		 *  interrupts may be outstanding, we must not just wait
		 *  for the first interrupt, but must poll until ours
		 *  pops up.
		 */
		if ( flag & DOIO_WAIT_FOR_INTERRUPT )
		{
			psw_t            io_new_psw;
			int              ccode;
			uint64_t         time_start;    	
			uint64_t         time_curr;    	

			int              ready    = 0;
			int              io_sub   = -1;
			struct _lowcore *lc       = NULL;
			int              do_retry = 1;

			/*
			 * We shouldn't perform a TPI loop, waiting for an
			 *  interrupt to occur, but should load a WAIT PSW
			 *  instead. Otherwise we may keep the channel subsystem
			 *  busy, not able to present the interrupt. When our
			 *  sync. interrupt arrived we reset the I/O old PSW to
			 *  its original value.
			 */
			memcpy( &io_new_psw, &lc->io_new_psw, sizeof(psw_t));

			ccode = iac();

			switch (ccode) {
			case 0:  		// primary-space
				io_sync_wait.mask =   _IO_PSW_MASK
				                    | _PSW_PRIM_SPACE_MODE
				                    | _PSW_IO_WAIT;
				break;
			case 1:			// secondary-space
				io_sync_wait.mask =   _IO_PSW_MASK
				                    | _PSW_SEC_SPACE_MODE
				                    | _PSW_IO_WAIT;
				break;
			case 2:			// access-register
				io_sync_wait.mask =   _IO_PSW_MASK
				                    | _PSW_ACC_REG_MODE
				                    | _PSW_IO_WAIT;
				break;
			case 3:			// home-space	
				io_sync_wait.mask =   _IO_PSW_MASK
				                    | _PSW_HOME_SPACE_MODE
				                    | _PSW_IO_WAIT;
				break;
			default:
				panic( "start_IO() : unexpected "
				       "address-space-control %d\n",
				       ccode);
				break;
			} /* endswitch */

			io_sync_wait.addr = FIX_PSW(&&io_wakeup);

			/*
			 * Martin didn't like modifying the new PSW, now we take
			 *  a fast exit in do_IRQ() instead
			 */
			*(__u32 *)__LC_SYNC_IO_WORD  = 1;

			asm volatile ("STCK %0" : "=m" (time_start));

			time_start = time_start >> 32;

			do
			{
				if ( flag & DOIO_TIMEOUT )
				{
					tpi_info_t tpi_info={0,};

					do
					{
						if ( tpi(&tpi_info) == 1 )
						{
							io_sub = tpi_info.irq;
							break;
						}
						else
						{
							udelay(100); /* usecs */
							asm volatile ("STCK %0" : "=m" (time_curr));

							if ( ((time_curr >> 32) - time_start ) >= 3 )
								do_retry = 0;          							

						} /* endif */
				
					} while ( do_retry );
				}
				else
				{
					__load_psw( io_sync_wait );

io_wakeup:
					io_sub  = (__u32)*(__u16 *)__LC_SUBCHANNEL_NR;

				} /* endif */

 				if ( do_retry )
					ready = s390_process_IRQ( io_sub );

				/*
				 * surrender when retry count's exceeded ...
				 */
			} while ( !(     ( io_sub == irq )
			              && ( ready  == 1   ))
			            && do_retry             );

			*(__u32 *)__LC_SYNC_IO_WORD = 0;

			if ( !do_retry )
				ret = -ETIMEDOUT;

		} /* endif */

		break;

	case 1 :            /* status pending */

		ioinfo[irq]->devstat.flag =   DEVSTAT_START_FUNCTION
		                            | DEVSTAT_STATUS_PENDING;

		/*
		 * initialize the device driver specific devstat irb area
		 */
		memset( &((devstat_t *) ioinfo[irq]->irq_desc.dev_id)->ii.irb,
		        '\0', sizeof( irb_t) );

		/*
		 * Let the common interrupt handler process the pending status.
		 *  However, we must avoid calling the user action handler, as
		 *  it won't be prepared to handle a pending status during
		 *  do_IO() processing inline. This also implies that process_IRQ
		 *  must terminate synchronously - especially if device sensing
		 *  is required.
		 */
		ioinfo[irq]->ui.flags.s_pend   = 1;
		ioinfo[irq]->ui.flags.busy     = 1;
		ioinfo[irq]->ui.flags.doio     = 1;

		s390_process_IRQ( irq );

		ioinfo[irq]->ui.flags.s_pend   = 0;
		ioinfo[irq]->ui.flags.busy     = 0;
		ioinfo[irq]->ui.flags.doio     = 0;

		ioinfo[irq]->ui.flags.repall   = 0;
		ioinfo[irq]->ui.flags.w4final  = 0;

		ioinfo[irq]->devstat.flag     |= DEVSTAT_FINAL_STATUS;

		/*
		 * In multipath mode a condition code 3 implies the last path
		 *  has gone, except we have previously restricted the I/O to
		 *  a particular path. A condition code 1 (0 won't occur)
		 *  results in return code EIO as well as 3 with another path
		 *  than the one used (i.e. path available mask is non-zero).
		 */
		if ( ioinfo[irq]->devstat.ii.irb.scsw.cc == 3 )
		{
			if ( flag & DOIO_VALID_LPM )
			{
				ioinfo[irq]->opm &= ~(ioinfo[irq]->devstat.ii.irb.esw.esw1.lpum);
			}
			else
			{
				ioinfo[irq]->opm = 0;

			} /* endif */
	
			if ( ioinfo[irq]->opm == 0 ) 	
			{
				ret                         = -ENODEV;
				ioinfo[irq]->ui.flags.oper  = 0;
			}
			else
			{
				ret = -EIO;

			} /* endif */

			ioinfo[irq]->devstat.flag  |= DEVSTAT_NOT_OPER;

#ifdef CONFIG_DEBUG_IO
			{

			stsch(irq, &(ioinfo[irq]->schib) );

			sprintf( buffer, "s390_start_IO(%04X) - irb for "
			         "device %04X, after status pending\n",
			         irq,
			         ioinfo[irq]->devstat.devno );

			s390_displayhex( buffer,
			                 &(ioinfo[irq]->devstat.ii.irb) ,
			                 sizeof(irb_t));

			sprintf( buffer, "s390_start_IO(%04X) - schib for "
			         "device %04X, after status pending\n",
			         irq,
			         ioinfo[irq]->devstat.devno );

			s390_displayhex( buffer,
			                 &(ioinfo[irq]->schib) ,
			                 sizeof(schib_t));


			if (ioinfo[irq]->devstat.flag & DEVSTAT_FLAG_SENSE_AVAIL)
			{
				sprintf( buffer, "s390_start_IO(%04X) - sense "
				         "data for "
				         "device %04X, after status pending\n",
				         irq,
				         ioinfo[irq]->devstat.devno );

				s390_displayhex( buffer,
					ioinfo[irq]->irq_desc.dev_id->ii.sense.data,
					ioinfo[irq]->irq_desc.dev_id->rescnt);

			} /* endif */
			}
#endif
			if (cio_debug_initialized) {
				stsch(irq, &(ioinfo[irq]->schib) );
				
				sprintf( buffer, "s390_start_IO(%04X) - irb for "
					 "device %04X, after status pending\n",
					 irq,
					 ioinfo[irq]->devstat.devno );
				
				s390_displayhex2( buffer,
						 &(ioinfo[irq]->devstat.ii.irb) ,
						 sizeof(irb_t), 2);
				
				sprintf( buffer, "s390_start_IO(%04X) - schib for "
					 "device %04X, after status pending\n",
					 irq,
					 ioinfo[irq]->devstat.devno );
				
				s390_displayhex2( buffer,
						 &(ioinfo[irq]->schib) ,
						 sizeof(schib_t), 2);
				
				
				if (ioinfo[irq]->devstat.flag & DEVSTAT_FLAG_SENSE_AVAIL) {
					sprintf( buffer, "s390_start_IO(%04X) - sense "
						 "data for "
						 "device %04X, after status pending\n",
						 irq,
						 ioinfo[irq]->devstat.devno );
					
					s390_displayhex2( buffer,
							  ioinfo[irq]->irq_desc.dev_id->ii.sense.data,
							  ioinfo[irq]->irq_desc.dev_id->rescnt, 2);
					
				} /* endif */
			}
		}
		else
		{
			ret                         = -EIO;
			ioinfo[irq]->devstat.flag  &= ~DEVSTAT_NOT_OPER;
			ioinfo[irq]->ui.flags.oper  = 1;

		} /* endif */

		break;

	case 2 :            /* busy */

		ret = -EBUSY;
		break;

	default:            /* device/path not operational */
		
		if ( flag & DOIO_VALID_LPM )
		{
			ioinfo[irq]->opm &= ~lpm;
		}
		else
		{
			ioinfo[irq]->opm = 0;

		} /* endif */
	
		if ( ioinfo[irq]->opm == 0 ) 	
		{
			ioinfo[irq]->ui.flags.oper  = 0;
			ioinfo[irq]->devstat.flag  |= DEVSTAT_NOT_OPER;

		} /* endif */

		ret = -ENODEV;

		memcpy( ioinfo[irq]->irq_desc.dev_id,
		        &(ioinfo[irq]->devstat),
		        sizeof( devstat_t) );


#ifdef CONFIG_DEBUG_IO

   			stsch(irq, &(ioinfo[irq]->schib) );

			sprintf( buffer, "s390_start_IO(%04X) - schib for "
			         "device %04X, after 'not oper' status\n",
			         irq,
			         ioinfo[irq]->devstat.devno );

			s390_displayhex( buffer,
			                 &(ioinfo[irq]->schib),
			                 sizeof(schib_t));
#endif
		if (cio_debug_initialized) {
			stsch(irq, &(ioinfo[irq]->schib) );

			sprintf( buffer, "s390_start_IO(%04X) - schib for "
			         "device %04X, after 'not oper' status\n",
			         irq,
			         ioinfo[irq]->devstat.devno );

			s390_displayhex2( buffer,
			                 &(ioinfo[irq]->schib),
			                 sizeof(schib_t), 2);
		}
			
		break;

	} /* endswitch */

	if ( flag & DOIO_WAIT_FOR_INTERRUPT)
	{
		disable_cpu_sync_isc( irq );

	} /* endif */

	if ( flag & DOIO_DONT_CALL_INTHDLR )
	{
		ioinfo[irq]->ui.flags.repnone = 0;

   } /* endif */

	return( ret);
}

int do_IO( int            irq,          /* IRQ */
           ccw1_t        *cpa,          /* channel program address */
           unsigned long  user_intparm, /* interruption parameter */
           __u8           lpm,          /* logical path mask */
           unsigned long  flag)         /* flags : see above */
{
	int ret = 0;
	char dbf_txt[15];

	SANITY_CHECK(irq);

	/* handler registered ? or free_irq() in process already ? */
	if ( !ioinfo[irq]->ui.flags.ready || ioinfo[irq]->ui.flags.unready )
	{
		return( -ENODEV );

	} /* endif */

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "doIO");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	/*
	 * Note: We ignore the device operational status - if not operational,
	 *        the SSCH will lead to an -ENODEV condition ...
	 */
	if ( !ioinfo[irq]->ui.flags.busy )         /* last I/O completed ? */
	{
		ret = s390_start_IO( irq, cpa, user_intparm, lpm, flag);
	}
	else if ( ioinfo[irq]->ui.flags.fast )
	{
		/*
		 * If primary status was received and ending status is missing,
		 *  the device driver won't be notified on the ending status
		 *  if early (fast) interrupt notification was requested.
		 *  Therefore we have to queue the next incoming request. If
		 *  halt_IO() is issued while there is a request queued, a HSCH
		 *  needs to be issued and the queued request must be deleted
		 *  but its intparm must be returned (see halt_IO() processing)
		 */
		if (     ioinfo[irq]->ui.flags.w4final
		      && !ioinfo[irq]->ui.flags.doio_q )
		{
			ioinfo[irq]->qflag    = flag;
			ioinfo[irq]->qcpa     = cpa;
			ioinfo[irq]->qintparm = user_intparm;
			ioinfo[irq]->qlpm     = lpm;
		}
		else
		{
			ret = -EBUSY;

		} /* endif */
	}
	else
	{
		ret = -EBUSY;

	} /* endif */

	return( ret );

}

/*
 * resume suspended I/O operation
 */
int resume_IO( int irq)
{
	int ret = 0;
	char dbf_txt[15];

	SANITY_CHECK(irq);

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "resIO");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	/*
	 * We allow for 'resume' requests only for active I/O operations
	 */
	if ( ioinfo[irq]->ui.flags.busy )
	{
		int ccode;

		ccode = rsch( irq);

		switch (ccode) {
		case 0 :
			break;

		case 1 :
		  	s390_process_IRQ( irq );
			ret = -EBUSY;
			break;

		case 2 :
			ret = -EINVAL;
			break;

		case 3 :
			/*
			 * useless to wait for request completion
			 *  as device is no longer operational !
			 */
			ioinfo[irq]->ui.flags.oper = 0;
			ioinfo[irq]->ui.flags.busy = 0;
			ret                        = -ENODEV;
			break;

		} /* endswitch */          	
		
	}
	else
	{
		ret = -ENOTCONN;

	} /* endif  */

	return( ret);
}

/*
 * Note: The "intparm" parameter is not used by the halt_IO() function
 *       itself, as no ORB is built for the HSCH instruction. However,
 *       it allows the device interrupt handler to associate the upcoming
 *       interrupt with the halt_IO() request.
 */
int halt_IO( int           irq,
             unsigned long user_intparm,
             unsigned long flag)  /* possible DOIO_WAIT_FOR_INTERRUPT */
{
	int            ret;
	int            ccode;
	char dbf_txt[15];

	SANITY_CHECK(irq);

	/*
	 * we only allow for halt_IO if the device has an I/O handler associated
	 */
	if ( !ioinfo[irq]->ui.flags.ready )
	{
		ret = -ENODEV;
	}
	/*
	 * we ignore the halt_io() request if ending_status was received but
	 *  a SENSE operation is waiting for completion.
	 */
	else if ( ioinfo[irq]->ui.flags.w4sense )
	{
		ret = 0;
	}
#if 0
	/*
	 * We don't allow for halt_io with a sync do_IO() requests pending.
	 */
	else if (    ioinfo[irq]->ui.flags.syncio
	          && (flag & DOIO_WAIT_FOR_INTERRUPT))
	{
		ret = -EBUSY;
	}
#endif
	else
	{
		if (cio_debug_initialized) {
			debug_text_event(cio_debug_trace_id, 2, "haltIO");
			sprintf(dbf_txt, "%x", irq);
			debug_text_event(cio_debug_trace_id, 2, dbf_txt);
		}
		/*
		 * If sync processing was requested we lock the sync ISC,
		 *  modify the device to present interrupts for this ISC only
		 *  and switch the CPU to handle this ISC + the console ISC
		 *  exclusively.
		 */
		if ( flag & DOIO_WAIT_FOR_INTERRUPT )
		{
			ret = enable_cpu_sync_isc( irq);

			if ( ret )
			{
				return( ret);
  	
			} /* endif */

		} /* endif */

		/*
		 * Issue "Halt subchannel" and process condition code
		 */
		ccode = hsch( irq );

		switch ( ccode ) {
		case 0:

			ioinfo[irq]->ui.flags.haltio = 1;

			if ( !ioinfo[irq]->ui.flags.doio )
			{
				ioinfo[irq]->ui.flags.busy   = 1;
				ioinfo[irq]->u_intparm       = user_intparm;
				ioinfo[irq]->devstat.cstat   = 0;
				ioinfo[irq]->devstat.dstat   = 0;
				ioinfo[irq]->devstat.lpum    = 0;
				ioinfo[irq]->devstat.flag    = DEVSTAT_HALT_FUNCTION;
				ioinfo[irq]->devstat.scnt    = 0;

			}
			else
			{
				ioinfo[irq]->devstat.flag   |= DEVSTAT_HALT_FUNCTION;

			} /* endif */

			/*
			 * If synchronous I/O processing is requested, we have
			 *  to wait for the corresponding interrupt to occur by
			 *  polling the interrupt condition. However, as multiple
			 *  interrupts may be outstanding, we must not just wait
			 *  for the first interrupt, but must poll until ours
			 *  pops up.
			 */
			if ( flag & DOIO_WAIT_FOR_INTERRUPT )
			{
				int              io_sub;
				__u32            io_parm;
				psw_t            io_new_psw;
				int              ccode;
  	
				int              ready = 0;
				struct _lowcore *lc    = NULL;

				/*
				 * We shouldn't perform a TPI loop, waiting for
				 *  an interrupt to occur, but should load a
				 *  WAIT PSW instead. Otherwise we may keep the
				 *  channel subsystem busy, not able to present
				 *  the interrupt. When our sync. interrupt
				 *  arrived we reset the I/O old PSW to its
				 *  original value.
				 */
				memcpy( &io_new_psw,
				        &lc->io_new_psw,
				        sizeof(psw_t));

				ccode = iac();

				switch (ccode) {
				case 0:  		// primary-space
					io_sync_wait.mask =   _IO_PSW_MASK
					                    | _PSW_PRIM_SPACE_MODE
					                    | _PSW_IO_WAIT;
					break;
				case 1:			// secondary-space
					io_sync_wait.mask =   _IO_PSW_MASK
					                    | _PSW_SEC_SPACE_MODE
					                    | _PSW_IO_WAIT;
					break;
				case 2:			// access-register
					io_sync_wait.mask =   _IO_PSW_MASK
					                    | _PSW_ACC_REG_MODE
					                    | _PSW_IO_WAIT;
					break;
				case 3:			// home-space	
					io_sync_wait.mask =   _IO_PSW_MASK
					                    | _PSW_HOME_SPACE_MODE
					                    | _PSW_IO_WAIT;
					break;
				default:
					panic( "halt_IO() : unexpected "
					       "address-space-control %d\n",
					       ccode);
					break;
				} /* endswitch */

				io_sync_wait.addr = FIX_PSW(&&hio_wakeup);

				/*
				 * Martin didn't like modifying the new PSW, now we take
				 *  a fast exit in do_IRQ() instead
				 */
				*(__u32 *)__LC_SYNC_IO_WORD  = 1;

				do
				{

					__load_psw( io_sync_wait );
hio_wakeup:
				   io_parm = *(__u32 *)__LC_IO_INT_PARM;
				   io_sub  = (__u32)*(__u16 *)__LC_SUBCHANNEL_NR;

				   ready = s390_process_IRQ( io_sub );

				} while ( !((io_sub == irq) && (ready == 1)) );

				*(__u32 *)__LC_SYNC_IO_WORD = 0;

			} /* endif */

			ret = 0;
			break;

		case 1 :            /* status pending */
	
			ioinfo[irq]->devstat.flag |= DEVSTAT_STATUS_PENDING;

			/*
			 * initialize the device driver specific devstat irb area
			 */
			memset( &ioinfo[irq]->irq_desc.dev_id->ii.irb,
			        '\0', sizeof( irb_t) );

			/*
			 * Let the common interrupt handler process the pending
			 *  status. However, we must avoid calling the user
			 *  action handler, as it won't be prepared to handle
			 *  a pending status during do_IO() processing inline.
			 *  This also implies that s390_process_IRQ must
			 *  terminate synchronously - especially if device
			 *  sensing is required.
			 */
			ioinfo[irq]->ui.flags.s_pend   = 1;
			ioinfo[irq]->ui.flags.busy     = 1;
			ioinfo[irq]->ui.flags.doio     = 1;

			s390_process_IRQ( irq );
			
			ioinfo[irq]->ui.flags.s_pend   = 0;
			ioinfo[irq]->ui.flags.busy     = 0;
			ioinfo[irq]->ui.flags.doio     = 0;
			ioinfo[irq]->ui.flags.repall   = 0;
			ioinfo[irq]->ui.flags.w4final  = 0;

			ioinfo[irq]->devstat.flag     |= DEVSTAT_FINAL_STATUS;

			/*
			 * In multipath mode a condition code 3 implies the last
			 *  path has gone, except we have previously restricted
			 *  the I/O to a particular path. A condition code 1
			 *  (0 won't occur) results in return code EIO as well
			 *  as 3 with another path than the one used (i.e. path available mask is non-zero).
			 */
			if ( ioinfo[irq]->devstat.ii.irb.scsw.cc == 3 )
			{
				ret                         = -ENODEV;
				ioinfo[irq]->devstat.flag  |= DEVSTAT_NOT_OPER;
				ioinfo[irq]->ui.flags.oper  = 0;
			}
			else
			{
				ret                         = -EIO;
				ioinfo[irq]->devstat.flag  &= ~DEVSTAT_NOT_OPER;
				ioinfo[irq]->ui.flags.oper  = 1;

			} /* endif */

			break;

		case 2 :            /* busy */

			ret = -EBUSY;
			break;

		default:            /* device not operational */

			ret = -ENODEV;
			break;

		} /* endswitch */

		if ( flag & DOIO_WAIT_FOR_INTERRUPT )
		{
			disable_cpu_sync_isc( irq );

		} /* endif */

	} /* endif */

	return( ret );
}

/*
 * Note: The "intparm" parameter is not used by the clear_IO() function
 *       itself, as no ORB is built for the CSCH instruction. However,
 *       it allows the device interrupt handler to associate the upcoming
 *       interrupt with the clear_IO() request.
 */
int clear_IO( int           irq,
              unsigned long user_intparm,
              unsigned long flag)  /* possible DOIO_WAIT_FOR_INTERRUPT */
{
	int            ret = 0;
	int            ccode;
	char dbf_txt[15];

	SANITY_CHECK(irq);

	if ( ioinfo[irq] == INVALID_STORAGE_AREA )
	{
		return( -ENODEV);
   }

	/*
	 * we only allow for clear_IO if the device has an I/O handler associated
	 */
	if ( !ioinfo[irq]->ui.flags.ready )
	{
		ret = -ENODEV;
	}
	/*
	 * we ignore the clear_io() request if ending_status was received but
	 *  a SENSE operation is waiting for completion.
	 */
	else if ( ioinfo[irq]->ui.flags.w4sense )
	{
		ret = 0;
	}
#if 0
	/*
	 * We don't allow for clear_io with a sync do_IO() requests pending.
	 *  Concurrent I/O is possible in SMP environments only, but the
	 *  sync. I/O request can be gated to one CPU at a time only.
	 */
	else if ( ioinfo[irq]->ui.flags.syncio )
	{
		ret = -EBUSY;
	}
#endif
	else
	{
		if (cio_debug_initialized) {
			debug_text_event(cio_debug_trace_id, 2, "clearIO");
			sprintf(dbf_txt, "%x", irq);
			debug_text_event(cio_debug_trace_id, 2, dbf_txt);
		}
		/*
		 * If sync processing was requested we lock the sync ISC,
		 *  modify the device to present interrupts for this ISC only
		 *  and switch the CPU to handle this ISC + the console ISC
		 *  exclusively.
		 */
		if ( flag & DOIO_WAIT_FOR_INTERRUPT )
		{
			ret = enable_cpu_sync_isc( irq);

			if ( ret )
			{
				return( ret);
  	
			} /* endif */

		} /* endif */

		/*
		 * Issue "Clear subchannel" and process condition code
		 */
		ccode = csch( irq );

		switch ( ccode ) {
		case 0:

			ioinfo[irq]->ui.flags.haltio = 1;

			if ( !ioinfo[irq]->ui.flags.doio )
			{
				ioinfo[irq]->ui.flags.busy   = 1;
				ioinfo[irq]->u_intparm       = user_intparm;
				ioinfo[irq]->devstat.cstat   = 0;
				ioinfo[irq]->devstat.dstat   = 0;
				ioinfo[irq]->devstat.lpum    = 0;
				ioinfo[irq]->devstat.flag    = DEVSTAT_CLEAR_FUNCTION;
				ioinfo[irq]->devstat.scnt    = 0;

			}
			else
			{
				ioinfo[irq]->devstat.flag   |= DEVSTAT_CLEAR_FUNCTION;

			} /* endif */

			/*
			 * If synchronous I/O processing is requested, we have
			 *  to wait for the corresponding interrupt to occur by
			 *  polling the interrupt condition. However, as multiple
			 *  interrupts may be outstanding, we must not just wait
			 *  for the first interrupt, but must poll until ours
			 *  pops up.
			 */
			if ( flag & DOIO_WAIT_FOR_INTERRUPT )
			{
				int              io_sub;
				__u32            io_parm;
				psw_t            io_new_psw;
				int              ccode;
  	
				int              ready = 0;
				struct _lowcore *lc    = NULL;

				/*
				 * We shouldn't perform a TPI loop, waiting for
				 *  an interrupt to occur, but should load a
				 *  WAIT PSW instead. Otherwise we may keep the
				 *  channel subsystem busy, not able to present
				 *  the interrupt. When our sync. interrupt
				 *  arrived we reset the I/O old PSW to its
				 *  original value.
				 */
				memcpy( &io_new_psw,
				        &lc->io_new_psw,
				        sizeof(psw_t));

				ccode = iac();

				switch (ccode) {
				case 0:  		// primary-space
					io_sync_wait.mask =   _IO_PSW_MASK
					                    | _PSW_PRIM_SPACE_MODE
					                    | _PSW_IO_WAIT;
					break;
				case 1:			// secondary-space
					io_sync_wait.mask =   _IO_PSW_MASK
					                    | _PSW_SEC_SPACE_MODE
					                    | _PSW_IO_WAIT;
					break;
				case 2:			// access-register
					io_sync_wait.mask =   _IO_PSW_MASK
					                    | _PSW_ACC_REG_MODE
					                    | _PSW_IO_WAIT;
					break;
				case 3:			// home-space	
					io_sync_wait.mask =   _IO_PSW_MASK
					                    | _PSW_HOME_SPACE_MODE
					                    | _PSW_IO_WAIT;
					break;
				default:
					panic( "clear_IO() : unexpected "
					       "address-space-control %d\n",
					       ccode);
					break;
				} /* endswitch */

				io_sync_wait.addr = FIX_PSW(&&cio_wakeup);

				/*
				 * Martin didn't like modifying the new PSW, now we take
				 *  a fast exit in do_IRQ() instead
				 */
				*(__u32 *)__LC_SYNC_IO_WORD  = 1;

				do
				{

					__load_psw( io_sync_wait );
cio_wakeup:
				   io_parm = *(__u32 *)__LC_IO_INT_PARM;
				   io_sub  = (__u32)*(__u16 *)__LC_SUBCHANNEL_NR;

				   ready = s390_process_IRQ( io_sub );

				} while ( !((io_sub == irq) && (ready == 1)) );

				*(__u32 *)__LC_SYNC_IO_WORD = 0;

			} /* endif */

			ret = 0;
			break;

		case 1 :            /* no status pending for csh */
			BUG();
			break;


		case 2 :            /* no busy for csh*/
			BUG();
			break;

		default:            /* device not operational */

			ret = -ENODEV;
			break;

		} /* endswitch */

		if ( flag & DOIO_WAIT_FOR_INTERRUPT )
		{
			disable_cpu_sync_isc( irq );

		} /* endif */

	} /* endif */

	return( ret );
}


/*
 * do_IRQ() handles all normal I/O device IRQ's (the special
 *          SMP cross-CPU interrupts have their own specific
 *          handlers).
 *
 */
asmlinkage void do_IRQ( struct pt_regs regs )
{
	/*
	 * Get interrupt info from lowcore
	 */
	volatile tpi_info_t *tpi_info = (tpi_info_t*)(__LC_SUBCHANNEL_ID);
	int cpu = smp_processor_id();

	/*
	 * take fast exit if CPU is in sync. I/O state
	 *
	 * Note: we have to turn off the WAIT bit and re-disable
	 *       interrupts prior to return as this was the initial
	 *       entry condition to synchronous I/O.
	 */
 	if (    *(__u32 *)__LC_SYNC_IO_WORD )
	{
		regs.psw.mask &= ~(_PSW_WAIT_MASK_BIT | _PSW_IO_MASK_BIT);
		return;
	} /* endif */

#ifdef CONFIG_FAST_IRQ
	do {
#endif /*  CONFIG_FAST_IRQ */
		
		/*
		 * Non I/O-subchannel thin interrupts are processed differently
		 */
		if ( tpi_info->adapter_IO == 1 &&
		     tpi_info->int_type == IO_INTERRUPT_TYPE )
		{
			irq_enter(cpu, -1);
			do_adapter_IO( tpi_info->intparm );
			irq_exit(cpu, -1);
		} 
		else 
		{
			unsigned int irq = tpi_info->irq;
	
			/*
			 * fix me !!!
			 *
			 * instead of boxing the device, we need to schedule device
			 * recognition, the interrupt stays pending. We need to
			 * dynamically allocate an ioinfo structure, etc..
			 */
			if ( ioinfo[irq] == INVALID_STORAGE_AREA )
			{
				return;	/* this keeps the device boxed ... */
			}
	
			irq_enter(cpu, irq);
			s390irq_spin_lock( irq );
			s390_process_IRQ( irq );
			s390irq_spin_unlock( irq );
			irq_exit(cpu, irq);
		}

#ifdef CONFIG_FAST_IRQ

		/*
		 * Are more interrupts pending?
		 * If so, the tpi instruction will update the lowcore 
		 * to hold the info for the next interrupt.
		 */
	} while ( tpi( NULL ) != 0 );

#endif /*  CONFIG_FAST_IRQ */

	return;
}


/*
 * s390_process_IRQ() handles status pending situations and interrupts
 *
 * Called by : do_IRQ()             - for "real" interrupts
 *             s390_start_IO, halt_IO()
 *                                  - status pending cond. after SSCH, or HSCH
 *             disable_subchannel() - status pending conditions (after MSCH)
 *
 * Returns: 0 - no ending status received, no further action taken
 *          1 - interrupt handler was called with ending status
 */
int s390_process_IRQ( unsigned int irq )
{
	int                    ccode;      /* cond code from tsch() operation */
	int                    irb_cc;     /* cond code from irb */
	int                    sdevstat;   /* struct devstat size to copy */
	unsigned int           fctl;       /* function control */
	unsigned int           stctl;      /* status   control */
	unsigned int           actl;       /* activity control */

	int               issense         = 0;
	int               ending_status   = 0;
	int               allow4handler   = 1;
	int               chnchk          = 0;
	devstat_t        *dp;
	devstat_t        *udp;

	char dbf_txt[15];
	char buffer[80];


	if (cio_count_irqs) {
		int cpu = smp_processor_id();
		s390_irq_count[cpu]++;
	}
	
	

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 3, "procIRQ");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 3, dbf_txt);
	}

	if ( ioinfo[irq] == INVALID_STORAGE_AREA )
	{
		/* we can't properly process the interrupt ... */
		tsch( irq, p_init_irb );
		return( 1 );

	} /* endif */

	dp  = &ioinfo[irq]->devstat;
	udp = ioinfo[irq]->irq_desc.dev_id;
	

	/*
	 * It might be possible that a device was not-oper. at the time
	 *  of free_irq() processing. This means the handler is no longer
	 *  available when the device possibly becomes ready again. In
	 *  this case we perform delayed disable_subchannel() processing.
	 */
	if ( !ioinfo[irq]->ui.flags.ready )
	{
		if ( !ioinfo[irq]->ui.flags.d_disable )
		{
#ifdef CONFIG_DEBUG_IO
			printk( KERN_CRIT"s390_process_IRQ(%04X) "
			        "- no interrupt handler registered "
					  "for device %04X !\n",
			        irq,
			        ioinfo[irq]->devstat.devno);
#endif /* CONFIG_DEBUG_IO */
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_msg_id, 0,
						    "s390_process_IRQ(%04X) - no interrupt handler registered for device %04X !\n",
						    irq, ioinfo[irq]->devstat.devno);
		} /* endif */

	} /* endif */

	/*
	 * retrieve the i/o interrupt information (irb),
	 *  update the device specific status information
	 *  and possibly call the interrupt handler.
	 *
	 * Note 1: At this time we don't process the resulting
	 *         condition code (ccode) from tsch(), although
	 *         we probably should.
	 *
	 * Note 2: Here we will have to check for channel
	 *         check conditions and call a channel check
	 *         handler.
	 *
	 * Note 3: If a start function was issued, the interruption
	 *         parameter relates to it. If a halt function was
	 *         issued for an idle device, the intparm must not
	 *         be taken from lowcore, but from the devstat area.
	 */
	ccode = tsch( irq, &(dp->ii.irb) );

	//
	// We must only accumulate the status if the device is busy already
	//
	if ( ioinfo[irq]->ui.flags.busy )
	{
		dp->dstat   |= dp->ii.irb.scsw.dstat;
		dp->cstat   |= dp->ii.irb.scsw.cstat;
                dp->intparm  = ioinfo[irq]->u_intparm;

	}
	else
	{
		dp->dstat    = dp->ii.irb.scsw.dstat;
		dp->cstat    = dp->ii.irb.scsw.cstat;

		dp->flag     = 0;   // reset status flags
		dp->intparm  = 0;

	} /* endif */

	dp->lpum = dp->ii.irb.esw.esw1.lpum;

	/*
	 * reset device-busy bit if no longer set in irb
	 */
	if (    (dp->dstat & DEV_STAT_BUSY                   ) 
	     && ((dp->ii.irb.scsw.dstat & DEV_STAT_BUSY) == 0))
	{
		dp->dstat &= ~DEV_STAT_BUSY;

	} /* endif */

	/*
	 * Save residual count and CCW information in case primary and
	 *  secondary status are presented with different interrupts.
	 */
	if ( dp->ii.irb.scsw.stctl
	     & ( SCSW_STCTL_PRIM_STATUS | SCSW_STCTL_INTER_STATUS ) ) {

		/*
		 * If the subchannel status shows status pending
		 * and we received a check condition, the count
		 * information is not meaningful.
		 */
		
		 if ( !(    (dp->ii.irb.scsw.stctl & SCSW_STCTL_STATUS_PEND) 
			 && (   dp->ii.irb.scsw.cstat 
			      & (   SCHN_STAT_CHN_DATA_CHK
				  | SCHN_STAT_CHN_CTRL_CHK
				  | SCHN_STAT_INTF_CTRL_CHK
				  | SCHN_STAT_PROG_CHECK
				  | SCHN_STAT_PROT_CHECK
				  | SCHN_STAT_CHAIN_CHECK )))) {

			 dp->rescnt = dp->ii.irb.scsw.count;
		 } else {
			 dp->rescnt = SENSE_MAX_COUNT;
		 }

		dp->cpa    = dp->ii.irb.scsw.cpa;

#ifdef CONFIG_DEBUG_IO
		if ( irq != cons_dev )
			printk( "s390_process_IRQ( %04X ) : "
			        "residual count from irb after tsch() %d\n",
			        irq, dp->rescnt );
#endif
		if (cio_debug_initialized)
			debug_sprintf_event(cio_debug_msg_id, 6,
					    "s390_process_IRQ( %04X ) : residual count from irq after tsch() %d\n",
					    irq, dp->rescnt);

	} /* endif */

	irb_cc = dp->ii.irb.scsw.cc;

	//
	// check for any kind of channel or interface control check but don't
	//  issue the message for the console device
	//
	if (    (dp->ii.irb.scsw.cstat
	            & (  SCHN_STAT_CHN_DATA_CHK
	               | SCHN_STAT_CHN_CTRL_CHK
			 | SCHN_STAT_INTF_CTRL_CHK )))
	{
		if (irq != cons_dev)
			printk( "Channel-Check or Interface-Control-Check "
				"received\n"
				" ... device %04X on subchannel %04X, dev_stat "
				": %02X sch_stat : %02X\n",
				ioinfo[irq]->devstat.devno,
				irq,
				dp->dstat,
				dp->cstat);
		if (cio_debug_initialized) {
			debug_sprintf_event(cio_debug_msg_id, 0,
					    "Channel-Check or Interface-Control-Check received\n");
			debug_sprintf_event(cio_debug_msg_id, 0,
					    "... device %04X on subchannel %04X, dev_stat: %02X sch_stat: %02X\n",
					    ioinfo[irq]->devstat.devno, irq, dp->dstat, dp->cstat);
		}

		chnchk = 1;

	} /* endif */

	if( dp->ii.irb.scsw.ectl==0)
	{
		issense=0;
	}
	else if (    (dp->ii.irb.scsw.stctl == SCSW_STCTL_STATUS_PEND)
	          && (dp->ii.irb.scsw.eswf  == 0                     ))
	{
		issense = 0;
	}
	else if (   (dp->ii.irb.scsw.stctl ==
		      (SCSW_STCTL_STATUS_PEND | SCSW_STCTL_INTER_STATUS)) 
		 && ((dp->ii.irb.scsw.actl & SCSW_ACTL_SUSPENDED) == 0)  )
	{
		issense = 0;
	}
	else
	{
		issense = dp->ii.irb.esw.esw0.erw.cons;

	} /* endif */

	if ( issense )
	{
		dp->scnt  = dp->ii.irb.esw.esw0.erw.scnt;
		dp->flag |= DEVSTAT_FLAG_SENSE_AVAIL;
                  	
		sdevstat = sizeof( devstat_t);

#ifdef CONFIG_DEBUG_IO
		if ( irq != cons_dev )
			printk( "s390_process_IRQ( %04X ) : "
			        "concurrent sense bytes avail %d\n",
			        irq, dp->scnt );
#endif
		if (cio_debug_initialized)
			debug_sprintf_event(cio_debug_msg_id, 4,
					    "s390_process_IRQ( %04X ): concurrent sense bytes avail %d\n",
					    irq, dp->scnt);
	}
	else
	{
		/* don't copy the sense data area ! */
		sdevstat = sizeof( devstat_t) - SENSE_MAX_COUNT;

	} /* endif */

	switch ( irb_cc ) {
	case 1:      /* status pending */

		dp->flag |= DEVSTAT_STATUS_PENDING;

	case 0:      /* normal i/o interruption */

		fctl  = dp->ii.irb.scsw.fctl;
		stctl = dp->ii.irb.scsw.stctl;
		actl  = dp->ii.irb.scsw.actl;

		if ( chnchk )
		{
			sprintf( buffer, "s390_process_IRQ(%04X) - irb for "
			         "device %04X after channel check\n",
			         irq,
			         dp->devno );

			s390_displayhex( buffer,
			                 &(dp->ii.irb) ,
			                 sizeof(irb_t));
			if (cio_debug_initialized) {
				
				sprintf( buffer, "s390_process_IRQ(%04X) - irb for "
			         "device %04X after channel check\n",
			         irq,
			         dp->devno );

				s390_displayhex2( buffer,
						  &(dp->ii.irb) ,
						  sizeof(irb_t), 0);
			}
		} /* endif */
			
		ioinfo[irq]->stctl |= stctl;

		ending_status =    ( stctl & SCSW_STCTL_SEC_STATUS                          )
			|| ( stctl == (SCSW_STCTL_ALERT_STATUS | SCSW_STCTL_STATUS_PEND)         )
			|| ( (fctl == SCSW_FCTL_HALT_FUNC)  && (stctl == SCSW_STCTL_STATUS_PEND) )
			|| ( (fctl == SCSW_FCTL_CLEAR_FUNC) && (stctl == SCSW_STCTL_STATUS_PEND) );

		/*
		 * Check for unsolicited interrupts - for debug purposes only
		 *
		 * We only consider an interrupt as unsolicited, if the device was not
		 *  actively in use (busy) and an interrupt other than an ALERT status
		 *  was received.
		 *
		 * Note: We must not issue a message to the console, if the
		 *       unsolicited interrupt applies to the console device
		 *       itself !
		 */
		if (    !( stctl & SCSW_STCTL_ALERT_STATUS )
		     &&  ( ioinfo[irq]->ui.flags.busy == 0 ) )
		{
#ifdef CONFIG_DEBUG_IO
			if (irq != cons_dev)
				printk( "Unsolicited interrupt received for device %04X on subchannel %04X\n"
					" ... device status : %02X subchannel status : %02X\n",
					dp->devno,
					irq,
					dp->dstat,
					dp->cstat);
			
			sprintf( buffer, "s390_process_IRQ(%04X) - irb for "
			         "device %04X, ending_status %d\n",
			         irq,
			         dp->devno,
			         ending_status);

			s390_displayhex( buffer,
			                 &(dp->ii.irb) ,
			                 sizeof(irb_t));
#endif
			if (cio_debug_initialized) {
				debug_sprintf_event(cio_debug_msg_id, 2, 
						    "Unsolicited interrupt received for device %04X on subchannel %04X\n"
						    " ... device status : %02X subchannel status : %02X\n",
						    dp->devno,
						    irq,
						    dp->dstat,
						    dp->cstat);
				sprintf( buffer, "s390_process_IRQ(%04X) - irb for "
					 "device %04X, ending_status %d\n",
					 irq,
					 dp->devno,
					 ending_status);
				
				s390_displayhex2( buffer,
						 &(dp->ii.irb) ,
						 sizeof(irb_t), 2);	
			}
		} /* endif */

		/*
		 * take fast exit if no handler is available
		 */
		if ( !ioinfo[irq]->ui.flags.ready )
			return( ending_status );     		

		/*
		 * Check whether we must issue a SENSE CCW ourselves if there is no
		 *  concurrent sense facility installed for the subchannel.
		 *
		 * Note: We should check for ioinfo[irq]->ui.flags.consns but VM
		 *       violates the ESA/390 architecture and doesn't present an
		 *       operand exception for virtual devices without concurrent
		 *       sense facility available/supported when enabling the
		 *       concurrent sense facility.
		 */
		if (    (    (dp->ii.irb.scsw.dstat & DEV_STAT_UNIT_CHECK )
		          && (!issense                                    ) )
		     || (ioinfo[irq]->ui.flags.delsense && ending_status    ) )
		{
			int            ret_io;
			ccw1_t        *s_ccw  = &ioinfo[irq]->senseccw;
			unsigned long  s_flag = 0;

			if ( ending_status )
			{
				/*
				 * We copy the current status information into the device driver
				 *  status area. Then we can use the local devstat area for device
				 *  sensing. When finally calling the IRQ handler we must not overlay
				 *  the original device status but copy the sense data only.
				 */
				memcpy( udp, dp, sizeof( devstat_t) );

				s_ccw->cmd_code = CCW_CMD_BASIC_SENSE;
				s_ccw->cda      = (__u32)virt_to_phys( ioinfo[irq]->sense_data );
				s_ccw->count    = SENSE_MAX_COUNT;
				s_ccw->flags    = CCW_FLAG_SLI;

				/*
				 * If free_irq() or a sync do_IO/s390_start_IO() is in
				 *  process we have to sense synchronously
				 */
				if ( ioinfo[irq]->ui.flags.unready || ioinfo[irq]->ui.flags.syncio )
				{
					s_flag = DOIO_WAIT_FOR_INTERRUPT;

				} /* endif */

				/*
				 * Reset status info
				 *
				 * It does not matter whether this is a sync. or async.
				 *  SENSE request, but we have to assure we don't call
				 *  the irq handler now, but keep the irq in busy state.
				 *  In sync. mode s390_process_IRQ() is called recursively,
				 *  while in async. mode we re-enter do_IRQ() with the
				 *  next interrupt.
				 *
				 * Note : this may be a delayed sense request !
				 */
				allow4handler                  = 0;

				ioinfo[irq]->ui.flags.fast     = 0;
				ioinfo[irq]->ui.flags.repall   = 0;
				ioinfo[irq]->ui.flags.w4final  = 0;
				ioinfo[irq]->ui.flags.delsense = 0;

				dp->cstat     = 0;
				dp->dstat     = 0;
				dp->rescnt    = SENSE_MAX_COUNT;

				ioinfo[irq]->ui.flags.w4sense  = 1;
			
				ret_io = s390_start_IO( irq,
				                        s_ccw,
				                        0xE2C5D5E2,  // = SENSe
				                        0,           // n/a
				                        s_flag);
			}
			else
			{
				/*
				 * we received an Unit Check but we have no final
				 *  status yet, therefore we must delay the SENSE
				 *  processing. However, we must not report this
				 *  intermediate status to the device interrupt
				 *  handler.
				 */
				ioinfo[irq]->ui.flags.fast     = 0;
				ioinfo[irq]->ui.flags.repall   = 0;

				ioinfo[irq]->ui.flags.delsense = 1;
				allow4handler                  = 0;

			} /* endif */

		} /* endif */

		/*
		 * we allow for the device action handler if .
		 *  - we received ending status
		 *  - the action handler requested to see all interrupts
		 *  - we received an intermediate status
		 *  - fast notification was requested (primary status)
		 *  - unsollicited interrupts
		 *
		 */
		if ( allow4handler )
		{
			allow4handler =    ending_status
				|| ( ioinfo[irq]->ui.flags.repall                                      )
				|| ( stctl & SCSW_STCTL_INTER_STATUS                                   )
				|| ( (ioinfo[irq]->ui.flags.fast ) && (stctl & SCSW_STCTL_PRIM_STATUS) )
				|| ( ioinfo[irq]->ui.flags.oper == 0                                   );

		} /* endif */

		/*
		 * We used to copy the device status information right before
		 *  calling the device action handler. However, in status
		 *  pending situations during do_IO() or halt_IO(), as well as
		 *  enable_subchannel/disable_subchannel processing we must
		 *  synchronously return the status information and must not
		 *  call the device action handler.
		 *
		 */
		if ( allow4handler )
		{
			/*
			 * if we were waiting for sense data we copy the sense
			 *  bytes only as the original status information was
			 *  saved prior to sense already.
			 */
			if ( ioinfo[irq]->ui.flags.w4sense )
			{
				int sense_count = SENSE_MAX_COUNT-ioinfo[irq]->devstat.rescnt;

#ifdef CONFIG_DEBUG_IO
				if ( irq != cons_dev )
					printk( "s390_process_IRQ( %04X ) : "
						"BASIC SENSE bytes avail %d\n",
						irq, sense_count );
#endif
				if (cio_debug_initialized)
					debug_sprintf_event(cio_debug_msg_id, 4,
							    "s390_process_IRQ( %04X ): BASIC SENSE bytes avail %d\n",
							    irq, sense_count);
				ioinfo[irq]->ui.flags.w4sense = 0;
				udp->flag |= DEVSTAT_FLAG_SENSE_AVAIL;
				udp->scnt  = sense_count;

				if ( sense_count >= 0 )
				{
					memcpy( udp->ii.sense.data,
					        ioinfo[irq]->sense_data,
					        sense_count);
				}
				else
				{
					panic( "s390_process_IRQ(%04x) encountered "
					       "negative sense count\n",
					       irq);

				} /* endif */
			}
			else
			{
				memcpy( udp, dp, sdevstat );

			}  /* endif */

		} /* endif */

		/*
		 * for status pending situations other than deferred interrupt
		 *  conditions detected by s390_process_IRQ() itself we must not
		 *  call the handler. This will synchronously be reported back
		 *  to the caller instead, e.g. when detected during do_IO().
		 */
		if (    ioinfo[irq]->ui.flags.s_pend
		     || ioinfo[irq]->ui.flags.unready
		     || ioinfo[irq]->ui.flags.repnone )
		{		
			if ( ending_status )
			{

				ioinfo[irq]->ui.flags.busy     = 0;
				ioinfo[irq]->ui.flags.doio     = 0;
				ioinfo[irq]->ui.flags.haltio   = 0;
				ioinfo[irq]->ui.flags.fast     = 0;
				ioinfo[irq]->ui.flags.repall   = 0;
				ioinfo[irq]->ui.flags.w4final  = 0;

				dp->flag  |= DEVSTAT_FINAL_STATUS;
				udp->flag |= DEVSTAT_FINAL_STATUS;

			} /* endif */

			allow4handler = 0;

		} /* endif */

		/*
		 * Call device action handler if applicable
		 */
		if ( allow4handler )
		{

			/*
			 *  We only reset the busy condition when we are sure that no further
			 *   interrupt is pending for the current I/O request (ending_status).
			 */
			if ( ending_status || !ioinfo[irq]->ui.flags.oper )
			{
				ioinfo[irq]->ui.flags.oper     = 1;  /* dev IS oper */

				ioinfo[irq]->ui.flags.busy     = 0;
				ioinfo[irq]->ui.flags.doio     = 0;
				ioinfo[irq]->ui.flags.haltio   = 0;
				ioinfo[irq]->ui.flags.fast     = 0;
				ioinfo[irq]->ui.flags.repall   = 0;
				ioinfo[irq]->ui.flags.w4final  = 0;

				dp->flag  |= DEVSTAT_FINAL_STATUS;
				udp->flag |= DEVSTAT_FINAL_STATUS;

				ioinfo[irq]->irq_desc.handler( irq, udp, NULL );

				//
				// reset intparm after final status or we will badly present unsolicited
				//  interrupts with a intparm value possibly no longer valid.
				//
				dp->intparm   = 0;

				//
				// Was there anything queued ? Start the pending channel program
				//  if there is one.
				//
				if ( ioinfo[irq]->ui.flags.doio_q )
				{
					int ret;

					ret = s390_start_IO( irq,
					                     ioinfo[irq]->qcpa,
					                     ioinfo[irq]->qintparm,
						             ioinfo[irq]->qlpm,
					                     ioinfo[irq]->qflag);

					ioinfo[irq]->ui.flags.doio_q = 0;

					/*
					 * If s390_start_IO() failed call the device's interrupt
					 *  handler, the IRQ related devstat area was setup by
					 *  s390_start_IO() accordingly already (status pending
					 *  condition).
					 */
					if ( ret )
					{
						ioinfo[irq]->irq_desc.handler( irq, udp, NULL );

					} /* endif */

				} /* endif */

			}
			else
			{
				ioinfo[irq]->ui.flags.w4final = 1;

				/*
				 * Eventually reset subchannel PCI status and
				 *  set the PCI or SUSPENDED flag in the user
				 *  device status block if appropriate.
				 */
				if ( dp->cstat & SCHN_STAT_PCI )
				{
					udp->flag |= DEVSTAT_PCI;
					dp->cstat &= ~SCHN_STAT_PCI;
				}

				if ( actl & SCSW_ACTL_SUSPENDED )
				{
					udp->flag |= DEVSTAT_SUSPENDED;

				} /* endif */

				ioinfo[irq]->irq_desc.handler( irq, udp, NULL );

			} /* endif */

		} /* endif */

		break;

	case 3:      /* device/path not operational */

		ioinfo[irq]->ui.flags.busy    = 0;
		ioinfo[irq]->ui.flags.doio    = 0;
		ioinfo[irq]->ui.flags.haltio  = 0;

		dp->cstat    = 0;
		dp->dstat    = 0;

		if ( ioinfo[irq]->ulpm != ioinfo[irq]->opm )
		{
			/*
			 * either it was the only path or it was restricted ...
			 */
			ioinfo[irq]->opm &= ~(ioinfo[irq]->devstat.ii.irb.esw.esw1.lpum);
		}
		else
		{
			ioinfo[irq]->opm = 0;

		} /* endif */
	
		if ( ioinfo[irq]->opm == 0 ) 	
		{
			ioinfo[irq]->ui.flags.oper  = 0;

		} /* endif */

		ioinfo[irq]->devstat.flag |= DEVSTAT_NOT_OPER;
		ioinfo[irq]->devstat.flag |= DEVSTAT_FINAL_STATUS;

		/*
		 * When we find a device "not oper" we save the status
		 *  information into the device status area and call the
		 *  device specific interrupt handler.
		 *
		 * Note: currently we don't have any way to reenable
		 *       the device unless an unsolicited interrupt
		 *       is presented. We don't check for spurious
		 *       interrupts on "not oper" conditions.
		 */

		if (    ( ioinfo[irq]->ui.flags.fast    )
		     && ( ioinfo[irq]->ui.flags.w4final ) )
		{
			/*
			 * If a new request was queued already, we have
			 *  to simulate the "not oper" status for the
			 *  queued request by switching the "intparm" value
			 *  and notify the interrupt handler.
			 */
			if ( ioinfo[irq]->ui.flags.doio_q )
			{
				ioinfo[irq]->devstat.intparm = ioinfo[irq]->qintparm;

			} /* endif */

		} /* endif */

		ioinfo[irq]->ui.flags.fast    = 0;
		ioinfo[irq]->ui.flags.repall  = 0;
		ioinfo[irq]->ui.flags.w4final = 0;

		/*
		 * take fast exit if no handler is available
		 */
		if ( !ioinfo[irq]->ui.flags.ready )
			return( ending_status );     		

		memcpy( udp, &(ioinfo[irq]->devstat), sdevstat );

		ioinfo[irq]->devstat.intparm  = 0;

		if ( !ioinfo[irq]->ui.flags.s_pend )
		{
			ioinfo[irq]->irq_desc.handler( irq, udp, NULL );

		} /* endif */

		ending_status    = 1;

		break;

	} /* endswitch */

	return( ending_status );
}

/*
 * Set the special i/o-interruption sublass 7 for the
 *  device specified by parameter irq. There can only
 *  be a single device been operated on this special
 *  isc. This function is aimed being able to check
 *  on special device interrupts in disabled state,
 *  without having to delay I/O processing (by queueing)
 *  for non-console devices.
 *
 * Setting of this isc is done by set_cons_dev(), while
 *  reset_cons_dev() resets this isc and re-enables the
 *  default isc3 for this device. wait_cons_dev() allows
 *  to actively wait on an interrupt for this device in
 *  disabed state. When the interrupt condition is
 *  encountered, wait_cons_dev(9 calls do_IRQ() to have
 *  the console device driver processing the interrupt.
 */
int set_cons_dev( int irq )
{
	int           ccode;
	unsigned long cr6 __attribute__ ((aligned (8)));
	int           rc = 0;
	char dbf_txt[15];

	SANITY_CHECK(irq);

	if ( cons_dev != -1  )
	{
		rc = -EBUSY;
	}
	else
	{
		if (cio_debug_initialized) {
			debug_text_event(cio_debug_trace_id, 4, "scons");
			sprintf(dbf_txt, "%x", irq);
			debug_text_event(cio_debug_trace_id, 4, dbf_txt);
		}
		
		/*
		 * modify the indicated console device to operate
		 *  on special console interrupt sublass 7
		 */
		ccode = stsch( irq, &(ioinfo[irq]->schib) );

		if (ccode)
		{
			rc                         = -ENODEV;
			ioinfo[irq]->devstat.flag |= DEVSTAT_NOT_OPER;
		}
		else
		{
			ioinfo[irq]->schib.pmcw.isc = 7;

			ccode = msch( irq, &(ioinfo[irq]->schib) );

			if (ccode)
			{
				rc = -EIO;
			}
			else
			{
				cons_dev = irq;

				/*
				 * enable console I/O-interrupt sublass 7
				 */
				__ctl_store( cr6, 6, 6);
				cr6 |= 0x01000000;
				__ctl_load( cr6, 6, 6);

			} /* endif */

		} /* endif */

	} /* endif */

	return( rc);
}

int reset_cons_dev( int irq)
{
	int     rc = 0;
	int     ccode;
	long    cr6 __attribute__ ((aligned (8)));
	char dbf_txt[15];

	SANITY_CHECK(irq);

	if ( cons_dev != -1  )
	{
		rc = -EBUSY;
	}
	else
	{
		if (cio_debug_initialized) {
			debug_text_event(cio_debug_trace_id, 4, "rcons");
			sprintf(dbf_txt, "%x", irq);
			debug_text_event(cio_debug_trace_id, 4, dbf_txt);
		}

		/*
		 * reset the indicated console device to operate
		 *  on default console interrupt sublass 3
		 */
		ccode = stsch( irq, &(ioinfo[irq]->schib) );

		if (ccode)
		{
			rc                         = -ENODEV;
			ioinfo[irq]->devstat.flag |= DEVSTAT_NOT_OPER;
		}
		else
		{

			ioinfo[irq]->schib.pmcw.isc = 3;

			ccode = msch( irq, &(ioinfo[irq]->schib) );

			if (ccode)
			{
				rc = -EIO;
			}
			else
			{
				cons_dev = -1;

				/*
				 * disable special console I/O-interrupt sublass 7
				 */
				__ctl_store( cr6, 6, 6);
				cr6 &= 0xFEFFFFFF;
				__ctl_load( cr6, 6, 6);

			} /* endif */

		} /* endif */

	} /* endif */

	return( rc);
}

int wait_cons_dev( int irq )
{
	int              rc = 0;
	long             save_cr6;
	char dbf_txt[15];

	if ( irq == cons_dev )
	{

		if (cio_debug_initialized) {
			debug_text_event(cio_debug_trace_id, 4, "wcons");
			sprintf(dbf_txt, "%x", irq);
			debug_text_event(cio_debug_trace_id, 4, dbf_txt);
		}

		/*
		 * before entering the spinlock we may already have
		 *  processed the interrupt on a different CPU ...
		 */
		if ( ioinfo[irq]->ui.flags.busy == 1 )
		{
			long cr6 __attribute__ ((aligned (8)));

			/*
			 * disable all, but isc 7 (console device)
			 */
			__ctl_store( cr6, 6, 6);
			save_cr6  = cr6;
			cr6      &= 0x01FFFFFF;
			__ctl_load( cr6, 6, 6);

			do {
				tpi_info_t tpi_info = {0,};
				if (tpi(&tpi_info) == 1) {
					s390_process_IRQ( tpi_info.irq );
				} else {
					s390irq_spin_unlock(irq);
					udelay(100);
					s390irq_spin_lock(irq);
				}
				eieio();
			} while (ioinfo[irq]->ui.flags.busy == 1);

			/*
			 * restore previous isc value
			 */
			cr6 = save_cr6;
			__ctl_load( cr6, 6, 6);

		} /* endif */

	}
	else
	{
		rc = EINVAL;

	} /* endif */


	return(rc);
}


int enable_cpu_sync_isc( int irq )
{
	int             ccode;
	long            cr6 __attribute__ ((aligned (8)));

	int             retry = 3;
	int             rc    = 0;
	char dbf_txt[15];

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "enisc");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	/* This one spins until it can get the sync_isc lock for irq# irq */

	if ( irq <= highest_subchannel && ioinfo[irq] != INVALID_STORAGE_AREA )
	{
		if ( atomic_read( &sync_isc ) != irq )
			atomic_compare_and_swap_spin( -1, irq, &sync_isc );

		sync_isc_cnt++;
		
		if ( sync_isc_cnt > 255 ) /* fixme : magic number */
		{
			panic("Too many recursive calls to enable_sync_isc");

		}
		/*
		 * we only run the STSCH/MSCH path for the first enablement
		 */
		else if ( sync_isc_cnt == 1 )
		{
			ioinfo[irq]->ui.flags.syncio = 1;

			ccode = stsch( irq, &(ioinfo[irq]->schib) );

			if ( !ccode )
			{
				ioinfo[irq]->schib.pmcw.isc = 5;

				do
				{
					ccode = msch( irq,
					              &(ioinfo[irq]->schib) );

					switch (ccode) {
					case 0: 
						/*
						 * enable special isc
						 */
						__ctl_store( cr6, 6, 6);
						cr6 |= 0x04000000;  // enable sync isc 5
						cr6 &= 0xEFFFFFFF;  // disable standard isc 3
						__ctl_load( cr6, 6, 6);
						retry = 0;
						break;
					
					case 1:
						//
						// process pending status
						//
						ioinfo[irq]->ui.flags.s_pend = 1;
						s390_process_IRQ( irq );
						ioinfo[irq]->ui.flags.s_pend = 0;
						
						rc = -EIO;  /* might be overwritten... */
						retry--;
						break;

					case 2: /* busy */
						retry = 0;
						rc = -EBUSY; 
						break;

					case 3: /* not oper*/
						retry = 0;
						rc = -ENODEV; 
						break;
					
					}

				} while ( retry );

			}
			else
			{
				rc = -ENODEV;     // device is not-operational

			} /* endif */

		} /* endif */


		if ( rc )	/* can only happen if stsch/msch fails */
		{
			sync_isc_cnt = 0;
			atomic_set( &sync_isc, -1);
		}
	}
	else
	{
#ifdef CONFIG_SYNC_ISC_PARANOIA
		panic( "enable_sync_isc: called with invalid %x\n", irq );
#endif

		rc = -EINVAL;

	} /* endif */

	return( rc);
}

int disable_cpu_sync_isc( int irq)
{
	int     rc         = 0;
	int     retry1     = 5;
	int     retry2     = 5;
	int     clear_pend = 0;

	int     ccode;
	long    cr6 __attribute__ ((aligned (8)));

	char dbf_txt[15];

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "disisc");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	if ( irq <= highest_subchannel && ioinfo[irq] != INVALID_STORAGE_AREA )
	{
		/*
		 * We disable if we're the top user only, as we may
		 *  run recursively ...	
		 * We must not decrease the count immediately; during
		 *  msch() processing we may face another pending
		 *  status we have to process recursively (sync).
		 */

#ifdef CONFIG_SYNC_ISC_PARANOIA
		if ( atomic_read( &sync_isc ) != irq )
			panic( "disable_sync_isc: called for %x while %x locked\n",
				irq, atomic_read( &sync_isc ) );
#endif

		if ( sync_isc_cnt == 1 )
		{
			ccode = stsch( irq, &(ioinfo[irq]->schib) );

			ioinfo[irq]->schib.pmcw.isc = 3;

			do
			{
			        retry2 = 5;
				do
				{
					ccode = msch( irq, &(ioinfo[irq]->schib) );

					switch ( ccode ) {
					case 0:
						/*
						 * disable special interrupt subclass in CPU
						 */
						__ctl_store( cr6, 6, 6);
						cr6 &= 0xFBFFFFFF; // disable sync isc 5
						cr6 |= 0x10000000; // enable standard isc 3
						__ctl_load( cr6, 6, 6);

						retry2 = 0;
						break;

					case 1: /* status pending */
						ioinfo[irq]->ui.flags.s_pend = 1;
						s390_process_IRQ( irq );
						ioinfo[irq]->ui.flags.s_pend = 0;

						retry2--;
						break;

					case 2: /* busy */
					        retry2--;
						udelay( 100); // give it time
						break;
						
					default: /* not oper */
						retry2 = 0;
						break;
					} /* endswitch */

				} while ( retry2 );

				retry1--;

				/* try stopping it ... */
				if ( (ccode) && !clear_pend )
				{
					clear_IO( irq, 0x00004711, 0 );
					clear_pend = 1;
               	
				} /* endif */

				udelay( 100);

			} while ( retry1 && ccode );

			ioinfo[irq]->ui.flags.syncio = 0;
		
			sync_isc_cnt = 0;	
			atomic_set( &sync_isc, -1);

		}
		else
		{
			sync_isc_cnt--;

		} /* endif */
	}
	else
	{
#ifdef CONFIG_SYNC_ISC_PARANOIA
		if ( atomic_read( &sync_isc ) != -1 )
			panic( "disable_sync_isc: called with invalid %x while %x locked\n", 
				irq, atomic_read( &sync_isc ) );
#endif

		rc = -EINVAL;

	} /* endif */

	return( rc);
}

//
// Input :
//   devno - device number
//   ps    - pointer to sense ID data area
//
// Output : none
//
void VM_virtual_device_info( __u16      devno,
                             senseid_t *ps )
{
	diag210_t  *p_diag_data;
	int        ccode;

	int        error = 0;

	if (cio_debug_initialized) 
		debug_text_event(cio_debug_trace_id, 4, "VMvdinf");

	if ( init_IRQ_complete )
	{
		p_diag_data = kmalloc( sizeof( diag210_t), GFP_DMA );
	}
	else
	{
		p_diag_data = alloc_bootmem_low( sizeof( diag210_t));

	} /* endif */

	p_diag_data->vrdcdvno = devno;
	p_diag_data->vrdclen  = sizeof( diag210_t);
	ccode                 = diag210( (diag210_t *)virt_to_phys( p_diag_data ) );
	ps->reserved          = 0xff;

	switch (p_diag_data->vrdcvcla) {
	case 0x80:

		switch (p_diag_data->vrdcvtyp) {
		case 00:

			ps->cu_type   = 0x3215;

			break;

		default:

			error = 1;

			break;

		} /* endswitch */

		break;

	case 0x40:

		switch (p_diag_data->vrdcvtyp) {
		case 0xC0:

			ps->cu_type   = 0x5080;

			break;

		case 0x80:

			ps->cu_type   = 0x2250;

			break;

		case 0x04:

			ps->cu_type   = 0x3277;

			break;

		case 0x01:

			ps->cu_type   = 0x3278;

			break;

		default:

			error = 1;

			break;

		} /* endswitch */

		break;

	case 0x20:

		switch (p_diag_data->vrdcvtyp) {
		case 0x84:

			ps->cu_type   = 0x3505;

			break;

		case 0x82:

			ps->cu_type   = 0x2540;

			break;

		case 0x81:

			ps->cu_type   = 0x2501;

			break;

		default:

			error = 1;

			break;

		} /* endswitch */

		break;

	case 0x10:

		switch (p_diag_data->vrdcvtyp) {
		case 0x84:

			ps->cu_type   = 0x3525;

			break;

		case 0x82:

			ps->cu_type   = 0x2540;

			break;

		case 0x4F:
		case 0x4E:
		case 0x48:

			ps->cu_type   = 0x3820;

			break;

		case 0x4D:
		case 0x49:
		case 0x45:

			ps->cu_type   = 0x3800;

			break;

		case 0x4B:

			ps->cu_type   = 0x4248;

			break;

		case 0x4A:

			ps->cu_type   = 0x4245;

			break;

		case 0x47:

			ps->cu_type   = 0x3262;

			break;

		case 0x43:

			ps->cu_type   = 0x3203;

			break;

		case 0x42:

			ps->cu_type   = 0x3211;

			break;

		case 0x41:

			ps->cu_type   = 0x1403;

			break;

		default:

			error = 1;

			break;

		} /* endswitch */

		break;

	case 0x08:

		switch (p_diag_data->vrdcvtyp) {
		case 0x82:

			ps->cu_type   = 0x3422;

			break;

		case 0x81:

			ps->cu_type   = 0x3490;

			break;

		case 0x10:

			ps->cu_type   = 0x3420;

			break;

		case 0x02:

			ps->cu_type   = 0x3430;

			break;

		case 0x01:

			ps->cu_type   = 0x3480;

			break;

		case 0x42:

			ps->cu_type   = 0x3424;

			break;

		case 0x44:

			ps->cu_type   = 0x9348;

			break;

		default:

			error = 1;

			break;

		} /* endswitch */

		break;

	case 02: /* special device class ... */

		switch (p_diag_data->vrdcvtyp) {
		case 0x20: /* OSA */

			ps->cu_type   = 0x3088;
			ps->cu_model  = 0x60;

			break;

		default:

			error = 1;
      	break;

		} /* endswitch */

		break;

	default:

		error = 1;

		break;

	} /* endswitch */

	if ( init_IRQ_complete )
	{
		kfree( p_diag_data );
	}
	else
	{
		free_bootmem( (unsigned long)p_diag_data, sizeof( diag210_t) );

	} /* endif */

	if ( error )
	{
		printk( "DIAG X'210' for "
		        "device %04X returned "
		        "(cc = %d): vdev class : %02X, "
			"vdev type : %04X \n ...  rdev class : %02X, rdev type : %04X, rdev model: %02X\n",
			devno,
			ccode,
			p_diag_data->vrdcvcla,
			p_diag_data->vrdcvtyp,
			p_diag_data->vrdcrccl,
			p_diag_data->vrdccrty,
			p_diag_data->vrdccrmd );
		if (cio_debug_initialized)
			debug_sprintf_event( cio_debug_msg_id, 0,
					     "DIAG X'210' for "
					     "device %04X returned "
					     "(cc = %d): vdev class : %02X, "
					     "vdev type : %04X \n ...  rdev class : %02X, rdev type : %04X, rdev model: %02X\n",
					     devno,
					     ccode,
					     p_diag_data->vrdcvcla,
					     p_diag_data->vrdcvtyp,
					     p_diag_data->vrdcrccl,
					     p_diag_data->vrdccrty,
					     p_diag_data->vrdccrmd );

	} /* endif */

}

/*
 * This routine returns the characteristics for the device
 *  specified. Some old devices might not provide the necessary
 *  command code information during SenseID processing. In this
 *  case the function returns -EINVAL. Otherwise the function
 *  allocates a decice specific data buffer and provides the
 *  device characteristics together with the buffer size. Its
 *  the callers responability to release the kernel memory if
 *  not longer needed. In case of persistent I/O problems -EBUSY
 *  is returned.
 *
 *  The function may be called enabled or disabled. However, the
 *   caller must have locked the irq it is requesting data for.
 *
 * Note : It would have been nice to collect this information
 *         during init_IRQ() processing but this is not possible
 *
 *         a) without statically pre-allocation fixed size buffers
 *            as virtual memory management isn't available yet.
 *
 *         b) without unnecessarily increase system startup by
 *            evaluating devices eventually not used at all.
 */
int read_dev_chars( int irq, void **buffer, int length )
{
	unsigned int  flags;
	ccw1_t       *rdc_ccw;
	devstat_t     devstat;
	char         *rdc_buf;
	int           devflag = 0;

	int           ret      = 0;
	int           emulated = 0;
	int           retry    = 5;

	char dbf_txt[15];

	if ( !buffer || !length )
	{
		return( -EINVAL );

	} /* endif */

	SANITY_CHECK(irq);

	if ( ioinfo[irq]->ui.flags.oper == 0 )
	{
		return( -ENODEV );

	} /* endif */

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "rddevch");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	/*
	 * Before playing around with irq locks we should assure
	 *   running disabled on (just) our CPU. Sync. I/O requests
	 *   also require to run disabled.
	 *
	 * Note : as no global lock is required, we must not use
	 *        cli(), but __cli() instead.  	
	 */
	__save_flags(flags);
	__cli();

	rdc_ccw = &ioinfo[irq]->senseccw;

	if ( !ioinfo[irq]->ui.flags.ready )
	{
		ret = request_irq( irq,
		                   init_IRQ_handler,
		                   SA_PROBE, "RDC", &devstat );

		if ( !ret )
		{
			emulated = 1;

		} /* endif */

	} /* endif */

	if ( !ret )
	{
		if ( ! *buffer )
		{
			rdc_buf  = kmalloc( length, GFP_KERNEL);
		}
		else
		{
			rdc_buf = *buffer;

		} /* endif */

		if ( !rdc_buf )
		{
			ret = -ENOMEM;
		}
		else
		{
			do
			{
				rdc_ccw->cmd_code = CCW_CMD_RDC;
				rdc_ccw->count    = length;
				rdc_ccw->flags    = CCW_FLAG_SLI;
				ret = set_normalized_cda( rdc_ccw, (unsigned long)rdc_buf );
				if (!ret) {

					memset( ioinfo[irq]->irq_desc.dev_id,
						'\0',
						sizeof( devstat_t));

					ret = s390_start_IO( irq,
							     rdc_ccw,
							     0x00524443, // RDC
							     0,          // n/a
							     DOIO_WAIT_FOR_INTERRUPT
							     | DOIO_DONT_CALL_INTHDLR );
					retry--;
					devflag = ioinfo[irq]->irq_desc.dev_id->flag;   
					
					clear_normalized_cda( rdc_ccw);  
				} else {
					udelay(100);  //wait for recovery
					retry--;
				}

			} while (    ( retry                                     )
			          && ( ret || (devflag & DEVSTAT_STATUS_PENDING) ) );

		} /* endif */

		if ( !retry )
		{
			ret = (ret==-ENOMEM)?-ENOMEM:-EBUSY;

		} /* endif */

		__restore_flags(flags);

		/*
		 * on success we update the user input parms
		 */
		if ( !ret )
		{
			*buffer = rdc_buf;

		} /* endif */

		if ( emulated )
		{
			free_irq( irq, &devstat);

		} /* endif */

	} /* endif */

	return( ret );
}

/*
 *  Read Configuration data
 */
int read_conf_data( int irq, void **buffer, int *length, __u8 lpm )
{
	unsigned long flags;
	int           ciw_cnt;

	int           found  = 0; // RCD CIW found
	int           ret    = 0; // return code

	char dbf_txt[15];

	SANITY_CHECK(irq);

	if ( !buffer || !length )
	{
		return( -EINVAL);
	}
	else if ( ioinfo[irq]->ui.flags.oper == 0 )
	{
		return( -ENODEV );
	}
	else if ( ioinfo[irq]->ui.flags.esid == 0 )
	{
		return( -EOPNOTSUPP );

	} /* endif */

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "rdconf");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	/*
	 * scan for RCD command in extended SenseID data
	 */
	
	for ( ciw_cnt = 0; (found == 0) && (ciw_cnt < 62); ciw_cnt++ )
	{
		if ( ioinfo[irq]->senseid.ciw[ciw_cnt].ct == CIW_TYPE_RCD )
		{
			/*
			 * paranoia check ...
			 */
			if ( ioinfo[irq]->senseid.ciw[ciw_cnt].cmd != 0 )
			{
				found = 1;

			} /* endif */

			break;

		} /* endif */

	} /* endfor */

	if ( found )
	{
		devstat_t  devstat;  /* inline device status area */
		devstat_t *pdevstat;
		int        ioflags;

		ccw1_t    *rcd_ccw  = &ioinfo[irq]->senseccw;
		char      *rcd_buf  = NULL;
		int        emulated = 0; /* no i/O handler installed */
		int        retry    = 5; /* retry count */

		__save_flags(flags);
		__cli();

		if ( !ioinfo[irq]->ui.flags.ready )
		{
			pdevstat = &devstat;
			ret      = request_irq( irq,
			                        init_IRQ_handler,
			                        SA_PROBE, "RCD", pdevstat );

			if ( !ret )
			{
				emulated = 1;

			} /* endif */
		}
		else
		{
			pdevstat = ioinfo[irq]->irq_desc.dev_id;

		} /* endif */

		if ( !ret )
		{
			if ( init_IRQ_complete )
			{
				rcd_buf = kmalloc( ioinfo[irq]->senseid.ciw[ciw_cnt].count,
				                   GFP_DMA);
			}
			else
			{
				rcd_buf = alloc_bootmem_low( ioinfo[irq]->senseid.ciw[ciw_cnt].count);

   		} /* endif */

			if ( rcd_buf == NULL )
			{
				ret = -ENOMEM;	

			} /* endif */

			if ( !ret )
			{
				memset( rcd_buf,
				        '\0',
				        ioinfo[irq]->senseid.ciw[ciw_cnt].count);
       	
				do
				{
					rcd_ccw->cmd_code = ioinfo[irq]->senseid.ciw[ciw_cnt].cmd;
					rcd_ccw->cda      = (__u32)virt_to_phys( rcd_buf );
					rcd_ccw->count    = ioinfo[irq]->senseid.ciw[ciw_cnt].count;
					rcd_ccw->flags    = CCW_FLAG_SLI;

					memset( pdevstat, '\0', sizeof( devstat_t));

					if ( lpm )
					{
						ioflags = DOIO_WAIT_FOR_INTERRUPT
						          | DOIO_VALID_LPM    					
						          | DOIO_DONT_CALL_INTHDLR;
					}
					else
					{
						ioflags =   DOIO_WAIT_FOR_INTERRUPT
						          | DOIO_DONT_CALL_INTHDLR;
						             					
					} /* endif */

					ret = s390_start_IO( irq,
					                     rcd_ccw,
					                     0x00524344,  // == RCD
					                     lpm,
					                     ioflags );
					switch ( ret ) {
					case 0:
					case -EIO:
						
						if ( !(pdevstat->flag & (   DEVSTAT_STATUS_PENDING
									    | DEVSTAT_NOT_OPER
									    | DEVSTAT_FLAG_SENSE_AVAIL ) ) )
						{
							retry = 0;  // we got it ...
						}
						else
						{
							retry--;    // try again ...
								
						} /* endif */
						
						break;

					default:   // -EBUSY, -ENODEV, ???
						retry = 0;
						
					} /* endswitch */
					
				} while ( retry );

			} /* endif */

			__restore_flags( flags );

		} /* endif */

		/*
		 * on success we update the user input parms
		 */
		if ( ret == 0 )
		{
			*length = ioinfo[irq]->senseid.ciw[ciw_cnt].count;
			*buffer = rcd_buf;
		}
		else
		{
			if ( rcd_buf != NULL )
			{
				if ( init_IRQ_complete )
				{
					kfree( rcd_buf );
				}
				else
				{
					free_bootmem( (unsigned long)rcd_buf,
					              ioinfo[irq]->senseid.ciw[ciw_cnt].count);

   			} /* endif */

			} /* endif */

			*buffer = NULL;
			*length = 0;
    	
		} /* endif */

		if ( emulated )
			free_irq( irq, pdevstat);
	}
	else
	{
		ret = -EOPNOTSUPP;

	} /* endif */

	return( ret );

}

int get_dev_info( int irq, s390_dev_info_t * pdi)
{
	return( get_dev_info_by_irq( irq, pdi));
}

static int __inline__ get_next_available_irq( ioinfo_t *pi)
{
	int ret_val = -ENODEV;

	while ( pi!=NULL ) {
		if ( pi->ui.flags.oper ) {
			ret_val = pi->irq;
			break;
		} else {
			pi = pi->next;
		}
	}

	return ret_val;
}


int get_irq_first( void )
{
   int ret_irq;

	if ( ioinfo_head )
	{
		if ( ioinfo_head->ui.flags.oper )
		{
			ret_irq = ioinfo_head->irq;
		}
		else if ( ioinfo_head->next )
		{
			ret_irq = get_next_available_irq( ioinfo_head->next );

		}
		else
		{
			ret_irq = -ENODEV;
   	
		} /* endif */
	}
	else
	{
		ret_irq = -ENODEV;

	} /* endif */

	return ret_irq;
}

int get_irq_next( int irq )
{
	int ret_irq;	

	if ( ioinfo[irq] != INVALID_STORAGE_AREA )
	{
		if ( ioinfo[irq]->next )
		{
			if ( ioinfo[irq]->next->ui.flags.oper )
			{
				ret_irq = ioinfo[irq]->next->irq;
			}
			else
			{
				ret_irq = get_next_available_irq( ioinfo[irq]->next );

			} /* endif */
		}
		else
		{
			ret_irq = -ENODEV;     	

		} /* endif */
	}
	else
	{
		ret_irq = -EINVAL;

	} /* endif */

	return ret_irq;
}

int get_dev_info_by_irq( int irq, s390_dev_info_t *pdi)
{

	SANITY_CHECK(irq);

	if ( pdi == NULL )
		return -EINVAL;

	pdi->devno = ioinfo[irq]->schib.pmcw.dev;
	pdi->irq   = irq;
	
	if (   ioinfo[irq]->ui.flags.oper
	    && !ioinfo[irq]->ui.flags.unknown ) 
	{
		pdi->status = 0;
		memcpy( &(pdi->sid_data),
			&ioinfo[irq]->senseid,
			sizeof( senseid_t));
	}
	else if ( ioinfo[irq]->ui.flags.unknown )
	{
		pdi->status = DEVSTAT_UNKNOWN_DEV;
		memset( &(pdi->sid_data),
			'\0',
			sizeof( senseid_t));
		pdi->sid_data.cu_type = 0xFFFF;
			
	}
	else
	{
		pdi->status = DEVSTAT_NOT_OPER;
		memset( &(pdi->sid_data),
			'\0',
			sizeof( senseid_t));
		pdi->sid_data.cu_type = 0xFFFF;
		
	} /* endif */
	
	if ( ioinfo[irq]->ui.flags.ready )
		pdi->status |= DEVSTAT_DEVICE_OWNED;


	return 0;
}


int get_dev_info_by_devno( __u16 devno, s390_dev_info_t *pdi)
{
	int i;
	int rc = -ENODEV;

	if ( devno > 0x0000ffff )
		return -ENODEV;
        if ( pdi == NULL )
		return -EINVAL;

	for ( i=0; i <= highest_subchannel; i++ ) {
		
		if (    ioinfo[i] != INVALID_STORAGE_AREA
		     && ioinfo[i]->schib.pmcw.dev == devno )
		{
			
			pdi->irq   = i;
			pdi->devno = devno;
			
			if (    ioinfo[i]->ui.flags.oper
			     && !ioinfo[i]->ui.flags.unknown )
			{
				pdi->status = 0;
				memcpy( &(pdi->sid_data),
					&ioinfo[i]->senseid,
					sizeof( senseid_t));
			}
			else if ( ioinfo[i]->ui.flags.unknown )
			{
				pdi->status = DEVSTAT_UNKNOWN_DEV;

				memset( &(pdi->sid_data),
					'\0',
					sizeof( senseid_t));

				pdi->sid_data.cu_type = 0xFFFF;
			}
			else
			{
				pdi->status = DEVSTAT_NOT_OPER;
				
				memset( &(pdi->sid_data),
					'\0',
					sizeof( senseid_t));

				pdi->sid_data.cu_type = 0xFFFF;

			} /* endif */

			if ( ioinfo[i]->ui.flags.ready )
				pdi->status |= DEVSTAT_DEVICE_OWNED;

			rc = 0; /* found */
			break;

		} /* endif */

	} /* endfor */

	return( rc);

}

int get_irq_by_devno( __u16 devno )
{
	int i;
	int rc = -1;

	if ( devno <= 0x0000ffff )
	{
		for ( i=0; i <= highest_subchannel; i++ )
		{
			if (    (ioinfo[i] != INVALID_STORAGE_AREA )
			     && (ioinfo[i]->schib.pmcw.dev == devno)
			     && (ioinfo[i]->schib.pmcw.dnv == 1    ) )
			{
				rc = i;
				break;

			} /* endif */

		} /* endfor */
	
	} /* endif */

	return( rc);
}

unsigned int get_devno_by_irq( int irq )
{

	if (    ( irq > highest_subchannel            )
	     || ( irq < 0                             )
	     || ( ioinfo[irq] == INVALID_STORAGE_AREA ) )
	{
		return -1;
	
	} /* endif */

	/*
	 * we don't need to check for the device be operational
	 *  as the initial STSCH will always present the device
	 *  number defined by the IOCDS regardless of the device
	 *  existing or not. However, there could be subchannels
	 *  defined who's device number isn't valid ...
	 */
	if ( ioinfo[irq]->schib.pmcw.dnv )
		return( ioinfo[irq]->schib.pmcw.dev );
	else
		return -1;
}

/*
 * s390_device_recognition_irq
 *
 * Used for individual device recognition. Issues the device
 *  independant SenseID command to obtain info the device type.
 *
 */
void s390_device_recognition_irq( int irq )
{
	int           ret;
	char dbf_txt[15];

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "devrec");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	/*
	 * We issue the SenseID command on I/O subchannels we think are
	 *  operational only.
	 */
	if (    ( ioinfo[irq] != INVALID_STORAGE_AREA )	
	     && ( ioinfo[irq]->schib.pmcw.st == 0     )
	     && ( ioinfo[irq]->ui.flags.oper == 1     ) )
	{
		int       irq_ret;
		devstat_t devstat;

		irq_ret = request_irq( irq,
		                       init_IRQ_handler,
		                       SA_PROBE,
		                       "INIT",
		                       &devstat);

		if ( !irq_ret )
		{
			ret = enable_cpu_sync_isc( irq );

			if ( !ret ) 
			{
				ioinfo[irq]->ui.flags.unknown = 0;

				memset( &ioinfo[irq]->senseid, '\0', sizeof( senseid_t));

				s390_SenseID( irq, &ioinfo[irq]->senseid, 0xff );
#if 0	/* FIXME */
				/*
				 * We initially check the configuration data for
				 *  those devices with more than a single path
				 */
				if ( ioinfo[irq]->schib.pmcw.pim != 0x80 )
				{
					char     *prcd;
					int       lrcd;

					ret = read_conf_data( irq, (void **)&prcd, &lrcd, 0 );

					if ( !ret )	// on success only ...
					{
						char buffer[80];
#ifdef CONFIG_DEBUG_IO
						sprintf( buffer,
						         "RCD for device(%04X)/"
						         "subchannel(%04X) returns :\n",
						         ioinfo[irq]->schib.pmcw.dev,
						         irq );

						s390_displayhex( buffer, prcd, lrcd );
#endif      				
						if (cio_debug_initialized) {
							sprintf( buffer,
								 "RCD for device(%04X)/"
								 "subchannel(%04X) returns :\n",
								 ioinfo[irq]->schib.pmcw.dev,
								 irq );
							
							s390_displayhex2( buffer, prcd, lrcd, 2);
						}
						if ( init_IRQ_complete )
						{
							kfree( prcd );
						}
						else
						{
							free_bootmem( (unsigned long)prcd, lrcd );

 			  			} /* endif */

					} /* endif */
					
				} /* endif */
#endif

				disable_cpu_sync_isc( irq );

			} /* endif */  	

			free_irq( irq, &devstat );

		} /* endif */
		
	} /* endif */

}

/*
 * s390_device_recognition_all
 *
 * Used for system wide device recognition.
 *
 */
void s390_device_recognition_all( void)
{
	int irq = 0; /* let's start with subchannel 0 ... */

	do
	{
		s390_device_recognition_irq( irq );

		irq ++;

	} while ( irq <= highest_subchannel );

}

/*
 * Function: s390_redo_validation
 * Look for no longer blacklisted devices
 * FIXME: there must be a better way to do this...
 */

void s390_redo_validation(void) 
{
	int irq = 0;
	int ret;

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 0, "redoval");
	}
	do {
		if (ioinfo[irq] == INVALID_STORAGE_AREA) {
			ret = s390_validate_subchannel(irq, 0);
			if (!ret) {
				s390_device_recognition_irq(irq);
				if (ioinfo[irq]->ui.flags.oper) {
					devreg_t *pdevreg;
					
					pdevreg = s390_search_devreg( ioinfo[irq] );
					if ( pdevreg != NULL ) {
						if ( pdevreg->oper_func != NULL )
							pdevreg->oper_func( irq, pdevreg );
						
					} 
				}
				if (cio_proc_devinfo) 
					if (irq < MAX_CIO_PROCFS_ENTRIES) {
						cio_procfs_device_create(ioinfo[irq]->devno);
				}
			}
		}
		irq++;
	} while (irq<=highest_subchannel);
}

/*
 * s390_search_devices
 *
 * Determines all subchannels available to the system.
 *
 */
void s390_process_subchannels( void)
{
	int   ret;
	int   irq = 0;   /* Evaluate all subchannels starting with 0 ... */

	do
	{
		ret = s390_validate_subchannel( irq, 0);

		if ( ret != -ENXIO)
			irq++;
	
  	} while ( (ret != -ENXIO) && (irq < __MAX_SUBCHANNELS) );

	highest_subchannel = (--irq);

	printk( "Highest subchannel number detected (hex) : %04X\n",
	        highest_subchannel);
	if (cio_debug_initialized)
		debug_sprintf_event(cio_debug_msg_id, 0, 
				    "Highest subchannel number detected (hex) : %04X\n",
				    highest_subchannel);	
}

/*
 * s390_validate_subchannel()
 *
 * Process the subchannel for the requested irq. Returns 1 for valid
 *  subchannels, otherwise 0.
 */
int s390_validate_subchannel( int irq, int enable )
{

	int      retry;     /* retry count for status pending conditions */
	int      ccode;     /* condition code for stsch() only */
	int      ccode2;    /* condition code for other I/O routines */
	schib_t *p_schib;
	int      ret;
	
	char dbf_txt[15];

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "valsch");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	/*
	 * The first subchannel that is not-operational (ccode==3)
	 *  indicates that there aren't any more devices available.
	 */
	if (    ( init_IRQ_complete                   )
	     && ( ioinfo[irq] != INVALID_STORAGE_AREA ) )
	{
		p_schib = &ioinfo[irq]->schib;
	}
	else
	{
		p_schib = p_init_schib;

	} /* endif */

	/*
	 * If we knew the device before we assume the worst case ... 	
	 */
	if ( ioinfo[irq] != INVALID_STORAGE_AREA )
	{
		ioinfo[irq]->ui.flags.oper = 0;
		ioinfo[irq]->ui.flags.dval = 0;

	} /* endif */

	ccode = stsch( irq, p_schib);

	if ( !ccode )
	{
		/*
		 * ... just being curious we check for non I/O subchannels
		 */
		if ( p_schib->pmcw.st )
		{
			printk( "Subchannel %04X reports "
			        "non-I/O subchannel type %04X\n",
			        irq,
			        p_schib->pmcw.st);
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_msg_id, 0,
						    "Subchannel %04X reports non-I/O subchannel type %04X\n",
						    irq, p_schib->pmcw.st);

			if ( ioinfo[irq] != INVALID_STORAGE_AREA )
				ioinfo[irq]->ui.flags.oper = 0;

		} /* endif */

		if ( p_schib->pmcw.dnv )
		{
		     if ( is_blacklisted( p_schib->pmcw.dev )) {
			  /* 
			   * This device must not be known to Linux. So we simply say that 
			   * there is no device and return ENODEV.
			   */
#ifdef CONFIG_DEBUG_IO
			  printk( "Blacklisted device detected at devno %04X\n", p_schib->pmcw.dev );
#endif
			  if (cio_debug_initialized)
				  debug_sprintf_event(cio_debug_msg_id, 0,
						      "Blacklisted device detected at devno %04X\n",
						      p_schib->pmcw.dev);
			  ret = -ENODEV;
		     } else {
		        if ( ioinfo[irq] == INVALID_STORAGE_AREA )
			{	
				if ( !init_IRQ_complete )
				{
					ioinfo[irq] =
					   (ioinfo_t *)alloc_bootmem_low( sizeof(ioinfo_t));
				}
				else
				{
					ioinfo[irq] =
					   (ioinfo_t *)kmalloc( sizeof(ioinfo_t),
				                           GFP_DMA );

				} /* endif */

				memset( ioinfo[irq], '\0', sizeof( ioinfo_t));
				memcpy( &ioinfo[irq]->schib,
			           p_init_schib,
			           sizeof( schib_t));
			
				/*
				 * We have to insert the new ioinfo element
				 *  into the linked list, either at its head,
				 *  its tail or insert it.
				 */
				if ( ioinfo_head == NULL )  /* first element */
				{
					ioinfo_head = ioinfo[irq];
					ioinfo_tail = ioinfo[irq];
				}
				else if ( irq < ioinfo_head->irq ) /* new head */
				{
					ioinfo[irq]->next = ioinfo_head;
					ioinfo_head->prev = ioinfo[irq];
					ioinfo_head       = ioinfo[irq];
				}
				else if ( irq > ioinfo_tail->irq ) /* new tail */
				{
					ioinfo_tail->next = ioinfo[irq];
					ioinfo[irq]->prev = ioinfo_tail;
					ioinfo_tail       = ioinfo[irq];
				}
				else /* insert element */
				{
					ioinfo_t *pi = ioinfo_head;

					for (pi=ioinfo_head; pi!=NULL; pi=pi->next) {

						if ( irq < pi->next->irq )
						{
							ioinfo[irq]->next = pi->next;
							ioinfo[irq]->prev = pi;
							pi->next->prev    = ioinfo[irq];
							pi->next          = ioinfo[irq];
							break;
						
						} /* endif */
					}
				} /* endif */

			} /* endif */

			// initialize some values ...	
			ioinfo[irq]->ui.flags.pgid_supp = 1;

			ioinfo[irq]->opm =   ioinfo[irq]->schib.pmcw.pim
			                   & ioinfo[irq]->schib.pmcw.pam
			                   & ioinfo[irq]->schib.pmcw.pom;

			if ( cio_show_msg )
			{
				printk( KERN_INFO"Detected device %04X "
				        "on subchannel %04X"
				        " - PIM = %02X, PAM = %02X, POM = %02X\n",
				        ioinfo[irq]->schib.pmcw.dev,
				        irq,
			   	     ioinfo[irq]->schib.pmcw.pim,
			      	  ioinfo[irq]->schib.pmcw.pam,
			 	       ioinfo[irq]->schib.pmcw.pom);

			} /* endif */
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_msg_id, 0,
						    "Detected device %04X "
						    "on subchannel %04X"
						    " - PIM = %02X, PAM = %02X, POM = %02X\n",
						    ioinfo[irq]->schib.pmcw.dev,
						    irq,
						    ioinfo[irq]->schib.pmcw.pim,
						    ioinfo[irq]->schib.pmcw.pam,
						    ioinfo[irq]->schib.pmcw.pom);

			/*
			 * initialize ioinfo structure
			 */
			ioinfo[irq]->irq             = irq;
			if(!ioinfo[irq]->ui.flags.ready)
			{
				ioinfo[irq]->nopfunc         = NULL;
				ioinfo[irq]->ui.flags.busy   = 0;
				ioinfo[irq]->ui.flags.dval   = 1;
				ioinfo[irq]->devstat.intparm = 0;
				
			}
			ioinfo[irq]->devstat.devno   = ioinfo[irq]->schib.pmcw.dev;
			ioinfo[irq]->devno           = ioinfo[irq]->schib.pmcw.dev;

			/*
			 * We should have at least one CHPID ...
			 */
			if ( ioinfo[irq]->opm )
			{
				/*
				 * We now have to initially ...
				 *  ... set "interruption sublass"
				 *  ... enable "concurrent sense"
				 *  ... enable "multipath mode" if more than one
				 *        CHPID is available. This is done regardless
				 *        whether multiple paths are available for us.
				 *
				 * Note : we don't enable the device here, this is temporarily
				 *        done during device sensing below.
				 */
				ioinfo[irq]->schib.pmcw.isc     = 3; /* could be smth. else */
				ioinfo[irq]->schib.pmcw.csense  = 1; /* concurrent sense */
				ioinfo[irq]->schib.pmcw.ena     = enable;
				ioinfo[irq]->schib.pmcw.intparm =
				                     ioinfo[irq]->schib.pmcw.dev;

				if (    ( ioinfo[irq]->opm != 0x80 )
				     && ( ioinfo[irq]->opm != 0x40 )
				     && ( ioinfo[irq]->opm != 0x20 )
				     && ( ioinfo[irq]->opm != 0x10 )
				     && ( ioinfo[irq]->opm != 0x08 )
				     && ( ioinfo[irq]->opm != 0x04 )
				     && ( ioinfo[irq]->opm != 0x02 )
				     && ( ioinfo[irq]->opm != 0x01 ) )
				{
					ioinfo[irq]->schib.pmcw.mp = 1; /* multipath mode */

				} /* endif */

				retry = 5;

				do
				{
					ccode2 = msch_err( irq, &ioinfo[irq]->schib);

					switch (ccode2) {
					case 0:  // successful completion
						//
						// concurrent sense facility available ...
						//
						ioinfo[irq]->ui.flags.oper   = 1;
						ioinfo[irq]->ui.flags.consns = 1;
						ret                          = 0;
						break;
      	
					case 1:  // status pending
						//
						// How can we have a pending status as
						//  device is disabled for interrupts ?
						//  Anyway, process it ...
						//
						ioinfo[irq]->ui.flags.s_pend = 1;
						s390_process_IRQ( irq);
						ioinfo[irq]->ui.flags.s_pend = 0;
						retry--;
						ret = -EIO;
						break;
   	
					case 2:  // busy
						/*
						 * we mark it not-oper as we can't
						 *  properly operate it !
						 */
						ioinfo[irq]->ui.flags.oper = 0;
						udelay( 100);	/* allow for recovery */
						retry--;
						ret = -EBUSY;
						break;

					case 3:  // not operational
						ioinfo[irq]->ui.flags.oper = 0;
						retry                      = 0;
						ret = -ENODEV;
						break;

					default:
#define PGMCHK_OPERAND_EXC      0x15

						if ( (ccode2 & PGMCHK_OPERAND_EXC) == PGMCHK_OPERAND_EXC )
						{
							/*
							 * re-issue the modify subchannel without trying to
							 *  enable the concurrent sense facility
							 */
							ioinfo[irq]->schib.pmcw.csense = 0;
   	
							ccode2 = msch_err( irq, &ioinfo[irq]->schib);

							if ( ccode2 != 0 )
							{
								printk( " ... msch() (2) failed with CC = %X\n",
								        ccode2 );
								if (cio_debug_initialized)
									debug_sprintf_event(cio_debug_msg_id, 0,
											    "msch() (2) failed with CC=%X\n",
											    ccode2);
								ioinfo[irq]->ui.flags.oper = 0;
								ret                        = -EIO;
							}
							else
							{
								ioinfo[irq]->ui.flags.oper   = 1;
								ioinfo[irq]->ui.flags.consns = 0;
								ret                          = 0;

							} /* endif */
						}
						else
						{
							printk( " ... msch() (1) failed with CC = %X\n",
							        ccode2);
							if (cio_debug_initialized)
								debug_sprintf_event(cio_debug_msg_id, 0,
										    "msch() (1) failed with CC = %X\n",
										    ccode2);
							ioinfo[irq]->ui.flags.oper = 0;
							ret                        = -EIO;

						} /* endif */
   	
						retry  = 0;
						break;

					} /* endswitch */

				} while ( ccode2 && retry );

				if ( (ccode2 != 0) && (ccode2 != 3) && (!retry) )
				{
					printk( " ... msch() retry count for "
					        "subchannel %04X exceeded, CC = %d\n",
					        irq,
					        ccode2);
					if (cio_debug_initialized)
						debug_sprintf_event(cio_debug_msg_id, 0,
								    " ... msch() retry count for "
								    "subchannel %04X exceeded, CC = %d\n",
								    irq, ccode2);		    

				} /* endif */
			}
			else
			{
				/* no path available ... */
				ioinfo[irq]->ui.flags.oper = 0;
				ret                        = -ENODEV;    	

			} /* endif */
		     }
		}
		else
		{
			ret = -ENODEV;

		} /* endif */
	}
	else
	{
		ret = -ENXIO;

	} /* endif */

	return( ret );
}

/*
 * s390_SenseID
 *
 * Try to obtain the 'control unit'/'device type' information
 *  associated with the subchannel.
 *
 * The function is primarily meant to be called without irq
 *  action handler in place. However, it also allows for
 *  use with an action handler in place. If there is already
 *  an action handler registered assure it can handle the
 *  s390_SenseID() related device interrupts - interruption
 *  parameter used is 0x00E2C9C4 ( SID ).
 */
int s390_SenseID( int irq, senseid_t *sid, __u8 lpm )
{
	ccw1_t    *sense_ccw;     /* ccw area for SenseID command */
	senseid_t  isid;          /* internal sid */				
	devstat_t  devstat;       /* required by request_irq() */
	__u8       pathmask;      /* calulate path mask */
	__u8       domask;        /* path mask to use */
	int        inlreq;        /* inline request_irq() */
	int        irq_ret;       /* return code */
	devstat_t *pdevstat;      /* ptr to devstat in use */
	int        retry;         /* retry count */
	int        io_retry;      /* retry indicator */

	senseid_t *psid     = sid;/* start with the external buffer */	
	int        sbuffer  = 0;  /* switch SID data buffer */

	char dbf_txt[15];

	int failure = 0;          /* nothing went wrong yet */

	SANITY_CHECK(irq);

	if ( ioinfo[irq]->ui.flags.oper == 0 )
	{
		return( -ENODEV );

	} /* endif */

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "snsID");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	inlreq = 0; /* to make the compiler quiet... */

	if ( !ioinfo[irq]->ui.flags.ready )
	{

		pdevstat = &devstat;

		/*
		 * Perform SENSE ID command processing. We have to request device
		 *  ownership and provide a dummy I/O handler. We issue sync. I/O
		 *  requests and evaluate the devstat area on return therefore
		 *  we don't need a real I/O handler in place.
		 */
		irq_ret = request_irq( irq, init_IRQ_handler, SA_PROBE, "SID", &devstat);

		if ( irq_ret == 0 )
			inlreq = 1;
	}
	else
	{
		inlreq   = 0;
		irq_ret  = 0;
		pdevstat = ioinfo[irq]->irq_desc.dev_id;

  	} /* endif */

	if ( irq_ret == 0 )
	{
		int i;

		s390irq_spin_lock( irq);

		if ( init_IRQ_complete )
		{
			sense_ccw = kmalloc( 2*sizeof( ccw1_t), GFP_DMA);
		}
		else	
		{
			sense_ccw = alloc_bootmem_low( 2*sizeof( ccw1_t));

		} /* endif */

		// more than one path installed ?
		if ( ioinfo[irq]->schib.pmcw.pim != 0x80 )
		{
			sense_ccw[0].cmd_code = CCW_CMD_SUSPEND_RECONN;
			sense_ccw[0].cda      = 0;
			sense_ccw[0].count    = 0;
			sense_ccw[0].flags    = CCW_FLAG_SLI | CCW_FLAG_CC;

			sense_ccw[1].cmd_code = CCW_CMD_SENSE_ID;
			sense_ccw[1].cda      = (__u32)virt_to_phys( sid );
			sense_ccw[1].count    = sizeof( senseid_t);
			sense_ccw[1].flags    = CCW_FLAG_SLI;
		}
		else
		{
			sense_ccw[0].cmd_code = CCW_CMD_SENSE_ID;
			sense_ccw[0].cda      = (__u32)virt_to_phys( sid );
			sense_ccw[0].count    = sizeof( senseid_t);
			sense_ccw[0].flags    = CCW_FLAG_SLI;

		} /* endif */

		for ( i = 0 ; (i < 8) ; i++ )
		{
			pathmask = 0x80 >> i;						

			domask = ioinfo[irq]->opm & pathmask;

			if ( lpm )
				domask &= lpm;

			if ( domask )
			{
				failure = 0;
				
				psid->reserved   = 0;
				psid->cu_type    = 0xFFFF;  /* initialize fields ... */
				psid->cu_model   = 0;
				psid->dev_type   = 0;
				psid->dev_model  = 0;

				retry            = 5;  /* retry count    */
				io_retry         = 1;  /* enable retries */

				/*
				 * We now issue a SenseID request. In case of BUSY,
				 *  STATUS PENDING or non-CMD_REJECT error conditions
				 *  we run simple retries.
				 */
				do
				{
					memset( pdevstat, '\0', sizeof( devstat_t) );

					irq_ret = s390_start_IO( irq,
					                         sense_ccw,
					                         0x00E2C9C4,  // == SID
								 domask,
					                         DOIO_WAIT_FOR_INTERRUPT
					                          | DOIO_TIMEOUT
					                          | DOIO_VALID_LPM
					                          | DOIO_DONT_CALL_INTHDLR );


					if ( psid->cu_type  == 0xFFFF )
					{

						failure = 1;

						if ( pdevstat->flag & DEVSTAT_STATUS_PENDING )
						{
#ifdef CONFIG_DEBUG_IO
							printk( "SenseID : device %04X on "
							        "Subchannel %04X "
							        "reports pending status, "
							        "retry : %d\n",
							        ioinfo[irq]->schib.pmcw.dev,
								irq,
							        retry);
#endif
							if (cio_debug_initialized)
								debug_sprintf_event(cio_debug_msg_id, 2,
										    "SenseID : device %04X on "
										    "Subchannel %04X "
										    "reports pending status, "
										    "retry : %d\n",
										    ioinfo[irq]->schib.pmcw.dev,
										    irq,
										    retry);		    
						} /* endif */

						if ( pdevstat->flag & DEVSTAT_FLAG_SENSE_AVAIL )
						{
							/*
							 * if the device doesn't support the SenseID
							 *  command further retries wouldn't help ...
							 */
							if (  pdevstat->ii.sense.data[0]
							    & (SNS0_CMD_REJECT | SNS0_INTERVENTION_REQ) )
							{
#ifdef CONFIG_DEBUG_IO
								printk( "SenseID : device %04X on "
								        "Subchannel %04X "
								        "reports cmd reject or "
								        "intervention required\n",
								        ioinfo[irq]->schib.pmcw.dev,
								        irq);
#endif
								if (cio_debug_initialized)
									debug_sprintf_event(cio_debug_msg_id, 2,
											    "SenseID : device %04X on "
											    "Subchannel %04X "
											    "reports cmd reject or "
											    "intervention required\n",
											    ioinfo[irq]->schib.pmcw.dev,
											    irq);		    
								io_retry = 0;
							}

							else
							{
#ifdef CONFIG_DEBUG_IO							
								printk( "SenseID : UC on "
								        "dev %04X, "
								        "retry %d, "
								        "lpum %02X, "
								        "cnt %02d, "
								        "sns :"
								        " %02X%02X%02X%02X "
								        "%02X%02X%02X%02X ...\n",
								        ioinfo[irq]->schib.pmcw.dev,
								        retry,
								        pdevstat->lpum,
								        pdevstat->scnt,
								        pdevstat->ii.sense.data[0],
								        pdevstat->ii.sense.data[1],
								        pdevstat->ii.sense.data[2],
								        pdevstat->ii.sense.data[3],
								        pdevstat->ii.sense.data[4],
								        pdevstat->ii.sense.data[5],
								        pdevstat->ii.sense.data[6],
								        pdevstat->ii.sense.data[7]);
#endif
								if (cio_debug_initialized) {
									debug_sprintf_event(cio_debug_msg_id, 2,
											    "SenseID : UC on "
											    "dev %04X, "
											    "retry %d, "
											    "lpum %02X, "
											    "cnt %02d, "
											    "sns :"
											    " %02X%02X%02X%02X "
											    "%02X%02X%02X%02X ...\n",
											    ioinfo[irq]->schib.pmcw.dev,
											    retry,
											    pdevstat->lpum,
											    pdevstat->scnt,
											    pdevstat->ii.sense.data[0],
											    pdevstat->ii.sense.data[1],
											    pdevstat->ii.sense.data[2],
											    pdevstat->ii.sense.data[3],
											    pdevstat->ii.sense.data[4],
											    pdevstat->ii.sense.data[5],
											    pdevstat->ii.sense.data[6],
											    pdevstat->ii.sense.data[7]);	    
									if (psid->reserved != 0xFF) 
										debug_sprintf_event(cio_debug_msg_id, 2,
												    "SenseID was not properly "
												    "executed!\n");
								}
							} /* endif */

						}
						else if (    ( pdevstat->flag & DEVSTAT_NOT_OPER )
							  || ( irq_ret        == -ENODEV         ) )
						{
#ifdef CONFIG_DEBUG_IO
							printk( "SenseID : path %02X for "
							        "device %04X on "
							        "subchannel %04X "
							        "is 'not operational'\n",
							        domask,
							        ioinfo[irq]->schib.pmcw.dev,
							        irq);
#endif
							if (cio_debug_initialized)
								debug_sprintf_event(cio_debug_msg_id, 2, 
										    "SenseID : path %02X for "
										    "device %04X on "
										    "subchannel %04X "
										    "is 'not operational'\n",
										    domask,
										    ioinfo[irq]->schib.pmcw.dev,
										    irq);		    

							io_retry          = 0;
							ioinfo[irq]->opm &= ~domask;
      	
						}

						else if (     ( pdevstat->flag !=
							       (   DEVSTAT_START_FUNCTION
								 | DEVSTAT_FINAL_STATUS    ) )
							   && !( pdevstat->flag &
								DEVSTAT_STATUS_PENDING       ) )
						{
#ifdef CONFIG_DEBUG_IO
							printk( "SenseID : start_IO() for "
							        "device %04X on "
							        "subchannel %04X "
							        "returns %d, retry %d, "
							        "status %04X\n",
							        ioinfo[irq]->schib.pmcw.dev,
							        irq,
							        irq_ret,
							        retry,
							        pdevstat->flag);
#endif
							if (cio_debug_initialized)
								debug_sprintf_event(cio_debug_msg_id, 2,
										    "SenseID : start_IO() for "
										    "device %04X on "
										    "subchannel %04X "
										    "returns %d, retry %d, "
										    "status %04X\n",
										    ioinfo[irq]->schib.pmcw.dev,
										    irq,
										    irq_ret,
										    retry,
										    pdevstat->flag);		    

						} /* endif */

					}
					else   // we got it ...
					{
						if (psid->reserved != 0xFF) {
							/* No, we failed after all... */
							failure = 1;
							retry--;

						} else {
					       
							if ( !sbuffer )	// switch buffers
							{
								/*
								 * we report back the
								 *  first hit only
								 */
								psid = &isid;
								
								if ( ioinfo[irq]->schib.pmcw.pim != 0x80 )
								{
									sense_ccw[1].cda = (__u32)virt_to_phys( psid );
								}
								else
								{
									sense_ccw[0].cda = (__u32)virt_to_phys( psid );

								} /* endif */

								/*
								 * if just the very first
								 *  was requested to be
								 *  sensed disable further
								 *  scans.
								 */	
								if ( !lpm )
									lpm = domask;
								
								sbuffer = 1;
								
							} /* endif */

							if ( pdevstat->rescnt < (sizeof( senseid_t) - 8) )
							{
								ioinfo[irq]->ui.flags.esid = 1;
       							
							} /* endif */

							io_retry = 0;
						
						}

					} /* endif */

					if ( io_retry )
					{
						retry--;

						if ( retry == 0 )
						{
							io_retry = 0;

						} /* endif */
      	
					} /* endif */

					if ((failure) && (io_retry)) {
						/* reset fields... */

						failure = 0;
						
						psid->reserved   = 0;
						psid->cu_type    = 0xFFFF;  
						psid->cu_model   = 0;
						psid->dev_type   = 0;
						psid->dev_model  = 0;
					}
						
				} while ( (io_retry) );

 			} /* endif - domask */

		} /* endfor */

		if ( init_IRQ_complete )
		{
			kfree( sense_ccw );
		}
		else	
		{
			free_bootmem( (unsigned long)sense_ccw, 2*sizeof(ccw1_t) );

		} /* endif */

		s390irq_spin_unlock( irq);

		/*
		 * If we installed the irq action handler we have to
		 *  release it too.
		 */
		if ( inlreq )
			free_irq( irq, pdevstat);

		/*
		 * if running under VM check there ... perhaps we should do
		 *  only if we suffered a command reject, but it doesn't harm
		 */
		if (    ( sid->cu_type == 0xFFFF    )
		     && ( MACHINE_IS_VM             ) )
		{
			VM_virtual_device_info( ioinfo[irq]->schib.pmcw.dev,
			                        sid );
		} /* endif */

		if ( sid->cu_type == 0xFFFF )
		{
			/*
			 * SenseID CU-type of 0xffff indicates that no device
			 *  information could be retrieved (pre-init value).
			 *
			 * If we can't couldn't identify the device type we
			 *  consider the device "not operational".
			 */
#ifdef CONFIG_DEBUG_IO
			printk( "SenseID : unknown device %04X on subchannel %04X\n",
			        ioinfo[irq]->schib.pmcw.dev,
			        irq);
#endif
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_msg_id, 2,
						    "SenseID : unknown device %04X on subchannel %04X\n",
						    ioinfo[irq]->schib.pmcw.dev,
						    irq);		    
			ioinfo[irq]->ui.flags.unknown = 1;

		} /* endif */

	
		/*
		 * Issue device info message if unit was operational .
		 */
		if ( !ioinfo[irq]->ui.flags.unknown ) {
			if ( sid->dev_type != 0 ) {
				if ( cio_show_msg ) 
					printk( KERN_INFO"SenseID : device %04X reports: "
						"CU  Type/Mod = %04X/%02X,"
						" Dev Type/Mod = %04X/%02X\n",
						ioinfo[irq]->schib.pmcw.dev,
						sid->cu_type,
						sid->cu_model,
						sid->dev_type,
						sid->dev_model);
				if (cio_debug_initialized)
					debug_sprintf_event(cio_debug_msg_id, 2,
							    "SenseID : device %04X reports: "
							    "CU  Type/Mod = %04X/%02X,"
							    " Dev Type/Mod = %04X/%02X\n",
							    ioinfo[irq]->schib.pmcw.dev,
							    sid->cu_type,
							    sid->cu_model,
							    sid->dev_type,
							    sid->dev_model);
			} else {
				if ( cio_show_msg ) 
					printk( KERN_INFO"SenseID : device %04X reports:"
						" Dev Type/Mod = %04X/%02X\n",
						ioinfo[irq]->schib.pmcw.dev,
						sid->cu_type,
						sid->cu_model);
				if (cio_debug_initialized)
					debug_sprintf_event(cio_debug_msg_id, 2,
							    "SenseID : device %04X reports:"
							    " Dev Type/Mod = %04X/%02X\n",
							    ioinfo[irq]->schib.pmcw.dev,
							    sid->cu_type,
							    sid->cu_model);		    
			} /* endif */

		} /* endif */

		if ( !ioinfo[irq]->ui.flags.unknown )
			irq_ret = 0;
		else
			irq_ret = -ENODEV;

	} /* endif */

   return( irq_ret );
}

static int __inline__ s390_SetMultiPath( int irq )
{
	int cc;

	cc = stsch( irq, &ioinfo[irq]->schib );

	if ( !cc )
	{
		ioinfo[irq]->schib.pmcw.mp = 1;     /* multipath mode */

		cc = msch( irq, &ioinfo[irq]->schib );

	} /* endif */

	return( cc);
}

/*
 * Device Path Verification
 *
 * Path verification is accomplished by checking which paths (CHPIDs) are
 *  available. Further, a path group ID is set, if possible in multipath
 *  mode, otherwise in single path mode.
 *
 * Note : This function must not be called during normal device recognition,
 *         but during device driver initiated request_irq() processing only.
 */
int s390_DevicePathVerification( int irq, __u8 usermask )
{
	int  ccode;
	__u8 pathmask;
	__u8 domask;

	int ret = 0;

	char dbf_txt[15];

	if (cio_debug_initialized) {
		debug_text_event(cio_debug_trace_id, 4, "dpver");
		sprintf(dbf_txt, "%x", irq);
		debug_text_event(cio_debug_trace_id, 4, dbf_txt);
	}

	if ( ioinfo[irq]->ui.flags.pgid_supp == 0 )
	{
		return( 0);	// just exit ...

	} /* endif */

	ccode = stsch( irq, &(ioinfo[irq]->schib) );

	if ( ccode )
	{
		ret = -ENODEV;
	}
	else if ( ioinfo[irq]->schib.pmcw.pim == 0x80 )
	{
		/*
		 * no error, just not required for single path only devices
		 */	
		ioinfo[irq]->ui.flags.pgid_supp = 0;
		ret = 0;
	}
	else
	{
		int    i;
		pgid_t pgid;
		__u8   dev_path;
		int    first  = 1;

		ioinfo[irq]->opm =   ioinfo[irq]->schib.pmcw.pim
		                   & ioinfo[irq]->schib.pmcw.pam
		                   & ioinfo[irq]->schib.pmcw.pom;

		if ( usermask )
		{
			dev_path = usermask;
		}
		else
		{
			dev_path = ioinfo[irq]->opm;

		} /* endif */

		/*
		 * let's build a path group ID if we don't have one yet
		 */
		if ( ioinfo[irq]->ui.flags.pgid == 0)
		{
			ioinfo[irq]->pgid.cpu_addr  = *(__u16 *)__LC_CPUADDR;
			ioinfo[irq]->pgid.cpu_id    = ((cpuid_t *)__LC_CPUID)->ident;
			ioinfo[irq]->pgid.cpu_model = ((cpuid_t *)__LC_CPUID)->machine;
			ioinfo[irq]->pgid.tod_high  = *(__u32 *)&irq_IPL_TOD;

			ioinfo[irq]->ui.flags.pgid  = 1;

		} /* endif */     		

		memcpy( &pgid, &ioinfo[irq]->pgid, sizeof(pgid_t));

		for ( i = 0; i < 8 && !ret ; i++)
		{
			pathmask = 0x80 >> i;						

			domask = dev_path & pathmask;

			if ( domask )
			{
				ret = s390_SetPGID( irq, domask, &pgid );

				/*
				 * For the *first* path we are prepared
				 *  for recovery
				 *
				 *  - If we fail setting the PGID we assume its
				 *     using  a different PGID already (VM) we
				 *     try to sense.
				 */
				if ( ret == -EOPNOTSUPP && first )
				{
					*(int *)&pgid = 0;
					
					ret   = s390_SensePGID( irq, domask, &pgid);
					first = 0;

					if ( ret == 0 )
					{
						/*
						 * Check whether we retrieved
						 *  a reasonable PGID ...
						 */	
						if ( pgid.inf.ps.state1 == SNID_STATE1_GROUPED )
						{
							memcpy( &(ioinfo[irq]->pgid),
							        &pgid,
							        sizeof(pgid_t) );
						}
						else // ungrouped or garbage ...
						{
							ret = -EOPNOTSUPP;

						} /* endif */
					}
					else
					{
						ioinfo[irq]->ui.flags.pgid_supp = 0;

#ifdef CONFIG_DEBUG_IO
						printk( "PathVerification(%04X) "
						        "- Device %04X doesn't "
						        " support path grouping\n",
						        irq,
						        ioinfo[irq]->schib.pmcw.dev);
#endif
						if (cio_debug_initialized)
							debug_sprintf_event(cio_debug_msg_id, 2,
									    "PathVerification(%04X) "
									    "- Device %04X doesn't "
									    " support path grouping\n",
									    irq,
									    ioinfo[irq]->schib.pmcw.dev);		    

					} /* endif */
				}
				else if ( ret == -EIO ) 
				{
#ifdef CONFIG_DEBUG_IO
					printk("PathVerification(%04X) - I/O error "
					       "on device %04X\n", irq,
					       ioinfo[irq]->schib.pmcw.dev);
#endif

					if (cio_debug_initialized)
						debug_sprintf_event(cio_debug_msg_id, 2,
								    "PathVerification(%04X) - I/O error "
								    "on device %04X\n", irq,
								    ioinfo[irq]->schib.pmcw.dev);

					ioinfo[irq]->ui.flags.pgid_supp = 0;
		    
				} else {
#ifdef CONFIG_DEBUG_IO
					printk( "PathVerification(%04X) "
						"- Unexpected error on device %04X\n",
						irq,
						ioinfo[irq]->schib.pmcw.dev);
#endif
					if (cio_debug_initialized)
						debug_sprintf_event(cio_debug_msg_id, 2,
								    "PathVerification(%04X) - "
								    "Unexpected error on device %04X\n",
								    irq,
								    ioinfo[irq]->schib.pmcw.dev);		    
					
					ioinfo[irq]->ui.flags.pgid_supp = 0;
					
				} /* endif */

			} /* endif */
			
		} /* endfor */

	} /* endif */

	return ret;

}

/*
 * s390_SetPGID
 *
 * Set Path Group ID
 *
 */
int s390_SetPGID( int irq, __u8 lpm, pgid_t *pgid )
{
	ccw1_t    *spid_ccw;    /* ccw area for SPID command */
	devstat_t  devstat;     /* required by request_irq() */
	devstat_t *pdevstat = &devstat;
        unsigned long flags;


	int        irq_ret = 0; /* return code */
	int        retry   = 5; /* retry count */
	int        inlreq  = 0; /* inline request_irq() */
	int        mpath   = 1; /* try multi-path first */

	SANITY_CHECK(irq);

	if ( ioinfo[irq]->ui.flags.oper == 0 )
	{
		return( -ENODEV );

	} /* endif */

	if ( !ioinfo[irq]->ui.flags.ready )
	{
		/*
		 * Perform SetPGID command processing. We have to request device
		 *  ownership and provide a dummy I/O handler. We issue sync. I/O
		 *  requests and evaluate the devstat area on return therefore
		 *  we don't need a real I/O handler in place.
		 */
		irq_ret = request_irq( irq,
		                       init_IRQ_handler,
		                       SA_PROBE,
		                       "SPID",
		                       pdevstat);

		if ( irq_ret == 0 )
			inlreq = 1;
	}
	else
	{
		pdevstat = ioinfo[irq]->irq_desc.dev_id;

	} /* endif */

	if ( irq_ret == 0 )
	{
		s390irq_spin_lock_irqsave( irq, flags);

		if ( init_IRQ_complete )
		{
			spid_ccw = kmalloc( 2*sizeof( ccw1_t), GFP_DMA);
		}
		else	
		{
			spid_ccw = alloc_bootmem_low( 2*sizeof( ccw1_t));

		} /* endif */

		spid_ccw[0].cmd_code = 0x5B;	/* suspend multipath reconnect */
		spid_ccw[0].cda      = 0;
		spid_ccw[0].count    = 0;
		spid_ccw[0].flags    = CCW_FLAG_SLI | CCW_FLAG_CC;

		spid_ccw[1].cmd_code = CCW_CMD_SET_PGID;
		spid_ccw[1].cda      = (__u32)virt_to_phys( pgid );
		spid_ccw[1].count    = sizeof( pgid_t);
		spid_ccw[1].flags    = CCW_FLAG_SLI;

		pgid->inf.fc = SPID_FUNC_MULTI_PATH | SPID_FUNC_ESTABLISH;

		/*
		 * We now issue a SetPGID request. In case of BUSY
		 *  or STATUS PENDING conditions we retry 5 times.
		 */
		do
		{
			memset( pdevstat, '\0', sizeof( devstat_t) );

			irq_ret = s390_start_IO( irq,
			                         spid_ccw,
			                         0xE2D7C9C4,  // == SPID
			                         lpm,         // n/a
			                         DOIO_WAIT_FOR_INTERRUPT
			                          | DOIO_VALID_LPM
			                          | DOIO_DONT_CALL_INTHDLR );

			if ( !irq_ret )
			{
				if ( pdevstat->flag & DEVSTAT_STATUS_PENDING )
				{
#ifdef CONFIG_DEBUG_IO
					printk( "SPID - Device %04X "
					        "on Subchannel %04X "
					        "reports pending status, "
					        "retry : %d\n",
					        ioinfo[irq]->schib.pmcw.dev,
					        irq,
					        retry);
#endif
					if (cio_debug_initialized)
						debug_sprintf_event(cio_debug_msg_id, 2,
								    "SPID - Device %04X "
								    "on Subchannel %04X "
								    "reports pending status, "
								    "retry : %d\n",
								    ioinfo[irq]->schib.pmcw.dev,
								    irq,
								    retry);	    
					retry--;
					irq_ret = -EIO;
				} /* endif */

				if ( pdevstat->flag == (   DEVSTAT_START_FUNCTION
				                         | DEVSTAT_FINAL_STATUS   ) )
				{
					retry = 0;	// successfully set ...
					irq_ret = 0;
				}
				else if ( pdevstat->flag & DEVSTAT_FLAG_SENSE_AVAIL )
				{
					/*
					 * If the device doesn't support the
					 *  Sense Path Group ID command
					 *  further retries wouldn't help ...
					 */
					if ( pdevstat->ii.sense.data[0] & SNS0_CMD_REJECT )
					{
						if ( mpath )
						{
							pgid->inf.fc =   SPID_FUNC_SINGLE_PATH
							               | SPID_FUNC_ESTABLISH;
							mpath        = 0;
							retry--;
							irq_ret = -EIO;
						}
						else
						{
							irq_ret = -EOPNOTSUPP;
							retry   = 0;			

						} /* endif */
					}
					else
					{
#ifdef CONFIG_DEBUG_IO
						printk( "SPID - device %04X,"
						        " unit check,"
						        " retry %d, cnt %02d,"
						        " sns :"
						        " %02X%02X%02X%02X %02X%02X%02X%02X ...\n",
						        ioinfo[irq]->schib.pmcw.dev,
						        retry,
						        pdevstat->scnt,
						        pdevstat->ii.sense.data[0],
						        pdevstat->ii.sense.data[1],
						        pdevstat->ii.sense.data[2],
						        pdevstat->ii.sense.data[3],
						        pdevstat->ii.sense.data[4],
						        pdevstat->ii.sense.data[5],
						        pdevstat->ii.sense.data[6],
						        pdevstat->ii.sense.data[7]);
#endif

						if (cio_debug_initialized)
							debug_sprintf_event(cio_debug_msg_id, 2,
									    "SPID - device %04X,"
									    " unit check,"
									    " retry %d, cnt %02d,"
									    " sns :"
									    " %02X%02X%02X%02X %02X%02X%02X%02X ...\n",
									    ioinfo[irq]->schib.pmcw.dev,
									    retry,
									    pdevstat->scnt,
									    pdevstat->ii.sense.data[0],
									    pdevstat->ii.sense.data[1],
									    pdevstat->ii.sense.data[2],
									    pdevstat->ii.sense.data[3],
									    pdevstat->ii.sense.data[4],
									    pdevstat->ii.sense.data[5],
									    pdevstat->ii.sense.data[6],
									    pdevstat->ii.sense.data[7]);

						retry--;
						irq_ret = -EIO;
		    
					} /* endif */

				}
				else if ( pdevstat->flag & DEVSTAT_NOT_OPER )
				{
					/* don't issue warnings during startup unless requested*/
					if (init_IRQ_complete || cio_notoper_msg) {   
						
						printk( "SPID - Device %04X "
							"on Subchannel %04X "
							"became 'not operational'\n",
							ioinfo[irq]->schib.pmcw.dev,
							irq);
						if (cio_debug_initialized)
							debug_sprintf_event(cio_debug_msg_id, 2,
									    "SPID - Device %04X "
									    "on Subchannel %04X "
									    "became 'not operational'\n",
									    ioinfo[irq]->schib.pmcw.dev,
									    irq);		    
					}

					retry = 0;
					irq_ret = -EIO;

				} /* endif */
			}
			else if ( irq_ret != -ENODEV )
			{
				retry--;
				irq_ret = -EIO;
			}
			else
			{
				retry = 0;
				irq_ret = -ENODEV;

			} /* endif */

		} while ( retry > 0 );


		if ( init_IRQ_complete )
		{
			kfree( spid_ccw );
		}
		else	
		{
			free_bootmem( (unsigned long)spid_ccw, 2*sizeof(ccw1_t) );

		} /* endif */

		s390irq_spin_unlock_irqrestore( irq, flags);

		/*
		 * If we installed the irq action handler we have to
		 *  release it too.
		 */
		if ( inlreq )
			free_irq( irq, pdevstat);

	} /* endif */

   return( irq_ret );
}


/*
 * s390_SensePGID
 *
 * Sense Path Group ID
 *
 */
int s390_SensePGID( int irq, __u8 lpm, pgid_t *pgid )
{
	ccw1_t    *snid_ccw;    /* ccw area for SNID command */
	devstat_t  devstat;     /* required by request_irq() */
	devstat_t *pdevstat = &devstat;

	int        irq_ret = 0; /* return code */
	int        retry   = 5; /* retry count */
	int        inlreq  = 0; /* inline request_irq() */
	unsigned long flags;

	SANITY_CHECK(irq);

	if ( ioinfo[irq]->ui.flags.oper == 0 )
	{
		return( -ENODEV );

	} /* endif */

	if ( !ioinfo[irq]->ui.flags.ready )
	{
		/*
		 * Perform SENSE PGID command processing. We have to request device
		 *  ownership and provide a dummy I/O handler. We issue sync. I/O
		 *  requests and evaluate the devstat area on return therefore
		 *  we don't need a real I/O handler in place.
		 */
		irq_ret = request_irq( irq,
		                       init_IRQ_handler,
		                       SA_PROBE,
		                       "SNID",
		                       pdevstat);

		if ( irq_ret == 0 )
			inlreq = 1;

   }
	else
	{
		pdevstat = ioinfo[irq]->irq_desc.dev_id;

	} /* endif */

	if ( irq_ret == 0 )
	{
		s390irq_spin_lock_irqsave( irq, flags);

		if ( init_IRQ_complete )
		{
			snid_ccw = kmalloc( sizeof( ccw1_t), GFP_DMA);
		}
		else	
		{
			snid_ccw = alloc_bootmem_low( sizeof( ccw1_t));

		} /* endif */

		snid_ccw->cmd_code = CCW_CMD_SENSE_PGID;
		snid_ccw->cda      = (__u32)virt_to_phys( pgid );
		snid_ccw->count    = sizeof( pgid_t);
		snid_ccw->flags    = CCW_FLAG_SLI;

		/*
		 * We now issue a SensePGID request. In case of BUSY
		 *  or STATUS PENDING conditions we retry 5 times.
		 */
		do
		{
			memset( pdevstat, '\0', sizeof( devstat_t) );

			irq_ret = s390_start_IO( irq,
			                         snid_ccw,
			                         0xE2D5C9C4,  // == SNID
			                         lpm,         // n/a
			                         DOIO_WAIT_FOR_INTERRUPT
			                          | DOIO_VALID_LPM
			                          | DOIO_DONT_CALL_INTHDLR );

			if ( irq_ret == 0 )
			{
				if ( pdevstat->flag & DEVSTAT_FLAG_SENSE_AVAIL )
				{
					/*
					 * If the device doesn't support the
					 *  Sense Path Group ID command
					 *  further retries wouldn't help ...
					 */
					if ( pdevstat->ii.sense.data[0] & SNS0_CMD_REJECT )
					{
						retry   = 0;
						irq_ret = -EOPNOTSUPP;
					}
					else
					{
#ifdef CONFIG_DEBUG_IO
						printk( "SNID - device %04X,"
						        " unit check,"
						        " flag %04X, "
						        " retry %d, cnt %02d,"
						        " sns :"
						        " %02X%02X%02X%02X %02X%02X%02X%02X ...\n",
						        ioinfo[irq]->schib.pmcw.dev,
						        pdevstat->flag,
						        retry,
						        pdevstat->scnt,
						        pdevstat->ii.sense.data[0],
						        pdevstat->ii.sense.data[1],
						        pdevstat->ii.sense.data[2],
						        pdevstat->ii.sense.data[3],
						        pdevstat->ii.sense.data[4],
						        pdevstat->ii.sense.data[5],
						        pdevstat->ii.sense.data[6],
						        pdevstat->ii.sense.data[7]);

#endif
						if (cio_debug_initialized)
							debug_sprintf_event(cio_debug_msg_id, 2,
									    "SNID - device %04X,"
									    " unit check,"
									    " flag %04X, "
									    " retry %d, cnt %02d,"
									    " sns :"
									    " %02X%02X%02X%02X %02X%02X%02X%02X ...\n",
									    ioinfo[irq]->schib.pmcw.dev,
									    pdevstat->flag,
									    retry,
									    pdevstat->scnt,
									    pdevstat->ii.sense.data[0],
									    pdevstat->ii.sense.data[1],
									    pdevstat->ii.sense.data[2],
									    pdevstat->ii.sense.data[3],
									    pdevstat->ii.sense.data[4],
									    pdevstat->ii.sense.data[5],
									    pdevstat->ii.sense.data[6],
									    pdevstat->ii.sense.data[7]);		    
						retry--;
						irq_ret = -EIO;

					} /* endif */
				}
				else if ( pdevstat->flag & DEVSTAT_NOT_OPER )
				{
					/* don't issue warnings during startup unless requested*/
					if (init_IRQ_complete || cio_notoper_msg) {  
						printk( "SNID - Device %04X "
							"on Subchannel %04X "
							"became 'not operational'\n",
							ioinfo[irq]->schib.pmcw.dev,
							irq);
						if (cio_debug_initialized)
							debug_sprintf_event(cio_debug_msg_id, 2,
									    "SNID - Device %04X "
									    "on Subchannel %04X "
									    "became 'not operational'\n",
									    ioinfo[irq]->schib.pmcw.dev,
									    irq);		    
					}

					retry = 0;
					irq_ret = -EIO;

				}
				else
				{
					retry = 0; // success ...
					irq_ret = 0;

				} /* endif */
			}
			else if ( irq_ret != -ENODEV ) // -EIO, or -EBUSY
			{

				if ( pdevstat->flag & DEVSTAT_STATUS_PENDING )
				{
#ifdef CONFIG_DEBUG_IO
					printk( "SNID - Device %04X "
					        "on Subchannel %04X "
					        "reports pending status, "
					        "retry : %d\n",
					        ioinfo[irq]->schib.pmcw.dev,
					        irq,
					        retry);
#endif
					if (cio_debug_initialized)
						debug_sprintf_event(cio_debug_msg_id, 2,
								    "SNID - Device %04X "
								    "on Subchannel %04X "
								    "reports pending status, "
								    "retry : %d\n",
								    ioinfo[irq]->schib.pmcw.dev,
								    irq,
								    retry);		    
				} /* endif */


				printk( "SNID - device %04X,"
				        " start_io() reports rc : %d, retrying ...\n",
				        ioinfo[irq]->schib.pmcw.dev,
				        irq_ret);
				if (cio_debug_initialized)
					debug_sprintf_event(cio_debug_msg_id, 2, 
							    "SNID - device %04X,"
							    " start_io() reports rc : %d, retrying ...\n",
							    ioinfo[irq]->schib.pmcw.dev,
							    irq_ret);
				retry--;
				irq_ret = -EIO;
			}
			else	// -ENODEV ...
			{
				retry = 0;
				irq_ret = -ENODEV;

			} /* endif */

		} while ( retry > 0 );


		if ( init_IRQ_complete )
		{
			kfree( snid_ccw );
		}
		else	
		{
			free_bootmem( (unsigned long)snid_ccw, sizeof(ccw1_t) );

		} /* endif */

		s390irq_spin_unlock_irqrestore( irq, flags);

		/*
		 * If we installed the irq action handler we have to
		 *  release it too.
		 */
		if ( inlreq )
			free_irq( irq, pdevstat);

	} /* endif */

   return( irq_ret );
}

/*
 * s390_do_crw_pending
 *
 * Called by the machine check handler to process CRW pending
 *  conditions. It may be a single CRW, or CRWs may be chained.
 *
 * Note : we currently process CRWs for subchannel source only
 */
void s390_do_crw_pending( crwe_t *pcrwe )
{
	int irq;
	int chpid;
	int dev_oper = 0;
	int dev_no   = -1;	
	int lock     = 0;

#ifdef CONFIG_DEBUG_CRW
	printk( "do_crw_pending : starting ...\n");
#endif
	if (cio_debug_initialized) 
		debug_sprintf_event(cio_debug_crw_id, 2, 
				    "do_crw_pending: starting\n");
	while ( pcrwe != NULL )
	{
		int is_owned = 0;

		switch ( pcrwe->crw.rsc ) {	
		case CRW_RSC_SCH :

			irq = pcrwe->crw.rsid;

#ifdef CONFIG_DEBUG_CRW
			printk( KERN_INFO"do_crw_pending : source is "
			        "subchannel %04X\n", irq);
#endif
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_crw_id, 2,
						    "source is subchannel %04X\n", irq);
			/*
			 * If the device isn't known yet
			 *   we can't lock it ...
			 */
			if ( ioinfo[irq] != INVALID_STORAGE_AREA )
			{
				s390irq_spin_lock( irq );
				lock = 1;

				dev_oper = ioinfo[irq]->ui.flags.oper;

				if ( ioinfo[irq]->ui.flags.dval )
					dev_no = ioinfo[irq]->devno;

				is_owned = ioinfo[irq]->ui.flags.ready;

			} /* endif */

#ifdef CONFIG_DEBUG_CRW
			printk( "do_crw_pending : subchannel validation - start ...\n");
#endif
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_crw_id, 4,
						    "subchannel validation - start\n");
			s390_validate_subchannel( irq, is_owned );

			if ( irq > highest_subchannel )
				highest_subchannel = irq;

#ifdef CONFIG_DEBUG_CRW
			printk( "do_crw_pending : subchannel validation - done\n");
#endif
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_crw_id, 4,
						    "subchannel validation - done\n");
			/*
			 * After the validate processing
			 *   the ioinfo control block
			 *   should be allocated ...
			 */
			if ( lock )
			{
				s390irq_spin_unlock( irq );

			} /* endif */


			if ( ioinfo[irq] != INVALID_STORAGE_AREA )
			{
#ifdef CONFIG_DEBUG_CRW
				printk( "do_crw_pending : ioinfo at "
#ifdef CONFIG_ARCH_S390X
					"%08lX\n",
					(unsigned long)ioinfo[irq]);
#else /* CONFIG_ARCH_S390X */
					"%08X\n",
				        (unsigned)ioinfo[irq]);
#endif /* CONFIG_ARCH_S390X */
#endif
				if (cio_debug_initialized)
					debug_sprintf_event(cio_debug_crw_id, 4,
							    "ioinfo at "
#ifdef CONFIG_ARCH_S390X
							    "%08lX\n",
							    (unsigned long)ioinfo[irq]);
#else /* CONFIG_ARCH_S390X */
				                            "%08X\n",
							    (unsigned)ioinfo[irq]);
#endif /* CONFIG_ARCH_S390X */
			} /* endif */


			if ( ioinfo[irq] != INVALID_STORAGE_AREA )
			{
				if ( ioinfo[irq]->ui.flags.oper == 0 )
				{
					 not_oper_handler_func_t nopfunc=ioinfo[irq]->nopfunc;
					 
					 /* remove procfs entry */
					 if (cio_proc_devinfo)
						 cio_procfs_device_remove(dev_no);
					/*
					 * If the device has gone
					 *  call not oper handler        	
					 */       	
					 if (( dev_oper == 1 )
					     && ( nopfunc != NULL))
					{
						
						free_irq( irq,ioinfo[irq]->irq_desc.dev_id );
						nopfunc( irq,DEVSTAT_DEVICE_GONE );			

					} /* endif */
				}
				else
				{
#ifdef CONFIG_DEBUG_CRW
					printk( "do_crw_pending : device "
					        "recognition - start ...\n");
#endif
					if (cio_debug_initialized)
						debug_sprintf_event(cio_debug_crw_id, 4,
								    "device recognition - start\n");
					s390_device_recognition_irq( irq );

#ifdef CONFIG_DEBUG_CRW
					printk( "do_crw_pending : device "
					        "recognition - done\n");
#endif
					if (cio_debug_initialized)
						debug_sprintf_event(cio_debug_crw_id, 4,
								    "device recognition - done\n");
					/*
					 * the device became operational
					 */
					if ( dev_oper == 0 )
					{
						devreg_t *pdevreg;

						pdevreg = s390_search_devreg( ioinfo[irq] );

						if ( pdevreg != NULL )
						{
							if ( pdevreg->oper_func != NULL )
								pdevreg->oper_func( irq, pdevreg );

						} /* endif */

						/* add new procfs entry */
						if (cio_proc_devinfo) 
							if (highest_subchannel < MAX_CIO_PROCFS_ENTRIES) {
								cio_procfs_device_create(ioinfo[irq]->devno);
							}
					}
					/*
					 * ... it is and was operational, but
					 *      the devno may have changed
					 */
					else if ((ioinfo[irq]->devno != dev_no) && ( ioinfo[irq]->nopfunc != NULL ))   					
					{
						int devno_old = ioinfo[irq]->devno;
						ioinfo[irq]->nopfunc( irq,
						                      DEVSTAT_REVALIDATE );				

						/* remove old entry, add new */
						if (cio_proc_devinfo) {
							cio_procfs_device_remove(devno_old);
							cio_procfs_device_create(ioinfo[irq]->devno);
						}
					} /* endif */

				} /* endif */

				/* get rid of dead procfs entries */
				if (cio_proc_devinfo) 
					cio_procfs_device_purge();

			} /* endif */

			break;

		case CRW_RSC_MONITOR :

#ifdef CONFIG_DEBUG_CRW
			printk( "do_crw_pending : source is "
			        "monitoring facility\n");
#endif
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_crw_id, 2,
						    "source is monitoring facility\n");
			break;

		case CRW_RSC_CPATH :   	

			chpid = pcrwe->crw.rsid;

#ifdef CONFIG_DEBUG_CRW
			printk( "do_crw_pending : source is "
			        "channel path %02X\n", chpid);
#endif
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_crw_id, 2,
						    "source is channel path %02X\n");
			break;

		case CRW_RSC_CONFIG : 	

#ifdef CONFIG_DEBUG_CRW
			printk( "do_crw_pending : source is "
			        "configuration-alert facility\n");
#endif
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_crw_id, 2,
						    "source is configuration-alert facility\n");
			break;

		case CRW_RSC_CSS :

#ifdef CONFIG_DEBUG_CRW
			printk( "do_crw_pending : source is "
			        "channel subsystem\n");
#endif
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_crw_id, 2,
						    "source is channel subsystem\n");
			break;

		default :

#ifdef CONFIG_DEBUG_CRW
			printk( "do_crw_pending : unknown source\n");
#endif
			if (cio_debug_initialized)
				debug_sprintf_event(cio_debug_crw_id, 2,
						    "unknown source\n");
			break;		

		} /* endswitch */

		pcrwe = pcrwe->crwe_next;

	} /* endwhile */

#ifdef CONFIG_DEBUG_CRW
	printk( "do_crw_pending : done\n");
#endif
	if (cio_debug_initialized)
		debug_sprintf_event(cio_debug_crw_id, 2,
				    "do_crw_pending: done\n");
   return;
}


/* added by Holger Smolinski for reipl support in reipl.S */
extern void do_reipl (int);
void
reipl ( int sch )
{
	int i;
	s390_dev_info_t dev_info;

	for ( i = 0; i <= highest_subchannel; i ++ ) 
	{
		if (    get_dev_info_by_irq( i, &dev_info ) == 0
                    && (dev_info.status & DEVSTAT_DEVICE_OWNED) )
		{
			free_irq ( i, ioinfo[i]->irq_desc.dev_id );
		}
	}

	if (MACHINE_IS_VM)
		cpcmd("IPL", NULL, 0);
	else
		do_reipl( 0x10000 | sch );
}


/*
 * Function: cio_debug_init
 * Initializes three debug logs (under /proc/s390dbf) for common I/O:
 * - cio_msg logs the messages which are printk'ed when CONFIG_DEBUG_IO is on
 * - cio_trace logs the calling of different functions
 * - cio_crw logs the messages which are printk'ed when CONFIG_DEBUG_CRW is on
 * debug levels depend on CONFIG_DEBUG_IO resp. CONFIG_DEBUG_CRW
 */
int cio_debug_init( void )
{
	int ret = 0;

	cio_debug_msg_id = debug_register("cio_msg",2,4,16*sizeof(long));
	if (cio_debug_msg_id != NULL) {
		debug_register_view(cio_debug_msg_id, &debug_sprintf_view);
#ifdef CONFIG_DEBUG_IO
		debug_set_level(cio_debug_msg_id, 6);
#else /* CONFIG_DEBUG_IO */
		debug_set_level(cio_debug_msg_id, 2);
#endif /* CONFIG_DEBUG_IO */
	} else {
		ret = -1;
	}
	cio_debug_trace_id = debug_register("cio_trace",4,4,8);
	if (cio_debug_trace_id != NULL) {
		debug_register_view(cio_debug_trace_id, &debug_hex_ascii_view);
#ifdef CONFIG_DEBUG_IO
		debug_set_level(cio_debug_trace_id, 6);
#else /* CONFIG_DEBUG_IO */
		debug_set_level(cio_debug_trace_id, 2);
#endif /* CONFIG_DEBUG_IO */
	} else {
		ret = -1;
	}
	cio_debug_crw_id = debug_register("cio_crw",2,4,16*sizeof(long));
	if (cio_debug_crw_id != NULL) {
		debug_register_view(cio_debug_crw_id, &debug_sprintf_view);
#ifdef CONFIG_DEBUG_CRW
		debug_set_level(cio_debug_crw_id, 6);
#else /* CONFIG_DEBUG_CRW */
		debug_set_level(cio_debug_crw_id, 2);
#endif /* CONFIG_DEBUG_CRW */
	} else {
		ret = -1;
	}
	if (ret)
		return ret;
	cio_debug_initialized = 1;
	return 0;
}

__initcall(cio_debug_init);

/* 
 * Display info on subchannels in /proc/subchannels. 
 * Adapted from procfs stuff in dasd.c by Cornelia Huck, 02/28/01.      
 */

typedef struct {
     char *data;
     int len;
} tempinfo_t;

#define MIN(a,b) ((a)<(b)?(a):(b))

static struct proc_dir_entry *chan_subch_entry;

static int chan_subch_open( struct inode *inode, struct file *file)
{
     int rc = 0;
     int size = 1;
     int len = 0;
     int i = 0;
     int j = 0;
     tempinfo_t *info;
     
     info = (tempinfo_t *) vmalloc( sizeof (tempinfo_t));
     if (info == NULL) {
	  printk( KERN_WARNING "No memory available for data\n");
	  return -ENOMEM;
     } else {
	  file->private_data = ( void * ) info;
     }

     size += (highest_subchannel+1) * 128;
     info->data = (char *) vmalloc( size );
     
     if (size && info->data == NULL) {
		printk (KERN_WARNING "No memory available for data\n");
		vfree (info);
		return -ENOMEM;
	}
     
     len += sprintf( info->data+len, 
		     "Device sch.  Dev Type/Model CU  in use  PIM PAM POM LPUM CHPIDs\n");
     len += sprintf( info->data+len, 
		     "--------------------------------------------------------------------------\n");

     for ( i=0; i <= highest_subchannel; i++) {
	  if ( !((ioinfo[i] == NULL) || (ioinfo[i] == INVALID_STORAGE_AREA) || !(ioinfo[i]->ui.flags.oper)) ) {
	       len += sprintf( info->data+len, 
			       "%04X   %04X  ", 
			       ioinfo[i]->schib.pmcw.dev, 
			       i );
	       if ( ioinfo[i]->senseid.dev_type != 0 ) {
		    len += sprintf( info->data+len, 
				    "%04X/%02X   %04X/%02X", 
				    ioinfo[i]->senseid.dev_type,
				    ioinfo[i]->senseid.dev_model, 
				    ioinfo[i]->senseid.cu_type,
				    ioinfo[i]->senseid.cu_model );
	       } else {
		    len += sprintf( info->data+len, 
				    "          %04X/%02X", 
				    ioinfo[i]->senseid.cu_type, 
				    ioinfo[i]->senseid.cu_model );
	       }
	       if (ioinfo[i]->ui.flags.ready) {
		    len += sprintf( info->data+len, "  yes " );
	       } else {
		    len += sprintf( info->data+len, "      " );
	       }
	       len += sprintf( info->data+len,
			       "    %02X  %02X  %02X  %02X   ",
			       ioinfo[i]->schib.pmcw.pim,
			       ioinfo[i]->schib.pmcw.pam,
			       ioinfo[i]->schib.pmcw.pom,
			       ioinfo[i]->schib.pmcw.lpum );
	       for ( j=0; j < 8; j++ ) {
		    len += sprintf( info->data+len,
				    "%02X",
				    ioinfo[i]->schib.pmcw.chpid[j] );
		    if (j==3) {
			 len += sprintf( info->data+len, " " );
		    }
	       }
	       len += sprintf( info->data+len, "\n" );
	  }
     }
     info->len = len;

     return rc;
}

static int chan_subch_close( struct inode *inode, struct file *file)
{
     int rc = 0;
     tempinfo_t *p_info = (tempinfo_t *) file->private_data;

     if (p_info) {
	  if (p_info->data)
	       vfree( p_info->data );
	  vfree( p_info );
     }
     
     return rc;
}

static ssize_t chan_subch_read( struct file *file, char *user_buf, size_t user_len, loff_t * offset)
{
     loff_t len;
     tempinfo_t *p_info = (tempinfo_t *) file->private_data;
     
     if ( *offset>=p_info->len) {
	  return 0;
     } else {
	  len = MIN(user_len, (p_info->len - *offset));
	  if (copy_to_user( user_buf, &(p_info->data[*offset]), len))
	       return -EFAULT; 
	  (* offset) += len;
	  return len;
     }
}

static struct file_operations chan_subch_file_ops =
{
     read:chan_subch_read,
     open:chan_subch_open,
     release:chan_subch_close,
};

static int chan_proc_init( void )
{
     chan_subch_entry = create_proc_entry( "subchannels", S_IFREG|S_IRUGO, &proc_root);
     chan_subch_entry->proc_fops = &chan_subch_file_ops;

     return 1;
}

__initcall(chan_proc_init);

void chan_proc_cleanup( void )
{
     remove_proc_entry( "subchannels", &proc_root);
}

/* 
 * Display device specific information under /proc/deviceinfo/<devno>
 */

static struct proc_dir_entry *cio_procfs_deviceinfo_root = NULL;

/* 
 * cio_procfs_device_list holds all devno-specific procfs directories
 */

typedef struct {
	int devno;
	struct proc_dir_entry *cio_device_entry;
	struct proc_dir_entry *cio_sensedata_entry;
	struct proc_dir_entry *cio_in_use_entry;
	struct proc_dir_entry *cio_chpid_entry;
} cio_procfs_entry_t;

typedef struct _cio_procfs_device{
	struct _cio_procfs_device *next;
	cio_procfs_entry_t *entry;
} cio_procfs_device_t;

cio_procfs_device_t *cio_procfs_device_list = NULL;

/*
 * File operations
 */

static int cio_device_entry_close( struct inode *inode, struct file *file)
{
     int rc = 0;
     tempinfo_t *p_info = (tempinfo_t *) file->private_data;

     if (p_info) {
	  if (p_info->data)
	       vfree( p_info->data );
	  vfree( p_info );
     }
     
     return rc;
}

static ssize_t cio_device_entry_read( struct file *file, char *user_buf, size_t user_len, loff_t * offset)
{
     loff_t len;
     tempinfo_t *p_info = (tempinfo_t *) file->private_data;
     
     if ( *offset>=p_info->len) {
	  return 0;
     } else {
	  len = MIN(user_len, (p_info->len - *offset));
	  if (copy_to_user( user_buf, &(p_info->data[*offset]), len))
	       return -EFAULT; 
	  (* offset) += len;
	  return len;
     }
}


static int cio_sensedata_entry_open( struct inode *inode, struct file *file)
{
	int rc = 0;
	int size = 1;
	int len = 0;
	tempinfo_t *info;
	int irq;
	int devno;
	char * devno_str;

	info = (tempinfo_t *) vmalloc(sizeof(tempinfo_t));
	if (info == NULL) {
		printk( KERN_WARNING "No memory available for data\n");
		rc = -ENOMEM;
	} else {
		file->private_data = (void *) info;
		size += 2 * 32;
		info->data = (char *) vmalloc(size);
		if (size && info->data == NULL) {
			printk(KERN_WARNING "No memory available for data\n");
			vfree(info);
			rc = -ENOMEM;
		} else {
			devno_str = kmalloc(6*sizeof(char), GFP_KERNEL);
			memset(devno_str, 0, 6*sizeof(char));
			memcpy(devno_str,file->f_dentry->d_parent->d_name.name, strlen(file->f_dentry->d_parent->d_name.name)+1);
			devno = simple_strtoul(devno_str, &devno_str, 16);
			irq = get_irq_by_devno(devno);
			if (irq != -1) {
				len += sprintf(info->data+len, "Dev Type/Mod: ");
				if (ioinfo[irq]->senseid.dev_type == 0) {
					len += sprintf(info->data+len, "%04X/%02X\n",
						       ioinfo[irq]->senseid.cu_type,
						       ioinfo[irq]->senseid.cu_model);
				} else {
					len += sprintf(info->data+len, "%04X/%02X\n",
						       ioinfo[irq]->senseid.dev_type,
						       ioinfo[irq]->senseid.dev_model);
					len+= sprintf(info->data+len, "CU Type/Mod:  %04X/%02X\n",
						      ioinfo[irq]->senseid.cu_type,
						      ioinfo[irq]->senseid.cu_model);
				}
			}
			info->len = len;
		}
	}
	
	return rc;
}

static int cio_in_use_entry_open( struct inode *inode, struct file *file)
{
	int rc = 0;
	int size = 1;
	int len = 0;
	tempinfo_t *info;
	int irq;
	int devno;
	char * devno_str;

	info = (tempinfo_t *) vmalloc(sizeof(tempinfo_t));
	if (info == NULL) {
		printk( KERN_WARNING "No memory available for data\n");
		rc = -ENOMEM;
	} else {
		file->private_data = (void *) info;
		size += 8;
		info->data = (char *) vmalloc(size);
		if (size && info->data == NULL) {
			printk(KERN_WARNING "No memory available for data\n");
			vfree(info);
			rc = -ENOMEM;
		} else {
			devno_str = kmalloc(6*sizeof(char), GFP_KERNEL);
			memset(devno_str, 0, 6*sizeof(char));
			memcpy(devno_str,file->f_dentry->d_parent->d_name.name, strlen(file->f_dentry->d_parent->d_name.name)+1);
			devno = simple_strtoul(devno_str, &devno_str, 16);
			irq = get_irq_by_devno(devno);
			if (irq != -1) {
				len += sprintf(info->data+len, "%s\n", ioinfo[irq]->ui.flags.ready?"yes":"no");
			}
			info->len = len;
		}
	}
	
	return rc;
}

static int cio_chpid_entry_open( struct inode *inode, struct file *file)
{
	int rc = 0;
	int size = 1;
	int len = 0;
	tempinfo_t *info;
	int irq;
	int devno;
	int i;
	char * devno_str;

	info = (tempinfo_t *) vmalloc(sizeof(tempinfo_t));
	if (info == NULL) {
		printk( KERN_WARNING "No memory available for data\n");
		rc = -ENOMEM;
	} else {
		file->private_data = (void *) info;
		size += 8*16;
		info->data = (char *) vmalloc(size);
		if (size && info->data == NULL) {
			printk(KERN_WARNING "No memory available for data\n");
			vfree(info);
			rc = -ENOMEM;
		} else {
			devno_str = kmalloc(6*sizeof(char), GFP_KERNEL);
			memset(devno_str, 0, 6*sizeof(char));
			memcpy(devno_str,file->f_dentry->d_parent->d_name.name, strlen(file->f_dentry->d_parent->d_name.name)+1);
			devno = simple_strtoul(devno_str, &devno_str, 16);
			irq = get_irq_by_devno(devno);
			if (irq != -1) {
				for (i=0; i<8; i++) {
					len += sprintf(info->data+len, "CHPID[%d]: ", i);
					len += sprintf(info->data+len, "%02X\n", ioinfo[irq]->schib.pmcw.chpid[i]);
				}
			}
			info->len = len;
		}
	}

	return rc;
}

static struct file_operations cio_sensedata_entry_file_ops =
{
     read:cio_device_entry_read,
     open:cio_sensedata_entry_open,
     release:cio_device_entry_close,
};

static struct file_operations cio_in_use_entry_file_ops =
{
     read:cio_device_entry_read,
     open:cio_in_use_entry_open,
     release:cio_device_entry_close,
};

static struct file_operations cio_chpid_entry_file_ops =
{
     read:cio_device_entry_read,
     open:cio_chpid_entry_open,
     release:cio_device_entry_close,
};

/*
 * Function: cio_procfs_device_create
 * create procfs entry for given device number
 * and insert it into list
 */
int cio_procfs_device_create(int devno)
{
	cio_procfs_entry_t *entry;
	cio_procfs_device_t *tmp;
	cio_procfs_device_t *where;
	char buf[8];
	int i;
	int rc = 0;


	/* create the directory entry */
	entry = (cio_procfs_entry_t *)kmalloc(sizeof(cio_procfs_entry_t), GFP_KERNEL);
	if (entry) {
		entry->devno = devno;
		sprintf(buf, "%x", devno);
		entry->cio_device_entry = proc_mkdir(buf, cio_procfs_deviceinfo_root);
		
		if (entry->cio_device_entry) {
			tmp = (cio_procfs_device_t *)kmalloc(sizeof(cio_procfs_device_t), GFP_KERNEL);
			if (tmp) {
				tmp->entry = entry;
				
				if (cio_procfs_device_list == NULL) {
					cio_procfs_device_list = tmp;
					tmp->next = NULL;
				} else {
					where = cio_procfs_device_list;
					i = where->entry->devno;
					while ((devno>i) && (where->next != NULL)) {
						where = where->next;
						i = where->entry->devno;
					}
					if (where->next == NULL) {
						where->next = tmp;
						tmp->next = NULL;
					} else {
						tmp->next = where->next;
						where->next = tmp;
					}
				}
				/* create the different entries */
				entry->cio_sensedata_entry = create_proc_entry( "sensedata", S_IFREG|S_IRUGO, entry->cio_device_entry);
				entry->cio_sensedata_entry->proc_fops = &cio_sensedata_entry_file_ops;
				entry->cio_in_use_entry = create_proc_entry( "in_use", S_IFREG|S_IRUGO, entry->cio_device_entry);
				entry->cio_in_use_entry->proc_fops = &cio_in_use_entry_file_ops;
				entry->cio_chpid_entry = create_proc_entry( "chpids", S_IFREG|S_IRUGO, entry->cio_device_entry);
				entry->cio_chpid_entry->proc_fops = &cio_chpid_entry_file_ops;
			} else {
				printk("Error, could not allocate procfs structure!\n");
				remove_proc_entry(buf, cio_procfs_deviceinfo_root);
				kfree(entry);
				rc = -ENOMEM;
			}
		} else {
			printk("Error, could not allocate procfs structure!\n");
			kfree(entry);
			rc = -ENOMEM;
		}

	} else {
		printk("Error, could not allocate procfs structure!\n");
		rc = -ENOMEM;
	}
	return rc;
}

/*
 * Function: cio_procfs_device_remove
 * remove procfs entry for given device number
 */
int cio_procfs_device_remove(int devno)
{
	int rc = 0;
	cio_procfs_device_t *tmp;
	cio_procfs_device_t *prev = NULL;

	tmp=cio_procfs_device_list;
	while (tmp) {
		if (tmp->entry->devno == devno)
			break;
		prev = tmp;
		tmp = tmp->next;
	}

	if (tmp) {
		char buf[8];
		
		remove_proc_entry("sensedata", tmp->entry->cio_device_entry);
		remove_proc_entry("in_use", tmp->entry->cio_device_entry);
		remove_proc_entry("chpid", tmp->entry->cio_device_entry);
		sprintf(buf, "%x", devno);
		remove_proc_entry(buf, cio_procfs_deviceinfo_root);
		
		if (tmp == cio_procfs_device_list) {
			cio_procfs_device_list = tmp->next;
		} else {
			prev->next = tmp->next;
		}
		kfree(tmp->entry);
		kfree(tmp);
	} else {
		rc = -ENODEV;
	}

	return rc;
}

/*
 * Function: cio_procfs_purge
 * purge /proc/deviceinfo of entries for gone devices
 */

int cio_procfs_device_purge(void) 
{
	int i;

	for (i=0; i<=highest_subchannel; i++) {
		if (ioinfo[i] != INVALID_STORAGE_AREA) {
			if (!ioinfo[i]->ui.flags.oper) 
				cio_procfs_device_remove(ioinfo[i]->devno);
		}
	}

	return 0;
}

/*
 * Function: cio_procfs_create
 * create /proc/deviceinfo/ and subdirs for the devices
 */
static int cio_procfs_create( void )
{
	int irq;

	if (cio_proc_devinfo) {

		cio_procfs_deviceinfo_root = proc_mkdir( "deviceinfo", &proc_root);
		
		if (highest_subchannel >= MAX_CIO_PROCFS_ENTRIES) {
			printk(KERN_ALERT "Warning: Not enough inodes for creating all entries under /proc/deviceinfo/. "
			       "Not every device will get an entry.\n");
		}
		
		for (irq=0; irq<=highest_subchannel; irq++) {
			if (irq >= MAX_CIO_PROCFS_ENTRIES)
				break;
			if (ioinfo[irq] != INVALID_STORAGE_AREA) {
				if (ioinfo[irq]->ui.flags.oper) 
					if (cio_procfs_device_create(ioinfo[irq]->devno) == -ENOMEM) {
						printk(KERN_CRIT "Out of memory while creating entries in /proc/deviceinfo/, "
						       "not all devices might show up\n");
					break;
					}
			}
		}
	
	}
	
	return 1;
}

__initcall(cio_procfs_create);

/*
 * Entry /proc/cio_ignore to display blacklisted ranges of devices.
 * un-ignore devices by piping to /proc/cio_ignore:
 * free all frees all blacklisted devices, free <range>,<range>,...
 * frees specified ranges of devnos
 * add <range>,<range>,... will add a range of devices to blacklist -
 * but only for devices not already known
 */

static struct proc_dir_entry *cio_ignore_proc_entry;

static int cio_ignore_proc_open(struct inode *inode, struct file *file)
{
	int rc = 0;
	int size = 1;
	int len = 0;
	tempinfo_t *info;
	dev_blacklist_range_t *tmp;
	long flags;

	info = (tempinfo_t *) vmalloc(sizeof(tempinfo_t));
	if (info == NULL) {
		printk( KERN_WARNING "No memory available for data\n");
		rc = -ENOMEM;
	} else {
		file->private_data = (void *) info;
		size += nr_blacklisted_ranges * 32;
		info->data = (char *) vmalloc(size);
		if (size && info->data == NULL) {
			printk( KERN_WARNING "No memory available for data\n");
			vfree (info);
			rc = -ENOMEM;
		} else {
			spin_lock_irqsave( &blacklist_lock, flags ); 
			tmp = dev_blacklist_range_head;
			while (tmp) {
				len += sprintf(info->data+len, "%04x ", tmp->from);
				if (tmp->to != tmp->from) 
					len += sprintf(info->data+len, "- %04x", tmp->to);
				len += sprintf(info->data+len, "\n");
				tmp = tmp->next;
			}
			spin_unlock_irqrestore( &blacklist_lock, flags );
			info->len = len;
		}
	}
	return rc;
}

static int cio_ignore_proc_close(struct inode *inode, struct file *file)
{
	int rc = 0;
	tempinfo_t *p_info = (tempinfo_t *) file->private_data;

     if (p_info) {
	  if (p_info->data)
	       vfree( p_info->data );
	  vfree( p_info );
     }
     
     return rc;
}

static ssize_t cio_ignore_proc_read( struct file *file, char *user_buf, size_t user_len, loff_t * offset)
{
     loff_t len;
     tempinfo_t *p_info = (tempinfo_t *) file->private_data;
     
     if ( *offset>=p_info->len) {
	  return 0;
     } else {
	  len = MIN(user_len, (p_info->len - *offset));
	  if (copy_to_user( user_buf, &(p_info->data[*offset]), len))
	       return -EFAULT; 
	  (* offset) += len;
	  return len;
     }
}

static ssize_t cio_ignore_proc_write (struct file *file, const char *user_buf,
				      size_t user_len, loff_t * offset)
{
	char *buffer = vmalloc (user_len);

	if (buffer == NULL)
		return -ENOMEM;
	if (copy_from_user (buffer, user_buf, user_len)) {
		vfree (buffer);
		return -EFAULT;
	}
	buffer[user_len]='\0';
#ifdef CIO_DEBUG_IO
	printk ("/proc/cio_ignore: '%s'\n", buffer);
#endif /* CIO_DEBUG_IO */
	if (cio_debug_initialized)
		debug_sprintf_event(cio_debug_msg_id, 2, "/proc/cio_ignore: '%s'\n",buffer);

	blacklist_parse_proc_parameters(buffer);

	return user_len;
}

static struct file_operations cio_ignore_proc_file_ops =
{
	read:cio_ignore_proc_read,
	open:cio_ignore_proc_open,
	write:cio_ignore_proc_write,
	release:cio_ignore_proc_close,
};

static int cio_ignore_proc_init(void)
{
	cio_ignore_proc_entry = create_proc_entry("cio_ignore", S_IFREG|S_IRUGO|S_IWUSR, &proc_root);
	cio_ignore_proc_entry->proc_fops = &cio_ignore_proc_file_ops;

	return 1;
}

__initcall(cio_ignore_proc_init);

/*
 * Entry /proc/irq_count
 * display how many irqs have occured per cpu...
 */

static struct proc_dir_entry *cio_irq_proc_entry;

static int cio_irq_proc_open(struct inode *inode, struct file *file)
{
	int rc = 0;
	int size = 1;
	int len = 0;
	tempinfo_t *info;
	int i;

	info = (tempinfo_t *) vmalloc(sizeof(tempinfo_t));
	if (info == NULL) {
		printk( KERN_WARNING "No memory available for data\n");
		rc = -ENOMEM;
	} else {
		file->private_data = (void *) info;
		size += NR_CPUS * 16;
		info->data = (char *) vmalloc(size);
		if (size && info->data == NULL) {
			printk( KERN_WARNING "No memory available for data\n");
			vfree (info);
			rc = -ENOMEM;
		} else {
			for (i=0; i< NR_CPUS; i++) {
				if (s390_irq_count[i] != 0) 
					len += sprintf(info->data+len, "%lx\n", s390_irq_count[i]);
			}
			info->len = len;
		}
	}
	return rc;
}

static int cio_irq_proc_close(struct inode *inode, struct file *file)
{
	int rc = 0;
	tempinfo_t *p_info = (tempinfo_t *) file->private_data;

	if (p_info) {
		if (p_info->data) 
			vfree(p_info->data);
		vfree(p_info);
	}

	return rc;
}

static ssize_t cio_irq_proc_read( struct file *file, char *user_buf, size_t user_len, loff_t * offset)
{
	loff_t len;
	tempinfo_t *p_info = (tempinfo_t *) file->private_data;
	
	if ( *offset>=p_info->len) {
		return 0;
	} else {
		len = MIN(user_len, (p_info->len - *offset));
		if (copy_to_user( user_buf, &(p_info->data[*offset]), len))
			return -EFAULT; 
		(* offset) += len;
		return len;
	}
}

static struct file_operations cio_irq_proc_file_ops = 
	{
		read:    cio_irq_proc_read,
		open:    cio_irq_proc_open,
		release: cio_irq_proc_close,
	};

static int cio_irq_proc_init(void)
{

	int i;

	if (cio_count_irqs) {
		for (i=0; i<NR_CPUS; i++) 
			s390_irq_count[i]=0;
		cio_irq_proc_entry = create_proc_entry("irq_count", S_IFREG|S_IRUGO, &proc_root);
		cio_irq_proc_entry->proc_fops = &cio_irq_proc_file_ops;
	}

	return 1;
}

__initcall(cio_irq_proc_init);	

/* end of procfs stuff */

schib_t *s390_get_schib( int irq )
{
	if ( (irq > highest_subchannel) || (irq < 0) )
		return NULL;
	if ( ioinfo[irq] == INVALID_STORAGE_AREA )
		return NULL;
	return &ioinfo[irq]->schib;

}


EXPORT_SYMBOL(halt_IO);
EXPORT_SYMBOL(clear_IO);
EXPORT_SYMBOL(do_IO);
EXPORT_SYMBOL(resume_IO);
EXPORT_SYMBOL(ioinfo);
EXPORT_SYMBOL(get_dev_info_by_irq);
EXPORT_SYMBOL(get_dev_info_by_devno);
EXPORT_SYMBOL(get_irq_by_devno);
EXPORT_SYMBOL(get_devno_by_irq);
EXPORT_SYMBOL(get_irq_first);
EXPORT_SYMBOL(get_irq_next);
EXPORT_SYMBOL(read_conf_data);
EXPORT_SYMBOL(read_dev_chars);
EXPORT_SYMBOL(s390_request_irq_special);
EXPORT_SYMBOL(s390_get_schib);
EXPORT_SYMBOL(s390_register_adapter_interrupt);
EXPORT_SYMBOL(s390_unregister_adapter_interrupt);
