/***************************************************************************
 *
 *  drivers/s390/char/tape.h
 *    tape device driver for 3480/3490E/3590 tapes.
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001 IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *
 ****************************************************************************
 */

#ifndef _TAPE_H
#define _TAPE_H

#include <linux/config.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/mtio.h>
#include <asm/debug.h>
#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif
#include "tape_idalbuf.h"

#define TAPE_VERSION_MAJOR 1
#define TAPE_VERSION_MINOR 10
#define TAPE_MAGIC "tape"
#define TAPE_BUSY(td) (td->treq != NULL)
#define TAPE_MINORS_PER_DEV 2       /* two minors per device */

#define TAPE_MERGE_RC(treq,rc) \
	if( ((rc) == 0) && ((treq)->rc != 0) ) \
		rc = (treq)->rc;

typedef enum{
	TAPE_NO_WAIT,
	TAPE_WAIT,
	TAPE_WAIT_INTERRUPTIBLE,
	TAPE_WAIT_INTERRUPTIBLE_NOHALTIO,
	TAPE_REMOVE_REQ_ON_WAKEUP,
	TAPE_SCHED_BLOCK,
} tape_wait_t;

typedef enum {
	MS_UNKNOWN,
	MS_LOADED,
	MS_UNLOADED,
	MS_SIZE
} tape_medium_state_t;

typedef enum {
	TS_UNUSED=0,
	TS_IN_USE,
	TS_INIT,
	TS_NOT_OPER,
	TS_SIZE
} tape_state_t;

typedef enum {
	TO_BLOCK,
	TO_BSB,
	TO_BSF,
	TO_DSE,
	TO_EGA,
	TO_FSB,
	TO_FSF,
	TO_LDI,
	TO_LBL,
	TO_MSE,
	TO_NOP,
	TO_RBA,
	TO_RBI,
	TO_RBU,
	TO_RBL,
	TO_RDC,
	TO_RFO,
	TO_RSD,
	TO_REW,
	TO_RUN,
	TO_SEN,
	TO_SID,
	TO_SNP,
	TO_SPG,
	TO_SWI,
	TO_SMR,
	TO_SYN,
	TO_TIO,
	TO_UNA,
	TO_WRI,
	TO_WTM,
	TO_MSEN,
	TO_LOAD,
	TO_READ_CONFIG, /* 3590 */
	TO_READ_ATTMSG, /* 3590 */
	TO_NOTHING,
	TO_DIS,
	TO_SIZE
} tape_op_t;

#define TAPE_INTERRUPTIBLE_OP(op) \
	(op == MTEOM) || \
	(op == MTRETEN)

struct _tape_dev_t; //Forward declaration

/* The tape device list lock */
extern rwlock_t   tape_dev_lock;

/* Tape CCW request */
 
typedef struct _tape_ccw_req_t{
	wait_queue_head_t wq;
	ccw1_t*           cpaddr;
	size_t            cplength;
	int               options;
	void*             kernbuf;
	size_t            kernbuf_size;
	idalbuf_t*        idal_buf;
	void*             userbuf;
	size_t            userbuf_size; 
	tape_op_t op;
	void   (*wakeup)(struct _tape_ccw_req_t* treq);
	void   (*wait)(struct _tape_ccw_req_t* treq);
        struct _tape_dev_t* tape_dev;  // Pointer for back reference
	int rc;
	struct _tape_ccw_req_t* recover;
} tape_ccw_req_t;

/* Callback typedefs */

typedef void (*tape_disc_shutdown_t) (void);
typedef void (*tape_event_handler_t) (struct _tape_dev_t*);
typedef tape_ccw_req_t* (*tape_reqgen_ioctl_t)(struct _tape_dev_t* td,int op,int count,int* rc);
typedef tape_ccw_req_t* (*tape_reqgen_bread_t)(struct request* req,struct _tape_dev_t* td,int tapeblock_major);
typedef void (*tape_reqgen_enable_loc_t) (tape_ccw_req_t*);
typedef void (*tape_free_bread_t)(tape_ccw_req_t*);
typedef tape_ccw_req_t* (*tape_reqgen_rw_t)(const char* data,size_t count,struct _tape_dev_t* td);
typedef int (*tape_setup_device_t) (struct _tape_dev_t*);
typedef void (*tape_cleanup_device_t) (struct _tape_dev_t*);
typedef int (*tape_disc_ioctl_overl_t)(struct _tape_dev_t*, unsigned int,unsigned long);
#ifdef CONFIG_DEVFS_FS
typedef devfs_handle_t (*tape_devfs_constructor_t) (struct _tape_dev_t*);
typedef void (*tape_devfs_destructor_t) (struct _tape_dev_t*);
#endif

