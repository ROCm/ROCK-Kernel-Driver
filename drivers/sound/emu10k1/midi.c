
/*
 **********************************************************************
 *     midi.c - /dev/midi interface for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>

#include "hwaccess.h"
#include "cardmo.h"
#include "cardmi.h"
#include "midi.h"

static spinlock_t midi_spinlock __attribute((unused)) = SPIN_LOCK_UNLOCKED;

static void init_midi_hdr(struct midi_hdr *midihdr)
{
	midihdr->bufferlength = MIDIIN_BUFLEN;
	midihdr->bytesrecorded = 0;
	midihdr->flags = 0;
}

static int midiin_add_buffer(struct emu10k1_mididevice *midi_dev, struct midi_hdr **midihdrptr)
{
	struct midi_hdr *midihdr;

	if ((midihdr = (struct midi_hdr *) kmalloc(sizeof(struct midi_hdr *), GFP_KERNEL)) == NULL) {
		ERROR();
		return -EINVAL;
	}

	init_midi_hdr(midihdr);

	if ((midihdr->data = (u8 *) kmalloc(MIDIIN_BUFLEN, GFP_KERNEL)) == NULL) {
		ERROR();
		kfree(midihdr);
		return -1;
	}

	if (emu10k1_mpuin_add_buffer(midi_dev->card->mpuin, midihdr) < 0) {
		ERROR();
		kfree(midihdr->data);
		kfree(midihdr);
		return -1;
	}

	*midihdrptr = midihdr;
	list_add_tail(&midihdr->list, &midi_dev->mid_hdrs);

	return 0;
}

static int emu10k1_midi_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct emu10k1_card *card;
	struct emu10k1_mididevice *midi_dev;
	struct list_head *entry;

	DPF(2, "emu10k1_midi_open()\n");

	/* Check for correct device to open */
	list_for_each(entry, &emu10k1_devs) {
		card = list_entry(entry, struct emu10k1_card, list);

		if (card->midi_num == minor)
			break;
	}

	if (entry == &emu10k1_devs)
		return -ENODEV;

	/* Wait for device to become free */
	down(&card->open_sem);
	while (card->open_mode & (file->f_mode << FMODE_MIDI_SHIFT)) {
		if (file->f_flags & O_NONBLOCK) {
			up(&card->open_sem);
			return -EBUSY;
		}

		up(&card->open_sem);
		interruptible_sleep_on(&card->open_wait);

		if (signal_pending(current)) {
			return -ERESTARTSYS;
		}

		down(&card->open_sem);
	}

	if ((midi_dev = (struct emu10k1_mididevice *) kmalloc(sizeof(*midi_dev), GFP_KERNEL)) == NULL) {
		return -EINVAL;
	}

	midi_dev->card = card;
	midi_dev->mistate = MIDIIN_STATE_STOPPED;
	init_waitqueue_head(&midi_dev->oWait);
	init_waitqueue_head(&midi_dev->iWait);
	midi_dev->ird = 0;
	midi_dev->iwr = 0;
	midi_dev->icnt = 0;
	INIT_LIST_HEAD(&midi_dev->mid_hdrs);

	if (file->f_mode & FMODE_READ) {
		struct midi_openinfo dsCardMidiOpenInfo;
		struct midi_hdr *midihdr1;
		struct midi_hdr *midihdr2;

		dsCardMidiOpenInfo.refdata = (unsigned long) midi_dev;

		if (emu10k1_mpuin_open(card, &dsCardMidiOpenInfo) < 0) {
			ERROR();
			kfree(midi_dev);
			return -ENODEV;
		}

		/* Add two buffers to receive sysex buffer */
		if (midiin_add_buffer(midi_dev, &midihdr1) < 0) {
			kfree(midi_dev);
			return -ENODEV;
		}

		if (midiin_add_buffer(midi_dev, &midihdr2) < 0) {
			list_del(&midihdr1->list);
			kfree(midihdr1->data);
			kfree(midihdr1);
			kfree(midi_dev);
			return -ENODEV;
		}
	}

	if (file->f_mode & FMODE_WRITE) {
		struct midi_openinfo dsCardMidiOpenInfo;

		dsCardMidiOpenInfo.refdata = (unsigned long) midi_dev;

		if (emu10k1_mpuout_open(card, &dsCardMidiOpenInfo) < 0) {
			ERROR();
			kfree(midi_dev);
			return -ENODEV;
		}
	}

	file->private_data = (void *) midi_dev;

	card->open_mode |= (file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ | FMODE_MIDI_WRITE);

	up(&card->open_sem);

	return 0;
}

