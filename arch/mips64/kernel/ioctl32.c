/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Written by Ulf Carlsson (ulfc@engr.sgi.com)
 * Copyright (C) 2000 Ralf Baechle
 *
 * Mostly stolen from the sparc64 ioctl32 implementation.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mtio.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/netdevice.h>
#include <linux/route.h>
#include <linux/hdreg.h>
#include <linux/blkpg.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/auto_fs.h>
#include <linux/ext2_fs.h>
#include <asm/types.h>
#include <asm/uaccess.h>

long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

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

static int rw_long(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	int err;
	unsigned long val;

	if (get_user(val, (u32 *)arg))
		return -EFAULT;
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&val);
	set_fs (old_fs);
	if (!err && put_user(val, (u32 *)arg))
		return -EFAULT;
	return err;
}

#define A(__x) ((unsigned long)(__x))

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
                __kernel_caddr_t32 ifru_data;
        } ifr_ifru;
};

struct ifconf32 {
        int     ifc_len;                        /* size of buffer       */
        __kernel_caddr_t32  ifcbuf;
};

static int dev_ifname32(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ireq32 *uir32 = (struct ireq32 *)arg;
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
	struct ioconf32 *uifc32 = (struct ioconf32 *)arg;
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
	ifr32 = (struct ifreq32 *)A(ifc32.ifcbuf);
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
	ifr32 = (struct ifreq32 *)A(ifc32.ifcbuf);
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
	struct ifreq32 *uifr = (struct ifreq32 *)arg;
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
	struct rtentry32 *ur = (struct rtentry32 *)arg;
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
		ret |= copy_from_user (devname, (char *)A(rtdev), 15);
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

struct hd_geometry32 {
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	u32 start;
};

static int hdio_getgeo(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct hd_geometry geo;
	int err;

	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, HDIO_GETGEO, (unsigned long)&geo);
	set_fs (old_fs);
	if (!err) {
		err = copy_to_user ((struct hd_geometry32 *)arg, &geo, 4);
		err |= __put_user (geo.start, &(((struct hd_geometry32 *)arg)->start));
	}

	return err ? -EFAULT : 0;
}

static int hdio_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	unsigned long kval;
	unsigned int *uvp;
	int error;

	set_fs(KERNEL_DS);
	error = sys_ioctl(fd, cmd, (long)&kval);
	set_fs(old_fs);

	if (error == 0) {
		uvp = (unsigned int *)arg;
		if (put_user(kval, uvp))
			error = -EFAULT;
	}

	return error;
}

struct blkpg_ioctl_arg32 {
	int op;
	int flags;
	int datalen;
	u32 data;
};

static int blkpg_ioctl_trans(unsigned int fd, unsigned int cmd,
                             struct blkpg_ioctl_arg32 *arg)
{
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;
	int err;
	mm_segment_t old_fs = get_fs();
	
	err = get_user(a.op, &arg->op);
	err |= __get_user(a.flags, &arg->flags);
	err |= __get_user(a.datalen, &arg->datalen);
	err |= __get_user((long)a.data, &arg->data);
	if (err) return err;
	switch (a.op) {
	case BLKPG_ADD_PARTITION:
	case BLKPG_DEL_PARTITION:
		if (a.datalen < sizeof(struct blkpg_partition))
			return -EINVAL;
                if (copy_from_user(&p, a.data, sizeof(struct blkpg_partition)))
			return -EFAULT;
		a.data = &p;
		set_fs (KERNEL_DS);
		err = sys_ioctl(fd, cmd, (unsigned long)&a);
		set_fs (old_fs);
	default:
		return -EINVAL;
	}                                        
	return err;
}

struct mtget32 {
	__u32	mt_type;
	__u32	mt_resid;
	__u32	mt_dsreg;
	__u32	mt_gstat;
	__u32	mt_erreg;
	__kernel_daddr_t32	mt_fileno;
	__kernel_daddr_t32	mt_blkno;
};
#define MTIOCGET32	_IOR('m', 2, struct mtget32)

struct mtpos32 {
	__u32	mt_blkno;
};
#define MTIOCPOS32	_IOR('m', 3, struct mtpos32)

struct mtconfiginfo32 {
	__u32	mt_type;
	__u32	ifc_type;
	__u16	irqnr;
	__u16	dmanr;
	__u16	port;
	__u32	debug;
	__u32	have_dens:1;
	__u32	have_bsf:1;
	__u32	have_fsr:1;
	__u32	have_bsr:1;
	__u32	have_eod:1;
	__u32	have_seek:1;
	__u32	have_tell:1;
	__u32	have_ras1:1;
	__u32	have_ras2:1;
	__u32	have_ras3:1;
	__u32	have_qfa:1;
	__u32	pad1:5;
	char	reserved[10];
};
#define	MTIOCGETCONFIG32	_IOR('m', 4, struct mtconfiginfo32)
#define	MTIOCSETCONFIG32	_IOW('m', 5, struct mtconfiginfo32)

