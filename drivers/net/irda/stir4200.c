/*****************************************************************************
*
* Filename:      stir4200.c
* Version:       0.4
* Description:   Irda SigmaTel USB Dongle
* Status:        Experimental
* Author:        Stephen Hemminger <shemminger@osdl.org>
*
*  	Based on earlier driver by Paul Stewart <stewart@parc.com>
*
*	Copyright (C) 2000, Roman Weissgaerber <weissg@vienna.at>
*	Copyright (C) 2001, Dag Brattli <dag@brattli.net>
*	Copyright (C) 2001, Jean Tourrilhes <jt@hpl.hp.com>
*	Copyright (C) 2004, Stephen Hemminger <shemminger@osdl.org>
*
*	This program is free software; you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation; either version 2 of the License, or
*	(at your option) any later version.
*
*	This program is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with this program; if not, write to the Free Software
*	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/

/*
 * This dongle does no framing, and requires polling to receive the
 * data.  The STIr4200 has bulk in and out endpoints just like
 * usr-irda devices, but the data it sends and receives is raw; like
 * irtty, it needs to call the wrap and unwrap functions to add and
 * remove SOF/BOF and escape characters to/from the frame.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/suspend.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/irda/irda_device.h>
#include <net/irda/wrapper.h>
#include <net/irda/crc.h>
#include <linux/crc32.h>

MODULE_AUTHOR("Stephen Hemminger <shemminger@osdl.org>");
MODULE_DESCRIPTION("IrDA-USB Dongle Driver for SigmaTel STIr4200");
MODULE_LICENSE("GPL");

static int qos_mtt_bits = 0x07;	/* 1 ms or more */
module_param(qos_mtt_bits, int, 0);
MODULE_PARM_DESC(qos_mtt_bits, "Minimum Turn Time");

static int rx_sensitivity = 1;	/* FIR 0..4, SIR 0..6 */
module_param(rx_sensitivity, int, 0);
MODULE_PARM_DESC(rx_sensitivity, "Set Receiver sensitivity (0-6, 0 is most sensitive)");

static int tx_power = 0;	/* 0 = highest ... 3 = lowest */
module_param(tx_power, int, 0);
MODULE_PARM_DESC(tx_power, "Set Transmitter power (0-3, 0 is highest power)");

static int rx_interval = 5;  /* milliseconds */
module_param(rx_interval, int, 0);
MODULE_PARM_DESC(rx_interval, "Receive polling interval (ms)");

#define STIR_IRDA_HEADER  	4
#define CTRL_TIMEOUT		100	   /* milliseconds */
#define TRANSMIT_TIMEOUT	200	   /* milliseconds */
#define STIR_FIFO_SIZE		4096
#define NUM_RX_URBS		2

enum FirChars {
	FIR_CE   = 0x7d,
	FIR_XBOF = 0x7f,
	FIR_EOF  = 0x7e,
};

enum StirRequests {
	REQ_WRITE_REG =		0x00,
	REQ_READ_REG =		0x01,
	REQ_READ_ROM =		0x02,
	REQ_WRITE_SINGLE =	0x03,
};

/* Register offsets */
enum StirRegs {
	REG_RSVD=0,
	REG_MODE,
	REG_PDCLK,
	REG_CTRL1,
	REG_CTRL2,
	REG_FIFOCTL,
	REG_FIFOLSB,
	REG_FIFOMSB,
	REG_DPLL,
	REG_IRDIG,
	REG_TEST=15,
};

enum StirModeMask {
	MODE_FIR = 0x80,
	MODE_SIR = 0x20,
	MODE_ASK = 0x10,
	MODE_FASTRX = 0x08,
	MODE_FFRSTEN = 0x04,
	MODE_NRESET = 0x02,
	MODE_2400 = 0x01,
};

enum StirPdclkMask {
	PDCLK_4000000 = 0x02,
	PDCLK_115200 = 0x09,
	PDCLK_57600 = 0x13,
	PDCLK_38400 = 0x1D,
	PDCLK_19200 = 0x3B,
	PDCLK_9600 = 0x77,
	PDCLK_2400 = 0xDF,
};

enum StirCtrl1Mask {
	CTRL1_SDMODE = 0x80,
	CTRL1_RXSLOW = 0x40,
	CTRL1_TXPWD = 0x10,
	CTRL1_RXPWD = 0x08,
	CTRL1_SRESET = 0x01,
};

enum StirCtrl2Mask {
	CTRL2_SPWIDTH = 0x08,
	CTRL2_REVID = 0x03,
};

enum StirFifoCtlMask {
	FIFOCTL_EOF = 0x80,
	FIFOCTL_UNDER = 0x40,
	FIFOCTL_OVER = 0x20,
	FIFOCTL_DIR = 0x10,
	FIFOCTL_CLR = 0x08,
	FIFOCTL_EMPTY = 0x04,
	FIFOCTL_RXERR = 0x02,
	FIFOCTL_TXERR = 0x01,
};

