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

#include <linux/compat.h>
#include <linux/init.h>
#include <linux/ioctl32.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/types.h>

#include <asm/ioctls.h>
#include <asm/types.h>
#include <asm/uaccess.h>

#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/cdrom.h>
#include <linux/dm-ioctl.h>
#include <linux/elevator.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <linux/kd.h>
#include <linux/loop.h>
#include <linux/lp.h>
#include <linux/mtio.h>
#include <linux/netdevice.h>
#include <linux/nbd.h>
#include <linux/ppp_defs.h>
#include <linux/raid/md_u.h>
#include <linux/random.h>
#include <linux/raw.h>
#include <linux/route.h>
#include <linux/rtc.h>
#include <linux/vt.h>
#include <linux/watchdog.h>

#include <linux/auto_fs.h>
#include <linux/auto_fs4.h>
#include <linux/devfs_fs.h>
#include <linux/ext2_fs.h>
#include <linux/ncp_fs.h>
#include <linux/smb_fs.h>

#include <linux/if_bonding.h>
#include <linux/if_ppp.h>
#include <linux/if_pppox.h>
#include <linux/if_tun.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>

#include <asm/dasd.h>
#include <asm/sockios.h>
#include <asm/tape390.h>

#include "compat_linux.h"

long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

struct hd_geometry32 {
	unsigned char	heads;
	unsigned char	sectors;
	unsigned short	cylinders;
	__u32		start;
};  

static inline int hd_geometry_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg,
				    struct file *f)
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

static int dev_ifname32(unsigned int fd, unsigned int cmd,
			unsigned long arg, struct file *f)
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

static int dev_ifconf(unsigned int fd, unsigned int cmd,
		      unsigned long arg, struct file *f)
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

static int bond_ioctl(unsigned int fd, unsigned int cmd,
		      unsigned long arg, struct file *f)
{
	struct ifreq ifr;
	mm_segment_t old_fs;
	int err, len;
	u32 data;
	
	if (copy_from_user(&ifr, (struct ifreq32 *)arg, sizeof(struct ifreq32)))
		return -EFAULT;
	ifr.ifr_data = (__kernel_caddr_t)get_zeroed_page(GFP_KERNEL);
	if (!ifr.ifr_data)
		return -EAGAIN;

	switch (cmd) {
	case SIOCBONDENSLAVE:
	case SIOCBONDRELEASE:
	case SIOCBONDSETHWADDR:
	case SIOCBONDCHANGEACTIVE:
		len = IFNAMSIZ * sizeof(char);
		break;
	case SIOCBONDSLAVEINFOQUERY:
		len = sizeof(struct ifslave);
		break;
	case SIOCBONDINFOQUERY:
		len = sizeof(struct ifbond);
		break;
	default:
		err = -EINVAL;
		goto out;
	};

	__get_user(data, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_data));
	if (copy_from_user(ifr.ifr_data, (char *)A(data), len)) {
		err = -EFAULT;
		goto out;
	}

	old_fs = get_fs();
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, cmd, (unsigned long)&ifr);
	set_fs (old_fs);
	if (!err) {
		len = copy_to_user((char *)A(data), ifr.ifr_data, len);
		if (len)
			err = -EFAULT;
	}

out:
	free_page((unsigned long)ifr.ifr_data);
	return err;
}

static int dev_ifsioc(unsigned int fd, unsigned int cmd,
			     unsigned long arg, struct file *f)
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

static int routing_ioctl(unsigned int fd, unsigned int cmd,
			 unsigned long arg, struct file *f)
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

static int do_ext2_ioctl(unsigned int fd, unsigned int cmd,
			 unsigned long arg, struct file *f)
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


struct loop_info32 {
	int			lo_number;      /* ioctl r/o */
	compat_dev_t	lo_device;      /* ioctl r/o */
	unsigned int		lo_inode;       /* ioctl r/o */
	compat_dev_t	lo_rdevice;     /* ioctl r/o */
	int			lo_offset;
	int			lo_encrypt_type;
	int			lo_encrypt_key_size;    /* ioctl w/o */
	int			lo_flags;       /* ioctl r/o */
	char			lo_name[LO_NAME_SIZE];
	unsigned char		lo_encrypt_key[LO_KEY_SIZE]; /* ioctl w/o */
	unsigned int		lo_init[2];
	char			reserved[4];
};

