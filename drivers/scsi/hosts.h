/*
 *  hosts.h Copyright (C) 1992 Drew Eckhardt
 *          Copyright (C) 1993, 1994, 1995, 1998, 1999 Eric Youngdale
 *
 *  mid to low-level SCSI driver interface header
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *	 Modified by Eric Youngdale eric@andante.org to
 *	 add scatter-gather, multiple outstanding request, and other
 *	 enhancements.
 *
 *  Further modified by Eric Youngdale to support multiple host adapters
 *  of the same type.
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 *
 *  Restructured scsi_host lists and associated functions.
 *  September 04, 2002 Mike Anderson (andmike@us.ibm.com)
 */

#ifndef _HOSTS_H
#define _HOSTS_H

#include <linux/config.h>
#include <linux/proc_fs.h>
#include <linux/types.h>

struct scsi_host_cmd_pool;


/* It is senseless to set SG_ALL any higher than this - the performance
 *  does not get any better, and it wastes memory
 */
#define SG_NONE 0
#define SG_ALL 0xff

#define DISABLE_CLUSTERING 0
#define ENABLE_CLUSTERING 1

/* The various choices mean:
 * NONE: Self evident.	Host adapter is not capable of scatter-gather.
 * ALL:	 Means that the host adapter module can do scatter-gather,
 *	 and that there is no limit to the size of the table to which
 *	 we scatter/gather data.
 * Anything else:  Indicates the maximum number of chains that can be
 *	 used in one scatter-gather request.
 */

/*
 * The Scsi_Host_Template type has all that is needed to interface with a SCSI
 * host in a device independent matter.	 There is one entry for each different
 * type of host adapter that is supported on the system.
 */