static int emu10k1_midi_release(struct inode *inode, struct file *file)
{
	struct emu10k1_mididevice *midi_dev = (struct emu10k1_mididevice *) file->private_data;
	struct emu10k1_card *card;

	lock_kernel();

	card = midi_dev->card;
	DPF(2, "emu10k1_midi_release()\n");

	if (file->f_mode & FMODE_WRITE) {
		if (!(file->f_flags & O_NONBLOCK)) {

			while (!signal_pending(current) && (card->mpuout->firstmidiq != NULL)) {
				DPF(4, "Cannot close - buffers not empty\n");

				interruptible_sleep_on(&midi_dev->oWait);

			}
		}

		emu10k1_mpuout_close(card);
	}

	if (file->f_mode & FMODE_READ) {
		struct midi_hdr *midihdr;

		if (midi_dev->mistate == MIDIIN_STATE_STARTED) {
			emu10k1_mpuin_stop(card);
			midi_dev->mistate = MIDIIN_STATE_STOPPED;
		}

		emu10k1_mpuin_reset(card);
		emu10k1_mpuin_close(card);

		while (!list_empty(&midi_dev->mid_hdrs)) {
			midihdr = list_entry(midi_dev->mid_hdrs.next, struct midi_hdr, list);

			list_del(midi_dev->mid_hdrs.next);
			kfree(midihdr->data);
			kfree(midihdr);
		}
	}

	kfree(midi_dev);

	down(&card->open_sem);
	card->open_mode &= ~((file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ | FMODE_MIDI_WRITE));
	up(&card->open_sem);
	wake_up_interruptible(&card->open_wait);

	unlock_kernel();

	return 0;
}

