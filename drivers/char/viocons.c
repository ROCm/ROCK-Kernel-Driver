/* -*- linux-c -*-
 *
 *  drivers/char/viocons.c
 *
 *  iSeries Virtual Terminal
 *
 *  Authors: Dave Boutcher <boutcher@us.ibm.com>
 *           Ryan Arnold <ryanarn@us.ibm.com>
 *           Colin Devilbiss <devilbis@us.ibm.com>
 *
 * (C) Copyright 2000, 2001, 2002, 2003 IBM Corporation
 *
 * This program is free software;  you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) anyu later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <asm/ioctls.h>
#include <linux/kd.h>
#include <linux/tty.h>

#include <asm/iSeries/vio.h>

#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvCallEvent.h>
#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/HvCall.h>

/* Check that the tty_driver_data actually points to our stuff
 */
#define VIOTTY_PARANOIA_CHECK 1
#define VIOTTY_MAGIC (0x0DCB)

static DECLARE_WAIT_QUEUE_HEAD(viocons_wait_queue);

#define VTTY_PORTS 10
#define VIOTTY_SERIAL_START 65

static u64 sndMsgSeq[VTTY_PORTS];
static u64 sndMsgAck[VTTY_PORTS];

static spinlock_t consolelock = SPIN_LOCK_UNLOCKED;

/*
 * The structure of the events that flow between us and OS/400.  You can't
 * mess with this unless the OS/400 side changes too
 */
struct viocharlpevent {
	struct HvLpEvent event;
	u32 reserved;
	u16 version;
	u16 subtype_result_code;
	u8 virtual_device;
	u8 len;
	u8 data[VIOCHAR_MAX_DATA];
};

#define VIOCHAR_WINDOW		10
#define VIOCHAR_HIGHWATERMARK	3

enum viocharsubtype {
	viocharopen = 0x0001,
	viocharclose = 0x0002,
	viochardata = 0x0003,
	viocharack = 0x0004,
	viocharconfig = 0x0005
};

enum viochar_rc {
	viochar_rc_ebusy = 1
};

/* When we get writes faster than we can send it to the partition,
 * buffer the data here.  There is one set of buffers for each virtual
 * port.
 * Note that used is a bit map of used buffers.
 * It had better have enough bits to hold NUM_BUF
 * the bitops assume it is a multiple of unsigned long
 */
#define NUM_BUF (8)
#define OVERFLOW_SIZE VIOCHAR_MAX_DATA

static struct overflow_buffer {
	unsigned long used;
	u8 *buffer[NUM_BUF];
	int bufferBytes[NUM_BUF];
	int curbuf;
	int bufferOverflow;
	int overflowMessage;
} overflow[VTTY_PORTS];

static struct tty_driver viotty_driver;
static struct tty_driver viottyS_driver;

static struct termios *viotty_termios[VTTY_PORTS];
static struct termios *viottyS_termios[VTTY_PORTS];
static struct termios *viotty_termios_locked[VTTY_PORTS];
static struct termios *viottyS_termios_locked[VTTY_PORTS];

void hvlog(char *fmt, ...)
{
	int i;
	static char buf[256];
	va_list args;

	va_start(args, fmt);
	i = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
	va_end(args);
	buf[i++] = '\r';
	HvCall_writeLogBuffer(buf, i);

}

/* Our port information.  We store a pointer to one entry in the
 * tty_driver_data
 */
static struct port_info_tag {
	int magic;
	struct tty_struct *tty;
	HvLpIndex lp;
	u8 vcons;
	u8 port;
} port_info[VTTY_PORTS];

/*
 * Make sure we're pointing to a valid port_info structure.  Shamelessly
 * plagerized from serial.c
 */
static inline int viotty_paranoia_check(struct port_info_tag *pi,
					char *name, const char *routine)
{
#ifdef VIOTTY_PARANOIA_CHECK
	static const char *badmagic = KERN_WARNING
		"Warning: bad magic number for port_info struct (%s) in %s\n";
	static const char *badinfo = KERN_WARNING
		"Warning: null port_info for (%s) in %s\n";

	if (!pi) {
		printk(badinfo, name, routine);
		return 1;
	}
	if (pi->magic != VIOTTY_MAGIC) {
		printk(badmagic, name, routine);
		return 1;
	}
#endif
	return 0;
}

/*
 * Add data to our pending-send buffers.  
 *
 * NOTE: Don't use printk in here because it gets nastily recursive.
 * hvlog can be used to log to the hypervisor buffer
 */
