/* Linux ISDN subsystem, common used functions
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/isdn.h>
#include <linux/smp_lock.h>
#include <linux/ctype.h>
#include "isdn_common.h"
#include "isdn_net_lib.h"
#include "isdn_net.h"
#include "isdn_tty.h"
#include "isdn_ppp.h"
#ifdef CONFIG_ISDN_AUDIO
#include "isdn_audio.h"
#endif
#include <linux/isdn_divertif.h>
#include <linux/devfs_fs_kernel.h>

MODULE_DESCRIPTION("ISDN4Linux: link layer");
MODULE_AUTHOR("Fritz Elfert");
MODULE_LICENSE("GPL");

static isdn_dev_t *isdndev;

isdn_dev_t *
get_isdn_dev(void) {
	return(isdndev);
}

/* Description of hardware-level-driver */
typedef struct isdn_driver {
	int                 di;
	char                id[20];
	atomic_t            refcnt;
	unsigned long       flags;            /* Misc driver Flags           */
	unsigned long       features;
	int                 channels;         /* Number of channels          */
	wait_queue_head_t   st_waitq;         /* Wait-Queue for status-reads */
	int                 maxbufsize;       /* Maximum Buffersize supported*/
	int                 stavail;          /* Chars avail on Status-device*/
	isdn_if            *interface;        /* Interface to driver         */
	char                msn2eaz[10][ISDN_MSNLEN];  /*  MSN->EAZ          */
	spinlock_t          lock;
	struct isdn_slot   *slots; 
	struct fsm_inst     fi;
} isdn_driver_t;

static spinlock_t	drivers_lock = SPIN_LOCK_UNLOCKED;
static isdn_driver_t	*drivers[ISDN_MAX_DRIVERS];

static void isdn_lock_driver(struct isdn_driver *drv);
static void isdn_unlock_driver(struct isdn_driver *drv);

/* ====================================================================== */

static void drv_destroy(struct isdn_driver *drv);

static inline struct isdn_driver *
get_drv(struct isdn_driver *drv)
{
	printk("get_drv %d: %d -> %d\n", drv->di, atomic_read(&drv->refcnt), 
	       atomic_read(&drv->refcnt) + 1); 
	atomic_inc(&drv->refcnt);
	return drv;
}

static inline void
put_drv(struct isdn_driver *drv)
{
	printk("put_drv %d: %d -> %d\n", drv->di, atomic_read(&drv->refcnt),
	       atomic_read(&drv->refcnt) - 1); 
	if (atomic_dec_and_test(&drv->refcnt)) {
		drv_destroy(drv);
	}
}

/* ====================================================================== */

static struct fsm slot_fsm;
static void slot_debug(struct fsm_inst *fi, char *fmt, ...);

static char *slot_st_str[] = {
	"ST_SLOT_NULL",
	"ST_SLOT_BOUND",
	"ST_SLOT_IN",
	"ST_SLOT_WAIT_DCONN",
	"ST_SLOT_DCONN",
	"ST_SLOT_WAIT_BCONN",
	"ST_SLOT_ACTIVE",
	"ST_SLOT_WAIT_BHUP",
	"ST_SLOT_WAIT_DHUP",
};

static char *ev_str[] = {
	"EV_DRV_REGISTER",
	"EV_STAT_RUN",
	"EV_STAT_STOP",
	"EV_STAT_UNLOAD",
	"EV_STAT_STAVAIL",
	"EV_STAT_ADDCH",
	"EV_STAT_ICALL",
	"EV_STAT_DCONN",
	"EV_STAT_BCONN",
	"EV_STAT_BHUP",
	"EV_STAT_DHUP",
	"EV_STAT_BSENT",
	"EV_STAT_CINF",
	"EV_STAT_CAUSE",
	"EV_STAT_DISPLAY",
	"EV_STAT_FAXIND",
	"EV_STAT_AUDIO",
	"EV_CMD_CLREAZ",
	"EV_CMD_SETEAZ",
	"EV_CMD_SETL2",
	"EV_CMD_SETL3",
	"EV_CMD_DIAL",
	"EV_CMD_ACCEPTD",
	"EV_CMD_ACCEPTB",
	"EV_CMD_HANGUP",
	"EV_DATA_REQ",
	"EV_DATA_IND",
	"EV_SLOT_BIND",
	"EV_SLOT_UNBIND",
};

static int __slot_command(struct isdn_slot *slot, isdn_ctrl *cmd);

static void isdn_v110_setl2(struct isdn_slot *slot, isdn_ctrl *cmd);
static void __isdn_v110_open(struct isdn_slot *slot);
static void __isdn_v110_close(struct isdn_slot *slot);
static void __isdn_v110_bsent(struct isdn_slot *slot, int pr, isdn_ctrl *cmd);
static int  isdn_v110_data_ind(struct isdn_slot *slot, struct sk_buff *skb);
static int  isdn_v110_data_req(struct isdn_slot *slot, struct sk_buff *skb);

static inline int
do_event_cb(struct isdn_slot *slot, int pr, void *arg)
{
	if (slot->event_cb)
		return slot->event_cb(slot, pr, arg);

	return -ENXIO;
}

static int
slot_bind(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	
	isdn_lock_driver(slot->drv);
	fsm_change_state(fi, ST_SLOT_BOUND);

	return 0;
}

/* just pass through command */
static int
slot_command(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	isdn_ctrl *c = arg;

	return __slot_command(slot, c);
}

/* just pass through status */
static int
slot_stat(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;

	do_event_cb(slot, pr, arg);
	return 0;
}

/* just pass through command */
static int
slot_setl2(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	isdn_ctrl *c = arg;

	isdn_v110_setl2(slot, c);

	return __slot_command(slot, c);
}

static int
slot_dial(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	isdn_ctrl *ctrl = arg;
	int retval;

	retval = __slot_command(slot, ctrl);
	if (retval >= 0)
		fsm_change_state(fi, ST_SLOT_WAIT_DCONN);

	return retval;
}

static int
slot_acceptd(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	isdn_ctrl *ctrl = arg;
	int retval;

	retval = __slot_command(slot, ctrl);
	if (retval >= 0)
		fsm_change_state(fi, ST_SLOT_WAIT_DCONN);

	return retval;
}

static int
slot_acceptb(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	isdn_ctrl *ctrl = arg;
	int retval;

	retval = __slot_command(slot, ctrl);
	if (retval >= 0)
		fsm_change_state(fi, ST_SLOT_WAIT_BCONN);

	return retval;
}

static int
slot_actv_hangup(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	isdn_ctrl *ctrl = arg;
	int retval;

	retval = __slot_command(slot, ctrl);
	if (retval >= 0) {
		fsm_change_state(fi, ST_SLOT_WAIT_BHUP);
	}
	return retval;
}

static int
slot_dconn(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;

	fsm_change_state(fi, ST_SLOT_DCONN);
	do_event_cb(slot, pr, arg);
	return 0;
}

static int
slot_bconn(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;

	fsm_change_state(fi, ST_SLOT_ACTIVE);
	__isdn_v110_open(slot);

	isdn_info_update();

	do_event_cb(slot, pr, arg);
	return 0;
}

static int
slot_bhup(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;

	__isdn_v110_close(slot);
	fsm_change_state(fi, ST_SLOT_WAIT_DHUP);

	do_event_cb(slot, pr, arg);
	return 0;
}

static int
slot_dhup(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;

	fsm_change_state(fi, ST_SLOT_BOUND);

	do_event_cb(slot, pr, arg);
	return 0;
}

static int
slot_data_req(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	struct sk_buff *skb = arg;

	return isdn_v110_data_req(slot, skb);
}

static int
slot_data_ind(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	struct sk_buff *skb = arg;

	/* Update statistics */
	slot->ibytes += skb->len;

	return isdn_v110_data_ind(slot, skb);
}

static int
slot_bsent(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	isdn_ctrl *ctrl = arg;

	__isdn_v110_bsent(slot, pr, ctrl);
	return 0;
}

static int
slot_icall(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	isdn_ctrl *ctrl = arg;
	int retval;

	isdn_lock_driver(slot->drv);
	fsm_change_state(fi, ST_SLOT_IN);
	slot_debug(fi, "ICALL: %s\n", ctrl->parm.num);
	if (isdndev->global_flags & ISDN_GLOBAL_STOPPED)
		return 0;
	
	strcpy(slot->num, ctrl->parm.setup.phone);
	/* Try to find a network-interface which will accept incoming call */
	retval = isdn_net_find_icall(slot, &ctrl->parm.setup);

	/* already taken by net now? */
	if (fi->state != ST_SLOT_IN)
		goto out;

	retval = isdn_tty_find_icall(slot, &ctrl->parm.setup);
 out:
	return 0;
}

