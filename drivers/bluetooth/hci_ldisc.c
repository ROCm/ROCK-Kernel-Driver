/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 * BlueZ HCI UART driver.
 *
 * $Id: hci_ldisc.c,v 1.2 2002/04/17 17:37:20 maxk Exp $    
 */
#define VERSION "2.0"

#include <linux/config.h>
#include <linux/module.h>

#include <linux/version.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include "hci_uart.h"

#ifndef HCI_UART_DEBUG
#undef  BT_DBG
#define BT_DBG( A... )
#undef  BT_DMP
#define BT_DMP( A... )
#endif

static struct hci_uart_proto *hup[HCI_UART_MAX_PROTO];

int hci_uart_register_proto(struct hci_uart_proto *p)
{
	if (p->id >= HCI_UART_MAX_PROTO)
		return -EINVAL;

	if (hup[p->id])
		return -EEXIST;

	hup[p->id] = p;
	return 0;
}

int hci_uart_unregister_proto(struct hci_uart_proto *p)
{
	if (p->id >= HCI_UART_MAX_PROTO)
		return -EINVAL;

	if (!hup[p->id])
		return -EINVAL;

	hup[p->id] = NULL;
	return 0;
}

static struct hci_uart_proto *n_hci_get_proto(unsigned int id)
{
	if (id >= HCI_UART_MAX_PROTO)
		return NULL;
	return hup[id];
}

/* ------- Interface to HCI layer ------ */
/* Initialize device */
static int n_hci_open(struct hci_dev *hdev)
{
	BT_DBG("%s %p", hdev->name, hdev);

	/* Nothing to do for UART driver */

	set_bit(HCI_RUNNING, &hdev->flags);
	return 0;
}

/* Reset device */
static int n_hci_flush(struct hci_dev *hdev)
{
	struct n_hci *n_hci  = (struct n_hci *) hdev->driver_data;
	struct tty_struct *tty = n_hci->tty;

	BT_DBG("hdev %p tty %p", hdev, tty);

	/* Drop TX queue */
	skb_queue_purge(&n_hci->txq);

	/* Flush any pending characters in the driver and discipline. */
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);

	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);

	if (n_hci->proto->flush)
		n_hci->proto->flush(n_hci);

	return 0;
}

/* Close device */
static int n_hci_close(struct hci_dev *hdev)
{
	BT_DBG("hdev %p", hdev);

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	n_hci_flush(hdev);
	return 0;
}

static int n_hci_tx_wakeup(struct n_hci *n_hci)
{
	struct hci_dev *hdev = &n_hci->hdev;
	
	if (test_and_set_bit(N_HCI_SENDING, &n_hci->tx_state)) {
		set_bit(N_HCI_TX_WAKEUP, &n_hci->tx_state);
		return 0;
	}

	BT_DBG("");
	do {
		register struct sk_buff *skb;
		register int len;

		clear_bit(N_HCI_TX_WAKEUP, &n_hci->tx_state);

		if (!(skb = skb_dequeue(&n_hci->txq)))
			break;

		len = n_hci->proto->send(n_hci, skb->data, skb->len);
		n_hci->hdev.stat.byte_tx += len;

		if (len == skb->len) {
			/* Complete frame was sent */

			switch (skb->pkt_type) {
			case HCI_COMMAND_PKT:
				hdev->stat.cmd_tx++;
				break;

			case HCI_ACLDATA_PKT:
				hdev->stat.acl_tx++;
				break;

			case HCI_SCODATA_PKT:
				hdev->stat.cmd_tx++;
				break;
			};

			kfree_skb(skb);
		} else {
			/* Subtract sent part and requeue  */
			skb_pull(skb, len);
			skb_queue_head(&n_hci->txq, skb);
		}
	} while (test_bit(N_HCI_TX_WAKEUP, &n_hci->tx_state));
	clear_bit(N_HCI_SENDING, &n_hci->tx_state);
	return 0;
}