static int buffer_add(u8 port, const char *buf, size_t len, int userFlag)
{
	size_t bleft = len;
	size_t curlen;
	char *cbuf = (char *) buf;
	int nextbuf;
	unsigned long flags;
	struct overflow_buffer *pov = &overflow[port];

	while (bleft > 0) {
		/*
		 * Addition 05/01/2003 by Ryan Arnold :  If we are going
		 * to have to copy_from_user() and the current buffer is
		 * already partially filled then we want to increment to
		 * the next buffer and try to see if that one is
		 * completely empty instead.  This is OK since, if
		 * pov->curbuf is being used when we hit this it probably
		 * means that it was filled last iteration or last time
		 * this function was called.
		 */
		if (userFlag && (test_bit(pov->curbuf, &pov->used) != 0)) {
			nextbuf = (pov->curbuf + 1) % NUM_BUF;
			pov->curbuf = nextbuf;
			/*
			 * In the following case should the next buffer be
			 * used then we don't want to add to it and we'll
			 * kick out an error message to the hvlog.
			 */
			if (test_bit(pov->curbuf, &pov->used) != 0) {
				hvlog("No overflow buffers available for copy_from_user().\n");
				pov->bufferOverflow++;
				pov->overflowMessage = 1;
				return len - bleft;
			}
		}

		/*
		 * If there is no space left in the current buffer, we have
		 * filled everything up, so return.  If we filled the previous
		 * buffer we would already have moved to the next one.
		 * If userFlag, then we'll never hit this code branch
		 * unless the buffer is empty, at which point we'll step
		 * over it.
		 */
		if (pov->bufferBytes[pov->curbuf] == OVERFLOW_SIZE) {
			hvlog("No overflow buffer available for memcpy().\n",
					pov->curbuf);
			pov->bufferOverflow++;
			pov->overflowMessage = 1;
			return len - bleft;
		}

		/*
		 * See if this buffer has been allocated.  If not, allocate it
		 */
		if (pov->buffer[pov->curbuf] == NULL) {
			pov->buffer[pov->curbuf] =
			    kmalloc(OVERFLOW_SIZE, GFP_ATOMIC);
			if (pov->buffer[pov->curbuf] == NULL) {
				hvlog("kmalloc failed allocating space for buffer %d.\n",
						pov->curbuf);
				return len - bleft;
			}
		}

		/*
		 * Addition 05/01/2003 by Ryan Arnold :  Copy the data
		 * into the buffer.  Since we don't want to hold a
		 * spinlock during a copy_from_user() operation we won't.
		 * This also means that we can't copy into partially used
		 * buffers because we may be interrupted between the
		 * copy_from_user() invocation and the grab of the
		 * spinlock by a call to send_buffers() which would throw
		 * the bufferBytes field for pov->curbuf out of whack
		 * resulting in the wrong data being output.  If we
		 * aren'te executing copy_from_user() we can hold a
		 * spinlock and execute memcpy() and protect the
		 * bufferBytes[] updated, and used operation all at
		 * once without fear of interrupt, meaning we can copy
		 * into partially used buffers.
		 */
		if (userFlag) {
			curlen = OVERFLOW_SIZE - pov->bufferBytes[pov->curbuf];
			if (curlen != OVERFLOW_SIZE) {
				/*
				 * This should never happen but if it does we
				 * want to know about it.
				 */
				hvlog("During userFlag, curlen != OVERFLOW_SIZE.\n");
			}
			copy_from_user(pov->buffer[pov->curbuf] +
				       pov->bufferBytes[pov->curbuf], cbuf,
				       curlen);
			spin_lock_irqsave(&consolelock, flags);
		} else {
			spin_lock_irqsave(&consolelock, flags);
			/*
			 * Figure out how much we can copy into this buffer.
			 */
			if (bleft <
			    (OVERFLOW_SIZE - pov->bufferBytes[pov->curbuf]))
				curlen = bleft;
			else
				curlen = OVERFLOW_SIZE -
				    pov->bufferBytes[pov->curbuf];

			memcpy(pov->buffer[pov->curbuf] +
			       pov->bufferBytes[pov->curbuf], cbuf,
			       curlen);
		}

		pov->bufferBytes[pov->curbuf] += curlen;
		cbuf += curlen;
		bleft -= curlen;

		/*
		 * Turn on the "used" bit for this buffer.  If it's
		 * already on, that's fine.  It won't be on for userFlag
		 * and needs to be set because an interrupt by
		 * send_buffers() could have turned it off between our
		 * copy_from_user() and spin_lock_irqsave() calls.
		 */
		set_bit(pov->curbuf, &pov->used);

		/*
		 * Now see if we've filled this buffer.  If not then
		 * we'll try to use it again later.  If we've filled it
		 * up then we'll advance the curbuf to the next in the
		 * circular queue.
		 */
		if (pov->bufferBytes[pov->curbuf] == OVERFLOW_SIZE) {
			nextbuf = (pov->curbuf + 1) % NUM_BUF;
			/*
			 * Move to the next buffer if it hasn't been used yet
			 */
			if (test_bit(nextbuf, &pov->used) == 0)
				pov->curbuf = nextbuf;
		}
		spin_unlock_irqrestore(&consolelock, flags);
	}
	return len;
}

/*
 * Initialize the common fields in a charLpEvent
 */
