/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Gerhard Tonn (ton@de.ibm.com)
 *
 * Heavily inspired by the 32-bit Sparc compat code which is  
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Written by Ulf Carlsson (ulfc@engr.sgi.com) 
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/netdevice.h>
#include <linux/route.h>
#include <linux/ext2_fs.h>
#include <linux/hdreg.h>
#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/dasd.h>
#include <asm/sockios.h>

#include "linux32.h"

long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

struct hd_geometry32 {
	unsigned char	heads;
	unsigned char	sectors;
	unsigned short	cylinders;
	__u32		start;
};  

static inline int hd_geometry_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct hd_geometry32 *hg32 = (struct hd_geometry32 *) A(arg);
	struct hd_geometry hg;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, cmd, (long)&hg);
	set_fs (old_fs);

	if (ret)
		return ret;

	ret = put_user (hg.heads, &(hg32->heads));
	ret |= __put_user (hg.sectors, &(hg32->sectors));
	ret |= __put_user (hg.cylinders, &(hg32->cylinders));
	ret |= __put_user (hg.start, &(hg32->start));

	return ret;
}

struct timeval32 {
	int tv_sec;
	int tv_usec;
};

#define EXT2_IOC32_GETFLAGS               _IOR('f', 1, int)
#define EXT2_IOC32_SETFLAGS               _IOW('f', 2, int)
#define EXT2_IOC32_GETVERSION             _IOR('v', 1, int)
#define EXT2_IOC32_SETVERSION             _IOW('v', 2, int)

struct ifmap32 {
	unsigned int mem_start;
	unsigned int mem_end;
	unsigned short base_addr;
	unsigned char irq;
	unsigned char dma;
	unsigned char port;
};

struct ifreq32 {
#define IFHWADDRLEN     6
#define IFNAMSIZ        16
        union {
                char    ifrn_name[IFNAMSIZ];	/* if name, e.g. "en0" */
        } ifr_ifrn;
        union {
                struct  sockaddr ifru_addr;
                struct  sockaddr ifru_dstaddr;
                struct  sockaddr ifru_broadaddr;
                struct  sockaddr ifru_netmask;
                struct  sockaddr ifru_hwaddr;
                short   ifru_flags;
                int     ifru_ivalue;
                int     ifru_mtu;
                struct  ifmap32 ifru_map;
                char    ifru_slave[IFNAMSIZ];   /* Just fits the size */
		char	ifru_newname[IFNAMSIZ];
                __u32	ifru_data;
        } ifr_ifru;
};

struct ifconf32 {
        int     ifc_len;                        /* size of buffer       */
        __u32	ifcbuf;
};

static int dev_ifname32(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ireq32 *uir32 = (struct ireq32 *) A(arg);
	struct net_device *dev;
	struct ifreq32 ifr32;

	if (copy_from_user(&ifr32, uir32, sizeof(struct ifreq32)))
		return -EFAULT;

	read_lock(&dev_base_lock);
	dev = __dev_get_by_index(ifr32.ifr_ifindex);
	if (!dev) {
		read_unlock(&dev_base_lock);
		return -ENODEV;
	}

	strcpy(ifr32.ifr_name, dev->name);
	read_unlock(&dev_base_lock);

	if (copy_to_user(uir32, &ifr32, sizeof(struct ifreq32)))
	    return -EFAULT;

	return 0;
}