typedef struct	SHT
{
    /* Used with loadable modules so that we know when it is safe to unload */
    struct module * module;

    /* The pointer to the /proc/scsi directory entry */
    struct proc_dir_entry *proc_dir;

    /* proc-fs info function.
     * Can be used to export driver statistics and other infos to the world
     * outside the kernel ie. userspace and it also provides an interface
     * to feed the driver with information. Check eata_dma_proc.c for reference
     */
    int (*proc_info)(char *, char **, off_t, int, int, int);

    /*
     * The name pointer is a pointer to the name of the SCSI
     * device detected.
     */
    const char *name;

    /*
     * The detect function shall return non zero on detection,
     * indicating the number of host adapters of this particular
     * type were found.	 It should also
     * initialize all data necessary for this particular
     * SCSI driver.  It is passed the host number, so this host
     * knows where the first entry is in the scsi_hosts[] array.
     *
     * Note that the detect routine MUST not call any of the mid level
     * functions to queue commands because things are not guaranteed
     * to be set up yet.  The detect routine can send commands to
     * the host adapter as long as the program control will not be
     * passed to scsi.c in the processing of the command.  Note
     * especially that scsi_malloc/scsi_free must not be called.
     */
    int (* detect)(struct SHT *);

    /* Used with loadable modules to unload the host structures.  Note:
     * there is a default action built into the modules code which may
     * be sufficient for most host adapters.  Thus you may not have to supply
     * this at all.
     */
    int (*release)(struct Scsi_Host *);

    /*
     * The info function will return whatever useful
     * information the developer sees fit.  If not provided, then
     * the name field will be used instead.
     */
    const char *(* info)(struct Scsi_Host *);

    /*
     * ioctl interface
     */
    int (*ioctl)(Scsi_Device *dev, int cmd, void *arg);

    /*
     * The command function takes a target, a command (this is a SCSI
     * command formatted as per the SCSI spec, nothing strange), a
     * data buffer pointer, and data buffer length pointer.  The return
     * is a status int, bit fielded as follows :
     * Byte What
     * 0    SCSI status code
     * 1    SCSI 1 byte message
     * 2    host error return.
     * 3    mid level error return
     */
    int (* command)(Scsi_Cmnd *);

    /*
     * The QueueCommand function works in a similar manner
     * to the command function.	 It takes an additional parameter,
     * void (* done)(int host, int code) which is passed the host
     * # and exit result when the command is complete.
     * Host number is the POSITION IN THE hosts array of THIS
     * host adapter.
     *
     * if queuecommand returns 0, then the HBA has accepted the
     * command.  The done() function must be called on the command
     * when the driver has finished with it. (you may call done on the
     * command before queuecommand returns, but in this case you
     * *must* return 0 from queuecommand).
     *
     * queuecommand may also reject the command, in which case it may
     * not touch the command and must not call done() for it.
     *
     * There are two possible rejection returns:
     *
     *   SCSI_MLQUEUE_DEVICE_BUSY: Block this device temporarily, but
     *   allow commands to other devices serviced by this host.
     *
     *   SCSI_MLQUEUE_HOST_BUSY: Block all devices served by this
     *   host temporarily.
     *
     *   for compatibility, any other non-zero return is treated the
     *   same as SCSI_MLQUEUE_HOST_BUSY.
     *
     *   NOTE: "temporarily" means either until the next command for
     *   this device/host completes, or a period of time determined by
     *   I/O pressure in the system if there are no other outstanding
     *   commands.
     * */
    int (* queuecommand)(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));

    /*
     * This is an error handling strategy routine.  You don't need to
     * define one of these if you don't want to - there is a default
     * routine that is present that should work in most cases.  For those
     * driver authors that have the inclination and ability to write their
     * own strategy routine, this is where it is specified.  Note - the
     * strategy routine is *ALWAYS* run in the context of the kernel eh
     * thread.  Thus you are guaranteed to *NOT* be in an interrupt handler
     * when you execute this, and you are also guaranteed to *NOT* have any
     * other commands being queued while you are in the strategy routine.
     * When you return from this function, operations return to normal.
     *
     * See scsi_error.c scsi_unjam_host for additional comments about what
     * this function should and should not be attempting to do.
     */
     int (*eh_strategy_handler)(struct Scsi_Host *);
     int (*eh_abort_handler)(Scsi_Cmnd *);
     int (*eh_device_reset_handler)(Scsi_Cmnd *);
     int (*eh_bus_reset_handler)(Scsi_Cmnd *);
     int (*eh_host_reset_handler)(Scsi_Cmnd *);

    /*
     * Old EH handlers, no longer used. Make them warn the user of old
     * drivers by using a wrong type
     */
    int (*abort)(int);
    int (*reset)(int,int);

    /*
     * slave_alloc()  -  Optional
     * 
     * Before the mid layer attempts to scan for a new device where none
     * currently exists, it will call this entry in your driver.  Should
     * your driver need to allocate any structs or perform any other init
     * items in order to send commands to a currently unused target/lun
     * combo, then this is where you can perform those allocations.  This
     * is specifically so that drivers won't have to perform any kind of
     * "is this a new device" checks in their queuecommand routine,
     * thereby making the hot path a bit quicker.
     *
     * Return values: 0 on success, non-0 on failure
     *
     * Deallocation:  If we didn't find any devices at this ID, you will
     * get an immediate call to slave_destroy().  If we find something here
     * then you will get a call to slave_configure(), then the device will be
     * used for however long it is kept around, then when the device is
     * removed from the system (or * possibly at reboot time), you will
     * then get a call to slave_detach().  This is assuming you implement
     * slave_configure and slave_destroy.  However, if you allocate memory
     * and hang it off the device struct, then you must implement the
     * slave_destroy() routine at a minimum in order to avoid leaking memory
     * each time a device is tore down.
     */
    int (* slave_alloc)(Scsi_Device *);

    /*
     * slave_configure()  -  Optional
     * 
     * Once the device has responded to an INQUIRY and we know the device
     * is online, we call into the low level driver with the Scsi_Device *
     * If the low level device driver implements this function, it *must*
     * perform the task of setting the queue depth on the device.  All other
     * tasks are optional and depend on what the driver supports and various
     * implementation details.
     * 
     * Things currently recommended to be handled at this time include:
     *
     * 1.  Setting the device queue depth.  Proper setting of this is
     *     described in the comments for scsi_adjust_queue_depth.
     * 2.  Determining if the device supports the various synchronous
     *     negotiation protocols.  The device struct will already have
     *     responded to INQUIRY and the results of the standard items
     *     will have been shoved into the various device flag bits, eg.
     *     device->sdtr will be true if the device supports SDTR messages.
     * 3.  Allocating command structs that the device will need.
     * 4.  Setting the default timeout on this device (if needed).
     * 5.  Anything else the low level driver might want to do on a device
     *     specific setup basis...
     * 6.  Return 0 on success, non-0 on error.  The device will be marked
     *     as offline on error so that no access will occur.  If you return
     *     non-0, your slave_detach routine will never get called for this
     *     device, so don't leave any loose memory hanging around, clean
     *     up after yourself before returning non-0
     */
    int (* slave_configure)(Scsi_Device *);

    /*
     * slave_destroy()  -  Optional
     *
     * Immediately prior to deallocating the device and after all activity
     * has ceased the mid layer calls this point so that the low level driver
     * may completely detach itself from the scsi device and vice versa.
     * The low level driver is responsible for freeing any memory it allocated
     * in the slave_alloc or slave_configure calls. 
     */
    void (* slave_destroy)(Scsi_Device *);

    /*
     * This function determines the bios parameters for a given
     * harddisk.  These tend to be numbers that are made up by
     * the host adapter.  Parameters:
     * size, device, list (heads, sectors, cylinders)
     */
    int (* bios_param)(struct scsi_device *, struct block_device *,
		    sector_t, int []);

    /*
     * This determines if we will use a non-interrupt driven
     * or an interrupt driven scheme,  It is set to the maximum number
     * of simultaneous commands a given host adapter will accept.
     */
    int can_queue;

    /*
     * In many instances, especially where disconnect / reconnect are
     * supported, our host also has an ID on the SCSI bus.  If this is
     * the case, then it must be reserved.  Please set this_id to -1 if
     * your setup is in single initiator mode, and the host lacks an
     * ID.
     */
    int this_id;

    /*
     * This determines the degree to which the host adapter is capable
     * of scatter-gather.
     */
    short unsigned int sg_tablesize;

    /*
     * if the host adapter has limitations beside segment count
     */
    short unsigned int max_sectors;

    /*
     * True if this host adapter can make good use of linked commands.
     * This will allow more than one command to be queued to a given
     * unit on a given host.  Set this to the maximum number of command
     * blocks to be provided for each device.  Set this to 1 for one
     * command block per lun, 2 for two, etc.  Do not set this to 0.
     * You should make sure that the host adapter will do the right thing
     * before you try setting this above 1.
     */
    short cmd_per_lun;

    /*
     * present contains counter indicating how many boards of this
     * type were found when we did the scan.
     */
    unsigned char present;

    /*
     * true if this host adapter uses unchecked DMA onto an ISA bus.
     */
    unsigned unchecked_isa_dma:1;

    /*
     * true if this host adapter can make good use of clustering.
     * I originally thought that if the tablesize was large that it
     * was a waste of CPU cycles to prepare a cluster list, but
     * it works out that the Buslogic is faster if you use a smaller
     * number of segments (i.e. use clustering).  I guess it is
     * inefficient.
     */
    unsigned use_clustering:1;

    /*
     * True for emulated SCSI host adapters (e.g. ATAPI)
     */
    unsigned emulated:1;

    unsigned highmem_io:1;

    /* 
     * True if the driver wishes to use the generic block layer
     * tag queueing functions
     */
    unsigned use_blk_tcq:1;

    /*
     * Name of proc directory
     */
    char *proc_name;

    /*
     * countdown for host blocking with no commands outstanding
     */
    unsigned int max_host_blocked;

    /*
     * Default value for the blocking.  If the queue is empty, host_blocked
     * counts down in the request_fn until it restarts host operations as
     * zero is reached.  
     *
     * FIXME: This should probably be a value in the template */
    #define SCSI_DEFAULT_HOST_BLOCKED	7

} Scsi_Host_Template;

