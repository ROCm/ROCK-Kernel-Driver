/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Written by Ulf Carlsson (ulfc@engr.sgi.com)
 * Copyright (C) 2000 Ralf Baechle
 * Copyright (C) 2002  Maciej W. Rozycki
 *
 * Mostly stolen from the sparc64 ioctl32 implementation.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/compat.h>
#include <linux/ioctl32.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/if.h>
#include <linux/mm.h>
#include <linux/mtio.h>
#include <linux/auto_fs.h>
#include <linux/auto_fs4.h>
#include <linux/devfs_fs.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fd.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/if_pppox.h>
#include <linux/if_tun.h>
#include <linux/cdrom.h>
#include <linux/blkdev.h>
#include <linux/loop.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/ext2_fs.h>
#include <linux/videodev.h>
#include <linux/netdevice.h>
#include <linux/raw.h>
#include <linux/smb_fs.h>
#include <linux/ncp_fs.h>
#include <linux/route.h>
#include <linux/hdreg.h>
#include <linux/raid/md.h>
#include <linux/blkpg.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/rtc.h>
#include <linux/pci.h>
#include <linux/dm-ioctl.h>

#include <scsi/scsi.h>
#undef __KERNEL__		/* This file was born to be ugly ...  */
#include <scsi/scsi_ioctl.h>
#define __KERNEL__
#include <scsi/sg.h>

#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_bonding.h>
#include <linux/watchdog.h>

#include <asm/ioctls.h>
#include <asm/module.h>
#include <linux/soundcard.h>
#include <linux/lp.h>

#include <linux/atm.h>
#include <linux/atmarp.h>
#include <linux/atmclip.h>
#include <linux/atmdev.h>
#include <linux/atmioc.h>
#include <linux/atmlec.h>
#include <linux/atmmpc.h>
#include <linux/atmsvc.h>
#include <linux/atm_tcp.h>
#include <linux/sonet.h>
#include <linux/atm_suni.h>
#include <linux/mtd/mtd.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <net/bluetooth/rfcomm.h>

#include <linux/usb.h>
#include <linux/usbdevice_fs.h>
#include <linux/nbd.h>
#include <linux/random.h>
#include <linux/filter.h>

#ifdef CONFIG_SIBYTE_TBPROF
#include <asm/sibyte/trace_prof.h>
#endif

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


#ifdef CONFIG_FB

struct fb_fix_screeninfo32 {
	char id[16];			/* identification string eg "TT Builtin" */
	__u32 smem_start;		/* Start of frame buffer mem */
					/* (physical address) */
	__u32 smem_len;			/* Length of frame buffer mem */
	__u32 type;			/* see FB_TYPE_*		*/
	__u32 type_aux;			/* Interleave for interleaved Planes */
	__u32 visual;			/* see FB_VISUAL_*		*/ 
	__u16 xpanstep;			/* zero if no hardware panning  */
	__u16 ypanstep;			/* zero if no hardware panning  */
	__u16 ywrapstep;		/* zero if no hardware ywrap    */
	__u32 line_length;		/* length of a line in bytes    */
	__u32 mmio_start;		/* Start of Memory Mapped I/O   */
					/* (physical address) */
	__u32 mmio_len;			/* Length of Memory Mapped I/O  */
	__u32 accel;			/* Type of acceleration available */
	__u16 reserved[3];		/* Reserved for future compatibility */
};

static int do_fbioget_fscreeninfo_ioctl(unsigned int fd, unsigned int cmd,
					unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct fb_fix_screeninfo fix;
	struct fb_fix_screeninfo32 *fix32 = (struct fb_fix_screeninfo32 *)arg;
	int err;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&fix);
	set_fs(old_fs);

	if (err == 0) {
		err = __copy_to_user((char *)fix32->id, (char *)fix.id,
				     sizeof(fix.id));
		err |= __put_user((__u32)(unsigned long)fix.smem_start,
				  &fix32->smem_start);
		err |= __put_user(fix.smem_len, &fix32->smem_len);
		err |= __put_user(fix.type, &fix32->type);
		err |= __put_user(fix.type_aux, &fix32->type_aux);
		err |= __put_user(fix.visual, &fix32->visual);
		err |= __put_user(fix.xpanstep, &fix32->xpanstep);
		err |= __put_user(fix.ypanstep, &fix32->ypanstep);
		err |= __put_user(fix.ywrapstep, &fix32->ywrapstep);
		err |= __put_user(fix.line_length, &fix32->line_length);
		err |= __put_user((__u32)(unsigned long)fix.mmio_start,
				  &fix32->mmio_start);
		err |= __put_user(fix.mmio_len, &fix32->mmio_len);
		err |= __put_user(fix.accel, &fix32->accel);
		err |= __copy_to_user((char *)fix32->reserved,
				      (char *)fix.reserved,
				      sizeof(fix.reserved));
		if (err)
			err = -EFAULT;
	}

	return err;
}

