/*
 * Definitions for talking to the PMU.  The PMU is a microcontroller
 * which controls battery charging and system power on PowerBook 3400
 * and 2400 models as well as the RTC and various other things.
 *
 * Copyright (C) 1998 Paul Mackerras.
 */

#include <linux/config.h>
/*
 * PMU commands
 */
#define PMU_POWER_CTRL0		0x10	/* control power of some devices */
#define PMU_POWER_CTRL		0x11	/* control power of some devices */
#define PMU_ADB_CMD		0x20	/* send ADB packet */
#define PMU_ADB_POLL_OFF	0x21	/* disable ADB auto-poll */
#define PMU_WRITE_NVRAM		0x33	/* write non-volatile RAM */
#define PMU_READ_NVRAM		0x3b	/* read non-volatile RAM */
#define PMU_SET_RTC		0x30	/* set real-time clock */
#define PMU_READ_RTC		0x38	/* read real-time clock */
#define PMU_SET_VOLBUTTON	0x40	/* set volume up/down position */
#define PMU_BACKLIGHT_BRIGHT	0x41	/* set backlight brightness */
#define PMU_GET_VOLBUTTON	0x48	/* get volume up/down position */
#define PMU_PCEJECT		0x4c	/* eject PC-card from slot */
#define PMU_BATTERY_STATE	0x6b	/* report battery state etc. */
#define PMU_SET_INTR_MASK	0x70	/* set PMU interrupt mask */
#define PMU_INT_ACK		0x78	/* read interrupt bits */
#define PMU_SHUTDOWN		0x7e	/* turn power off */
#define PMU_SLEEP		0x7f	/* put CPU to sleep */
#define PMU_POWER_EVENTS	0x8f	/* Send power-event commands to PMU */
#define PMU_RESET		0xd0	/* reset CPU */
#define PMU_GET_BRIGHTBUTTON	0xd9	/* report brightness up/down pos */
#define PMU_GET_COVER		0xdc	/* report cover open/closed */
#define PMU_SYSTEM_READY	0xdf	/* tell PMU we are awake */

/* Bits to use with the PMU_POWER_CTRL0 command */
#define PMU_POW0_ON		0x80	/* OR this to power ON the device */
#define PMU_POW0_OFF		0x00	/* leave bit 7 to 0 to power it OFF */
#define PMU_POW0_HARD_DRIVE	0x04	/* Hard drive power (on wallstreet/lombard ?) */

/* Bits to use with the PMU_POWER_CTRL command */
#define PMU_POW_ON		0x80	/* OR this to power ON the device */
#define PMU_POW_OFF		0x00	/* leave bit 7 to 0 to power it OFF */
#define PMU_POW_BACKLIGHT	0x01	/* backlight power */
#define PMU_POW_CHARGER		0x02	/* battery charger power */
#define PMU_POW_IRLED		0x04	/* IR led power (on wallstreet) */
#define PMU_POW_MEDIABAY	0x08	/* media bay power (wallstreet/lombard ?) */


/* Bits in PMU interrupt and interrupt mask bytes */
#define PMU_INT_ADB_AUTO	0x04	/* ADB autopoll, when PMU_INT_ADB */
#define PMU_INT_PCEJECT		0x04	/* PC-card eject buttons */
#define PMU_INT_SNDBRT		0x08	/* sound/brightness up/down buttons */
#define PMU_INT_ADB		0x10	/* ADB autopoll or reply data */
#define PMU_INT_BATTERY		0x20
#define PMU_INT_WAKEUP		0x40
#define PMU_INT_TICK		0x80	/* 1-second tick interrupt */

/* Kind of PMU (model) */
enum {
	PMU_UNKNOWN,
	PMU_OHARE_BASED,	/* 2400, 3400, 3500 (old G3 powerbook) */
	PMU_HEATHROW_BASED,	/* PowerBook G3 series */
	PMU_PADDINGTON_BASED,	/* 1999 PowerBook G3 */
	PMU_KEYLARGO_BASED,	/* Core99 motherboard (PMU99) */
};