/* Send frames from HCI layer */
static int n_hci_send_frame(struct sk_buff *skb)
{
	struct hci_dev* hdev = (struct hci_dev *) skb->dev;
	struct tty_struct *tty;
	struct n_hci *n_hci;

	if (!hdev) {
		BT_ERR("Frame for uknown device (hdev=NULL)");
		return -ENODEV;
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	n_hci = (struct n_hci *) hdev->driver_data;
	tty = n_hci->tty;

	BT_DBG("%s: type %d len %d", hdev->name, skb->pkt_type, skb->len);

	if (n_hci->proto->preq) {
		skb = n_hci->proto->preq(n_hci, skb);
		if (!skb)
			return 0;
	}
	
	skb_queue_tail(&n_hci->txq, skb);
	n_hci_tx_wakeup(n_hci);
	return 0;
}

static void n_hci_destruct(struct hci_dev *hdev)
{
	struct n_hci *n_hci;

	if (!hdev) return;

	BT_DBG("%s", hdev->name);

	n_hci = (struct n_hci *) hdev->driver_data;
	kfree(n_hci);

	MOD_DEC_USE_COUNT;
}

/* ------ LDISC part ------ */
/* n_hci_tty_open
 * 
 *     Called when line discipline changed to N_HCI.
 *
 * Arguments:
 *     tty    pointer to tty info structure
 * Return Value:    
 *     0 if success, otherwise error code
 */
static int n_hci_tty_open(struct tty_struct *tty)
{
	struct n_hci *n_hci = (void *)tty->disc_data;

	BT_DBG("tty %p", tty);

	if (n_hci)
		return -EEXIST;

	if (!(n_hci = kmalloc(sizeof(struct n_hci), GFP_KERNEL))) {
		BT_ERR("Can't allocate controll structure");
		return -ENFILE;
	}
	memset(n_hci, 0, sizeof(struct n_hci));

	tty->disc_data = n_hci;
	n_hci->tty = tty;

	spin_lock_init(&n_hci->rx_lock);
	skb_queue_head_init(&n_hci->txq);

	/* Flush any pending characters in the driver and line discipline */
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);

	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	
	MOD_INC_USE_COUNT;
	return 0;
}

/* n_hci_tty_close()
 *
 *    Called when the line discipline is changed to something
 *    else, the tty is closed, or the tty detects a hangup.
 */
static void n_hci_tty_close(struct tty_struct *tty)
{
	struct n_hci *n_hci = (void *)tty->disc_data;

	BT_DBG("tty %p", tty);

	/* Detach from the tty */
	tty->disc_data = NULL;

	if (n_hci) {
		struct hci_dev *hdev = &n_hci->hdev;
		n_hci_close(hdev);

		if (test_and_clear_bit(N_HCI_PROTO_SET, &n_hci->flags)) {
			n_hci->proto->close(n_hci);
			hci_unregister_dev(hdev);
		}
				
		MOD_DEC_USE_COUNT;
	}
}

/* n_hci_tty_wakeup()
 *
 *    Callback for transmit wakeup. Called when low level
 *    device driver can accept more send data.
 *
 * Arguments:        tty    pointer to associated tty instance data
 * Return Value:    None
 */
static void n_hci_tty_wakeup( struct tty_struct *tty )
{
	struct n_hci *n_hci = (void *)tty->disc_data;

	BT_DBG("");

	if (!n_hci)
		return;

	tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);

	if (tty != n_hci->tty)
		return;

	n_hci_tx_wakeup(n_hci);
}

/* n_hci_tty_room()
 * 
 *    Callback function from tty driver. Return the amount of 
 *    space left in the receiver's buffer to decide if remote
 *    transmitter is to be throttled.
 *
 * Arguments:        tty    pointer to associated tty instance data
 * Return Value:    number of bytes left in receive buffer
 */
static int n_hci_tty_room (struct tty_struct *tty)
{
	return 65536;
}

/* n_hci_tty_receive()
 * 
 *     Called by tty low level driver when receive data is
 *     available.
 *     
 * Arguments:  tty          pointer to tty isntance data
 *             data         pointer to received data
 *             flags        pointer to flags for data
 *             count        count of received data in bytes
 *     
 * Return Value:    None
 */
static void n_hci_tty_receive(struct tty_struct *tty, const __u8 * data, char *flags, int count)
{
	struct n_hci *n_hci = (void *)tty->disc_data;
	
	if (!n_hci || tty != n_hci->tty)
		return;

	if (!test_bit(N_HCI_PROTO_SET, &n_hci->flags))
		return;
	
	spin_lock(&n_hci->rx_lock);
	n_hci->proto->recv(n_hci, (void *) data, count);
	n_hci->hdev.stat.byte_rx += count;
	spin_unlock(&n_hci->rx_lock);

	if (test_and_clear_bit(TTY_THROTTLED,&tty->flags) && tty->driver.unthrottle)
		tty->driver.unthrottle(tty);
}

static int n_hci_register_dev(struct n_hci *n_hci)
{
	struct hci_dev *hdev;

	BT_DBG("");

	/* Initialize and register HCI device */
	hdev = &n_hci->hdev;

	hdev->type = HCI_UART;
	hdev->driver_data = n_hci;

	hdev->open  = n_hci_open;
	hdev->close = n_hci_close;
	hdev->flush = n_hci_flush;
	hdev->send  = n_hci_send_frame;
	hdev->destruct = n_hci_destruct;

	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device %s", hdev->name);
		return -ENODEV;
	}
	MOD_INC_USE_COUNT;
	return 0;
}

