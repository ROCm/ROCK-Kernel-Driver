/***********************************************************************
 *  drivers/s390/char/tape.c
 *    tape device driver for S/390 and zSeries tapes.
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001 IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Michael Holzheu <holzheu@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *
 ***********************************************************************
 */

#include "tapedefs.h" // kernel 2.2 compatibility defines

#include <linux/stddef.h>    // defines NULL
#include <linux/proc_fs.h>   // for /proc/tapedevices
#include <linux/init.h>      // for kernel parameters
#include <linux/kmod.h>      // for requesting modules
#include <linux/spinlock.h>  // for locks
#include <asm/types.h>       // for variable types
#ifdef CONFIG_S390_TAPE_DYNAMIC
#include <asm/s390dyn.h>
#endif
#include "tape.h"
#ifdef CONFIG_S390_TAPE_3590
#include "tape3590.h"
#endif
#ifdef CONFIG_S390_TAPE_3490
#include "tape3490.h"
#endif
#ifdef CONFIG_S390_TAPE_3480
#include "tape3480.h"
#endif
#ifdef CONFIG_S390_TAPE_BLOCK
#include "tapeblock.h"
#endif
#ifdef CONFIG_S390_TAPE_CHAR
#include "tapechar.h"
#endif
#ifdef CONFIG_PROC_FS
#include <linux/vmalloc.h>
#endif
#define PRINTK_HEADER "T390:"
#define TAPE_MAX_DEVREGS        (256 / TAPE_MINORS_PER_DEV)

#define TAPE_NO_IO          0
#define TAPE_DO_IO          1

#define TAPE_CIO_PRIVATE_DATA

#ifdef CONFIG_KMOD
#define tape_request_module(a) request_module(a)
#else
#define tape_request_module(a)
#endif

/*******************************************************************
 * Internal Prototypes
 *******************************************************************/

static void tape_do_irq (int irq, void *int_parm, struct pt_regs *regs);
static inline int tape_halt_io(tape_dev_t* td);
static void tape_wait(tape_ccw_req_t* treq);
#ifdef CONFIG_S390_TAPE_DYNAMIC
/* functions for dyn. dev. attach/detach */
static int tape_oper_handler ( int irq, struct _devreg *dreg);
static void tape_noper_handler ( int irq, int status );
#endif
static inline void tape_disable_device(tape_dev_t* td);
static inline int tape_enable_device(tape_dev_t* td);


/*******************************************************************
 * GLOBALS
 *******************************************************************/

static devreg_t          *tape_devreg[TAPE_MAX_DEVREGS];
static int               tape_devregct=0;
static int               tape_autoprobe = 1;
static tape_discipline_t *tape_first_disc = NULL;
tape_dev_t               *tape_first_dev = NULL;
tape_frontend_t          *tape_first_front = NULL;
char                     *tape[256] = { NULL, }; 

/*
 * Lock hirarchy:
 * tape_discipline_lock > tape_dev_lock > td->lock
 */

rwlock_t   tape_dev_lock=RW_LOCK_UNLOCKED;
static rwlock_t   tape_discipline_lock=RW_LOCK_UNLOCKED;

#ifdef TAPE_DEBUG
debug_info_t *tape_dbf_area = NULL;
#endif

const char* tape_med_st_verbose[MS_SIZE]={
	"UNKNOWN ",
	"LOADED  ",
	"UNLOADED"
};

const char* tape_state_verbose[TS_SIZE]={
	"UNUSED",
	"IN_USE", 
	"INIT  ",
	"NOT_OP"
};

const char* tape_op_verbose[TO_SIZE] = {
	"BLK",
	"BSB",
	"BSF",
	"DSE",
	"EGA",
	"FSB",
	"FSF",
	"LDI",
	"LBL",
	"MSE",
	"NOP",
	"RBA",
	"RBI",
	"RBU",
	"RBL",
	"RDC",
	"RFO",
	"RSD",
	"REW",
	"RUN",
	"SEN",
	"SID",
	"SNP",
	"SPG",
	"SWI",
	"SMR",
	"SYN",
	"TIO",
	"UNA",
	"WRI",
	"WTM",
	"MSN",
	"LOA",
	"RCF", /* 3590 */
	"RAT", /* 3590 */
	"NOT"
};

/*******************************************************************
 * DEVFS Functions
 *******************************************************************/

#ifdef CONFIG_DEVFS_FS

/*
 * Create devfs root entry (devno in hex) for device td
 */

static inline devfs_handle_t
tape_mkdevfsroot (tape_dev_t* td) 
{
    char devno [10];
    sprintf (devno,"tape/%04x",td->devinfo.devno);
    return devfs_mk_dir(NULL, devno, NULL);
}

/*
 * Remove devfs root entry for device td
 */

static inline void
tape_rmdevfsroot (tape_dev_t* td)
{
    devfs_remove("tape/%04x", td->devinfo.devno);
}

#endif

/*******************************************************************
 * PROCFS Functions
 *******************************************************************/

#ifdef CONFIG_PROC_FS
/* functions used in tape_proc_file_ops */
static ssize_t tape_proc_devices_read (struct file *file, char *user_buf, size_t user_len, loff_t * offset);
static int tape_proc_devices_open (struct inode *inode, struct file *file);
static int tape_proc_devices_release (struct inode *inode, struct file *file);

/* our proc tapedevices entry */
static struct proc_dir_entry *tape_proc_devices;

typedef struct {
	char *data;
	int len;
} tape_procinfo_t;

static struct file_operations tape_proc_devices_file_ops =
{
	.owner = THIS_MODULE,
	.read = tape_proc_devices_read,	/* read */
	.open = tape_proc_devices_open,	/* open */
	.release = tape_proc_devices_release,	/* close */
};

/* 
 * Initialize procfs stuff on startup
 */

static inline void
tape_proc_init (void) {
	tape_proc_devices = create_proc_entry ("tapedevices",
						S_IFREG | S_IRUGO | S_IWUSR,
						&proc_root);
	if (tape_proc_devices == NULL) 
	        goto error;
	tape_proc_devices->proc_fops = &tape_proc_devices_file_ops;
	tape_proc_devices->proc_iops = &tape_proc_devices_inode_ops;
	return;
 error:
	PRINT_WARN ("tape: Cannot register procfs entry tapedevices\n");
	return;
}

/*
 * Open function for /proc/tapedevices
 */