static void initDataEvent(struct viocharlpevent *viochar, HvLpIndex lp)
{
	memset(viochar, 0, sizeof(struct viocharlpevent));

	viochar->event.xFlags.xValid = 1;
	viochar->event.xFlags.xFunction = HvLpEvent_Function_Int;
	viochar->event.xFlags.xAckInd = HvLpEvent_AckInd_NoAck;
	viochar->event.xFlags.xAckType = HvLpEvent_AckType_DeferredAck;
	viochar->event.xType = HvLpEvent_Type_VirtualIo;
	viochar->event.xSubtype = viomajorsubtype_chario | viochardata;
	viochar->event.xSourceLp = HvLpConfig_getLpIndex();
	viochar->event.xTargetLp = lp;
	viochar->event.xSizeMinus1 = sizeof(struct viocharlpevent);
	viochar->event.xSourceInstanceId = viopath_sourceinst(lp);
	viochar->event.xTargetInstanceId = viopath_targetinst(lp);
}

/*
 * Send pending data
 *
 * NOTE: Don't use printk in here because it gets nastily recursive.
 * hvlog can be used to log to the hypervisor buffer
 */
static void send_buffers(u8 port, HvLpIndex lp)
{
	HvLpEvent_Rc hvrc;
	int nextbuf;
	struct viocharlpevent *viochar;
	unsigned long flags;
	struct overflow_buffer *pov = &overflow[port];

	spin_lock_irqsave(&consolelock, flags);

	viochar = (struct viocharlpevent *)
	    vio_get_event_buffer(viomajorsubtype_chario);

	/*
	 * Make sure we got a buffer
	 */
	if (viochar == NULL) {
		hvlog("Yikes...can't get viochar buffer\n");
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}

	if (pov->used == 0) {
		hvlog("in sendbuffers, but no buffers used\n");
		vio_free_event_buffer(viomajorsubtype_chario, viochar);
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}

	/*
	 * curbuf points to the buffer we're filling.  We want to
	 * start sending AFTER this one.  
	 */
	nextbuf = (pov->curbuf + 1) % NUM_BUF;

	/*
	 * Loop until we find a buffer with the used bit on
	 */
	while (test_bit(nextbuf, &pov->used) == 0)
		nextbuf = (nextbuf + 1) % NUM_BUF;

	initDataEvent(viochar, lp);

	/*
	 * While we have buffers with data, and our send window
	 * is open, send them
	 */
	while ((test_bit(nextbuf, &pov->used)) &&
	       ((sndMsgSeq[port] - sndMsgAck[port]) < VIOCHAR_WINDOW)) {
		viochar->len = pov->bufferBytes[nextbuf];
		viochar->event.xCorrelationToken = sndMsgSeq[port]++;
		viochar->event.xSizeMinus1 =
			offsetof(struct viocharlpevent, data) + viochar->len;

		memcpy(viochar->data, pov->buffer[nextbuf], viochar->len);

		hvrc = HvCallEvent_signalLpEvent(&viochar->event);
		if (hvrc) {
			/*
			 * MUST unlock the spinlock before doing a printk
			 */
			vio_free_event_buffer(viomajorsubtype_chario, viochar);
			spin_unlock_irqrestore(&consolelock, flags);

			printk(KERN_WARNING_VIO
			       "console error sending event! return code %d\n",
			       (int)hvrc);
			return;
		}

		/*
		 * clear the used bit, zero the number of bytes in
		 * this buffer, and move to the next buffer
		 */
		clear_bit(nextbuf, &pov->used);
		pov->bufferBytes[nextbuf] = 0;
		nextbuf = (nextbuf + 1) % NUM_BUF;
	}


	/*
	 * If we have emptied all the buffers, start at 0 again.
	 * this will re-use any allocated buffers
	 */
	if (pov->used == 0) {
		pov->curbuf = 0;

		if (pov->overflowMessage)
			pov->overflowMessage = 0;

		if (port_info[port].tty) {
			if ((port_info[port].tty->flags &
						(1 << TTY_DO_WRITE_WAKEUP)) &&
			    (port_info[port].tty->ldisc.write_wakeup))
				(port_info[port].tty->ldisc.write_wakeup)(
						port_info[port].tty);
			wake_up_interruptible(&port_info[port].tty->write_wait);
		}
	}

	vio_free_event_buffer(viomajorsubtype_chario, viochar);
	spin_unlock_irqrestore(&consolelock, flags);
}

/*
 * Our internal writer.  Gets called both from the console device and
 * the tty device.  the tty pointer will be NULL if called from the console.
 *
 * NOTE: Don't use printk in here because it gets nastily recursive.  hvlog
 * can be used to log to the hypervisor buffer
 */
