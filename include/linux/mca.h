/*
 * Header for Microchannel Architecture Bus
 * Written by Martin Kolinek, February 1996
 */

#ifndef _LINUX_MCA_H
#define _LINUX_MCA_H

/* FIXME: This shouldn't happen, but we need everything that previously
 * included mca.h to compile.  Take it out later when the MCA #includes
 * are sorted out */
#include <linux/device.h>

/* The detection of MCA bus is done in the real mode (using BIOS).
 * The information is exported to the protected code, where this
 * variable is set to one in case MCA bus was detected.
 */
#ifndef MCA_bus__is_a_macro
extern int  MCA_bus;
#endif

/* Maximal number of MCA slots - actually, some machines have less, but
 * they all have sufficient number of POS registers to cover 8.
 */
#define MCA_MAX_SLOT_NR  8

/* Most machines have only one MCA bus.  The only multiple bus machines
 * I know have at most two */
#define MAX_MCA_BUSSES 2

#define MCA_PRIMARY_BUS		0
#define MCA_SECONDARY_BUS	1

/* MCA_NOTFOUND is an error condition.  The other two indicate
 * motherboard POS registers contain the adapter.  They might be
 * returned by the mca_find_adapter() function, and can be used as
 * arguments to mca_read_stored_pos().  I'm not going to allow direct
 * access to the motherboard registers until we run across an adapter
 * that requires it.  We don't know enough about them to know if it's
 * safe.
 *
 * See Documentation/mca.txt or one of the existing drivers for
 * more information.
 */
#define MCA_NOTFOUND	(-1)
#define MCA_INTEGSCSI	(MCA_MAX_SLOT_NR)
#define MCA_INTEGVIDEO	(MCA_MAX_SLOT_NR+1)
#define MCA_MOTHERBOARD (MCA_MAX_SLOT_NR+2)

#define MCA_DUMMY_POS_START	0x10000
#define MCA_INTEGSCSI_POS	(MCA_DUMMY_POS_START+1)
#define MCA_INTEGVIDEO_POS	(MCA_DUMMY_POS_START+2)
#define MCA_MOTHERBOARD_POS	(MCA_DUMMY_POS_START+3)


/* MCA registers */

#define MCA_MOTHERBOARD_SETUP_REG	0x94
#define MCA_ADAPTER_SETUP_REG		0x96
#define MCA_POS_REG(n)			(0x100+(n))

#define MCA_ENABLED	0x01	/* POS 2, set if adapter enabled */

/* Max number of adapters, including both slots and various integrated
 * things.
 */
#define MCA_NUMADAPTERS (MCA_MAX_SLOT_NR+3)

/* Returns the slot of the first enabled adapter matching id.  User can
 * specify a starting slot beyond zero, to deal with detecting multiple
 * devices.  Returns MCA_NOTFOUND if id not found.  Also checks the
 * integrated adapters.
 */
extern int mca_find_adapter(int id, int start);
extern int mca_find_unused_adapter(int id, int start);

/* adapter state info - returns 0 if no */
extern int mca_isadapter(int slot);
extern int mca_isenabled(int slot);

extern int mca_is_adapter_used(int slot);
extern int mca_mark_as_used(int slot);
extern void mca_mark_as_unused(int slot);

/* gets a byte out of POS register (stored in memory) */
extern unsigned char mca_read_stored_pos(int slot, int reg);

/* This can be expanded later.  Right now, it gives us a way of
 * getting meaningful information into the MCA_info structure,
 * so we can have a more interesting /proc/mca.
 */
extern void mca_set_adapter_name(int slot, char* name);
extern char* mca_get_adapter_name(int slot);

/* This sets up an information callback for /proc/mca/slot?.  The
 * function is called with the buffer, slot, and device pointer (or
 * some equally informative context information, or nothing, if you
 * prefer), and is expected to put useful information into the
 * buffer.  The adapter name, id, and POS registers get printed
 * before this is called though, so don't do it again.
 *
 * This should be called with a NULL procfn when a module
 * unregisters, thus preventing kernel crashes and other such
 * nastiness.
 */
typedef int (*MCA_ProcFn)(char* buf, int slot, void* dev);

/* These routines actually mess with the hardware POS registers.  They
 * temporarily disable the device (and interrupts), so make sure you know
 * what you're doing if you use them.  Furthermore, writing to a POS may
 * result in two devices trying to share a resource, which in turn can
 * result in multiple devices sharing memory spaces, IRQs, or even trashing
 * hardware.  YOU HAVE BEEN WARNED.
 *
 * You can only access slots with this.  Motherboard registers are off
 * limits.
 */

/* read a byte from the specified POS register. */
extern unsigned char mca_read_pos(int slot, int reg);

/* write a byte to the specified POS register. */
extern void mca_write_pos(int slot, int reg, unsigned char byte);

/* Should only be called by the NMI interrupt handler, this will do some
 * fancy stuff to figure out what might have generated a NMI.
 */
extern void mca_handle_nmi(void);

enum MCA_AdapterStatus {
	MCA_ADAPTER_NORMAL = 0,
	MCA_ADAPTER_NONE = 1,
	MCA_ADAPTER_DISABLED = 2,
	MCA_ADAPTER_ERROR = 3
};

struct mca_bus {
	u64			default_dma_mask;
	int			number;
	struct device		dev;
};
#define to_mca_bus(mdev) container_of(mdev, struct mca_bus, dev)

struct mca_device {
	u64			dma_mask;
	int			pos_id;
	int			slot;

	/* is there a driver installed? 0 - No, 1 - Yes */
	int			driver_loaded;
	/* POS registers */
	unsigned char		pos[8];
	/* if a pseudo adapter of the motherboard, this is the motherboard
	 * register value to use for setup cycles */
	short			pos_register;
	
	enum MCA_AdapterStatus	status;
#ifdef CONFIG_PROC_FS
	/* name of the proc/mca file */
	char			procname[8];
	/* /proc info callback */
	MCA_ProcFn		procfn;
	/* device/context info for proc callback */
	void			*proc_dev;
#endif
	struct device		dev;
};
#define to_mca_device(mdev) container_of(mdev, struct mca_device, dev)

struct mca_driver {
	const short		*id_table;
	struct device_driver	driver;
};
#define to_mca_driver(mdriver) container_of(mdriver, struct mca_driver, driver)

extern int mca_driver_register(struct mca_driver *);
extern int mca_register_device(int bus, struct mca_device *);
extern struct mca_device *mca_find_device_by_slot(int slot);
extern int mca_system_init(void);
extern struct mca_bus *mca_attach_bus(int);


extern spinlock_t mca_lock;

extern struct bus_type mca_bus_type;

#ifdef CONFIG_PROC_FS
extern void mca_do_proc_init(void);
extern void mca_set_adapter_procfn(int slot, MCA_ProcFn, void* dev);
#else
static inline void mca_do_proc_init(void)
{
}

static inline void mca_set_adapter_procfn(int slot, MCA_ProcFn *fn, void* dev)
{
}
#endif

#endif /* _LINUX_MCA_H */