static ssize_t emu10k1_midi_read(struct file *file, char *buffer, size_t count, loff_t * pos)
{
	struct emu10k1_mididevice *midi_dev = (struct emu10k1_mididevice *) file->private_data;
	ssize_t ret = 0;
	u16 cnt;
	unsigned long flags;

	DPD(4, "emu10k1_midi_read(), count %x\n", (u32) count);

	if (pos != &file->f_pos)
		return -ESPIPE;

	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;

	if (midi_dev->mistate == MIDIIN_STATE_STOPPED) {
		if (emu10k1_mpuin_start(midi_dev->card) < 0) {
			ERROR();
			return -EINVAL;
		}

		midi_dev->mistate = MIDIIN_STATE_STARTED;
	}

	while (count > 0) {
		cnt = MIDIIN_BUFLEN - midi_dev->ird;

		spin_lock_irqsave(&midi_spinlock, flags);

		if (midi_dev->icnt < cnt)
			cnt = midi_dev->icnt;

		spin_unlock_irqrestore(&midi_spinlock, flags);

		if (cnt > count)
			cnt = count;

		if (cnt <= 0) {
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EAGAIN;
			DPF(2, " Go to sleep...\n");

			interruptible_sleep_on(&midi_dev->iWait);

			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;

			continue;
		}

		if (copy_to_user(buffer, midi_dev->iBuf + midi_dev->ird, cnt)) {
			ERROR();
			return ret ? ret : -EFAULT;
		}

		midi_dev->ird += cnt;
		midi_dev->ird %= MIDIIN_BUFLEN;

		spin_lock_irqsave(&midi_spinlock, flags);

		midi_dev->icnt -= cnt;

		spin_unlock_irqrestore(&midi_spinlock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;

		if (midi_dev->icnt == 0)
			break;
	}

	return ret;
}

static ssize_t emu10k1_midi_write(struct file *file, const char *buffer, size_t count, loff_t * pos)
{
	struct emu10k1_mididevice *midi_dev = (struct emu10k1_mididevice *) file->private_data;
	struct midi_hdr *midihdr;
	ssize_t ret = 0;
	unsigned long flags;

	DPD(4, "emu10k1_midi_write(), count=%x\n", (u32) count);

	if (pos != &file->f_pos)
		return -ESPIPE;

	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;

	if ((midihdr = (struct midi_hdr *) kmalloc(sizeof(struct midi_hdr *), GFP_KERNEL)) == NULL)
		return -EINVAL;

	midihdr->bufferlength = count;
	midihdr->bytesrecorded = 0;
	midihdr->flags = 0;

	if ((midihdr->data = (u8 *) kmalloc(count, GFP_KERNEL)) == NULL) {
		ERROR();
		kfree(midihdr);
		return -EINVAL;
	}

	if (copy_from_user(midihdr->data, buffer, count)) {
		kfree(midihdr->data);
		kfree(midihdr);
		return ret ? ret : -EFAULT;
	}

	spin_lock_irqsave(&midi_spinlock, flags);

	if (emu10k1_mpuout_add_buffer(midi_dev->card, midihdr) < 0) {
		ERROR();
		kfree(midihdr->data);
		kfree(midihdr);
		spin_unlock_irqrestore(&midi_spinlock, flags);
		return -EINVAL;
	}

	spin_unlock_irqrestore(&midi_spinlock, flags);

	return count;
}

static unsigned int emu10k1_midi_poll(struct file *file, struct poll_table_struct *wait)
{
	DPF(4, "emu10k1_midi_poll() called\n");
	return 0;
}

int emu10k1_midi_callback(unsigned long msg, unsigned long refdata, unsigned long *pmsg)
{
	struct emu10k1_mididevice *midi_dev = (struct emu10k1_mididevice *) refdata;
	struct midi_hdr *midihdr = NULL;
	unsigned long flags;
	int i;

	DPF(4, "emu10k1_midi_callback()\n");

	spin_lock_irqsave(&midi_spinlock, flags);

	switch (msg) {
	case ICARDMIDI_OUTLONGDATA:
		midihdr = (struct midi_hdr *) pmsg[2];

		kfree(midihdr->data);
		kfree(midihdr);
		wake_up_interruptible(&midi_dev->oWait);

		break;

	case ICARDMIDI_INLONGDATA:
		midihdr = (struct midi_hdr *) pmsg[2];

		for (i = 0; i < midihdr->bytesrecorded; i++) {
			midi_dev->iBuf[midi_dev->iwr++] = midihdr->data[i];
			midi_dev->iwr %= MIDIIN_BUFLEN;
		}

		midi_dev->icnt += midihdr->bytesrecorded;

		if (midi_dev->mistate == MIDIIN_STATE_STARTED) {
			init_midi_hdr(midihdr);
			emu10k1_mpuin_add_buffer(midi_dev->card->mpuin, midihdr);
			wake_up_interruptible(&midi_dev->iWait);
		}
		break;

	case ICARDMIDI_INDATA:
		{
			u8 *pBuf = (u8 *) & pmsg[1];
			u16 bytesvalid = pmsg[2];

			for (i = 0; i < bytesvalid; i++) {
				midi_dev->iBuf[midi_dev->iwr++] = pBuf[i];
				midi_dev->iwr %= MIDIIN_BUFLEN;
			}

			midi_dev->icnt += bytesvalid;
		}

		wake_up_interruptible(&midi_dev->iWait);
		break;

	default:		/* Unknown message */
		return -1;
	}

	spin_unlock_irqrestore(&midi_spinlock, flags);

	return 0;
}

/* MIDI file operations */
struct file_operations emu10k1_midi_fops = {
        owner:		THIS_MODULE,
	read:		emu10k1_midi_read,
	write:		emu10k1_midi_write,
	poll:		emu10k1_midi_poll,
	open:		emu10k1_midi_open,
	release:	emu10k1_midi_release,
};