static int loop_status(unsigned int fd, unsigned int cmd,
		       unsigned long arg, struct file *f)
{
	mm_segment_t old_fs = get_fs();
	struct loop_info l;
	int err = -EINVAL;

	switch(cmd) {
	case LOOP_SET_STATUS:
		err = get_user(l.lo_number, &((struct loop_info32 *)arg)->lo_number);
		err |= __get_user(l.lo_device, &((struct loop_info32 *)arg)->lo_device);
		err |= __get_user(l.lo_inode, &((struct loop_info32 *)arg)->lo_inode);
		err |= __get_user(l.lo_rdevice, &((struct loop_info32 *)arg)->lo_rdevice);
		err |= __copy_from_user((char *)&l.lo_offset, (char *)&((struct loop_info32 *)arg)->lo_offset,
					   8 + (unsigned long)l.lo_init - (unsigned long)&l.lo_offset);
		if (err) {
			err = -EFAULT;
		} else {
			set_fs (KERNEL_DS);
			err = sys_ioctl (fd, cmd, (unsigned long)&l);
			set_fs (old_fs);
		}
		break;
	case LOOP_GET_STATUS:
		set_fs (KERNEL_DS);
		err = sys_ioctl (fd, cmd, (unsigned long)&l);
		set_fs (old_fs);
		if (!err) {
			err = put_user(l.lo_number, &((struct loop_info32 *)arg)->lo_number);
			err |= __put_user(l.lo_device, &((struct loop_info32 *)arg)->lo_device);
			err |= __put_user(l.lo_inode, &((struct loop_info32 *)arg)->lo_inode);
			err |= __put_user(l.lo_rdevice, &((struct loop_info32 *)arg)->lo_rdevice);
			err |= __copy_to_user((char *)&((struct loop_info32 *)arg)->lo_offset,
					   (char *)&l.lo_offset, (unsigned long)l.lo_init - (unsigned long)&l.lo_offset);
			if (err)
				err = -EFAULT;
		}
		break;
	default: {
		static int count = 0;
		if (++count <= 20)
			printk("%s: Unknown loop ioctl cmd, fd(%d) "
			       "cmd(%08x) arg(%08lx)\n",
			       __FUNCTION__, fd, cmd, arg);
	}
	}
	return err;
}


struct blkpg_ioctl_arg32 {
	int op;
	int flags;
	int datalen;
	u32 data;
};
                                
