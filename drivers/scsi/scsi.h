/*
 *  scsi.h Copyright (C) 1992 Drew Eckhardt 
 *         Copyright (C) 1993, 1994, 1995, 1998, 1999 Eric Youngdale
 *  generic SCSI package header file by
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *       Modified by Eric Youngdale eric@andante.org to
 *       add scatter-gather, multiple outstanding request, and other
 *       enhancements.
 */

#ifndef _SCSI_H
#define _SCSI_H

#include <linux/config.h>	    /* for CONFIG_SCSI_LOGGING */
#include <linux/devfs_fs_kernel.h>  /* some morons don't know struct pointers */
#include <scsi/scsi.h>


/*
 * These are the values that the SCpnt->sc_data_direction and 
 * SRpnt->sr_data_direction can take.  These need to be set
 * The SCSI_DATA_UNKNOWN value is essentially the default.
 * In the event that the command creator didn't bother to
 * set a value, you will see SCSI_DATA_UNKNOWN.
 */
#define SCSI_DATA_UNKNOWN       0
#define SCSI_DATA_WRITE         1
#define SCSI_DATA_READ          2
#define SCSI_DATA_NONE          3

#ifdef CONFIG_PCI
#include <linux/pci.h>
#if ((SCSI_DATA_UNKNOWN == PCI_DMA_BIDIRECTIONAL) && (SCSI_DATA_WRITE == PCI_DMA_TODEVICE) && (SCSI_DATA_READ == PCI_DMA_FROMDEVICE) && (SCSI_DATA_NONE == PCI_DMA_NONE))
#define scsi_to_pci_dma_dir(scsi_dir)	((int)(scsi_dir))
#else
extern __inline__ int scsi_to_pci_dma_dir(unsigned char scsi_dir)
{
        if (scsi_dir == SCSI_DATA_UNKNOWN)
                return PCI_DMA_BIDIRECTIONAL;
        if (scsi_dir == SCSI_DATA_WRITE)
                return PCI_DMA_TODEVICE;
        if (scsi_dir == SCSI_DATA_READ)
                return PCI_DMA_FROMDEVICE;
        return PCI_DMA_NONE;
}
#endif
#endif

#if defined(CONFIG_SBUS) && !defined(CONFIG_SUN3) && !defined(CONFIG_SUN3X)
#include <asm/sbus.h>
#if ((SCSI_DATA_UNKNOWN == SBUS_DMA_BIDIRECTIONAL) && (SCSI_DATA_WRITE == SBUS_DMA_TODEVICE) && (SCSI_DATA_READ == SBUS_DMA_FROMDEVICE) && (SCSI_DATA_NONE == SBUS_DMA_NONE))
#define scsi_to_sbus_dma_dir(scsi_dir)	((int)(scsi_dir))
#else
extern __inline__ int scsi_to_sbus_dma_dir(unsigned char scsi_dir)
{
        if (scsi_dir == SCSI_DATA_UNKNOWN)
                return SBUS_DMA_BIDIRECTIONAL;
        if (scsi_dir == SCSI_DATA_WRITE)
                return SBUS_DMA_TODEVICE;
        if (scsi_dir == SCSI_DATA_READ)
                return SBUS_DMA_FROMDEVICE;
        return SBUS_DMA_NONE;
}
#endif
#endif

/*
 * Some defs, in case these are not defined elsewhere.
 */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define MAX_SCSI_DEVICE_CODE 14
extern const char *const scsi_device_types[MAX_SCSI_DEVICE_CODE];

#ifdef DEBUG
#define SCSI_TIMEOUT (5*HZ)
#else
#define SCSI_TIMEOUT (2*HZ)
#endif

/*
 * Used for debugging the new queueing code.  We want to make sure
 * that the lock state is consistent with design.  Only do this in
 * the user space simulator.
 */
#define ASSERT_LOCK(_LOCK, _COUNT)

#if defined(CONFIG_SMP) && defined(CONFIG_USER_DEBUG)
#undef ASSERT_LOCK
#define ASSERT_LOCK(_LOCK,_COUNT)       \
        { if( (_LOCK)->lock != _COUNT )   \
                panic("Lock count inconsistent %s %d\n", __FILE__, __LINE__); \
                                                                                       }
#endif

/*
 *  Use these to separate status msg and our bytes
 *
 *  These are set by:
 *
 *      status byte = set from target device
 *      msg_byte    = return status from host adapter itself.
 *      host_byte   = set by low-level driver to indicate status.
 *      driver_byte = set by mid-level.
 */
#define status_byte(result) (((result) >> 1) & 0x1f)
#define msg_byte(result)    (((result) >> 8) & 0xff)
#define host_byte(result)   (((result) >> 16) & 0xff)
#define driver_byte(result) (((result) >> 24) & 0xff)
#define suggestion(result)  (driver_byte(result) & SUGGEST_MASK)

#define sense_class(sense)  (((sense) >> 4) & 0x7)
#define sense_error(sense)  ((sense) & 0xf)
#define sense_valid(sense)  ((sense) & 0x80);