/* should become broadcast later */
static int
slot_in_dhup(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;

	isdn_unlock_driver(slot->drv);
	fsm_change_state(fi, ST_SLOT_NULL);
	do_event_cb(slot, pr, arg);
	return 0;
}

static int
slot_unbind(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_slot *slot = fi->userdata;
	isdn_ctrl cmd;

	isdn_unlock_driver(slot->drv);
	fsm_change_state(fi, ST_SLOT_NULL);
	strcpy(slot->num, "???");
	cmd.parm.num[0] = '\0';
	isdn_slot_command(slot, ISDN_CMD_SETEAZ, &cmd);
	slot->ibytes = 0;
	slot->obytes = 0;
	slot->usage = ISDN_USAGE_NONE;
	put_drv(slot->drv);
	isdn_info_update();
	return 0;
}

static struct fsm_node slot_fn_tbl[] = {
	{ ST_SLOT_NULL,          EV_SLOT_BIND,   slot_bind        },
	{ ST_SLOT_NULL,          EV_STAT_ICALL,  slot_icall       },

	{ ST_SLOT_BOUND,         EV_CMD_CLREAZ,  slot_command     },
	{ ST_SLOT_BOUND,         EV_CMD_SETEAZ,  slot_command     },
	{ ST_SLOT_BOUND,         EV_CMD_SETL2,   slot_setl2       },
	{ ST_SLOT_BOUND,         EV_CMD_SETL3,   slot_command     },
	{ ST_SLOT_BOUND,         EV_CMD_DIAL,    slot_dial        },
	{ ST_SLOT_BOUND,         EV_SLOT_UNBIND, slot_unbind      },

	{ ST_SLOT_IN,            EV_CMD_SETL2,   slot_setl2       },
	{ ST_SLOT_IN,            EV_CMD_SETL3,   slot_command     },
	{ ST_SLOT_IN,            EV_CMD_ACCEPTD, slot_acceptd     },
	{ ST_SLOT_IN,            EV_STAT_DHUP,   slot_in_dhup     },

	{ ST_SLOT_WAIT_DCONN,    EV_STAT_DCONN,  slot_dconn       },
	{ ST_SLOT_WAIT_DCONN,    EV_STAT_DHUP,   slot_dhup        },

	{ ST_SLOT_DCONN,         EV_CMD_ACCEPTB, slot_acceptb     },
	{ ST_SLOT_DCONN,         EV_STAT_BCONN,  slot_bconn       },

	{ ST_SLOT_WAIT_BCONN,    EV_STAT_BCONN,  slot_bconn       },

	{ ST_SLOT_ACTIVE,        EV_DATA_REQ,    slot_data_req    },
	{ ST_SLOT_ACTIVE,        EV_DATA_IND,    slot_data_ind    },
	{ ST_SLOT_ACTIVE,        EV_CMD_HANGUP,  slot_actv_hangup },
	{ ST_SLOT_ACTIVE,        EV_STAT_BSENT,  slot_bsent       },
	{ ST_SLOT_ACTIVE,        EV_STAT_BHUP,   slot_bhup        },
	{ ST_SLOT_ACTIVE,        EV_STAT_FAXIND, slot_stat        },
	{ ST_SLOT_ACTIVE,        EV_STAT_AUDIO,  slot_stat        },

	{ ST_SLOT_WAIT_BHUP,     EV_STAT_BHUP,   slot_bhup        },

	{ ST_SLOT_WAIT_DHUP,     EV_STAT_DHUP,   slot_dhup        },
};

static struct fsm slot_fsm = {
	.st_cnt = ARRAY_SIZE(slot_st_str),
	.st_str = slot_st_str,
	.ev_cnt = ARRAY_SIZE(ev_str),
	.ev_str = ev_str,
	.fn_cnt = ARRAY_SIZE(slot_fn_tbl),
	.fn_tbl = slot_fn_tbl,
};

static void slot_debug(struct fsm_inst *fi, char *fmt, ...)
{
	va_list args;
	struct isdn_slot *slot = fi->userdata;
	char buf[128];
	char *p = buf;

	va_start(args, fmt);
	p += sprintf(p, "slot (%d:%d): ", slot->di, slot->ch);
	p += vsprintf(p, fmt, args);
	va_end(args);
	printk(KERN_DEBUG "%s\n", buf);
}

/* ====================================================================== */

static spinlock_t stat_lock = SPIN_LOCK_UNLOCKED;

static struct fsm drv_fsm;

enum {
	ST_DRV_NULL,
	ST_DRV_LOADED,
	ST_DRV_RUNNING,
};

static char *drv_st_str[] = {
	"ST_DRV_NULL",
	"ST_DRV_LOADED",
	"ST_DRV_RUNNING",
};

#define DRV_FLAG_REJBUS  1

static int __drv_command(struct isdn_driver *drv, isdn_ctrl *cmd);

static int
isdn_writebuf_skb(struct isdn_slot *slot, struct sk_buff *skb)
{
	struct sk_buff *skb2;
	struct isdn_driver *drv = slot->drv;
	int hl = drv->interface->hl_hdrlen;
	int retval;

	if (skb_headroom(skb) >= hl) {
		retval = drv->interface->writebuf_skb(slot->di, slot->ch, 1, skb);
		goto out;
	}
	skb2 = skb_realloc_headroom(skb, hl);
	if (!skb2) {
		retval = -ENOMEM;
		goto out;
	}
	retval = drv->interface->writebuf_skb(slot->di, slot->ch, 1, skb2);
	if (retval < 0)
		kfree_skb(skb2);
	else
		kfree_skb(skb);

 out:
	if (retval > 0)
		slot->obytes += retval;

	return retval;
}

int
__isdn_drv_lookup(char *drvid)
{
	int drvidx;

	for (drvidx = 0; drvidx < ISDN_MAX_DRIVERS; drvidx++) {
		if (!drivers[drvidx])
			continue;

		if (strcmp(drivers[drvidx]->id, drvid) == 0)
			return drvidx;
	}
	return -1;
}

int
isdn_drv_lookup(char *drvid)
{
	unsigned long flags;
	int drvidx;

	spin_lock_irqsave(&drivers_lock, flags);
	drvidx = __isdn_drv_lookup(drvid);
	spin_unlock_irqrestore(&drivers_lock, flags);
	return drvidx;
}

static void
drv_destroy(struct isdn_driver *drv)
{
	kfree(drv->slots);
	kfree(drv);
}

static struct isdn_driver *
get_drv_by_nr(int di)
{
	unsigned long flags;
	struct isdn_driver *drv;
	
	BUG_ON(di < 0 || di >= ISDN_MAX_DRIVERS);

	spin_lock_irqsave(&drivers_lock, flags);
	drv = drivers[di];
	if (drv)
		get_drv(drv);
	spin_unlock_irqrestore(&drivers_lock, flags);
	return drv;
}

char *
isdn_drv_drvid(int di)
{
	if (!drivers[di]) {
		isdn_BUG();
		return "";
	}
	return drivers[di]->id;
}

/* 
 * Helper keeping track of the features the drivers support
 */
static void
set_global_features(void)
{
	unsigned long flags;
	int drvidx;

	isdndev->global_features = 0;
	spin_lock_irqsave(&drivers_lock, flags);
	for (drvidx = 0; drvidx < ISDN_MAX_DRIVERS; drvidx++) {
		if (!drivers[drvidx])
			continue;
		if (drivers[drvidx]->fi.state != ST_DRV_RUNNING)
			continue;
		isdndev->global_features |= drivers[drvidx]->features;
	}
	spin_unlock_irqrestore(&drivers_lock, flags);
}

/*
 * driver state machine
 */
static int  isdn_add_channels(struct isdn_driver *, int);
static void isdn_receive_skb_callback(int di, int ch, struct sk_buff *skb);
static int  isdn_status_callback(isdn_ctrl * c);

static void isdn_v110_add_features(struct isdn_driver *drv);

static int
drv_register(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_driver *drv = fi->userdata;
	isdn_if *iif = arg;
	
	fsm_change_state(fi, ST_DRV_LOADED);
	drv->maxbufsize = iif->maxbufsize;
	drv->interface = iif;
	iif->channels = drv->di;
	iif->rcvcallb_skb = isdn_receive_skb_callback;
	iif->statcallb = isdn_status_callback;

	isdn_info_update();
	return(0);
}

static int
drv_stat_run(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_driver *drv = fi->userdata;
	fsm_change_state(fi, ST_DRV_RUNNING);

	drv->features = drv->interface->features;
	isdn_v110_add_features(drv);
	set_global_features();
	return(0);
}

static int
drv_stat_stop(struct fsm_inst *fi, int pr, void *arg)
{
	fsm_change_state(fi, ST_DRV_LOADED);
	set_global_features();
	return(0);
}

