/* 
 * File...........: linux/drivers/s390/block/dasd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 *
 * History of changes (starts July 2000)
 * 02/01/01 added dynamic registration of ioctls
 */

#ifndef DASD_H
#define DASD_H

#undef ERP_DEBUG               /* enable debug messages */
#undef ERP_FULL_ERP            /* enable full ERP - experimental code !!!! */
#define CONFIG_DASD_DYNAMIC

#include <linux/ioctl.h>
#include <asm/irq.h>

#define IOCTL_LETTER 'D'
/* Disable the volume (for Linux) */
#define BIODASDDISABLE _IO(IOCTL_LETTER,0) 
/* Enable the volume (for Linux) */
#define BIODASDENABLE  _IO(IOCTL_LETTER,1) 
/* Issue a reserve/release command, rsp. */
#define BIODASDRSRV    _IO(IOCTL_LETTER,2) /* reserve */
#define BIODASDRLSE    _IO(IOCTL_LETTER,3) /* release */
#define BIODASDSLCK    _IO(IOCTL_LETTER,4) /* steal lock */
/* Read sense ID infpormation */
#define BIODASDRSID    _IOR(IOCTL_LETTER,0,senseid_t)
/* Format the volume or an extent */
#define BIODASDFORMAT  _IOW(IOCTL_LETTER,0,format_data_t) 
/* translate blocknumber of partition to absolute */
#define BIODASDRWTB    _IOWR(IOCTL_LETTER,0,int)

typedef int(*dasd_ioctl_fn_t) (struct inode *inp, int no, long args);
int dasd_ioctl_no_register(int no, dasd_ioctl_fn_t handler);
int dasd_ioctl_no_unregister(int no, dasd_ioctl_fn_t handler);

#define DASD_NAME "dasd"
#define DASD_PARTN_BITS 2
#define DASD_PER_MAJOR ( 1U<<(MINORBITS-DASD_PARTN_BITS))

/* 
 * struct format_data_t
 * represents all data necessary to format a dasd
 */
typedef struct format_data_t {
	int start_unit; /* from track */
	int stop_unit;  /* to track */
	int blksize;    /* sectorsize */
        int intensity;  /* 0: normal, 1:record zero, 3:home address, 4 invalidate tracks */
} __attribute__ ((packed)) format_data_t;

#define DASD_FORMAT_DEFAULT_START_UNIT 0
#define DASD_FORMAT_DEFAULT_STOP_UNIT -1
#define DASD_FORMAT_DEFAULT_BLOCKSIZE -1
#define DASD_FORMAT_DEFAULT_INTENSITY -1

#define DASD_FORMAT_INTENS_WRITE_RECZERO 0x01
#define DASD_FORMAT_INTENS_WRITE_HOMEADR 0x02
#define DASD_FORMAT_INTENS_INVALIDATE    0x04
#define DASD_FORMAT_INTENS_CDL 0x08
#ifdef __KERNEL__
#include <linux/version.h>
#include <linux/major.h>
#include <linux/wait.h>
#include <asm/ccwcache.h>
#include <linux/blk.h> 
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
#include <linux/blkdev.h> 
#include <linux/devfs_fs_kernel.h>
#endif
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/compatmac.h>

#include <asm/s390dyn.h>
#include <asm/todclk.h>
#include <asm/debug.h>

/* Kernel Version Compatibility section */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,98))
typedef struct request *request_queue_t;
#define block_device_operations file_operations
#define __setup(x,y) struct dasd_device_t
#define devfs_register_blkdev(major,name,ops) register_blkdev(major,name,ops)
#define register_disk(dd,dev,partn,ops,size) \
do { \
	dd->sizes[MINOR(dev)] = size >> 1; \
	resetup_one_dev(dd,MINOR(dev)>>DASD_PARTN_BITS); \
} while(0)
#define init_waitqueue_head(x) do { *x = NULL; } while(0)
#define blk_cleanup_queue(x) do {} while(0)
#define blk_init_queue(x...) do {} while(0)
#define blk_queue_headactive(x...) do {} while(0)
#define blk_queue_make_request(x) do {} while(0)
#define list_empty(x) (0)
#define INIT_BLK_DEV(d_major,d_request_fn,d_queue_fn,d_current) \
do { \
        blk_dev[d_major].request_fn = d_request_fn; \
        blk_dev[d_major].queue = d_queue_fn; \
        blk_dev[d_major].current_request = d_current; \
} while(0)
#define INIT_GENDISK(D_MAJOR,D_NAME,D_PARTN_BITS,D_PER_MAJOR) \
	major:D_MAJOR, \
	major_name:D_NAME, \
	minor_shift:D_PARTN_BITS, \
	max_p:1 << D_PARTN_BITS, \
	max_nr:D_PER_MAJOR, \
	nr_real:D_PER_MAJOR,