#define NEEDS_RETRY     0x2001
#define SUCCESS         0x2002
#define FAILED          0x2003
#define QUEUED          0x2004
#define SOFT_ERROR      0x2005
#define ADD_TO_MLQUEUE  0x2006

/*
 * These are the values that scsi_cmd->state can take.
 */
#define SCSI_STATE_TIMEOUT         0x1000
#define SCSI_STATE_FINISHED        0x1001
#define SCSI_STATE_FAILED          0x1002
#define SCSI_STATE_QUEUED          0x1003
#define SCSI_STATE_UNUSED          0x1006
#define SCSI_STATE_DISCONNECTING   0x1008
#define SCSI_STATE_INITIALIZING    0x1009
#define SCSI_STATE_BHQUEUE         0x100a
#define SCSI_STATE_MLQUEUE         0x100b

/*
 * These are the values that the owner field can take.
 * They are used as an indication of who the command belongs to.
 */
#define SCSI_OWNER_HIGHLEVEL      0x100
#define SCSI_OWNER_MIDLEVEL       0x101
#define SCSI_OWNER_LOWLEVEL       0x102
#define SCSI_OWNER_ERROR_HANDLER  0x103
#define SCSI_OWNER_BH_HANDLER     0x104
#define SCSI_OWNER_NOBODY         0x105

#define IDENTIFY_BASE       0x80
#define IDENTIFY(can_disconnect, lun)   (IDENTIFY_BASE |\
		     ((can_disconnect) ?  0x40 : 0) |\
		     ((lun) & 0x07))


/*
 * This defines the scsi logging feature.  It is a means by which the
 * user can select how much information they get about various goings on,
 * and it can be really useful for fault tracing.  The logging word is divided
 * into 8 nibbles, each of which describes a loglevel.  The division of things
 * is somewhat arbitrary, and the division of the word could be changed if it
 * were really needed for any reason.  The numbers below are the only place where these
 * are specified.  For a first go-around, 3 bits is more than enough, since this
 * gives 8 levels of logging (really 7, since 0 is always off).  Cutting to 2 bits
 * might be wise at some point.
 */

#define SCSI_LOG_ERROR_SHIFT              0
#define SCSI_LOG_TIMEOUT_SHIFT            3
#define SCSI_LOG_SCAN_SHIFT               6
#define SCSI_LOG_MLQUEUE_SHIFT            9
#define SCSI_LOG_MLCOMPLETE_SHIFT         12
#define SCSI_LOG_LLQUEUE_SHIFT            15
#define SCSI_LOG_LLCOMPLETE_SHIFT         18
#define SCSI_LOG_HLQUEUE_SHIFT            21
#define SCSI_LOG_HLCOMPLETE_SHIFT         24
#define SCSI_LOG_IOCTL_SHIFT              27

#define SCSI_LOG_ERROR_BITS               3
#define SCSI_LOG_TIMEOUT_BITS             3
#define SCSI_LOG_SCAN_BITS                3
#define SCSI_LOG_MLQUEUE_BITS             3
#define SCSI_LOG_MLCOMPLETE_BITS          3
#define SCSI_LOG_LLQUEUE_BITS             3
#define SCSI_LOG_LLCOMPLETE_BITS          3
#define SCSI_LOG_HLQUEUE_BITS             3
#define SCSI_LOG_HLCOMPLETE_BITS          3
#define SCSI_LOG_IOCTL_BITS               3

#ifdef CONFIG_SCSI_LOGGING

#define SCSI_CHECK_LOGGING(SHIFT, BITS, LEVEL, CMD)     \
{                                                       \
        unsigned int mask;                              \
                                                        \
        mask = (1 << (BITS)) - 1;                       \
        if( ((scsi_logging_level >> (SHIFT)) & mask) > (LEVEL) ) \
        {                                               \
                (CMD);                                  \
        }						\
}

#define SCSI_SET_LOGGING(SHIFT, BITS, LEVEL)            \
{                                                       \
        unsigned int mask;                              \
                                                        \
        mask = ((1 << (BITS)) - 1) << SHIFT;            \
        scsi_logging_level = ((scsi_logging_level & ~mask) \
                              | ((LEVEL << SHIFT) & mask));     \
}



#else

/*
 * With no logging enabled, stub these out so they don't do anything.
 */
#define SCSI_SET_LOGGING(SHIFT, BITS, LEVEL)

#define SCSI_CHECK_LOGGING(SHIFT, BITS, LEVEL, CMD)
#endif

/*
 * These are the macros that are actually used throughout the code to
 * log events.  If logging isn't enabled, they are no-ops and will be
 * completely absent from the user's code.
 *
 * The 'set' versions of the macros are really intended to only be called
 * from the /proc filesystem, and in production kernels this will be about
 * all that is ever used.  It could be useful in a debugging environment to
 * bump the logging level when certain strange events are detected, however.
 */