struct fb_cmap32 {
	__u32 start;			/* First entry  */
	__u32 len;			/* Number of entries */
	__u32 red;			/* Red values   */
	__u32 green;
	__u32 blue;
	__u32 transp;			/* transparency, can be NULL */
};

static int do_fbiocmap_ioctl(unsigned int fd, unsigned int cmd,
			     unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	u32 red = 0, green = 0, blue = 0, transp = 0;
	struct fb_cmap cmap;
	struct fb_cmap32 *cmap32 = (struct fb_cmap32 *)arg;
	int err;

	memset(&cmap, 0, sizeof(cmap));

	err = __get_user(cmap.start, &cmap32->start);
	err |= __get_user(cmap.len, &cmap32->len);
	err |= __get_user(red, &cmap32->red);
	err |= __get_user(green, &cmap32->green);
	err |= __get_user(blue, &cmap32->blue);
	err |= __get_user(transp, &cmap32->transp);
	if (err)
		return -EFAULT;

	err = -ENOMEM;
	cmap.red = kmalloc(cmap.len * sizeof(__u16), GFP_KERNEL);
	if (!cmap.red)
		goto out;
	cmap.green = kmalloc(cmap.len * sizeof(__u16), GFP_KERNEL);
	if (!cmap.green)
		goto out;
	cmap.blue = kmalloc(cmap.len * sizeof(__u16), GFP_KERNEL);
	if (!cmap.blue)
		goto out;
	if (transp) {
		cmap.transp = kmalloc(cmap.len * sizeof(__u16), GFP_KERNEL);
		if (!cmap.transp)
			goto out;
	}
			
	if (cmd == FBIOPUTCMAP) {
		err = __copy_from_user(cmap.red, (char *)A(red),
				       cmap.len * sizeof(__u16));
		err |= __copy_from_user(cmap.green, (char *)A(green),
					cmap.len * sizeof(__u16));
		err |= __copy_from_user(cmap.blue, (char *)A(blue),
					cmap.len * sizeof(__u16));
		if (cmap.transp)
			err |= __copy_from_user(cmap.transp, (char *)A(transp),
						cmap.len * sizeof(__u16));
		if (err) {
			err = -EFAULT;
			goto out;
		}
	}

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&cmap);
	set_fs(old_fs);
	if (err)
		goto out;

	if (cmd == FBIOGETCMAP) {
		err = __copy_to_user((char *)A(red), cmap.red,
				     cmap.len * sizeof(__u16));
		err |= __copy_to_user((char *)A(green), cmap.blue,
				      cmap.len * sizeof(__u16));
		err |= __copy_to_user((char *)A(blue), cmap.blue,
				      cmap.len * sizeof(__u16));
		if (cmap.transp)
			err |= __copy_to_user((char *)A(transp), cmap.transp,
					      cmap.len * sizeof(__u16));
		if (err) {
			err = -EFAULT;
			goto out;
		}
	}

out:
	if (cmap.red)
		kfree(cmap.red);
	if (cmap.green)
		kfree(cmap.green);
	if (cmap.blue)
		kfree(cmap.blue);
	if (cmap.transp)
		kfree(cmap.transp);

	return err;
}

#endif /* CONFIG_FB */


static int do_siocgstamp(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct compat_timeval *up = (struct compat_timeval *)arg;
	struct timeval ktv;
	mm_segment_t old_fs = get_fs();
	int err;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&ktv);
	set_fs(old_fs);
	if (!err) {
		err = put_user(ktv.tv_sec, &up->tv_sec);
		err |= __put_user(ktv.tv_usec, &up->tv_usec);
	}

	return err;
}

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
                compat_caddr_t ifru_data;
        } ifr_ifru;
};

struct ifconf32 {
        int     ifc_len;                        /* size of buffer       */
        compat_caddr_t  ifcbuf;
};