static int internal_write(struct tty_struct *tty, const char *buf,
			  size_t len, int userFlag)
{
	HvLpEvent_Rc hvrc;
	size_t bleft = len;
	size_t curlen;
	const char *curbuf = buf;
	struct viocharlpevent *viochar;
	unsigned long flags;
	struct port_info_tag *pi = NULL;
	HvLpIndex lp;
	u8 port;

	/*
	 * Changed 05/01/2003 by Ryan Arnold :  We spinlock so that
	 * we can guarentee that the tty doesn't get changed on us
	 * between when we fetch the driver data and when we paranoia
	 * check.  It is unlikely that this will ever happen but
	 * we'll do it anyway.
	 */
	spin_lock_irqsave(&consolelock, flags);
	if (tty) {
		pi = (struct port_info_tag *) tty->driver_data;
		if (!pi || viotty_paranoia_check(pi, tty->name,
					"viotty_internal_write")) {
			spin_unlock_irqrestore(&consolelock, flags);
			return -ENODEV;
		}
		lp = pi->lp;
		port = pi->port;
	} else {
		/*
		 * If this is the console device, use the lp from the
		 * first port entry
		 */
		port = 0;
		lp = port_info[0].lp;
	}
	spin_unlock_irqrestore(&consolelock, flags);

	/* Always put console output in the hypervisor console log */
	if (port == 0)
		HvCall_writeLogBuffer(buf, len);

	/*
	 * If the path to this LP is closed, don't bother doing anything
	 * more. just dump the data on the floor
	 */
	if (!viopath_isactive(lp))
		return len;

	/* If there is already data queued for this port, send it. */
	if (overflow[port].used)
		send_buffers(port, lp);

	viochar = (struct viocharlpevent *)
	    vio_get_event_buffer(viomajorsubtype_chario);
	/* Make sure we got a buffer */
	if (viochar == NULL) {
		hvlog("Yikes...can't get viochar buffer\n");
		return -1;
	}

	initDataEvent(viochar, lp);

	while ((bleft > 0) && (overflow[port].used == 0) &&
	       ((sndMsgSeq[port] - sndMsgAck[port]) < VIOCHAR_WINDOW)) {
		if (bleft > VIOCHAR_MAX_DATA)
			curlen = VIOCHAR_MAX_DATA;
		else
			curlen = bleft;

		viochar->len = curlen;
		viochar->event.xCorrelationToken = sndMsgSeq[port]++;

		if (userFlag)
			copy_from_user(viochar->data, curbuf, curlen);
		else
			memcpy(viochar->data, curbuf, curlen);

		viochar->event.xSizeMinus1 =
		    offsetof(struct viocharlpevent, data) + curlen;

		hvrc = HvCallEvent_signalLpEvent(&viochar->event);
		if (hvrc) {
			vio_free_event_buffer(viomajorsubtype_chario, viochar);

			hvlog("viocons: error sending event! %d\n", (int)hvrc);
			return len - bleft;
		}

		curbuf += curlen;
		bleft -= curlen;
	}

	/* If we didn't send it all, buffer as much of it as we can. */
	if (bleft > 0)
		bleft -= buffer_add(port, curbuf, bleft, userFlag);
	vio_free_event_buffer(viomajorsubtype_chario, viochar);

	return len - bleft;
}


/*
 * console device write
 */
static void viocons_write(struct console *co, const char *s, unsigned count)
{
	int index;
	int foundcr;
	int slicebegin;
	int sliceend;

	static const char nl = '\n';
	static const char cr = '\r';

	/*
	 * This parser will ensure that all single instances of
	 * either \n or \r are matched into carriage return/line feed
	 * combinations.  It also allows for instances where there
	 * already exist \n\r combinations as well as the reverse,
	 * \r\n combinations.
	 */
	foundcr = 0;
	slicebegin = 0;
	sliceend = 0;

	for (index = 0; index < count; index++) {
		if (!foundcr && (s[index] == nl)) {
			if ((sliceend > slicebegin) && (sliceend < count)) {
				internal_write(NULL, &s[slicebegin],
					       sliceend - slicebegin, 0);
				slicebegin = sliceend;
			}
			internal_write(NULL, &cr, 1, 0);
		}
		if (foundcr && (s[index] != nl) && (index >= 2) &&
				(s[index - 2] != nl)) {
			internal_write(NULL, &s[slicebegin],
					sliceend - slicebegin, 0);
			slicebegin = sliceend;
			internal_write(NULL, &nl, 1, 0);
		}
		sliceend++;
		foundcr = (s[index] == cr);
	}

	internal_write(NULL, &s[slicebegin], sliceend - slicebegin, 0);

	if (count > 1) {
		if (foundcr && (s[count - 1] != nl))
			internal_write(NULL, &nl, 1, 0);
		else if ((s[count - 1] == nl) && (s[count - 2] != cr))
			internal_write(NULL, &cr, 1, 0);
	}
}

/*
 * Work out a the device associate with this console
 */
static struct tty_driver *viocons_device(struct console *c, int *index)
{
	*index = c->index;
	return &viotty_driver;
}

/*
 * Do console device setup
 */
static int __init viocons_setup(struct console *co, char *options)
{
	return 0;
}

/*
 * console device I/O methods
 */