static int
tape_proc_devices_open (struct inode *inode, struct file *file)
{
	tape_dev_t* td;
	tape_procinfo_t* procinfo;
	char* data = NULL;
	int size=0,check_size = -1;
	int pos=0;
	int rc=0;
	long lockflags,lockflags2;
	tape_ccw_req_t *treq;

	procinfo = kmalloc (sizeof(tape_procinfo_t),GFP_KERNEL);
	if (!procinfo){
		rc = -ENOMEM;
		goto out_no_lock;
	}

	/* Find out mem size for output, ensure that after releasing lock   */
	/* (vmalloc must not be called with interrupts disabled) no devices */
	/* have been added/removed                                          */

	do{
		size = 100; // Headline
		read_lock_irqsave(&tape_dev_lock,lockflags);

		for (td=tape_first_dev;td!=NULL;td=td->next)
			size+=100; // FIXME: Guess better!

		if(size == check_size)
                        break;

		read_unlock_irqrestore(&tape_dev_lock,lockflags);

		if(data)
			vfree(data);
		data=vmalloc(size);
		if (!data) {
                    kfree (procinfo);
                    rc = -ENOMEM;
                    goto out_no_lock;
		}
		check_size = size;
	}while (1);

	// We have the tape_dev lock now

#ifdef CONFIG_S390_TAPE_CHAR
	pos+=sprintf(data+pos,"TapeNo\tDevNo\tCuType\tCuModel\tDevType\tDevMod\tBlkSize\tState\tOp\tMedState\n");
#else
	pos+=sprintf(data+pos,"TapeNo\tDevNo\tCuType\tCuModel\tDevType\tDevMod\tState\tOp\tMedState\n");
#endif

	for (td=tape_first_dev;td!=NULL;td=td->next) {
		s390irq_spin_lock_irqsave (td->devinfo.irq,lockflags2);
		treq = tape_get_active_ccw_req(td);
		pos+=sprintf(data+pos,"%d\t",td->first_minor/TAPE_MINORS_PER_DEV);
		pos+=sprintf(data+pos,"%04X\t",td->devinfo.devno);
		pos+=sprintf(data+pos,"%04X\t",td->devinfo.sid_data.cu_type);
		pos+=sprintf(data+pos,"%02X\t",td->devinfo.sid_data.cu_model);
		pos+=sprintf(data+pos,"%04X\t",td->devinfo.sid_data.dev_type);
		pos+=sprintf(data+pos,"%02X\t",td->devinfo.sid_data.dev_model);
#ifdef CONFIG_S390_TAPE_CHAR
		if(td->char_data.block_size == 0)
			pos+=sprintf(data+pos,"auto\t");
		else
			pos+=sprintf(data+pos,"%i\t",td->char_data.block_size);
#endif
		pos+=sprintf(data+pos,"%s\t",((tape_state_get(td) >= 0) &&
			(tape_state_get(td) < TS_SIZE)) ?
			tape_state_verbose[tape_state_get (td)] : "UNKNOWN");
		pos+=sprintf(data+pos,"%s\t",(treq != NULL) ? 
			tape_op_verbose[treq->op] : "---");
		pos+=sprintf(data+pos,"%s\n",tape_med_st_verbose[td->medium_state]);
		s390irq_spin_unlock_irqrestore (td->devinfo.irq,lockflags2);
	}

	procinfo->data=data;
	procinfo->len=pos;
        if (pos>size) BUG(); // we've overwritten some memory
	file->private_data= (void*) procinfo;
	read_unlock_irqrestore(&tape_dev_lock,lockflags);
out_no_lock:
	return rc;
}

/*
 * Read function for /proc/tapedevices
 */

static ssize_t
tape_proc_devices_read (struct file *file, char *user_buf, size_t user_len, loff_t * offset)
{
	loff_t len = 0;
	tape_procinfo_t *p_info = (tape_procinfo_t *) file->private_data;

	if (*offset >= p_info->len) {
		goto out; /* EOF */
	} else {
		len =  user_len<(p_info->len - *offset)?user_len:(p_info->len - *offset);
		if (copy_to_user (user_buf, &(p_info->data[*offset]), len))
			return -EFAULT;
		(*offset) += len;
	}
out:
	return len;
}

/*
 * Close function for /proc/tapedevices
 */

static int
tape_proc_devices_release (struct inode *inode, struct file *file)
{
	int rc = 0;
	tape_procinfo_t *p_info = (tape_procinfo_t *) file->private_data;
        vfree(p_info->data);
        kfree (p_info);
	return rc;
}

/*
 * Cleanup all stuff registered to the procfs
 */
static inline void
tape_proc_cleanup (void)
{
        if (tape_proc_devices != NULL)
		remove_proc_entry ("tapedevices", &proc_root);
}

#endif /* CONFIG_PROC_FS */

/*******************************************************************
 * Wait/Wakeup Functions
 *******************************************************************/

static void tape_wake_up_remove(tape_ccw_req_t* treq){
	tape_remove_ccw_req(treq->tape_dev,treq);
	tape_free_ccw_req(treq);
}
 
static void tape_wake_up(tape_ccw_req_t* treq){
        treq->wakeup = NULL;
        wake_up(&treq->wq);
}
 
static void tape_wake_up_interruptible(tape_ccw_req_t* treq){
        treq->wakeup = NULL;
        wake_up_interruptible(&treq->wq);
}
 
#ifdef CONFIG_S390_TAPE_BLOCK
static void tape_schedule_tapeblock(tape_ccw_req_t* treq){
        treq->wakeup = NULL;
        tapeblock_schedule_exec_io((tape_dev_t*)(treq->tape_dev));;
}
#endif
 
static void tape_wait_event(tape_ccw_req_t* treq){
        wait_event (treq->wq,(treq->wakeup == NULL));
}
 
static void tape_wait_event_interruptible(tape_ccw_req_t* treq){
        wait_event_interruptible(treq->wq,(treq->wakeup == NULL));
	if (signal_pending (current)) {
		treq->rc = tape_halt_io(treq->tape_dev);
		if(treq->rc == -ERESTARTSYS)
		        PRINT_INFO("IO stopped on irq %d\n",treq->tape_dev->devinfo.irq); /* FIXME: only put into dbf */
                else if(treq->rc == 0)
		        PRINT_INFO("could not stop IO,irq was faster on irq %d\n",treq->tape_dev->devinfo.irq); /* FIXME: only put into dbf */
                else
			PRINT_WARN("IO error while stopping IO on irq %d\n",treq->tape_dev->devinfo.irq);
	}
}

static void tape_wait_event_interruptible_nohaltio(tape_ccw_req_t* treq){
	wait_event_interruptible(treq->wq,(treq->wakeup == NULL));
}

/*******************************************************************
 * DYNAMIC ATTACH/DETACH Functions
 *******************************************************************/

static inline void
tape_init_devregs(void)
{
        long lockflags;
	write_lock_irqsave(&tape_dev_lock,lockflags);
	memset(tape_devreg,0,sizeof(devreg_t*) * TAPE_MAX_DEVREGS);
	write_unlock_irqrestore(&tape_dev_lock,lockflags);
}

/*
 * Alloc a devreg for a devno
 */

static inline devreg_t *
tape_create_devno_devreg (int devno)
{
        devreg_t *devreg = kmalloc (sizeof (devreg_t), GFP_KERNEL);
        if (devreg != NULL) {
                memset (devreg, 0, sizeof (devreg_t));
                devreg->ci.devno = devno;
                devreg->flag = DEVREG_TYPE_DEVNO;
                devreg->oper_func = tape_oper_handler;
        }
        return devreg;
}

/*
 * Alloc a devreg for a cu-type 
 */
 
static inline devreg_t *
tape_create_cu_devreg (int cu_type)
{
        devreg_t *devreg = kmalloc (sizeof (devreg_t), GFP_KERNEL);
        if (devreg != NULL) {
                memset (devreg, 0, sizeof (devreg_t));
                devreg->ci.hc.ctype = cu_type;
                devreg->flag = DEVREG_MATCH_CU_TYPE | DEVREG_TYPE_DEVCHARS;
                devreg->oper_func = tape_oper_handler;
        }
        return devreg;
}

/*
 * Create devregs for device numbers from "from" to "to"
 */
 