static int
drv_stat_unload(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_driver *drv = fi->userdata;
	unsigned long flags;

	spin_lock_irqsave(&drivers_lock, flags);
	drivers[drv->di] = NULL;
	spin_unlock_irqrestore(&drivers_lock, flags);
	put_drv(drv);

	isdndev->channels -= drv->channels;

	isdn_info_update();
	return 0;
}

static int
drv_stat_stavail(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_driver *drv = fi->userdata;
	unsigned long flags;
	isdn_ctrl *c = arg;
	
	spin_lock_irqsave(&stat_lock, flags);
	drv->stavail += c->arg;
	spin_unlock_irqrestore(&stat_lock, flags);
	wake_up_interruptible(&drv->st_waitq);
	return 0;
}

static int
drv_to_slot(struct fsm_inst *fi, int pr, void *arg)
{
	struct isdn_driver *drv = fi->userdata;
	isdn_ctrl *c = arg;
	int ch = c->arg & 0xff;

	return fsm_event(&drv->slots[ch].fi, pr, arg);
}

static struct fsm_node drv_fn_tbl[] = {
	{ ST_DRV_NULL,    EV_DRV_REGISTER, drv_register     },

	{ ST_DRV_LOADED,  EV_STAT_RUN,     drv_stat_run     },
	{ ST_DRV_LOADED,  EV_STAT_STAVAIL, drv_stat_stavail },
	{ ST_DRV_LOADED,  EV_STAT_UNLOAD,  drv_stat_unload  },

	{ ST_DRV_RUNNING, EV_STAT_STOP,    drv_stat_stop    },
	{ ST_DRV_RUNNING, EV_STAT_STAVAIL, drv_stat_stavail },
	{ ST_DRV_RUNNING, EV_STAT_ICALL,   drv_to_slot      },
	{ ST_DRV_RUNNING, EV_STAT_DCONN,   drv_to_slot      },
	{ ST_DRV_RUNNING, EV_STAT_BCONN,   drv_to_slot      },
	{ ST_DRV_RUNNING, EV_STAT_BHUP,    drv_to_slot      },
	{ ST_DRV_RUNNING, EV_STAT_DHUP,    drv_to_slot      },
	{ ST_DRV_RUNNING, EV_STAT_BSENT,   drv_to_slot      },
	{ ST_DRV_RUNNING, EV_STAT_CINF,    drv_to_slot      },
	{ ST_DRV_RUNNING, EV_STAT_CAUSE,   drv_to_slot      },
	{ ST_DRV_RUNNING, EV_STAT_DISPLAY, drv_to_slot      },
	{ ST_DRV_RUNNING, EV_STAT_FAXIND,  drv_to_slot      },
	{ ST_DRV_RUNNING, EV_STAT_AUDIO,   drv_to_slot      },
};

static struct fsm drv_fsm = {
	.st_cnt = ARRAY_SIZE(drv_st_str),
	.st_str = drv_st_str,
	.ev_cnt = ARRAY_SIZE(ev_str),
	.ev_str = ev_str,
	.fn_cnt = ARRAY_SIZE(drv_fn_tbl),
	.fn_tbl = drv_fn_tbl,
};

static void drv_debug(struct fsm_inst *fi, char *fmt, ...)
{
	va_list args;
	struct isdn_driver *drv = fi->userdata;
	char buf[128];
	char *p = buf;

	va_start(args, fmt);
	p += sprintf(p, "%s: ", drv->id);
	p += vsprintf(p, fmt, args);
	va_end(args);
	printk(KERN_DEBUG "%s\n", buf);
}

/* ====================================================================== */
/* callbacks from hardware driver                                         */
/* ====================================================================== */

/* Receive a packet from B-Channel. */
static void
isdn_receive_skb_callback(int di, int ch, struct sk_buff *skb)
{
	struct isdn_driver *drv;

	drv = get_drv_by_nr(di);
	if (!drv) {
		/* hardware driver is buggy - driver isn't registered */
		isdn_BUG();
		goto out;
	}
	/* we short-cut here instead of going through the driver fsm */
	if (drv->fi.state != ST_DRV_RUNNING) {
		/* hardware driver is buggy - driver isn't running */
		isdn_BUG();
		goto out;
	}
	if (fsm_event(&drv->slots[ch].fi, EV_DATA_IND, skb))
		dev_kfree_skb(skb);
 out:
	put_drv(drv);
}

/* Receive status indications */
static int
isdn_status_callback(isdn_ctrl *c)
{
	struct isdn_driver *drv;
	int rc;

	drv = get_drv_by_nr(c->driver);
	if (!drv) {
		/* hardware driver is buggy - driver isn't registered */
		isdn_BUG();
		return 1;
	}
	
	switch (c->command) {
		case ISDN_STAT_STAVAIL:
			rc = fsm_event(&drv->fi, EV_STAT_STAVAIL, c);
			break;
		case ISDN_STAT_RUN:
			rc = fsm_event(&drv->fi, EV_STAT_RUN, c);
			break;
		case ISDN_STAT_STOP:
			rc = fsm_event(&drv->fi, EV_STAT_STOP, c);
			break;
		case ISDN_STAT_UNLOAD:
			rc = fsm_event(&drv->fi, EV_STAT_UNLOAD, c);
			break;
		case ISDN_STAT_ADDCH:
			rc = fsm_event(&drv->fi, EV_STAT_ADDCH, c);
			break;
		case ISDN_STAT_ICALL:
			rc = fsm_event(&drv->fi, EV_STAT_ICALL, c);
			break;
		case ISDN_STAT_DCONN:
			rc = fsm_event(&drv->fi, EV_STAT_DCONN, c);
			break;
		case ISDN_STAT_BCONN:
			rc = fsm_event(&drv->fi, EV_STAT_BCONN, c);
			break;
		case ISDN_STAT_BHUP:
			rc = fsm_event(&drv->fi, EV_STAT_BHUP, c);
			break;
		case ISDN_STAT_DHUP:
			rc = fsm_event(&drv->fi, EV_STAT_DHUP, c);
			break;
		case ISDN_STAT_BSENT:
			rc = fsm_event(&drv->fi, EV_STAT_BSENT, c);
			break;
		case ISDN_STAT_CINF:
			rc = fsm_event(&drv->fi, EV_STAT_CINF, c);
			break;
		case ISDN_STAT_CAUSE:
			rc = fsm_event(&drv->fi, EV_STAT_CAUSE, c);
			break;
		case ISDN_STAT_DISPLAY:
			rc = fsm_event(&drv->fi, EV_STAT_DISPLAY, c);
			break;
		case ISDN_STAT_FAXIND:
			rc = fsm_event(&drv->fi, EV_STAT_FAXIND, c);
			break;
		case ISDN_STAT_AUDIO:
			rc = fsm_event(&drv->fi, EV_STAT_AUDIO, c);
			break;
#warning FIXME divert interface
#if 0
		case ISDN_STAT_ICALL:
			/* Find any ttyI, waiting for D-channel setup */
			if (isdn_tty_stat_callback(i, c)) {
				cmd.driver = di;
				cmd.arg = c->arg;
				cmd.command = ISDN_CMD_ACCEPTB;
				isdn_command(&cmd);
				break;
			}
			break;
			switch (r) {
				case 0:
                                         if (divert_if)
						 if ((retval = divert_if->stat_callback(c))) 
							 return(retval); /* processed */
					if ((!retval) && (drivers[di]->flags & DRV_FLAG_REJBUS)) {
						/* No tty responding */
						cmd.driver = di;
						cmd.arg = c->arg;
						cmd.command = ISDN_CMD_HANGUP;
						isdn_command(&cmd);
						retval = 2;
					}
					break;
				case 1: /* incoming call accepted by net interface */

				case 2:	/* For calling back, first reject incoming call ... */
				case 3:	/* Interface found, but down, reject call actively  */
					retval = 2;
					printk(KERN_INFO "isdn: Rejecting Call\n");
					cmd.driver = di;
					cmd.arg = c->arg;
					cmd.command = ISDN_CMD_HANGUP;
					isdn_command(&cmd);
					if (r == 3)
						break;
					/* Fall through */
				case 4:
					/* ... then start callback. */
					break;
				case 5:
					/* Number would eventually match, if longer */
					retval = 3;
					break;
			}
			dbg_statcallb("ICALL: ret=%d\n", retval);
			return retval;
			break;
		case ISDN_STAT_DHUP:
                        if (divert_if)
				divert_if->stat_callback(c); 
			break;
		case ISDN_STAT_DISCH:
			save_flags(flags);
			cli();
			for (i = 0; i < ISDN_MAX_CHANNELS; i++)
				if ((slots[i].di == di) &&
				    (slots[i].ch == c->arg)) {
					if (c->parm.num[0])
						slots[i].usage &= ~ISDN_USAGE_DISABLED;
					else if (USG_NONE(isdn_slot_usage(i)))
						slots[i].usage |= ISDN_USAGE_DISABLED;
					else 
						retval = -1;
					break;
				}
			restore_flags(flags);
			break;
		case CAPI_PUT_MESSAGE:
			return(isdn_capi_rec_hl_msg(&c->parm.cmsg));
	        case ISDN_STAT_PROT:
	        case ISDN_STAT_REDIR:
                        if (divert_if)
				return(divert_if->stat_callback(c));
#endif
		default:
			rc = 1;
	}
	put_drv(drv);
	return rc;
}