#define SCSI_LOG_ERROR_RECOVERY(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_ERROR_SHIFT, SCSI_LOG_ERROR_BITS, LEVEL,CMD);
#define SCSI_LOG_TIMEOUT(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_TIMEOUT_SHIFT, SCSI_LOG_TIMEOUT_BITS, LEVEL,CMD);
#define SCSI_LOG_SCAN_BUS(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_SCAN_SHIFT, SCSI_LOG_SCAN_BITS, LEVEL,CMD);
#define SCSI_LOG_MLQUEUE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_MLQUEUE_SHIFT, SCSI_LOG_MLQUEUE_BITS, LEVEL,CMD);
#define SCSI_LOG_MLCOMPLETE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_MLCOMPLETE_SHIFT, SCSI_LOG_MLCOMPLETE_BITS, LEVEL,CMD);
#define SCSI_LOG_LLQUEUE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_LLQUEUE_SHIFT, SCSI_LOG_LLQUEUE_BITS, LEVEL,CMD);
#define SCSI_LOG_LLCOMPLETE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_LLCOMPLETE_SHIFT, SCSI_LOG_LLCOMPLETE_BITS, LEVEL,CMD);
#define SCSI_LOG_HLQUEUE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_HLQUEUE_SHIFT, SCSI_LOG_HLQUEUE_BITS, LEVEL,CMD);
#define SCSI_LOG_HLCOMPLETE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_HLCOMPLETE_SHIFT, SCSI_LOG_HLCOMPLETE_BITS, LEVEL,CMD);
#define SCSI_LOG_IOCTL(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_IOCTL_SHIFT, SCSI_LOG_IOCTL_BITS, LEVEL,CMD);


#define SCSI_SET_ERROR_RECOVERY_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_ERROR_SHIFT, SCSI_LOG_ERROR_BITS, LEVEL);
#define SCSI_SET_TIMEOUT_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_TIMEOUT_SHIFT, SCSI_LOG_TIMEOUT_BITS, LEVEL);
#define SCSI_SET_SCAN_BUS_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_SCAN_SHIFT, SCSI_LOG_SCAN_BITS, LEVEL);
#define SCSI_SET_MLQUEUE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_MLQUEUE_SHIFT, SCSI_LOG_MLQUEUE_BITS, LEVEL);
#define SCSI_SET_MLCOMPLETE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_MLCOMPLETE_SHIFT, SCSI_LOG_MLCOMPLETE_BITS, LEVEL);
#define SCSI_SET_LLQUEUE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_LLQUEUE_SHIFT, SCSI_LOG_LLQUEUE_BITS, LEVEL);
#define SCSI_SET_LLCOMPLETE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_LLCOMPLETE_SHIFT, SCSI_LOG_LLCOMPLETE_BITS, LEVEL);
#define SCSI_SET_HLQUEUE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_HLQUEUE_SHIFT, SCSI_LOG_HLQUEUE_BITS, LEVEL);
#define SCSI_SET_HLCOMPLETE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_HLCOMPLETE_SHIFT, SCSI_LOG_HLCOMPLETE_BITS, LEVEL);
#define SCSI_SET_IOCTL_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_IOCTL_SHIFT, SCSI_LOG_IOCTL_BITS, LEVEL);

/* host byte codes */
#define DID_OK          0x00	/* NO error                                */
#define DID_NO_CONNECT  0x01	/* Couldn't connect before timeout period  */
#define DID_BUS_BUSY    0x02	/* BUS stayed busy through time out period */
#define DID_TIME_OUT    0x03	/* TIMED OUT for other reason              */
#define DID_BAD_TARGET  0x04	/* BAD target.                             */
#define DID_ABORT       0x05	/* Told to abort for some other reason     */
#define DID_PARITY      0x06	/* Parity error                            */
#define DID_ERROR       0x07	/* Internal error                          */
#define DID_RESET       0x08	/* Reset by somebody.                      */
#define DID_BAD_INTR    0x09	/* Got an interrupt we weren't expecting.  */
#define DID_PASSTHROUGH 0x0a	/* Force command past mid-layer            */
#define DID_SOFT_ERROR  0x0b	/* The low level driver just wish a retry  */
#define DRIVER_OK       0x00	/* Driver status                           */

/*
 *  These indicate the error that occurred, and what is available.
 */

#define DRIVER_BUSY         0x01
#define DRIVER_SOFT         0x02
#define DRIVER_MEDIA        0x03
#define DRIVER_ERROR        0x04

#define DRIVER_INVALID      0x05
#define DRIVER_TIMEOUT      0x06
#define DRIVER_HARD         0x07
#define DRIVER_SENSE	    0x08

#define SUGGEST_RETRY       0x10
#define SUGGEST_ABORT       0x20
#define SUGGEST_REMAP       0x30
#define SUGGEST_DIE         0x40
#define SUGGEST_SENSE       0x80
#define SUGGEST_IS_OK       0xff

#define DRIVER_MASK         0x0f
#define SUGGEST_MASK        0xf0