static struct console viocons = {
	.name = "ttyS",
	.write = viocons_write,
	.device = viocons_device,
	.setup = viocons_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

/*
 * TTY Open method
 */
static int viotty_open(struct tty_struct *tty, struct file *filp)
{
	int port;
	unsigned long flags;

	port = tty->index;

	if (port >= VIOTTY_SERIAL_START)
		port -= VIOTTY_SERIAL_START;

	if ((port < 0) || (port >= VTTY_PORTS))
		return -ENODEV;

	spin_lock_irqsave(&consolelock, flags);

	/* If some other TTY is already connected here, reject the open */
	if ((port_info[port].tty) && (port_info[port].tty != tty)) {
		spin_unlock_irqrestore(&consolelock, flags);
		printk(KERN_WARNING_VIO
		       "console attempt to open device twice from different ttys\n");
		return -EBUSY;
	}
	tty->driver_data = &port_info[port];
	port_info[port].tty = tty;
	spin_unlock_irqrestore(&consolelock, flags);

	return 0;
}

/*
 * TTY Close method
 */
static void viotty_close(struct tty_struct *tty, struct file *filp)
{
	unsigned long flags;
	struct port_info_tag *pi = NULL;

	spin_lock_irqsave(&consolelock, flags);
	pi = (struct port_info_tag *) tty->driver_data;

	if (!pi || viotty_paranoia_check(pi, tty->name, "viotty_close")) {
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}

/*	if (atomic_read(&tty->count) == 1) { */
	if (tty->count == 1)
		pi->tty = NULL;

	spin_unlock_irqrestore(&consolelock, flags);

}

/*
 * TTY Write method
 */
static int viotty_write(struct tty_struct *tty, int from_user,
			const unsigned char *buf, int count)
{
	return internal_write(tty, buf, count, from_user);
}

/*
 * TTY put_char method
 */
static void viotty_put_char(struct tty_struct *tty, unsigned char ch)
{
	internal_write(tty, &ch, 1, 0);
}

/*
 * TTY flush_chars method
 */
static void viotty_flush_chars(struct tty_struct *tty)
{
}

/*
 * TTY write_room method
 */
static int viotty_write_room(struct tty_struct *tty)
{
	int i;
	int room = 0;
	struct port_info_tag *pi = NULL;
	unsigned long flags;

	spin_lock_irqsave(&consolelock, flags);
	pi = (struct port_info_tag *) tty->driver_data;
	if (!pi || viotty_paranoia_check(pi, tty->name, "viotty_sendbuffers")) {
		spin_unlock_irqrestore(&consolelock, flags);
		return 0;
	}

	/*
	 * If no buffers are used, return the max size.
	 */
	if (overflow[pi->port].used == 0) {
		spin_unlock_irqrestore(&consolelock, flags);
		return VIOCHAR_MAX_DATA * NUM_BUF;
	}

	/*
	 * We retain the spinlock because we want to get an accurate
	 * count and it can change on us between each operation if we
	 * don't hold the spinlock.
	 */
	for (i = 0; ((i < NUM_BUF) && (room < VIOCHAR_MAX_DATA)); i++)
		room += (OVERFLOW_SIZE - overflow[pi->port].bufferBytes[i]);
	spin_unlock_irqrestore(&consolelock, flags);

	if (room > VIOCHAR_MAX_DATA)
		room = VIOCHAR_MAX_DATA;
	return room;
}

/*
 * TTY chars_in_buffer_room method
 */
static int viotty_chars_in_buffer(struct tty_struct *tty)
{
	return 0;
}

static void viotty_flush_buffer(struct tty_struct *tty)
{
}

static int viotty_ioctl(struct tty_struct *tty, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	/*
	 * the ioctls below read/set the flags usually shown in the leds
	 * don't use them - they will go away without warning
	 */
	case KDGETLED:
	case KDGKBLED:
		return put_user(0, (char *) arg);

	case KDSKBLED:
		return 0;
	}

	return n_tty_ioctl(tty, file, cmd, arg);
}

static void viotty_throttle(struct tty_struct *tty)
{
}

static void viotty_unthrottle(struct tty_struct *tty)
{
}

static void viotty_set_termios(struct tty_struct *tty,
			       struct termios *old_termios)
{
}

static void viotty_stop(struct tty_struct *tty)
{
}

static void viotty_start(struct tty_struct *tty)
{
}

static void viotty_hangup(struct tty_struct *tty)
{
}

static void viotty_break(struct tty_struct *tty, int break_state)
{
}

static void viotty_send_xchar(struct tty_struct *tty, char ch)
{
}

static void viotty_wait_until_sent(struct tty_struct *tty, int timeout)
{
}

/*
 * Handle an open charLpEvent.  Could be either interrupt or ack
 */