#ifdef CONFIG_NET

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

int siocdevprivate_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq *u_ifreq64;
	struct ifreq32 *u_ifreq32 = (struct ifreq32 *) arg;
	char tmp_buf[IFNAMSIZ];
	void *data64;
	u32 data32;

	if (copy_from_user(&tmp_buf[0], &(u_ifreq32->ifr_ifrn.ifrn_name[0]),
			   IFNAMSIZ))
		return -EFAULT;
	if (__get_user(data32, &u_ifreq32->ifr_ifru.ifru_data))
		return -EFAULT;
	data64 = (void *) A(data32);

	u_ifreq64 = compat_alloc_user_space(sizeof(*u_ifreq64));

	/* Don't check these user accesses, just let that get trapped
	 * in the ioctl handler instead.
	 */
	copy_to_user(&u_ifreq64->ifr_ifrn.ifrn_name[0], &tmp_buf[0], IFNAMSIZ);
	__put_user(data64, &u_ifreq64->ifr_ifru.ifru_data);

	return sys_ioctl(fd, cmd, (unsigned long) u_ifreq64);
}

static int dev_ifsioc(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq ifr;
	mm_segment_t old_fs;
	int err;
	
	switch (cmd) {
	case SIOCSIFMAP:
		err = copy_from_user(&ifr, (struct ifreq32 *)arg, sizeof(ifr.ifr_name));
		err |= __get_user(ifr.ifr_map.mem_start, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_start));
		err |= __get_user(ifr.ifr_map.mem_end, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_end));
		err |= __get_user(ifr.ifr_map.base_addr, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.base_addr));
		err |= __get_user(ifr.ifr_map.irq, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.irq));
		err |= __get_user(ifr.ifr_map.dma, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.dma));
		err |= __get_user(ifr.ifr_map.port, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.port));
		if (err)
			return -EFAULT;
		break;
	default:
		if (copy_from_user(&ifr, (struct ifreq32 *)arg, sizeof(struct ifreq32)))
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
			if (copy_to_user((struct ifreq32 *)arg, &ifr, sizeof(struct ifreq32)))
				return -EFAULT;
			break;
		case SIOCGIFMAP:
			err = copy_to_user((struct ifreq32 *)arg, &ifr, sizeof(ifr.ifr_name));
			err |= __put_user(ifr.ifr_map.mem_start, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_start));
			err |= __put_user(ifr.ifr_map.mem_end, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_end));
			err |= __put_user(ifr.ifr_map.base_addr, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.base_addr));
			err |= __put_user(ifr.ifr_map.irq, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.irq));
			err |= __put_user(ifr.ifr_map.dma, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.dma));
			err |= __put_user(ifr.ifr_map.port, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.port));
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

#endif /* CONFIG_NET */

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

static int ret_einval(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
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
	compat_daddr_t	mt_fileno;
	compat_daddr_t	mt_blkno;
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

typedef int (* ioctl32_handler_t)(unsigned int, unsigned int, unsigned long, struct file *);
                                                                                
#define COMPATIBLE_IOCTL(cmd)		HANDLE_IOCTL((cmd),sys_ioctl)
#define HANDLE_IOCTL(cmd,handler)	{ (cmd), (ioctl32_handler_t)(handler), NULL },
#define IOCTL_TABLE_START \
	struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END \
	};

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>
COMPATIBLE_IOCTL(TCGETA)
COMPATIBLE_IOCTL(TCSETA)
COMPATIBLE_IOCTL(TCSETAW)
COMPATIBLE_IOCTL(TCSETAF)
COMPATIBLE_IOCTL(TCSBRK)
COMPATIBLE_IOCTL(TCXONC)
COMPATIBLE_IOCTL(TCFLSH)
COMPATIBLE_IOCTL(TCGETS)
COMPATIBLE_IOCTL(TCSETS)
COMPATIBLE_IOCTL(TCSETSW)
COMPATIBLE_IOCTL(TCSETSF)
COMPATIBLE_IOCTL(TIOCLINUX)