/* PMU PMU_POWER_EVENTS commands */
enum {
	PMU_PWR_GET_POWERUP_EVENTS	= 0x00,
	PMU_PWR_SET_POWERUP_EVENTS	= 0x01,
	PMU_PWR_CLR_POWERUP_EVENTS	= 0x02,
	PMU_PWR_GET_WAKEUP_EVENTS	= 0x03,
	PMU_PWR_SET_WAKEUP_EVENTS	= 0x04,
	PMU_PWR_CLR_WAKEUP_EVENTS	= 0x05,
};

/* Power events wakeup bits */
enum {
	PMU_PWR_WAKEUP_KEY		= 0x01,	/* Wake on key press */
	PMU_PWR_WAKEUP_AC_INSERT	= 0x02, /* Wake on AC adapter plug */
	PMU_PWR_WAKEUP_AC_CHANGE	= 0x04,
	PMU_PWR_WAKEUP_LID_OPEN		= 0x08,
	PMU_PWR_WAKEUP_RING		= 0x10,
};
	
/*
 * Ioctl commands for the /dev/pmu device
 */
#include <linux/ioctl.h>

/* no param */
#define PMU_IOC_SLEEP		_IO('B', 0)
/* out param: u32*	backlight value: 0 to 15 */
#define PMU_IOC_GET_BACKLIGHT	_IOR('B', 1, sizeof(__u32*))
/* in param: u32	backlight value: 0 to 15 */
#define PMU_IOC_SET_BACKLIGHT	_IOW('B', 2, sizeof(__u32))
/* out param: u32*	PMU model */
#define PMU_IOC_GET_MODEL	_IOR('B', 3, sizeof(__u32*))
/* out param: u32*	has_adb: 0 or 1 */
#define PMU_IOC_HAS_ADB		_IOR('B', 4, sizeof(__u32*)) 

#ifdef __KERNEL__

extern int find_via_pmu(void);
extern int via_pmu_start(void);

extern int pmu_request(struct adb_request *req,
		void (*done)(struct adb_request *), int nbytes, ...);

extern void pmu_poll(void);

/* For use before switching interrupts off for a long time;
 * warning: not stackable
 */
extern void pmu_suspend(void);
extern void pmu_resume(void);

extern void pmu_enable_irled(int on);

extern void pmu_restart(void);
extern void pmu_shutdown(void);

extern int pmu_present(void);
extern int pmu_get_model(void);

#ifdef CONFIG_PMAC_PBOOK
/*
 * Stuff for putting the powerbook to sleep and waking it again.
 *
 */
#include <linux/list.h>

struct pmu_sleep_notifier
{
	int (*notifier_call)(struct pmu_sleep_notifier *self, int when);
	int priority;
	struct list_head list;
};

/* Code values for calling sleep/wakeup handlers
 *
 * Note: If a sleep request got cancelled, all drivers will get
 * the PBOOK_SLEEP_REJECT, even those who didn't get the PBOOK_SLEEP_REQUEST.
 */
#define PBOOK_SLEEP_REQUEST	1
#define PBOOK_SLEEP_NOW		2
#define PBOOK_SLEEP_REJECT	3
#define PBOOK_WAKE		4

/* Result codes returned by the notifiers */
#define PBOOK_SLEEP_OK		0
#define PBOOK_SLEEP_REFUSE	-1

/* priority levels in notifiers */
#define SLEEP_LEVEL_VIDEO	100	/* Video driver (first wake) */
#define SLEEP_LEVEL_SOUND	90	/* Sound driver */
#define SLEEP_LEVEL_MEDIABAY	80	/* Media bay driver */
#define SLEEP_LEVEL_BLOCK	70	/* IDE, SCSI */
#define SLEEP_LEVEL_NET		60	/* bmac */
#define SLEEP_LEVEL_ADB		50	/* ADB */
#define SLEEP_LEVEL_MISC	30	/* Anything */
#define SLEEP_LEVEL_LAST	0	/* Anything */

/* special register notifier functions */
int pmu_register_sleep_notifier(struct pmu_sleep_notifier* notifier);
int pmu_unregister_sleep_notifier(struct pmu_sleep_notifier* notifier);

#endif /* CONFIG_PMAC_PBOOK */


#endif	/* __KERNEL__ */