static void vioHandleOpenEvent(struct HvLpEvent *event)
{
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	u8 port = cevent->virtual_device;
	int reject = 0;

	/*
	 * Change 05/01/2003 by Ryan Arnold:  The following two local
	 * variables are being removed because they aren't actually
	 * used once they are ste.  The porper action is to set the
	 * parameter event's xRc and mSubTypeRc fields.  This fixes
	 * the bug where a successfull open is returned to the client
	 * even when the console is in use by another partition and
	 * the open event actuall failed.  Since the event ptr was
	 * being re0used we were always sending back the same
	 * event->xRc and cevent->mSubTypeRc that we were getting
	 * from the original event.
	 *
	 * u16 eventSubtypeRc;
	 * u8 eventRc;
	 */

	if (event->xFlags.xFunction == HvLpEvent_Function_Ack) {
		if (port >= VTTY_PORTS)
			return;

		spin_lock_irqsave(&consolelock, flags);
		/* Got the lock, don't cause console output */

		if (event->xRc == HvLpEvent_Rc_Good) {
			sndMsgSeq[port] = sndMsgAck[port] = 0;
			/*
			 * Changed 05/01/2003 by Ryan Arnold: this linve was
			 * moved UP into this if block because, in its
			 * previous position it prevent the primary partition
			 * from EVER getting the console because of the order
			 * of operations was such that the hosting partion
			 * aws always accepted as the target LP regardless of
			 * whether the event->xRc was good or not.  This
			 * fix allows connections from the primary partition
			 * but once one is connected from the primary
			 * partition nothing short of a reboot of linux will
			 * allow access from the hosting partition again
			 * without a required iSeries fix.
			 */
			port_info[port].lp = event->xTargetLp;
		}
		spin_unlock_irqrestore(&consolelock, flags);

		if (event->xRc != HvLpEvent_Rc_Good)
			printk(KERN_WARNING_VIO
			       "viocons: event->xRc != HvLpEvent_Rc_Good, event->xRc == (%d).\n",
			       event->xRc);

		if (event->xCorrelationToken != 0) {
			unsigned long semptr = event->xCorrelationToken;
			up((struct semaphore *) semptr);
		} else
			printk(KERN_WARNING_VIO
			       "console: wierd...got open ack without semaphore\n");
		return;
	}

	/*
	 * This had better require an ack, otherwise complain
	 */
	if (event->xFlags.xAckInd != HvLpEvent_AckInd_DoAck) {
		printk(KERN_WARNING_VIO
		       "console: viocharopen without ack bit!\n");
		return;
	}

	spin_lock_irqsave(&consolelock, flags);
	/* Got the lock, don't cause console output */

	/* Make sure this is a good virtual tty */
	if (port >= VTTY_PORTS) {
		/*
		 * Change 05/01/2003 by Ryan Arnold: This is where
		 * the local variable assignments were changed over
		 * to re-assigning values to the original event data
		 * members since we reuse the event.  This was also
		 * change in the two other else if & else blocks
		 * below.
		 */
		event->xRc = HvLpEvent_Rc_SubtypeError;
		cevent->subtype_result_code = viorc_openRejected;
		/*
		 * Flag state here since we can't printk while holding
		 * a spinlock.
		 */
		reject = 1;
	} else if ((port_info[port].lp != HvLpIndexInvalid) &&
		   (port_info[port].lp != event->xSourceLp)) {
		/*
		 * If this is tty is already connected to a different
		 * partition, fail.
		 */
		event->xRc = HvLpEvent_Rc_SubtypeError;
		cevent->subtype_result_code = viorc_openRejected;
		reject = 2;
	} else {
		port_info[port].lp = event->xSourceLp;
		event->xRc = HvLpEvent_Rc_Good;
		cevent->subtype_result_code = viorc_good;
		sndMsgSeq[port] = sndMsgAck[port] = 0;
		reject = 0;
	}

	spin_unlock_irqrestore(&consolelock, flags);

	if (reject == 1)
		printk("viocons: console open rejected : bad virtual tty.\n");
	else if (reject == 2)
		printk("viocons: console open rejected : console in exclusive use by another partition.\n");

	/* Return the acknowledgement */
	HvCallEvent_ackLpEvent(event);
}

/*
 * Handle a close open charLpEvent.  Could be either interrupt or ack
 */
static void vioHandleCloseEvent(struct HvLpEvent *event)
{
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	u8 port = cevent->virtual_device;

	if (event->xFlags.xFunction == HvLpEvent_Function_Int) {
		if (port >= VTTY_PORTS)
			return;

		/* For closes, just mark the console partition invalid */
		spin_lock_irqsave(&consolelock, flags);
		/* Got the lock, don't cause console output */

		if (port_info[port].lp == event->xSourceLp)
			port_info[port].lp = HvLpIndexInvalid;

		spin_unlock_irqrestore(&consolelock, flags);
		printk(KERN_INFO_VIO
		       "console close from %d\n", event->xSourceLp);
	} else
		printk(KERN_WARNING_VIO
		       "console got unexpected close acknowlegement\n");
}

/*
 * Handle a config charLpEvent.  Could be either interrupt or ack
 */
static void vioHandleConfig(struct HvLpEvent *event)
{
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	int len;

	len = cevent->len;
	HvCall_writeLogBuffer(cevent->data, cevent->len);

	if (cevent->data[0] == 0x01)
		printk(KERN_INFO_VIO
		       "console window resized to %d: %d: %d: %d\n",
		       cevent->data[1], cevent->data[2],
		       cevent->data[3], cevent->data[4]);
	else
		printk(KERN_WARNING_VIO "console unknown config event\n");
	return;
}

/*
 * Handle a data charLpEvent. 
 */