static int blkpg_ioctl_trans(unsigned int fd, unsigned int cmd,
			     unsigned long uarg, struct file *f)
{
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;
	struct blkpg_ioctl_arg32 *arg = (void*)A(uarg);
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


typedef struct ica_z90_status_t {
  int totalcount;
  int leedslitecount;
  int leeds2count;
  int requestqWaitCount;
  int pendingqWaitCount;
  int totalOpenCount;
  int cryptoDomain;
  unsigned char status[64];
  unsigned char qdepth[64];
} ica_z90_status;

typedef struct _ica_rsa_modexpo {
  char         *inputdata;
  unsigned int  inputdatalength;
  char         *outputdata;
  unsigned int  outputdatalength;
  char         *b_key;
  char         *n_modulus;
} ica_rsa_modexpo_t;

typedef struct _ica_rsa_modexpo_32 {
  u32          inputdata;
  u32          inputdatalength;
  u32          outputdata;
  u32          outputdatalength;
  u32          b_key;
  u32          n_modulus;
} ica_rsa_modexpo_32_t;

typedef struct _ica_rsa_modexpo_crt {
  char         *inputdata;
  unsigned int  inputdatalength;
  char         *outputdata;
  unsigned int  outputdatalength;
  char         *bp_key;
  char         *bq_key;
  char         *np_prime;
  char         *nq_prime;
  char         *u_mult_inv;
} ica_rsa_modexpo_crt_t;

typedef struct _ica_rsa_modexpo_crt_32 {
  u32          inputdata;
  u32          inputdatalength;
  u32          outputdata;
  u32          outputdatalength;
  u32          bp_key;
  u32          bq_key;
  u32          np_prime;
  u32          nq_prime;
  u32          u_mult_inv;
} ica_rsa_modexpo_crt_32_t;

#define ICA_IOCTL_MAGIC 'z'
#define ICARSAMODEXPO   _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x05, 0)
#define ICARSACRT       _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x06, 0) 
#define ICARSAMODMULT   _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x07, 0)
#define ICAZ90STATUS    _IOC(_IOC_READ, ICA_IOCTL_MAGIC, 0x10, sizeof(ica_z90_status))
#define ICAZ90QUIESCE   _IOC(_IOC_NONE, ICA_IOCTL_MAGIC, 0x11, 0)
#define ICAZ90HARDRESET _IOC(_IOC_NONE, ICA_IOCTL_MAGIC, 0x12, 0)
#define ICAZ90HARDERROR _IOC(_IOC_NONE, ICA_IOCTL_MAGIC, 0x13, 0)

static int do_rsa_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *f)
{
	mm_segment_t old_fs = get_fs();
	int err = 0;
	ica_rsa_modexpo_t rsa;
	ica_rsa_modexpo_32_t *rsa32 = (ica_rsa_modexpo_32_t *)arg;
	u32 inputdata, outputdata, b_key, n_modulus;

	memset (&rsa, 0, sizeof(rsa));

	err |= __get_user (inputdata, &rsa32->inputdata);
	err |= __get_user (rsa.inputdatalength, &rsa32->inputdatalength);
	err |= __get_user (outputdata, &rsa32->outputdata);
	err |= __get_user (rsa.outputdatalength, &rsa32->outputdatalength);
	err |= __get_user (b_key, &rsa32->b_key);
	err |= __get_user (n_modulus, &rsa32->n_modulus);
	if (err)
		return -EFAULT;

	rsa.inputdata = (char *)kmalloc(rsa.inputdatalength, GFP_KERNEL);
	if (!rsa.inputdata) {
		err = -ENOMEM;
		goto cleanup;
	}
	if (copy_from_user(rsa.inputdata, (char *)(u64)(inputdata & 0x7fffffff), 
			   rsa.inputdatalength)) {
		err = -EFAULT;
		goto cleanup;
	}

	rsa.outputdata = (char *)kmalloc(rsa.outputdatalength, GFP_KERNEL);
	if (!rsa.outputdata) {
		err = -ENOMEM;
		goto cleanup;
	}

	rsa.b_key = (char *)kmalloc(rsa.inputdatalength, GFP_KERNEL);
	if (!rsa.b_key) {
		err = -ENOMEM;
		goto cleanup;
	}
	if (copy_from_user(rsa.b_key, (char *)(u64)(b_key & 0x7fffffff), 
			   rsa.inputdatalength)) {
		err = -EFAULT;
		goto cleanup;
	}

	rsa.n_modulus = (char *)kmalloc(rsa.inputdatalength, GFP_KERNEL);
	if (!rsa.n_modulus) {
		err = -ENOMEM;
		goto cleanup;
	}
	if (copy_from_user(rsa.n_modulus, (char *)(u64)(n_modulus & 0x7fffffff), 
			   rsa.inputdatalength)) {
		err = -EFAULT;
		goto cleanup;
	}

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&rsa);
	set_fs(old_fs);
	if (err < 0)
		goto cleanup;

	if (copy_to_user((char *)(u64)(outputdata & 0x7fffffff), rsa.outputdata,
			 rsa.outputdatalength))
		err = -EFAULT;

cleanup:
	if (rsa.inputdata)
		kfree(rsa.inputdata);
	if (rsa.outputdata)
		kfree(rsa.outputdata);
	if (rsa.b_key)
		kfree(rsa.b_key);
	if (rsa.n_modulus)
		kfree(rsa.n_modulus);
	
	return err;
}

