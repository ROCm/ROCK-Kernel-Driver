/*
 * Linux Plug and Play Support
 * Copyright by Adam Belay <ambx1@neo.rr.com>
 *
 */

#ifndef _LINUX_PNP_H
#define _LINUX_PNP_H

#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/list.h>


/*
 * Device Managemnt
 */

#define DEVICE_COUNT_IRQ	2
#define DEVICE_COUNT_DMA	2
#define DEVICE_COUNT_RESOURCE	12
#define MAX_DEVICES		8

struct pnp_resource;
struct pnp_protocol;
struct pnp_id;

struct pnp_card {
	char name[80];
	unsigned char number;		/* card number */
	struct list_head global_list;	/* node in global list of cards */
	struct list_head protocol_list;	/* node in protocol's list of cards */
	struct list_head devices;	/* devices attached to the card */
	struct pnp_protocol * protocol;
	struct pnp_id * id;		/* contains supported EISA IDs*/

	unsigned char	pnpver;		/* Plug & Play version */
	unsigned char	productver;	/* product version */
	unsigned int	serial;		/* serial number */
	unsigned char	checksum;	/* if zero - checksum passed */
	void	      * protocol_data;	/* Used to store protocol specific data */

	struct pnpc_driver * driver;	/* pointer to the driver bound to this device */
	struct list_head rdevs;		/* a list of devices requested by the card driver */
	struct proc_dir_entry *procdir;	/* directory entry in /proc/bus/isapnp */
	struct	device	dev;		/* Driver Model device interface */
};

#define global_to_pnp_card(n) list_entry(n, struct pnp_card, global_list)
#define protocol_to_pnp_card(n) list_entry(n, struct pnp_card, protocol_list)
#define to_pnp_card(n) list_entry(n, struct pnp_card, dev)
#define pnp_for_each_card(card) \
	for(dev = global_to_pnp_card(pnp_cards.next); \
	dev != global_to_pnp_card(&cards); \
	dev = global_to_pnp_card(card>global_list.next))

static inline void *pnpc_get_drvdata (struct pnp_card *pcard)
{
	return dev_get_drvdata(&pcard->dev);
}

static inline void pnpc_set_drvdata (struct pnp_card *pcard, void *data)
{
	dev_set_drvdata(&pcard->dev, data);
}

static inline void *pnpc_get_protodata (struct pnp_card *pcard)
{
	return pcard->protocol_data;
}

static inline void pnpc_set_protodata (struct pnp_card *pcard, void *data)
{
	pcard->protocol_data = data;
}

struct pnp_dev {
	char name[80];			/* device name */
	int active;			/* status of the device */
	int ro;				/* read only */
	struct list_head global_list;	/* node in global list of devices */
	struct list_head protocol_list;	/* node in list of device's protocol */
	struct list_head card_list;	/* node in card's list of devices */
	struct list_head rdev_list;	/* node in cards list of requested devices */
	struct pnp_protocol * protocol;
	struct pnp_card * card;
	struct pnp_id * id;		/* contains supported EISA IDs*/

	void * protocol_data;		/* Used to store protocol specific data */
	unsigned char number;		/* must be unique */
	unsigned short	regs;		/* ISAPnP: supported registers */
	
	struct pnp_resources *res;	/* possible resource information */
	struct resource resource[DEVICE_COUNT_RESOURCE]; /* I/O and memory regions + expansion ROMs */
	struct resource dma_resource[DEVICE_COUNT_DMA];
	struct resource irq_resource[DEVICE_COUNT_IRQ];

	struct pnp_driver * driver;	/* pointer to the driver bound to this device */
	struct	device	    dev;	/* Driver Model device interface */
	int		    flags;	/* used by protocols */
	struct proc_dir_entry *procent;	/* device entry in /proc/bus/isapnp */
};

#define global_to_pnp_dev(n) list_entry(n, struct pnp_dev, global_list)
#define card_to_pnp_dev(n) list_entry(n, struct pnp_dev, card_list)
#define protocol_to_pnp_dev(n) list_entry(n, struct pnp_dev, protocol_list)
#define	to_pnp_dev(n) container_of(n, struct pnp_dev, dev)
#define pnp_for_each_dev(dev) \
	for(dev = global_to_pnp_dev(pnp_global.next); \
	dev != global_to_pnp_dev(&pnp_global); \
	dev = global_to_pnp_dev(dev->global_list.next))

