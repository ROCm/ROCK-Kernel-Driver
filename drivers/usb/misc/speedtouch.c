/*
 *  Driver Module for Alcatel SpeedTouch USB xDSL modem
 *  Copyright 2001, Alcatel
 *  Written by Johan Verrept (Johan.Verrept@advalvas.be)
 *

1.5A:   - Version for inclusion in 2.5 series kernel
        - Modifcations by Richard Purdie (rpurdie@rpsys.net)
        - made compatible with kernel 2.5.6 onwards by changing
	  udsl_usb_send_data_context->urb changed to a pointer 
	  and adding code to alloc and free it
        - remove_wait_queue() added to udsl_atm_processqueue_thread() 

1.5:	- fixed memory leak when atmsar_decode_aal5 returned NULL.
	 (reported by stephen.robinson@zen.co.uk)

1.4:	- changed the spin_lock() under interrupt to spin_lock_irqsave()
	- unlink all active send urbs of a vcc that is being closed.

1.3.1:	- added the version number

1.3:	- Added multiple send urb support
	- fixed memory leak and vcc->tx_inuse starvation bug
	  when not enough memory left in vcc.

1.2:	- Fixed race condition in udsl_usb_send_data()
1.1:	- Turned off packet debugging
 
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/smp_lock.h>

#include <linux/atm.h>
#include <linux/atmdev.h>
#include "atmsar.h"

const char *udsl_version = "1.5A";

/*
#define DEBUG 1
#define DEBUG_PACKET 1
*/

#ifdef DEBUG
#define PDEBUG(arg...)  printk(KERN_DEBUG "SpeedTouch USB: " arg)
#else
#define PDEBUG(arg...)
#endif


#ifdef DEBUG_PACKET
#define PACKETDEBUG(arg...) udsl_print_packet ( arg )
#else
#define PACKETDEBUG(arg...)
#endif

#define DRIVER_AUTHOR	"Johan Verrept, Johan.Verrept@advalvas.be"
#define DRIVER_DESC	"Driver for the Alcatel Speed Touch USB ADSL modem"
#define DRIVER_VERSION	"1.5A"

#define SPEEDTOUCH_VENDORID		0x06b9
#define SPEEDTOUCH_PRODUCTID		0x4061

#define MAX_UDSL			1
#define UDSL_OBUF_SIZE			32768
#define UDSL_MINOR			48
#define UDSL_NUMBER_RCV_URBS		1
#define UDSL_NUMBER_SND_URBS		1
#define UDSL_RECEIVE_BUFFER_SIZE	64*53
/* max should be (1500 IP mtu + 2 ppp bytes + 32 * 5 cellheader overhead) for
 * PPPoA and (1500 + 14 + 32*5 cellheader overhead) for PPPoE */
#define UDSL_MAX_AAL5_MRU		2048
#define UDSL_SEND_CONTEXTS		8

#define UDSL_IOCTL_START		1
#define UDSL_IOCTL_STOP			2

/* endpoint declarations */

#define UDSL_ENDPOINT_DATA_OUT		0x07
#define UDSL_ENDPOINT_DATA_IN		0x87

/* usb_device_id struct */

static struct usb_device_id udsl_usb_ids[] = {
	{USB_DEVICE (SPEEDTOUCH_VENDORID, SPEEDTOUCH_PRODUCTID)},
	{}			/* list terminator */
};

/* not exporting this prevents the depmod from generating the map that causes the modules to be isnserted as driver.
 * we do not want this, we want the script run.
MODULE_DEVICE_TABLE ( usb, udsl_usb_ids);
*/
/* context declarations */

struct udsl_data_ctx {
	struct sk_buff *skb;
	struct urb *urb;
	struct udsl_instance_data *instance;
};

struct udsl_usb_send_data_context {
	struct urb *urb;
	struct sk_buff *skb;
	struct atm_vcc *vcc;
	struct udsl_instance_data *instance;
};

/*
 * UDSL main driver data
 */

struct udsl_instance_data {
	int minor;

	/* usb device part */
	struct usb_device *usb_dev;
	struct udsl_data_ctx *rcvbufs;
	struct sk_buff_head sndqueue;
	spinlock_t sndqlock;
	struct udsl_usb_send_data_context send_ctx[UDSL_NUMBER_SND_URBS];
	int data_started;

	/* atm device part */
	struct atm_dev *atm_dev;
	struct sk_buff_head recvqueue;
	spinlock_t recvqlock;