static int mt_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct mtconfiginfo info;
	struct mtget get;
	struct mtpos pos;
	unsigned long kcmd;
	void *karg;
	int err = 0;

	switch(cmd) {
	case MTIOCPOS32:
		kcmd = MTIOCPOS;
		karg = &pos;
		break;
	case MTIOCGET32:
		kcmd = MTIOCGET;
		karg = &get;
		break;
	case MTIOCGETCONFIG32:
		kcmd = MTIOCGETCONFIG;
		karg = &info;
		break;
	case MTIOCSETCONFIG32:
		kcmd = MTIOCSETCONFIG;
		karg = &info;
		err = __get_user(info.mt_type, &((struct mtconfiginfo32 *)arg)->mt_type);
		err |= __get_user(info.ifc_type, &((struct mtconfiginfo32 *)arg)->ifc_type);
		err |= __get_user(info.irqnr, &((struct mtconfiginfo32 *)arg)->irqnr);
		err |= __get_user(info.dmanr, &((struct mtconfiginfo32 *)arg)->dmanr);
		err |= __get_user(info.port, &((struct mtconfiginfo32 *)arg)->port);
		err |= __get_user(info.debug, &((struct mtconfiginfo32 *)arg)->debug);
		err |= __copy_from_user((char *)&info.debug + sizeof(info.debug),
				     (char *)&((struct mtconfiginfo32 *)arg)->debug
				     + sizeof(((struct mtconfiginfo32 *)arg)->debug), sizeof(__u32));
		if (err)
			return -EFAULT;
		break;
	default:
		do {
			static int count = 0;
			if (++count <= 20)
				printk("mt_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		return -EINVAL;
	}
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, kcmd, (unsigned long)karg);
	set_fs (old_fs);
	if (err)
		return err;
	switch (cmd) {
	case MTIOCPOS32:
		err = __put_user(pos.mt_blkno, &((struct mtpos32 *)arg)->mt_blkno);
		break;
	case MTIOCGET32:
		err = __put_user(get.mt_type, &((struct mtget32 *)arg)->mt_type);
		err |= __put_user(get.mt_resid, &((struct mtget32 *)arg)->mt_resid);
		err |= __put_user(get.mt_dsreg, &((struct mtget32 *)arg)->mt_dsreg);
		err |= __put_user(get.mt_gstat, &((struct mtget32 *)arg)->mt_gstat);
		err |= __put_user(get.mt_erreg, &((struct mtget32 *)arg)->mt_erreg);
		err |= __put_user(get.mt_fileno, &((struct mtget32 *)arg)->mt_fileno);
		err |= __put_user(get.mt_blkno, &((struct mtget32 *)arg)->mt_blkno);
		break;
	case MTIOCGETCONFIG32:
		err = __put_user(info.mt_type, &((struct mtconfiginfo32 *)arg)->mt_type);
		err |= __put_user(info.ifc_type, &((struct mtconfiginfo32 *)arg)->ifc_type);
		err |= __put_user(info.irqnr, &((struct mtconfiginfo32 *)arg)->irqnr);
		err |= __put_user(info.dmanr, &((struct mtconfiginfo32 *)arg)->dmanr);
		err |= __put_user(info.port, &((struct mtconfiginfo32 *)arg)->port);
		err |= __put_user(info.debug, &((struct mtconfiginfo32 *)arg)->debug);
		err |= __copy_to_user((char *)&((struct mtconfiginfo32 *)arg)->debug
			    		   + sizeof(((struct mtconfiginfo32 *)arg)->debug),
					   (char *)&info.debug + sizeof(info.debug), sizeof(__u32));
		break;
	case MTIOCSETCONFIG32:
		break;
	}
	return err ? -EFAULT: 0;
}

#define AUTOFS_IOC_SETTIMEOUT32 _IOWR(0x93,0x64,unsigned int)