/* ====================================================================== */

/*
 * Register a new ISDN interface
 */
int
register_isdn(isdn_if *iif)
{
	struct isdn_driver *drv;
	unsigned long flags;
	int drvidx;

	drv = kmalloc(sizeof(*drv), GFP_ATOMIC);
	if (!drv) {
		printk(KERN_WARNING "register_isdn: out of mem\n");
		goto fail;
	}
	memset(drv, 0, sizeof(*drv));

	atomic_set(&drv->refcnt, 0);
	spin_lock_init(&drv->lock);
	init_waitqueue_head(&drv->st_waitq);
	drv->fi.fsm = &drv_fsm;
	drv->fi.state = ST_DRV_NULL;
	drv->fi.debug = 1;
	drv->fi.userdata = drv;
	drv->fi.printdebug = drv_debug;

	spin_lock_irqsave(&drivers_lock, flags);
	for (drvidx = 0; drvidx < ISDN_MAX_DRIVERS; drvidx++)
		if (!drivers[drvidx])
			break;

	if (drvidx == ISDN_MAX_DRIVERS)
		goto fail_unlock;

	if (!strlen(iif->id))
		sprintf(iif->id, "line%d", drvidx);

	if (__isdn_drv_lookup(iif->id) >= 0)
		goto fail_unlock;

	strcpy(drv->id, iif->id);
	if (isdn_add_channels(drv, iif->channels))
		goto fail_unlock;

	drv->di = drvidx;
	drivers[drvidx] = get_drv(drv);
	spin_unlock_irqrestore(&drivers_lock, flags);

	fsm_event(&drv->fi, EV_DRV_REGISTER, iif);
	return 1;
	
 fail_unlock:
	spin_unlock_irqrestore(&drivers_lock, flags);
	kfree(drv);
 fail:
	return 0;
}

/* ====================================================================== */

#if defined(CONFIG_ISDN_DIVERSION) || defined(CONFIG_ISDN_DIVERSION_MODULE)
static isdn_divert_if *divert_if; /* = NULL */
#else
#define divert_if ((isdn_divert_if *) NULL)
#endif

static int isdn_wildmat(char *s, char *p);

static void
isdn_lock_driver(struct isdn_driver *drv)
{
	// FIXME don't ignore return value
	try_module_get(drv->interface->owner);
}

static void
isdn_unlock_driver(struct isdn_driver *drv)
{
	module_put(drv->interface->owner);
}

#if defined(ISDN_DEBUG_NET_DUMP) || defined(ISDN_DEBUG_MODEM_DUMP)
void
isdn_dumppkt(char *s, u_char * p, int len, int dumplen)
{
	int dumpc;

	printk(KERN_DEBUG "%s(%d) ", s, len);
	for (dumpc = 0; (dumpc < dumplen) && (len); len--, dumpc++)
		printk(" %02x", *p++);
	printk("\n");
}
#endif

/*
 * I picked the pattern-matching-functions from an old GNU-tar version (1.10)
 * It was originally written and put to PD by rs@mirror.TMC.COM (Rich Salz)
 */
static int
isdn_star(char *s, char *p)
{
	while (isdn_wildmat(s, p)) {
		if (*++s == '\0')
			return (2);
	}
	return (0);
}

/*
 * Shell-type Pattern-matching for incoming caller-Ids
 * This function gets a string in s and checks, if it matches the pattern
 * given in p.
 *
 * Return:
 *   0 = match.
 *   1 = no match.
 *   2 = no match. Would eventually match, if s would be longer.
 *
 * Possible Patterns:
 *
 * '?'     matches one character
 * '*'     matches zero or more characters
 * [xyz]   matches the set of characters in brackets.
 * [^xyz]  matches any single character not in the set of characters
 */

static int
isdn_wildmat(char *s, char *p)
{
	register int last;
	register int matched;
	register int reverse;
	register int nostar = 1;

	if (!(*s) && !(*p))
		return(1);
	for (; *p; s++, p++)
		switch (*p) {
			case '\\':
				/*
				 * Literal match with following character,
				 * fall through.
				 */
				p++;
			default:
				if (*s != *p)
					return (*s == '\0')?2:1;
				continue;
			case '?':
				/* Match anything. */
				if (*s == '\0')
					return (2);
				continue;
			case '*':
				nostar = 0;	
				/* Trailing star matches everything. */
				return (*++p ? isdn_star(s, p) : 0);
			case '[':
				/* [^....] means inverse character class. */
				if ((reverse = (p[1] == '^')))
					p++;
				for (last = 0, matched = 0; *++p && (*p != ']'); last = *p)
					/* This next line requires a good C compiler. */
					if (*p == '-' ? *s <= *++p && *s >= last : *s == *p)
						matched = 1;
				if (matched == reverse)
					return (1);
				continue;
		}
	return (*s == '\0')?0:nostar;
}

int isdn_msncmp( const char * msn1, const char * msn2 )
{
	char TmpMsn1[ ISDN_MSNLEN ];
	char TmpMsn2[ ISDN_MSNLEN ];
	char *p;

	for ( p = TmpMsn1; *msn1 && *msn1 != ':'; )  // Strip off a SPID
		*p++ = *msn1++;
	*p = '\0';

	for ( p = TmpMsn2; *msn2 && *msn2 != ':'; )  // Strip off a SPID
		*p++ = *msn2++;
	*p = '\0';

	return isdn_wildmat( TmpMsn1, TmpMsn2 );
}

static int
__drv_command(struct isdn_driver *drv, isdn_ctrl *c)
{
#ifdef ISDN_DEBUG_COMMAND
	switch (c->command) {
	case ISDN_CMD_SETL2: 
		printk(KERN_DEBUG "ISDN_CMD_SETL2 %d/%ld\n", c->driver, c->arg & 0xff); break;
	case ISDN_CMD_SETL3: 
		printk(KERN_DEBUG "ISDN_CMD_SETL3 %d/%ld\n", c->driver, c->arg & 0xff); break;
	case ISDN_CMD_DIAL: 
		printk(KERN_DEBUG "ISDN_CMD_DIAL %d/%ld\n", c->driver, c->arg & 0xff); break;
	case ISDN_CMD_ACCEPTD: 
		printk(KERN_DEBUG "ISDN_CMD_ACCEPTD %d/%ld\n", c->driver, c->arg & 0xff); break;
	case ISDN_CMD_ACCEPTB: 
		printk(KERN_DEBUG "ISDN_CMD_ACCEPTB %d/%ld\n", c->driver, c->arg & 0xff); break;
	case ISDN_CMD_HANGUP: 
		printk(KERN_DEBUG "ISDN_CMD_HANGUP %d/%ld\n", c->driver, c->arg & 0xff); break;
	case ISDN_CMD_CLREAZ: 
		printk(KERN_DEBUG "ISDN_CMD_CLREAZ %d/%ld\n", c->driver, c->arg & 0xff); break;
	case ISDN_CMD_SETEAZ: 
		printk(KERN_DEBUG "ISDN_CMD_SETEAZ %d/%ld\n", c->driver, c->arg & 0xff); break;
	default:
		printk(KERN_DEBUG "%s: cmd = %d\n", __FUNCTION__, c->command);
	}
#endif
	return drv->interface->command(c);
}

static int
__slot_command(struct isdn_slot *slot, isdn_ctrl *cmd)
{
	struct isdn_driver *drv = slot->drv;

	return __drv_command(drv, cmd);
}

/*
 * Begin of a CAPI like LL<->HL interface, currently used only for 
 * supplementary service (CAPI 2.0 part III)
 */
#include <linux/isdn/capicmd.h>

int
isdn_capi_rec_hl_msg(capi_msg *cm)
{
	switch(cm->Command) {
	case CAPI_FACILITY:
		/* in the moment only handled in tty */
		return isdn_tty_capi_facility(cm);
	default:
		return -1;
	}
}

/*
 * Get integer from char-pointer, set pointer to end of number
 */
int
isdn_getnum(char **p)
{
	int v = -1;

	while (*p[0] >= '0' && *p[0] <= '9')
		v = ((v < 0) ? 0 : (v * 10)) + (int) ((*p[0]++) - '0');
	return v;
}