	struct atmsar_vcc_data *atmsar_vcc_list;
};

struct udsl_instance_data *minor_data[MAX_UDSL];

static const char udsl_driver_name[] = "Alcatel SpeedTouch USB";

/* data thread */
static int datapid = 0;
DECLARE_WAIT_QUEUE_HEAD (udsl_wqh);

#ifdef DEBUG_PACKET
int udsl_print_packet (const unsigned char *data, int len);
#endif

/*
 * atm driver prototypes and stuctures
 */

static int udsl_atm_open (struct atm_vcc *vcc, short vpi, int vci);
static void udsl_atm_close (struct atm_vcc *vcc);
static int udsl_atm_ioctl (struct atm_dev *dev, unsigned int cmd, void *arg);
static int udsl_atm_send (struct atm_vcc *vcc, struct sk_buff *skb);
int udsl_atm_proc_read (struct atm_dev *atm_dev, loff_t * pos, char *page);
void udsl_atm_processqueue (struct udsl_instance_data *instance);

static struct atmdev_ops udsl_atm_devops = {
	open:udsl_atm_open,
	close:udsl_atm_close,
	ioctl:udsl_atm_ioctl,
	send:udsl_atm_send,
	proc_read:udsl_atm_proc_read,
};

struct udsl_atm_dev_data {
	struct atmsar_vcc_data *atmsar_vcc;
};

/*
 * usb driver prototypes and structures
 */
static void *udsl_usb_probe (struct usb_device *dev, unsigned int ifnum,
			     const struct usb_device_id *id);
static void udsl_usb_disconnect (struct usb_device *dev, void *ptr);
int udsl_usb_send_data (struct udsl_instance_data *instance, struct atm_vcc *vcc,
			struct sk_buff *skb);
static int udsl_usb_ioctl (struct usb_device *hub, unsigned int code, void *user_data);
static int udsl_usb_cancelsends (struct udsl_instance_data *instance, struct atm_vcc *vcc);

static struct usb_driver udsl_usb_driver = {
	name:udsl_driver_name,
	probe:udsl_usb_probe,
	disconnect:udsl_usb_disconnect,
	ioctl:udsl_usb_ioctl,
	id_table:udsl_usb_ids,
};

/************
**   ATM   **
************/

/***************************************************************************
*
* init functions
*
****************************************************************************/

struct atm_dev *udsl_atm_startdevice (struct udsl_instance_data *instance,
				      struct atmdev_ops *devops)
{
	MOD_INC_USE_COUNT;
	instance->atm_dev = atm_dev_register (udsl_driver_name, devops, -1, 0);
	instance->atm_dev->dev_data = instance;
	instance->atm_dev->ci_range.vpi_bits = ATM_CI_MAX;
	instance->atm_dev->ci_range.vci_bits = ATM_CI_MAX;
	instance->atm_dev->signal = ATM_PHY_SIG_LOST;

	skb_queue_head_init (&instance->recvqueue);

	/* tmp init atm device, set to 128kbit */
	instance->atm_dev->link_rate = 128 * 1000 / 424;

	return instance->atm_dev;
}

void udsl_atm_stopdevice (struct udsl_instance_data *instance)
{
	struct atm_vcc *walk;
	struct sk_buff *skb;
	struct atm_dev *atm_dev;
	unsigned long iflags;
	if (!instance->atm_dev)
		return;

	atm_dev = instance->atm_dev;

	/* clean queue */
	spin_lock_irqsave (&instance->recvqlock, iflags);
	while (!skb_queue_empty (&instance->recvqueue)) {
		skb = skb_dequeue (&instance->recvqueue);
		dev_kfree_skb (skb);
	};
	spin_unlock_irqrestore (&instance->recvqlock, iflags);

	atm_dev->signal = ATM_PHY_SIG_LOST;
	walk = atm_dev->vccs;
	shutdown_atm_dev (atm_dev);

	for (; walk; walk = walk->next)
		wake_up (&walk->sleep);

	instance->atm_dev = NULL;
	MOD_DEC_USE_COUNT;
}

void udsl_atm_set_mac (struct udsl_instance_data *instance, const char mac[6])
{
	if (!instance->atm_dev)
		return;

	memcpy (instance->atm_dev->esi, mac, 6);
}