enum StirDiagMask {
	IRDIG_RXHIGH = 0x80,
	IRDIG_RXLOW = 0x40,
};

enum StirTestMask {
	TEST_PLLDOWN = 0x80,
	TEST_LOOPIR = 0x40,
	TEST_LOOPUSB = 0x20,
	TEST_TSTENA = 0x10,
	TEST_TSTOSC = 0x0F,
};

enum StirState {
	STIR_STATE_RECEIVING=0,
	STIR_STATE_TXREADY,
};

struct stir_cb {
        struct usb_device *usbdev;      /* init: probe_irda */
        struct net_device *netdev;      /* network layer */
        struct irlap_cb   *irlap;       /* The link layer we are binded to */
        struct net_device_stats stats;	/* network statistics */
        struct qos_info   qos;
	unsigned long     state;
	unsigned 	  speed;	/* Current speed */

	wait_queue_head_t thr_wait;	/* transmit thread wakeup */
	struct completion thr_exited;
	pid_t		  thr_pid;

	unsigned int	  tx_bulkpipe;
	void		 *tx_data;	/* wrapped data out */
	unsigned	  tx_len;
	unsigned	  tx_newspeed;
	unsigned	  tx_mtt;

	unsigned int	  rx_intpipe;
	iobuff_t  	  rx_buff;	/* receive unwrap state machine */
	struct timespec	  rx_time;

	struct urb	 *rx_urbs[NUM_RX_URBS];
	void		 *rx_data[NUM_RX_URBS];
};


/* These are the currently known USB ids */
static struct usb_device_id dongles[] = {
    /* SigmaTel, Inc,  STIr4200 IrDA/USB Bridge */
    { USB_DEVICE(0x066f, 0x4200) },
    { }
};

MODULE_DEVICE_TABLE(usb, dongles);

static int fifo_txwait(struct stir_cb *stir, unsigned space);
static void stir_usb_receive(struct urb *urb, struct pt_regs *regs);

/* Send control message to set dongle register */
static int write_reg(struct stir_cb *stir, __u16 reg, __u8 value)
{
	struct usb_device *dev = stir->usbdev;

	pr_debug("%s: write reg %d = 0x%x\n",
		 stir->netdev->name, reg, value);
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			       REQ_WRITE_SINGLE,
			       USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE,
			       value, reg, NULL, 0,
			       MSECS_TO_JIFFIES(CTRL_TIMEOUT));
}

/* Send control message to read multiple registers */
static inline int read_reg(struct stir_cb *stir, __u16 reg,
		    __u8 *data, __u16 count)
{
	struct usb_device *dev = stir->usbdev;

	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			       REQ_READ_REG,
			       USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			       0, reg, data, count,
			       MSECS_TO_JIFFIES(CTRL_TIMEOUT));
}

/*
 * Prepare a FIR IrDA frame for transmission to the USB dongle.  The
 * FIR transmit frame is documented in the datasheet.  It consists of
 * a two byte 0x55 0xAA sequence, two little-endian length bytes, a
 * sequence of exactly 16 XBOF bytes of 0x7E, two BOF bytes of 0x7E,
 * then the data escaped as follows:
 *
 *    0x7D -> 0x7D 0x5D
 *    0x7E -> 0x7D 0x5E
 *    0x7F -> 0x7D 0x5F
 *
 * Then, 4 bytes of little endian (stuffed) FCS follow, then two
 * trailing EOF bytes of 0x7E.
 */
static inline __u8 *stuff_fir(__u8 *p, __u8 c)
{
	switch(c) {
	case 0x7d:
	case 0x7e:
	case 0x7f:
		*p++ = 0x7d;
		c ^= IRDA_TRANS;
		/* fall through */
	default:
		*p++ = c;
	}
	return p;
}

/* Take raw data in skb and put it wrapped into buf */
static unsigned wrap_fir_skb(const struct sk_buff *skb, __u8 *buf)
{
	__u8 *ptr = buf;
	__u32 fcs = ~(crc32_le(~0, skb->data, skb->len));
	__u16 wraplen;
	int i;

	/* Header */
	buf[0] = 0x55;
	buf[1] = 0xAA;

	ptr = buf + STIR_IRDA_HEADER;
	memset(ptr, 0x7f, 16);
	ptr += 16;

	/* BOF */
	*ptr++  = 0x7e;
	*ptr++  = 0x7e;

	/* Address / Control / Information */
	for (i = 0; i < skb->len; i++)
		ptr = stuff_fir(ptr, skb->data[i]);

	/* FCS */
	ptr = stuff_fir(ptr, fcs & 0xff);
	ptr = stuff_fir(ptr, (fcs >> 8) & 0xff);
	ptr = stuff_fir(ptr, (fcs >> 16) & 0xff);
	ptr = stuff_fir(ptr, (fcs >> 24) & 0xff);

	/* EOFs */
	*ptr++ = 0x7e;
	*ptr++ = 0x7e;

	/* Total length, minus the header */
	wraplen = (ptr - buf) - STIR_IRDA_HEADER;
	buf[2] = wraplen & 0xff;
	buf[3] = (wraplen >> 8) & 0xff;

	return wraplen + STIR_IRDA_HEADER;
}