static void vioHandleData(struct HvLpEvent *event)
{
	struct tty_struct *tty;
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	struct port_info_tag *pi;
	int len;
	u8 port = cevent->virtual_device;

	if (port >= VTTY_PORTS) {
		printk(KERN_WARNING_VIO
		       "console data on invalid virtual device %d\n", port);
		return;
	}
	/*
	 * Change 05/01/2003 - Ryan Arnold: If a partition other than
	 * the current exclusive partition tries to send us data
	 * events then just drop them on the floor because we don't
	 * want his stinking data.  He isn't authorized to receive
	 * data because he wasn't the first one to get the console,
	 * therefore he shouldn't be allowed to send data either.
	 * This will work without an iSeries fix.
	 */
	if (port_info[port].lp != event->xSourceLp)
		return;

	tty = port_info[port].tty;

	if (tty == NULL) {
		printk(KERN_WARNING_VIO "no tty for virtual device %d\n", port);
		return;
	}

	if (tty->magic != TTY_MAGIC) {
		printk(KERN_WARNING_VIO "tty bad magic\n");
		return;
	}

	/*
	 * Hold the spinlock so that we don't take an interrupt that
	 * changes tty between the time we fetch the port_info_tag
	 * pointer and the time we paranoia check.
	 */
	spin_lock_irqsave(&consolelock, flags);

	/*
	 * Just to be paranoid, make sure the tty points back to this port
	 */
	pi = (struct port_info_tag *)tty->driver_data;
	if (!pi || viotty_paranoia_check(pi, tty->name, "vioHandleData")) {
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}
	spin_unlock_irqrestore(&consolelock, flags);

	len = cevent->len;
	if (len == 0)
		return;

	/*
	 * Don't log the user's input to the hypervisor log or their password
	 * will appear on a hypervisor log display.
	 */

	/* Don't copy more bytes than there is room for in the buffer */
	if (tty->flip.count + len > TTY_FLIPBUF_SIZE) {
		len = TTY_FLIPBUF_SIZE - tty->flip.count;
		printk(KERN_WARNING_VIO "console input buffer overflow!\n");
	}

	memcpy(tty->flip.char_buf_ptr, cevent->data, len);
	memset(tty->flip.flag_buf_ptr, TTY_NORMAL, len);

	/* Update the kernel buffer end */
	tty->flip.count += len;
	tty->flip.char_buf_ptr += len;
	tty->flip.flag_buf_ptr += len;
	tty_flip_buffer_push(tty);
}

/*
 * Handle an ack charLpEvent. 
 */
static void vioHandleAck(struct HvLpEvent *event)
{
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	unsigned long flags;
	u8 port = cevent->virtual_device;

	if (port >= VTTY_PORTS) {
		printk(KERN_WARNING_VIO
		       "viocons: data on invalid virtual device\n");
		return;
	}

	spin_lock_irqsave(&consolelock, flags);
	sndMsgAck[port] = event->xCorrelationToken;
	spin_unlock_irqrestore(&consolelock, flags);

	if (overflow[port].used)
		send_buffers(port, port_info[port].lp);
}

/*
 * Handle charLpEvents and route to the appropriate routine
 */