#define MAX_COMMAND_SIZE    16
#define SCSI_SENSE_BUFFERSIZE   64

/*
 *  SCSI command sets
 */

#define SCSI_UNKNOWN    0
#define SCSI_1          1
#define SCSI_1_CCS      2
#define SCSI_2          3
#define SCSI_3          4

/*
 *  Every SCSI command starts with a one byte OP-code.
 *  The next byte's high three bits are the LUN of the
 *  device.  Any multi-byte quantities are stored high byte
 *  first, and may have a 5 bit MSB in the same byte
 *  as the LUN.
 */

/*
 *  As the scsi do command functions are intelligent, and may need to
 *  redo a command, we need to keep track of the last command
 *  executed on each one.
 */

#define WAS_RESET       0x01
#define WAS_TIMEDOUT    0x02
#define WAS_SENSE       0x04
#define IS_RESETTING    0x08
#define IS_ABORTING     0x10
#define ASKED_FOR_SENSE 0x20
#define SYNC_RESET      0x40

/*
 * This specifies "machine infinity" for host templates which don't
 * limit the transfer size.  Note this limit represents an absolute
 * maximum, and may be over the transfer limits allowed for individual
 * devices (e.g. 256 for SCSI-1)
 */
#define SCSI_DEFAULT_MAX_SECTORS	1024

/*
 * This is the crap from the old error handling code.  We have it in a special
 * place so that we can more easily delete it later on.
 */
#include "scsi_obsolete.h"

/*
 * Forward-declaration of structs for prototypes.
 */
struct Scsi_Host;
struct scatterlist;

/*
 * Add some typedefs so that we can prototyope a bunch of the functions.
 */
typedef struct scsi_device Scsi_Device;
typedef struct scsi_cmnd Scsi_Cmnd;
typedef struct scsi_request Scsi_Request;

#define SCSI_CMND_MAGIC 0xE25C23A5
#define SCSI_REQ_MAGIC  0x75F6D354

/*
 * Here is where we prototype most of the mid-layer.
 */

extern unsigned int scsi_logging_level;		/* What do we log? */

/*
 * These are the error handling functions defined in scsi_error.c
 */
extern void scsi_times_out(Scsi_Cmnd * SCpnt);
extern void scsi_add_timer(Scsi_Cmnd * SCset, int timeout,
			   void (*complete) (Scsi_Cmnd *));
extern int scsi_delete_timer(Scsi_Cmnd * SCset);
extern void scsi_error_handler(void *host);
extern int scsi_decide_disposition(Scsi_Cmnd * SCpnt);
extern int scsi_block_when_processing_errors(Scsi_Device *);
extern void scsi_sleep(int);

/*
 * Prototypes for functions in scsicam.c
 */
extern int  scsi_partsize(unsigned char *buf, unsigned long capacity,
                    unsigned int *cyls, unsigned int *hds,
                    unsigned int *secs);

/*
 * Prototypes for functions in scsi_lib.c
 */
extern int scsi_maybe_unblock_host(Scsi_Device * SDpnt);
extern void scsi_setup_cmd_retry(Scsi_Cmnd *SCpnt);
extern void scsi_io_completion(Scsi_Cmnd * SCpnt, int good_sectors,
			       int block_sectors);
extern int scsi_queue_insert(struct scsi_cmnd *cmd, int reason);
extern void scsi_queue_next_request(request_queue_t *q, struct scsi_cmnd *cmd);
extern request_queue_t *scsi_alloc_queue(struct scsi_device *sdev);
extern void scsi_free_queue(request_queue_t *q);
extern int scsi_init_queue(void);
extern void scsi_exit_queue(void);

/*
 * Prototypes for functions in scsi.c
 */
extern int scsi_dispatch_cmd(Scsi_Cmnd * SCpnt);
extern int scsi_setup_command_freelist(struct Scsi_Host *shost);
extern void scsi_destroy_command_freelist(struct Scsi_Host *shost);
extern struct scsi_cmnd *scsi_get_command(struct scsi_device *dev, int flags);
extern void scsi_put_command(struct scsi_cmnd *cmd);
extern void scsi_adjust_queue_depth(Scsi_Device *, int, int);
extern int scsi_track_queue_full(Scsi_Device *, int);
extern int scsi_slave_attach(struct scsi_device *);
extern void scsi_slave_detach(struct scsi_device *);
extern int scsi_device_get(struct scsi_device *);
extern void scsi_device_put(struct scsi_device *);
extern void scsi_set_device_offline(struct scsi_device *);
extern void scsi_done(Scsi_Cmnd * SCpnt);
extern void scsi_finish_command(Scsi_Cmnd *);
extern int scsi_retry_command(Scsi_Cmnd *);
extern int scsi_attach_device(struct scsi_device *);
extern void scsi_detach_device(struct scsi_device *);
extern void scsi_rescan_device(struct scsi_device *);
extern int scsi_get_device_flags(unsigned char *vendor, unsigned char *model);

/*
 * Newer request-based interfaces.
 */