static unsigned wrap_sir_skb(struct sk_buff *skb, __u8 *buf)
{
	__u16 wraplen;

	wraplen = async_wrap_skb(skb, buf + STIR_IRDA_HEADER,
				 STIR_FIFO_SIZE - STIR_IRDA_HEADER);
	buf[0] = 0x55;
	buf[1] = 0xAA;
	buf[2] = wraplen & 0xff;
	buf[3] = (wraplen >> 8) & 0xff;

	return wraplen + STIR_IRDA_HEADER;
}

/*
 * Frame is fully formed in the rx_buff so check crc
 * and pass up to irlap
 * setup for next receive
 */
static void fir_eof(struct stir_cb *stir)
{
	iobuff_t *rx_buff = &stir->rx_buff;
	int len = rx_buff->len - 4;
	__u32 fcs;
	struct sk_buff *nskb;

	if (unlikely(len <= 0)) {
		pr_debug("%s: short frame len %d\n",
			 stir->netdev->name, len);

		++stir->stats.rx_errors;
		++stir->stats.rx_length_errors;
		return;
	}

	fcs = rx_buff->data[len] |
		rx_buff->data[len+1] << 8 |
		rx_buff->data[len+2] << 16 |
		rx_buff->data[len+3] << 24;

	if (unlikely(fcs != ~(crc32_le(~0, rx_buff->data, len)))) {
		pr_debug("%s: crc error\n", stir->netdev->name);
		irda_device_set_media_busy(stir->netdev, TRUE);
		stir->stats.rx_errors++;
		stir->stats.rx_crc_errors++;
		return;
	}

	/* If can't get new buffer, just drop and reuse */
	nskb = dev_alloc_skb(IRDA_SKB_MAX_MTU);
	if (unlikely(!nskb))
		++stir->stats.rx_dropped;
	else {
		struct sk_buff *oskb = rx_buff->skb;
		skb_reserve(nskb, 1);

		/* Set correct length in socket buffer */
		skb_put(oskb, len);

		oskb->mac.raw  = oskb->data;
		oskb->protocol = htons(ETH_P_IRDA);
		oskb->dev = stir->netdev;

		netif_rx(oskb);

		stir->stats.rx_packets++;
		stir->stats.rx_bytes += len;
		rx_buff->skb = nskb;
		rx_buff->head = nskb->data;
	}

	rx_buff->data = rx_buff->head;
	rx_buff->len = 0;
}

/* Unwrap FIR stuffed data and bump it to IrLAP */
static void stir_fir_chars(struct stir_cb *stir,
			    const __u8 *bytes, int len)
{
	iobuff_t *rx_buff = &stir->rx_buff;
	int	i;

	for (i = 0; i < len; i++) {
		__u8	byte = bytes[i];

		switch(rx_buff->state) {
		case OUTSIDE_FRAME:
			/* ignore garbage till start of frame */
			if (unlikely(byte != FIR_EOF))
				continue;
			/* Now receiving frame */
			rx_buff->state = BEGIN_FRAME;
			rx_buff->in_frame = TRUE;

			/* Time to initialize receive buffer */
			rx_buff->data = rx_buff->head;
			rx_buff->len = 0;
			continue;

		case LINK_ESCAPE:
			if (byte == FIR_EOF) {
				pr_debug("%s: got EOF after escape\n",
					 stir->netdev->name);
				goto frame_error;
			}
			rx_buff->state = INSIDE_FRAME;
			byte ^= IRDA_TRANS;
			break;

		case BEGIN_FRAME:
			/* ignore multiple BOF/EOF */
			if (byte == FIR_EOF)
				continue;
			rx_buff->state = INSIDE_FRAME;

			/* fall through */
		case INSIDE_FRAME:
			switch(byte) {
			case FIR_CE:
				rx_buff->state = LINK_ESCAPE;
				continue;
			case FIR_XBOF:
				/* 0x7f is not used in this framing */
				pr_debug("%s: got XBOF without escape\n",
					 stir->netdev->name);
				goto frame_error;
			case FIR_EOF:
				rx_buff->state = OUTSIDE_FRAME;
				rx_buff->in_frame = FALSE;
				fir_eof(stir);
				continue;
			}
			break;
		}

		/* add byte to rx buffer */
		if (unlikely(rx_buff->len >= rx_buff->truesize)) {
			pr_debug("%s: fir frame exceeds %d\n",
				 stir->netdev->name, rx_buff->truesize);
			++stir->stats.rx_over_errors;
			goto error_recovery;
		}

		rx_buff->data[rx_buff->len++] = byte;
		continue;

	frame_error:
		++stir->stats.rx_frame_errors;

	error_recovery:
		++stir->stats.rx_errors;
		irda_device_set_media_busy(stir->netdev, TRUE);
		rx_buff->state = OUTSIDE_FRAME;
		rx_buff->in_frame = FALSE;
	}
}