/*
 * The scsi_hosts array is the array containing the data for all
 * possible <supported> scsi hosts.   This is similar to the
 * Scsi_Host_Template, except that we have one entry for each
 * actual physical host adapter on the system, stored as a linked
 * list.  Note that if there are 2 aha1542 boards, then there will
 * be two Scsi_Host entries, but only 1 Scsi_Host_Template entry.
 */

struct Scsi_Host
{
/* private: */
    /*
     * This information is private to the scsi mid-layer.  Wrapping it in a
     * struct private is a way of marking it in a sort of C++ type of way.
     */
    struct list_head      sh_list;
    struct list_head	  my_devices;

    struct scsi_host_cmd_pool *cmd_pool;
    spinlock_t            free_list_lock;
    struct list_head      free_list;   /* backup store of cmd structs */
    struct list_head      starved_list;

    spinlock_t		  default_lock;
    spinlock_t		  *host_lock;

    struct list_head	eh_cmd_q;
    struct task_struct    * ehandler;  /* Error recovery thread. */
    struct semaphore      * eh_wait;   /* The error recovery thread waits on
                                          this. */
    struct completion     * eh_notify; /* wait for eh to begin or end */
    struct semaphore      * eh_action; /* Wait for specific actions on the
                                          host. */
    unsigned int            eh_active:1; /* Indicates the eh thread is awake and active if
                                          this is true. */
    unsigned int            eh_kill:1; /* set when killing the eh thread */
    wait_queue_head_t       host_wait;
    Scsi_Host_Template    * hostt;
    volatile unsigned short host_busy;   /* commands actually active on low-level */
    volatile unsigned short host_failed; /* commands that failed. */
    
/* public: */
    unsigned short host_no;  /* Used for IOCTL_GET_IDLUN, /proc/scsi et al. */
    int resetting; /* if set, it means that last_reset is a valid value */
    unsigned long last_reset;