/* Tape Discipline */

typedef struct _tape_discipline_t {
	struct module               *owner;
	unsigned int                cu_type;
	tape_setup_device_t         setup_device;
	tape_cleanup_device_t       cleanup_device;
	tape_event_handler_t        init_device;
	tape_event_handler_t        process_eov;
	tape_event_handler_t        irq;
	tape_reqgen_bread_t         bread;
	tape_free_bread_t           free_bread;
	tape_reqgen_enable_loc_t    bread_enable_locate;
	tape_reqgen_rw_t            write_block;
	tape_reqgen_rw_t            read_block;
	tape_reqgen_ioctl_t         ioctl;
	tape_disc_shutdown_t        shutdown;
	tape_disc_ioctl_overl_t     discipline_ioctl_overload;
	void*                       next;
} tape_discipline_t  __attribute__ ((aligned(8)));

/* Frontend */

typedef struct _tape_frontend_t {
	tape_setup_device_t device_setup;
#ifdef CONFIG_DEVFS_FS
	tape_devfs_constructor_t mkdevfstree;
	tape_devfs_destructor_t rmdevfstree;
#endif
	void* next;
} tape_frontend_t  __attribute__ ((aligned(8)));

/* Char Frontend Data */

typedef struct _tape_char_front_data_t{
	int     block_size;               /* block size of tape */
} tape_char_data_t;

/* Block Frontend Data */

typedef struct _tape_blk_front_data_t{
	request_queue_t request_queue;
	struct request* current_request;
	int blk_retries;
	long position;
	atomic_t bh_scheduled;
	struct tq_struct bh_tq;
} tape_blk_data_t;

/* Tape Info */
 
typedef struct _tape_dev_t {
	atomic_t use_count;            /* Reference count, when == 0 delete */
	int      first_minor;          /* each tape device has two minors */
	s390_dev_info_t devinfo;       /* device info from Common I/O */
	devstat_t devstat;             /* contains irq, devno, status */
	struct file *filp;             /* backpointer to file structure */
	int tape_state;                /* State of the device. See tape_stat */
	int medium_state;              /* loaded, unloaded, unkown etc. */
	tape_discipline_t* discipline; /* The used discipline */
	void* discdata;                /* discipline specific data */
	tape_ccw_req_t* treq;          /* Active Tape request */
	tape_op_t last_op;             /* Last Tape operation */
	void*  next;                   /* ptr to next tape_dev */
	tape_char_data_t  char_data;   /* Character dev frontend data */
	tape_blk_data_t   blk_data;    /* Block dev frontend data */
} tape_dev_t  __attribute__ ((aligned(8)));

/* tape functions */

#define TAPE_MEMB_IRQ       0
#define TAPE_MEMB_MINOR     1
#define TAPE_MEMB_QUEUE     2

tape_dev_t* __tape_get_device_by_member(unsigned long value, int member);

/*
 * Search for tape structure with specific minor number
 */
 
static inline tape_dev_t *
tape_get_device_by_minor(int minor)
{
        return __tape_get_device_by_member(minor, TAPE_MEMB_MINOR);
}

/*
 * Search for tape structure with specific IRQ
 */
 
static inline tape_dev_t *
tape_get_device_by_irq(int irq)
{
        return __tape_get_device_by_member(irq, TAPE_MEMB_IRQ);
}

/*
 * Search for tape structure with specific queue
 */

static inline tape_dev_t*
tape_get_device_by_queue(void* queue)
{
	return __tape_get_device_by_member((unsigned long)queue, TAPE_MEMB_QUEUE);
}

/*
 * Increment use count of tape structure
 */
 
static inline void
tape_get_device(tape_dev_t* td)
{
        if (td!=NULL)
                atomic_inc(&(td->use_count));
}

void tape_put_device(tape_dev_t* td);

