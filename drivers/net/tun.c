/*
 *  TUN - Universal TUN/TAP device driver.
 *  Copyright (C) 1999-2000 Maxim Krasnyansky <max_mk@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  $Id: tun.c,v 1.3 2000/10/23 10:01:25 maxk Exp $
 */

/*
 *  Daniel Podlejski <underley@underley.eu.org>
 *    Modifications for 2.3.99-pre5 kernel.
 */

#define TUN_VER "1.3"

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/random.h>

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/miscdevice.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#ifdef TUN_DEBUG
static int debug=0;
#endif

/* Network device part of the driver */

/* Net device open. */
static int tun_net_open(struct net_device *dev)
{
#ifdef TUN_DEBUG  
	struct tun_struct *tun = (struct tun_struct *)dev->priv;

	DBG(KERN_INFO "%s: tun_net_open\n", tun->name);
#endif

	netif_start_queue(dev);

	return 0;
}

/* Net device close. */
static int tun_net_close(struct net_device *dev)
{
#ifdef TUN_DEBUG  
	struct tun_struct *tun = (struct tun_struct *)dev->priv;

	DBG(KERN_INFO "%s: tun_net_close\n", tun->name);
#endif

	netif_stop_queue(dev);

	return 0;
}

/* Net device start xmit */
static int tun_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct tun_struct *tun = (struct tun_struct *)dev->priv;

	DBG(KERN_INFO "%s: tun_net_xmit %d\n", tun->name, skb->len);

	/* Queue frame */
	skb_queue_tail(&tun->txq, skb);
	if (skb_queue_len(&tun->txq) >= TUN_TXQ_SIZE)
		netif_stop_queue(dev);

	if (tun->flags & TUN_FASYNC)
		kill_fasync(&tun->fasync, SIGIO, POLL_IN);

	/* Wake up process */ 
	wake_up_interruptible(&tun->read_wait);

	return 0;
}

static void tun_net_mclist(struct net_device *dev)
{
#ifdef TUN_DEBUG
	struct tun_struct *tun = (struct tun_struct *)dev->priv;

	DBG(KERN_INFO "%s: tun_net_mclist\n", tun->name);
#endif

	/* Nothing to do for multicast filters. 
	 * We always accept all frames.
	 */
	return;
}

static struct net_device_stats *tun_net_stats(struct net_device *dev)
{
	struct tun_struct *tun = (struct tun_struct *)dev->priv;

	return &tun->stats;
}

/* Initialize net device. */
int tun_net_init(struct net_device *dev)
{
	struct tun_struct *tun = (struct tun_struct *)dev->priv;
   
	DBG(KERN_INFO "%s: tun_net_init\n", tun->name);

	SET_MODULE_OWNER(dev);
	dev->open = tun_net_open;
	dev->hard_start_xmit = tun_net_xmit;
	dev->stop = tun_net_close;
	dev->get_stats = tun_net_stats;

	switch (tun->flags & TUN_TYPE_MASK) {
	case TUN_TUN_DEV:
		/* Point-to-Point TUN Device */
		dev->hard_header_len = 0;
		dev->addr_len = 0;
		dev->mtu = 1500;

		/* Type PPP seems most suitable */
		dev->type = ARPHRD_PPP; 
		dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
		dev->tx_queue_len = 10;

		dev_init_buffers(dev);
		break;

	case TUN_TAP_DEV:
		/* Ethernet TAP Device */
		dev->set_multicast_list = tun_net_mclist;

		/* Generate random Ethernet address.  */
		*(u16 *)dev->dev_addr = htons(0x00FF);
		get_random_bytes(dev->dev_addr + sizeof(u16), 4);

		ether_setup(dev);
		break;
	};

	return 0;
}

/* Character device part */

/* Poll */
static unsigned int tun_chr_poll(struct file *file, poll_table * wait)
{  
	struct tun_struct *tun = (struct tun_struct *)file->private_data;

	DBG(KERN_INFO "%s: tun_chr_poll\n", tun->name);

	poll_wait(file, &tun->read_wait, wait);
 
	if (skb_queue_len(&tun->txq))
		return POLLIN | POLLRDNORM;

	return POLLOUT | POLLWRNORM;
}

