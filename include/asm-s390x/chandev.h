/*
 *  include/asm-s390/chandev.h
 *
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 * 
 *  Generic channel device initialisation support. 
 */
#include <linux/version.h>
#include <asm/types.h>
#include <linux/netdevice.h>

/* chandev_type is a bitmask for registering & describing device types. */
typedef enum
{
	none=0x0,
	ctc=0x1,
	escon=0x2,
	lcs=0x4,
	osad=0x8,
	qeth=0x10,
	claw=0x20,
} chandev_type;

typedef enum
{
	no_category,
	network_device,
	serial_device,
} chandev_category;

/*
 * The chandev_probeinfo structure is passed to the device driver with configuration
 * info for which irq's & ports to use when attempting to probe the device.
 */
typedef struct
{
        int     read_irq;
	int     write_irq;
	u16     read_devno;
	u16     write_devno;
        s16     port_protocol_no; /* -1 don't care */
	u8      hint_port_no;   /* lcs specific */
	u8      max_port_no;    /* lcs specific */
	chandev_type chan_type;
	u8      checksum_received_ip_pkts;
	u8      use_hw_stats; /* where available e.g. lcs */
	u16     cu_type;      /* control unit type */
	u8      cu_model;     /* control unit model */
	u16     dev_type;     /* device type */
	u8      dev_model;    /* device model */
	char    *parmstr;       /* driver specific parameters added by add_parms keyword */
	/* newdevice used internally by chandev.c */
	struct  chandev_activelist *newdevice; 
	s32     devif_num; 
/* devif_num=-1 implies don't care,0 implies tr0, info used by chandev_initnetdevice */
} chandev_probeinfo;

/*
 * This is a wrapper to the machine check handler & should be used
 * instead of reqest_irq or s390_request_irq_special for anything
 * using the channel device layer.
 */
int chandev_request_irq(unsigned int   irq,
                      void           (*handler)(int, void *, struct pt_regs *),
                      unsigned long  irqflags,
                      const char    *devname,
                      void          *dev_id);

typedef enum
{
	good=0,
	not_oper,
	first_msck=not_oper,
	no_path,
	revalidate,
	gone,
	last_msck,
} chandev_msck_status;

typedef int (*chandev_probefunc)(chandev_probeinfo *probeinfo);
typedef void (*chandev_shutdownfunc)(void *device);
typedef void (*chandev_unregfunc)(void *device);
typedef void (*chandev_reoperfunc)(void *device,int msck_for_read_chan,chandev_msck_status prevstatus);



/* A driver should call chandev_register_and_probe when ready to be probed,
 * after registeration the drivers probefunction will be called asynchronously
 * when more devices become available at normal task time.
 * The shutdownfunc parameter is used so that the channel layer
 * can request a driver to close unregister itself & release its interrupts.
 * repoper func is used when a device becomes operational again after being temporarily
 * not operational the previous status is sent in the prevstatus variable.
 * This can be used in cases when the default handling isn't quite adequete
 * e.g. if a ssch is needed to reinitialize long running channel programs.
 */
int chandev_register_and_probe(chandev_probefunc probefunc,
			       chandev_shutdownfunc shutdownfunc,
			       chandev_reoperfunc reoperfunc,
			       chandev_type chan_type);

/* The chandev_unregister function is typically called when a module is being removed 
 * from the system. The shutdown parameter if TRUE calls shutdownfunc for each 
 * device instance so the driver writer doesn't have to.
 */
void chandev_unregister(chandev_probefunc probefunc,int call_shutdown);

/* chandev_initdevice should be called immeadiately before returning after */
/* a successful probe. */
int chandev_initdevice(chandev_probeinfo *probeinfo,void *dev_ptr,u8 port_no,char *devname,
chandev_category category,chandev_unregfunc unreg_dev);

/* chandev_initnetdevice registers a network device with the channel layer. 
 * It returns the device structure if successful,if dev=NULL it kmallocs it, 
 * On device initialisation failure it will kfree it under ALL curcumstances
 * i.e. if dev is not NULL on entering this routine it MUST be malloced with kmalloc. 
 * The base name is tr ( e.g. tr0 without the 0 ), for token ring eth for ethernet,
 *  ctc or escon for ctc device drivers.
 * If valid function pointers are given they will be called to setup,
 * register & unregister the device. 
 * An example of setup is eth_setup in drivers/net/net_init.c.
 * An example of init_dev is init_trdev(struct net_device *dev)
 * & an example of unregister is unregister_trdev, 
 * unregister_netdev should be used for escon & ctc
 * as there is no network unregister_ctcdev in the kernel.
*/

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
struct net_device *chandev_initnetdevice(chandev_probeinfo *probeinfo,u8 port_no,
					 struct net_device *dev,int sizeof_priv, 
					 char *basename, 
					 struct net_device *(*init_netdevfunc)
					 (struct net_device *dev, int sizeof_priv),
					 void (*unreg_netdevfunc)(struct net_device *dev));
#else
struct device *chandev_initnetdevice(chandev_probeinfo *probeinfo,u8 port_no,
				     struct device *dev,int sizeof_priv,
				     char *basename, 
				     struct device *(*init_netdevfunc)
				     (struct device *dev, int sizeof_priv),
				     void (*unreg_netdevfunc)(struct device *dev));
#endif