static inline int dev_ifconf(unsigned int fd, unsigned int cmd,
			     unsigned long arg)
{
	struct ioconf32 *uifc32 = (struct ioconf32 *) A(arg);
	struct ifconf32 ifc32;
	struct ifconf ifc;
	struct ifreq32 *ifr32;
	struct ifreq *ifr;
	mm_segment_t old_fs;
	int len;
	int err;

	if (copy_from_user(&ifc32, uifc32, sizeof(struct ifconf32)))
		return -EFAULT;

	if(ifc32.ifcbuf == 0) {
		ifc32.ifc_len = 0;
		ifc.ifc_len = 0;
		ifc.ifc_buf = NULL;
	} else {
		ifc.ifc_len = ((ifc32.ifc_len / sizeof (struct ifreq32))) *
			sizeof (struct ifreq);
		ifc.ifc_buf = kmalloc (ifc.ifc_len, GFP_KERNEL);
		if (!ifc.ifc_buf)
			return -ENOMEM;
	}
	ifr = ifc.ifc_req;
	ifr32 = (struct ifreq32 *) A(ifc32.ifcbuf);
	len = ifc32.ifc_len / sizeof (struct ifreq32);
	while (len--) {
		if (copy_from_user(ifr++, ifr32++, sizeof (struct ifreq32))) {
			err = -EFAULT;
			goto out;
		}
	}

	old_fs = get_fs();
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, SIOCGIFCONF, (unsigned long)&ifc);	
	set_fs (old_fs);
	if (err)
		goto out;

	ifr = ifc.ifc_req;
	ifr32 = (struct ifreq32 *) A(ifc32.ifcbuf);
	len = ifc.ifc_len / sizeof (struct ifreq);
	ifc32.ifc_len = len * sizeof (struct ifreq32);

	while (len--) {
		if (copy_to_user(ifr32++, ifr++, sizeof (struct ifreq32))) {
			err = -EFAULT;
			goto out;
		}
	}

	if (copy_to_user(uifc32, &ifc32, sizeof(struct ifconf32))) {
		err = -EFAULT;
		goto out;
	}
out:
	if(ifc.ifc_buf != NULL)
		kfree (ifc.ifc_buf);
	return err;
}

static inline int dev_ifsioc(unsigned int fd, unsigned int cmd,
			     unsigned long arg)
{
	struct ifreq32 *uifr = (struct ifreq32 *) A(arg);
	struct ifreq ifr;
	mm_segment_t old_fs;
	int err;
	
	switch (cmd) {
	case SIOCSIFMAP:
		err = copy_from_user(&ifr, uifr, sizeof(ifr.ifr_name));
		err |= __get_user(ifr.ifr_map.mem_start, &(uifr->ifr_ifru.ifru_map.mem_start));
		err |= __get_user(ifr.ifr_map.mem_end, &(uifr->ifr_ifru.ifru_map.mem_end));
		err |= __get_user(ifr.ifr_map.base_addr, &(uifr->ifr_ifru.ifru_map.base_addr));
		err |= __get_user(ifr.ifr_map.irq, &(uifr->ifr_ifru.ifru_map.irq));
		err |= __get_user(ifr.ifr_map.dma, &(uifr->ifr_ifru.ifru_map.dma));
		err |= __get_user(ifr.ifr_map.port, &(uifr->ifr_ifru.ifru_map.port));
		if (err)
			return -EFAULT;
		break;
	default:
		if (copy_from_user(&ifr, uifr, sizeof(struct ifreq32)))
			return -EFAULT;
		break;
	}
	old_fs = get_fs();
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, cmd, (unsigned long)&ifr);
	set_fs (old_fs);
	if (!err) {
		switch (cmd) {
		case SIOCGIFFLAGS:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFMEM:
		case SIOCGIFHWADDR:
		case SIOCGIFINDEX:
		case SIOCGIFADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFDSTADDR:
		case SIOCGIFNETMASK:
		case SIOCGIFTXQLEN:
			if (copy_to_user(uifr, &ifr, sizeof(struct ifreq32)))
				return -EFAULT;
			break;
		case SIOCGIFMAP:
			err = copy_to_user(uifr, &ifr, sizeof(ifr.ifr_name));
			err |= __put_user(ifr.ifr_map.mem_start, &(uifr->ifr_ifru.ifru_map.mem_start));
			err |= __put_user(ifr.ifr_map.mem_end, &(uifr->ifr_ifru.ifru_map.mem_end));
			err |= __put_user(ifr.ifr_map.base_addr, &(uifr->ifr_ifru.ifru_map.base_addr));
			err |= __put_user(ifr.ifr_map.irq, &(uifr->ifr_ifru.ifru_map.irq));
			err |= __put_user(ifr.ifr_map.dma, &(uifr->ifr_ifru.ifru_map.dma));
			err |= __put_user(ifr.ifr_map.port, &(uifr->ifr_ifru.ifru_map.port));
			if (err)
				err = -EFAULT;
			break;
		}
	}
	return err;
}