/***************************************************************************
*
* ATM helper functions
*
****************************************************************************/
struct sk_buff *udsl_atm_alloc_tx (struct atm_vcc *vcc, unsigned int size)
{
	struct atmsar_vcc_data *atmsar_vcc =
	    ((struct udsl_atm_dev_data *) vcc->dev_data)->atmsar_vcc;
	if (atmsar_vcc)
		return atmsar_alloc_tx (atmsar_vcc, size);

	printk (KERN_INFO
		"SpeedTouch USB: udsl_atm_alloc_tx could not find correct alloc_tx function !\n");
	return NULL;
}

int udsl_atm_proc_read (struct atm_dev *atm_dev, loff_t * pos, char *page)
{
	struct udsl_instance_data *instance = (struct udsl_instance_data *) atm_dev->dev_data;
	int left = *pos;

	if (!left--)
		return sprintf (page, "Speed Touch USB:%d (%02x:%02x:%02x:%02x:%02x:%02x)\n",
				instance->minor, atm_dev->esi[0], atm_dev->esi[1], atm_dev->esi[2],
				atm_dev->esi[3], atm_dev->esi[4], atm_dev->esi[5]);

	if (!left--)
		return sprintf (page, "AAL0: tx %d ( %d err ), rx %d ( %d err, %d drop )\n",
				atomic_read (&atm_dev->stats.aal0.tx),
				atomic_read (&atm_dev->stats.aal0.tx_err),
				atomic_read (&atm_dev->stats.aal0.rx),
				atomic_read (&atm_dev->stats.aal0.rx_err),
				atomic_read (&atm_dev->stats.aal0.rx_drop));

	if (!left--)
		return sprintf (page, "AAL5: tx %d ( %d err ), rx %d ( %d err, %d drop )\n",
				atomic_read (&atm_dev->stats.aal5.tx),
				atomic_read (&atm_dev->stats.aal5.tx_err),
				atomic_read (&atm_dev->stats.aal5.rx),
				atomic_read (&atm_dev->stats.aal5.rx_err),
				atomic_read (&atm_dev->stats.aal5.rx_drop));

	return 0;
}

/***************************************************************************
*
* ATM DATA functions
*
****************************************************************************/
int udsl_atm_send (struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct udsl_atm_dev_data *dev_data = (struct udsl_atm_dev_data *) vcc->dev_data;
	struct udsl_instance_data *instance = (struct udsl_instance_data *) vcc->dev->dev_data;
	struct sk_buff *new = NULL;
	int err;

	PDEBUG ("udsl_atm_send called\n");

	if (!dev_data)
		return -EINVAL;

	switch (vcc->qos.aal) {
	case ATM_AAL5:
		new = atmsar_encode_aal5 (dev_data->atmsar_vcc, skb);
		if (!new)
			goto nomem;
		if (new != skb) {
			vcc->pop (vcc, skb);
			skb = new;
		}
		new = atmsar_encode_rawcell (dev_data->atmsar_vcc, skb);
		if (!new)
			goto nomem;
		if (new != skb) {
			vcc->pop (vcc, skb);
			skb = new;
		}
		err = udsl_usb_send_data (instance, vcc, skb);
		PDEBUG ("udsl_atm_send successfull (%d)\n", err);
		return err;
		break;
	default:
		return -EINVAL;
	};

	PDEBUG ("udsl_atm_send unsuccessfull\n");
	return 0;
      nomem:
	vcc->pop (vcc, skb);
	return -ENOMEM;
};