static int n_hci_set_proto(struct n_hci *n_hci, int id)
{
	struct hci_uart_proto *p;
	int err;	
	
	p = n_hci_get_proto(id);
	if (!p)
		return -EPROTONOSUPPORT;

	err = p->open(n_hci);
	if (err)
		return err;

	n_hci->proto = p;

	err = n_hci_register_dev(n_hci);
	if (err) {
		p->close(n_hci);
		return err;
	}
	return 0;
}

/* n_hci_tty_ioctl()
 *
 *    Process IOCTL system call for the tty device.
 *
 * Arguments:
 *
 *    tty        pointer to tty instance data
 *    file       pointer to open file object for device
 *    cmd        IOCTL command code
 *    arg        argument for IOCTL call (cmd dependent)
 *
 * Return Value:    Command dependent
 */
static int n_hci_tty_ioctl(struct tty_struct *tty, struct file * file,
                            unsigned int cmd, unsigned long arg)
{
	struct n_hci *n_hci = (void *)tty->disc_data;
	int err = 0;

	BT_DBG("");

	/* Verify the status of the device */
	if (!n_hci)
		return -EBADF;

	switch (cmd) {
	case HCIUARTSETPROTO:
		if (!test_and_set_bit(N_HCI_PROTO_SET, &n_hci->flags)) {
			err = n_hci_set_proto(n_hci, arg);
			if (err) {
				clear_bit(N_HCI_PROTO_SET, &n_hci->flags);
				return err;
			}
			tty->low_latency = 1;
		} else	
			return -EBUSY;

	case HCIUARTGETPROTO:
		if (test_bit(N_HCI_PROTO_SET, &n_hci->flags))
			return n_hci->proto->id;
		return -EUNATCH;
		
	default:
		err = n_tty_ioctl(tty, file, cmd, arg);
		break;
	};

	return err;
}

/*
 * We don't provide read/write/poll interface for user space.
 */
static ssize_t n_hci_tty_read(struct tty_struct *tty, struct file *file, unsigned char *buf, size_t nr)
{
	return 0;
}
static ssize_t n_hci_tty_write(struct tty_struct *tty, struct file *file, const unsigned char *data, size_t count)
{
	return 0;
}
static unsigned int n_hci_tty_poll(struct tty_struct *tty, struct file *filp, poll_table *wait)
{
	return 0;
}

#ifdef CONFIG_BLUEZ_HCIUART_H4
int h4_init(void);
int h4_deinit(void);
#endif

int __init n_hci_init(void)
{
	static struct tty_ldisc n_hci_ldisc;
	int err;

	BT_INFO("BlueZ HCI UART driver ver %s Copyright (C) 2000,2001 Qualcomm Inc", 
		VERSION);
	BT_INFO("Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>");

	/* Register the tty discipline */

	memset(&n_hci_ldisc, 0, sizeof (n_hci_ldisc));
	n_hci_ldisc.magic       = TTY_LDISC_MAGIC;
	n_hci_ldisc.name        = "n_hci";
	n_hci_ldisc.open        = n_hci_tty_open;
	n_hci_ldisc.close       = n_hci_tty_close;
	n_hci_ldisc.read        = n_hci_tty_read;
	n_hci_ldisc.write       = n_hci_tty_write;
	n_hci_ldisc.ioctl       = n_hci_tty_ioctl;
	n_hci_ldisc.poll        = n_hci_tty_poll;
	n_hci_ldisc.receive_room= n_hci_tty_room;
	n_hci_ldisc.receive_buf = n_hci_tty_receive;
	n_hci_ldisc.write_wakeup= n_hci_tty_wakeup;

	if ((err = tty_register_ldisc(N_HCI, &n_hci_ldisc))) {
		BT_ERR("Can't register HCI line discipline (%d)", err);
		return err;
	}

#ifdef CONFIG_BLUEZ_HCIUART_H4
	h4_init();
#endif
	
	return 0;
}

void n_hci_cleanup(void)
{
	int err;

#ifdef CONFIG_BLUEZ_HCIUART_H4
	h4_deinit();
#endif

	/* Release tty registration of line discipline */
	if ((err = tty_register_ldisc(N_HCI, NULL)))
		BT_ERR("Can't unregister HCI line discipline (%d)", err);
}

module_init(n_hci_init);
module_exit(n_hci_cleanup);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>");
MODULE_DESCRIPTION("BlueZ HCI UART driver ver " VERSION);
MODULE_LICENSE("GPL");