/* Unwrap SIR stuffed data and bump it up to IrLAP */
static void stir_sir_chars(struct stir_cb *stir,
			    const __u8 *bytes, int len)
{
	int i;

	for (i = 0; i < len; i++)
		async_unwrap_char(stir->netdev, &stir->stats,
				  &stir->rx_buff, bytes[i]);
}

static inline int isfir(u32 speed)
{
	return (speed == 4000000);
}

static inline void unwrap_chars(struct stir_cb *stir,
				const __u8 *bytes, int length)
{
	if (isfir(stir->speed))
		stir_fir_chars(stir, bytes, length);
	else
		stir_sir_chars(stir, bytes, length);
}

/* Mode parameters for each speed */
static const struct {
	unsigned speed;
	__u8 pdclk;
} stir_modes[] = {
        { 2400,    PDCLK_2400 },
        { 9600,    PDCLK_9600 },
        { 19200,   PDCLK_19200 },
        { 38400,   PDCLK_38400 },
        { 57600,   PDCLK_57600 },
        { 115200,  PDCLK_115200 },
        { 4000000, PDCLK_4000000 },
};


/*
 * Setup chip for speed.
 *  Called at startup to initialize the chip
 *  and on speed changes.
 *
 * Note: Write multiple registers doesn't appear to work
 */
static int change_speed(struct stir_cb *stir, unsigned speed)
{
	int i, err;
	__u8 mode;

	pr_debug("%s: change speed %d\n", stir->netdev->name, speed);
	for (i = 0; i < ARRAY_SIZE(stir_modes); ++i) {
		if (speed == stir_modes[i].speed)
			goto found;
	}

	ERROR("%s: invalid speed %d\n", stir->netdev->name, speed);
	return -EINVAL;

 found:
	pr_debug("%s: speed change from %d to %d\n",
		 stir->netdev->name, stir->speed, speed);

	/* Make sure any previous Tx is really finished. This happens
	 * when we answer an incomming request ; the ua:rsp and the
	 * speed change are bundled together, so we need to wait until
	 * the packet we just submitted has been sent. Jean II */
	if (fifo_txwait(stir, 0))
		return -EIO;

	/* Set clock */
	err = write_reg(stir, REG_PDCLK, stir_modes[i].pdclk);
	if (err)
		goto out;

	mode = MODE_NRESET | MODE_FASTRX;
	if (isfir(speed))
		mode |= MODE_FIR | MODE_FFRSTEN;
	else
		mode |= MODE_SIR;

	if (speed == 2400)
		mode |= MODE_2400;

	err = write_reg(stir, REG_MODE, mode);
	if (err)
		goto out;

	/* This resets TEMIC style transceiver if any. */
	err = write_reg(stir, REG_CTRL1,
			CTRL1_SDMODE | (tx_power & 3) << 1);
	if (err)
		goto out;

	err = write_reg(stir, REG_CTRL1, (tx_power & 3) << 1);

 out:
	stir->speed = speed;
	return err;
}

static int stir_reset(struct stir_cb *stir)
{
	int err;

	/* reset state */
	stir->rx_buff.in_frame = FALSE;
	stir->rx_buff.state = OUTSIDE_FRAME;
	stir->speed = -1;

	/* Undocumented magic to tweak the DPLL */
	err = write_reg(stir, REG_DPLL, 0x15);
	if (err)
		goto out;

	/* Reset sensitivity */
	err = write_reg(stir, REG_CTRL2, (rx_sensitivity & 7) << 5);
	if (err)
		goto out;

	err = change_speed(stir, 9600);
 out:
	return err;
}

/*
 * Called from net/core when new frame is available.
 */
static int stir_hard_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct stir_cb *stir = netdev->priv;

	netif_stop_queue(netdev);

	/* the IRDA wrapping routines don't deal with non linear skb */
	SKB_LINEAR_ASSERT(skb);

	if (unlikely(skb->len) == 0) 		/* speed change only */
		stir->tx_len = 0;
	else if (isfir(stir->speed))
		stir->tx_len = wrap_fir_skb(skb, stir->tx_data);
	else
		stir->tx_len = wrap_sir_skb(skb, stir->tx_data);

	stir->stats.tx_packets++;
	stir->stats.tx_bytes += skb->len;

	stir->tx_mtt = irda_get_mtt(skb);
	stir->tx_newspeed = irda_get_next_speed(skb);

	if (!test_and_set_bit(STIR_STATE_TXREADY, &stir->state))
		wake_up(&stir->thr_wait);

	dev_kfree_skb(skb);
	return 0;
}

