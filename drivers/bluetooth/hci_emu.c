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
 * BlueZ HCI virtual device driver.
 *
 * $Id: hci_emu.c,v 1.1 2001/06/01 08:12:10 davem Exp $ 
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/random.h>

#include <linux/skbuff.h>
#include <linux/miscdevice.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/bluez.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci_emu.h>

/* HCI device part */

int hci_emu_open(struct hci_dev *hdev)
{
	hdev->flags |= HCI_RUNNING;
	return 0;
}

int hci_emu_flush(struct hci_dev *hdev)
{
	struct hci_emu_struct *hci_emu = (struct hci_emu_struct *) hdev->driver_data;
	bluez_skb_queue_purge(&hci_emu->readq);
	return 0;
}

int hci_emu_close(struct hci_dev *hdev)
{
	hdev->flags &= ~HCI_RUNNING;
	hci_emu_flush(hdev);
	return 0;
}

int hci_emu_send_frame(struct sk_buff *skb)
{
	struct hci_dev* hdev = (struct hci_dev *) skb->dev;
	struct hci_emu_struct *hci_emu;

	if (!hdev) {
		ERR("Frame for uknown device (hdev=NULL)");
		return -ENODEV;
	}

	if (!(hdev->flags & HCI_RUNNING))
		return -EBUSY;

	hci_emu = (struct hci_emu_struct *) hdev->driver_data;

	memcpy(skb_push(skb, 1), &skb->pkt_type, 1);
	skb_queue_tail(&hci_emu->readq, skb);

	if (hci_emu->flags & HCI_EMU_FASYNC)
		kill_fasync(&hci_emu->fasync, SIGIO, POLL_IN);
	wake_up_interruptible(&hci_emu->read_wait);

	return 0;
}

/* Character device part */

/* Poll */
static unsigned int hci_emu_chr_poll(struct file *file, poll_table * wait)
{  
	struct hci_emu_struct *hci_emu = (struct hci_emu_struct *) file->private_data;

	poll_wait(file, &hci_emu->read_wait, wait);
 
	if (skb_queue_len(&hci_emu->readq))
		return POLLIN | POLLRDNORM;

	return POLLOUT | POLLWRNORM;
}

/* Get packet from user space buffer(already verified) */
static __inline__ ssize_t hci_emu_get_user(struct hci_emu_struct *hci_emu, const char *buf, size_t count)
{
	struct sk_buff *skb;

	if (count > HCI_EMU_MAX_FRAME)
		return -EINVAL;

	if (!(skb = bluez_skb_alloc(count, GFP_KERNEL)))
		return -ENOMEM;
	
	copy_from_user(skb_put(skb, count), buf, count); 

	skb->dev = (void *) &hci_emu->hdev;
	skb->pkt_type = *((__u8 *) skb->data);
	skb_pull(skb, 1);

	hci_recv_frame(skb);

	return count;
} 

/* Write */
static ssize_t hci_emu_chr_write(struct file * file, const char * buf, 
			     size_t count, loff_t *pos)
{
	struct hci_emu_struct *hci_emu = (struct hci_emu_struct *) file->private_data;

	if (verify_area(VERIFY_READ, buf, count))
		return -EFAULT;

	return hci_emu_get_user(hci_emu, buf, count);
}

/* Put packet to user space buffer(already verified) */
static __inline__ ssize_t hci_emu_put_user(struct hci_emu_struct *hci_emu,
				       struct sk_buff *skb, char *buf, int count)
{
	int len = count, total = 0;
	char *ptr = buf;

	len = MIN(skb->len, len); 
	copy_to_user(ptr, skb->data, len); 
	total += len;

	hci_emu->hdev.stat.byte_tx += len;
	switch (skb->pkt_type) {
		case HCI_COMMAND_PKT:
			hci_emu->hdev.stat.cmd_tx++;
			break;

		case HCI_ACLDATA_PKT:
			hci_emu->hdev.stat.acl_tx++;
			break;

		case HCI_SCODATA_PKT:
			hci_emu->hdev.stat.cmd_tx++;
			break;
	};