static inline void *pnp_get_drvdata (struct pnp_dev *pdev)
{
	return dev_get_drvdata(&pdev->dev);
}

static inline void pnp_set_drvdata (struct pnp_dev *pdev, void *data)
{
	dev_set_drvdata(&pdev->dev, data);
}

static inline void *pnp_get_protodata (struct pnp_dev *pdev)
{
	return pdev->protocol_data;
}

static inline void pnp_set_protodata (struct pnp_dev *pdev, void *data)
{
	pdev->protocol_data = data;
}

struct pnp_fixup {
	char id[7];
	void (*quirk_function)(struct pnp_dev *dev);	/* fixup function */
};


/*
 * Driver Management
 */

struct pnp_id {
	char id[7];
	struct pnp_id * next;
};

struct pnp_device_id {
	char id[7];
	unsigned long driver_data;	/* data private to the driver */
};

struct pnp_card_id {
	char id[7];
	unsigned long driver_data;	/* data private to the driver */
	struct {
		char id[7];
	} devs[MAX_DEVICES];		/* logical devices */
};

struct pnp_driver {
	struct list_head node;
	char *name;
	const struct pnp_device_id *id_table;
	int  (*probe)  (struct pnp_dev *dev, const struct pnp_device_id *dev_id);
	void (*remove) (struct pnp_dev *dev);
	struct device_driver driver;
};

#define	to_pnp_driver(drv) container_of(drv,struct pnp_driver, driver)

struct pnpc_driver {
	struct list_head node;
	char *name;
	const struct pnp_card_id *id_table;
	int  (*probe)  (struct pnp_card *card, const struct pnp_card_id *card_id);
	void (*remove) (struct pnp_card *card);
	struct device_driver driver;
};

#define	to_pnpc_driver(drv) container_of(drv,struct pnpc_driver, driver)


/*
 * Resource Management
 */

/* Use these instead of directly reading pnp_dev to get resource information */
#define pnp_port_start(dev,bar)   ((dev)->resource[(bar)].start)
#define pnp_port_end(dev,bar)     ((dev)->resource[(bar)].end)
#define pnp_port_flags(dev,bar)   ((dev)->resource[(bar)].flags)
#define pnp_port_len(dev,bar) \
	((pnp_port_start((dev),(bar)) == 0 &&	\
	  pnp_port_end((dev),(bar)) ==		\
	  pnp_port_start((dev),(bar))) ? 0 :	\
	  					\
	 (pnp_port_end((dev),(bar)) -		\
	  pnp_port_start((dev),(bar)) + 1))

#define pnp_mem_start(dev,bar)   ((dev)->resource[(bar+8)].start)
#define pnp_mem_end(dev,bar)     ((dev)->resource[(bar+8)].end)
#define pnp_mem_flags(dev,bar)   ((dev)->resource[(bar+8)].flags)
#define pnp_mem_len(dev,bar) \
	((pnp_mem_start((dev),(bar)) == 0 &&	\
	  pnp_mem_end((dev),(bar)) ==		\
	  pnp_mem_start((dev),(bar))) ? 0 :	\
	  					\
	 (pnp_mem_end((dev),(bar)) -		\
	  pnp_mem_start((dev),(bar)) + 1))

#define pnp_irq(dev,bar)	 ((dev)->irq_resource[(bar)].start)
#define pnp_irq_flags(dev,bar)	 ((dev)->irq_resource[(bar)].flags)

#define pnp_dma(dev,bar)	 ((dev)->dma_resource[(bar)].start)
#define pnp_dma_flags(dev,bar)	 ((dev)->dma_resource[(bar)].flags)

#define PNP_PORT_FLAG_16BITADDR	(1<<0)
#define PNP_PORT_FLAG_FIXED	(1<<1)

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


/* 
 * Protocol Management
 */

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
	struct list_head	cards;
	struct list_head	devices;
};

#define to_pnp_protocol(n) list_entry(n, struct pnp_protocol, protocol_list)
#define protocol_for_each_card(protocol,card) \
	for((card) = protocol_to_pnp_card((protocol)->cards.next); \
	(card) != protocol_to_pnp_card(&(protocol)->cards); \
	(card) = protocol_to_pnp_card((card)->protocol_list.next))