/*
 * Wait for the transmit FIFO to have space for next data
 */
static int fifo_txwait(struct stir_cb *stir, unsigned space)
{
	int err;
	unsigned count;
	__u8 regs[3];
	unsigned long timeout = jiffies + HZ/10;

	for(;;) {
		/* Read FIFO status and count */
		err = read_reg(stir, REG_FIFOCTL, regs, 3);
		if (unlikely(err != 3)) {
			WARNING("%s: FIFO register read error: %d\n",
				stir->netdev->name, err);
			return err;
		}

		/* is fifo receiving already, or empty */
		if (!(regs[0] & FIFOCTL_DIR)
		    || (regs[0] & FIFOCTL_EMPTY))
			return 0;

		if (signal_pending(current))
			return -EINTR;

		/* shutting down? */
		if (!netif_running(stir->netdev)
		    || !netif_device_present(stir->netdev))
			return -ESHUTDOWN;

		count = (unsigned)(regs[2] & 0x1f) << 8 | regs[1];

		pr_debug("%s: fifo status 0x%x count %u\n",
			 stir->netdev->name, regs[0], count);

		/* only waiting for some space */
		if (space && STIR_FIFO_SIZE - 4 > space + count)
			return 0;

		if (time_after(jiffies, timeout)) {
			WARNING("%s: transmit fifo timeout status=0x%x count=%d\n",
				stir->netdev->name, regs[0], count);
			++stir->stats.tx_errors;
			irda_device_set_media_busy(stir->netdev, TRUE);
			return -ETIMEDOUT;
		}

		/* estimate transfer time for remaining chars */
		wait_ms((count * 8000) / stir->speed);
	}
}


/* Wait for turnaround delay before starting transmit.  */
static void turnaround_delay(long us, const struct timespec *last)
{
	long ticks;
	struct timespec now = CURRENT_TIME;

	if (us <= 0)
		return;

	us -= (now.tv_sec - last->tv_sec) * USEC_PER_SEC;
	us -= (now.tv_nsec - last->tv_nsec) / NSEC_PER_USEC;
	if (us < 10)
		return;

	ticks = us / (1000000 / HZ);
	if (ticks > 0) {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1 + ticks);
	} else
		udelay(us);
}

/*
 * Start receiver by submitting a request to the receive pipe.
 * If nothing is available it will return after rx_interval.
 */
static void receive_start(struct stir_cb *stir)
{
	int i;

	if (test_and_set_bit(STIR_STATE_RECEIVING, &stir->state))
		return;

	if (fifo_txwait(stir, 0))
		return;

	for (i = 0; i < NUM_RX_URBS; i++) {
		struct urb *urb = stir->rx_urbs[i];

		usb_fill_int_urb(urb, stir->usbdev, stir->rx_intpipe,
				 stir->rx_data[i], STIR_FIFO_SIZE,
				 stir_usb_receive, stir, rx_interval);

		if (usb_submit_urb(urb, GFP_KERNEL))
			urb->status = -EINVAL;
	}

	if (i == 0) {
		/* if nothing got queued, then just retry next time */
		if (net_ratelimit())
			WARNING("%s: no receive buffers avaiable\n",
				stir->netdev->name);

		clear_bit(STIR_STATE_RECEIVING, &stir->state);
	}
}

/* Stop all pending receive Urb's */
static void receive_stop(struct stir_cb *stir)
{
	int i;

	for (i = 0; i < NUM_RX_URBS; i++) {
		struct urb *urb = stir->rx_urbs[i];
		usb_unlink_urb(urb);
	}
}

/* Send wrapped data (in tx_data) to device */
static void stir_send(struct stir_cb *stir)
{
	int rc;

	if (test_and_clear_bit(STIR_STATE_RECEIVING, &stir->state)) {
		receive_stop(stir);

		turnaround_delay(stir->tx_mtt, &stir->rx_time);

		if (stir->rx_buff.in_frame)
			++stir->stats.collisions;
	}
	else if (fifo_txwait(stir, stir->tx_len))
		return; /* shutdown or major errors */

	stir->netdev->trans_start = jiffies;

	pr_debug("%s: send %d\n", stir->netdev->name, stir->tx_len);
	rc = usb_bulk_msg(stir->usbdev,
			  stir->tx_bulkpipe,
			  stir->tx_data, stir->tx_len,
			  NULL, MSECS_TO_JIFFIES(TRANSMIT_TIMEOUT));

	if (unlikely(rc)) {
		WARNING("%s: usb bulk message failed %d\n",
			stir->netdev->name, rc);
		stir->stats.tx_errors++;
	}
}