static inline void
tape_create_devregs_range(int from, int to)
{
	int i;
	long lockflags;
 
	write_lock_irqsave(&tape_dev_lock,lockflags);
	for (i=from;i<=to;i++) {
		// register for attch/detach of a devno
		if(tape_devregct >= TAPE_MAX_DEVREGS){
			PRINT_WARN ("Could not create devregs for devno range %04x - %04x.\n",i,to);
			PRINT_WARN ("These devices cannot be used. Use autoprobe\n");
			PRINT_WARN ("or specify device ranges more precisely!\n");
			break;
		}
		tape_devreg[tape_devregct]=tape_create_devno_devreg(i);
		if (tape_devreg[tape_devregct]!=NULL) {
                        s390_device_register (tape_devreg[tape_devregct++]);
		} else {
                	PRINT_WARN ("Could not create devreg for devno %04x, dyn. attach for this devno deactivated.\n",i);
		}
	}
	write_unlock_irqrestore(&tape_dev_lock,lockflags);
        return;
}

/*
 * Create a devreg for a discipline
 */

static inline void
tape_create_devreg_for_disc(tape_discipline_t *disc)
{
	int devreg_nr;
	long lockflags;
 
	write_lock_irqsave(&tape_dev_lock,lockflags);
	for(devreg_nr = 0; devreg_nr < TAPE_MAX_DEVREGS; devreg_nr++){
		if(tape_devreg[devreg_nr] == NULL)
			break;
	}
	if(devreg_nr == TAPE_MAX_DEVREGS){
		PRINT_WARN ("Could not create devreg for discipline (%x), dyn. attach for this discipline deactivated.\n",disc->cu_type);
		goto out_unlock;
	}
	tape_devreg[devreg_nr] = tape_create_cu_devreg(disc->cu_type);
	if (tape_devreg[devreg_nr] != NULL) {
		s390_device_register(tape_devreg[devreg_nr]);
	} else {
		PRINT_WARN("Could not alloc devreg: Out of memory\n");
		PRINT_WARN("Dynamic attach/detach will not work!\n");
	}
out_unlock:
	write_unlock_irqrestore(&tape_dev_lock,lockflags);
	return;
}

/*
 * Free all devregs
 */ 

static inline void
tape_delete_all_devregs(void)
{
	int i;
	long lockflags;

	write_lock_irqsave(&tape_dev_lock,lockflags);

	for(i = 0; i < TAPE_MAX_DEVREGS; i++){
		if(tape_devreg[i]){
			s390_device_unregister(tape_devreg[i]);
			kfree(tape_devreg[i]);
		}
	}
	memset(tape_devreg,0,sizeof(devreg_t*) * TAPE_MAX_DEVREGS);
	write_unlock_irqrestore(&tape_dev_lock,lockflags);
}

/*
 * Free Devregs for a discipline
 */

static inline void
tape_delete_devreg_for_disc(tape_discipline_t* disc)
{
	int i;
	long lockflags;
 
	write_lock_irqsave(&tape_dev_lock,lockflags);
 
	for(i = 0; i < TAPE_MAX_DEVREGS; i++){
		if(tape_devreg[i]){
			if(tape_devreg[i]->ci.hc.ctype == disc->cu_type){
				s390_device_unregister(tape_devreg[i]);
				kfree(tape_devreg[i]);
				tape_devreg[i] = NULL;
			}
		}
	}
 
	write_unlock_irqrestore(&tape_dev_lock,lockflags);
}

/*******************************************************************
 * Module/Kernel Parameter Handling
 *******************************************************************/

#ifndef MODULE
static char tape_parm_string[1024] __initdata = { 0, };

/*
 * Get Kernel parameters (str) seperated by ","
 * and store them into the tape[] array
 */

static void
tape_split_parm_string (char *str)
{
	char *tmp = str;
	int count = 0;
	while (tmp != NULL && *tmp != '\0') {
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
		tape[count] = kmalloc (len * sizeof (char), GFP_ATOMIC);
		if (tape[count] == NULL) {
			printk (KERN_WARNING PRINTK_HEADER
				"can't store tape= parameter no %d\n",
				count + 1);
			break;
		}
		memcpy (tape[count], tmp, len * sizeof (char));
		count++;
		tmp = end;
	};
}

/*
 * This function is called for each "tape=" Kernel parameter
 * at Kernel initialization. We need tape_parm_setup because of
 * 2.2 compatibility (At least I assume this :-))
 */
 
void __init
tape_parm_setup (char *str, int *ints)
{
	int len = strlen (tape_parm_string);
	if (len != 0) {
		strcat (tape_parm_string, ",");
	}
	strcat (tape_parm_string, str);
}

int __init
tape_parm_call_setup (char *str)
{
	int dummy;
	tape_parm_setup (str, &dummy);
	return 1;
}

__setup("tape=", tape_parm_call_setup);
#endif   /* not defined MODULE */


/*
 * Convert string to int
 */

static inline int
tape_parm_strtoul (char *str, char **stra)
{
	char *temp = str;
	int val;
	if (*temp == '0') {
		temp++;         /* strip leading zero */
		if (*temp == 'x')
			temp++; /* strip leading x */
	}
	val = simple_strtoul (temp, &temp, 16); /* interpret anything as hex */
	*stra = temp;
	return val;
}

/*
 * Parse Kernel/Module Parameters and create devregs for dynamic attach/detach
 */ 

static inline void
tape_parm_parse (char **str)
{
	char *temp;
	int from, to;
 
	if (*str==NULL) {
		/* no params present -> leave */
		return;
	}
	while (*str) {
		temp = *str;
		from = 0;
		to = 0;
		from = tape_parm_strtoul (temp, &temp);
		to = from;
		if (*temp == '-') {
			temp++;
			to = tape_parm_strtoul (temp, &temp);
		}
		tape_create_devregs_range(from,to);
		str++;
	}
}

/*******************************************************************
 * Tape device (td) functions for create, free, enq, deq, enable,
 * disable, get and put
 *******************************************************************/

/*
 * Enable Device
 */

static inline int 
tape_enable_device(tape_dev_t* td)
{
#ifdef CONFIG_DEVFS_FS
	tape_frontend_t* frontend;
#endif
	int rc = 0;

	if(td->discipline->setup_device(td) != 0){
		rc = -ENOMEM;
		goto out;
	}

	/* Register IRQ */

#ifdef TAPE_CIO_PRIVATE_DATA
	#ifdef CONFIG_S390_TAPE_DYNAMIC
		rc = s390_request_irq_special (td->devinfo.irq, tape_do_irq,
			tape_noper_handler,0, TAPE_MAGIC, &(td->devstat));
	#else
		rc = s390_request_irq (td->devinfo.irq, tape_do_irq, 0, TAPE_MAGIC, &(td->devstat));
	#endif
#else
	#ifdef CONFIG_S390_TAPE_DYNAMIC
		rc = s390_request_irq_special (td->devinfo.irq, tape_do_irq,
			tape_noper_handler,0, (char*)td, &(td->devstat));
	#else
		rc = s390_request_irq (td->devinfo.irq, tape_do_irq, 0, (char*)td, &(td->devstat));
	#endif
#endif /* TAPE_CIO_PRIVATE_DATA */
	if (rc){
		PRINT_WARN ("Cannot register irq %d, rc=%d\n", td->devinfo.irq, rc);
		td->discipline->cleanup_device(td);
		goto out;
	}
	/* Create devfs entries */
#ifdef CONFIG_DEVFS_FS
	if (tape_mkdevfsroot(td)==NULL){
		PRINT_WARN ("Cannot create a devfs directory for device %04x\n",td->devinfo.devno);
		goto out_undo;
	}
		
	for (frontend=tape_first_front;frontend!=NULL;frontend=frontend->next){
		if(frontend->mkdevfstree(td) == NULL){
			goto out_undo;
		}
	}
#endif
#ifdef TAPE_CIO_PRIVATE_DATA
	s390_set_private_data(td->devinfo.irq,td);
#endif
out:
	return rc;
out_undo:
        tape_disable_device(td);
        return -ENOMEM;        
}

