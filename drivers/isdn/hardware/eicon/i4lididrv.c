/* $Id: i4lididrv.c,v 1.1.2.2 2002/10/02 14:38:37 armin Exp $
 *
 * ISDN interface module for Eicon active cards.
 * I4L - IDI Interface
 * 
 * Copyright 1998-2000 by Armin Schindler (mac@melware.de) 
 * Copyright 1999-2002 Cytronics & Melware (info@melware.de)
 * 
 * Thanks to	Deutsche Mailbox Saar-Lor-Lux GmbH
 *		for sponsoring and testing fax
 *		capabilities with Diva Server cards.
 *		(dor@deutschemailbox.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>

#include "i4lididrv.h"
#include <linux/smp_lock.h>
#include "divasync.h"

#include "../avmb1/capicmd.h"  /* this should be moved in a common place */

#define INCLUDE_INLINE_FUNCS

static eicon_card *cards = (eicon_card *) NULL;   /* glob. var , contains
                                                     start of card-list   */

static char *DRIVERNAME = "Eicon Diva - native I4L Interface driver (http://www.melware.net)";
static char *DRIVERLNAME = "diva2i4l";
static char *DRIVERRELEASE = "2.0";
static char *eicon_revision = "$Revision: 1.1.2.2 $";
extern char *eicon_idi_revision;

#define EICON_CTRL_VERSION 2

ulong DebugVar;

static spinlock_t status_lock;
static spinlock_t ll_lock;

#define MAX_DESCRIPTORS  32
extern void DIVA_DIDD_Read(DESCRIPTOR *, int);

static dword notify_handle;
static DESCRIPTOR DAdapter;
static DESCRIPTOR MAdapter;

/* Parameter to be set by insmod */
static char *id  = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
static int debug = 1;

MODULE_DESCRIPTION(             "ISDN4Linux Interface for Eicon active card driver");
MODULE_AUTHOR(                  "Armin Schindler");
MODULE_SUPPORTED_DEVICE(        "ISDN subsystem and Eicon active card driver");
MODULE_PARM_DESC(id,   		"ID-String for ISDN4Linux");
MODULE_PARM(id,           	"s");
MODULE_PARM_DESC(debug,		"Initial debug value");
MODULE_PARM(debug,           	"i");
MODULE_LICENSE("GPL");

void no_printf (unsigned char * x ,...)
{
  /* dummy debug function */
}
DIVA_DI_PRINTF dprintf = no_printf;

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)
#include "debuglib.c"

static char *
eicon_getrev(const char *revision)
{
	char *rev;
	char *p;
	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else rev = "1.0";
	return rev;

}

static void
stop_dbg(void)
{
		DbgDeregister();
		memset(&MAdapter, 0, sizeof(MAdapter));
		dprintf = no_printf;
}

static eicon_chan *
find_channel(eicon_card *card, int channel)
{
	if ((channel >= 0) && (channel < card->nchannels))
        	return &(card->bch[channel]);
	eicon_log(card, 1, "%s: Invalid channel %d\n", DRIVERLNAME, channel);
	return NULL;
}

static void
eicon_rx_request(struct eicon_card *card)
{
	struct sk_buff *skb, *skb2, *skb_new;
	eicon_IND *ind, *ind2, *ind_new;
	eicon_chan *chan;

	if (!card) {
		eicon_log(card, 1, "%s: NULL card in rcv_dispatch !\n", DRIVERLNAME);
		return;
	}

	while((skb = skb_dequeue(&card->rcvq))) {
		ind = (eicon_IND *)skb->data;

		if ((chan = card->IdTable[ind->IndId]) == NULL) {
			if (DebugVar & 1) {
				switch(ind->Ind) {
					case N_DISC_ACK:
						/* doesn't matter if this happens */
						break;
					default:
						eicon_log(card, 1, "idi: Indication for unknown channel Ind=%d Id=%x\n", ind->Ind, ind->IndId);
						eicon_log(card, 1, "idi_hdl: Ch??: Ind=%d Id=%x Ch=%d MInd=%d MLen=%d Len=%d\n",
						ind->Ind,ind->IndId,ind->IndCh,ind->MInd,ind->MLength,ind->RBuffer.length);
				}
			}
			dev_kfree_skb(skb);
			continue;
		}

		if (chan->e.complete) { /* check for rec-buffer chaining */
			if (ind->MLength == ind->RBuffer.length) {
				chan->e.complete = 1;
				idi_handle_ind(card, skb);
				continue;
			}
			else {
				chan->e.complete = 0;
				ind->Ind = ind->MInd;
				skb_queue_tail(&chan->e.R, skb);
				continue;
			}
		}
		else {
			if (!(skb2 = skb_dequeue(&chan->e.R))) {
				chan->e.complete = 1;
				eicon_log(card, 1, "%s: buffer incomplete, but 0 in queue\n", DRIVERLNAME);
				dev_kfree_skb(skb);
				continue;
			}
			ind2 = (eicon_IND *)skb2->data;
			skb_new = alloc_skb(((sizeof(eicon_IND)-1)+ind->RBuffer.length+ind2->RBuffer.length),
				GFP_ATOMIC);
			if (!skb_new) {
			eicon_log(card, 1, "%s: skb_alloc failed in rcv_dispatch()\n", DRIVERLNAME);
				dev_kfree_skb(skb);
				dev_kfree_skb(skb2);
				continue;
			}
			ind_new = (eicon_IND *)skb_put(skb_new,
				((sizeof(eicon_IND)-1)+ind->RBuffer.length+ind2->RBuffer.length));
			ind_new->Ind = ind2->Ind;
			ind_new->IndId = ind2->IndId;
			ind_new->IndCh = ind2->IndCh;
			ind_new->MInd = ind2->MInd;
			ind_new->MLength = ind2->MLength;
			ind_new->RBuffer.length = ind2->RBuffer.length + ind->RBuffer.length;
			memcpy(&ind_new->RBuffer.P, &ind2->RBuffer.P, ind2->RBuffer.length);
			memcpy((&ind_new->RBuffer.P)+ind2->RBuffer.length, &ind->RBuffer.P, ind->RBuffer.length);
			dev_kfree_skb(skb);
			dev_kfree_skb(skb2);
			if (ind->MLength == ind->RBuffer.length) {
				chan->e.complete = 2;
				idi_handle_ind(card, skb_new);
				continue;
			}
			else {
				chan->e.complete = 0;
				skb_queue_tail(&chan->e.R, skb_new);
				continue;
			}
		}
	}
}