void udsl_atm_processqueue (struct udsl_instance_data *instance)
{
	struct atmsar_vcc_data *atmsar_vcc = NULL;
	struct sk_buff *new = NULL, *skb = NULL, *tmp = NULL;
	unsigned long iflags;

	/* quick check */
	spin_lock_irqsave (&instance->recvqlock, iflags);
	if (skb_queue_empty (&instance->recvqueue)) {
		spin_unlock_irqrestore (&instance->recvqlock, iflags);
		return;
	}
	PDEBUG ("udsl_atm_processqueue entered\n");

	while (!skb_queue_empty (&instance->recvqueue)) {
		skb = skb_dequeue (&instance->recvqueue);

		spin_unlock_irqrestore (&instance->recvqlock, iflags);

		PDEBUG ("skb = %p, skb->len = %d\n", skb, skb->len);
		PACKETDEBUG (skb->data, skb->len);

		while ((new =
			atmsar_decode_rawcell (instance->atmsar_vcc_list, skb,
					       &atmsar_vcc)) != NULL) {
			PDEBUG ("(after cell processing)skb->len = %d\n", new->len);
			switch (atmsar_vcc->type) {
			case ATMSAR_TYPE_AAL5:
				tmp = new;
				new = atmsar_decode_aal5 (atmsar_vcc, new);

				/* we can't send NULL skbs upstream, the ATM layer would try to close the vcc... */
				if (new) {
					PDEBUG ("(after aal5 decap) skb->len = %d\n", new->len);
					if (new->len && atm_charge (atmsar_vcc->vcc, new->truesize)) {
						PACKETDEBUG (new->data, new->len);
						atmsar_vcc->vcc->push (atmsar_vcc->vcc, new);
					} else {
						PDEBUG
						    ("dropping incoming packet : rx_inuse = %d, vcc->sk->rcvbuf = %d, skb->true_size = %d\n",
						     atomic_read (&atmsar_vcc->vcc->rx_inuse),
						     atmsar_vcc->vcc->sk->rcvbuf, new->truesize);
						dev_kfree_skb (new);
					}
				} else {
					PDEBUG ("atmsar_decode_aal5 returned NULL!\n");
					dev_kfree_skb (tmp);
				}
				break;
			default:
				/* not supported. we delete the skb. */
				printk (KERN_INFO
					"SpeedTouch USB: illegal vcc type. Dropping packet.\n");
				dev_kfree_skb (new);
				break;
			}
		};
		dev_kfree_skb (skb);
		spin_lock_irqsave (&instance->recvqlock, iflags);
	};

	spin_unlock_irqrestore (&instance->recvqlock, iflags);
	PDEBUG ("udsl_atm_processqueue successfull\n");
}

int udsl_atm_processqueue_thread (void *data)
{
	int i = 0;
	DECLARE_WAITQUEUE (wait, current);

	lock_kernel ();
	daemonize ();

	/* Setup a nice name */
	strcpy (current->comm, "kSpeedSARd");

	add_wait_queue (&udsl_wqh, &wait);

	for (;;) {
		interruptible_sleep_on (&udsl_wqh);
		if (signal_pending (current))
			break;
		PDEBUG ("SpeedSARd awoke\n");
		for (i = 0; i < MAX_UDSL; i++)
			if (minor_data[i])
				udsl_atm_processqueue (minor_data[i]);
	};

	remove_wait_queue (&udsl_wqh, &wait);
	datapid = 0;
	PDEBUG ("SpeedSARd is exiting\n");
	return 0;
}


void udsl_atm_sar_start (void)
{
	datapid = kernel_thread (udsl_atm_processqueue_thread, (void *) NULL,
				 CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
}

void udsl_atm_sar_stop (void)
{
	int ret;
	/* Kill the thread */
	ret = kill_proc (datapid, SIGTERM, 1);
	if (!ret) {
		/* Wait 10 seconds */
		int count = 10 * 100;

		while (datapid && --count) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout (1);
		}

		if (!count)
			err ("giving up on killing SpeedSAR thread.");
	}
}

