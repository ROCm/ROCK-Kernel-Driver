/*
 * File...........: linux/drivers/s390/block/dasd_types.h
 * Author.........: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Created........: 08/31/1999
 * Last Modified..: 09/29/1999
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000

 * List of Changes:
 - Initial Release as of 09/29/1999

 * Description

 * Restrictions

 * Known Bugs

 * Todo-List

 */

#ifndef DASD_TYPES_H
#define DASD_TYPES_H

#include <linux/config.h>
#include <linux/dasd.h>
#include <linux/blkdev.h>

#include <asm/irq.h>

#define CCW_DEFINE_EXTENT 0x63
#define CCW_LOCATE_RECORD 0x43
#define CCW_READ_DEVICE_CHARACTERISTICS 0x64

typedef
enum {
	dasd_none = -1,
#ifdef CONFIG_DASD_ECKD
	dasd_eckd,
#endif				/* CONFIG_DASD_ECKD */
#ifdef CONFIG_DASD_MDSK                                        
        dasd_mdsk,                                             
#endif                          /* CONFIG_DASD_MDSK */         
#ifdef CONFIG_DASD_CKD                                        
        dasd_ckd,                                             
#endif                          /* CONFIG_DASD_CKD */         
        dasd_end
} dasd_type_t;

typedef
struct {
	__u16 cu_type;
	struct {
		unsigned char support:2;
		unsigned char async:1;
		unsigned char reserved:1;
		unsigned char cache_info:1;
		unsigned char model:3;
	} __attribute__ ((packed)) cu_model;
	__u16 dev_type;
	__u8 dev_model;
	struct {
		unsigned char mult_burst:1;
		unsigned char RT_in_LR:1;
		unsigned char reserved1:1;
		unsigned char RD_IN_LR:1;
		unsigned char reserved2:4;
		unsigned char reserved3:8;
		unsigned char defect_wr:1;
		unsigned char reserved4:2;
		unsigned char striping:1;
		unsigned char reserved5:4;
		unsigned char cfw:1;
		unsigned char reserved6:2;
		unsigned char cache:1;
		unsigned char dual_copy:1;
		unsigned char dfw:1;
		unsigned char reset_alleg:1;
		unsigned char sense_down:1;
	} __attribute__ ((packed)) facilities;
	__u8 dev_class;
	__u8 unit_type;
	__u16 no_cyl;
	__u16 trk_per_cyl;
	__u8 sec_per_trk;
	__u8 byte_per_track[3];
	__u16 home_bytes;
	__u8 formula;
	union {
		struct {
			__u8 f1;
			__u16 f2;
			__u16 f3;
		} __attribute__ ((packed)) f_0x01;
		struct {
			__u8 f1;
			__u8 f2;
			__u8 f3;
			__u8 f4;
			__u8 f5;
		} __attribute__ ((packed)) f_0x02;
	} __attribute__ ((packed)) factors;
	__u16 first_alt_trk;
	__u16 no_alt_trk;
	__u16 first_dia_trk;
	__u16 no_dia_trk;
	__u16 first_sup_trk;
	__u16 no_sup_trk;
	__u8 MDR_ID;
	__u8 OBR_ID;
	__u8 director;
	__u8 rd_trk_set;
	__u16 max_rec_zero;
	__u8 reserved1;
	__u8 RWANY_in_LR;
	__u8 factor6;
	__u8 factor7;
	__u8 factor8;
	__u8 reserved2[3];
	__u8 reserved3[10];
} __attribute__ ((packed, aligned (32))) 

dasd_eckd_characteristics_t;

/* eckd count area */
typedef struct {
	__u16 cyl;
	__u16 head;
	__u8 record;
	__u8 kl;
	__u16 dl;
} __attribute__ ((packed))

eckd_count_t;

#ifdef CONFIG_DASD_CKD
struct dasd_ckd_characteristics {
	char info[64];
};

#endif				/* CONFIG_DASD_CKD */

#ifdef CONFIG_DASD_ECKD
struct dasd_eckd_characteristics {
	char info[64];
};

#endif				/* CONFIG_DASD_ECKD */

typedef
union {
	char __attribute__ ((aligned (32))) bytes[64];
#ifdef CONFIG_DASD_CKD
	struct dasd_ckd_characteristics ckd;
#endif				/* CONFIG_DASD_CKD */
#ifdef CONFIG_DASD_ECKD
	dasd_eckd_characteristics_t eckd;
#endif				/* CONFIG_DASD_ECKD */
} __attribute__ ((aligned (32))) 