COMPATIBLE_IOCTL(TIOCGETD)
COMPATIBLE_IOCTL(TIOCSETD)
COMPATIBLE_IOCTL(TIOCEXCL)
COMPATIBLE_IOCTL(TIOCNXCL)
COMPATIBLE_IOCTL(TIOCCONS)
COMPATIBLE_IOCTL(TIOCGSOFTCAR)
COMPATIBLE_IOCTL(TIOCSSOFTCAR)
COMPATIBLE_IOCTL(TIOCSWINSZ)
COMPATIBLE_IOCTL(TIOCGWINSZ)
COMPATIBLE_IOCTL(TIOCMGET)
COMPATIBLE_IOCTL(TIOCMBIC)
COMPATIBLE_IOCTL(TIOCMBIS)
COMPATIBLE_IOCTL(TIOCMSET)
COMPATIBLE_IOCTL(TIOCPKT)
COMPATIBLE_IOCTL(TIOCNOTTY)
COMPATIBLE_IOCTL(TIOCSTI)
COMPATIBLE_IOCTL(TIOCOUTQ)
COMPATIBLE_IOCTL(TIOCSPGRP)
COMPATIBLE_IOCTL(TIOCGPGRP)
COMPATIBLE_IOCTL(TIOCSCTTY)
COMPATIBLE_IOCTL(TIOCGPTN)
COMPATIBLE_IOCTL(TIOCSPTLCK)
COMPATIBLE_IOCTL(TIOCGSERIAL)
COMPATIBLE_IOCTL(TIOCSSERIAL)
COMPATIBLE_IOCTL(TIOCSERGETLSR)

COMPATIBLE_IOCTL(FIOCLEX)
COMPATIBLE_IOCTL(FIONCLEX)
COMPATIBLE_IOCTL(FIOASYNC)
COMPATIBLE_IOCTL(FIONBIO)
COMPATIBLE_IOCTL(FIONREAD)

#ifdef CONFIG_FB
/* Big F */
COMPATIBLE_IOCTL(FBIOGET_VSCREENINFO)
COMPATIBLE_IOCTL(FBIOPUT_VSCREENINFO)
HANDLE_IOCTL(FBIOGET_FSCREENINFO, do_fbioget_fscreeninfo_ioctl)
HANDLE_IOCTL(FBIOGETCMAP, do_fbiocmap_ioctl)
HANDLE_IOCTL(FBIOPUTCMAP, do_fbiocmap_ioctl)
COMPATIBLE_IOCTL(FBIOPAN_DISPLAY)
#endif /* CONFIG_FB */

/* Big K */
COMPATIBLE_IOCTL(PIO_FONT)
COMPATIBLE_IOCTL(GIO_FONT)
COMPATIBLE_IOCTL(KDSIGACCEPT)
COMPATIBLE_IOCTL(KDGETKEYCODE)
COMPATIBLE_IOCTL(KDSETKEYCODE)
COMPATIBLE_IOCTL(KIOCSOUND)
COMPATIBLE_IOCTL(KDMKTONE)
COMPATIBLE_IOCTL(KDGKBTYPE)
COMPATIBLE_IOCTL(KDSETMODE)
COMPATIBLE_IOCTL(KDGETMODE)
COMPATIBLE_IOCTL(KDSKBMODE)
COMPATIBLE_IOCTL(KDGKBMODE)
COMPATIBLE_IOCTL(KDSKBMETA)
COMPATIBLE_IOCTL(KDGKBMETA)
COMPATIBLE_IOCTL(KDGKBENT)
COMPATIBLE_IOCTL(KDSKBENT)
COMPATIBLE_IOCTL(KDGKBSENT)
COMPATIBLE_IOCTL(KDSKBSENT)
COMPATIBLE_IOCTL(KDGKBDIACR)
COMPATIBLE_IOCTL(KDSKBDIACR)
COMPATIBLE_IOCTL(KDKBDREP)
COMPATIBLE_IOCTL(KDGKBLED)
COMPATIBLE_IOCTL(KDSKBLED)
COMPATIBLE_IOCTL(KDGETLED)
COMPATIBLE_IOCTL(KDSETLED)
COMPATIBLE_IOCTL(GIO_SCRNMAP)
COMPATIBLE_IOCTL(PIO_SCRNMAP)
COMPATIBLE_IOCTL(GIO_UNISCRNMAP)
COMPATIBLE_IOCTL(PIO_UNISCRNMAP)
COMPATIBLE_IOCTL(PIO_FONTRESET)
COMPATIBLE_IOCTL(PIO_UNIMAPCLR)