/*
 * Transmit state machine thread
 */
static int stir_transmit_thread(void *arg)
{
	struct stir_cb *stir = arg;
	struct net_device *dev = stir->netdev;
	DECLARE_WAITQUEUE(wait, current);

	daemonize("%s", dev->name);
	allow_signal(SIGTERM);

	while (netif_running(dev)
	       && netif_device_present(dev)
	       && !signal_pending(current))
	{
		/* make swsusp happy with our thread */
		if (current->flags & PF_FREEZE) {
			receive_stop(stir);

			write_reg(stir, REG_CTRL1, CTRL1_TXPWD|CTRL1_RXPWD);

			refrigerator(PF_IOTHREAD);

			stir_reset(stir);
		}

		/* if something to send? */
		if (test_and_clear_bit(STIR_STATE_TXREADY, &stir->state)) {
			unsigned new_speed = stir->tx_newspeed;

			/* Note that we may both send a packet and
			 * change speed in some cases. Jean II */

			if (stir->tx_len != 0)
				stir_send(stir);

			if (stir->speed != new_speed)
				change_speed(stir, new_speed);

			netif_wake_queue(stir->netdev);
			continue;
		}

		if (irda_device_txqueue_empty(dev))
			receive_start(stir);

		set_task_state(current, TASK_INTERRUPTIBLE);
		add_wait_queue(&stir->thr_wait, &wait);
		if (test_bit(STIR_STATE_TXREADY, &stir->state))
			__set_task_state(current, TASK_RUNNING);
		else
			schedule_timeout(HZ/10);
		remove_wait_queue(&stir->thr_wait, &wait);
	}

	complete_and_exit (&stir->thr_exited, 0);
}


/*
 * Receive wrapped data into rx_data buffer.
 * This chip doesn't block until data is available, we just have
 * to read the FIFO perodically (ugh).
 */
static void stir_usb_receive(struct urb *urb, struct pt_regs *regs)
{
	struct stir_cb *stir = urb->context;
	int err;

	if (!netif_running(stir->netdev))
		return;

	switch (urb->status) {
	case 0:
		if(urb->actual_length > 0) {
			pr_debug("%s: receive %d\n",
				 stir->netdev->name, urb->actual_length);
			unwrap_chars(stir, urb->transfer_buffer,
				     urb->actual_length);

			stir->netdev->last_rx = jiffies;
			stir->rx_time = CURRENT_TIME;
		}
		break;

	case -ECONNRESET:	/* killed but pending */
	case -ENOENT:		/* killed but not in use */
	case -ESHUTDOWN:
		/* These are normal errors when URB is cancelled */
		stir->rx_buff.in_frame = FALSE;
		stir->rx_buff.state = OUTSIDE_FRAME;
		return;

	default:
		WARNING("%s: received status %d\n", stir->netdev->name,
			urb->status);
		stir->stats.rx_errors++;
		urb->status = 0;
	}

	/* kernel thread is stopping receiver don't resubmit */
	if (!test_bit(STIR_STATE_RECEIVING, &stir->state))
		return;

	/* resubmit existing urb */
	err = usb_submit_urb(urb, GFP_ATOMIC);

	/* in case of error, the kernel thread will restart us */
	if (err) {
		WARNING("%s: usb receive submit error: %d\n",
			stir->netdev->name, err);
		urb->status = -ENOENT;
		wake_up(&stir->thr_wait);
	}
}


/*
 * Function stir_net_open (dev)
 *
 *    Network device is taken up. Usually this is done by "ifconfig irda0 up"
 */
