#ifndef _LINUX_PNP_H
#define _LINUX_PNP_H

#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/list.h>


/* Device Managemnt */

#define DEVICE_COUNT_IRQ	2
#define DEVICE_COUNT_DMA	2
#define DEVICE_COUNT_RESOURCE	12

struct pnp_resource;
struct pnp_protocol;

struct pnp_card {			/* this is for ISAPNP */ 
	struct list_head node;		/* node in list of cards */
	char name[80];
	unsigned char number;		/* card number */
	struct list_head ids;		/* stores all supported dev ids */
	struct list_head devices;	/* devices attached to the card */
	unsigned char	pnpver;		/* Plug & Play version */
	unsigned char	productver;	/* product version */
	unsigned int	serial;		/* serial number */
	unsigned char	checksum;	/* if zero - checksum passed */
	struct proc_dir_entry *procdir;	/* directory entry in /proc/bus/isapnp */
};

#define to_pnp_card(n) list_entry(n, struct pnp_card, node)

struct pnp_dev {
	char name[80];			/* device name */
	int active;			/* status of the device */
	int ro;				/* read only */
	struct list_head dev_list;	/* node in list of device's protocol */
	struct list_head global_list;
	struct list_head card_list;
	struct pnp_protocol * protocol;
	struct pnp_card *card;

	unsigned char number;		/* must be unique */
	unsigned short	regs;		/* ISAPnP: supported registers */
	struct list_head ids;		/* stores all supported dev ids */
	struct pnp_resources *res;	/* possible resource information */
	struct resource resource[DEVICE_COUNT_RESOURCE]; /* I/O and memory regions + expansion ROMs */
	struct resource dma_resource[DEVICE_COUNT_DMA];
	struct resource irq_resource[DEVICE_COUNT_IRQ];

	struct pnp_driver * driver;	/* which driver has allocated this device */
	struct	device	    dev;	/* Driver Model device interface */
	void  		  * driver_data;/* data private to the driver */
	void		  * protocol_data;
	struct proc_dir_entry *procent;	/* device entry in /proc/bus/isapnp */
};

#define global_to_pnp_dev(n) list_entry(n, struct pnp_dev, global_list)
#define card_to_pnp_dev(n) list_entry(n, struct pnp_dev, card_list)
#define protocol_to_pnp_dev(n) list_entry(n, struct pnp_dev, dev_list)
#define	to_pnp_dev(n) container_of(n, struct pnp_dev, dev)
#define pnp_for_each_dev(dev) \
	for(dev = global_to_pnp_dev(pnp_global.next); \
	dev != global_to_pnp_dev(&pnp_global); \
	dev = global_to_pnp_dev(dev->global_list.next))

struct pnp_fixup {
	char id[7];
	void (*quirk_function)(struct pnp_dev *dev);	/* fixup function */
};

/*
 * Linux Plug and Play Support
 * Copyright by Adam Belay <ambx1@neo.rr.com>
 *
 */

/* Driver Management */

struct pnp_id {
	char id[7];
	unsigned long driver_data;	/* data private to the driver */
	struct list_head id_list;	/* node in card's or device's list */
};

#define to_pnp_id(n) list_entry(n, struct pnp_id, id_list)

struct pnp_driver {
	struct list_head node;
	char *name;
	const struct pnp_id *card_id_table;
	const struct pnp_id *id_table;
	int  (*probe)  (struct pnp_dev *dev, const struct pnp_id *card_id,
		 	const struct pnp_id *dev_id);
	void (*remove) (struct pnp_dev *dev);
	struct device * (*legacy) (void);
	struct device_driver	driver;
};

#define	to_pnp_driver(drv) container_of(drv,struct pnp_driver, driver)


/* Resource Management */

#define DEV_IO(dev, index) (dev->resource[index].start)
#define DEV_MEM(dev, index) (dev->resource[index+8].start)
#define DEV_IRQ(dev, index) (dev->irq_resource[index].start)
#define DEV_DMA(dev, index) (dev->dma_resource[index].start)