static inline struct request * 
dasd_next_request( request_queue_t *queue ) 
{
    return *queue;
}
static inline void 
dasd_dequeue_request( request_queue_t * q, struct request *req )
{
        *q = req->next;
        req->next = NULL;
}
#else
#define INIT_BLK_DEV(d_major,d_request_fn,d_queue_fn,d_current) \
do { \
        blk_dev[d_major].queue = d_queue_fn; \
} while(0)
#define INIT_GENDISK(D_MAJOR,D_NAME,D_PARTN_BITS,D_PER_MAJOR) \
	major:D_MAJOR, \
	major_name:D_NAME, \
	minor_shift:D_PARTN_BITS, \
	max_p:1 << D_PARTN_BITS, \
	nr_real:D_PER_MAJOR,
static inline struct request * 
dasd_next_request( request_queue_t *queue ) 
{
        return blkdev_entry_next_request(&queue->queue_head);
}
static inline void 
dasd_dequeue_request( request_queue_t * q, struct request *req )
{
        blkdev_dequeue_request (req);
}
#endif

/* dasd_range_t are used for dynamic device att-/detachment */
typedef struct dasd_devreg_t {
        devreg_t devreg; /* the devreg itself */
        /* build a linked list of devregs, needed for cleanup */
        struct list_head list;
} dasd_devreg_t;

typedef struct {
	struct list_head list;
	int no;
	dasd_ioctl_fn_t handler;
} dasd_ioctl_list_t;

typedef enum {
	dasd_era_fatal = -1,	/* no chance to recover              */
	dasd_era_none = 0,	/* don't recover, everything alright */
	dasd_era_msg = 1,	/* don't recover, just report...     */
	dasd_era_recover = 2	/* recovery action recommended       */
} dasd_era_t;

/* BIT DEFINITIONS FOR SENSE DATA */
#define DASD_SENSE_BIT_0 0x80
#define DASD_SENSE_BIT_1 0x40
#define DASD_SENSE_BIT_2 0x20
#define DASD_SENSE_BIT_3 0x10

#define check_then_set(where,from,to) \
do { \
        if ((*(where)) != (from) ) { \
                printk (KERN_ERR PRINTK_HEADER "was %d\n", *(where)); \
                BUG(); \
        } \
        (*(where)) = (to); \
} while (0)

#define DASD_MESSAGE(d_loglevel,d_device,d_string,d_args...)\
do { \
        int d_devno = d_device->devinfo.devno; \
        int d_irq = d_device->devinfo.irq; \
        char *d_name = d_device->name; \
        int d_major = MAJOR(d_device->kdev); \
        int d_minor = MINOR(d_device->kdev); \
        printk(d_loglevel PRINTK_HEADER \
               "/dev/%s(%d:%d),%04X IRQ0x%x:" \
               d_string "\n",d_name,d_major,d_minor,d_devno,d_irq,d_args ); \
} while(0)

/* 
 * struct dasd_sizes_t
 * represents all data needed to access dasd with properly set up sectors
 */
typedef
struct dasd_sizes_t {
	unsigned long blocks; /* size of volume in blocks */
	unsigned int bp_block; /* bytes per block */
	unsigned int s2b_shift; /* log2 (bp_block/512) */
        unsigned int pt_block; /* from which block to read the partn table */
} dasd_sizes_t;

/* 
 * struct dasd_chanq_t 
 * represents a queue of channel programs related to a single device
 */
typedef
struct dasd_chanq_t {
	ccw_req_t *head;
	ccw_req_t *tail;
} dasd_chanq_t;