extern Scsi_Request *scsi_allocate_request(Scsi_Device *);
extern void scsi_release_request(Scsi_Request *);
extern void scsi_wait_req(Scsi_Request *, const void *cmnd,
			  void *buffer, unsigned bufflen,
			  int timeout, int retries);

extern void scsi_do_req(Scsi_Request *, const void *cmnd,
			void *buffer, unsigned bufflen,
			void (*done) (struct scsi_cmnd *),
			int timeout, int retries);
extern int scsi_insert_special_req(Scsi_Request * SRpnt, int);
extern void scsi_init_cmd_from_req(Scsi_Cmnd *, Scsi_Request *);

/*
 * Prototypes for functions in scsi_proc.c
 */
#ifdef CONFIG_PROC_FS
extern int scsi_init_procfs(void);
extern void scsi_exit_procfs(void);

extern void scsi_proc_host_add(struct Scsi_Host *);
extern void scsi_proc_host_rm(struct Scsi_Host *);

extern struct proc_dir_entry *proc_scsi;
extern void proc_print_scsidevice(Scsi_Device * sdev, char *buffer, int *size, int len);
#else
static inline int scsi_init_procfs(void) { return 0; }
static inline void scsi_exit_procfs(void) { ; }
static inline void proc_print_scsidevice(Scsi_Device * sdev, char *buffer, int *size, int len) { ; }

static inline void scsi_proc_host_add(struct Scsi_Host *);
static inline void scsi_proc_host_rm(struct Scsi_Host *);
#endif /* CONFIG_PROC_FS */

/*
 * Prototypes for functions in scsi_scan.c
 */
extern struct scsi_device *scsi_add_device(struct Scsi_Host *,
		uint, uint, uint);
extern int scsi_remove_device(struct scsi_device *);
extern u64 scsi_calculate_bounce_limit(struct Scsi_Host *);

/*
 * Prototypes for functions in constants.c
 * Some of these used to live in constants.h
 */
extern void print_Scsi_Cmnd (Scsi_Cmnd *);
extern void print_command(unsigned char *);
extern void print_sense(const char *, Scsi_Cmnd *);
extern void print_req_sense(const char *, Scsi_Request *);
extern void print_driverbyte(int scsiresult);
extern void print_hostbyte(int scsiresult);
extern void print_status(unsigned char status);
extern int print_msg(const unsigned char *);
extern const char *scsi_sense_key_string(unsigned char);
extern const char *scsi_extd_sense_format(unsigned char, unsigned char);

/*
 * dev_info: for the black/white list in the old scsi_static_device_list
 */
struct dev_info {
	char *vendor;
	char *model;
	char *revision;	/* revision known to be bad, unused */
	unsigned flags;
};

extern struct dev_info scsi_static_device_list[];

/*
 * scsi_dev_info_list: structure to hold black/white listed devices.
 */
struct scsi_dev_info_list {
	struct list_head dev_info_list;
	char vendor[8];
	char model[16];
	unsigned flags;
	unsigned compatible; /* for use with scsi_static_device_list entries */
};
extern struct list_head scsi_dev_info_list;
extern int scsi_dev_info_list_add_str(char *);

/*
 * scsi_target: representation of a scsi target, for now, this is only
 * used for single_lun devices. If no one has active IO to the target,
 * starget_sdev_user is NULL, else it points to the active sdev.
 */
struct scsi_target {
	struct scsi_device *starget_sdev_user;
	unsigned int starget_refcnt;
};

/*
 *  The scsi_device struct contains what we know about each given scsi
 *  device.
 *
 * FIXME(eric) - One of the great regrets that I have is that I failed to
 * define these structure elements as something like sdev_foo instead of foo.
 * This would make it so much easier to grep through sources and so forth.
 * I propose that all new elements that get added to these structures follow
 * this convention.  As time goes on and as people have the stomach for it,
 * it should be possible to go back and retrofit at least some of the elements
 * here with with the prefix.
 */

struct scsi_device {
	/*
	 * This information is private to the scsi mid-layer.
	 */
	struct list_head    siblings;   /* list of all devices on this host */
	struct list_head    same_target_siblings; /* just the devices sharing same target id */
	wait_queue_head_t   scpnt_wait;	/* Used to wait if
					   device is busy */
	struct Scsi_Host *host;
	request_queue_t *request_queue;
	volatile unsigned short device_busy;	/* commands actually active on low-level */
	spinlock_t sdev_lock;           /* also the request queue_lock */
	spinlock_t list_lock;
	struct list_head cmd_list;	/* queue of in use SCSI Command structures */
	struct list_head starved_entry;
        Scsi_Cmnd *current_cmnd;	/* currently active command */
	unsigned short queue_depth;	/* How deep of a queue we want */
	unsigned short last_queue_full_depth; /* These two are used by */
	unsigned short last_queue_full_count; /* scsi_track_queue_full() */
	unsigned long last_queue_full_time;/* don't let QUEUE_FULLs on the same
					   jiffie count on our counter, they
					   could all be from the same event. */