static void
eicon_ack_request(struct eicon_card *card)
{
	struct sk_buff *skb;

	if (!card) {
		eicon_log(card, 1, "%s: NULL card in ack_dispatch!\n", DRIVERLNAME);
		return;
	}
	while((skb = skb_dequeue(&card->rackq))) {
		idi_handle_ack(card, skb);
	}
}

/*
 *  IDI-Callback function
 */
static void
eicon_idi_callback(ENTITY *de)
{
	eicon_card *ccard = (eicon_card *)de->R;
	struct sk_buff *skb;
	eicon_RC *ack;
	eicon_IND *ind;
	int len = 0;

	if (de->complete == 255) {
		/* Return Code */
		skb = alloc_skb(sizeof(eicon_RC), GFP_ATOMIC);
		if (!skb) {
			eicon_log(ccard, 1, "%s: skb_alloc failed in _idi_callback()\n", DRIVERLNAME);
		} else {
			ack = (eicon_RC *)skb_put(skb, sizeof(eicon_RC));
			ack->Rc = de->Rc;
			if (de->Rc == ASSIGN_OK) {
				ack->RcId = de->Id;
				de->user[1] = de->Id;
			} else {
				ack->RcId = de->user[1];
			}
			ack->RcCh = de->RcCh;
			ack->Reference = de->user[0];
			skb_queue_tail(&ccard->rackq, skb);
			eicon_ack_request(ccard);
			eicon_log(ccard, 128, "idi_cbk: Ch%d: Rc=%x Id=%x RLen=%x compl=%x\n",
				de->user[0], de->Rc, ack->RcId, de->RLength, de->complete);
			DBG_TRC(("idi_cbk: Ch%d: Rc=%x Id=%x RLen=%x compl=%x",
				de->user[0], de->Rc, ack->RcId, de->RLength, de->complete))
	                de->Rc = 0;
		}
	} else {
		/* Indication */
		if (de->complete) {
			len = de->RLength;
		} else {
			len = 270;
			if (de->RLength <= 270)
				eicon_log(ccard, 1, "idi_cbk: ind not complete but <= 270\n");
		}
		skb = alloc_skb((sizeof(eicon_IND) + len - 1), GFP_ATOMIC);
		if (!skb) {
			eicon_log(ccard, 1, "%s: skb_alloc failed in _idi_callback()\n", DRIVERLNAME);
		} else {
			ind = (eicon_IND *)skb_put(skb, (sizeof(eicon_IND) + len - 1));
			ind->Ind = de->Ind;
			ind->IndId = de->user[1];
			ind->IndCh = de->IndCh;
			ind->MInd  = de->Ind;
			ind->RBuffer.length = len;
			ind->MLength = de->RLength;
			memcpy(&ind->RBuffer.P, &de->RBuffer->P, len);
			skb_queue_tail(&ccard->rcvq, skb);
			eicon_rx_request(ccard);
			eicon_log(ccard, 128, "idi_cbk: Ch%d: Ind=%x Id=%x RLen=%x compl=%x\n",
				de->user[0], de->Ind, ind->IndId, de->RLength, de->complete);
			DBG_TRC(("idi_cbk: Ch%d: Ind=%x Id=%x RLen=%x compl=%x",
				de->user[0], de->Ind, ind->IndId, de->RLength, de->complete))
	                de->Ind = 0;
		}
	}
	de->RNum = 0;
	de->RNR = 0;
    eicon_tx_request(ccard);
}

/*
**  Kernel thread to prevent in_interrupt
*/
static DECLARE_TASK_QUEUE(tq_divad);
static struct semaphore diva_thread_sem;
static struct semaphore diva_thread_end;
static int divad_pid = -1;
static int divad_thread(void * data);
static void diva_tx(void *data);
static atomic_t thread_running;

static void __init
diva_init_thread(void)
{
  int pid = 0;

  pid = kernel_thread(divad_thread, NULL, CLONE_KERNEL);
  if (pid >= 0) {
       divad_pid = pid;
  }
}

static int
divad_thread(void * data)
{
  atomic_inc(&thread_running);
  if (atomic_read(&thread_running) > 1) {
      printk(KERN_WARNING"%s: thread already running\n", DRIVERLNAME);
      return(0);
  }

  printk(KERN_INFO "%s: thread started with pid %d\n", DRIVERLNAME, current->pid);
  exit_mm(current);
  exit_files(current);
  exit_fs(current);

  /* Set to RealTime */
  current->policy = SCHED_FIFO;
  current->rt_priority = 33;

  strcpy(current->comm, "kdiva2i4ld");

  for(;;) {
    down_interruptible(&diva_thread_sem);
    if(!(atomic_read(&thread_running)))
      break;
    if(signal_pending(current)) {
         flush_signals(current);
    } else {
         run_task_queue(&tq_divad);
    }
  }
  up(&diva_thread_end);
  divad_pid = -1;
  return 0;
}