/*
 * Disable Device
 */

static inline void
tape_disable_device(tape_dev_t* td)
{
#ifdef CONFIG_DEVFS_FS
	tape_frontend_t* frontend;
#endif
	td->discipline->cleanup_device(td);
#ifdef TAPE_CIO_PRIVATE_DATA 
	s390_set_private_data(td->devinfo.irq,NULL);
#else
	ioinfo[td->devinfo.irq]->irq_desc.name = NULL;
#endif

	free_irq (td->devinfo.irq, &(td->devstat)); 
#ifdef CONFIG_DEVFS_FS
        for (frontend=tape_first_front;frontend!=NULL;frontend=frontend->next)
                frontend->rmdevfstree(td);
        tape_rmdevfsroot(td);
#endif
	tape_state_set(td,TS_NOT_OPER);
}

/*
 * Append Tape device (td) to our tape info list
 * Must be called with hold tape_dev_lock
 */
 
static inline void
tape_enq_device(tape_dev_t* td)
{
	tape_dev_t *temptd = NULL;
 
	if (tape_first_dev == NULL) {
		tape_first_dev = td;
	} else {
		temptd = tape_first_dev;
		while (temptd->next != NULL)
			temptd = temptd->next;
		temptd->next = td;
	}
}
 
/*
 * Remove Tape device (td) from our tape info list
 * Must be called with hold tape_dev_lock
 */
 
static inline void
tape_deq_device(tape_dev_t* td)
{
	tape_dev_t* lasttd;
 
	if (td==tape_first_dev) {
		tape_first_dev=td->next;
	} else {
		lasttd=tape_first_dev;
		while (lasttd->next!=td) lasttd=lasttd->next;
		lasttd->next=td->next;
	}
}


/*
 * Get Free minor number
 * Must be called with held tape_dev_lock
 */

static inline int
tape_get_new_minor(int devno)
{
	int i,tape_num = -1;
	tape_dev_t* newtape;

	if(!tape_autoprobe){
		/* we have static device ranges, so fingure out the */
		/* tape_num of the attached tape                    */
		for (i=0;i<tape_devregct;i++){
			if (tape_devreg[i]->ci.devno==devno) {
				tape_num=TAPE_MINORS_PER_DEV*i;
				goto out;
			}
		}
	} else {
		/* we are running in autoprobe mode, find a free */
		/* tape_num */
		i = 0;
		newtape=tape_first_dev;
		while (newtape!=NULL) {
			if (newtape->first_minor==i) {
				/* tape num in use. try next one */
				i+=TAPE_MINORS_PER_DEV;
				newtape=tape_first_dev;
			} else {
				/* tape num not used by newtape. look at next */
				/* tape info */
				newtape=newtape->next;
			}
		}
		if(i>255) /* No more minor available */
			tape_num = -1;
		else
			tape_num = i;
	}
out:
	return tape_num;
}


/*
 * Create device: Alloc, enable and enq device
 */

static inline tape_dev_t*
tape_create_device(int irq, int devno, tape_discipline_t* disc)
{
	int rc = 0;
	int tape_num;
	tape_dev_t *td;
	long lockflags;

	write_lock_irqsave(&tape_dev_lock,lockflags);

	td = kmalloc (sizeof (tape_dev_t), GFP_ATOMIC);
	if (td == NULL) {
		tape_sprintf_exception (tape_dbf_area,2,"ti:no mem \n");
		PRINT_INFO ("tape: can't allocate memory for "
			"tape info structure\n");
		goto error;
	}
	memset(td,0,sizeof(tape_dev_t));
        tape_num = tape_get_new_minor(devno);
        if(tape_num == -1){
                PRINT_WARN("tape: could not get minor for tape %x\n",devno);
                goto error;
        }
	rc = get_dev_info_by_irq (irq, &(td->devinfo));
	if (rc == -ENODEV) {    /* end of device list */
		goto error;
	}
	td->discipline = disc;
	atomic_set(&(td->use_count),1);
	td->first_minor = tape_num;
	td->medium_state = MS_UNKNOWN;
	td->next = NULL;
	td->discdata = td->treq = NULL;
	tape_state_set (td, TS_INIT);
	td->discdata=NULL;
	td->last_op = TO_NOTHING;

	if(td->discipline->setup_device(td) != 0)
		goto error;
	if(tape_enable_device(td) !=0)	
		goto error;
	tape_enq_device(td);
	PRINT_INFO ("using devno %04x with discipline %04x on irq %d as tape device %d\n",devno,disc->cu_type,irq,tape_num/2);
	write_unlock_irqrestore(&tape_dev_lock,lockflags);
	return td;
error:
	tape_sprintf_event (tape_dbf_area,3,"tsetup err: %x\n",rc);
	if(td)
		kfree (td);
	write_unlock_irqrestore(&tape_dev_lock,lockflags);
	return NULL;
}

/*
 * Free Device storage
 */

static void
tape_free_device(tape_dev_t* td)
{
        if (TAPE_BUSY(td)) 
	        BUG(); /* one should _not_ free the device when a request is pending */
	tape_sprintf_event (tape_dbf_area,6,"free irq: %x\n",td->devinfo.irq);
        kfree(td);
}

/*
 * Decrement use count of tape structure
 * if use count == 0 tape structure is freed
 */
 
inline void
tape_put_device(tape_dev_t* td)
{
	if (td==NULL)
		BUG();
	if(atomic_dec_and_test(&(td->use_count)))
		tape_free_device(td);
}

/*
 * Find the tape_dev_t structure associated with member
 * and increase use count:
 *
 * member:    TAPE_MEMB_IRQ, TAPE_MEMB_MINOR
 */

tape_dev_t *
__tape_get_device_by_member(unsigned long value, int member)
{
	tape_dev_t *td = NULL;
	long lockflags;
 
	read_lock_irqsave(&tape_dev_lock,lockflags);
	td = tape_first_dev;
	while(td != NULL) {
		switch(member){
			case TAPE_MEMB_IRQ:
				if(td->devinfo.irq == value)
					goto out;
				break;
			case TAPE_MEMB_MINOR:
				if((value >= td->first_minor)  && 
				    (value < (td->first_minor + TAPE_MINORS_PER_DEV)) )
					goto out;
				break;
			case TAPE_MEMB_QUEUE:
				if(((unsigned long)(&td->blk_data.request_queue)) == value)
					goto out;
				break;
			default:
				BUG();
		}
		td = td->next;
	}
out:
	if(td) // found!
		atomic_inc(&(td->use_count));
	read_unlock_irqrestore(&tape_dev_lock,lockflags);
	return td;
}

/*
 * Scan all irqs an create tape devices for all matching cu types
 */