struct rtentry32
{
	unsigned int	rt_pad1;
	struct sockaddr	rt_dst;		/* target address		*/
	struct sockaddr	rt_gateway;	/* gateway addr (RTF_GATEWAY)	*/
	struct sockaddr	rt_genmask;	/* target network mask (IP)	*/
	unsigned short	rt_flags;
	short		rt_pad2;
	unsigned int	rt_pad3;
	unsigned int	rt_pad4;
	short		rt_metric;	/* +1 for binary compatibility!	*/
	unsigned int	rt_dev;		/* forcing the device at add	*/
	unsigned int	rt_mtu;		/* per route MTU/Window 	*/
#ifndef __KERNEL__
#define rt_mss	rt_mtu			/* Compatibility :-(            */
#endif
	unsigned int	rt_window;	/* Window clamping 		*/
	unsigned short	rt_irtt;	/* Initial RTT			*/
};

static inline int routing_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct rtentry32 *ur = (struct rtentry32 *) A(arg);
	struct rtentry r;
	char devname[16];
	u32 rtdev;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	ret = copy_from_user (&r.rt_dst, &(ur->rt_dst), 3 * sizeof(struct sockaddr));
	ret |= __get_user (r.rt_flags, &(ur->rt_flags));
	ret |= __get_user (r.rt_metric, &(ur->rt_metric));
	ret |= __get_user (r.rt_mtu, &(ur->rt_mtu));
	ret |= __get_user (r.rt_window, &(ur->rt_window));
	ret |= __get_user (r.rt_irtt, &(ur->rt_irtt));
	ret |= __get_user (rtdev, &(ur->rt_dev));
	if (rtdev) {
		ret |= copy_from_user (devname, (char *) A(rtdev), 15);
		r.rt_dev = devname; devname[15] = 0;
	} else
		r.rt_dev = 0;
	if (ret)
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, cmd, (long)&r);
	set_fs (old_fs);
	return ret;
}

static int do_ext2_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case EXT2_IOC32_GETFLAGS: cmd = EXT2_IOC_GETFLAGS; break;
	case EXT2_IOC32_SETFLAGS: cmd = EXT2_IOC_SETFLAGS; break;
	case EXT2_IOC32_GETVERSION: cmd = EXT2_IOC_GETVERSION; break;
	case EXT2_IOC32_SETVERSION: cmd = EXT2_IOC_SETVERSION; break;
	}
	return sys_ioctl(fd, cmd, arg);
}

static int w_long(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	int err;
	unsigned long val;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&val);
	set_fs (old_fs);
	if (!err && put_user((unsigned int) val, (u32 *)arg))
		return -EFAULT;
	return err;
}

struct ioctl32_handler {
	unsigned int cmd;
	int (*function)(unsigned int, unsigned int, unsigned long);
};

struct ioctl32_list {
	struct ioctl32_handler handler;
	struct ioctl32_list *next;
};

#define IOCTL32_DEFAULT(cmd)		{ { cmd, (void *) sys_ioctl }, 0 }
#define IOCTL32_HANDLER(cmd, handler)	{ { cmd, (void *) handler }, 0 }

static struct ioctl32_list ioctl32_handler_table[] = {
	IOCTL32_DEFAULT(FIBMAP),
	IOCTL32_DEFAULT(FIGETBSZ),

	IOCTL32_DEFAULT(DASDAPIVER),
	IOCTL32_DEFAULT(BIODASDDISABLE),
	IOCTL32_DEFAULT(BIODASDENABLE),
	IOCTL32_DEFAULT(BIODASDRSRV),
	IOCTL32_DEFAULT(BIODASDRLSE),
	IOCTL32_DEFAULT(BIODASDSLCK),
	IOCTL32_DEFAULT(BIODASDINFO),
	IOCTL32_DEFAULT(BIODASDFMT),

	IOCTL32_DEFAULT(BLKRRPART),

	IOCTL32_HANDLER(HDIO_GETGEO, hd_geometry_ioctl),

	IOCTL32_DEFAULT(TCGETA),
	IOCTL32_DEFAULT(TCSETA),
	IOCTL32_DEFAULT(TCSETAW),
	IOCTL32_DEFAULT(TCSETAF),
	IOCTL32_DEFAULT(TCSBRK),
	IOCTL32_DEFAULT(TCXONC),
	IOCTL32_DEFAULT(TCFLSH),
	IOCTL32_DEFAULT(TCGETS),
	IOCTL32_DEFAULT(TCSETS),
	IOCTL32_DEFAULT(TCSETSW),
	IOCTL32_DEFAULT(TCSETSF),
	IOCTL32_DEFAULT(TIOCLINUX),