/* Get packet from user space buffer(already verified) */
static __inline__ ssize_t tun_get_user(struct tun_struct *tun, const char *buf, size_t count)
{
	struct tun_pi pi = { 0, __constant_htons(ETH_P_IP) };
	register const char *ptr = buf; 
	register int len = count;
	struct sk_buff *skb;

	if (len > TUN_MAX_FRAME)
		return -EINVAL;

	if (!(tun->flags & TUN_NO_PI)) {
		if ((len -= sizeof(pi)) < 0)
			return -EINVAL;

		copy_from_user(&pi, ptr, sizeof(pi));
		ptr += sizeof(pi);
	}
 
	if (!(skb = alloc_skb(len + 2, GFP_KERNEL))) {
		tun->stats.rx_dropped++;
		return -ENOMEM;
	}

	skb_reserve(skb, 2);
	copy_from_user(skb_put(skb, len), ptr, len); 

	skb->dev = &tun->dev;
	switch (tun->flags & TUN_TYPE_MASK) {
	case TUN_TUN_DEV:
		skb->mac.raw = skb->data;
		skb->protocol = pi.proto;
		break;
	case TUN_TAP_DEV:
		skb->protocol = eth_type_trans(skb, &tun->dev);
		break;
	};

	if (tun->flags & TUN_NOCHECKSUM)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
 
	netif_rx(skb);
   
	tun->stats.rx_packets++;
	tun->stats.rx_bytes += len;

	return count;
} 

/* Write */
static ssize_t tun_chr_write(struct file * file, const char * buf, 
			     size_t count, loff_t *pos)
{
	struct tun_struct *tun = (struct tun_struct *)file->private_data;

	DBG(KERN_INFO "%s: tun_chr_write %d\n", tun->name, count);

	if (!(tun->flags & TUN_IFF_SET))
		return -EBUSY;

	if (verify_area(VERIFY_READ, buf, count))
		return -EFAULT;

	return tun_get_user(tun, buf, count);
}

/* Put packet to user space buffer(already verified) */
static __inline__ ssize_t tun_put_user(struct tun_struct *tun,
				       struct sk_buff *skb,
				       char *buf, int count)
{
	struct tun_pi pi = { 0, skb->protocol };
	int len = count, total = 0;
	char *ptr = buf;

	if (!(tun->flags & TUN_NO_PI)) {
		if ((len -= sizeof(pi)) < 0)
			return -EINVAL;

		if (len < skb->len) {
			/* Packet will be striped */
			pi.flags |= TUN_PKT_STRIP;
		}
 
		copy_to_user(ptr, &pi, sizeof(pi));

		total += sizeof(pi);
		ptr += sizeof(pi);
	}       

	len = MIN(skb->len, len); 
	copy_to_user(ptr, skb->data, len); 
	total += len;

	tun->stats.tx_packets++;
	tun->stats.tx_bytes += len;

	return total;
}