static struct isdn_slot *
get_slot_by_minor(int minor)
{
	int di, ch;
	struct isdn_driver *drv;

	for (di = 0; di < ISDN_MAX_DRIVERS; di++) {
		drv = get_drv_by_nr(di);
		if (!drv)
			continue;

		for (ch = 0; ch < drv->channels; ch++) {
			if (minor-- == 0)
				goto found;
		}
		put_drv(drv);
	}
	return NULL;

 found:
	return drv->slots + ch;
}

static inline void
put_slot(struct isdn_slot *slot)
{
	put_drv(slot->drv);
}

static char *
isdn_statstr(void)
{
	static char istatbuf[2048];
	struct isdn_slot *slot;
	char *p;
	int i;

	sprintf(istatbuf, "idmap:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		slot = get_slot_by_minor(i);
		if (slot) {
			sprintf(p, "%s ", slot->drv->id);
			put_slot(slot);
		} else {
			sprintf(p, "- ");
		}
		p = istatbuf + strlen(istatbuf);
	}
	sprintf(p, "\nchmap:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		slot = get_slot_by_minor(i);
		if (slot) {
			sprintf(p, "%d ", slot->ch);
			put_slot(slot);
		} else {
			sprintf(p, "-1 ");
		}
		p = istatbuf + strlen(istatbuf);
	}
	sprintf(p, "\ndrmap:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		slot = get_slot_by_minor(i);
		if (slot) {
			sprintf(p, "%d ", slot->di);
			put_slot(slot);
		} else {
			sprintf(p, "-1 ");
		}
	}
	sprintf(p, "\nusage:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		slot = get_slot_by_minor(i);
		if (slot) {
			sprintf(p, "%d ", slot->usage);
			put_slot(slot);
		} else {
			sprintf(p, "0 ");
		}
		p = istatbuf + strlen(istatbuf);
	}
	sprintf(p, "\nflags:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_DRIVERS; i++) {
		slot = get_slot_by_minor(i);
		if (slot) {
			sprintf(p, "0 ");
			put_slot(slot);
		} else {
			sprintf(p, "? ");
		}
		p = istatbuf + strlen(istatbuf);
	}
	sprintf(p, "\nphone:\t");
	p = istatbuf + strlen(istatbuf);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		slot = get_slot_by_minor(i);
		if (slot) {
			sprintf(p, "%s ", slot->num);
			put_slot(slot);
		} else {
			sprintf(p, " ");
		}
		p = istatbuf + strlen(istatbuf);
	}
	sprintf(p, "\n");
	return istatbuf;
}

/* 
 * /dev/isdninfo
 */

void
isdn_info_update(void)
{
	infostruct *p = isdndev->infochain;

	while (p) {
		*(p->private) = 1;
		p = (infostruct *) p->next;
	}
	wake_up_interruptible(&(isdndev->info_waitq));
}

static int
isdn_status_open(struct inode *ino, struct file *filep)
{
	infostruct *p;
	
	p = kmalloc(sizeof(infostruct), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->next = (char *) isdndev->infochain;
	p->private = (char *) &(filep->private_data);
	isdndev->infochain = p;
	/* At opening we allow a single update */
	filep->private_data = (char *) 1;

	return 0;
}

static int
isdn_status_release(struct inode *ino, struct file *filep)
{
	infostruct *p = isdndev->infochain;
	infostruct *q = NULL;
	
	lock_kernel();

	while (p) {
		if (p->private == (char *) &(filep->private_data)) {
			if (q)
				q->next = p->next;
			else
				isdndev->infochain = (infostruct *) (p->next);
			kfree(p);
			goto out;
		}
		q = p;
		p = (infostruct *) (p->next);
	}
	printk(KERN_WARNING "isdn: No private data while closing isdnctrl\n");

 out:
	unlock_kernel();
	return 0;
}

static ssize_t
isdn_status_read(struct file *file, char *buf, size_t count, loff_t * off)
{
	int retval;
	size_t len = 0;
	char *p;

	if (off != &file->f_pos)
		return -ESPIPE;

	if (!file->private_data) {
		if (file->f_flags & O_NONBLOCK)
			return  -EAGAIN;
		interruptible_sleep_on(&(isdndev->info_waitq));
	}
	lock_kernel();
	p = isdn_statstr();
	file->private_data = 0;
	if ((len = strlen(p)) <= count) {
		if (copy_to_user(buf, p, len)) {
			retval = -EFAULT;
			goto out;
		}
		*off += len;
		retval = len;
		goto out;
	}
	retval = 0;
	goto out;

 out:
	unlock_kernel();
	return retval;
}

static ssize_t
isdn_status_write(struct file *file, const char *buf, size_t count, loff_t *off)
{
	return -EPERM;
}

static unsigned int
isdn_status_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(file, &(isdndev->info_waitq), wait);
	lock_kernel();
	if (file->private_data)
		mask |= POLLIN | POLLRDNORM;
	unlock_kernel();
	return mask;
}

static int
isdn_status_ioctl(struct inode *inode, struct file *file, uint cmd, ulong arg)
{
	static unsigned long zero_ul = 0UL;
	int ret;
	struct isdn_slot *slot;

	switch (cmd) {
	case IIOCGETDVR:
		return (TTY_DV +
			(NET_DV << 8) +
			(INF_DV << 16));
	case IIOCGETCPS:
		if (arg) {
			ulong *p = (ulong *) arg;
			int i;
			if ((ret = verify_area(VERIFY_WRITE, (void *) arg,
					       sizeof(ulong) * ISDN_MAX_CHANNELS * 2)))
				return ret;
			for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
				slot = get_slot_by_minor(i);
				if (slot) {
					put_user(slot->ibytes, p++);
					put_user(slot->obytes, p++);
					put_slot(slot);
				} else {
					put_user(zero_ul, p++);
					put_user(zero_ul, p++);
				}
			}
			return 0;
		} else
			return -EINVAL;
		break;
	case IIOCNETGPN:
		return isdn_net_ioctl(inode, file, cmd, arg);
	default:
		return -EINVAL;
	}
}

static struct file_operations isdn_status_fops =
{
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= isdn_status_read,
	.write		= isdn_status_write,
	.poll		= isdn_status_poll,
	.ioctl		= isdn_status_ioctl,
	.open		= isdn_status_open,
	.release	= isdn_status_release,
};

/*
 * /dev/isdnctrlX
 */

static int
isdn_ctrl_open(struct inode *ino, struct file *file)
{
	unsigned int minor = iminor(ino);
	struct isdn_slot *slot = get_slot_by_minor(minor - ISDN_MINOR_CTRL);

	if (!slot)
		return -ENODEV;

	isdn_lock_driver(slot->drv);
	file->private_data = slot;

	return 0;
}

static int
isdn_ctrl_release(struct inode *ino, struct file *file)
{
	struct isdn_slot *slot = file->private_data;

	if (isdndev->profd == current)
		isdndev->profd = NULL;

	isdn_unlock_driver(slot->drv);
	put_slot(slot);

	return 0;
}

static ssize_t
isdn_ctrl_read(struct file *file, char *buf, size_t count, loff_t * off)
{
	struct isdn_slot *slot = file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	size_t len = 0;

	if (off != &file->f_pos)
		return -ESPIPE;

	if (!slot->drv->interface->readstat) {
		isdn_BUG();
		return 0;
	}
 	add_wait_queue(&slot->drv->st_waitq, &wait);
	for (;;) {
		spin_lock_irqsave(&stat_lock, flags);
		len = slot->drv->stavail;
		spin_unlock_irqrestore(&stat_lock, flags);
		if (len > 0)
			break;
		if (signal_pending(current)) {
			len = -ERESTARTSYS;
			break;
		}
		if (file->f_flags & O_NONBLOCK) {
			len = -EAGAIN;
			break;
		}
		schedule();
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&slot->drv->st_waitq, &wait);
	
	if (len < 0)
		return len;
	
	if (count > len)
		count = len;
		
	len = slot->drv->interface->readstat(buf, count, 1, slot->di, 
					     slot->ch);

	spin_lock_irqsave(&stat_lock, flags);
	if (len) {
		slot->drv->stavail -= len;
	} else {
		isdn_BUG();
		slot->drv->stavail = 0;
	}
	spin_unlock_irqrestore(&stat_lock, flags);

	*off += len;
	return len;
}

static ssize_t
isdn_ctrl_write(struct file *file, const char *buf, size_t count, loff_t *off)
{
	struct isdn_slot *slot = file->private_data;
	int retval;

	if (off != &file->f_pos) {
		retval = -ESPIPE;
		goto out;
	}
	if (!slot->drv->interface->writecmd) {
		retval = -EINVAL;
		goto out;
	}
	retval = slot->drv->interface->writecmd(buf, count, 1, slot->di, 
						slot->ch);

 out:
	return retval;
}