/***************************************************************************
*
* SAR driver entries
*
****************************************************************************/
int udsl_atm_open (struct atm_vcc *vcc, short vpi, int vci)
{
	struct udsl_atm_dev_data *dev_data;
	struct udsl_instance_data *instance = (struct udsl_instance_data *) vcc->dev->dev_data;

	PDEBUG ("udsl_atm_open called\n");

	/* at the moment only AAL5 support */
	if (vcc->qos.aal != ATM_AAL5)
		return -EINVAL;

	MOD_INC_USE_COUNT;
	dev_data =
	    (struct udsl_atm_dev_data *) kmalloc (sizeof (struct udsl_atm_dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;

	dev_data->atmsar_vcc =
	    atmsar_open (&(instance->atmsar_vcc_list), vcc, ATMSAR_TYPE_AAL5, vpi, vci, 0, 0,
			 ATMSAR_USE_53BYTE_CELL | ATMSAR_SET_PTI);
	if (!dev_data->atmsar_vcc) {
		kfree (dev_data);
		return -ENOMEM;	/* this is the only reason atmsar_open can fail... */
	}

	vcc->vpi = vpi;
	vcc->vci = vci;
	set_bit (ATM_VF_ADDR, &vcc->flags);
	set_bit (ATM_VF_PARTIAL, &vcc->flags);
	set_bit (ATM_VF_READY, &vcc->flags);
	vcc->dev_data = dev_data;
	vcc->alloc_tx = udsl_atm_alloc_tx;

	dev_data->atmsar_vcc->mtu = UDSL_MAX_AAL5_MRU;

	PDEBUG ("udsl_atm_open successfull\n");
	return 0;
}

void udsl_atm_close (struct atm_vcc *vcc)
{
	struct udsl_atm_dev_data *dev_data = (struct udsl_atm_dev_data *) vcc->dev_data;
	struct udsl_instance_data *instance = (struct udsl_instance_data *) vcc->dev->dev_data;

	PDEBUG ("udsl_atm_close called\n");

	/* freeing resources */
	/* cancel all sends on this vcc */
	udsl_usb_cancelsends (instance, vcc);

	atmsar_close (&(instance->atmsar_vcc_list), dev_data->atmsar_vcc);
	kfree (dev_data);
	vcc->dev_data = NULL;
	clear_bit (ATM_VF_PARTIAL, &vcc->flags);

	/* freeing address */
	vcc->vpi = ATM_VPI_UNSPEC;
	vcc->vci = ATM_VCI_UNSPEC;
	clear_bit (ATM_VF_ADDR, &vcc->flags);

	MOD_DEC_USE_COUNT;

	PDEBUG ("udsl_atm_close successfull\n");
	return;
};

int udsl_atm_ioctl (struct atm_dev *dev, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case ATM_QUERYLOOP:
		return put_user (ATM_LM_NONE, (int *) arg) ? -EFAULT : 0;
	default:
		return -ENOIOCTLCMD;
	}
};


/************
**   USB   **
************/

/***************************************************************************
*
* usb data functions
*
****************************************************************************/

struct udsl_cb {
	struct atm_vcc *vcc;
};

static void udsl_usb_send_data_complete (struct urb *urb)
{
	struct udsl_usb_send_data_context *ctx = (struct udsl_usb_send_data_context *) urb->context;
	struct udsl_instance_data *instance = ctx->instance;
	int err;
	unsigned long flags;

	PDEBUG ("udsl_usb_send_data_completion (vcc = %p, skb = %p, status %d)\n", ctx->vcc,
		ctx->skb, urb->status);

	ctx->vcc->pop (ctx->vcc, ctx->skb);
	ctx->skb = NULL;

	spin_lock_irqsave (&instance->sndqlock, flags);
	if (skb_queue_empty (&instance->sndqueue)) {
		spin_unlock_irqrestore (&instance->sndqlock, flags);
		return;
	}
	/* submit next skb */
	ctx->skb = skb_dequeue (&(instance->sndqueue));
	ctx->vcc = ((struct udsl_cb *) (ctx->skb->cb))->vcc;
	spin_unlock_irqrestore (&instance->sndqlock, flags);
	FILL_BULK_URB (urb,
		       instance->usb_dev,
		       usb_sndbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_OUT),
		       (unsigned char *) ctx->skb->data,
		       ctx->skb->len, (usb_complete_t) udsl_usb_send_data_complete, ctx);

	err = usb_submit_urb (urb, GFP_KERNEL);

	PDEBUG ("udsl_usb_send_data_completion (send packet %p with length %d), retval = %d\n",
		ctx->skb, ctx->skb->len, err);
}

int udsl_usb_cancelsends (struct udsl_instance_data *instance, struct atm_vcc *vcc)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave (&instance->sndqlock, flags);
	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++) {
		if (!instance->send_ctx[i].skb)
			continue;
		if (instance->send_ctx[i].vcc == vcc) {
			usb_unlink_urb (instance->send_ctx[i].urb);
			usb_free_urb (instance->send_ctx[i].urb);
			instance->send_ctx[i].vcc->pop (instance->send_ctx[i].vcc,
							instance->send_ctx[i].skb);
			instance->send_ctx[i].skb = NULL;
		}
	}
	spin_unlock_irqrestore (&instance->sndqlock, flags);

	return 0;
}

/**** send ******/
int udsl_usb_send_data (struct udsl_instance_data *instance, struct atm_vcc *vcc,
			struct sk_buff *skb)
{
	int err, i;
	struct urb *urb;
	unsigned long flags;

	PDEBUG ("udsl_usb_send_data entered, sending packet %p with length %d\n", skb, skb->len);

	if (!instance->data_started)
		return -EAGAIN;

	PACKETDEBUG (skb->data, skb->len);

	spin_lock_irqsave (&instance->sndqlock, flags);
	((struct udsl_cb *) skb->cb)->vcc = vcc;