/* Big S */
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_IDLUN)
COMPATIBLE_IOCTL(SCSI_IOCTL_DOORLOCK)
COMPATIBLE_IOCTL(SCSI_IOCTL_DOORUNLOCK)
COMPATIBLE_IOCTL(SCSI_IOCTL_TEST_UNIT_READY)
COMPATIBLE_IOCTL(SCSI_IOCTL_TAGGED_ENABLE)
COMPATIBLE_IOCTL(SCSI_IOCTL_TAGGED_DISABLE)
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_BUS_NUMBER)
COMPATIBLE_IOCTL(SCSI_IOCTL_SEND_COMMAND)

/* Big V */
COMPATIBLE_IOCTL(VT_SETMODE)
COMPATIBLE_IOCTL(VT_GETMODE)
COMPATIBLE_IOCTL(VT_GETSTATE)
COMPATIBLE_IOCTL(VT_OPENQRY)
COMPATIBLE_IOCTL(VT_ACTIVATE)
COMPATIBLE_IOCTL(VT_WAITACTIVE)
COMPATIBLE_IOCTL(VT_RELDISP)
COMPATIBLE_IOCTL(VT_DISALLOCATE)
COMPATIBLE_IOCTL(VT_RESIZE)
COMPATIBLE_IOCTL(VT_RESIZEX)
COMPATIBLE_IOCTL(VT_LOCKSWITCH)
COMPATIBLE_IOCTL(VT_UNLOCKSWITCH)