	unsigned int id, lun, channel;

	unsigned int manufacturer;	/* Manufacturer of device, for using 
					 * vendor-specific cmd's */
	unsigned sector_size;	/* size in bytes */

	int attached;		/* # of high level drivers attached to 
				 * this */
	int access_count;	/* Count of open channels/mounts */

	void *hostdata;		/* available to low-level driver */
	devfs_handle_t de;      /* directory for the device      */
	char type;
	char scsi_level;
	unsigned char inquiry_len;	/* valid bytes in 'inquiry' */
	unsigned char * inquiry;	/* INQUIRY response data */
	char * vendor;		/* [back_compat] point into 'inquiry' ... */
	char * model;		/* ... after scan; point to static string */
	char * rev;		/* ... "nullnullnullnull" before scan */
	unsigned char current_tag;	/* current tag */
//	unsigned char sync_min_period;	/* Not less than this period */
//	unsigned char sync_max_offset;	/* Not greater than this offset */
	struct scsi_target      *sdev_target;   /* used only for single_lun */

	unsigned online:1;
	unsigned writeable:1;
	unsigned removable:1;
	unsigned random:1;
	unsigned changed:1;	/* Data invalid due to media change */
	unsigned busy:1;	/* Used to prevent races */
	unsigned lockable:1;	/* Able to prevent media removal */
	unsigned locked:1;      /* Media removal disabled */
	unsigned borken:1;	/* Tell the Seagate driver to be 
				 * painfully slow on this device */
	unsigned disconnect:1;	/* can disconnect */
	unsigned soft_reset:1;	/* Uses soft reset option */
	unsigned sdtr:1;	/* Device supports SDTR messages */
	unsigned wdtr:1;	/* Device supports WDTR messages */
	unsigned ppr:1;		/* Device supports PPR messages */
	unsigned tagged_supported:1;	/* Supports SCSI-II tagged queuing */
	unsigned tagged_queue:1;/* This is going away!!!!  Look at simple_tags
				   instead!!!  Please fix your driver now!! */
	unsigned simple_tags:1;	/* simple queue tag messages are enabled */
	unsigned ordered_tags:1;/* ordered queue tag messages are enabled */
	unsigned single_lun:1;	/* Indicates we should only allow I/O to
				 * one of the luns for the device at a 
				 * time. */
	unsigned was_reset:1;	/* There was a bus reset on the bus for 
				 * this device */
	unsigned expecting_cc_ua:1;	/* Expecting a CHECK_CONDITION/UNIT_ATTN
					 * because we did a bus reset. */
	unsigned ten:1;		/* support ten byte read / write */
	unsigned remap:1;	/* support remapping  */
//	unsigned sync:1;	/* Sync transfer state, managed by host */
//	unsigned wide:1;	/* WIDE transfer state, managed by host */

	unsigned int device_blocked;	/* Device returned QUEUE_FULL. */

	unsigned int max_device_blocked; /* what device_blocked counts down from  */
  	/* default value if the device doesn't override */
	#define SCSI_DEFAULT_DEVICE_BLOCKED	3


	// Flag to allow revalidate to succeed in sd_open
	int allow_revalidate;
	struct device sdev_driverfs_dev;
};
#define	to_scsi_device(d)	\
	container_of(d, struct scsi_device, sdev_driverfs_dev)


/*
 * The Scsi_Cmnd structure is used by scsi.c internally, and for communication
 * with low level drivers that support multiple outstanding commands.
 */
typedef struct scsi_pointer {
	char *ptr;		/* data pointer */
	int this_residual;	/* left in this buffer */
	struct scatterlist *buffer;	/* which buffer */
	int buffers_residual;	/* how many buffers left */

        dma_addr_t dma_handle;

	volatile int Status;
	volatile int Message;
	volatile int have_data_in;
	volatile int sent_command;
	volatile int phase;
} Scsi_Pointer;

/*
 * This is essentially a slimmed down version of Scsi_Cmnd.  The point of
 * having this is that requests that are injected into the queue as result
 * of things like ioctls and character devices shouldn't be using a
 * Scsi_Cmnd until such a time that the command is actually at the head
 * of the queue and being sent to the driver.
 */
struct scsi_request {
	int     sr_magic;
	int     sr_result;	/* Status code from lower level driver */
	unsigned char sr_sense_buffer[SCSI_SENSE_BUFFERSIZE];		/* obtained by REQUEST SENSE
						 * when CHECK CONDITION is
						 * received on original command 
						 * (auto-sense) */

	struct Scsi_Host *sr_host;
	Scsi_Device *sr_device;
	Scsi_Cmnd *sr_command;
	struct request *sr_request;	/* A copy of the command we are
				   working on */
	unsigned sr_bufflen;	/* Size of data buffer */
	void *sr_buffer;		/* Data buffer */
	int sr_allowed;
	unsigned char sr_data_direction;
	unsigned char sr_cmd_len;
	unsigned char sr_cmnd[MAX_COMMAND_SIZE];
	void (*sr_done) (struct scsi_cmnd *);	/* Mid-level done function */
	int sr_timeout_per_command;
	unsigned short sr_use_sg;	/* Number of pieces of scatter-gather */
	unsigned short sr_sglist_len;	/* size of malloc'd scatter-gather list */
	unsigned sr_underflow;	/* Return error if less than
				   this amount is transferred */
 	void * upper_private_data;	/* reserved for owner (usually upper
 					   level driver) of this request */
};

