#ifndef _SCSI_SCSI_DEVICE_H
#define _SCSI_SCSI_DEVICE_H

#include <linux/device.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct request_queue;
struct scsi_cmnd;
struct scsi_mode_data;


struct scsi_device {
	struct class_device	sdev_classdev;

	struct list_head    siblings;   /* list of all devices on this host */
	struct list_head    same_target_siblings; /* just the devices sharing same target id */
	struct Scsi_Host *host;
	struct request_queue *request_queue;
	volatile unsigned short device_busy;	/* commands actually active on low-level */
	spinlock_t sdev_lock;           /* also the request queue_lock */
	spinlock_t list_lock;
	struct list_head cmd_list;	/* queue of in use SCSI Command structures */
	struct list_head starved_entry;
	struct scsi_cmnd *current_cmnd;	/* currently active command */
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

	int access_count;	/* Count of open channels/mounts */

	void *hostdata;		/* available to low-level driver */
	char devfs_name[256];	/* devfs junk */
	char type;
	char scsi_level;
	unsigned char inquiry_len;	/* valid bytes in 'inquiry' */
	unsigned char * inquiry;	/* INQUIRY response data */
	char * vendor;		/* [back_compat] point into 'inquiry' ... */
	char * model;		/* ... after scan; point to static string */
	char * rev;		/* ... "nullnullnullnull" before scan */
	unsigned char current_tag;	/* current tag */
	struct scsi_target      *sdev_target;   /* used only for single_lun */

	unsigned online:1;

	unsigned writeable:1;
	unsigned removable:1;
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
	unsigned expecting_cc_ua:1; /* Expecting a CHECK_CONDITION/UNIT_ATTN
				     * because we did a bus reset. */
	unsigned use_10_for_rw:1; /* first try 10-byte read / write */
	unsigned use_10_for_ms:1; /* first try 10-byte mode sense/select */
	unsigned no_start_on_add:1;	/* do not issue start on add */

	unsigned int device_blocked;	/* Device returned QUEUE_FULL. */

	unsigned int max_device_blocked; /* what device_blocked counts down from  */
#define SCSI_DEFAULT_DEVICE_BLOCKED	3

	struct device sdev_driverfs_dev;
};
#define	to_scsi_device(d)	\
	container_of(d, struct scsi_device, sdev_driverfs_dev)

extern struct scsi_device *scsi_add_device(struct Scsi_Host *,
		uint, uint, uint);
extern int scsi_remove_device(struct scsi_device *);
extern void scsi_set_device_offline(struct scsi_device *);

extern int scsi_device_get(struct scsi_device *);
extern void scsi_device_put(struct scsi_device *);

extern void scsi_adjust_queue_depth(struct scsi_device *, int, int);
extern int scsi_track_queue_full(struct scsi_device *, int);

extern int scsi_set_medium_removal(struct scsi_device *, char);

extern int scsi_mode_sense(struct scsi_device *sdev, int dbd, int modepage,
			   unsigned char *buffer, int len, int timeout,
			   int retries, struct scsi_mode_data *data);

#endif /* _SCSI_SCSI_DEVICE_H */