	IOCTL32_DEFAULT(TIOCGETD),
	IOCTL32_DEFAULT(TIOCSETD),
	IOCTL32_DEFAULT(TIOCEXCL),
	IOCTL32_DEFAULT(TIOCNXCL),
	IOCTL32_DEFAULT(TIOCCONS),
	IOCTL32_DEFAULT(TIOCGSOFTCAR),
	IOCTL32_DEFAULT(TIOCSSOFTCAR),
	IOCTL32_DEFAULT(TIOCSWINSZ),
	IOCTL32_DEFAULT(TIOCGWINSZ),
	IOCTL32_DEFAULT(TIOCMGET),
	IOCTL32_DEFAULT(TIOCMBIC),
	IOCTL32_DEFAULT(TIOCMBIS),
	IOCTL32_DEFAULT(TIOCMSET),
	IOCTL32_DEFAULT(TIOCPKT),
	IOCTL32_DEFAULT(TIOCNOTTY),
	IOCTL32_DEFAULT(TIOCSTI),
	IOCTL32_DEFAULT(TIOCOUTQ),
	IOCTL32_DEFAULT(TIOCSPGRP),
	IOCTL32_DEFAULT(TIOCGPGRP),
	IOCTL32_DEFAULT(TIOCSCTTY),
	IOCTL32_DEFAULT(TIOCGPTN),
	IOCTL32_DEFAULT(TIOCSPTLCK),
	IOCTL32_DEFAULT(TIOCGSERIAL),
	IOCTL32_DEFAULT(TIOCSSERIAL),
	IOCTL32_DEFAULT(TIOCSERGETLSR),

	IOCTL32_DEFAULT(FIOCLEX),
	IOCTL32_DEFAULT(FIONCLEX),
	IOCTL32_DEFAULT(FIOASYNC),
	IOCTL32_DEFAULT(FIONBIO),
	IOCTL32_DEFAULT(FIONREAD),

	IOCTL32_DEFAULT(PIO_FONT),
	IOCTL32_DEFAULT(GIO_FONT),
	IOCTL32_DEFAULT(KDSIGACCEPT),
	IOCTL32_DEFAULT(KDGETKEYCODE),
	IOCTL32_DEFAULT(KDSETKEYCODE),
	IOCTL32_DEFAULT(KIOCSOUND),
	IOCTL32_DEFAULT(KDMKTONE),
	IOCTL32_DEFAULT(KDGKBTYPE),
	IOCTL32_DEFAULT(KDSETMODE),
	IOCTL32_DEFAULT(KDGETMODE),
	IOCTL32_DEFAULT(KDSKBMODE),
	IOCTL32_DEFAULT(KDGKBMODE),
	IOCTL32_DEFAULT(KDSKBMETA),
	IOCTL32_DEFAULT(KDGKBMETA),
	IOCTL32_DEFAULT(KDGKBENT),
	IOCTL32_DEFAULT(KDSKBENT),
	IOCTL32_DEFAULT(KDGKBSENT),
	IOCTL32_DEFAULT(KDSKBSENT),
	IOCTL32_DEFAULT(KDGKBDIACR),
	IOCTL32_DEFAULT(KDSKBDIACR),
	IOCTL32_DEFAULT(KDGKBLED),
	IOCTL32_DEFAULT(KDSKBLED),
	IOCTL32_DEFAULT(KDGETLED),
	IOCTL32_DEFAULT(KDSETLED),
	IOCTL32_DEFAULT(GIO_SCRNMAP),
	IOCTL32_DEFAULT(PIO_SCRNMAP),
	IOCTL32_DEFAULT(GIO_UNISCRNMAP),
	IOCTL32_DEFAULT(PIO_UNISCRNMAP),
	IOCTL32_DEFAULT(PIO_FONTRESET),
	IOCTL32_DEFAULT(PIO_UNIMAPCLR),