	/* we are already queueing */
	if (!skb_queue_empty (&instance->sndqueue)) {
		skb_queue_tail (&instance->sndqueue, skb);
		spin_unlock_irqrestore (&instance->sndqlock, flags);
		PDEBUG ("udsl_usb_send_data: already queing, skb (0x%p) queued\n", skb);
		return 0;
	}

	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++)
		if (instance->send_ctx[i].skb == NULL)
			break;

	/* we must start queueing */
	if (i == UDSL_NUMBER_SND_URBS) {
		skb_queue_tail (&instance->sndqueue, skb);
		spin_unlock_irqrestore (&instance->sndqlock, flags);
		PDEBUG ("udsl_usb_send_data: skb (0x%p) queued\n", skb);
		return 0;
	};

	/* init context */
	urb = instance->send_ctx[i].urb;
	instance->send_ctx[i].skb = skb;
	instance->send_ctx[i].vcc = vcc;
	instance->send_ctx[i].instance = instance;

	spin_unlock_irqrestore (&instance->sndqlock, flags);

	/* submit packet */
	FILL_BULK_URB (urb,
		       instance->usb_dev,
		       usb_sndbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_OUT),
		       (unsigned char *) skb->data,
		       skb->len,
		       (usb_complete_t) udsl_usb_send_data_complete, &(instance->send_ctx[i])
	    );

	err = usb_submit_urb (urb, GFP_KERNEL);

	if (err != 0)
		skb_unlink (skb);

	PDEBUG ("udsl_usb_send_data done (retval = %d)\n", err);
	return err;
}

/********* receive *******/
void udsl_usb_data_receive (struct urb *urb)
{
	struct udsl_data_ctx *ctx;
	struct udsl_instance_data *instance;
	unsigned long flags;

	if (!urb)
		return;

	PDEBUG ("udsl_usb_receive_data entered, got packet %p with length %d an status %d\n", urb,
		urb->actual_length, urb->status);

	ctx = (struct udsl_data_ctx *) urb->context;
	if (!ctx || !ctx->skb)
		return;

	instance = ctx->instance;

	switch (urb->status) {
	case 0:
		PDEBUG ("udsl_usb_data_receive: processing urb with ctx %p, urb %p (%p), skb %p\n",
			ctx, ctx ? ctx->urb : NULL, urb, ctx ? ctx->skb : NULL);
		/* update the skb structure */
		skb_put (ctx->skb, urb->actual_length);

		/* queue the skb for processing and wake the SAR */
		spin_lock_irqsave (&instance->recvqlock, flags);
		skb_queue_tail (&instance->recvqueue, ctx->skb);
		spin_unlock_irqrestore (&instance->recvqlock, flags);
		wake_up (&udsl_wqh);
		/* get a new skb */
		ctx->skb = dev_alloc_skb (UDSL_RECEIVE_BUFFER_SIZE);
		if (!ctx->skb) {
			PDEBUG ("No skb, loosing urb.\n");
			usb_free_urb (ctx->urb);
			ctx->urb = NULL;
			return;
		}
		break;
	case -EPIPE:		/* stall or babble */
		usb_clear_halt (urb->dev, usb_rcvbulkpipe (urb->dev, UDSL_ENDPOINT_DATA_IN));
		break;
	case -ENOENT:		/* buffer was unlinked */
	case -EILSEQ:		/* unplug or timeout */
	case -ETIMEDOUT:	/* unplug or timeout */
		/* 
		 * we don't do anything here and we don't resubmit
		 */
		return;
	}

	FILL_BULK_URB (urb,
		       instance->usb_dev,
		       usb_rcvbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_IN),
		       (unsigned char *) ctx->skb->data,
		       UDSL_RECEIVE_BUFFER_SIZE, (usb_complete_t) udsl_usb_data_receive, ctx);
	usb_submit_urb (urb, GFP_KERNEL);
	return;
};