static int stir_net_open(struct net_device *netdev)
{
	struct stir_cb *stir = netdev->priv;
	int i, err;
	char	hwname[16];

	err = stir_reset(stir);
	if (err)
		goto err_out1;

	/* Note: Max SIR frame possible is 4273 */
	stir->tx_data = kmalloc(STIR_FIFO_SIZE, GFP_KERNEL);
	if (!stir->tx_data) {
		ERROR("%s(), alloc failed for rxbuf!\n", __FUNCTION__);
		goto err_out1;
	}

	/* Initialize for SIR/FIR to copy data directly into skb.  */
	stir->rx_buff.truesize = IRDA_SKB_MAX_MTU;
	stir->rx_buff.skb = dev_alloc_skb(IRDA_SKB_MAX_MTU);
	if (!stir->rx_buff.skb) {
		ERROR("%s(), dev_alloc_skb() failed for rxbuf!\n",
		      __FUNCTION__);
		goto err_out2;
	}
	skb_reserve(stir->rx_buff.skb, 1);
	stir->rx_buff.head = stir->rx_buff.skb->data;
	stir->rx_time = CURRENT_TIME;

	/* Allocate N receive buffer's and urbs */
	for (i = 0; i < NUM_RX_URBS; i++) {
		stir->rx_urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!stir->rx_urbs[i]){
			ERROR("%s(), usb_alloc_urb failed\n", __FUNCTION__);
			goto err_out3;
		}

		stir->rx_data[i] = kmalloc(STIR_FIFO_SIZE, GFP_KERNEL);
		if (!stir->rx_data) {
			usb_free_urb(stir->rx_urbs[i]);
			ERROR("%s(), alloc failed for rxbuf!\n", __FUNCTION__);
			goto err_out3;
		}
	}

	netif_start_queue(netdev);

	/*
	 * Now that everything should be initialized properly,
	 * Open new IrLAP layer instance to take care of us...
	 * Note : will send immediately a speed change...
	 */
	sprintf(hwname, "usb#%d", stir->usbdev->devnum);
	err = -ENOMEM;
	stir->irlap = irlap_open(netdev, &stir->qos, hwname);
	if (!stir->irlap) {
		ERROR("%s(): irlap_open failed\n", __FUNCTION__);
		goto err_out3;
	}

	/** Start kernel thread for transmit.  */
	stir->thr_pid = kernel_thread(stir_transmit_thread, stir,
				      CLONE_FS|CLONE_FILES);
	if (stir->thr_pid < 0) {
		err = stir->thr_pid;
		WARNING("%s: unable to start kernel thread\n",
			stir->netdev->name);
		goto err_out4;
	}

	return 0;

 err_out4:
	irlap_close(stir->irlap);
 err_out3:
	while(--i >= 0) {
		usb_free_urb(stir->rx_urbs[i]);
		kfree(stir->rx_data[i]);
	}
	kfree_skb(stir->rx_buff.skb);
 err_out2:
	kfree(stir->tx_data);
 err_out1:
	return err;
}

/*
 * Function stir_net_close (stir)
 *
 *    Network device is taken down. Usually this is done by
 *    "ifconfig irda0 down"
 */
static int stir_net_close(struct net_device *netdev)
{
	struct stir_cb *stir = netdev->priv;
	int i;

	/* Stop transmit processing */
	netif_stop_queue(netdev);

	/* Kill transmit thread */
	kill_proc(stir->thr_pid, SIGTERM, 1);
	wait_for_completion(&stir->thr_exited);
	kfree(stir->tx_data);

	clear_bit(STIR_STATE_RECEIVING, &stir->state);
	receive_stop(stir);

	for (i = 0; i < NUM_RX_URBS; i++) {
		usb_free_urb(stir->rx_urbs[i]);
		kfree(stir->rx_data[i]);
	}
	kfree_skb(stir->rx_buff.skb);

	/* Stop and remove instance of IrLAP */
	if (stir->irlap)
		irlap_close(stir->irlap);

	stir->irlap = NULL;

	return 0;
}

/*
 * IOCTLs : Extra out-of-band network commands...
 */