static int do_rsa_crt_ioctl(unsigned int fd, unsigned int cmd,
			    unsigned long arg, struct file *f)
{
	mm_segment_t old_fs = get_fs();
	int err = 0;
	ica_rsa_modexpo_crt_t rsa;
	ica_rsa_modexpo_crt_32_t *rsa32 = (ica_rsa_modexpo_crt_32_t *)arg;
	u32 inputdata, outputdata, bp_key, bq_key, np_prime, nq_prime, u_mult_inv;

	memset (&rsa, 0, sizeof(rsa));

	err |= __get_user (inputdata, &rsa32->inputdata);
	err |= __get_user (rsa.inputdatalength, &rsa32->inputdatalength);
	err |= __get_user (outputdata, &rsa32->outputdata);
	err |= __get_user (rsa.outputdatalength, &rsa32->outputdatalength);
	err |= __get_user (bp_key, &rsa32->bp_key);
	err |= __get_user (bq_key, &rsa32->bq_key);
	err |= __get_user (np_prime, &rsa32->np_prime);
	err |= __get_user (nq_prime, &rsa32->nq_prime);
	err |= __get_user (u_mult_inv, &rsa32->u_mult_inv);
	if (err)
		return -EFAULT;

	rsa.inputdata = (char *)kmalloc(rsa.inputdatalength, GFP_KERNEL);
	if (!rsa.inputdata) {
		err = -ENOMEM;
		goto cleanup;
	}
	if (copy_from_user(rsa.inputdata, (char *)(u64)(inputdata & 0x7fffffff), 
			   rsa.inputdatalength)) {
		err = -EFAULT;
		goto cleanup;
	}

	rsa.outputdata = (char *)kmalloc(rsa.outputdatalength, GFP_KERNEL);
	if (!rsa.outputdata) {
		err = -ENOMEM;
		goto cleanup;
	}

	rsa.bp_key = (char *)kmalloc(rsa.inputdatalength/2 + 8, GFP_KERNEL);
	if (!rsa.bp_key) {
		err = -ENOMEM;
		goto cleanup;
	}
	if (copy_from_user(rsa.bp_key, (char *)(u64)(bp_key & 0x7fffffff), 
			   rsa.inputdatalength/2 + 8)) {
		err = -EFAULT;
		goto cleanup;
	}

	rsa.bq_key = (char *)kmalloc(rsa.inputdatalength/2, GFP_KERNEL);
	if (!rsa.bq_key) {
		err = -ENOMEM;
		goto cleanup;
	}
	if (copy_from_user(rsa.bq_key, (char *)(u64)(bq_key & 0x7fffffff), 
			   rsa.inputdatalength/2)) {
		err = -EFAULT;
		goto cleanup;
	}

	rsa.np_prime = (char *)kmalloc(rsa.inputdatalength/2 + 8, GFP_KERNEL);
	if (!rsa.np_prime) {
		err = -ENOMEM;
		goto cleanup;
	}
	if (copy_from_user(rsa.np_prime, (char *)(u64)(np_prime & 0x7fffffff), 
			   rsa.inputdatalength/2 + 8)) {
		err = -EFAULT;
		goto cleanup;
	}

	rsa.nq_prime = (char *)kmalloc(rsa.inputdatalength/2, GFP_KERNEL);
	if (!rsa.nq_prime) {
		err = -ENOMEM;
		goto cleanup;
	}
	if (copy_from_user(rsa.nq_prime, (char *)(u64)(nq_prime & 0x7fffffff), 
			   rsa.inputdatalength/2)) {
		err = -EFAULT;
		goto cleanup;
	}

	rsa.u_mult_inv = (char *)kmalloc(rsa.inputdatalength/2 + 8, GFP_KERNEL);
	if (!rsa.u_mult_inv) {
		err = -ENOMEM;
		goto cleanup;
	}
	if (copy_from_user(rsa.u_mult_inv, (char *)(u64)(u_mult_inv & 0x7fffffff), 
			   rsa.inputdatalength/2 + 8)) {
		err = -EFAULT;
		goto cleanup;
	}

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&rsa);
	set_fs(old_fs);
	if (err < 0)
		goto cleanup;

	if (copy_to_user((char *)(u64)(outputdata & 0x7fffffff), rsa.outputdata,
			 rsa.outputdatalength))
		err = -EFAULT;