#define protocol_for_each_dev(protocol,dev) \
	for((dev) = protocol_to_pnp_dev((protocol)->devices.next); \
	(dev) != protocol_to_pnp_dev(&(protocol)->devices); \
	(dev) = protocol_to_pnp_dev((dev)->protocol_list.next))


#if defined(CONFIG_PNP)

/* core */
int pnp_register_protocol(struct pnp_protocol *protocol);
void pnp_unregister_protocol(struct pnp_protocol *protocol);
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
int compare_pnp_id(struct pnp_id * pos, const char * id);
int pnp_add_id(struct pnp_id *id, struct pnp_dev *dev);
int pnp_register_driver(struct pnp_driver *drv);
void pnp_unregister_driver(struct pnp_driver *drv);

#else

/* just in case anyone decides to call these without PnP Support Enabled */
static inline int pnp_register_protocol(struct pnp_protocol *protocol) { return -ENODEV; }
static inline void pnp_unregister_protocol(struct pnp_protocol *protocol) { }
static inline int pnp_init_device(struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_add_device(struct pnp_dev *dev) { return -ENODEV; }
static inline void pnp_remove_device(struct pnp_dev *dev) { }
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
static inline int compare_pnp_id(struct list_head * id_list, const char * id) { return -ENODEV; }
static inline int pnp_add_id(struct pnp_id *id, struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_register_driver(struct pnp_driver *drv) { return -ENODEV; }
static inline void pnp_unregister_driver(struct pnp_driver *drv) { ; }

#endif /* CONFIG_PNP */


#if defined(CONFIG_PNP_CARD)

/* card */
int pnpc_add_card(struct pnp_card *card);
void pnpc_remove_card(struct pnp_card *card);
int pnpc_add_device(struct pnp_card *card, struct pnp_dev *dev);
void pnpc_remove_device(struct pnp_dev *dev);
struct pnp_dev * pnp_request_card_device(struct pnp_card *card, const char *id, struct pnp_dev *from);
void pnp_release_card_device(struct pnp_dev *dev);
int pnpc_register_driver(struct pnpc_driver * drv);
void pnpc_unregister_driver(struct pnpc_driver *drv);
int pnpc_add_id(struct pnp_id *id, struct pnp_card *card);
extern struct list_head pnp_cards;

#else

static inline int pnpc_add_card(struct pnp_card *card) { return -ENODEV; }
static inline void pnpc_remove_card(struct pnp_card *card) { ; }
static inline int pnpc_add_device(struct pnp_card *card, struct pnp_dev *dev) { return -ENODEV; }
static inline void pnpc_remove_device(struct pnp_dev *dev) { ; }
static inline struct pnp_dev * pnp_request_card_device(struct pnp_card *card, const char *id, struct pnp_dev *from) { return NULL; }
static inline void pnp_release_card_device(struct pnp_dev *dev) { ; }
static inline int pnpc_register_driver(struct pnpc_driver *drv) { return -ENODEV; }
static inline void pnpc_unregister_driver(struct pnpc_driver *drv) { ; }
static inline int pnpc_add_id(struct pnp_id *id, struct pnp_card *card) { return -ENODEV; }

#endif /* CONFIG_PNP_CARD */


#if defined(CONFIG_ISAPNP)

/* compat */
struct pnp_card *pnp_find_card(unsigned short vendor,
				 unsigned short device,
				 struct pnp_card *from);
struct pnp_dev *pnp_find_dev(struct pnp_card *card,
				unsigned short vendor,
				unsigned short function,
				struct pnp_dev *from);

#else

static inline struct pnp_card *pnp_find_card(unsigned short vendor,
				 unsigned short device,
				 struct pnp_card *from) { return NULL; }
static inline struct pnp_dev *pnp_find_dev(struct pnp_card *card,
				unsigned short vendor,
				unsigned short function,
				struct pnp_dev *from) { return NULL; }

#endif /* CONFIG_ISAPNP */


#ifdef DEBUG
#define pnp_dbg(format, arg...) printk(KERN_DEBUG "pnp: " format "\n" , ## arg)
#else
#define pnp_dbg(format, arg...) do {} while (0)
#endif

#endif /* __KERNEL__ */

#endif /* _LINUX_PNP_H */