static unsigned int
isdn_ctrl_poll(struct file *file, poll_table *wait)
{
	struct isdn_slot *slot = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &slot->drv->st_waitq, wait);
	mask = POLLOUT | POLLWRNORM;
	if (slot->drv->stavail)
		mask |= POLLIN | POLLRDNORM;

	return mask;
}


static int
isdn_ctrl_ioctl(struct inode *inode, struct file *file, uint cmd, ulong arg)
{
	isdn_ctrl c;
	int drvidx;
	int ret;
	int i;
	char *p;
	/* save stack space */
	union {
		char bname[20];
		isdn_ioctl_struct iocts;
	} iocpar;

#define iocts iocpar.iocts
#define bname iocpar.bname

/*
 * isdn net devices manage lots of configuration variables as linked lists.
 * Those lists must only be manipulated from user space. Some of the ioctl's
 * service routines access user space and are not atomic. Therefor, ioctl's
 * manipulating the lists and ioctl's sleeping while accessing the lists
 * are serialized by means of a semaphore.
 */
	switch (cmd) {
	case IIOCNETAIF:
	case IIOCNETASL:
	case IIOCNETDIF:
	case IIOCNETSCF:
	case IIOCNETGCF:
	case IIOCNETANM:
	case IIOCNETGNM:
	case IIOCNETDNM:
	case IIOCNETDIL:
	case IIOCNETALN:
	case IIOCNETDLN:
	case IIOCNETHUP:
		return isdn_net_ioctl(inode, file, cmd, arg);
	case IIOCSETVER:
		isdndev->net_verbose = arg;
		printk(KERN_INFO "isdn: Verbose-Level is %d\n", isdndev->net_verbose);
		return 0;
	case IIOCSETGST:
		if (arg) {
			isdndev->global_flags |= ISDN_GLOBAL_STOPPED;
			isdn_net_hangup_all();
		} else {
			isdndev->global_flags &= ~ISDN_GLOBAL_STOPPED;
		}
		return 0;
	case IIOCSETBRJ:
		drvidx = -1;
		if (arg) {
			char *p;
			if (copy_from_user((char *) &iocts, (char *) arg,
					   sizeof(isdn_ioctl_struct)))
				return -EFAULT;
			if (strlen(iocts.drvid)) {
				if ((p = strchr(iocts.drvid, ',')))
					*p = 0;
				drvidx = isdn_drv_lookup(iocts.drvid);
			}
		}
		if (drvidx == -1)
			return -ENODEV;
		if (iocts.arg)
			drivers[drvidx]->flags |= DRV_FLAG_REJBUS;
		else
			drivers[drvidx]->flags &= ~DRV_FLAG_REJBUS;
		return 0;
	case IIOCSIGPRF:
		isdndev->profd = current;
		return 0;
		break;
	case IIOCGETPRF:
		/* Get all Modem-Profiles */
		if (arg) {
			char *p = (char *) arg;
			int i;

			for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
				if (copy_to_user(p, isdn_mdm.info[i].emu.profile,
						 ISDN_MODEM_NUMREG))
					return -EFAULT;
				p += ISDN_MODEM_NUMREG;
				if (copy_to_user(p, isdn_mdm.info[i].emu.pmsn, ISDN_MSNLEN))
					return -EFAULT;
				p += ISDN_MSNLEN;
				if (copy_to_user(p, isdn_mdm.info[i].emu.plmsn, ISDN_LMSNLEN))
					return -EFAULT;
				p += ISDN_LMSNLEN;
			}
			return (ISDN_MODEM_NUMREG + ISDN_MSNLEN + ISDN_LMSNLEN) * ISDN_MAX_CHANNELS;
		} else
			return -EINVAL;
		break;
	case IIOCSETPRF:
		/* Set all Modem-Profiles */
		if (arg) {
			char *p = (char *) arg;
			int i;

			for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
				if (copy_from_user(isdn_mdm.info[i].emu.profile, p,
						   ISDN_MODEM_NUMREG))
					return -EFAULT;
				p += ISDN_MODEM_NUMREG;
				if (copy_from_user(isdn_mdm.info[i].emu.plmsn, p, ISDN_LMSNLEN))
					return -EFAULT;
				p += ISDN_LMSNLEN;
				if (copy_from_user(isdn_mdm.info[i].emu.pmsn, p, ISDN_MSNLEN))
					return -EFAULT;
				p += ISDN_MSNLEN;
			}
			return 0;
		} else
			return -EINVAL;
		break;
	case IIOCSETMAP:
	case IIOCGETMAP:
		/* Set/Get MSN->EAZ-Mapping for a driver */
		if (arg) {

			if (copy_from_user((char *) &iocts,
					   (char *) arg,
					   sizeof(isdn_ioctl_struct)))
				return -EFAULT;
			drvidx = isdn_drv_lookup(iocts.drvid);
			if (drvidx == -1)
				return -ENODEV;
			if (cmd == IIOCSETMAP) {
				int loop = 1;

				p = (char *) iocts.arg;
				i = 0;
				while (loop) {
					int j = 0;

					while (1) {
						if ((ret = get_user(bname[j], p++)))
							return ret;
						switch (bname[j]) {
						case '\0':
							loop = 0;
							/* Fall through */
						case ',':
							bname[j] = '\0';
							strcpy(drivers[drvidx]->msn2eaz[i], bname);
							j = ISDN_MSNLEN;
							break;
						default:
							j++;
						}
						if (j >= ISDN_MSNLEN)
							break;
					}
					if (++i > 9)
						break;
				}
			} else {
				p = (char *) iocts.arg;
				for (i = 0; i < 10; i++) {
					sprintf(bname, "%s%s",
						strlen(drivers[drvidx]->msn2eaz[i]) ?
						drivers[drvidx]->msn2eaz[i] : "_",
						(i < 9) ? "," : "\0");
					if (copy_to_user(p, bname, strlen(bname) + 1))
						return -EFAULT;
					p += strlen(bname);
				}
			}
			return 0;
		} else
			return -EINVAL;
	case IIOCDBGVAR:
		if (arg) {
			if (copy_to_user((char *) arg, (char *) &isdndev, sizeof(ulong)))
				return -EFAULT;
			return 0;
		} else
			return -EINVAL;
		break;
	default:
		if ((cmd & IIOCDRVCTL) == IIOCDRVCTL)
			cmd = ((cmd >> _IOC_NRSHIFT) & _IOC_NRMASK) & ISDN_DRVIOCTL_MASK;
		else
			return -EINVAL;
		if (arg) {
			if (copy_from_user((char *) &iocts, (char *) arg, sizeof(isdn_ioctl_struct)))
				return -EFAULT;
			drvidx = isdn_drv_lookup(iocts.drvid);
			if (drvidx == -1)
				return -ENODEV;
			if ((ret = verify_area(VERIFY_WRITE, (void *) arg,
					       sizeof(isdn_ioctl_struct))))
				return ret;
			c.driver = drvidx;
			c.command = ISDN_CMD_IOCTL;
			c.arg = cmd;
			memcpy(c.parm.num, (char *) &iocts.arg, sizeof(ulong));
			ret = __drv_command(drivers[drvidx], &c);
			memcpy((char *) &iocts.arg, c.parm.num, sizeof(ulong));
			if (copy_to_user((char *) arg, &iocts, sizeof(isdn_ioctl_struct)))
				return -EFAULT;
			return ret;
		} else
			return -EINVAL;
	}
#undef iocts
#undef bname
}

static struct file_operations isdn_ctrl_fops =
{
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= isdn_ctrl_read,
	.write		= isdn_ctrl_write,
	.poll		= isdn_ctrl_poll,
	.ioctl		= isdn_ctrl_ioctl,
	.open		= isdn_ctrl_open,
	.release	= isdn_ctrl_release,
};


/*
 * file_operations for major 45, /dev/isdn*
 * stolen from drivers/char/misc.c
 */

static int
isdn_open(struct inode * inode, struct file * file)
{
	int minor = iminor(inode);
	int err = -ENODEV;
	struct file_operations *old_fops, *new_fops = NULL;
	
	if (minor >= ISDN_MINOR_CTRL && minor <= ISDN_MINOR_CTRLMAX)
		new_fops = fops_get(&isdn_ctrl_fops);
#ifdef CONFIG_ISDN_PPP
	else if (minor >= ISDN_MINOR_PPP && minor <= ISDN_MINOR_PPPMAX)
		new_fops = fops_get(&isdn_ppp_fops);
#endif
	else if (minor == ISDN_MINOR_STATUS)
		new_fops = fops_get(&isdn_status_fops);

	if (!new_fops)
		goto out;

	err = 0;
	old_fops = file->f_op;
	file->f_op = new_fops;
	if (file->f_op->open) {
		err = file->f_op->open(inode,file);
		if (err) {
			fops_put(file->f_op);
			file->f_op = fops_get(old_fops);
		}
	}
	fops_put(old_fops);
	
 out:
	return err;
}