#ifdef CONFIG_NET
/* Socket level stuff */
COMPATIBLE_IOCTL(FIOSETOWN)
COMPATIBLE_IOCTL(SIOCSPGRP)
COMPATIBLE_IOCTL(FIOGETOWN)
COMPATIBLE_IOCTL(SIOCGPGRP)
COMPATIBLE_IOCTL(SIOCATMARK)
COMPATIBLE_IOCTL(SIOCSIFLINK)
COMPATIBLE_IOCTL(SIOCSIFENCAP)
COMPATIBLE_IOCTL(SIOCGIFENCAP)
COMPATIBLE_IOCTL(SIOCSIFBR)
COMPATIBLE_IOCTL(SIOCGIFBR)
COMPATIBLE_IOCTL(SIOCSARP)
COMPATIBLE_IOCTL(SIOCGARP)
COMPATIBLE_IOCTL(SIOCDARP)
COMPATIBLE_IOCTL(SIOCSRARP)
COMPATIBLE_IOCTL(SIOCGRARP)
COMPATIBLE_IOCTL(SIOCDRARP)
COMPATIBLE_IOCTL(SIOCADDDLCI)
COMPATIBLE_IOCTL(SIOCDELDLCI)
/* SG stuff */
COMPATIBLE_IOCTL(SG_SET_TIMEOUT)
COMPATIBLE_IOCTL(SG_GET_TIMEOUT)
COMPATIBLE_IOCTL(SG_EMULATED_HOST)
COMPATIBLE_IOCTL(SG_SET_TRANSFORM)
COMPATIBLE_IOCTL(SG_GET_TRANSFORM)
COMPATIBLE_IOCTL(SG_SET_RESERVED_SIZE)
COMPATIBLE_IOCTL(SG_GET_RESERVED_SIZE)
COMPATIBLE_IOCTL(SG_GET_SCSI_ID)
COMPATIBLE_IOCTL(SG_SET_FORCE_LOW_DMA)
COMPATIBLE_IOCTL(SG_GET_LOW_DMA)
COMPATIBLE_IOCTL(SG_SET_FORCE_PACK_ID)
COMPATIBLE_IOCTL(SG_GET_PACK_ID)
COMPATIBLE_IOCTL(SG_GET_NUM_WAITING)
COMPATIBLE_IOCTL(SG_SET_DEBUG)
COMPATIBLE_IOCTL(SG_GET_SG_TABLESIZE)
COMPATIBLE_IOCTL(SG_GET_COMMAND_Q)
COMPATIBLE_IOCTL(SG_SET_COMMAND_Q)
COMPATIBLE_IOCTL(SG_GET_VERSION_NUM)
COMPATIBLE_IOCTL(SG_NEXT_CMD_LEN)
COMPATIBLE_IOCTL(SG_SCSI_RESET)
COMPATIBLE_IOCTL(SG_IO)
COMPATIBLE_IOCTL(SG_GET_REQUEST_TABLE)
COMPATIBLE_IOCTL(SG_SET_KEEP_ORPHAN)
COMPATIBLE_IOCTL(SG_GET_KEEP_ORPHAN)
/* PPP stuff */
COMPATIBLE_IOCTL(PPPIOCGFLAGS)
COMPATIBLE_IOCTL(PPPIOCSFLAGS)
COMPATIBLE_IOCTL(PPPIOCGASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCGUNIT)
COMPATIBLE_IOCTL(PPPIOCGRASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSRASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCGMRU)
COMPATIBLE_IOCTL(PPPIOCSMRU)
COMPATIBLE_IOCTL(PPPIOCSMAXCID)
COMPATIBLE_IOCTL(PPPIOCGXASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSXASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCXFERUNIT)
COMPATIBLE_IOCTL(PPPIOCGNPMODE)
COMPATIBLE_IOCTL(PPPIOCSNPMODE)
COMPATIBLE_IOCTL(PPPIOCGDEBUG)
COMPATIBLE_IOCTL(PPPIOCSDEBUG)
COMPATIBLE_IOCTL(PPPIOCNEWUNIT)
COMPATIBLE_IOCTL(PPPIOCATTACH)
COMPATIBLE_IOCTL(PPPIOCGCHAN)
/* PPPOX */
COMPATIBLE_IOCTL(PPPOEIOCSFWD)
COMPATIBLE_IOCTL(PPPOEIOCDFWD)
/* CDROM stuff */
COMPATIBLE_IOCTL(CDROMPAUSE)
COMPATIBLE_IOCTL(CDROMRESUME)
COMPATIBLE_IOCTL(CDROMPLAYMSF)
COMPATIBLE_IOCTL(CDROMPLAYTRKIND)
COMPATIBLE_IOCTL(CDROMREADTOCHDR)
COMPATIBLE_IOCTL(CDROMREADTOCENTRY)
COMPATIBLE_IOCTL(CDROMSTOP)
COMPATIBLE_IOCTL(CDROMSTART)
COMPATIBLE_IOCTL(CDROMEJECT)
COMPATIBLE_IOCTL(CDROMVOLCTRL)
COMPATIBLE_IOCTL(CDROMSUBCHNL)
COMPATIBLE_IOCTL(CDROMEJECT_SW)
COMPATIBLE_IOCTL(CDROMMULTISESSION)
COMPATIBLE_IOCTL(CDROM_GET_MCN)
COMPATIBLE_IOCTL(CDROMRESET)
COMPATIBLE_IOCTL(CDROMVOLREAD)
COMPATIBLE_IOCTL(CDROMSEEK)
COMPATIBLE_IOCTL(CDROMPLAYBLK)
COMPATIBLE_IOCTL(CDROMCLOSETRAY)
COMPATIBLE_IOCTL(CDROM_SET_OPTIONS)
COMPATIBLE_IOCTL(CDROM_CLEAR_OPTIONS)
COMPATIBLE_IOCTL(CDROM_SELECT_SPEED)
COMPATIBLE_IOCTL(CDROM_SELECT_DISC)
COMPATIBLE_IOCTL(CDROM_MEDIA_CHANGED)
COMPATIBLE_IOCTL(CDROM_DRIVE_STATUS)
COMPATIBLE_IOCTL(CDROM_DISC_STATUS)
COMPATIBLE_IOCTL(CDROM_CHANGER_NSLOTS)
COMPATIBLE_IOCTL(CDROM_LOCKDOOR)
COMPATIBLE_IOCTL(CDROM_DEBUG)
COMPATIBLE_IOCTL(CDROM_GET_CAPABILITY)
/* DVD ioctls */
COMPATIBLE_IOCTL(DVD_READ_STRUCT)
COMPATIBLE_IOCTL(DVD_WRITE_STRUCT)
COMPATIBLE_IOCTL(DVD_AUTH)
/* Big L */
COMPATIBLE_IOCTL(LOOP_SET_FD)
COMPATIBLE_IOCTL(LOOP_CLR_FD)