static inline void
tape_create_devs_for_disc(tape_discipline_t* disc)
{
	int irq,i;
	tape_dev_t* td = NULL;
	s390_dev_info_t dinfo;

	for (irq = get_irq_first(); irq!=-ENODEV; irq=get_irq_next(irq)) {
		if(get_dev_info_by_irq (irq, &dinfo) == -ENODEV)
			continue;
		if(disc->cu_type != dinfo.sid_data.cu_type)
			/* Wrong type - try next one */
			continue;

		tape_sprintf_event (tape_dbf_area,3,"det irq:  %x\n",irq);
		tape_sprintf_event (tape_dbf_area,3,"cu     :  %x\n",disc->cu_type);


		if(!tape_autoprobe) {
		        for( i=0; i<tape_devregct; i++ ) {
		                if(tape_devreg[i]->ci.devno == dinfo.devno) {
			                td = tape_create_device(irq, dinfo.devno, disc);
					if(!td) {
					        PRINT_WARN( "Could not initialize tape 0x%x\n",dinfo.devno);
						continue;
					}
					if(disc->init_device)
					        disc->init_device(td); /* XXX */
					tape_state_set (td, TS_UNUSED);
					break;
				}
			}
		}
		else {
		        td = tape_create_device(irq,dinfo.devno,disc);
		  
			if(!td){
			        PRINT_WARN( "Could not initialize tape 0x%x\n",dinfo.devno);
			        continue;
			}
			if(disc->init_device)
			        disc->init_device(td); /* XXX */
			tape_state_set (td, TS_UNUSED); 
		}
	}
}

/*
 * Go through our tape info list and disable, deq and free all devices with 
 * matching cu type
 */

static inline void
tape_delete_devs_for_disc(tape_discipline_t* disc)
{
	tape_dev_t *td;
	long lockflags;

	write_lock_irqsave(&tape_dev_lock,lockflags);
	td = tape_first_dev;
	while (td !=NULL){
		if(td->discipline == disc){
			tape_deq_device(td);
			write_unlock_irqrestore(&tape_dev_lock,lockflags);
			tape_disable_device(td);
			tape_put_device(td);
			write_lock_irqsave(&tape_dev_lock,lockflags);
			td = tape_first_dev;
		} else {
			td=td->next;
		}
	}
	write_unlock_irqrestore(&tape_dev_lock,lockflags);
}
		
/*******************************************************************
 * TAPE Request and IO Functions
 *******************************************************************/

/*
 * Allocate a new tape ccw request
 */

inline tape_ccw_req_t *
tape_alloc_ccw_req (int cplength, int datasize,int idal_buf_size, tape_op_t operation)
{
	tape_ccw_req_t* treq;
	int kmalloc_flags;
	if(in_interrupt())
		kmalloc_flags = GFP_ATOMIC;
	else
		kmalloc_flags = GFP_KERNEL;

	treq = (tape_ccw_req_t*) kmalloc(sizeof(tape_ccw_req_t),kmalloc_flags);
	if(!treq)
		goto error;
	memset(treq,0,sizeof(tape_ccw_req_t));
	treq->kernbuf_size = datasize;
	treq->userbuf_size = datasize;
	treq->op           = operation;
	treq->cplength     = cplength;
	init_waitqueue_head (&treq->wq); 

	// alloc small kernel buffer

	if(datasize > PAGE_SIZE)
		BUG();
	if(datasize > 0){
		// the kernbuf must be below 2GB --> GFP_DMA
		treq->kernbuf = kmalloc(datasize,kmalloc_flags | GFP_DMA); 
		if(!treq->kernbuf)
			goto error;
		memset(treq->kernbuf, 0, datasize);
	}

	// alloc idal kernel buffer

	if(idal_buf_size > 0){
		treq->idal_buf = idalbuf_alloc(idal_buf_size);
		if(!treq->idal_buf)
			goto error;
	}
		
	if(cplength < 0)
		BUG();

	// the channel program must be below 2GB --> GFP_DMA
	treq->cpaddr = kmalloc(cplength * sizeof(ccw1_t), kmalloc_flags | GFP_DMA);
	if(!treq->cpaddr)
		goto error;
	memset(treq->cpaddr, 0, cplength*sizeof(ccw1_t));
	return treq;
error:
	tape_sprintf_exception (tape_dbf_area,1,"cqra nomem\n");
	if(treq)
		tape_free_ccw_req(treq);
	return NULL;
}

/*
 * Free tape ccw request
 */

void
tape_free_ccw_req (tape_ccw_req_t * treq)
{
	if(!treq)
                BUG();
	if(treq->cpaddr)
		kfree(treq->cpaddr);
	if(treq->kernbuf)
		kfree(treq->kernbuf);
	if(treq->idal_buf)
		idalbuf_free(treq->idal_buf);
	kfree(treq);
}

/*
 * Add request to request queue (At the moment queue length = 1)
 */

static inline int
tape_add_ccw_req(tape_dev_t* td,tape_ccw_req_t* treq)
{
	if(td->treq)
		return -1;
	td->treq = treq;
	treq->tape_dev = td;
	return 0;
}

/*
 * Remove request from request queue (At the moment queue length = 1)
 */

int
tape_remove_ccw_req(tape_dev_t* td,tape_ccw_req_t* treq)
{
	if(treq != td->treq)
		BUG();
	td->last_op = treq->op;
        td->treq=NULL;
	treq->tape_dev=NULL;
        return 0;
}

/*
 * Get the active ccw request
 */

tape_ccw_req_t*
tape_get_active_ccw_req(tape_dev_t* td)
{
	return td->treq;
}

/*
 * Stop the active ccw request
 */

static inline int
tape_halt_io(tape_dev_t* td)
{
	int retries = 0;
	int irq = td->devinfo.irq;
	int rc = 0;
	long lockflags;
	tape_ccw_req_t* treq;

	s390irq_spin_lock_irqsave (td->devinfo.irq, lockflags);
	treq = tape_get_active_ccw_req(td);
	/* check if interrupt has already been processed */
	if(!treq)
		goto out;
	if(treq->wakeup == NULL)
		goto out;
	while (retries < 5){
		if ( retries < 2 )
			rc = halt_IO(irq, (long)treq, treq->options);
		else
			rc = clear_IO(irq, (long)treq, treq->options);
		switch (rc) {
		case 0:         /* termination successful */
			rc = -ERESTARTSYS;
			goto out;
		case -ENODEV:
		        PRINT_INFO ("device gone, retry\n"); /* FIXME: s390dbf only */
			break;
		case -EIO:
			PRINT_INFO ("I/O error, retry\n"); /* FIXME: s390dbf only */
			break;
		case -EBUSY:
			PRINT_INFO ( "device busy, retry later\n"); /* FIXME: s390dbf only */
			break;
		default:
			PRINT_ERR ( "line %d unknown RC=%d, please report"
			   	    " to linux390@de.ibm.com\n", __LINE__, rc);
			BUG ();
		}
		retries ++;
	}
out:
	s390irq_spin_unlock_irqrestore(td->devinfo.irq, lockflags);
	return rc;
}


/*
 * The tape IO function:
 * tape_do_io MUST be called with locked td lock!
 */
 