static int stir_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct stir_cb *stir = dev->priv;
	int ret = 0;

	switch (cmd) {
	case SIOCSBANDWIDTH: /* Set bandwidth */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		/* Check if the device is still there */
		if (netif_device_present(stir->netdev))
			ret = change_speed(stir, irq->ifr_baudrate);
		break;

	case SIOCSMEDIABUSY: /* Set media busy */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		/* Check if the IrDA stack is still there */
		if (netif_running(stir->netdev))
			irda_device_set_media_busy(stir->netdev, TRUE);
		break;

	case SIOCGRECEIVING:
		/* Only approximately true */
		irq->ifr_receiving = test_bit(STIR_STATE_RECEIVING, &stir->state);
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

/*
 * Get device stats (for /proc/net/dev and ifconfig)
 */
static struct net_device_stats *stir_net_get_stats(struct net_device *dev)
{
	struct stir_cb *stir = dev->priv;
	return &stir->stats;
}

/*
 *    Parse the various endpoints and find the one we need.
 *
 * The endpoint are the pipes used to communicate with the USB device.
 * The spec defines 2 endpoints of type bulk transfer, one in, and one out.
 * These are used to pass frames back and forth with the dongle.
 */
static int stir_setup_usb(struct stir_cb *stir, struct usb_interface *intf)
{
	struct usb_device *usbdev = interface_to_usbdev(intf);
	const struct usb_host_interface *interface
		= &intf->altsetting[intf->act_altsetting];
	const struct usb_endpoint_descriptor *ep_in = NULL;
	const struct usb_endpoint_descriptor *ep_out = NULL;
	int i;

	if (interface->desc.bNumEndpoints != 2) {
		WARNING("%s: expected two endpoints\n", __FUNCTION__);
		return -ENODEV;
	}

	for(i = 0; i < interface->desc.bNumEndpoints; i++) {
		const struct usb_endpoint_descriptor *ep
			= &interface->endpoint[i].desc;

		if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		    == USB_ENDPOINT_XFER_BULK) {
			/* We need to find an IN and an OUT */
			if ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
				ep_in = ep;
			else
				ep_out = ep;
		} else
			WARNING("%s: unknown endpoint type 0x%x\n",
				__FUNCTION__, ep->bmAttributes);
	}

	if (!ep_in || !ep_out)
		return -EIO;

	stir->tx_bulkpipe = usb_sndbulkpipe(usbdev,
			    ep_out->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	stir->rx_intpipe = usb_rcvintpipe(usbdev,
			    ep_in->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	return 0;
}

/*
 * This routine is called by the USB subsystem for each new device
 * in the system. We need to check if the device is ours, and in
 * this case start handling it.
 * Note : it might be worth protecting this function by a global
 * spinlock... Or not, because maybe USB already deal with that...
 */
static int stir_probe(struct usb_interface *intf,
		      const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct stir_cb *stir = NULL;
	struct net_device *net;
	int ret = -ENOMEM;

	/* Allocate network device container. */
	net = alloc_irdadev(sizeof(*stir));
	if(!net)
		goto err_out1;

	SET_MODULE_OWNER(net);
	SET_NETDEV_DEV(net, &intf->dev);
	stir = net->priv;
	stir->netdev = net;
	stir->usbdev = dev;

	ret = stir_setup_usb(stir, intf);
	if (ret != 0) {
		ERROR("%s(), Bogus endpoints...\n", __FUNCTION__);
		goto err_out2;
	}

	printk(KERN_INFO "SigmaTel STIr4200 IRDA/USB found at address %d, "
		"Vendor: %x, Product: %x\n",
	       dev->devnum, dev->descriptor.idVendor,
	       dev->descriptor.idProduct);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&stir->qos);

	/* That's the Rx capability. */
	stir->qos.baud_rate.bits       &= IR_2400 | IR_9600 | IR_19200 |
					 IR_38400 | IR_57600 | IR_115200 |
					 (IR_4000000 << 8);
	stir->qos.min_turn_time.bits   &= qos_mtt_bits;
	irda_qos_bits_to_value(&stir->qos);

	init_completion (&stir->thr_exited);
	init_waitqueue_head (&stir->thr_wait);

	/* Override the network functions we need to use */
	net->hard_start_xmit = stir_hard_xmit;
	net->open            = stir_net_open;
	net->stop            = stir_net_close;
	net->get_stats	     = stir_net_get_stats;
	net->do_ioctl        = stir_net_ioctl;

	ret = stir_reset(stir);
	if (ret)
		goto err_out2;

	ret = register_netdev(net);
	if (ret != 0)
		goto err_out2;

	MESSAGE("IrDA: Registered SigmaTel device %s\n", net->name);

	usb_set_intfdata(intf, stir);

	return 0;

err_out2:
	free_netdev(net);
err_out1:
	return ret;
}

/*
 * The current device is removed, the USB layer tell us to shut it down...
 */
static void stir_disconnect(struct usb_interface *intf)
{
	struct stir_cb *stir = usb_get_intfdata(intf);
	struct net_device *net;

	usb_set_intfdata(intf, NULL);
	if (!stir)
		return;

	/* Stop transmitter */
	net = stir->netdev;
	netif_device_detach(net);

	/* Remove netdevice */
	unregister_netdev(net);

	/* No longer attached to USB bus */
	stir->usbdev = NULL;

	free_netdev(net);
}


/* Power management suspend, so power off the transmitter/receiver */
static int stir_suspend(struct usb_interface *intf, u32 state)
{
	struct stir_cb *stir = usb_get_intfdata(intf);

	netif_device_detach(stir->netdev);
	return 0;
}

/* Coming out of suspend, so reset hardware */
static int stir_resume(struct usb_interface *intf)
{
	struct stir_cb *stir = usb_get_intfdata(intf);

	netif_device_attach(stir->netdev);

	/* receiver restarted when send thread wakes up */
	return 0;
}

/*
 * USB device callbacks
 */
static struct usb_driver irda_driver = {
	.owner		= THIS_MODULE,
	.name		= "stir4200",
	.probe		= stir_probe,
	.disconnect	= stir_disconnect,
	.id_table	= dongles,
	.suspend	= stir_suspend,
	.resume		= stir_resume,
};

/*
 * Module insertion
 */
static int __init stir_init(void)
{
	if (usb_register(&irda_driver) < 0)
		return -1;

	MESSAGE("SigmaTel support registered\n");
	return 0;
}
module_init(stir_init);

/*
 * Module removal
 */
static void __exit stir_cleanup(void)
{
	/* Deregister the driver and remove all pending instances */
	usb_deregister(&irda_driver);
}
module_exit(stir_cleanup);