/* And these ioctls need translation */
HANDLE_IOCTL(SIOCGIFNAME, dev_ifname32)
HANDLE_IOCTL(SIOCGIFCONF, dev_ifconf)
HANDLE_IOCTL(SIOCGIFFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMETRIC, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMETRIC, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMTU, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMTU, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMEM, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMEM, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFHWADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFHWADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCADDMULTI, dev_ifsioc)
HANDLE_IOCTL(SIOCDELMULTI, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFINDEX, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMAP, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMAP, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFBRDADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFBRDADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFDSTADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFDSTADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFNETMASK, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFNETMASK, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFPFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFPFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCGPPPSTATS, dev_ifsioc)
HANDLE_IOCTL(SIOCGPPPCSTATS, dev_ifsioc)
HANDLE_IOCTL(SIOCGPPPVER, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFTXQLEN, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFTXQLEN, dev_ifsioc)
HANDLE_IOCTL(SIOCADDRT, routing_ioctl)
HANDLE_IOCTL(SIOCDELRT, routing_ioctl)
/*
 * Note SIOCRTMSG is no longer, so this is safe and * the user would
 * have seen just an -EINVAL anyways.
 */
HANDLE_IOCTL(SIOCRTMSG, ret_einval)
HANDLE_IOCTL(SIOCGSTAMP, do_siocgstamp)

#endif /* CONFIG_NET */

HANDLE_IOCTL(EXT2_IOC32_GETFLAGS, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_SETFLAGS, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_GETVERSION, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_SETVERSION, do_ext2_ioctl)

HANDLE_IOCTL(HDIO_GETGEO, hdio_getgeo)		/* hdreg.h ioctls  */
HANDLE_IOCTL(HDIO_GET_UNMASKINTR, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_MULTCOUNT, hdio_ioctl_trans)
// HDIO_OBSOLETE_IDENTITY
//HANDLE_IOCTL(HDIO_GET_KEEPSETTINGS, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_32BIT, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_NOWERR, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_DMA, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_NICE, hdio_ioctl_trans)
COMPATIBLE_IOCTL(HDIO_GET_IDENTITY)
// HDIO_TRISTATE_HWIF				/* not implemented */
// HDIO_DRIVE_TASK				/* To do, need specs */
COMPATIBLE_IOCTL(HDIO_DRIVE_CMD)
COMPATIBLE_IOCTL(HDIO_SET_MULTCOUNT)
COMPATIBLE_IOCTL(HDIO_SET_UNMASKINTR)
//COMPATIBLE_IOCTL(HDIO_SET_KEEPSETTINGS)
COMPATIBLE_IOCTL(HDIO_SET_32BIT)
COMPATIBLE_IOCTL(HDIO_SET_NOWERR)
COMPATIBLE_IOCTL(HDIO_SET_DMA)
COMPATIBLE_IOCTL(HDIO_SET_PIO_MODE)
COMPATIBLE_IOCTL(HDIO_SET_NICE)

COMPATIBLE_IOCTL(BLKROSET)			/* fs.h ioctls  */
COMPATIBLE_IOCTL(BLKROGET)
COMPATIBLE_IOCTL(BLKRRPART)
HANDLE_IOCTL(BLKGETSIZE, w_long)

COMPATIBLE_IOCTL(BLKFLSBUF)
COMPATIBLE_IOCTL(BLKSECTSET)
HANDLE_IOCTL(BLKSECTGET, w_long)
COMPATIBLE_IOCTL(BLKSSZGET)
HANDLE_IOCTL(BLKPG, blkpg_ioctl_trans)
COMPATIBLE_IOCTL(BLKBSZGET)
COMPATIBLE_IOCTL(BLKBSZSET)

#ifdef CONFIG_MD
/* status */
COMPATIBLE_IOCTL(RAID_VERSION)
COMPATIBLE_IOCTL(GET_ARRAY_INFO)
COMPATIBLE_IOCTL(GET_DISK_INFO)
COMPATIBLE_IOCTL(PRINT_RAID_DEBUG)
COMPATIBLE_IOCTL(RAID_AUTORUN)

/* configuration */
COMPATIBLE_IOCTL(CLEAR_ARRAY)
COMPATIBLE_IOCTL(ADD_NEW_DISK)
COMPATIBLE_IOCTL(HOT_REMOVE_DISK)
COMPATIBLE_IOCTL(SET_ARRAY_INFO)
COMPATIBLE_IOCTL(SET_DISK_INFO)
COMPATIBLE_IOCTL(WRITE_RAID_INFO)
COMPATIBLE_IOCTL(UNPROTECT_ARRAY)
COMPATIBLE_IOCTL(PROTECT_ARRAY)
COMPATIBLE_IOCTL(HOT_ADD_DISK)
COMPATIBLE_IOCTL(SET_DISK_FAULTY)