static void
stop_diva_thread(void)
{
    if (divad_pid >= 0) {
         atomic_set(&thread_running, 0);
         up(&diva_thread_sem);
         down_interruptible(&diva_thread_end);
    }
}

void
eicon_tx_request(struct eicon_card *card)
{
  card->tq.routine = diva_tx;
  card->tq.data = (void *)card;
  queue_task(&card->tq, &tq_divad);
  up(&diva_thread_sem);
}

static void
diva_tx(void *data)
{
        struct eicon_card *card = (eicon_card *) data;
	struct sk_buff *skb;
	struct sk_buff *skb2;
	eicon_chan *chan;
	eicon_chan_ptr *chan2;
	eicon_REQ *reqbuf = 0;
	int ReqCount = 0;
	int tmpid = 0;
	int quloop = 1;
	int dlev = 0;
	ENTITY *ep = 0;

        if (!card) {
                eicon_log(card, 1, "%s: NULL card in transmit !\n", DRIVERLNAME);
                return;
        }

	ReqCount = 0;
	if (!(skb2 = skb_dequeue(&card->sndq)))
		quloop = 0;
	while(quloop) {
		chan2 = (eicon_chan_ptr *)skb2->data;
		chan = chan2->ptr;
		if (!chan->e.busy) {
		 if((skb = skb_dequeue(&chan->e.X))) {

		  reqbuf = (eicon_REQ *)skb->data;
		  if ((reqbuf->Reference) && (chan->e.B2Id == 0) && (reqbuf->ReqId & 0x1f)) {
			eicon_log(card, 16, "%s: transmit: error Id=0 on %d (Net)\n", DRIVERLNAME, chan->No);
		  } else {
			dlev = 160;
			if (reqbuf->ReqId & 0x1f) { /* if this is no ASSIGN */

				if (!reqbuf->Reference) { /* Signal Layer */
					ep = &chan->de;
					tmpid = chan->e.D3Id;
					chan->e.ReqCh = 0;
				}
				else {                  /* Net Layer */
					ep = &chan->be;
					tmpid = chan->e.B2Id;
					chan->e.ReqCh = 1;
					if (((reqbuf->Req & 0x0f) == 0x08) ||
					    ((reqbuf->Req & 0x0f) == 0x01)) { /* Send Data */
						chan->waitq = reqbuf->XBuffer.length;
						chan->waitpq += reqbuf->XBuffer.length;
						dlev = 128;
					}
				}

			} else {        /* It is an ASSIGN */
				if (!reqbuf->Reference)
					ep = &chan->de;
				else
					ep = &chan->be;
				ep->Id = reqbuf->ReqId;
				tmpid = reqbuf->ReqId;

				if (!reqbuf->Reference)
					chan->e.ReqCh = 0;
				else
					chan->e.ReqCh = 1;
			}

			chan->e.ref = chan->No;
			chan->e.Req = reqbuf->Req;
			ReqCount++;
			if (ep) {
				ep->callback = eicon_idi_callback;
				ep->R = (BUFFERS *)card;
				ep->user[0] = (word)chan->No;
				ep->user[1] = (word)tmpid;
				ep->XNum = 1;
				ep->RNum = 0;
				ep->RNR = 0;
				ep->Rc = 0;
				ep->Ind = 0;
				ep->X->PLength = reqbuf->XBuffer.length;
				memcpy(ep->X->P, &reqbuf->XBuffer.P, reqbuf->XBuffer.length);
				ep->ReqCh = reqbuf->ReqCh;
				ep->Req = reqbuf->Req;
			}
			chan->e.busy = 1;
			eicon_log(card, dlev, "idi: Req=%d Id=%x Ch=%d Len=%d Ref=%d\n",
				reqbuf->Req, tmpid,
				reqbuf->ReqCh, reqbuf->XBuffer.length,
				chan->e.ref);
			if (ep) {
				card->d.request(ep);
				if (ep->Rc)
					eicon_idi_callback(ep);
			}
		  }
		  dev_kfree_skb(skb);
		 }
		 dev_kfree_skb(skb2);
		}
		else {
			skb_queue_tail(&card->sackq, skb2);
			eicon_log(card, 128, "%s: transmit: busy chan %d\n", DRIVERLNAME, chan->No);
		}
		if (!(skb2 = skb_dequeue(&card->sndq)))
			quloop = 0;
	}
	while((skb = skb_dequeue(&card->sackq))) {
		skb_queue_tail(&card->sndq, skb);
	}
}