static inline int
__tape_do_io(tape_dev_t * td,tape_ccw_req_t *treq,tape_wait_t type,int io)
{
	int rc = 0;
 
        if ((td==NULL) || (treq==NULL)) 
                BUG();

	if(tape_state_get(td) == TS_NOT_OPER){
		rc = -ENODEV;
		goto out;
	}
	if(TAPE_BUSY(td)){
		tape_sprintf_event (tape_dbf_area,1,"tape: IRQ - Tape busy\n");	
		rc = -EBUSY;
		goto out;
	}
	tape_add_ccw_req(td,treq);
	switch(type){
		case TAPE_REMOVE_REQ_ON_WAKEUP:
			treq->wakeup = tape_wake_up_remove;
			break;
		case TAPE_NO_WAIT: 
			// needed for retry
			break; 
		case TAPE_WAIT:
			treq->wakeup = tape_wake_up;
			treq->wait   = tape_wait_event;
			break;
		case TAPE_WAIT_INTERRUPTIBLE:
			treq->wakeup = tape_wake_up_interruptible;
			treq->wait   = tape_wait_event_interruptible;
			break;
		case TAPE_WAIT_INTERRUPTIBLE_NOHALTIO:
			// neded for dummy load 
			treq->wakeup = tape_wake_up_interruptible;
			treq->wait   = tape_wait_event_interruptible_nohaltio;
			break;
#ifdef CONFIG_S390_TAPE_BLOCK
		case TAPE_SCHED_BLOCK:
			treq->wakeup = tape_schedule_tapeblock;
			treq->wait   = NULL;
			break;
#endif
		default:
                        BUG();
	}
 
	if(io)
		rc = do_IO(td->devinfo.irq,treq->cpaddr,
		(unsigned long)treq, 0x00, treq->options);
 
	if(rc) {
		tape_sprintf_event (tape_dbf_area,1,"tape: DOIO failed with er = %i\n",rc);
		treq->wakeup = NULL;
		treq->wait   = NULL; /*XXX ??*/
		tape_remove_ccw_req(td,treq);
	}
out:
	return rc;
}

/*
 * Do the io request and wait for it
 */

int
tape_do_io_and_wait(tape_dev_t * td,tape_ccw_req_t *treq,tape_wait_t type)
{
	int rc = 0;
	long lockflags;
        if (td==NULL) 
                BUG();
	s390irq_spin_lock_irqsave (td->devinfo.irq, lockflags);
	rc = __tape_do_io(td,treq,type,TAPE_DO_IO);
	s390irq_spin_unlock_irqrestore (td->devinfo.irq, lockflags);
	if(rc == 0)
		tape_wait(treq);
	return rc;
}

/*
 * Needed for MTLOAD: Just create a wait request without doing io
 */

int
tape_do_wait_req(tape_dev_t * td,tape_ccw_req_t *treq,tape_wait_t type)
{
	int rc = 0;
	long lockflags;
        if (td==NULL)
                BUG();
	s390irq_spin_lock_irqsave (td->devinfo.irq, lockflags);
	rc = __tape_do_io(td,treq,type,TAPE_NO_IO);
	s390irq_spin_unlock_irqrestore (td->devinfo.irq, lockflags);
	tape_wait(treq);
	return rc;
}

/*
 * Just do the io
 */

int
tape_do_io(tape_dev_t * td,tape_ccw_req_t *treq,tape_wait_t type)
{
	int rc = 0;
	long lockflags;
	s390irq_spin_lock_irqsave (td->devinfo.irq, lockflags);
	rc = __tape_do_io(td,treq,type,TAPE_DO_IO);
	s390irq_spin_unlock_irqrestore (td->devinfo.irq, lockflags);
	return rc;
}

/*
 * Just do the io, but we are already locked here (in irq)
 */

int
tape_do_io_irq(tape_dev_t * td,tape_ccw_req_t *treq,tape_wait_t type)
{
        return __tape_do_io(td,treq,type,TAPE_DO_IO);
}

/*
 * Wait for an io request
 */

static void
tape_wait(tape_ccw_req_t* treq)
{
	tape_dev_t* td;
	if(treq==NULL)
                BUG();
	td = treq->tape_dev;
        if (td==NULL)
                BUG();       
	if(treq->wait==NULL)
                BUG();
        treq->wait(treq);
	tape_remove_ccw_req(td,treq);
}

/*
 * Tape interrupt routine, called from Ingo's I/O layer
 */

static void
tape_do_irq (int irq, void *int_parm, struct pt_regs *regs)
{
	tape_dev_t *td;
#ifdef TAPE_CIO_PRIVATE_DATA
	td = (tape_dev_t*)s390_get_private_data(irq);
#else
	td = (tape_dev_t*)ioinfo[irq]->irq_desc.name;
#endif
	if(!td) {
		PRINT_ERR ("tape: could not get device structure for irq %d in interrupt\n",irq);
		goto out;
	}
	if(td->devstat.dstat != 0x0c){
		tape_sprintf_event (tape_dbf_area,3,"-- Tape Interrupthandler --\n");
		tape_dump_sense_dbf(td);
	}
	if (tape_state_get(td) == TS_NOT_OPER) {
		tape_sprintf_event (tape_dbf_area,6,"tape:device is not operational\n");
		goto out;
	}
	td->discipline->irq(td);
out:
	return;
}

/*
 * Oper Handler is called from Ingo's I/O layer when a new tape device is 
 * attached. We create a new devinfo for the new device, enable and enq it.
 */

static int 
tape_oper_handler ( int irq, struct _devreg *dreg) {
	tape_dev_t* td=tape_first_dev;
	int rc=0;
	s390_dev_info_t dinfo;
	tape_discipline_t* disc;
	long lockflags;

	td = tape_get_device_by_irq(irq);
	if (td!=NULL) {
		// irq is (still) used by tape. tell ingo to try again later
		PRINT_ERR ("tape:Oper handler for irq %d called, but irq is still (internally) used.\n",irq);
		rc = -EAGAIN;
		goto out;
	}

	rc = get_dev_info_by_irq (irq, &dinfo);
	if (rc == -ENODEV) {
                PRINT_ERR ("tape: Cannot get dev info for irq %d in the oper handler.\n",irq);
                rc = -EAGAIN;
                goto out;
	}

	read_lock_irqsave(&tape_discipline_lock,lockflags);

	disc = tape_first_disc;
	while ((disc != NULL) && (disc->cu_type != dinfo.sid_data.cu_type))
		disc = (tape_discipline_t *) (disc->next);

	if (disc == NULL){
		PRINT_WARN ("tape: No matching discipline for cu_type %x found in the oper handler, ignoring device %04x.\n",dinfo.sid_data.cu_type,dinfo.devno);
		rc = -ENODEV;
		goto out_unlock;
	}

	/* Allocate tape structure  */
	td = tape_create_device(irq,dinfo.devno,disc);

	if (td == NULL) {
		PRINT_WARN( "Could not initialize tape 0x%x\n",dinfo.devno);
		rc = -ENOBUFS;
		goto out_unlock;
	} else {
		if(disc->init_device)
			disc->init_device(td);
	}
	tape_state_set (td, TS_UNUSED); 
out_unlock:
        read_unlock_irqrestore(&tape_discipline_lock,lockflags);
out:
	return rc;
}

/*
 * Not Oper Handler is called from Ingo's IO layer, when a tape device
 * is detached. We deq, disable and free the tape info for this device
 */

static void
tape_noper_handler ( int irq, int status ) {
	tape_dev_t *td;
	long lockflags;
	tape_ccw_req_t *treq;

	td = tape_get_device_by_irq(irq);
	if (td==NULL) {
                PRINT_ERR("tape: not operational handler called for irq %x, but tape does not hold this irq.\n",irq);
                goto error;
        }

	write_lock_irqsave(&tape_dev_lock,lockflags);
	tape_deq_device(td);
	write_unlock_irqrestore(&tape_dev_lock,lockflags);

	tape_disable_device(td);

	s390irq_spin_lock_irqsave (td->devinfo.irq, lockflags);
	treq = tape_get_active_ccw_req(td);
	if (treq) {
		// device is in use!
                treq->rc= -ENODEV;
		if(treq->wakeup)
			treq->wakeup (treq);
		PRINT_WARN ("Tape #%d is detached while it was busy.\n",td->first_minor/TAPE_MINORS_PER_DEV);
	} else {
		// device is unused!
		PRINT_WARN ("Tape #%d is detached now.\n",td->first_minor/TAPE_MINORS_PER_DEV);
	}

	s390irq_spin_unlock_irqrestore (td->devinfo.irq, lockflags);	

	/* decrement use count twice ! */	
	tape_put_device(td);
	tape_put_device(td);
error:
	return;
}