/* Read */
static ssize_t tun_chr_read(struct file * file, char * buf, 
			    size_t count, loff_t *pos)
{
	struct tun_struct *tun = (struct tun_struct *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	struct sk_buff *skb;
	ssize_t ret = 0;

	DBG(KERN_INFO "%s: tun_chr_read\n", tun->name);

	add_wait_queue(&tun->read_wait, &wait);
	while (count) {
		current->state = TASK_INTERRUPTIBLE;

		/* Read frames from device queue */
		if (!(skb=skb_dequeue(&tun->txq))) {
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
		netif_start_queue(&tun->dev);

		if (!verify_area(VERIFY_WRITE, buf, count))
			ret = tun_put_user(tun, skb, buf, count);
		else
			ret = -EFAULT;

		kfree_skb(skb);
		break;
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&tun->read_wait, &wait);

	return ret;
}

static loff_t tun_chr_lseek(struct file * file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static int tun_set_iff(struct tun_struct *tun, unsigned long arg)
{
	struct ifreq ifr;
	char *mask;

	if (copy_from_user(&ifr, (void *)arg, sizeof(ifr)))
		return -EFAULT;
	ifr.ifr_name[IFNAMSIZ-1] = '\0';

	if (tun->flags & TUN_IFF_SET)
		return -EEXIST;

	/* Set dev type */
	if (ifr.ifr_flags & IFF_TUN) {
		/* TUN device */
		tun->flags |= TUN_TUN_DEV;
		mask = "tun%d";
	} else if (ifr.ifr_flags & IFF_TAP) {
		/* TAP device */
		tun->flags |= TUN_TAP_DEV;
		mask = "tap%d";
	} else 
		return -EINVAL;
   
	if (ifr.ifr_flags & IFF_NO_PI)
		tun->flags |= TUN_NO_PI;

	if (*ifr.ifr_name)
		strcpy(tun->dev.name, ifr.ifr_name);
	else
		strcpy(tun->dev.name, mask);

	/* Register net device */
	if (register_netdev(&tun->dev))
		return -EBUSY;

	tun->flags |= TUN_IFF_SET;
	strcpy(tun->name, tun->dev.name);

	/* Return iface info to the user space */
	strcpy(ifr.ifr_name, tun->dev.name);
	copy_to_user((void *)arg, &ifr, sizeof(ifr));

	return 0;   
}

static int tun_chr_ioctl(struct inode *inode, struct file *file, 
			 unsigned int cmd, unsigned long arg)
{
	struct tun_struct *tun = (struct tun_struct *)file->private_data;

	DBG(KERN_INFO "%s: tun_chr_ioctl\n", tun->name);

	switch (cmd) {
	case TUNSETIFF:
		return tun_set_iff(tun, arg); 

	case TUNSETNOCSUM:
		/* Disable/Enable checksum on net iface */
		if (arg)
			tun->flags |= TUN_NOCHECKSUM;
		else
			tun->flags &= ~TUN_NOCHECKSUM;

		DBG(KERN_INFO "%s: checksum %s\n",
		    tun->name, arg ? "disabled" : "enabled");
		break;

#ifdef TUN_DEBUG
	case TUNSETDEBUG:
		tun->debug = arg;
		break;
#endif

	default:
		return -EINVAL;
	};

	return 0;
}

static int tun_chr_fasync(int fd, struct file *file, int on)
{
	struct tun_struct *tun = (struct tun_struct *)file->private_data;
	int ret;

	DBG(KERN_INFO "%s: tun_chr_fasync %d\n", tun->name, on);

	if ((ret = fasync_helper(fd, file, on, &tun->fasync)) < 0)
		return ret; 
 
	if (on)
		tun->flags |= TUN_FASYNC;
	else 
		tun->flags &= ~TUN_FASYNC;

	return 0;
}

static int tun_chr_open(struct inode *inode, struct file * file)
{
	struct tun_struct *tun = NULL; 

	DBG1(KERN_INFO "tunX: tun_chr_open\n");

	tun = kmalloc(sizeof(struct tun_struct), GFP_KERNEL);
	if (tun == NULL)
		return -ENOMEM;

	memset(tun, 0, sizeof(struct tun_struct));
	file->private_data = tun;

	skb_queue_head_init(&tun->txq);
	init_waitqueue_head(&tun->read_wait);

	sprintf(tun->name, "tunX");

	tun->dev.init = tun_net_init;
	tun->dev.priv = tun;

	return 0;
}

static int tun_chr_close(struct inode *inode, struct file *file)
{
	struct tun_struct *tun = (struct tun_struct *)file->private_data;

	DBG(KERN_INFO "%s: tun_chr_close\n", tun->name);

	if (tun->flags & TUN_IFF_SET) {
		rtnl_lock();
		dev_close(&tun->dev);
		rtnl_unlock();

		/* Drop TX queue */
		skb_queue_purge(&tun->txq);

		unregister_netdev(&tun->dev);
	}

	kfree(tun);
	file->private_data = NULL;

	return 0;
}

static struct file_operations tun_fops = {
	owner:	THIS_MODULE,	
	llseek:	tun_chr_lseek,
	read:	tun_chr_read,
	write:	tun_chr_write,
	poll:	tun_chr_poll,
	ioctl:	tun_chr_ioctl,
	open:	tun_chr_open,
	release:tun_chr_close,
	fasync:	tun_chr_fasync		
};

static struct miscdevice tun_miscdev=
{
        TUN_MINOR,
        "net/tun",
        &tun_fops
};

int __init tun_init(void)
{
	printk(KERN_INFO "Universal TUN/TAP device driver %s " 
	       "(C)1999-2000 Maxim Krasnyansky\n", TUN_VER);

	if (misc_register(&tun_miscdev)) {
		printk(KERN_ERR "tun: Can't register misc device %d\n", TUN_MINOR);
		return -EIO;
	}

	return 0;
}

void tun_cleanup(void)
{
	misc_deregister(&tun_miscdev);  
}

module_init(tun_init);
module_exit(tun_cleanup);