#define PNP_PORT_FLAG_16BITADDR	(1<<0)
#define PNP_PORT_FLAG_FIXED		(1<<1)

struct pnp_port {
	unsigned short min;		/* min base number */
	unsigned short max;		/* max base number */
	unsigned char align;		/* align boundary */
	unsigned char size;		/* size of range */
	unsigned char flags;		/* port flags */
	unsigned char pad;		/* pad */
	struct pnp_resources *res;	/* parent */
	struct pnp_port *next;		/* next port */
};

struct pnp_irq {
	unsigned short map;		/* bitmaks for IRQ lines */
	unsigned char flags;		/* IRQ flags */
	unsigned char pad;		/* pad */
	struct pnp_resources *res;	/* parent */
	struct pnp_irq *next;		/* next IRQ */
};

struct pnp_dma {
	unsigned char map;		/* bitmask for DMA channels */
	unsigned char flags;		/* DMA flags */
	struct pnp_resources *res;	/* parent */
	struct pnp_dma *next;		/* next port */
};

struct pnp_mem {
	unsigned int min;		/* min base number */
	unsigned int max;		/* max base number */
	unsigned int align;		/* align boundary */
	unsigned int size;		/* size of range */
	unsigned char flags;		/* memory flags */
	unsigned char pad;		/* pad */
	struct pnp_resources *res;	/* parent */
	struct pnp_mem *next;		/* next memory resource */
};

struct pnp_mem32 {
	unsigned char data[17];
	struct pnp_resources *res;	/* parent */
	struct pnp_mem32 *next;		/* next 32-bit memory resource */
};

#define PNP_RES_PRIORITY_PREFERRED	0
#define PNP_RES_PRIORITY_ACCEPTABLE	1
#define PNP_RES_PRIORITY_FUNCTIONAL	2
#define PNP_RES_PRIORITY_INVALID	65535

struct pnp_resources {
	unsigned short priority;	/* priority */
	unsigned short dependent;	/* dependent resources */
	struct pnp_port *port;		/* first port */
	struct pnp_irq *irq;		/* first IRQ */
	struct pnp_dma *dma;		/* first DMA */
	struct pnp_mem *mem;		/* first memory resource */
	struct pnp_mem32 *mem32;	/* first 32-bit memory */
	struct pnp_dev *dev;		/* parent */
	struct pnp_resources *dep;	/* dependent resources */
};

#define PNP_DYNAMIC		0	/* get or set current resource */
#define PNP_STATIC		1	/* get or set resource for next boot */

struct pnp_cfg {
	struct pnp_port *port[8];
	struct pnp_irq *irq[2];
	struct pnp_dma *dma[2];
	struct pnp_mem *mem[4];
	struct pnp_dev request;
};


/* Protocol Management */

struct pnp_protocol {
	struct list_head	protocol_list;
	char			name[DEVICE_NAME_SIZE];

	/* functions */
	int (*get)(struct pnp_dev *dev);
	int (*set)(struct pnp_dev *dev, struct pnp_cfg *config, char flags);
	int (*disable)(struct pnp_dev *dev);

	/* used by pnp layer only (look but don't touch) */
	unsigned char		number;		/* protocol number*/
	struct device		dev;		/* link to driver model */
	struct list_head	devices;
};

#define to_pnp_protocol(n) list_entry(n, struct pnp_protocol, protocol_list)


#if defined(CONFIG_PNP)

/* core */
int pnp_protocol_register(struct pnp_protocol *protocol);
void pnp_protocol_unregister(struct pnp_protocol *protocol);
int pnp_init_device(struct pnp_dev *dev);
int pnp_add_device(struct pnp_dev *dev);
void pnp_remove_device(struct pnp_dev *dev);
extern struct list_head pnp_global;