cleanup:
	if (rsa.inputdata)
		kfree(rsa.inputdata);
	if (rsa.outputdata)
		kfree(rsa.outputdata);
	if (rsa.bp_key)
		kfree(rsa.bp_key);
	if (rsa.bq_key)
		kfree(rsa.bq_key);
	if (rsa.np_prime)
		kfree(rsa.np_prime);
	if (rsa.nq_prime)
		kfree(rsa.nq_prime);
	if (rsa.u_mult_inv)
		kfree(rsa.u_mult_inv);
	
	return err;
}

static int w_long(unsigned int fd, unsigned int cmd, unsigned long arg,
		  struct file *f)
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

int siocdevprivate_ioctl(unsigned int fd, unsigned int cmd,
			 unsigned long arg, struct file *f)
{
	/* siocdevprivate cannot be emulated properly */
	return -EINVAL;
}

#define COMPATIBLE_IOCTL(cmd)		HANDLE_IOCTL((cmd), NULL)
#define HANDLE_IOCTL(cmd,handler)	{ (cmd), (handler), NULL },
#define IOCTL_TABLE_START \
	struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END \
	};

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>

COMPATIBLE_IOCTL(DASDAPIVER)
COMPATIBLE_IOCTL(BIODASDDISABLE)
COMPATIBLE_IOCTL(BIODASDENABLE)
COMPATIBLE_IOCTL(BIODASDRSRV)
COMPATIBLE_IOCTL(BIODASDRLSE)
COMPATIBLE_IOCTL(BIODASDSLCK)
COMPATIBLE_IOCTL(BIODASDINFO)
COMPATIBLE_IOCTL(BIODASDFMT)

COMPATIBLE_IOCTL(TAPE390_DISPLAY)
COMPATIBLE_IOCTL(BLKRASET)
COMPATIBLE_IOCTL(BLKFRASET)
COMPATIBLE_IOCTL(BLKBSZGET)
COMPATIBLE_IOCTL(BLKGETSIZE64)

HANDLE_IOCTL(HDIO_GETGEO, hd_geometry_ioctl)

COMPATIBLE_IOCTL(TCSBRKP)

COMPATIBLE_IOCTL(TIOCGSERIAL)
COMPATIBLE_IOCTL(TIOCSSERIAL)

COMPATIBLE_IOCTL(SIOCGSTAMP)

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
HANDLE_IOCTL(SIOCGIFTXQLEN, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFTXQLEN, dev_ifsioc)
HANDLE_IOCTL(SIOCADDRT, routing_ioctl)
HANDLE_IOCTL(SIOCDELRT, routing_ioctl)
HANDLE_IOCTL(SIOCBONDENSLAVE, bond_ioctl)
HANDLE_IOCTL(SIOCBONDRELEASE, bond_ioctl)
HANDLE_IOCTL(SIOCBONDSETHWADDR, bond_ioctl)
HANDLE_IOCTL(SIOCBONDSLAVEINFOQUERY, bond_ioctl)
HANDLE_IOCTL(SIOCBONDINFOQUERY, bond_ioctl)
HANDLE_IOCTL(SIOCBONDCHANGEACTIVE, bond_ioctl)

HANDLE_IOCTL(EXT2_IOC32_GETFLAGS, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_SETFLAGS, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_GETVERSION, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_SETVERSION, do_ext2_ioctl)

HANDLE_IOCTL(LOOP_SET_STATUS, loop_status)
HANDLE_IOCTL(LOOP_GET_STATUS, loop_status)

HANDLE_IOCTL(ICARSAMODEXPO, do_rsa_ioctl)
HANDLE_IOCTL(ICARSACRT, do_rsa_crt_ioctl)
HANDLE_IOCTL(ICARSAMODMULT, do_rsa_ioctl)

COMPATIBLE_IOCTL(ICAZ90STATUS)
COMPATIBLE_IOCTL(ICAZ90QUIESCE)
COMPATIBLE_IOCTL(ICAZ90HARDRESET)
COMPATIBLE_IOCTL(ICAZ90HARDERROR)

HANDLE_IOCTL(BLKRAGET, w_long)
HANDLE_IOCTL(BLKGETSIZE, w_long)
HANDLE_IOCTL(BLKFRAGET, w_long)
HANDLE_IOCTL(BLKSECTGET, w_long)
HANDLE_IOCTL(BLKPG, blkpg_ioctl_trans)

IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