static int
eicon_command(eicon_card * card, isdn_ctrl * c)
{
        ulong a;
        eicon_chan *chan;
	isdn_ctrl cmd;
	int ret = 0;

	eicon_log(card, 16, "%s_cmd 0x%x with arg 0x%lx (0x%lx)\n", DRIVERLNAME,
		c->command, c->arg, (ulong) *c->parm.num);

        switch (c->command) {
		case ISDN_CMD_IOCTL:
			memcpy(&a, c->parm.num, sizeof(ulong));
			switch (c->arg) {
				case EICON_IOCTL_GETVER:
					return(EICON_CTRL_VERSION);
				case EICON_IOCTL_MANIF:
					if (!card->flags & EICON_FLAGS_RUNNING)
						return -ENODEV;
					if (!card->d.features & DI_MANAGE)
						return -ENODEV;
					ret = eicon_idi_manage(
						card, 
						(eicon_manifbuf *)a);
					return ret;

				case EICON_IOCTL_GETXLOG:
					return -ENODEV;
				case EICON_IOCTL_DEBUGVAR:
					DebugVar = a;
					eicon_log(card, 1, "%s: Debug Value set to %ld\n", DRIVERLNAME, DebugVar);
					return 0;
				case EICON_IOCTL_LOADPCI:
					eicon_log(card, 1, "%s: Wrong version of load-utility,\n", DRIVERLNAME);
					eicon_log(card, 1, "%s: re-compile eiconctrl !\n", DRIVERLNAME);
					eicon_log(card, 1, "%s: Maybe update of utility is necessary !\n", DRIVERLNAME);
					return -EINVAL;
				default:	
					return -EINVAL;
			}
			break;
		case ISDN_CMD_DIAL:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			if ((chan->fsm_state != EICON_STATE_NULL) && (chan->fsm_state != EICON_STATE_LISTEN)) {
				eicon_log(card, 1, "%s: Dial on channel %d with state %d\n", DRIVERLNAME,
					chan->No, chan->fsm_state);
				return -EBUSY;
			}
			chan->fsm_state = EICON_STATE_OCALL;
			
			ret = idi_connect_req(card, chan, c->parm.setup.phone,
						     c->parm.setup.eazmsn,
						     c->parm.setup.si1,
						     c->parm.setup.si2);
			if (ret) {
				cmd.driver = card->myid;
				cmd.command = ISDN_STAT_DHUP;
				cmd.arg &= 0x1f;
				card->interface.statcallb(&cmd);
			}
			return ret;
		case ISDN_CMD_ACCEPTD:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			if (chan->fsm_state == EICON_STATE_ICALL) { 
				idi_connect_res(card, chan);
			}
			return 0;
		case ISDN_CMD_ACCEPTB:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			return 0;
		case ISDN_CMD_HANGUP:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			idi_hangup(card, chan);
			return 0;
		case ISDN_CMD_SETEAZ:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			chan->eazmask = 0x3ff;
			eicon_idi_listen_req(card, chan);
			return 0;
		case ISDN_CMD_CLREAZ:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			chan->eazmask = 0;
			eicon_idi_listen_req(card, chan);
			return 0;
		case ISDN_CMD_SETL2:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			chan->l2prot = (c->arg >> 8);
                        memcpy(chan->a_para, c->parm.aux.para, sizeof(chan->a_para));
			return 0;
		case ISDN_CMD_SETL3:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			chan->l3prot = (c->arg >> 8);
#ifdef CONFIG_ISDN_TTY_FAX
			if (chan->l3prot == ISDN_PROTO_L3_FCLASS2) {
				chan->fax = c->parm.fax;
				eicon_log(card, 128, "idi_cmd: Ch%d: SETL3 struct fax=0x%x\n",chan->No, chan->fax);
			}
#endif
			return 0;
#ifdef CONFIG_ISDN_TTY_FAX
		case ISDN_CMD_FAXCMD:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			if (!chan->fax)
				break;
			idi_fax_cmd(card, chan);
			return 0;
#endif
		case ISDN_CMD_AUDIO:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			idi_audio_cmd(card, chan, c->arg >> 8, c->parm.num);
			return 0;
		case CAPI_PUT_MESSAGE:
			if (!card->flags & EICON_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			if (c->parm.cmsg.Length < 8)
				break;
			switch(c->parm.cmsg.Command) {
				case CAPI_FACILITY:
					if (c->parm.cmsg.Subcommand == CAPI_REQ)
						return(capipmsg(card, chan, &c->parm.cmsg));
					break;
				case CAPI_MANUFACTURER:
				default:
					break;
			}
			return 0;
        }
	
        return -EINVAL;
}

static int
find_free_number(void)
{
  int num = 0;
  char cid[40];
  eicon_card *p;
  ulong flags;

  spin_lock_irqsave(&ll_lock, flags);
  while(num < 100) {
    sprintf(cid, "%s%d", id, num);
    num++;
    p = cards;
    while (p) {
      if (!strcmp(p->regname, cid))
        break;
      p = p->next;
    }
		if (p)
		{
	    spin_unlock_irqrestore(&ll_lock, flags);
  	  return(num - 1);
		}
  }
  spin_unlock_irqrestore(&ll_lock, flags);
  return(999);
}

/*
 * Find card with given driverId
 */
static inline eicon_card *
eicon_findcard(int driverid)
{
  eicon_card *p;
  ulong flags;

  spin_lock_irqsave(&ll_lock, flags);
  p = cards;
  while (p) {
    if (p->myid == driverid) {
      spin_unlock_irqrestore(&ll_lock, flags);
      return p;
    }
    p = p->next;
  }
  spin_unlock_irqrestore(&ll_lock, flags);
  return (eicon_card *) 0;
}

/*
 * Wrapper functions for interface to linklevel
 */
static int
if_command(isdn_ctrl * c)
{
        eicon_card *card = eicon_findcard(c->driver);

        if (card)
                return (eicon_command(card, c));
        printk(KERN_ERR
             "%s: if_command %d called with invalid driverId %d!\n", DRIVERLNAME,
               c->command, c->driver);
        return -ENODEV;
}

static int
if_writecmd(const u_char * buf, int len, int user, int id, int channel)
{
	/* Not used */
        return (len);
}

static int
if_readstatus(u_char * buf, int len, int user, int id, int channel)
{
  int count = 0;
  int cnt = 0;
  u_char *p = buf;
  struct sk_buff *skb;
  ulong flags;

        eicon_card *card = eicon_findcard(id);
	
        if (card) {
                if (!card->flags & EICON_FLAGS_RUNNING)
                        return -ENODEV;
	
		spin_lock_irqsave(&status_lock, flags);
		while((skb = skb_dequeue(&card->statq))) {

			if ((skb->len + count) > len)
				cnt = len - count;
			else
				cnt = skb->len;

			if (user)
				copy_to_user(p, skb->data, cnt);
			else
				memcpy(p, skb->data, cnt);

			count += cnt;
			p += cnt;

			if (cnt == skb->len) {
				dev_kfree_skb(skb);
				if (card->statq_entries > 0)
					card->statq_entries--;
			} else {
				skb_pull(skb, cnt);
				skb_queue_head(&card->statq, skb);
				spin_unlock_irqrestore(&status_lock, flags);
				return count;
			}
		}
		card->statq_entries = 0;
		spin_unlock_irqrestore(&status_lock, flags);
		return count;
        }
        printk(KERN_ERR
               "%s: if_readstatus called with invalid driverId!\n", DRIVERLNAME);
        return 0;
}

static int
if_sendbuf(int id, int channel, int ack, struct sk_buff *skb)
{
        eicon_card *card = eicon_findcard(id);
	eicon_chan *chan;
	int ret = 0;
	int len;

	len = skb->len;

        if (card) {
                if (!card->flags & EICON_FLAGS_RUNNING)
                        return -ENODEV;
        	if (!(chan = find_channel(card, channel)))
			return -ENODEV;

		if (chan->fsm_state == EICON_STATE_ACTIVE) {
#ifdef CONFIG_ISDN_TTY_FAX
			if (chan->l2prot == ISDN_PROTO_L2_FAX) {
				if ((ret = idi_faxdata_send(card, chan, skb)) > 0)
					ret = len;
			}
			else
#endif
				ret = idi_send_data(card, chan, ack, skb, 1, 1);
			return (ret);
		} else {
			return -ENODEV;
		}
        }
        printk(KERN_ERR
               "%s: if_sendbuf called with invalid driverId!\n", DRIVERLNAME);
        return -ENODEV;
}

/* jiftime() copied from HiSax */
static inline int jiftime(char *s, long mark)
{
        s += 8;

        *s-- = '\0';
        *s-- = mark % 10 + '0';
        mark /= 10;
        *s-- = mark % 10 + '0';
        mark /= 10;
        *s-- = '.';
        *s-- = mark % 10 + '0';
        mark /= 10;
        *s-- = mark % 6 + '0';
        mark /= 6;
        *s-- = ':';
        *s-- = mark % 10 + '0';
        mark /= 10;
        *s-- = mark % 10 + '0';
        return(8);
}

void
eicon_putstatus(eicon_card * card, char * buf)
{
  int count;
  isdn_ctrl cmd;
  u_char *p;
  struct sk_buff *skb;
  ulong flags;

	if (!card) {
		if (!(card = cards))
			return;
	}

	spin_lock_irqsave(&status_lock, flags);
	count = strlen(buf);
	skb = alloc_skb(count, GFP_ATOMIC);
	if (!skb) {
		spin_unlock_irqrestore(&status_lock, flags);
		printk(KERN_ERR "%s: could not alloc skb in putstatus\n", DRIVERLNAME);
		return;
	}
	p = skb_put(skb, count);
	memcpy(p, buf, count);

	skb_queue_tail(&card->statq, skb);

	if (card->statq_entries >= MAX_STATUS_BUFFER) {
		if ((skb = skb_dequeue(&card->statq))) {
			count -= skb->len;
			dev_kfree_skb(skb);
		} else
			count = 0;
	} else
		card->statq_entries++;

	spin_unlock_irqrestore(&status_lock, flags);
        if (count) {
                cmd.command = ISDN_STAT_STAVAIL;
                cmd.driver = card->myid;
                cmd.arg = count;
		card->interface.statcallb(&cmd);
        }
}

/*
 * Debug and Log 
 */
void
eicon_log(eicon_card * card, int level, const char *fmt, ...)
{
	va_list args;
	char Line[160];
	u_char *p;


	if ((DebugVar & level) || (DebugVar & 256)) {
		va_start(args, fmt);

		if (DebugVar & level) {
			if (DebugVar & 256) {
				/* log-buffer */
				p = Line;
				p += jiftime(p, jiffies);
				*p++ = 32;
				p += vsprintf(p, fmt, args);
				*p = 0;	
				eicon_putstatus(card, Line);
			} else {
				/* printk, syslogd */
				vsprintf(Line, fmt, args);
				printk(KERN_DEBUG "%s", Line);
			}
		}

		va_end(args);
	}
}


/*
 * Allocate a new card-struct, initialize it
 * link it into cards-list.
 */
static void
eicon_alloccard(DESCRIPTOR *d)
{
  int j;
  char cid[40];
  eicon_card *card;
  ulong flags;

	sprintf(cid, "%s%d", id, find_free_number());
	if (!(card = (eicon_card *) kmalloc(sizeof(eicon_card), GFP_KERNEL))) {
		eicon_log(card, 1,
		       "%s: (%s) Could not allocate card-struct.\n", DRIVERLNAME, cid);
		return;
	}
	memset((char *) card, 0, sizeof(eicon_card));
	skb_queue_head_init(&card->sndq);
	skb_queue_head_init(&card->rcvq);
	skb_queue_head_init(&card->rackq);
	skb_queue_head_init(&card->sackq);
	skb_queue_head_init(&card->statq);
	card->statq_entries = 0;
	card->interface.owner = THIS_MODULE;
	card->interface.maxbufsize = 4000;
	card->interface.command = if_command;
	card->interface.writebuf_skb = if_sendbuf;
	card->interface.writecmd = if_writecmd;
	card->interface.readstat = if_readstatus;
	card->interface.features =
		ISDN_FEATURE_L2_X75I |
		ISDN_FEATURE_L2_HDLC |
		ISDN_FEATURE_L2_TRANS |
		ISDN_FEATURE_L3_TRANS |
		ISDN_FEATURE_L3_TRANSDSP |
		ISDN_FEATURE_P_UNKNOWN;
	card->interface.hl_hdrlen = 20;
	card->ptype = ISDN_PTYPE_UNKNOWN;
	strcpy(card->interface.id, cid);
	card->myid = -1;
	card->type = d->type;

	if (d->features & (DI_FAX3 | DI_EXTD_FAX))
		card->interface.features |= (ISDN_FEATURE_L2_FAX | ISDN_FEATURE_L3_FCLASS2);
	if (d->features & DI_MODEM)
		card->interface.features |= ISDN_FEATURE_L2_MODEM;
	if (d->features & DI_V110)
		card->interface.features |= (ISDN_FEATURE_L2_V11096|ISDN_FEATURE_L2_V11019|ISDN_FEATURE_L2_V11038);

	card->flags = 0;
	card->nchannels = d->channels;
	card->interface.channels = d->channels;
	if (!(card->bch = (eicon_chan *) vmalloc(sizeof(eicon_chan) * (card->nchannels + 1)))) {
		eicon_log(card, 1,
		       "%s: (%s) Could not allocate bch-struct.\n", DRIVERLNAME, cid);
		kfree(card);
		return;
	}
	for (j=0; j< (card->nchannels + 1); j++) {
		memset((char *)&card->bch[j], 0, sizeof(eicon_chan));
		card->bch[j].statectrl = 0;
		card->bch[j].l2prot = ISDN_PROTO_L2_X75I;
		card->bch[j].l3prot = ISDN_PROTO_L3_TRANS;
		card->bch[j].e.D3Id = 0;
		card->bch[j].e.B2Id = 0;
		card->bch[j].e.Req = 0;
		card->bch[j].No = j;
		card->bch[j].tskb1 = NULL;
		card->bch[j].tskb2 = NULL;
		skb_queue_head_init(&card->bch[j].e.X);
		skb_queue_head_init(&card->bch[j].e.R);
	}

	if (!(card->dbuf = (DBUFFER *) kmalloc((sizeof(DBUFFER) * (card->nchannels + 1))*2
						 , GFP_KERNEL))) {
		eicon_log(card, 1,
		       "%s: (%s) Could not allocate DBUFFER-struct.\n", DRIVERLNAME, cid);
		kfree(card);
		vfree(card->bch);
		return;
	}
	if (!(card->sbuf = (BUFFERS *) kmalloc((sizeof(BUFFERS) * (card->nchannels + 1)) * 2, GFP_KERNEL))) {
		eicon_log(card, 1,
		       "%s: (%s) Could not allocate BUFFERS-struct.\n", DRIVERLNAME, cid);
		kfree(card);
		vfree(card->bch);
		kfree(card->dbuf);
		return;
	}
	if (!(card->sbufp = (char *) kmalloc((270 * (card->nchannels + 1)) * 2, GFP_KERNEL))) {
		eicon_log(card, 1,
		       "%s: (%s) Could not allocate BUFFERSP-struct.\n", DRIVERLNAME, cid);
		kfree(card);
		vfree(card->bch);
		kfree(card->dbuf);
		kfree(card->sbuf);
		return;
	}
	for (j=0; j< (card->nchannels + 1); j++) {
		memset((char *)&card->dbuf[j], 0, sizeof(DBUFFER));
		card->bch[j].de.RBuffer = (DBUFFER *)&card->dbuf[j];
		memset((char *)&card->dbuf[j+(card->nchannels+1)], 0, sizeof(BUFFERS));
		card->bch[j].be.RBuffer = (DBUFFER *)&card->dbuf[j+(card->nchannels+1)];

		memset((char *)&card->sbuf[j], 0, sizeof(BUFFERS));
		card->bch[j].de.X = (BUFFERS *)&card->sbuf[j];
		memset((char *)&card->sbuf[j+(card->nchannels+1)], 0, sizeof(BUFFERS));
		card->bch[j].be.X = (BUFFERS *)&card->sbuf[j+(card->nchannels+1)];

		memset((char *)&card->sbufp[j], 0, 270);
		card->bch[j].de.X->P = (char *)&card->sbufp[j * 270];
		memset((char *)&card->sbufp[j+(card->nchannels+1)], 0, 270);
		card->bch[j].be.X->P = (char *)&card->sbufp[(j+(card->nchannels+1)) * 270];
	}
	memcpy(&card->d, d, sizeof(*d)); /* DESCRIPTOR entries */

	/* initializing some variables */
	card->lock = SPIN_LOCK_UNLOCKED;
	card->ReadyInt = 0;

	for(j = 0; j < 256; j++)
		card->IdTable[j] = NULL;

	for(j = 0; j < (card->d.channels + 1); j++) {
		card->bch[j].e.busy = 0;
		card->bch[j].e.D3Id = 0;
		card->bch[j].e.B2Id = 0;
		card->bch[j].e.ref = 0;
		card->bch[j].e.Req = 0;
		card->bch[j].e.complete = 1;
		card->bch[j].fsm_state = EICON_STATE_NULL;
	}
	printk(KERN_INFO "%s: registered card '%s' with %d channels\n",
		DRIVERLNAME, cid, d->channels);

  spin_lock_irqsave(&ll_lock, flags);
	card->next = cards;
	cards = card;
  spin_unlock_irqrestore(&ll_lock, flags);
}

/*
 * register card at linklevel
 */
static int
eicon_registercard(eicon_card * card)
{
	isdn_ctrl cmd;

        if (!register_isdn(&card->interface)) {
                printk(KERN_WARNING
                       "%s: Unable to register %s\n", DRIVERLNAME,
                       card->interface.id);
                return -1;
        }
        card->myid = card->interface.channels;
        sprintf(card->regname, "%s", card->interface.id);

	/* after register we start it */
	card->flags |= EICON_FLAGS_LOADED;
	card->flags |= EICON_FLAGS_RUNNING;
	cmd.command = ISDN_STAT_RUN;
	cmd.driver = card->myid;
	cmd.arg = 0;
	card->interface.statcallb(&cmd);

        return 0;
}

static void 
unregister_card(eicon_card * card, int rme)
{
  int count;
  int channel;
  isdn_ctrl cmd;
  eicon_chan *chan;

  if(rme) {
      /* before unload we need to remove the signal entity */
      for(channel = 0; channel < card->nchannels; channel++)
        {
		chan = &(card->bch[channel]);
                if (chan->e.D3Id) {
                        idi_do_req(card, chan, REMOVE, 0);
			count = 100;
			while(count--) {
				if (!chan->e.D3Id)
					break;
				SLEEP(2);
			}
			if (!count)
				printk(KERN_WARNING"%s: ch:%d unlink to diva module not successful !\n",
					DRIVERLNAME, chan->No);
		}
        }
  }

  cmd.command = ISDN_STAT_UNLOAD;
  cmd.driver = card->myid;
  card->interface.statcallb(&cmd);
  DBG_TRC(("channel entities freed"));
}

static void
eicon_freecard(eicon_card *card) {
	int i;

	for(i = 0; i < (card->nchannels + 1); i++) {
		skb_queue_purge(&card->bch[i].e.X);
		skb_queue_purge(&card->bch[i].e.R);
	}
	skb_queue_purge(&card->sndq);
	skb_queue_purge(&card->rcvq);
	skb_queue_purge(&card->rackq);
	skb_queue_purge(&card->sackq);
	skb_queue_purge(&card->statq);

	kfree(card->sbufp);
	kfree(card->sbuf);
	kfree(card->dbuf);
	vfree(card->bch);
	kfree(card);
        DBG_TRC(("card structures freed"));
}

static int
eicon_addcard(DESCRIPTOR *d)
{
  eicon_card *p;
  eicon_card *q = NULL;
  int registered;
  int added = 0;
  int failed = 0;
  ulong flags;

	eicon_alloccard(d);
        p = cards;
        while (p) {
		registered = 0;
		if (!p->interface.statcallb) {
			/* Not yet registered.
			 * Try to register and activate it.
			 */
			added++;
			if (!eicon_registercard(p))
				registered = 1;
		} else {
			/* Card already registered */
			registered = 1;
		}

                if (registered) {
			/* Init OK, next card ... */
                        spin_lock_irqsave(&ll_lock, flags);
                        q = p;
                        p = p->next;
                        spin_unlock_irqrestore(&ll_lock, flags);
                } else {
                        /* registering failed, remove card from list, free memory */
                        printk(KERN_ERR
                               "%s: Initialization of %s failed\n", DRIVERLNAME,
                               p->interface.id);
                        spin_lock_irqsave(&ll_lock, flags);
                        if (q) {
                                q->next = p->next;
                                eicon_freecard(p);
                                p = q->next;
                        } else {
                                cards = p->next;
                                eicon_freecard(p);
                                p = cards;
                        }
                        spin_unlock_irqrestore(&ll_lock, flags);
			failed++;
                }
	}
        return (added - failed);
}

static void *
didd_callback(void *context, DESCRIPTOR* adapter, int removal)
{
  eicon_card *cp = NULL, *lastcp = NULL;
  ulong flags;

  if (adapter->type == IDI_DADAPTER)
  {
    printk(KERN_ERR "%s: Change in DAdapter ? Oops ?.\n", DRIVERLNAME);
    DBG_ERR(("Notification about IDI_DADAPTER change ! Oops."));
    return(NULL);
  }
  else if (adapter->type == IDI_DIMAINT)
  {
    if (removal)
    {
				stop_dbg();
    }
    else
    {
      memcpy(&MAdapter, adapter, sizeof(MAdapter));
      dprintf = (DIVA_DI_PRINTF)MAdapter.request;
      DbgRegister("I4L", DRIVERRELEASE, DBG_DEFAULT);
    }
  }
  else if ((adapter->type > 0) &&
           (adapter->type < 16))
  {  /* IDI Adapter */
    if (removal)
    {
      spin_lock_irqsave(&ll_lock, flags);
      lastcp = cp = cards;
      while (cp) {
        if (cp->d.request == adapter->request)
        {
          spin_unlock_irqrestore(&ll_lock, flags);
          DBG_LOG(("remove adapter from list"));
          unregister_card(cp, 0);
          spin_lock_irqsave(&ll_lock, flags);
          if (cp == lastcp)
            cards = cp->next;
          else
            lastcp->next = cp->next;
          eicon_freecard(cp);
          break;
        }
        lastcp = cp;
        cp = cp->next;
      }
      spin_unlock_irqrestore(&ll_lock, flags);
    }
    else
    {
      if (adapter->channels) {
        DBG_LOG(("add adapter to list"));
        eicon_addcard(adapter);
      }
    }
  }
  return(NULL);
}

static int __init
connect_didd(void)
{
  int x = 0;
  int dadapter = 0;
  IDI_SYNC_REQ req;
  DESCRIPTOR DIDD_Table[MAX_DESCRIPTORS];

  DIVA_DIDD_Read(DIDD_Table, sizeof(DIDD_Table));

  for (x = 0; x < MAX_DESCRIPTORS; x++)
  {
    if (DIDD_Table[x].type == IDI_DADAPTER)
    {  /* DADAPTER found */
      dadapter = 1;
      memcpy(&DAdapter, &DIDD_Table[x], sizeof(DAdapter));
      req.didd_notify.e.Req = 0;
      req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REGISTER_ADAPTER_NOTIFY;
      req.didd_notify.info.callback = didd_callback;
      req.didd_notify.info.context = 0;
      DAdapter.request((ENTITY *)&req);
      if (req.didd_notify.e.Rc != 0xff)
			{
					stop_dbg();
					return(0);
			}
      notify_handle = req.didd_notify.info.handle;
    }
    else if (DIDD_Table[x].type == IDI_DIMAINT)
    {  /* MAINT found */
      memcpy(&MAdapter, &DIDD_Table[x], sizeof(DAdapter));
      dprintf = (DIVA_DI_PRINTF)MAdapter.request;
      DbgRegister("I4L", DRIVERRELEASE, DBG_DEFAULT);
    }
    else if ((DIDD_Table[x].type > 0) &&
             (DIDD_Table[x].type < 16))
    {  /* IDI Adapter found */
      if (DIDD_Table[x].channels) {
        eicon_addcard(&DIDD_Table[x]);
      }
    }
  }

	if (!dadapter) {
			stop_dbg();
	}

  return(dadapter);
}

static void __exit
disconnect_didd(void)
{
  IDI_SYNC_REQ req;

	stop_dbg();

  req.didd_notify.e.Req = 0;
  req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER_NOTIFY;
  req.didd_notify.info.handle = notify_handle;
  DAdapter.request((ENTITY *)&req);
}

/*
** proc entry
*/
extern struct proc_dir_entry *proc_net_isdn_eicon;
static struct proc_dir_entry *i4lidi_proc_entry = NULL;

static int
i4lidi_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  int len = 0;
  char tmprev[32];

  len += sprintf(page+len, "%s\n", DRIVERNAME);
  len += sprintf(page+len, "name     : %s\n", DRIVERLNAME);
  len += sprintf(page+len, "release  : %s\n", DRIVERRELEASE);
  strcpy(tmprev, eicon_revision);
  len += sprintf(page+len, "revision : %s/", eicon_getrev(tmprev));
  strcpy(tmprev, eicon_idi_revision);
  len += sprintf(page+len, "%s\n", eicon_getrev(tmprev));

  if (off + count >= len)
    *eof = 1;
  if (len < off)
    return 0;
  *start = page + off;
  return((count < len-off) ? count : len-off);
}