/*
 * Write sense data to dbf
 */

void tape_dump_sense_dbf(tape_dev_t* td)
{
	devstat_t *stat = &td->devstat;
	const char* op;
	if(TAPE_BUSY(td))
		op = tape_op_verbose[tape_get_active_ccw_req(td)->op];
	else
		op = "---";
	tape_sprintf_event (tape_dbf_area,3,"DSTAT : %02x   CSTAT: %02x\n",stat->dstat,stat->cstat);
	tape_sprintf_event (tape_dbf_area,3,"DEVICE: %04x OP   : %s\n",td->devinfo.devno,op);
	tape_sprintf_event (tape_dbf_area,3,"%08x %08x\n",*((unsigned int*)&stat->ii.sense.data[0]),*((unsigned int*)&stat->ii.sense.data[4]));
	tape_sprintf_event (tape_dbf_area,3,"%08x %08x\n",*((unsigned int*)&stat->ii.sense.data[8]),*((unsigned int*)&stat->ii.sense.data[12]));
	tape_sprintf_event (tape_dbf_area,3,"%08x %08x\n",*((unsigned int*)&stat->ii.sense.data[16]),*((unsigned int*)&stat->ii.sense.data[20]));
	tape_sprintf_event (tape_dbf_area,3,"%08x %08x\n",*((unsigned int*)&stat->ii.sense.data[24]),*((unsigned int*)&stat->ii.sense.data[28]));
}

/*
 * Write sense data to console/dbf
 */

void
tape_dump_sense (tape_dev_t* td)
{
	devstat_t *stat = &td->devstat;
#if 1 /* XXX */
	PRINT_INFO ("-------------------------------------------------\n");
	PRINT_INFO ("DSTAT : %02x  CSTAT: %02x  CPA: %04x\n",stat->dstat,stat->cstat,stat->cpa);
	PRINT_INFO ("DEVICE: %04x\n",td->devinfo.devno);
	if(TAPE_BUSY(td))
		PRINT_INFO("OP    : %s\n",tape_op_verbose[tape_get_active_ccw_req(td)->op]);
		
	PRINT_INFO ("Sense data: %02X%02X%02X%02X %02X%02X%02X%02X "
		    " %02X%02X%02X%02X %02X%02X%02X%02X \n",
		    stat->ii.sense.data[0], stat->ii.sense.data[1],
		    stat->ii.sense.data[2], stat->ii.sense.data[3],
		    stat->ii.sense.data[4], stat->ii.sense.data[5],
		    stat->ii.sense.data[6], stat->ii.sense.data[7],
		    stat->ii.sense.data[8], stat->ii.sense.data[9],
		    stat->ii.sense.data[10], stat->ii.sense.data[11],
		    stat->ii.sense.data[12], stat->ii.sense.data[13],
		    stat->ii.sense.data[14], stat->ii.sense.data[15]);
	PRINT_INFO ("Sense data: %02X%02X%02X%02X %02X%02X%02X%02X "
		    " %02X%02X%02X%02X %02X%02X%02X%02X \n",
		    stat->ii.sense.data[16], stat->ii.sense.data[17],
		    stat->ii.sense.data[18], stat->ii.sense.data[19],
		    stat->ii.sense.data[20], stat->ii.sense.data[21],
		    stat->ii.sense.data[22], stat->ii.sense.data[23],
		    stat->ii.sense.data[24], stat->ii.sense.data[25],
		    stat->ii.sense.data[26], stat->ii.sense.data[27],
		    stat->ii.sense.data[28], stat->ii.sense.data[29],
		    stat->ii.sense.data[30], stat->ii.sense.data[31]);
	PRINT_INFO ("--------------------------------------------------\n");
#endif
}

/*******************************************************************
 * TAPE Discipline functions
 *******************************************************************/

/*
 * Register backend discipline
 */

int
tape_register_discipline(tape_discipline_t *disc)
{
        tape_discipline_t *disc_ptr;
	long lockflags;

        disc->next = NULL;

	write_lock_irqsave(&tape_discipline_lock,lockflags);

        if (tape_first_disc == NULL) {
            tape_first_disc = disc;
        } else {
                for(disc_ptr = tape_first_disc; disc_ptr->next!=NULL; disc_ptr=(tape_discipline_t*)disc_ptr->next);
                disc_ptr->next = disc;
        }

	tape_create_devs_for_disc(disc);
	if(tape_autoprobe)
		tape_create_devreg_for_disc(disc);

	write_unlock_irqrestore(&tape_discipline_lock,lockflags);
        return 0;
}

/*
 * Unregister backend discipline
 */

void 
tape_unregister_discipline(tape_discipline_t* disc)
{
	tape_discipline_t* lastdisc;
	long lockflags;

	write_lock_irqsave(&tape_discipline_lock,lockflags);

	tape_delete_devs_for_disc(disc);
	if(tape_autoprobe)
		tape_delete_devreg_for_disc(disc);

	if (disc==tape_first_disc) {
		tape_first_disc=disc->next;
        } else {
		lastdisc=tape_first_disc;
		while (lastdisc->next!=disc) lastdisc=lastdisc->next;
		lastdisc->next=disc->next;
	}
	
	disc->shutdown();

	write_unlock_irqrestore(&tape_discipline_lock,lockflags);
}

/*
 * Cleanup all registered disciplines
 */

static inline void
tape_cleanup_disciplines (void) 
{
        long lockflags;
        tape_discipline_t* disc;
	/* Unregister all backend disciplines - this also deletes the devices */

	write_lock_irqsave(&tape_discipline_lock,lockflags);
        disc=tape_first_disc;
        while (disc != NULL) {
		write_unlock_irqrestore(&tape_discipline_lock,lockflags);
		tape_unregister_discipline(disc);
		kfree(disc);
		write_lock_irqsave(&tape_discipline_lock,lockflags);
		disc=tape_first_disc; 
        }
	write_unlock_irqrestore(&tape_discipline_lock,lockflags);
}


/*******************************************************************
 * TAPE Init Functions
 *******************************************************************/

/*
 *      tape_print_banner will be called on initialisation and print a nice banner
 */

static inline void 
tape_print_banner (void) 
{
        char *opt_char,*opt_block;

        /* print banner */        
        PRINT_WARN ("IBM zSeries Tape Device Driver (v%d.%02d).\n",TAPE_VERSION_MAJOR,TAPE_VERSION_MINOR);
        PRINT_WARN ("(C) IBM Deutschland Entwicklung GmbH, 2000 - 2001\n");
        opt_char=opt_block="not present";
#ifdef CONFIG_S390_TAPE_CHAR
        opt_char="built in";
#endif
#ifdef CONFIG_S390_TAPE_BLOCK
        opt_block="built in";
#endif
        /* print feature info */
        PRINT_WARN ("character device frontend   : %s\n",opt_char);
        PRINT_WARN ("block device frontend       : %s\n",opt_block);
}