/*
 * FIXME(eric) - one of the great regrets that I have is that I failed to define
 * these structure elements as something like sc_foo instead of foo.  This would
 * make it so much easier to grep through sources and so forth.  I propose that
 * all new elements that get added to these structures follow this convention.
 * As time goes on and as people have the stomach for it, it should be possible to 
 * go back and retrofit at least some of the elements here with with the prefix.
 */
struct scsi_cmnd {
	int     sc_magic;

	struct scsi_device *device;
	unsigned short state;
	unsigned short owner;
	Scsi_Request *sc_request;
	struct scsi_cmnd *reset_chain;

	struct list_head list;  /* scsi_cmnd participates in queue lists */

	struct list_head eh_entry; /* entry for the host eh_cmd_q */
	int eh_state;		/* Used for state tracking in error handlr */
	int eh_eflags;		/* Used by error handlr */
	void (*done) (struct scsi_cmnd *);	/* Mid-level done function */
	/*
	   A SCSI Command is assigned a nonzero serial_number when internal_cmnd
	   passes it to the driver's queue command function.  The serial_number
	   is cleared when scsi_done is entered indicating that the command has
	   been completed.  If a timeout occurs, the serial number at the moment
	   of timeout is copied into serial_number_at_timeout.  By subsequently
	   comparing the serial_number and serial_number_at_timeout fields
	   during abort or reset processing, we can detect whether the command
	   has already completed.  This also detects cases where the command has
	   completed and the SCSI Command structure has already being reused
	   for another command, so that we can avoid incorrectly aborting or
	   resetting the new command.
	 */

	unsigned long serial_number;
	unsigned long serial_number_at_timeout;

	int retries;
	int allowed;
	int timeout_per_command;
	int timeout_total;
	int timeout;

	/*
	 * We handle the timeout differently if it happens when a reset, 
	 * abort, etc are in process. 
	 */
	unsigned volatile char internal_timeout;

	unsigned char cmd_len;
	unsigned char old_cmd_len;
	unsigned char sc_data_direction;
	unsigned char sc_old_data_direction;

	/* These elements define the operation we are about to perform */
	unsigned char cmnd[MAX_COMMAND_SIZE];
	unsigned request_bufflen;	/* Actual request size */

	struct timer_list eh_timeout;	/* Used to time out the command. */
	void *request_buffer;		/* Actual requested buffer */

	/* These elements define the operation we ultimately want to perform */
	unsigned char data_cmnd[MAX_COMMAND_SIZE];
	unsigned short old_use_sg;	/* We save  use_sg here when requesting
					 * sense info */
	unsigned short use_sg;	/* Number of pieces of scatter-gather */
	unsigned short sglist_len;	/* size of malloc'd scatter-gather list */
	unsigned short abort_reason;	/* If the mid-level code requests an
					 * abort, this is the reason. */
	unsigned bufflen;	/* Size of data buffer */
	void *buffer;		/* Data buffer */

	unsigned underflow;	/* Return error if less than
				   this amount is transferred */
	unsigned old_underflow;	/* save underflow here when reusing the
				 * command for error handling */

	unsigned transfersize;	/* How much we are guaranteed to
				   transfer with each SCSI transfer
				   (ie, between disconnect / 
				   reconnects.   Probably == sector
				   size */

	int resid;		/* Number of bytes requested to be
				   transferred less actual number
				   transferred (0 if not supported) */

	struct request *request;	/* The command we are
				   	   working on */

	unsigned char sense_buffer[SCSI_SENSE_BUFFERSIZE];		/* obtained by REQUEST SENSE
						 * when CHECK CONDITION is
						 * received on original command 
						 * (auto-sense) */

	unsigned flags;

	/*
	 * Used to indicate that a command which has timed out also
	 * completed normally.  Typically the completion function will
	 * do nothing but set this flag in this instance because the
	 * timeout handler is already running.
	 */
	unsigned done_late:1;

	/* Low-level done function - can be used by low-level driver to point
	 *        to completion function.  Not used by mid/upper level code. */
	void (*scsi_done) (struct scsi_cmnd *);

	/*
	 * The following fields can be written to by the host specific code. 
	 * Everything else should be left alone. 
	 */

	Scsi_Pointer SCp;	/* Scratchpad used by some host adapters */

	unsigned char *host_scribble;	/* The host adapter is allowed to
					   * call scsi_malloc and get some memory
					   * and hang it here.     The host adapter
					   * is also expected to call scsi_free
					   * to release this memory.  (The memory
					   * obtained by scsi_malloc is guaranteed
					   * to be at an address < 16Mb). */