static void __init
create_proc(void)
{
  if(!(i4lidi_proc_entry = create_proc_entry(DRIVERLNAME,
                          S_IFREG | S_IRUGO | S_IWUSR, proc_net_isdn_eicon)))
  {
    printk(KERN_WARNING "%s: failed to create proc entry.\n", DRIVERLNAME);
    return;
  }
  i4lidi_proc_entry->read_proc = i4lidi_proc_read;
  i4lidi_proc_entry->owner = THIS_MODULE;
}

static void __exit
remove_proc(void)
{
  if(i4lidi_proc_entry)
      remove_proc_entry(DRIVERLNAME,  proc_net_isdn_eicon);
}

/*
** load / unload
*/
static int __init
i4l_idi_init(void)
{
  int ret = 0;
  char tmprev[50];

  status_lock = SPIN_LOCK_UNLOCKED;
  ll_lock = SPIN_LOCK_UNLOCKED;

  if (strlen(id) < 1)
    strcpy(id, "diva");

  DebugVar = debug;

  init_MUTEX_LOCKED(&diva_thread_sem);
  init_MUTEX_LOCKED(&diva_thread_end);

  printk(KERN_INFO "%s\n", DRIVERNAME);
  printk(KERN_INFO "%s: Rel:%s  Rev:",DRIVERLNAME , DRIVERRELEASE);
  strcpy(tmprev, eicon_revision);
  printk("%s/", eicon_getrev(tmprev));
  strcpy(tmprev, eicon_idi_revision);
  printk("%s\n", eicon_getrev(tmprev));

  diva_init_thread();

  if(!connect_didd()) {
    printk(KERN_ERR "%s: failed to connect to DIDD.\n", DRIVERLNAME);
    stop_diva_thread();
    ret = -EIO;
    goto out;
  }    
  create_proc();

out:
  return(ret);
}

static void __exit
i4l_idi_exit(void)
{
  eicon_card *card, *last, *cc;
  ulong flags;

  spin_lock_irqsave(&ll_lock, flags);
  cc = cards;
  card = cc;
  cards = NULL;
  spin_unlock_irqrestore(&ll_lock, flags);

  remove_proc();

  while (card) {
    unregister_card(card, 1); 
    card = card->next;
  }

  stop_diva_thread();
  disconnect_didd();

  card = cc;
  while (card) {
    last = card;
    card = card->next;
    eicon_freecard(last);
  }
  printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(i4l_idi_init);
module_exit(i4l_idi_exit);