/*
 *      tape_init will register the driver for each tape.
 */

int
tape_init (void)
{
        static int initialized=0;

        if (initialized) // Only init the devices once
            return 0;
        initialized=1;

	tape_init_devregs();
#ifdef TAPE_DEBUG
        tape_dbf_area = debug_register ( "tape", 2, 2, 3*sizeof(long));
        debug_register_view(tape_dbf_area,&debug_sprintf_view);
        tape_sprintf_event (tape_dbf_area,3,"begin init\n");
#endif /* TAPE_DEBUG */
	tape_print_banner();
#ifndef MODULE
        tape_split_parm_string(tape_parm_string);
#endif
#ifdef CONFIG_DEVFS_FS
        devfs_mk_dir (NULL, "tape", NULL);
#endif /* CONFIG_DEVFS_FS */

        tape_sprintf_event (tape_dbf_area,3,"dev detect\n");
	/* Allocate the tape structures */
        if (*tape!=NULL) { /* if parameters are present */
		PRINT_INFO ("Using ranges supplied in parameters, disabling autoprobe mode.\n");
		tape_parm_parse (tape);
		tape_autoprobe = 0;
	} else {
		PRINT_INFO ("No parameters supplied, enabling autoprobe mode for all supported devices.\n");
		tape_autoprobe = 1;
	}
#ifdef CONFIG_PROC_FS
	tape_proc_init();
#endif /* CONFIG_PROC_FS */
#ifdef  CONFIG_S390_TAPE_3480 
	tape_register_discipline(tape3480_init());
#else
#ifndef CONFIG_S390_TAPE
	tape_request_module("tape_3480_mod");
#endif
#endif /* CONFIG_S390_TAPE_3480 */

#ifdef CONFIG_S390_TAPE_3490
        tape_register_discipline(tape3490_init());
#else
#ifndef CONFIG_S390_TAPE
        tape_request_module("tape_3490_mod");
#endif
#endif /* CONFIG_S390_TAPE_3490 */

#ifdef CONFIG_S390_TAPE_3590
        tape_register_discipline(tape3590_init());
#else
#ifndef CONFIG_S390_TAPE
        tape_request_module("tape_3590_mod");
#endif
#endif /* CONFIG_S390_TAPE_3590 */
	return 0;
}

/*******************************************************************
 * TAPE Module functions
 *******************************************************************/

#ifdef MODULE
MODULE_AUTHOR("(C) 2001 IBM Deutschland Entwicklung GmbH by Carsten Otte and Michael Holzheu (cotte@de.ibm.com,holzheu@de.ibm.com)");
MODULE_DESCRIPTION("Linux on zSeries channel attached tape device driver");
MODULE_PARM (tape, "1-" __MODULE_STRING (256) "s");

/*
 * The famous init_module
 */

int
init_module (void)
{
#ifdef CONFIG_S390_TAPE_CHAR
	tapechar_init ();
#endif
#ifdef CONFIG_S390_TAPE_BLOCK
	tapeblock_init ();
#endif
        return 0;
}

/*
 * Cleanup the frontends
 */
static inline void
tape_cleanup_frontends(void) 
{
        tape_frontend_t* frontend, *tempfe;

	/* Now get rid of the frontends */
#ifdef CONFIG_S390_TAPE_CHAR
	tapechar_uninit();
#endif
#ifdef CONFIG_S390_TAPE_BLOCK
        tapeblock_uninit();
#endif
        frontend=tape_first_front;
	while (frontend != NULL) {
		tempfe = frontend;
		frontend = frontend->next;
		kfree (tempfe);
	}
}


/*
 * Cleanup module
 */

void
cleanup_module (void)
{
        tape_sprintf_event (tape_dbf_area,6,"cleaup mod");
	tape_cleanup_disciplines();
#ifdef CONFIG_DEVFS_FS
	devfs_remove("tape"); /* devfs checks for NULL */
#endif CONFIG_DEVFS_FS
#ifdef CONFIG_PROC_FS
	tape_proc_cleanup();
#endif 
	tape_cleanup_frontends();
	/* Deallocate devregs in case of not autoprobe */
	if(!tape_autoprobe)
		tape_delete_all_devregs();
#ifdef TAPE_DEBUG
        debug_unregister (tape_dbf_area);
#endif /* TAPE_DEBUG */
}
#endif				/* MODULE */


/*******************************************************************
 * TAPE Tapestate functions
 *******************************************************************/

inline void
tape_state_set (tape_dev_t * td, tape_state_t newstate)
{
    if (td->tape_state == TS_NOT_OPER) {
        tape_sprintf_event(tape_dbf_area,3,"ts_set err: not oper\n");
    } else {
        tape_sprintf_event(tape_dbf_area,4,"ts. dev:  %x\n",td->first_minor);
        tape_sprintf_event(tape_dbf_area,4,"old ts:   %s\n",(((tape_state_get (td) < TO_SIZE) && (tape_state_get (td) >=0 )) ?  tape_state_verbose[tape_state_get (td)] : "UNKNOWN TS"));
        tape_sprintf_event(tape_dbf_area,4,"%s\n", 
			(((tape_state_get (td) < TO_SIZE) &&
			(tape_state_get (td) >=0 )) ?
			tape_state_verbose[tape_state_get (td)] :
			"UNKNOWN TS"));
        tape_sprintf_event (tape_dbf_area,4,"new ts:   \n");
        tape_sprintf_event (tape_dbf_area,4,"%s\n",(((newstate < TO_SIZE) &&
                                              (newstate >= 0)) ?
                                             tape_state_verbose[newstate] :
                                             "UNKNOWN TS"));
	td->tape_state = newstate;
    }
}

inline tape_state_t
tape_state_get (tape_dev_t * td)
{
	return (td->tape_state);
}

inline void
tape_med_state_set(tape_dev_t * td, tape_medium_state_t newstate)
{
	if(td->medium_state == newstate)
		goto out;
	switch(newstate){
		case MS_UNLOADED:
			PRINT_INFO("(%x): Tape is unloaded\n",td->devstat.devno);
			break;
		case MS_LOADED:
			PRINT_INFO("(%x): Tape has been mounted\n",td->devstat.devno);
			break;
		default:
			// print nothing
			break;
	}
	td->medium_state = newstate;
out:
	return;
}
	

/*******************************************************************
 * TAPE Exported Functions 
 *******************************************************************/

EXPORT_SYMBOL(tape_register_discipline);
EXPORT_SYMBOL(tape_unregister_discipline);
EXPORT_SYMBOL(tape_dbf_area);
EXPORT_SYMBOL(tape_alloc_ccw_req);
EXPORT_SYMBOL(tape_free_ccw_req); 
EXPORT_SYMBOL(tape_state_set);
EXPORT_SYMBOL(tape_state_get);
EXPORT_SYMBOL(tape_med_state_set);
EXPORT_SYMBOL(tape_state_verbose);
EXPORT_SYMBOL(tape_op_verbose);
EXPORT_SYMBOL(tape_dump_sense);
EXPORT_SYMBOL(tape_dump_sense_dbf);
EXPORT_SYMBOL(tape_do_io);
EXPORT_SYMBOL(tape_do_io_irq);
EXPORT_SYMBOL(tape_do_io_and_wait);
EXPORT_SYMBOL(tape_remove_ccw_req);
EXPORT_SYMBOL(tape_get_active_ccw_req);