/* Discipline functions */
int tape_register_discipline(tape_discipline_t* disc);
void tape_unregister_discipline(tape_discipline_t* disc);

/* tape initialisation functions */
int tape_init(void);

/* a function for dumping device sense info */
void tape_dump_sense (tape_dev_t* td);
void tape_dump_sense_dbf(tape_dev_t* td);
 
/* functions for handling the status of a device */
inline void tape_state_set (tape_dev_t* td, tape_state_t newstate);
inline tape_state_t tape_state_get (tape_dev_t* td);
inline void tape_med_state_set(tape_dev_t* td, tape_medium_state_t newstate);

/* functions for alloc'ing ccw and IO stuff */
inline  tape_ccw_req_t* tape_alloc_ccw_req(int cplength,int datasize,int idal_buf_size, tape_op_t op);
void tape_free_ccw_req (tape_ccw_req_t * request);
int tape_do_io(tape_dev_t * td,tape_ccw_req_t *treq,tape_wait_t type);
int tape_do_io_irq(tape_dev_t * td,tape_ccw_req_t *treq,tape_wait_t type);
int tape_do_io_and_wait(tape_dev_t * td,tape_ccw_req_t *treq,tape_wait_t type);
int tape_do_wait_req(tape_dev_t * td,tape_ccw_req_t *treq,tape_wait_t type);
int tape_remove_ccw_req(tape_dev_t* td,tape_ccw_req_t* treq);
tape_ccw_req_t* tape_get_active_ccw_req(tape_dev_t* td);

/* The debug area */
#ifdef TAPE_DEBUG
        extern debug_info_t *tape_dbf_area;
        #define tape_sprintf_event debug_sprintf_event
        #define tape_sprintf_exception debug_sprintf_exception
#else
        #define tape_sprintf_event
        #define tape_sprintf_exception
#endif

/* functions for building ccws */
static inline ccw1_t* 
__ccwprep(ccw1_t* ccw, __u8 cmd_code, __u8 flags, __u16 memsize, void* cda,int ccw_count)
{
	int i;
#ifdef CONFIG_ARCH_S390X
        if ((unsigned long)cda >= (1UL<<31)){
                printk("cda: %p\n",cda);
	        BUG();
        }
#endif /* CONFIG_ARCH_S390X */
	for(i = 0 ; i < ccw_count; i++){
        	ccw[i].cmd_code = cmd_code;
		ccw[i].flags |= CCW_FLAG_CC;
		ccw[i].count = memsize;
		if(cda == 0)
			ccw[i].cda = (unsigned long)&(ccw[i].cmd_code);
		else
			ccw[i].cda = (unsigned long)cda;
	}
        ccw[ccw_count-1].flags = flags;
	return &ccw[ccw_count];
};

extern inline ccw1_t*
tape_ccw_cc(ccw1_t *ccw,__u8 cmd_code,__u16 memsize,void* cda,int ccw_count)
{
        return __ccwprep(ccw,cmd_code,CCW_FLAG_CC,memsize,cda,ccw_count);
}
 
extern inline ccw1_t*
tape_ccw_end(ccw1_t *ccw,__u8 cmd_code,__u16 memsize,void* cda,int ccw_count)
{
        return __ccwprep(ccw,cmd_code,0,memsize,cda,ccw_count);
}

extern inline ccw1_t*
tape_ccw_cc_idal(ccw1_t *ccw,__u8 cmd_code,idalbuf_t* idal)
{
	ccw->cmd_code = cmd_code;
	ccw->flags    = CCW_FLAG_CC;
	idalbuf_set_normalized_cda(ccw,idal);
	return ccw++;
}

extern inline ccw1_t*
tape_ccw_end_idal(ccw1_t *ccw,__u8 cmd_code,idalbuf_t* idal)
{
        ccw->cmd_code = cmd_code;
        ccw->flags    = 0;
        idalbuf_set_normalized_cda(ccw,idal);
        return ccw++;
}

/* Global vars */
extern const char* tape_state_verbose[TS_SIZE];
extern const char* tape_op_verbose[TO_SIZE];

/* Some linked lists for storing plugins and devices */
extern tape_dev_t *tape_first_dev;
extern tape_frontend_t *tape_first_front;

#endif /* for ifdef tape.h */