static void vioHandleCharEvent(struct HvLpEvent *event)
{
	int charminor;

	if (event == NULL)
		return;

	charminor = event->xSubtype & VIOMINOR_SUBTYPE_MASK;
	switch (charminor) {
	case viocharopen:
		vioHandleOpenEvent(event);
		break;
	case viocharclose:
		vioHandleCloseEvent(event);
		break;
	case viochardata:
		vioHandleData(event);
		break;
	case viocharack:
		vioHandleAck(event);
		break;
	case viocharconfig:
		vioHandleConfig(event);
		break;
	default:
		if ((event->xFlags.xFunction == HvLpEvent_Function_Int) &&
		    (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck)) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

/*
 * Send an open event
 */
static int send_open(HvLpIndex remoteLp, void *sem)
{
	return HvCallEvent_signalLpEventFast(remoteLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_chario | viocharopen,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(remoteLp),
			viopath_targetinst(remoteLp),
			(u64)(unsigned long)sem, VIOVERSION << 16,
			0, 0, 0, 0);
}

static struct tty_operations serial_ops = {
	.open = viotty_open,
	.close = viotty_close,
	.write = viotty_write,
	.put_char = viotty_put_char,
	.flush_chars = viotty_flush_chars,
	.write_room = viotty_write_room,
	.chars_in_buffer = viotty_chars_in_buffer,
	.flush_buffer = viotty_flush_buffer,
	.ioctl = viotty_ioctl,
	.throttle = viotty_throttle,
	.unthrottle = viotty_unthrottle,
	.set_termios = viotty_set_termios,
	.stop = viotty_stop,
	.start = viotty_start,
	.hangup = viotty_hangup,
	.break_ctl = viotty_break,
	.send_xchar = viotty_send_xchar,
	.wait_until_sent = viotty_wait_until_sent,
};

int __init viocons_init2(void)
{
	DECLARE_MUTEX_LOCKED(Semaphore);
	int rc;

	/* Now open to the primary LP */
	printk(KERN_INFO_VIO "console open path to primary\n");
	/* +2 for fudge */
	rc = viopath_open(HvLpConfig_getPrimaryLpIndex(),
			viomajorsubtype_chario, VIOCHAR_WINDOW + 2);
	if (rc)
		printk(KERN_WARNING_VIO "console error opening to primary %d\n",
				rc);

	if (viopath_hostLp == HvLpIndexInvalid)
		vio_set_hostlp();

	/*
	 * And if the primary is not the same as the hosting LP, open to the 
	 * hosting lp
	 */
	if ((viopath_hostLp != HvLpIndexInvalid) &&
	    (viopath_hostLp != HvLpConfig_getPrimaryLpIndex())) {
		printk(KERN_INFO_VIO "console open path to hosting (%d)\n",
				viopath_hostLp);
		rc = viopath_open(viopath_hostLp, viomajorsubtype_chario,
				VIOCHAR_WINDOW + 2);	/* +2 for fudge */
		if (rc)
			printk(KERN_WARNING_VIO
				"console error opening to partition %d: %d\n",
				viopath_hostLp, rc);
	}

	if (vio_setHandler(viomajorsubtype_chario, vioHandleCharEvent) < 0)
		printk(KERN_WARNING_VIO
				"Error seting handler for console events!\n");

	printk(KERN_INFO_VIO "console major number is %d\n", TTY_MAJOR);

	/* First, try to open the console to the hosting lp.
	 * Wait on a semaphore for the response.
	 */
	if ((viopath_isactive(viopath_hostLp)) &&
	    (send_open(viopath_hostLp, (void *)&Semaphore) == 0)) {
		printk(KERN_INFO_VIO
			"opening console to hosting partition %d\n",
			viopath_hostLp);
		//down(&Semaphore);
		while (atomic_read(&Semaphore.count) == 0)
			mb();
		atomic_set(&Semaphore.count, 0);
	}

	/*
	 * If we don't have an active console, try the primary
	 */
	if ((!viopath_isactive(port_info[0].lp)) &&
	    (viopath_isactive(HvLpConfig_getPrimaryLpIndex())) &&
	    (send_open(HvLpConfig_getPrimaryLpIndex(), (void *)&Semaphore)
	     == 0)) {
		printk(KERN_INFO_VIO "opening console to primary partition\n");
		//down(&Semaphore);
		while (atomic_read(&Semaphore.count) == 0)
			mb();
		atomic_set(&Semaphore.count, 0);
	}
	return 0;
}

static int viocons_init3(void)
{
	int ret = viocons_init2();

	if (ret)
		return ret;

	/* Initialize the tty_driver structure */
	memset(&viotty_driver, 0, sizeof(struct tty_driver));
	viotty_driver.magic = TTY_DRIVER_MAGIC;
	viotty_driver.owner = THIS_MODULE;
	viotty_driver.driver_name = "vioconsole";
	viotty_driver.devfs_name = "vcs/";
	viotty_driver.name = "tty";
	viotty_driver.major = TTY_MAJOR;
	viotty_driver.minor_start = 1;
	viotty_driver.num = VTTY_PORTS;
	viotty_driver.type = TTY_DRIVER_TYPE_CONSOLE;
	viotty_driver.subtype = 1;
	viotty_driver.init_termios = tty_std_termios;
	viotty_driver.flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_RESET_TERMIOS;
	viotty_driver.termios = viotty_termios;
	viotty_driver.termios_locked = viotty_termios_locked;
	tty_set_operations(&viotty_driver, &serial_ops);

	viottyS_driver = viotty_driver;
	viottyS_driver.devfs_name = "tts/";
	viottyS_driver.name = "ttyS";
	viottyS_driver.major = TTY_MAJOR;
	viottyS_driver.minor_start = VIOTTY_SERIAL_START;
	viottyS_driver.type = TTY_DRIVER_TYPE_SERIAL;
	viottyS_driver.termios = viottyS_termios;
	viottyS_driver.termios_locked = viottyS_termios_locked;

	if (tty_register_driver(&viotty_driver))
		printk(KERN_WARNING_VIO "Couldn't register console driver\n");

	if (tty_register_driver(&viottyS_driver))
		printk(KERN_WARNING_VIO "Couldn't register console S driver\n");
	/* Now create the vcs and vcsa devfs entries so mingetty works */
#if defined(CONFIG_DEVFS_FS)
	{
		struct tty_driver temp_driver = viotty_driver;
		int i;

		temp_driver.name = "vcs%d";
		for (i = 0; i < VTTY_PORTS; i++)
			tty_register_devfs(&temp_driver,
					   0, i + temp_driver.minor_start);

		temp_driver.name = "vcsa%d";
		for (i = 0; i < VTTY_PORTS; i++)
			tty_register_devfs(&temp_driver,
					   0, i + temp_driver.minor_start);

		/*
		 * For compatibility with some earlier code only!
		 * This will go away!!!
		 */
		temp_driver.name = "viocons/%d";
		temp_driver.name_base = 0;
		for (i = 0; i < VTTY_PORTS; i++)
			tty_register_devfs(&temp_driver,
					   0, i + temp_driver.minor_start);
	}
#endif

	return 0;
}

static int __init viocons_init(void)
{
	int i;

	printk(KERN_INFO_VIO "registering console\n");
	for (i = 0; i < VTTY_PORTS; i++) {
		port_info[i].port = i;
		port_info[i].lp = HvLpIndexInvalid;
		port_info[i].magic = VIOTTY_MAGIC;
	}
	HvCall_setLogBufferFormatAndCodepage(HvCall_LogBuffer_ASCII, 437);
	register_console(&viocons);
	return 0;
}

console_initcall(viocons_init);
module_init(viocons_init3);