/* resource */
struct pnp_resources * pnp_build_resource(struct pnp_dev *dev, int dependent);
struct pnp_resources * pnp_find_resources(struct pnp_dev *dev, int depnum);
int pnp_get_max_depnum(struct pnp_dev *dev);
int pnp_add_irq_resource(struct pnp_dev *dev, int depnum, struct pnp_irq *data);
int pnp_add_dma_resource(struct pnp_dev *dev, int depnum, struct pnp_dma *data);
int pnp_add_port_resource(struct pnp_dev *dev, int depnum, struct pnp_port *data);
int pnp_add_mem_resource(struct pnp_dev *dev, int depnum, struct pnp_mem *data);
int pnp_add_mem32_resource(struct pnp_dev *dev, int depnum, struct pnp_mem32 *data);
int pnp_activate_dev(struct pnp_dev *dev);
int pnp_disable_dev(struct pnp_dev *dev);
int pnp_raw_set_dev(struct pnp_dev *dev, int depnum, int mode);

/* driver */
int pnp_add_id(struct pnp_id *id, struct pnp_dev *dev);
int pnp_register_driver(struct pnp_driver *drv);
void pnp_unregister_driver(struct pnp_driver *drv);

/* compat */
struct pnp_card *pnp_find_card(unsigned short vendor,
				 unsigned short device,
				 struct pnp_card *from);
struct pnp_dev *pnp_find_dev(struct pnp_card *card,
				unsigned short vendor,
				unsigned short function,
				struct pnp_dev *from);


#else

/* just in case anyone decides to call these without PnP Support Enabled */
static inline int pnp_protocol_register(struct pnp_protocol *protocol) { return -ENODEV; }
static inline void pnp_protocol_unregister(struct pnp_protocol *protocol) { ; }
static inline int pnp_init_device(struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_add_device(struct pnp_dev *dev) { return -ENODEV; }
static inline void pnp_remove_device(struct pnp_dev *dev) { ; }
static inline struct pnp_resources * pnp_build_resource(struct pnp_dev *dev, int dependent) { return NULL; }
static inline struct pnp_resources * pnp_find_resources(struct pnp_dev *dev, int depnum) { return NULL; }
static inline int pnp_get_max_depnum(struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_add_irq_resource(struct pnp_dev *dev, int depnum, struct pnp_irq *data) { return -ENODEV; }
static inline int pnp_add_dma_resource(struct pnp_dev *dev, int depnum, struct pnp_irq *data) { return -ENODEV; }
static inline int pnp_add_port_resource(struct pnp_dev *dev, int depnum, struct pnp_irq *data) { return -ENODEV; }
static inline int pnp_add_mem_resource(struct pnp_dev *dev, int depnum, struct pnp_irq *data) { return -ENODEV; }
static inline int pnp_add_mem32_resource(struct pnp_dev *dev, int depnum, struct pnp_irq *data) { return -ENODEV; }
static inline int pnp_activate_dev(struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_disable_dev(struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_raw_set_dev(struct pnp_dev *dev, int depnum, int mode) { return -ENODEV; }
static inline int pnp_add_id(struct pnp_id *id, struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_register_driver(struct pnp_driver *drv) { return -ENODEV; }
static inline void pnp_unregister_driver(struct pnp_driver *drv) { ; }
static inline struct pnp_card *pnp_find_card(unsigned short vendor,
				 unsigned short device,
				 struct pnp_card *from) { return NULL; }
static inline struct pnp_dev *pnp_find_dev(struct pnp_card *card,
				unsigned short vendor,
				unsigned short function,
				struct pnp_dev *from) { return NULL; }

#endif /* CONFIG_PNP */


#ifdef DEBUG
#define pnp_dbg(format, arg...) printk(KERN_DEBUG "pnp: " format "\n" , ## arg)
#else
#define pnp_dbg(format, arg...) do {} while (0)
#endif

#endif /* __KERNEL__ */

#endif /* _LINUX_PNP_H */