static struct file_operations isdn_fops =
{
	.owner		= THIS_MODULE,
	.open		= isdn_open,
};

char *
isdn_map_eaz2msn(char *msn, int di)
{
	struct isdn_driver *this = drivers[di];
	int i;

	if (strlen(msn) == 1) {
		i = msn[0] - '0';
		if ((i >= 0) && (i <= 9))
			if (strlen(this->msn2eaz[i]))
				return (this->msn2eaz[i]);
	}
	return (msn);
}

/*
 * Find an unused ISDN-channel, whose feature-flags match the
 * given L2- and L3-protocols.
 */
struct isdn_slot *
isdn_get_free_slot(int usage, int l2_proto, int l3_proto,
		   int pre_dev, int pre_chan, char *msn)
{
	struct isdn_driver *drv;
	struct isdn_slot *slot;
	int di, ch;
	unsigned long flags;
	unsigned long features;

	features = ((1 << l2_proto) | (0x10000 << l3_proto));

	for (di = 0; di < ISDN_MAX_DRIVERS; di++) {
		if (pre_dev >= 0 && pre_dev != di)
			continue;

		drv = get_drv_by_nr(di);
		if (!drv)
			continue;

		if (drv->fi.state != ST_DRV_RUNNING)
			goto put;

		if ((drv->features & features) != features)
			goto put;

		spin_lock_irqsave(&drv->lock, flags);
		for (ch = 0; ch < drv->channels; ch++) {
			if (pre_chan >= 0 && pre_chan != ch)
				continue;

			slot = &drv->slots[ch];

			if (!USG_NONE(slot->usage))
				continue;

			if (slot->usage & ISDN_USAGE_DISABLED)
				continue;

			if (strcmp(isdn_map_eaz2msn(msn, drv->di), "-") == 0)
				continue;

			goto found;
			
		}
		spin_unlock_irqrestore(&drv->lock, flags);

	put:
		put_drv(drv);
	}
	return NULL;

 found:
	slot->usage = usage;
	spin_unlock_irqrestore(&drv->lock, flags);

	isdn_info_update();
	fsm_event(&slot->fi, EV_SLOT_BIND, NULL);
	return slot;
}

/*
 * Set state of ISDN-channel to 'unused'
 */
void
isdn_slot_free(struct isdn_slot *slot)
{
	fsm_event(&slot->fi, EV_SLOT_UNBIND, NULL);
}

/*
 * Return: length of data on success, -ERRcode on failure.
 */
int
isdn_slot_write(struct isdn_slot *slot, struct sk_buff *skb)
{
	return fsm_event(&slot->fi, EV_DATA_REQ, skb);
}

static int
isdn_add_channels(struct isdn_driver *drv, int n)
{
	struct isdn_slot *slot;
	int ch;

       	if (n < 1)
		return 0;

	if (isdndev->channels + n > ISDN_MAX_CHANNELS) {
		printk(KERN_WARNING "register_isdn: Max. %d channels supported\n",
		       ISDN_MAX_CHANNELS);
		return -EBUSY;
	}
	isdndev->channels += n;
	drv->slots = kmalloc(sizeof(struct isdn_slot) * n, GFP_ATOMIC);
	if (!drv->slots)
		return -ENOMEM;
	memset(drv->slots, 0, sizeof(struct isdn_slot) * n);
	for (ch = 0; ch < n; ch++) {
		slot = drv->slots + ch;

		slot->ch = ch;
		slot->di = drv->di;
		slot->drv = drv;
		strcpy(slot->num, "???");
		slot->fi.fsm = &slot_fsm;
		slot->fi.state = ST_SLOT_NULL;
		slot->fi.debug = 1;
		slot->fi.userdata = slot;
		slot->fi.printdebug = slot_debug;
	}
	drv->channels = n;
	return 0;
}

/*
 * Low-level-driver registration
 */

#if defined(CONFIG_ISDN_DIVERSION) || defined(CONFIG_ISDN_DIVERSION_MODULE)

/*
 * map_drvname
 */
static char *map_drvname(int di)
{
	if ((di < 0) || (di >= ISDN_MAX_DRIVERS)) 
		return(NULL);
	return(isdndev->drvid[di]); /* driver name */
}

/*
 * map_namedrv
 */
static int map_namedrv(char *id)
{
	int i;

	for (i = 0; i < ISDN_MAX_DRIVERS; i++) {
		if (!strcmp(dev->drvid[i],id)) 
			return(i);
	}
	return(-1);
}

/*
 * DIVERT_REG_NAME
 */
int DIVERT_REG_NAME(isdn_divert_if *i_div)
{
	if (i_div->if_magic != DIVERT_IF_MAGIC) 
		return(DIVERT_VER_ERR);
	switch (i_div->cmd) {
		case DIVERT_CMD_REL:
			if (divert_if != i_div) 
				return(DIVERT_REL_ERR);
			divert_if = NULL; /* free interface */
			MOD_DEC_USE_COUNT;
			return(DIVERT_NO_ERR);
		case DIVERT_CMD_REG:
			if (divert_if) 
				return(DIVERT_REG_ERR);
			i_div->ll_cmd = isdn_command; /* set command function */
			i_div->drv_to_name = map_drvname; 
			i_div->name_to_drv = map_namedrv; 
			MOD_INC_USE_COUNT;
			divert_if = i_div; /* remember interface */
			return(DIVERT_NO_ERR);
		default:
			return(DIVERT_CMD_ERR);   
	}
}

EXPORT_SYMBOL(DIVERT_REG_NAME);

#endif


EXPORT_SYMBOL(register_isdn);
#ifdef CONFIG_ISDN_PPP
EXPORT_SYMBOL(isdn_ppp_register_compressor);
EXPORT_SYMBOL(isdn_ppp_unregister_compressor);
#endif

int
isdn_slot_maxbufsize(struct isdn_slot *slot)
{
	return slot->drv->maxbufsize;
}

int
isdn_slot_hdrlen(struct isdn_slot *slot)
{
	return slot->drv->interface->hl_hdrlen;
}

char *
isdn_slot_map_eaz2msn(struct isdn_slot *slot, char *msn)
{
	return isdn_map_eaz2msn(msn, slot->di);
}

int
isdn_slot_command(struct isdn_slot *slot, int cmd, isdn_ctrl *ctrl)
{
	ctrl->command = cmd;
	ctrl->driver = slot->di;

	switch (cmd) {
	case ISDN_CMD_SETL2:
	case ISDN_CMD_SETL3:
	case ISDN_CMD_PROT_IO:
		ctrl->arg &= ~0xff; ctrl->arg |= slot->ch;
		break;
	case ISDN_CMD_DIAL:
		if (isdndev->global_flags & ISDN_GLOBAL_STOPPED)
			return -EBUSY;

		/* fall through */
	default:
		ctrl->arg = slot->ch;
		break;
	}
	switch (cmd) {
	case ISDN_CMD_CLREAZ:
		return fsm_event(&slot->fi, EV_CMD_CLREAZ, ctrl);
	case ISDN_CMD_SETEAZ:
		return fsm_event(&slot->fi, EV_CMD_SETEAZ, ctrl);
	case ISDN_CMD_SETL2:
		return fsm_event(&slot->fi, EV_CMD_SETL2, ctrl);
	case ISDN_CMD_SETL3:
		return fsm_event(&slot->fi, EV_CMD_SETL3, ctrl);
	case ISDN_CMD_DIAL:
		return fsm_event(&slot->fi, EV_CMD_DIAL, ctrl);
	case ISDN_CMD_ACCEPTD:
		return fsm_event(&slot->fi, EV_CMD_ACCEPTD, ctrl);
	case ISDN_CMD_ACCEPTB:
		return fsm_event(&slot->fi, EV_CMD_ACCEPTB, ctrl);
	case ISDN_CMD_HANGUP:
		return fsm_event(&slot->fi, EV_CMD_HANGUP, ctrl);
	}
	HERE;
	return -1;
}