#define DASD_DEVICE_FORMAT_STRING "Device: %p"
#define DASD_DEVICE_DEBUG_EVENT(d_level, d_device, d_str, d_data...)\
do {\
        if ( d_device->debug_area != NULL )\
        debug_sprintf_event(d_device->debug_area,d_level,\
                    DASD_DEVICE_FORMAT_STRING d_str "\n",\
                    d_device, d_data);\
} while(0);
#define DASD_DEVICE_DEBUG_EXCEPTION(d_level, d_device, d_str, d_data...)\
do {\
        if ( d_device->debug_area != NULL )\
        debug_sprintf_exception(d_device->debug_area,d_level,\
                        DASD_DEVICE_FORMAT_STRING d_str "\n",\
                        d_device, d_data);\
} while(0);

#define DASD_DRIVER_FORMAT_STRING "Driver: <[%p]>"
#define DASD_DRIVER_DEBUG_EVENT(d_level, d_fn, d_str, d_data...)\
do {\
        if ( dasd_debug_area != NULL )\
        debug_sprintf_event(dasd_debug_area, d_level,\
                    DASD_DRIVER_FORMAT_STRING #d_fn ":" d_str "\n",\
                    d_fn, d_data);\
} while(0);
#define DASD_DRIVER_DEBUG_EXCEPTION(d_level, d_fn, d_str, d_data...)\
do {\
        if ( dasd_debug_area != NULL )\
        debug_sprintf_exception(dasd_debug_area, d_level,\
                        DASD_DRIVER_FORMAT_STRING #d_fn ":" d_str "\n",\
                        d_fn, d_data);\
} while(0);

struct dasd_device_t;
struct request;

/* 
 * signatures for the functions of dasd_discipline_t 
 * make typecasts much easier
 */
typedef ccw_req_t *(*dasd_erp_action_fn_t) (ccw_req_t * cqr);
typedef ccw_req_t *(*dasd_erp_postaction_fn_t) (ccw_req_t * cqr);

typedef int (*dasd_ck_id_fn_t) (s390_dev_info_t *);
typedef int (*dasd_ck_characteristics_fn_t) (struct dasd_device_t *);
typedef int (*dasd_fill_geometry_fn_t) (struct dasd_device_t *, struct hd_geometry *);
typedef ccw_req_t *(*dasd_format_fn_t) (struct dasd_device_t *, struct format_data_t *);
typedef ccw_req_t *(*dasd_init_analysis_fn_t) (struct dasd_device_t *);
typedef int (*dasd_do_analysis_fn_t) (struct dasd_device_t *);
typedef int (*dasd_io_starter_fn_t) (ccw_req_t *);
typedef void (*dasd_int_handler_fn_t)(int irq, void *, struct pt_regs *);
typedef dasd_era_t (*dasd_error_examine_fn_t) (ccw_req_t *, devstat_t * stat);
typedef dasd_erp_action_fn_t (*dasd_error_analyse_fn_t) (ccw_req_t *);
typedef dasd_erp_postaction_fn_t (*dasd_erp_analyse_fn_t) (ccw_req_t *);
typedef ccw_req_t *(*dasd_cp_builder_fn_t)(struct dasd_device_t *,struct request *);
typedef char *(*dasd_dump_sense_fn_t)(struct dasd_device_t *,ccw_req_t *);
typedef ccw_req_t *(*dasd_reserve_fn_t)(struct dasd_device_t *);
typedef ccw_req_t *(*dasd_release_fn_t)(struct dasd_device_t *);
typedef ccw_req_t *(*dasd_merge_cp_fn_t)(struct dasd_device_t *);


/*
 * the dasd_discipline_t is
 * sth like a table of virtual functions, if you think of dasd_eckd
 * inheriting dasd...
 * no, currently we are not planning to reimplement the driver in C++
 */