	return total;
}

/* Read */
static ssize_t hci_emu_chr_read(struct file * file, char * buf, size_t count, loff_t *pos)
{
	struct hci_emu_struct *hci_emu = (struct hci_emu_struct *) file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	struct sk_buff *skb;
	ssize_t ret = 0;

	add_wait_queue(&hci_emu->read_wait, &wait);
	while (count) {
		current->state = TASK_INTERRUPTIBLE;

		/* Read frames from device queue */
		if (!(skb = skb_dequeue(&hci_emu->readq))) {
			if (file->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}

			/* Nothing to read, let's sleep */
			schedule();
			continue;
		}

		if (!verify_area(VERIFY_WRITE, buf, count))
			ret = hci_emu_put_user(hci_emu, skb, buf, count);
		else
			ret = -EFAULT;

		bluez_skb_free(skb);
		break;
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&hci_emu->read_wait, &wait);

	return ret;
}

static int hci_emu_chr_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static int hci_emu_chr_fasync(int fd, struct file *file, int on)
{
	struct hci_emu_struct *hci_emu = (struct hci_emu_struct *) file->private_data;
	int ret;

	if ((ret = fasync_helper(fd, file, on, &hci_emu->fasync)) < 0)
		return ret; 
 
	if (on)
		hci_emu->flags |= HCI_EMU_FASYNC;
	else 
		hci_emu->flags &= ~HCI_EMU_FASYNC;

	return 0;
}

static int hci_emu_chr_open(struct inode *inode, struct file * file)
{
	struct hci_emu_struct *hci_emu = NULL; 
	struct hci_dev *hdev;

	if (!(hci_emu = kmalloc(sizeof(struct hci_emu_struct), GFP_KERNEL)))
		return -ENOMEM;

	memset(hci_emu, 0, sizeof(struct hci_emu_struct));

	skb_queue_head_init(&hci_emu->readq);
	init_waitqueue_head(&hci_emu->read_wait);

	/* Initialize and register HCI device */
	hdev = &hci_emu->hdev;

	hdev->type = HCI_EMU;
	hdev->driver_data = hci_emu;

	hdev->open  = hci_emu_open;
	hdev->close = hci_emu_close;
	hdev->flush = hci_emu_flush;
	hdev->send  = hci_emu_send_frame;

	if (hci_register_dev(hdev) < 0) {
		kfree(hci_emu);
		return -EBUSY;
	}

	file->private_data = hci_emu;
	return 0;   
}

static int hci_emu_chr_close(struct inode *inode, struct file *file)
{
	struct hci_emu_struct *hci_emu = (struct hci_emu_struct *) file->private_data;

	if (hci_unregister_dev(&hci_emu->hdev) < 0) {
		ERR("Can't unregister HCI device %s", hci_emu->hdev.name);
	}

	kfree(hci_emu);
	file->private_data = NULL;

	return 0;
}

static struct file_operations hci_emu_fops = {
	owner:	THIS_MODULE,	
	llseek:	no_llseek,
	read:	hci_emu_chr_read,
	write:	hci_emu_chr_write,
	poll:	hci_emu_chr_poll,
	ioctl:	hci_emu_chr_ioctl,
	open:	hci_emu_chr_open,
	release:hci_emu_chr_close,
	fasync:	hci_emu_chr_fasync		
};

static struct miscdevice hci_emu_miscdev=
{
        HCI_EMU_MINOR,
        "hci_emu",
        &hci_emu_fops
};

int __init hci_emu_init(void)
{
	INF("BlueZ HCI EMU driver ver %s Copyright (C) 2000,2001 Qualcomm Inc",  
		BLUEZ_VER);
	INF("Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>");

	if (misc_register(&hci_emu_miscdev)) {
		ERR("Can't register misc device %d\n", HCI_EMU_MINOR);
		return -EIO;
	}

	return 0;
}

void hci_emu_cleanup(void)
{
	misc_deregister(&hci_emu_miscdev);
}

module_init(hci_emu_init);
module_exit(hci_emu_cleanup);