/* usage */
COMPATIBLE_IOCTL(RUN_ARRAY)
COMPATIBLE_IOCTL(START_ARRAY)
COMPATIBLE_IOCTL(STOP_ARRAY)
COMPATIBLE_IOCTL(STOP_ARRAY_RO)
COMPATIBLE_IOCTL(RESTART_ARRAY_RW)
#endif /* CONFIG_MD */

#ifdef CONFIG_SIBYTE_TBPROF
COMPATIBLE_IOCTL(SBPROF_ZBSTART),
COMPATIBLE_IOCTL(SBPROF_ZBSTOP),
COMPATIBLE_IOCTL(SBPROF_ZBWAITFULL),
#endif /* CONFIG_SIBYTE_TBPROF */

#if defined(CONFIG_BLK_DEV_DM) || defined(CONFIG_BLK_DEV_DM_MODULE)
	IOCTL32_DEFAULT(DM_VERSION),
	IOCTL32_DEFAULT(DM_REMOVE_ALL),
	IOCTL32_DEFAULT(DM_DEV_CREATE),
	IOCTL32_DEFAULT(DM_DEV_REMOVE),
	IOCTL32_DEFAULT(DM_DEV_RELOAD),
	IOCTL32_DEFAULT(DM_DEV_SUSPEND),
	IOCTL32_DEFAULT(DM_DEV_RENAME),
	IOCTL32_DEFAULT(DM_DEV_DEPS),
	IOCTL32_DEFAULT(DM_DEV_STATUS),
	IOCTL32_DEFAULT(DM_TARGET_STATUS),
	IOCTL32_DEFAULT(DM_TARGET_WAIT),
#endif /* CONFIG_BLK_DEV_DM */

COMPATIBLE_IOCTL(MTIOCTOP)			/* mtio.h ioctls  */
HANDLE_IOCTL(MTIOCGET32, mt_ioctl_trans)
HANDLE_IOCTL(MTIOCPOS32, mt_ioctl_trans)
HANDLE_IOCTL(MTIOCGETCONFIG32, mt_ioctl_trans)
HANDLE_IOCTL(MTIOCSETCONFIG32, mt_ioctl_trans)
// MTIOCRDFTSEG
// MTIOCWRFTSEG
// MTIOCVOLINFO
// MTIOCGETSIZE
// MTIOCFTFORMAT
// MTIOCFTCMD

COMPATIBLE_IOCTL(AUTOFS_IOC_READY)		/* auto_fs.h ioctls */
COMPATIBLE_IOCTL(AUTOFS_IOC_FAIL)
COMPATIBLE_IOCTL(AUTOFS_IOC_CATATONIC)
COMPATIBLE_IOCTL(AUTOFS_IOC_PROTOVER)
HANDLE_IOCTL(AUTOFS_IOC_SETTIMEOUT32, ioc_settimeout)
COMPATIBLE_IOCTL(AUTOFS_IOC_EXPIRE)
COMPATIBLE_IOCTL(AUTOFS_IOC_EXPIRE_MULTI)

/* Little p (/dev/rtc, /dev/envctrl, etc.) */
COMPATIBLE_IOCTL(_IOR('p', 20, int[7]))		/* RTCGET */
COMPATIBLE_IOCTL(_IOW('p', 21, int[7]))		/* RTCSET */
COMPATIBLE_IOCTL(RTC_AIE_ON)
COMPATIBLE_IOCTL(RTC_AIE_OFF)
COMPATIBLE_IOCTL(RTC_UIE_ON)
COMPATIBLE_IOCTL(RTC_UIE_OFF)
COMPATIBLE_IOCTL(RTC_PIE_ON)
COMPATIBLE_IOCTL(RTC_PIE_OFF)
COMPATIBLE_IOCTL(RTC_WIE_ON)
COMPATIBLE_IOCTL(RTC_WIE_OFF)
COMPATIBLE_IOCTL(RTC_ALM_SET)
COMPATIBLE_IOCTL(RTC_ALM_READ)
COMPATIBLE_IOCTL(RTC_RD_TIME)
COMPATIBLE_IOCTL(RTC_SET_TIME)
COMPATIBLE_IOCTL(RTC_WKALM_SET)
COMPATIBLE_IOCTL(RTC_WKALM_RD)
IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);

#define NR_IOCTL_TRANS		(sizeof(ioctl_translations) /	\
				 sizeof(ioctl_translations[0]))
