/* $Id: kcapi.c,v 1.21.6.8 2001/09/23 22:24:33 kai Exp $
 * 
 * Kernel CAPI 2.0 Module
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * Copyright 2002 by Kai Germaschewski <kai@germaschewski.name>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#define CONFIG_AVMB1_COMPAT

#include "kcapi.h"
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#ifdef CONFIG_AVMB1_COMPAT
#include <linux/b1lli.h>
#endif

static char *revision = "$Revision: 1.21.6.8 $";

/* ------------------------------------------------------------- */

static int showcapimsgs = 0;

MODULE_DESCRIPTION("CAPI4Linux: kernel CAPI layer");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");
MODULE_PARM(showcapimsgs, "i");

/* ------------------------------------------------------------- */

struct capi_notifier {
	struct capi_notifier *next;
	unsigned int cmd;
	u32 controller;
	u16 applid;
	u32 ncci;
};

/* ------------------------------------------------------------- */

static struct capi_version driver_version = {2, 0, 1, 1<<4};
static char driver_serial[CAPI_SERIAL_LEN] = "0004711";
static char capi_manufakturer[64] = "AVM Berlin";

#define NCCI2CTRL(ncci)    (((ncci) >> 24) & 0x7f)

LIST_HEAD(capi_drivers);
spinlock_t capi_drivers_lock = SPIN_LOCK_UNLOCKED;

struct capi20_appl *capi_applications[CAPI_MAXAPPL];
struct capi_ctr *capi_cards[CAPI_MAXCONTR];

static int ncards;
static struct sk_buff_head recv_queue;

static struct work_struct tq_state_notify;
static struct work_struct tq_recv_notify;

/* -------- controller ref counting -------------------------------------- */

static inline struct capi_ctr *
capi_ctr_get(struct capi_ctr *card)
{
	if (!try_module_get(card->owner))
		return NULL;
	return card;
}

static inline void
capi_ctr_put(struct capi_ctr *card)
{
	module_put(card->owner);
}

/* ------------------------------------------------------------- */

static inline struct capi_ctr *get_capi_ctr_by_nr(u16 contr)
{
	if (contr - 1 >= CAPI_MAXCONTR)
		return NULL;

	return capi_cards[contr - 1];
}

static inline struct capi20_appl *get_capi_appl_by_nr(u16 applid)
{
	if (applid - 1 >= CAPI_MAXAPPL)
		return NULL;

	return capi_applications[applid - 1];
}

/* -------- util functions ------------------------------------ */

static inline int capi_cmd_valid(u8 cmd)
{
	switch (cmd) {
	case CAPI_ALERT:
	case CAPI_CONNECT:
	case CAPI_CONNECT_ACTIVE:
	case CAPI_CONNECT_B3_ACTIVE:
	case CAPI_CONNECT_B3:
	case CAPI_CONNECT_B3_T90_ACTIVE:
	case CAPI_DATA_B3:
	case CAPI_DISCONNECT_B3:
	case CAPI_DISCONNECT:
	case CAPI_FACILITY:
	case CAPI_INFO:
	case CAPI_LISTEN:
	case CAPI_MANUFACTURER:
	case CAPI_RESET_B3:
	case CAPI_SELECT_B_PROTOCOL:
		return 1;
	}
	return 0;
}