	int result;		/* Status code from lower level driver */

	unsigned char tag;	/* SCSI-II queued command tag */
	unsigned long pid;	/* Process ID, starts at 0 */
};

/*
 *  Flag bit for the internal_timeout array
 */
#define NORMAL_TIMEOUT 0

/*
 * Definitions and prototypes used for scsi mid-level queue.
 */
#define SCSI_MLQUEUE_HOST_BUSY   0x1055
#define SCSI_MLQUEUE_DEVICE_BUSY 0x1056
#define SCSI_MLQUEUE_EH_RETRY    0x1057

/*
 * old style reset request from external source
 * (private to sg.c and scsi_error.c, supplied by scsi_obsolete.c)
 */
#define SCSI_TRY_RESET_DEVICE	1
#define SCSI_TRY_RESET_BUS	2
#define SCSI_TRY_RESET_HOST	3

extern int scsi_reset_provider(Scsi_Device *, int);

#define MSG_SIMPLE_TAG	0x20
#define MSG_HEAD_TAG	0x21
#define MSG_ORDERED_TAG	0x22

#define SCSI_NO_TAG	(-1)    /* identify no tag in use */

/**
 * scsi_activate_tcq - turn on tag command queueing
 * @SDpnt:	device to turn on TCQ for
 * @depth:	queue depth
 *
 * Notes:
 *	Eventually, I hope depth would be the maximum depth
 *	the device could cope with and the real queue depth
 *	would be adjustable from 0 to depth.
 **/
static inline void scsi_activate_tcq(Scsi_Device *SDpnt, int depth) {
        if(SDpnt->tagged_supported) {
		if(!blk_queue_tagged(SDpnt->request_queue))
			blk_queue_init_tags(SDpnt->request_queue, depth);
		scsi_adjust_queue_depth(SDpnt, MSG_ORDERED_TAG, depth);
        }
}

/**
 * scsi_deactivate_tcq - turn off tag command queueing
 * @SDpnt:	device to turn off TCQ for
 **/
static inline void scsi_deactivate_tcq(Scsi_Device *SDpnt, int depth) {
	if(blk_queue_tagged(SDpnt->request_queue))
		blk_queue_free_tags(SDpnt->request_queue);
	scsi_adjust_queue_depth(SDpnt, 0, depth);
}

/**
 * scsi_populate_tag_msg - place a tag message in a buffer
 * @SCpnt:	pointer to the Scsi_Cmnd for the tag
 * @msg:	pointer to the area to place the tag
 *
 * Notes:
 *	designed to create the correct type of tag message for the 
 *	particular request.  Returns the size of the tag message.
 *	May return 0 if TCQ is disabled for this device.
 **/
static inline int scsi_populate_tag_msg(Scsi_Cmnd *SCpnt, char *msg) {
        struct request *req = SCpnt->request;

        if(!blk_rq_tagged(req))
                return 0;

	if (req->flags & REQ_HARDBARRIER)
                *msg++ = MSG_ORDERED_TAG;
        else
                *msg++ = MSG_SIMPLE_TAG;

        *msg++ = SCpnt->request->tag;

        return 2;
}

/**
 * scsi_find_tag - find a tagged command by device
 * @SDpnt:	pointer to the ScSI device
 * @tag:	the tag number
 *
 * Notes:
 *	Only works with tags allocated by the generic blk layer.
 **/
static inline Scsi_Cmnd *scsi_find_tag(Scsi_Device *SDpnt, int tag) {

        struct request *req;

        if(tag == SCSI_NO_TAG)
                /* single command, look in space */
                return SDpnt->current_cmnd;

        req = blk_queue_find_tag(SDpnt->request_queue, tag);

        if(req == NULL)
                return NULL;

        return (Scsi_Cmnd *)req->special;
}

#define scsi_eh_eflags_chk(scp, flags) (scp->eh_eflags & flags)

#define scsi_eh_eflags_set(scp, flags) do { \
	scp->eh_eflags |= flags; \
	} while(0)

#define scsi_eh_eflags_clr(scp, flags) do { \
	scp->eh_eflags &= ~flags; \
	} while(0)

#define scsi_eh_eflags_clr_all(scp) (scp->eh_eflags = 0)

/*
 * Scsi Error Handler Flags
 */
#define SCSI_EH_CANCEL_CMD	0x0001	/* Cancel this cmd */
#define SCSI_EH_REC_TIMEOUT	0x0002	/* EH retry timed out */

#define SCSI_SENSE_VALID(scmd) ((scmd->sense_buffer[0] & 0x70) == 0x70)

extern int scsi_eh_scmd_add(struct scsi_cmnd *, int);

int scsi_set_medium_removal(Scsi_Device *dev, char state);

extern int scsi_device_register(struct scsi_device *);
extern void scsi_device_unregister(struct scsi_device *);

extern int scsi_sysfs_register(void);
extern void scsi_sysfs_unregister(void);

#endif /* _SCSI_H */