int udsl_usb_data_init (struct udsl_instance_data *instance)
{
	int i, succes;

	if (instance->data_started)
		return 1;

	/* set alternate setting 1 on interface 1 */
	usb_set_interface (instance->usb_dev, 1, 2);

	PDEBUG ("max packet size on endpoint %d is %d\n", UDSL_ENDPOINT_DATA_OUT,
		usb_maxpacket (instance->usb_dev,
			       usb_sndbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_OUT), 0));

	instance->rcvbufs =
	    (struct udsl_data_ctx *) kmalloc (sizeof (struct udsl_data_ctx) * UDSL_NUMBER_RCV_URBS,
					      GFP_KERNEL);
	if (!instance->rcvbufs)
		return -ENOMEM;

	memset (instance->rcvbufs, 0, sizeof (struct udsl_data_ctx) * UDSL_NUMBER_RCV_URBS);

	skb_queue_head_init (&instance->sndqueue);

	for (i = 0, succes = 0; i < UDSL_NUMBER_RCV_URBS; i++) {
		struct udsl_data_ctx *ctx = &(instance->rcvbufs[i]);

		ctx->urb = NULL;
		ctx->skb = dev_alloc_skb (UDSL_RECEIVE_BUFFER_SIZE);
		if (!ctx->skb)
			continue;

		ctx->urb = usb_alloc_urb (0, GFP_KERNEL);
		if (!ctx->urb) {
			kfree_skb (ctx->skb);
			ctx->skb = NULL;
			break;
		};

		FILL_BULK_URB (ctx->urb,
			       instance->usb_dev,
			       usb_rcvbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_IN),
			       (unsigned char *) ctx->skb->data,
			       UDSL_RECEIVE_BUFFER_SIZE,
			       (usb_complete_t) udsl_usb_data_receive, ctx);


		ctx->instance = instance;

		PDEBUG ("udsl_usb_data_init: usb with skb->truesize = %d (Asked for %d)\n",
			ctx->skb->truesize, UDSL_RECEIVE_BUFFER_SIZE);

		if (usb_submit_urb (ctx->urb, GFP_KERNEL) < 0)
			PDEBUG ("udsl_usb_data_init: Submit failed, loosing urb.\n");
		else
			succes++;
	}

	PDEBUG ("udsl_usb_data_init %d urb%s queued for receive\n", succes,
		(succes != 1) ? "s" : "");

	for (i = 0, succes = 0; i < UDSL_NUMBER_SND_URBS; i++) {
		instance->send_ctx[i].urb = usb_alloc_urb (0, GFP_KERNEL);
		PDEBUG ("udsl_usb_data_init: send urb allocted address %p\n",
			instance->send_ctx[i].urb);
		if (instance->send_ctx[i].urb)
			succes++;
	}

	PDEBUG ("udsl_usb_data_init %d urb%s queued for send\n", succes, (succes != 1) ? "s" : "");

	instance->data_started = 1;
	instance->atm_dev->signal = ATM_PHY_SIG_FOUND;

	return 0;
}

int udsl_usb_data_exit (struct udsl_instance_data *instance)
{
	int i;

	if (!instance->data_started)
		return 0;

	if (!instance->rcvbufs)
		return 0;

	/* destroy urbs */
	for (i = 0; i < UDSL_NUMBER_RCV_URBS; i++) {
		struct udsl_data_ctx *ctx = &(instance->rcvbufs[i]);

		if ((!ctx->urb) || (!ctx->skb))
			continue;

		if (ctx->urb->status == -EINPROGRESS)
			usb_unlink_urb (ctx->urb);

		usb_free_urb (ctx->urb);
		kfree_skb (ctx->skb);
		ctx->skb = NULL;
	}

	for (i = 0; i < UDSL_NUMBER_SND_URBS; i++) {
		struct udsl_usb_send_data_context *ctx = &(instance->send_ctx[i]);

		if (ctx->urb->status == -EINPROGRESS)
			usb_unlink_urb (ctx->urb);

		if (ctx->skb)
			ctx->vcc->pop (ctx->vcc, ctx->skb);
		ctx->skb = NULL;

		usb_free_urb (ctx->urb);

	}

	/* free receive contexts */
	kfree (instance->rcvbufs);
	instance->rcvbufs = NULL;

	instance->data_started = 0;
	instance->atm_dev->signal = ATM_PHY_SIG_LOST;

	return 0;
};


/***************************************************************************
*
* usb driver entries
*
****************************************************************************/
#define hex2int(c) ( (c >= '0')&&(c <= '9') ?  (c - '0') : ((c & 0xf)+9) )


static int udsl_usb_ioctl (struct usb_device *dev, unsigned int code, void *user_data)
{
	struct udsl_instance_data *instance;
	int i;

	for (i = 0; i < MAX_UDSL; i++)
		if (minor_data[i] && (minor_data[i]->usb_dev == dev))
			break;

	if (i == MAX_UDSL)
		return -EINVAL;

	instance = minor_data[i];

	switch (code) {
	case UDSL_IOCTL_START:
		return udsl_usb_data_init (instance);
		break;
	case UDSL_IOCTL_STOP:
		return udsl_usb_data_exit (instance);
		break;
	default:
		break;
	}
	return -EINVAL;
}