static inline int capi_subcmd_valid(u8 subcmd)
{
	switch (subcmd) {
	case CAPI_REQ:
	case CAPI_CONF:
	case CAPI_IND:
	case CAPI_RESP:
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------ */

static void register_appl(struct capi_ctr *card, u16 applid, capi_register_params *rparam)
{
	card = capi_ctr_get(card);

	card->register_appl(card, applid, rparam);
}


static void release_appl(struct capi_ctr *card, u16 applid)
{
	DBG("applid %#x", applid);
	
	card->release_appl(card, applid);
	capi_ctr_put(card);
}


/* -------- Notifier handling --------------------------------- */

static struct capi_notifier_list{
	struct capi_notifier *head;
	struct capi_notifier *tail;
} notifier_list;

static spinlock_t notifier_lock = SPIN_LOCK_UNLOCKED;

static inline void notify_enqueue(struct capi_notifier *np)
{
	struct capi_notifier_list *q = &notifier_list;
	unsigned long flags;

	spin_lock_irqsave(&notifier_lock, flags);
	if (q->tail) {
		q->tail->next = np;
		q->tail = np;
	} else {
		q->head = q->tail = np;
	}
	spin_unlock_irqrestore(&notifier_lock, flags);
}

static inline struct capi_notifier *notify_dequeue(void)
{
	struct capi_notifier_list *q = &notifier_list;
	struct capi_notifier *np = 0;
	unsigned long flags;

	spin_lock_irqsave(&notifier_lock, flags);
	if (q->head) {
		np = q->head;
		if ((q->head = np->next) == 0)
 			q->tail = 0;
		np->next = 0;
	}
	spin_unlock_irqrestore(&notifier_lock, flags);
	return np;
}

static int notify_push(unsigned int cmd, u32 controller,
				u16 applid, u32 ncci)
{
	struct capi_notifier *np;

	if (!try_module_get(THIS_MODULE)) {
		printk(KERN_WARNING "%s: cannot reserve module\n", __FUNCTION__);
		return -1;
	}
	np = (struct capi_notifier *)kmalloc(sizeof(struct capi_notifier), GFP_ATOMIC);
	if (!np) {
		module_put(THIS_MODULE);
		return -1;
	}
	memset(np, 0, sizeof(struct capi_notifier));
	np->cmd = cmd;
	np->controller = controller;
	np->applid = applid;
	np->ncci = ncci;
	notify_enqueue(np);
	/*
	 * The notifier will result in adding/deleteing
	 * of devices. Devices can only removed in
	 * user process, not in bh.
	 */
	__module_get(THIS_MODULE);
	if (schedule_work(&tq_state_notify) == 0)
		module_put(THIS_MODULE);
	return 0;
}

/* -------- KCI_CONTRUP --------------------------------------- */

static void notify_up(u32 contr)
{
	struct capi_ctr *card = get_capi_ctr_by_nr(contr);
	struct capi20_appl *ap;
	u16 applid;

        printk(KERN_DEBUG "kcapi: notify up contr %d\n", contr);

	for (applid = 1; applid <= CAPI_MAXAPPL; applid++) {
		ap = get_capi_appl_by_nr(applid);
		if (ap && ap->callback)
			ap->callback(KCI_CONTRUP, contr, &card->profile);
	}
}

/* -------- KCI_CONTRDOWN ------------------------------------- */

static void notify_down(u32 contr)
{
	struct capi20_appl *ap;
	u16 applid;

        printk(KERN_DEBUG "kcapi: notify down contr %d\n", contr);

	for (applid = 1; applid <= CAPI_MAXAPPL; applid++) {
		ap = get_capi_appl_by_nr(applid);
		if (ap && ap->callback)
			ap->callback(KCI_CONTRDOWN, contr, 0);
	}
}

/* ------------------------------------------------------------ */

static inline void notify_doit(struct capi_notifier *np)
{
	switch (np->cmd) {
		case KCI_CONTRUP:
			notify_up(np->controller);
			break;
		case KCI_CONTRDOWN:
			notify_down(np->controller);
			break;
	}
}

static void notify_handler(void *dummy)
{
	struct capi_notifier *np;

	while ((np = notify_dequeue()) != 0) {
		notify_doit(np);
		kfree(np);
		module_put(THIS_MODULE);
	}
	module_put(THIS_MODULE);
}
	
/* -------- Receiver ------------------------------------------ */

static void recv_handler(void *dummy)
{
	struct sk_buff *skb;
	struct capi20_appl *ap;

	while ((skb = skb_dequeue(&recv_queue)) != 0) {
		ap = get_capi_appl_by_nr(CAPIMSG_APPID(skb->data));
		if (!ap) {
			printk(KERN_ERR "kcapi: recv_handler: applid %d ? (%s)\n",
			       ap->applid, capi_message2str(skb->data));
			kfree_skb(skb);
			continue;
		}

		if (   CAPIMSG_COMMAND(skb->data) == CAPI_DATA_B3
		    && CAPIMSG_SUBCOMMAND(skb->data) == CAPI_IND) {
			ap->nrecvdatapkt++;
		} else {
			ap->nrecvctlpkt++;
		}
		ap->recv_message(ap, skb);
	}
}

void capi_ctr_handle_message(struct capi_ctr * card, u16 appl, struct sk_buff *skb)
{
	int showctl = 0;
	u8 cmd, subcmd;

	if (card->cardstate != CARD_RUNNING) {
		printk(KERN_INFO "kcapi: controller %d not active, got: %s",
		       card->cnr, capi_message2str(skb->data));
		goto error;
	}
	cmd = CAPIMSG_COMMAND(skb->data);
        subcmd = CAPIMSG_SUBCOMMAND(skb->data);
	if (cmd == CAPI_DATA_B3 && subcmd == CAPI_IND) {
		card->nrecvdatapkt++;
	        if (card->traceflag > 2) showctl |= 2;
	} else {
		card->nrecvctlpkt++;
	        if (card->traceflag) showctl |= 2;
	}
	showctl |= (card->traceflag & 1);
	if (showctl & 2) {
		if (showctl & 1) {
			printk(KERN_DEBUG "kcapi: got [0x%lx] id#%d %s len=%u\n",
			       (unsigned long) card->cnr,
			       CAPIMSG_APPID(skb->data),
			       capi_cmd2str(cmd, subcmd),
			       CAPIMSG_LEN(skb->data));
		} else {
			printk(KERN_DEBUG "kcapi: got [0x%lx] %s\n",
					(unsigned long) card->cnr,
					capi_message2str(skb->data));
		}

	}
	skb_queue_tail(&recv_queue, skb);
	schedule_work(&tq_recv_notify);
	return;

error:
	kfree_skb(skb);
}

EXPORT_SYMBOL(capi_ctr_handle_message);

void capi_ctr_ready(struct capi_ctr * card)
{
	u16 appl;
	struct capi20_appl *ap;

	card->cardstate = CARD_RUNNING;

	for (appl = 1; appl <= CAPI_MAXAPPL; appl++) {
		ap = get_capi_appl_by_nr(appl);
		if (!ap) continue;
		register_appl(card, appl, &ap->rparam);
	}

        printk(KERN_NOTICE "kcapi: card %d \"%s\" ready.\n",
	       card->cnr, card->name);

	notify_push(KCI_CONTRUP, card->cnr, 0, 0);
}

EXPORT_SYMBOL(capi_ctr_ready);

void capi_ctr_reseted(struct capi_ctr * card)
{
	u16 appl;

	DBG("");

        if (card->cardstate == CARD_DETECTED)
		return;

        card->cardstate = CARD_DETECTED;

	memset(card->manu, 0, sizeof(card->manu));
	memset(&card->version, 0, sizeof(card->version));
	memset(&card->profile, 0, sizeof(card->profile));
	memset(card->serial, 0, sizeof(card->serial));

	for (appl = 1; appl <= CAPI_MAXAPPL; appl++) {
		struct capi20_appl *ap = get_capi_appl_by_nr(appl);
		if (!ap)
			continue;

		capi_ctr_put(card);
	}

	printk(KERN_NOTICE "kcapi: card %d down.\n", card->cnr);

	notify_push(KCI_CONTRDOWN, card->cnr, 0, 0);
}

EXPORT_SYMBOL(capi_ctr_reseted);

void capi_ctr_suspend_output(struct capi_ctr *card)
{
	if (!card->blocked) {
		printk(KERN_DEBUG "kcapi: card %d suspend\n", card->cnr);
		card->blocked = 1;
	}
}

EXPORT_SYMBOL(capi_ctr_suspend_output);

void capi_ctr_resume_output(struct capi_ctr *card)
{
	if (card->blocked) {
		printk(KERN_DEBUG "kcapi: card %d resume\n", card->cnr);
		card->blocked = 0;
	}
}

EXPORT_SYMBOL(capi_ctr_resume_output);

/* ------------------------------------------------------------- */

int
attach_capi_ctr(struct capi_ctr *card)
{
	int i;

	for (i = 0; i < CAPI_MAXCONTR; i++) {
		if (capi_cards[i] == NULL)
			break;
	}
	if (i == CAPI_MAXCONTR) {
		printk(KERN_ERR "kcapi: out of controller slots\n");
	   	return -EBUSY;
	}
	capi_cards[i] = card;

	card->nrecvctlpkt = 0;
	card->nrecvdatapkt = 0;
	card->nsentctlpkt = 0;
	card->nsentdatapkt = 0;
	card->cnr = i + 1;
	card->cardstate = CARD_DETECTED;
	card->blocked = 0;
	card->traceflag = showcapimsgs;

	sprintf(card->procfn, "capi/controllers/%d", card->cnr);
	card->procent = create_proc_entry(card->procfn, 0, 0);
	if (card->procent) {
	   card->procent->read_proc = 
		(int (*)(char *,char **,off_t,int,int *,void *))
			card->ctr_read_proc;
	   card->procent->data = card;
	}

	ncards++;
	printk(KERN_NOTICE "kcapi: Controller %d: %s attached\n",
			card->cnr, card->name);
	return 0;
}

EXPORT_SYMBOL(attach_capi_ctr);

int detach_capi_ctr(struct capi_ctr *card)
{
        if (card->cardstate != CARD_DETECTED)
		capi_ctr_reseted(card);

	ncards--;

	if (card->procent) {
	   remove_proc_entry(card->procfn, 0);
	   card->procent = 0;
	}
	capi_cards[card->cnr - 1] = NULL;
	printk(KERN_NOTICE "kcapi: Controller %d: %s unregistered\n",
			card->cnr, card->name);

	return 0;
}

EXPORT_SYMBOL(detach_capi_ctr);

/* ------------------------------------------------------------- */
/* -------- CAPI2.0 Interface ---------------------------------- */
/* ------------------------------------------------------------- */

u16 capi20_isinstalled(void)
{
	int i;
	for (i = 0; i < CAPI_MAXCONTR; i++) {
		if (capi_cards[i] && capi_cards[i]->cardstate == CARD_RUNNING)
			return CAPI_NOERROR;
	}
	return CAPI_REGNOTINSTALLED;
}

EXPORT_SYMBOL(capi20_isinstalled);

u16 capi20_register(struct capi20_appl *ap)
{
	int i;
	u16 applid;

	DBG("");

	if (ap->rparam.datablklen < 128)
		return CAPI_LOGBLKSIZETOSMALL;

	for (applid = 1; applid <= CAPI_MAXAPPL; applid++) {
		if (capi_applications[applid - 1] == NULL)
			break;
	}
	if (applid > CAPI_MAXAPPL)
		return CAPI_TOOMANYAPPLS;

	ap->applid = applid;
	capi_applications[applid - 1] = ap;

	ap->nrecvctlpkt = 0;
	ap->nrecvdatapkt = 0;
	ap->nsentctlpkt = 0;
	ap->nsentdatapkt = 0;
	ap->callback = 0;
	
	for (i = 0; i < CAPI_MAXCONTR; i++) {
		if (!capi_cards[i] || capi_cards[i]->cardstate != CARD_RUNNING)
			continue;
		register_appl(capi_cards[i], applid, &ap->rparam);
	}
	printk(KERN_DEBUG "kcapi: appl %d up\n", applid);

	return CAPI_NOERROR;
}

EXPORT_SYMBOL(capi20_register);

u16 capi20_release(struct capi20_appl *ap)
{
	int i;

	DBG("applid %#x", ap->applid);

	for (i = 0; i < CAPI_MAXCONTR; i++) {
		if (!capi_cards[i] || capi_cards[i]->cardstate != CARD_RUNNING)
			continue;
		release_appl(capi_cards[i], ap->applid);
	}
	capi_applications[ap->applid - 1] = NULL;
	printk(KERN_DEBUG "kcapi: appl %d down\n", ap->applid);

	return CAPI_NOERROR;
}

EXPORT_SYMBOL(capi20_release);

u16 capi20_put_message(struct capi20_appl *ap, struct sk_buff *skb)
{
	struct capi_ctr *card;
	int showctl = 0;
	u8 cmd, subcmd;

	DBG("applid %#x", ap->applid);
 
	if (ncards == 0)
		return CAPI_REGNOTINSTALLED;
	if (ap->applid == 0)
		return CAPI_ILLAPPNR;
	if (skb->len < 12
	    || !capi_cmd_valid(CAPIMSG_COMMAND(skb->data))
	    || !capi_subcmd_valid(CAPIMSG_SUBCOMMAND(skb->data)))
		return CAPI_ILLCMDORSUBCMDORMSGTOSMALL;
	card = get_capi_ctr_by_nr(CAPIMSG_CONTROLLER(skb->data));
	if (!card || card->cardstate != CARD_RUNNING) {
		card = get_capi_ctr_by_nr(1); // XXX why?
	        if (!card || card->cardstate != CARD_RUNNING) 
			return CAPI_REGNOTINSTALLED;
	}
	if (card->blocked)
		return CAPI_SENDQUEUEFULL;

	cmd = CAPIMSG_COMMAND(skb->data);
        subcmd = CAPIMSG_SUBCOMMAND(skb->data);

	if (cmd == CAPI_DATA_B3 && subcmd== CAPI_REQ) {
		card->nsentdatapkt++;
		ap->nsentdatapkt++;
	        if (card->traceflag > 2) showctl |= 2;
	} else {
		card->nsentctlpkt++;
		ap->nsentctlpkt++;
	        if (card->traceflag) showctl |= 2;
	}
	showctl |= (card->traceflag & 1);
	if (showctl & 2) {
		if (showctl & 1) {
			printk(KERN_DEBUG "kcapi: put [%#x] id#%d %s len=%u\n",
			       CAPIMSG_CONTROLLER(skb->data),
			       CAPIMSG_APPID(skb->data),
			       capi_cmd2str(cmd, subcmd),
			       CAPIMSG_LEN(skb->data));
		} else {
			printk(KERN_DEBUG "kcapi: put [%#x] %s\n",
			       CAPIMSG_CONTROLLER(skb->data),
			       capi_message2str(skb->data));
		}

	}
	return card->send_message(card, skb);
}

EXPORT_SYMBOL(capi20_put_message);

u16 capi20_get_manufacturer(u32 contr, u8 *buf)
{
	struct capi_ctr *card;

	if (contr == 0) {
		strlcpy(buf, capi_manufakturer, CAPI_MANUFACTURER_LEN);
		return CAPI_NOERROR;
	}
	card = get_capi_ctr_by_nr(contr);
	if (!card || card->cardstate != CARD_RUNNING) 
		return CAPI_REGNOTINSTALLED;
	strlcpy(buf, card->manu, CAPI_MANUFACTURER_LEN);
	return CAPI_NOERROR;
}

EXPORT_SYMBOL(capi20_get_manufacturer);

u16 capi20_get_version(u32 contr, struct capi_version *verp)
{
	struct capi_ctr *card;

	if (contr == 0) {
		*verp = driver_version;
		return CAPI_NOERROR;
	}
	card = get_capi_ctr_by_nr(contr);
	if (!card || card->cardstate != CARD_RUNNING) 
		return CAPI_REGNOTINSTALLED;

	memcpy((void *) verp, &card->version, sizeof(capi_version));
	return CAPI_NOERROR;
}

EXPORT_SYMBOL(capi20_get_version);

u16 capi20_get_serial(u32 contr, u8 *serial)
{
	struct capi_ctr *card;

	if (contr == 0) {
		strlcpy(serial, driver_serial, CAPI_SERIAL_LEN);
		return CAPI_NOERROR;
	}
	card = get_capi_ctr_by_nr(contr);
	if (!card || card->cardstate != CARD_RUNNING) 
		return CAPI_REGNOTINSTALLED;

	strlcpy((void *) serial, card->serial, CAPI_SERIAL_LEN);
	return CAPI_NOERROR;
}

EXPORT_SYMBOL(capi20_get_serial);

u16 capi20_get_profile(u32 contr, struct capi_profile *profp)
{
	struct capi_ctr *card;

	if (contr == 0) {
		profp->ncontroller = ncards;
		return CAPI_NOERROR;
	}
	card = get_capi_ctr_by_nr(contr);
	if (!card || card->cardstate != CARD_RUNNING) 
		return CAPI_REGNOTINSTALLED;

	memcpy((void *) profp, &card->profile,
			sizeof(struct capi_profile));
	return CAPI_NOERROR;
}

EXPORT_SYMBOL(capi20_get_profile);

#ifdef CONFIG_AVMB1_COMPAT
static int old_capi_manufacturer(unsigned int cmd, void *data)
{
	avmb1_loadandconfigdef ldef;
	avmb1_resetdef rdef;
	struct capi_ctr *card;
	capiloaddata ldata;
	int retval;

	switch (cmd) {
	case AVMB1_LOAD:
	case AVMB1_LOAD_AND_CONFIG:

		if (cmd == AVMB1_LOAD) {
			if (copy_from_user((void *)&ldef, data,
					   sizeof(avmb1_loaddef)))
				return -EFAULT;
			ldef.t4config.len = 0;
			ldef.t4config.data = 0;
		} else {
			if (copy_from_user((void *)&ldef, data,
					   sizeof(avmb1_loadandconfigdef)))
				return -EFAULT;
		}
		card = get_capi_ctr_by_nr(ldef.contr);
		card = capi_ctr_get(card);
		if (!card)
			return -ESRCH;
		if (card->load_firmware == 0) {
			printk(KERN_DEBUG "kcapi: load: no load function\n");
			return -ESRCH;
		}

		if (ldef.t4file.len <= 0) {
			printk(KERN_DEBUG "kcapi: load: invalid parameter: length of t4file is %d ?\n", ldef.t4file.len);
			return -EINVAL;
		}
		if (ldef.t4file.data == 0) {
			printk(KERN_DEBUG "kcapi: load: invalid parameter: dataptr is 0\n");
			return -EINVAL;
		}

		ldata.firmware.user = 1;
		ldata.firmware.data = ldef.t4file.data;
		ldata.firmware.len = ldef.t4file.len;
		ldata.configuration.user = 1;
		ldata.configuration.data = ldef.t4config.data;
		ldata.configuration.len = ldef.t4config.len;

		if (card->cardstate != CARD_DETECTED) {
			printk(KERN_INFO "kcapi: load: contr=%d not in detect state\n", ldef.contr);
			return -EBUSY;
		}
		card->cardstate = CARD_LOADING;

		retval = card->load_firmware(card, &ldata);

		if (retval) {
			card->cardstate = CARD_DETECTED;
			capi_ctr_put(card);
			return retval;
		}

		while (card->cardstate != CARD_RUNNING) {

			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/10);	/* 0.1 sec */

			if (signal_pending(current)) {
				capi_ctr_put(card);
				return -EINTR;
			}
		}
		capi_ctr_put(card);
		return 0;

	case AVMB1_RESETCARD:
		if (copy_from_user((void *)&rdef, data, sizeof(avmb1_resetdef)))
			return -EFAULT;
		card = get_capi_ctr_by_nr(rdef.contr);
		if (!card)
			return -ESRCH;

		if (card->cardstate == CARD_DETECTED)
			return 0;

		card->reset_ctr(card);

		while (card->cardstate > CARD_DETECTED) {

			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/10);	/* 0.1 sec */

			if (signal_pending(current))
				return -EINTR;
		}
		return 0;

	}
	return -EINVAL;
}
#endif