	IOCTL32_DEFAULT(VT_SETMODE),
	IOCTL32_DEFAULT(VT_GETMODE),
	IOCTL32_DEFAULT(VT_GETSTATE),
	IOCTL32_DEFAULT(VT_OPENQRY),
	IOCTL32_DEFAULT(VT_ACTIVATE),
	IOCTL32_DEFAULT(VT_WAITACTIVE),
	IOCTL32_DEFAULT(VT_RELDISP),
	IOCTL32_DEFAULT(VT_DISALLOCATE),
	IOCTL32_DEFAULT(VT_RESIZE),
	IOCTL32_DEFAULT(VT_RESIZEX),
	IOCTL32_DEFAULT(VT_LOCKSWITCH),
	IOCTL32_DEFAULT(VT_UNLOCKSWITCH),

	IOCTL32_DEFAULT(SIOCGSTAMP),

	IOCTL32_HANDLER(SIOCGIFNAME, dev_ifname32),
	IOCTL32_HANDLER(SIOCGIFCONF, dev_ifconf),
	IOCTL32_HANDLER(SIOCGIFFLAGS, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFFLAGS, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFMETRIC, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFMETRIC, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFMTU, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFMTU, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFMEM, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFMEM, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFHWADDR, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFHWADDR, dev_ifsioc),
	IOCTL32_HANDLER(SIOCADDMULTI, dev_ifsioc),
	IOCTL32_HANDLER(SIOCDELMULTI, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFINDEX, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFMAP, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFMAP, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFADDR, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFADDR, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFBRDADDR, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFBRDADDR, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFDSTADDR, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFDSTADDR, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFNETMASK, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFNETMASK, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFPFLAGS, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFPFLAGS, dev_ifsioc),
	IOCTL32_HANDLER(SIOCGIFTXQLEN, dev_ifsioc),
	IOCTL32_HANDLER(SIOCSIFTXQLEN, dev_ifsioc),
	IOCTL32_HANDLER(SIOCADDRT, routing_ioctl),
	IOCTL32_HANDLER(SIOCDELRT, routing_ioctl),

	IOCTL32_HANDLER(EXT2_IOC32_GETFLAGS, do_ext2_ioctl),
	IOCTL32_HANDLER(EXT2_IOC32_SETFLAGS, do_ext2_ioctl),
	IOCTL32_HANDLER(EXT2_IOC32_GETVERSION, do_ext2_ioctl),
	IOCTL32_HANDLER(EXT2_IOC32_SETVERSION, do_ext2_ioctl),

	IOCTL32_HANDLER(BLKGETSIZE, w_long)

};

#define NR_IOCTL32_HANDLERS	(sizeof(ioctl32_handler_table) /	\
				 sizeof(ioctl32_handler_table[0]))

static struct ioctl32_list *ioctl32_hash_table[1024];

static inline int ioctl32_hash(unsigned int cmd)
{
	return ((cmd >> 6) ^ (cmd >> 4) ^ cmd) & 0x3ff;
}

int sys32_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	int (*handler)(unsigned int, unsigned int, unsigned long, struct file * filp);
	struct file *filp;
	struct ioctl32_list *l;
	int error;

	l = ioctl32_hash_table[ioctl32_hash(cmd)];

	error = -EBADF;

	filp = fget(fd);
	if (!filp)
		return error;

	if (!filp->f_op || !filp->f_op->ioctl) {
		error = sys_ioctl (fd, cmd, arg);
		goto out;
	}

	while (l && l->handler.cmd != cmd)
		l = l->next;

	if (l) {
		handler = (void *)l->handler.function;
		error = handler(fd, cmd, arg, filp);
	} else {
		error = -EINVAL;
		printk("unknown ioctl: %08x\n", cmd);
	}
out:
	fput(filp);
	return error;
}

static void ioctl32_insert(struct ioctl32_list *entry)
{
	int hash = ioctl32_hash(entry->handler.cmd);
	if (!ioctl32_hash_table[hash])
		ioctl32_hash_table[hash] = entry;
	else {
		struct ioctl32_list *l;
		l = ioctl32_hash_table[hash];
		while (l->next)
			l = l->next;
		l->next = entry;
		entry->next = 0;
	}
}

static int __init init_ioctl32(void)
{
	int i;
	for (i = 0; i < NR_IOCTL32_HANDLERS; i++)
		ioctl32_insert(&ioctl32_handler_table[i]);
	return 0;
}

__initcall(init_ioctl32);