    /*
     *	These three parameters can be used to allow for wide scsi,
     *	and for host adapters that support multiple busses
     *	The first two should be set to 1 more than the actual max id
     *	or lun (i.e. 8 for normal systems).
     */
    unsigned int max_id;
    unsigned int max_lun;
    unsigned int max_channel;

    /* These parameters should be set by the detect routine */
    unsigned long base;
    unsigned long io_port;
    unsigned char n_io_port;
    unsigned char dma_channel;
    unsigned int  irq;

    /*
     * This is a unique identifier that must be assigned so that we
     * have some way of identifying each detected host adapter properly
     * and uniquely.  For hosts that do not support more than one card
     * in the system at one time, this does not need to be set.  It is
     * initialized to 0 in scsi_register.
     */
    unsigned int unique_id;

    /*
     * The rest can be copied from the template, or specifically
     * initialized, as required.
     */

    /*
     * The maximum length of SCSI commands that this host can accept.
     * Probably 12 for most host adapters, but could be 16 for others.
     * For drivers that don't set this field, a value of 12 is
     * assumed.  I am leaving this as a number rather than a bit
     * because you never know what subsequent SCSI standards might do
     * (i.e. could there be a 20 byte or a 24-byte command a few years
     * down the road?).  
     */
    unsigned char max_cmd_len;

    int this_id;
    int can_queue;
    short cmd_per_lun;
    short unsigned int sg_tablesize;
    short unsigned int max_sectors;

    unsigned in_recovery:1;
    unsigned unchecked_isa_dma:1;
    unsigned use_clustering:1;
    unsigned highmem_io:1;
    unsigned use_blk_tcq:1;

    /*
     * Host has requested that no further requests come through for the
     * time being.
     */
    unsigned host_self_blocked:1;
    
    /*
     * Host uses correct SCSI ordering not PC ordering. The bit is
     * set for the minority of drivers whose authors actually read the spec ;)
     */
    unsigned reverse_ordering:1;