typedef struct dasd_discipline_t {
	char ebcname[8]; /* a name used for tagging and printks */
        char name[8];		/* a name used for tagging and printks */
	int max_blocks;	/* maximum number of blocks to be chained */
	dasd_ck_id_fn_t id_check;	/* to check sense data */
	dasd_ck_characteristics_fn_t check_characteristics;	/* to check the characteristics */
	dasd_init_analysis_fn_t init_analysis;	/* to start the analysis of the volume */
	dasd_do_analysis_fn_t do_analysis;	/* to complete the analysis of the volume */
	dasd_fill_geometry_fn_t fill_geometry;	/* to set up hd_geometry */
	dasd_io_starter_fn_t start_IO;
        dasd_format_fn_t format_device;		/* to format the device */
	dasd_error_examine_fn_t examine_error;
	dasd_error_analyse_fn_t erp_action;
	dasd_erp_analyse_fn_t erp_postaction;
        dasd_cp_builder_fn_t build_cp_from_req;
        dasd_dump_sense_fn_t dump_sense;
        dasd_int_handler_fn_t int_handler;
        dasd_reserve_fn_t reserve;
        dasd_release_fn_t release;
        dasd_merge_cp_fn_t merge_cp;
	
	struct dasd_discipline_t *next;	/* used for list of disciplines */
} dasd_discipline_t;

#define DASD_MAJOR_INFO_REGISTERED 1
#define DASD_MAJOR_INFO_IS_STATIC 2

typedef struct major_info_t {
	struct list_head list;
	struct dasd_device_t **dasd_device;
	int flags;
	struct gendisk gendisk; /* actually contains the major number */
} __attribute__ ((packed)) major_info_t;

typedef struct dasd_profile_info_t {
        unsigned long dasd_io_reqs;	/* number of requests processed at all */
        unsigned long dasd_io_secs[32];	/* histogram of request's sizes */
        unsigned long dasd_io_times[32];	/* histogram of requests's times */
        unsigned long dasd_io_timps[32];	/* histogram of requests's times per sector */
        unsigned long dasd_io_time1[32];	/* histogram of time from build to start */
       unsigned  long dasd_io_time2[32];	/* histogram of time from start to irq */
        unsigned long dasd_io_time2ps[32];	/* histogram of time from start to irq */
        unsigned long dasd_io_time3[32];	/* histogram of time from irq to end */
} dasd_profile_info_t;

typedef struct dasd_device_t {
	s390_dev_info_t devinfo;
	dasd_discipline_t *discipline;
	int level;
        int open_count;
        kdev_t kdev;
        major_info_t *major_info;
	struct dasd_chanq_t queue;
        wait_queue_head_t wait_q;
        request_queue_t request_queue;
        struct timer_list timer;      
	devstat_t dev_status; /* needed ONLY!! for request_irq */
        dasd_sizes_t sizes;
        char name[16]; /* The name of the device in /dev */
	char *private;	/* to be used by the discipline internally */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        devfs_handle_t devfs_entry;
#endif /* LINUX_IS_24 */
	struct tq_struct bh_tq;
        atomic_t bh_scheduled;
        debug_info_t *debug_area;
        dasd_profile_info_t profile;
        struct proc_dir_entry *proc_dir; /* directory node */
        struct proc_dir_entry *proc_info; /* information from dasd_device_t */
        struct proc_dir_entry *proc_stats; /* statictics information */
}  dasd_device_t;

/* dasd_device_t.level can be: */
#define DASD_DEVICE_LEVEL_UNKNOWN 0x00
#define DASD_DEVICE_LEVEL_RECOGNIZED 0x01
#define DASD_DEVICE_LEVEL_ANALYSIS_PENDING 0x02
#define DASD_DEVICE_LEVEL_ANALYSIS_PREPARED 0x04
#define DASD_DEVICE_LEVEL_ANALYSED 0x08
#define DASD_DEVICE_LEVEL_ONLINE 0x10

int dasd_init (void);
void dasd_discipline_enq (dasd_discipline_t *);
int dasd_discipline_deq(dasd_discipline_t *);
int dasd_start_IO (ccw_req_t *);
void dasd_int_handler (int , void *, struct pt_regs *);
ccw_req_t *default_erp_action (ccw_req_t *);
ccw_req_t *default_erp_postaction (ccw_req_t *);
int dasd_chanq_deq (dasd_chanq_t *, ccw_req_t *);
ccw_req_t *dasd_alloc_request (char *, int, int);
void dasd_free_request (ccw_req_t *);
extern int (*genhd_dasd_name) (char *, int, int, struct gendisk *);
extern int (*genhd_dasd_fillgeo) (int, struct hd_geometry *);
int dasd_oper_handler (int irq, devreg_t * devreg);
void dasd_schedule_bh (dasd_device_t *);

#endif /* __KERNEL__ */

#endif				/* DASD_H */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