dasd_characteristics_t;

#define CQR_STATUS_EMPTY  0x00
#define CQR_STATUS_FILLED 0x01
#define CQR_STATUS_QUEUED 0x02
#define CQR_STATUS_IN_IO  0x04
#define CQR_STATUS_DONE   0x08
#define CQR_STATUS_RETRY  0x10
#define CQR_STATUS_ERROR  0x20
#define CQR_STATUS_FAILED 0x40
#define CQR_STATUS_SLEEP  0x80

#define CQR_FLAGS_SLEEP   0x01
#define CQR_FLAGS_WAIT    0x02
#define CQR_FLAGS_NOLOCK  0x04
#define CQR_FLAGS_NORETRY 0x08

typedef
struct cqr_t {
	unsigned int    magic;    /* magic number should be "DASD" */
	atomic_t status;	/* current status of request */
	unsigned short retries; /* counter for retry in error case */
	unsigned short cplength;/* Length of channel program  (CP) */
	unsigned short devindex;/* device number */
	unsigned short flags;    /* Flags for execution */
	
	void * data;		/* additional data area for CP */
	ccw1_t *cpaddr;		/* Address of CP */
	struct request *req;	/* backpointer to struct request */
	struct cqr_t *next;     /* forward chain in chanq */
	struct cqr_t *int4cqr;  /* which cqr ist the nect PCI for? */
	unsigned long long buildclk;
	unsigned long long startclk;
	unsigned long long stopclk;
	unsigned long long endclk;
	devstat_t *dstat;	/* savearea for devstat */
	spinlock_t lock;
	int options;
} __attribute__ ((packed)) 
cqr_t;

typedef
struct {
	unsigned long int kbytes;
	unsigned int bp_sector;
	unsigned int bp_block;
	unsigned int blocks;
	unsigned int s2b_shift;
	unsigned int b2k_shift;
	unsigned int label_block;
} dasd_sizes_t;

#define DASD_CHANQ_ACTIVE 0x01
#define DASD_CHANQ_BUSY 0x02
#define DASD_REQUEST_Q_BROKEN 0x04

typedef
struct dasd_chanq_t {
	volatile cqr_t *head;
	volatile cqr_t *tail;
	spinlock_t q_lock;	/* lock for queue operations */
	spinlock_t f_lock;	/* lock for flag operations */
	int queued_requests;
	atomic_t flags;
	atomic_t dirty_requests;
	struct dasd_chanq_t *next_q;	/* pointer to next queue */
} __attribute__ ((packed, aligned (16))) 
dasd_chanq_t;

#define DASD_INFO_FLAGS_INITIALIZED 0x01
#define DASD_INFO_FLAGS_NOT_FORMATTED 0x02
#define DASD_INFO_FLAGS_PARTNS_DETECTED 0x04

typedef
struct dasd_information_t {
	devstat_t dev_status;
	dasd_characteristics_t *rdc_data;
	dasd_volume_label_t *label;
	dasd_type_t type;
	dev_info_t info;
	dasd_sizes_t sizes;
	dasd_chanq_t queue;
	int open_count;
	spinlock_t lock;
	struct semaphore sem;
	unsigned long flags;
	int irq;
	struct proc_dir_entry *proc_device;
	devfs_handle_t devfs_entry;
	union {
		struct {
			eckd_count_t count_data;
		} eckd;
		struct {
			char dummy;
		} fba;
		struct {
			char dummy;
		} mdsk;
		struct {
			char dummy;
		} ckd;
	} private;
} dasd_information_t;

typedef struct {
	int start_unit;
	int stop_unit;
	int blksize;
} format_data_t;

typedef
struct {
	int (*ck_devinfo) (dev_info_t *);
	cqr_t *(*get_req_ccw) (int, struct request *);
	cqr_t *(*rw_label) (int, int, char *);
	int (*ck_characteristics) (dasd_characteristics_t *);
	cqr_t *(*fill_sizes_first) (int);
	int (*fill_sizes_last) (int);
	int (*dasd_format) (int, format_data_t *);
} dasd_operations_t;

extern dasd_information_t *dasd_info[];

#endif				/* DASD_TYPES_H */