    /*
     * Host has rejected a command because it was busy.
     */
    unsigned int host_blocked;

    /*
     * Value host_blocked counts down from
     */
    unsigned int max_host_blocked;

    /* 
     * Support for driverfs filesystem
     */
    struct device *host_gendev;

    /*
     * We should ensure that this is aligned, both for better performance
     * and also because some compilers (m68k) don't automatically force
     * alignment to a long boundary.
     */
    unsigned long hostdata[0]  /* Used for storage of host specific stuff */
        __attribute__ ((aligned (sizeof(unsigned long))));
};

#define	to_scsi_host(d)	d->class_data
	
/*
 * These two functions are used to allocate and free a pseudo device
 * which will connect to the host adapter itself rather than any
 * physical device.  You must deallocate when you are done with the
 * thing.  This physical pseudo-device isn't real and won't be available
 * from any high-level drivers.
 */
extern void scsi_free_host_dev(Scsi_Device *);
extern Scsi_Device * scsi_get_host_dev(struct Scsi_Host *);

extern void scsi_unblock_requests(struct Scsi_Host *);
extern void scsi_block_requests(struct Scsi_Host *);
extern void scsi_report_bus_reset(struct Scsi_Host *, int);

static inline void scsi_assign_lock(struct Scsi_Host *shost, spinlock_t *lock)
{
	shost->host_lock = lock;
}

static inline void scsi_set_device(struct Scsi_Host *shost,
                                   struct device *dev)
{
        shost->host_gendev = dev;
}

static inline struct device *scsi_get_device(struct Scsi_Host *shost)
{
        return shost->host_gendev;
}

struct Scsi_Device_Template
{
    struct list_head list;
    const char * name;
    struct module * module;	  /* Used for loadable modules */
    unsigned char scsi_type;
    int (*attach)(Scsi_Device *); /* Attach devices to arrays */
    void (*detach)(Scsi_Device *);
    int (*init_command)(Scsi_Cmnd *);     /* Used by new queueing code. 
                                           Selects command for blkdevs */
    void (*rescan)(Scsi_Device *);
    struct device_driver scsi_driverfs_driver;
};

/*
 * Highlevel driver registration/unregistration.
 */
extern int scsi_register_device(struct Scsi_Device_Template *);
extern int scsi_unregister_device(struct Scsi_Device_Template *);

/*
 * HBA allocation/freeing.
 */
extern struct Scsi_Host * scsi_register(Scsi_Host_Template *, int);
extern void scsi_unregister(struct Scsi_Host *);

/*
 * HBA registration/unregistration.
 */
extern int scsi_add_host(struct Scsi_Host *, struct device *);
extern int scsi_remove_host(struct Scsi_Host *);

/*
 * Legacy HBA template registration/unregistration.
 */
extern int scsi_register_host(Scsi_Host_Template *);
extern int scsi_unregister_host(Scsi_Host_Template *);

extern struct Scsi_Host *scsi_host_hn_get(unsigned short);
extern void scsi_host_put(struct Scsi_Host *);

/**
 * scsi_find_device - find a device given the host
 * @shost:	SCSI host pointer
 * @channel:	SCSI channel (zero if only one channel)
 * @pun:	SCSI target number (physical unit number)
 * @lun:	SCSI Logical Unit Number
 **/
static inline Scsi_Device *scsi_find_device(struct Scsi_Host *shost,
                                            int channel, int pun, int lun) {
        Scsi_Device *sdev;

	list_for_each_entry (sdev, &shost->my_devices, siblings)
                if (sdev->channel == channel && sdev->id == pun
                   && sdev->lun ==lun)
                        return sdev;
        return NULL;
}

/*
 * sysfs support
 */
extern int scsi_upper_driver_register(struct Scsi_Device_Template *);
extern void scsi_upper_driver_unregister(struct Scsi_Device_Template *);

extern struct device_class shost_devclass;

#endif