static int ioc_settimeout(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return rw_long(fd, AUTOFS_IOC_SETTIMEOUT, arg);
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

	IOCTL32_HANDLER(HDIO_GETGEO, hdio_getgeo),	/* hdreg.h ioctls  */
	IOCTL32_HANDLER(HDIO_GET_UNMASKINTR, hdio_ioctl_trans),
	IOCTL32_HANDLER(HDIO_GET_MULTCOUNT, hdio_ioctl_trans),
	// HDIO_OBSOLETE_IDENTITY
	IOCTL32_HANDLER(HDIO_GET_KEEPSETTINGS, hdio_ioctl_trans),
	IOCTL32_HANDLER(HDIO_GET_32BIT, hdio_ioctl_trans),
	IOCTL32_HANDLER(HDIO_GET_NOWERR, hdio_ioctl_trans),
	IOCTL32_HANDLER(HDIO_GET_DMA, hdio_ioctl_trans),
	IOCTL32_HANDLER(HDIO_GET_NICE, hdio_ioctl_trans),
	IOCTL32_DEFAULT(HDIO_GET_IDENTITY),
	IOCTL32_DEFAULT(HDIO_DRIVE_RESET),
	// HDIO_TRISTATE_HWIF				/* not implemented */
	// HDIO_DRIVE_TASK				/* To do, need specs */
	IOCTL32_DEFAULT(HDIO_DRIVE_CMD),
	IOCTL32_DEFAULT(HDIO_SET_MULTCOUNT),
	IOCTL32_DEFAULT(HDIO_SET_UNMASKINTR),
	IOCTL32_DEFAULT(HDIO_SET_KEEPSETTINGS),
	IOCTL32_DEFAULT(HDIO_SET_32BIT),
	IOCTL32_DEFAULT(HDIO_SET_NOWERR),
	IOCTL32_DEFAULT(HDIO_SET_DMA),
	IOCTL32_DEFAULT(HDIO_SET_PIO_MODE),
	IOCTL32_DEFAULT(HDIO_SCAN_HWIF),
	IOCTL32_DEFAULT(HDIO_SET_NICE),
	//HDIO_UNREGISTER_HWIF

	IOCTL32_DEFAULT(BLKROSET),			/* fs.h ioctls  */
	IOCTL32_DEFAULT(BLKROGET),
	IOCTL32_DEFAULT(BLKRRPART),
	IOCTL32_HANDLER(BLKGETSIZE, w_long),

	IOCTL32_DEFAULT(BLKFLSBUF),
	IOCTL32_DEFAULT(BLKRASET),
	IOCTL32_HANDLER(BLKRAGET, w_long),
	IOCTL32_DEFAULT(BLKFRASET),
	IOCTL32_HANDLER(BLKFRAGET, w_long),
	IOCTL32_DEFAULT(BLKSECTSET),
	IOCTL32_HANDLER(BLKSECTGET, w_long),
	IOCTL32_DEFAULT(BLKSSZGET),
	IOCTL32_HANDLER(BLKPG, blkpg_ioctl_trans),
	IOCTL32_DEFAULT(BLKELVGET),
	IOCTL32_DEFAULT(BLKELVSET),

	IOCTL32_DEFAULT(MTIOCTOP),			/* mtio.h ioctls  */
	IOCTL32_HANDLER(MTIOCGET32, mt_ioctl_trans),
	IOCTL32_HANDLER(MTIOCPOS32, mt_ioctl_trans),
	IOCTL32_HANDLER(MTIOCGETCONFIG32, mt_ioctl_trans),
	IOCTL32_HANDLER(MTIOCSETCONFIG32, mt_ioctl_trans),
	// MTIOCRDFTSEG
	// MTIOCWRFTSEG
	// MTIOCVOLINFO
	// MTIOCGETSIZE
	// MTIOCFTFORMAT
	// MTIOCFTCMD

	IOCTL32_DEFAULT(AUTOFS_IOC_READY),		/* auto_fs.h ioctls */
	IOCTL32_DEFAULT(AUTOFS_IOC_FAIL),
	IOCTL32_DEFAULT(AUTOFS_IOC_CATATONIC),
	IOCTL32_DEFAULT(AUTOFS_IOC_PROTOVER),
	IOCTL32_HANDLER(AUTOFS_IOC_SETTIMEOUT32, ioc_settimeout),
	IOCTL32_DEFAULT(AUTOFS_IOC_EXPIRE)
};

#define NR_IOCTL32_HANDLERS	(sizeof(ioctl32_handler_table) /	\
				 sizeof(ioctl32_handler_table[0]))

static struct ioctl32_list *ioctl32_hash_table[1024];

static inline int ioctl32_hash(unsigned int cmd)
{
	return ((cmd >> 6) ^ (cmd >> 4) ^ cmd) & 0x3ff;
}

int sys32_ioctl(unsigned int fd, unsigned int cmd, unsigned int arg)
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