int capi20_manufacturer(unsigned int cmd, void *data)
{
        struct capi_ctr *card;

	switch (cmd) {
#ifdef CONFIG_AVMB1_COMPAT
	case AVMB1_LOAD:
	case AVMB1_LOAD_AND_CONFIG:
	case AVMB1_RESETCARD:
	case AVMB1_GET_CARDINFO:
	case AVMB1_REMOVECARD:
		return old_capi_manufacturer(cmd, data);
#endif
	case KCAPI_CMD_TRACE:
	{
		kcapi_flagdef fdef;

		if (copy_from_user((void *)&fdef, data, sizeof(kcapi_flagdef)))
			return -EFAULT;

		card = get_capi_ctr_by_nr(fdef.contr);
		if (!card)
			return -ESRCH;

		card->traceflag = fdef.flag;
		printk(KERN_INFO "kcapi: contr %d set trace=%d\n",
			card->cnr, card->traceflag);
		return 0;
	}

	default:
		printk(KERN_ERR "kcapi: manufacturer command %d unknown.\n",
					cmd);
		break;

	}
	return -EINVAL;
}

EXPORT_SYMBOL(capi20_manufacturer);

/* temporary hack */
void capi20_set_callback(struct capi20_appl *ap,
			 void (*callback) (unsigned int cmd, __u32 contr, void *data))
{
	ap->callback = callback;
}

EXPORT_SYMBOL(capi20_set_callback);

/* ------------------------------------------------------------- */
/* -------- Init & Cleanup ------------------------------------- */
/* ------------------------------------------------------------- */

/*
 * init / exit functions
 */

static int __init kcapi_init(void)
{
	char *p;
	char rev[32];

	skb_queue_head_init(&recv_queue);

	INIT_WORK(&tq_state_notify, notify_handler, NULL);
	INIT_WORK(&tq_recv_notify, recv_handler, NULL);

        kcapi_proc_init();

	if ((p = strchr(revision, ':')) != 0 && p[1]) {
		strlcpy(rev, p + 2, sizeof(rev));
		if ((p = strchr(rev, '$')) != 0 && p > rev)
		   *(p-1) = 0;
	} else
		strcpy(rev, "1.0");

        printk(KERN_NOTICE "CAPI Subsystem Rev %s\n", rev);

	return 0;
}

static void __exit kcapi_exit(void)
{
        kcapi_proc_exit();
}

module_init(kcapi_init);
module_exit(kcapi_exit);