int
isdn_slot_dial(struct isdn_slot *slot, struct dial_info *dial)
{
	isdn_ctrl cmd;
	int retval;
	char *msn = isdn_slot_map_eaz2msn(slot, dial->msn);

	/* check for DOV */
	if (dial->si1 == 7 && tolower(dial->phone[0]) == 'v') { /* DOV call */
		dial->si1 = 1;
		dial->phone++; /* skip v/V */
	}

	strcpy(slot->num, dial->phone);
	slot->usage |= ISDN_USAGE_OUTGOING;
	isdn_info_update();

	retval = isdn_slot_command(slot, ISDN_CMD_CLREAZ, &cmd);
	if (retval)
		return retval;

	strcpy(cmd.parm.num, msn);
	retval = isdn_slot_command(slot, ISDN_CMD_SETEAZ, &cmd);

	cmd.arg = dial->l2_proto << 8;
	cmd.parm.fax = dial->fax;
	retval = isdn_slot_command(slot, ISDN_CMD_SETL2, &cmd);
	if (retval)
		return retval;

	cmd.arg = dial->l3_proto << 8;
	retval = isdn_slot_command(slot, ISDN_CMD_SETL3, &cmd);
	if (retval)
		return retval;

	cmd.parm.setup.si1 = dial->si1;
	cmd.parm.setup.si2 = dial->si2;
	strcpy(cmd.parm.setup.eazmsn, msn);
	strcpy(cmd.parm.setup.phone, dial->phone);

	printk(KERN_INFO "ISDN: Dialing %s -> %s (SI %d/%d) (B %d/%d)\n",
	       cmd.parm.setup.eazmsn, cmd.parm.setup.phone,
	       cmd.parm.setup.si1, cmd.parm.setup.si2,
	       dial->l2_proto, dial->l3_proto);

	return isdn_slot_command(slot, ISDN_CMD_DIAL, &cmd);
}

int
isdn_hard_header_len(void)
{
	int drvidx;
	int max = 0;
	
	for (drvidx = 0; drvidx < ISDN_MAX_DRIVERS; drvidx++) {
		if (drivers[drvidx] && 
		    max < drivers[drvidx]->interface->hl_hdrlen) {
			max = drivers[drvidx]->interface->hl_hdrlen;
		}
	}
	return max;
}

static void isdn_init_devfs(void)
{
	devfs_mk_dir("isdn");

#ifdef CONFIG_ISDN_PPP
{
	int i;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		devfs_mk_cdev(MKDEV(ISDN_MAJOR, ISDN_MINOR_PPP + i),
				0600 | S_IFCHR, "isdn/ippp%d", i);
}
#endif

	devfs_mk_cdev(MKDEV(ISDN_MAJOR, ISDN_MINOR_STATUS),
			0600 | S_IFCHR, "isdn/isdninfo");
	devfs_mk_cdev(MKDEV(ISDN_MAJOR, ISDN_MINOR_CTRL),
			0600 | S_IFCHR, "isdn/isdnctrl");
}

static void isdn_cleanup_devfs(void)
{
#ifdef CONFIG_ISDN_PPP
	int i;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) 
		devfs_remove("isdn/ippp%d", i);
#endif
	devfs_remove("isdn/isdninfo");
	devfs_remove("isdn/isdnctrl");
	devfs_remove("isdn");
}

/*
 * Allocate and initialize all data, register modem-devices
 */
static int __init isdn_init(void)
{
	int retval;

	retval = fsm_new(&slot_fsm);
	if (retval)
		goto err;

	retval = fsm_new(&drv_fsm);
	if (retval)
		goto err_slot_fsm;

	isdndev = vmalloc(sizeof(*isdndev));
	if (!isdndev) {
		retval = -ENOMEM;
		goto err_drv_fsm;
	}
	memset(isdndev, 0, sizeof(*isdndev));
	init_MUTEX(&isdndev->sem);
	init_waitqueue_head(&isdndev->info_waitq);

	retval = register_chrdev(ISDN_MAJOR, "isdn", &isdn_fops);
	if (retval) {
		printk(KERN_WARNING "isdn: Could not register control devices\n");
		goto err_vfree;
	}
	isdn_init_devfs();
	retval = isdn_tty_init();
	if (retval < 0) {
		printk(KERN_WARNING "isdn: Could not register tty devices\n");
		goto err_cleanup_devfs;
	}
#ifdef CONFIG_ISDN_PPP
	retval = isdn_ppp_init();
	if (retval < 0) {
		printk(KERN_WARNING "isdn: Could not create PPP-device-structs\n");
		goto err_tty_modem;
	}
#endif                          /* CONFIG_ISDN_PPP */

	isdn_net_lib_init();
	printk(KERN_NOTICE "ISDN subsystem initialized\n");
	isdn_info_update();
	return 0;

#ifdef CONFIG_ISDN_PPP
 err_tty_modem:
	isdn_tty_exit();
#endif
 err_cleanup_devfs:
	isdn_cleanup_devfs();
	unregister_chrdev(ISDN_MAJOR, "isdn");
 err_vfree:
	vfree(isdndev);
 err_drv_fsm:
	fsm_free(&drv_fsm);
 err_slot_fsm:
	fsm_free(&slot_fsm);
 err:
	return retval;
}

/*
 * Unload module
 */
static void __exit isdn_exit(void)
{
#ifdef CONFIG_ISDN_PPP
	isdn_ppp_cleanup();
#endif
	isdn_net_lib_exit();

	isdn_tty_exit();
	unregister_chrdev(ISDN_MAJOR, "isdn");
	isdn_cleanup_devfs();
	vfree(isdndev);
	fsm_free(&drv_fsm);
	fsm_free(&slot_fsm);
}

module_init(isdn_init);
module_exit(isdn_exit);

static void
isdn_v110_add_features(struct isdn_driver *drv)
{
	unsigned long features = drv->features >> ISDN_FEATURE_L2_SHIFT;

	if (features & ISDN_FEATURE_L2_TRANS)
		drv->features |= (ISDN_FEATURE_L2_V11096|
				  ISDN_FEATURE_L2_V11019|
				  ISDN_FEATURE_L2_V11038) << 
			ISDN_FEATURE_L2_SHIFT;
}

static void
__isdn_v110_open(struct isdn_slot *slot)
{
	if (!slot->iv110.v110emu)
		return;

	isdn_v110_open(slot, &slot->iv110);
}

static void
__isdn_v110_close(struct isdn_slot *slot)
{
	if (!slot->iv110.v110emu)
		return;

	isdn_v110_close(slot, &slot->iv110);
}

static void
__isdn_v110_bsent(struct isdn_slot *slot, int pr, isdn_ctrl *c)
{
	if (!slot->iv110.v110emu) {
		do_event_cb(slot, pr, c);
		return;
	}
	isdn_v110_bsent(slot, &slot->iv110);
}

/*
 * Intercept command from Linklevel to Lowlevel.
 * If layer 2 protocol is V.110 and this is not supported by current
 * lowlevel-driver, use driver's transparent mode and handle V.110 in
 * linklevel instead.
 */
static void
isdn_v110_setl2(struct isdn_slot *slot, isdn_ctrl *cmd)
{
	struct isdn_driver *drv = slot->drv;

	unsigned long l2prot = (cmd->arg >> 8) & 255;
	unsigned long l2_feature = 1 << l2prot;
	unsigned long features = drv->interface->features >> 
		ISDN_FEATURE_L2_SHIFT;
	
	switch (l2prot) {
	case ISDN_PROTO_L2_V11096:
	case ISDN_PROTO_L2_V11019:
	case ISDN_PROTO_L2_V11038:
		/* If V.110 requested, but not supported by
		 * HL-driver, set emulator-flag and change
		 * Layer-2 to transparent
		 */
		if (!(features & l2_feature)) {
			slot->iv110.v110emu = l2prot;
			cmd->arg = (cmd->arg & 255) |
				(ISDN_PROTO_L2_TRANS << 8);
		} else
			slot->iv110.v110emu = 0;
	}
}

static int
isdn_v110_data_ind(struct isdn_slot *slot, struct sk_buff *skb)
{
	if (!slot->iv110.v110emu)
		goto recv;
		
	skb = isdn_v110_decode(slot->iv110.v110, skb);
	if (!skb)
		return 0;

recv:
	if (slot->event_cb)
		slot->event_cb(slot, EV_DATA_IND, skb);
	return 0;
}

static int
isdn_v110_data_req(struct isdn_slot *slot, struct sk_buff *skb)
{
	int retval, v110_ret;
	struct sk_buff *nskb = NULL;

	if (!slot->iv110.v110emu)
		return isdn_writebuf_skb(slot, skb);

	atomic_inc(&slot->iv110.v110use);
	nskb = isdn_v110_encode(slot->iv110.v110, skb);
	atomic_dec(&slot->iv110.v110use);
	if (!nskb)
		return -ENOMEM;

	v110_ret = *(int *)nskb->data;
	skb_pull(nskb, sizeof(int));
	if (!nskb->len) {
		dev_kfree_skb(nskb);
		return v110_ret;
	}
	
	retval = isdn_writebuf_skb(slot, nskb);
	if (retval <= 0) {
		dev_kfree_skb(nskb);
		return retval;
	}
	dev_kfree_skb(skb);

	atomic_inc(&slot->iv110.v110use);
	slot->iv110.v110->skbuser++;
	atomic_dec(&slot->iv110.v110use);

	/* For V.110 return unencoded data length */
	return v110_ret;
}