void *udsl_usb_probe (struct usb_device *dev, unsigned int ifnum, const struct usb_device_id *id)
{
	int i;
	unsigned char mac[6];
	unsigned char mac_str[13];
	struct udsl_instance_data *instance = NULL;

	PDEBUG ("Trying device with Vendor=0x%x, Product=0x%x, ifnum %d\n",
		dev->descriptor.idVendor, dev->descriptor.idProduct, ifnum);

	if ((dev->descriptor.bDeviceClass != USB_CLASS_VENDOR_SPEC) ||
	    (dev->descriptor.idVendor != SPEEDTOUCH_VENDORID) ||
	    (dev->descriptor.idProduct != SPEEDTOUCH_PRODUCTID) || (ifnum != 1))
		return NULL;

	MOD_INC_USE_COUNT;

	for (i = 0; i < MAX_UDSL; i++)
		if (minor_data[i] == NULL)
			break;

	if (i >= MAX_UDSL) {
		printk (KERN_INFO "No minor table space available for SpeedTouch USB\n");
		return NULL;
	};

	PDEBUG ("Device Accepted, assigning minor %d\n", i);

	/* device init */
	instance = kmalloc (sizeof (struct udsl_instance_data), GFP_KERNEL);
	if (!instance) {
		PDEBUG ("No memory for Instance data!\n");
		return NULL;
	}

	/* initialize structure */
	memset (instance, 0, sizeof (struct udsl_instance_data));
	instance->minor = i;
	instance->usb_dev = dev;
	instance->rcvbufs = NULL;
	spin_lock_init (&instance->sndqlock);
	spin_lock_init (&instance->recvqlock);

	udsl_atm_startdevice (instance, &udsl_atm_devops);

	/* set MAC address, it is stored in the serial number */
	usb_string (instance->usb_dev, instance->usb_dev->descriptor.iSerialNumber, mac_str, 13);
	for (i = 0; i < 6; i++)
		mac[i] = (hex2int (mac_str[i * 2]) * 16) + (hex2int (mac_str[i * 2 + 1]));

	PDEBUG ("MAC is %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4],
		mac[5]);
	udsl_atm_set_mac (instance, mac);

	minor_data[instance->minor] = instance;

	return instance;
}

void udsl_usb_disconnect (struct usb_device *dev, void *ptr)
{
	struct udsl_instance_data *instance = (struct udsl_instance_data *) ptr;
	int i = instance->minor;

	/* unlinking receive buffers */
	udsl_usb_data_exit (instance);

	/* removing atm device */
	if (instance->atm_dev)
		udsl_atm_stopdevice (instance);

	PDEBUG ("disconnecting minor %d\n", i);

	while (MOD_IN_USE > 1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout (1);
	}

	kfree (instance);
	minor_data[i] = NULL;

	MOD_DEC_USE_COUNT;
}

/***************************************************************************
*
* Driver Init
*
****************************************************************************/

int udsl_usb_init (void)
{
	int i;

	PDEBUG ("Initializing SpeedTouch Driver Version %s\n", udsl_version);

	for (i = 0; i < MAX_UDSL; i++)
		minor_data[i] = NULL;

	init_waitqueue_head (&udsl_wqh);
	udsl_atm_sar_start ();

	return usb_register (&udsl_usb_driver);
}

int udsl_usb_cleanup (void)
{
	/* killing threads */
	udsl_atm_sar_stop ();
	usb_deregister (&udsl_usb_driver);
	return 0;
}

#ifdef MODULE
int init_module (void)
{
	return udsl_usb_init ();
}

int cleanup_module (void)
{
	return udsl_usb_cleanup ();
}
#endif

#ifdef DEBUG_PACKET
/*******************************************************************************
*
* Debug 
*
*******************************************************************************/

int udsl_print_packet (const unsigned char *data, int len)
{
	unsigned char buffer[256];
	int i = 0, j = 0;

	for (i = 0; i < len;) {
		buffer[0] = '\0';
		sprintf (buffer, "%.3d :", i);
		for (j = 0; (j < 16) && (i < len); j++, i++) {
			sprintf (buffer, "%s %2.2x", buffer, data[i]);
		}
		PDEBUG ("%s\n", buffer);
	}
	return i;
};

#endif				/* PACKETDEBUG */

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_LICENSE ("GPL");
