/* 
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 * 
 * Based on sparc64 ioctl32.c by:
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 *
 * ppc64 changes:
 *
 * Copyright (C) 2000  Ken Aaker (kdaaker@rchland.vnet.ibm.com)
 * Copyright (C) 2001  Anton Blanchard (antonb@au.ibm.com)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define INCLUDES
#include "compat_ioctl.c"
#include <linux/ncp_fs.h>
#include <asm/ppc32.h>

#define CODE
#include "compat_ioctl.c"

struct hd_big_geometry32 {
	unsigned char heads;
	unsigned char sectors;
	unsigned int cylinders;
	u32 start;
};
                        
static int hdio_getgeo_big(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct hd_big_geometry geo;
	int err;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&geo);
	set_fs (old_fs);
	if (!err) {
		struct hd_big_geometry32 *up = (struct hd_big_geometry32 *) arg;

		if (put_user(geo.heads, &up->heads) ||
		    __put_user(geo.sectors, &up->sectors) ||
		    __put_user(geo.cylinders, &up->cylinders) ||
		    __put_user(((u32) geo.start), &up->start))
			err = -EFAULT;
	}
	return err;
}

struct ncp_ioctl_request_32 {
	unsigned int function;
	unsigned int size;
	compat_caddr_t data;
};

struct ncp_fs_info_v2_32 {
	int version;
	unsigned int mounted_uid;
	unsigned int connection;
	unsigned int buffer_size;

	unsigned int volume_number;
	__u32 directory_id;

	__u32 dummy1;
	__u32 dummy2;
	__u32 dummy3;
};

struct ncp_objectname_ioctl_32
{
	int		auth_type;
	unsigned int	object_name_len;
	compat_caddr_t	object_name;	/* an userspace data, in most cases user name */
};

struct ncp_privatedata_ioctl_32
{
	unsigned int	len;
	compat_caddr_t	data;		/* ~1000 for NDS */
};

#define	NCP_IOC_NCPREQUEST_32		_IOR('n', 1, struct ncp_ioctl_request_32)

#define NCP_IOC_GETMOUNTUID2_32		_IOW('n', 2, unsigned int)

#define NCP_IOC_GET_FS_INFO_V2_32	_IOWR('n', 4, struct ncp_fs_info_v2_32)

#define NCP_IOC_GETOBJECTNAME_32	_IOWR('n', 9, struct ncp_objectname_ioctl_32)
#define NCP_IOC_SETOBJECTNAME_32	_IOR('n', 9, struct ncp_objectname_ioctl_32)
#define NCP_IOC_GETPRIVATEDATA_32	_IOWR('n', 10, struct ncp_privatedata_ioctl_32)
#define NCP_IOC_SETPRIVATEDATA_32	_IOR('n', 10, struct ncp_privatedata_ioctl_32)

static int do_ncp_ncprequest(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_ioctl_request_32 n32;
	struct ncp_ioctl_request n;
	mm_segment_t old_fs;
	int err;

	if (copy_from_user(&n32, (struct ncp_ioctl_request_32*)arg,
	    sizeof(n32)))
		return -EFAULT;

	n.function = n32.function;
	n.size = n32.size;
	if (n.size > 65536)
		return -EINVAL;
	n.data = vmalloc(65536);	/* 65536 must be same as NCP_PACKET_SIZE_INTERNAL in ncpfs */
	if (!n.data)
		return -ENOMEM;
	err = -EFAULT;
	if (copy_from_user(n.data, (void *)A(n32.data), n.size))
		goto out;

	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, NCP_IOC_NCPREQUEST, (unsigned long)&n);
	set_fs (old_fs);
        if(err <= 0)
		goto out;
	if (err > 65536) {
		err = -EINVAL;
		goto out;
	}
	if (copy_to_user((void *)A(n32.data), n.data, err)) {
		err = -EFAULT;
		goto out;
	}
 out:
	vfree(n.data);
	return err;
}

static int do_ncp_getmountuid2(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	__kernel_uid_t kuid;
	int err;

	cmd = NCP_IOC_GETMOUNTUID2;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&kuid);
	set_fs(old_fs);

	if (!err)
		err = put_user(kuid, (unsigned int*)arg);

	return err;
}

static int do_ncp_getfsinfo2(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct ncp_fs_info_v2_32 n32;
	struct ncp_fs_info_v2 n;
	int err;

	if (copy_from_user(&n32, (struct ncp_fs_info_v2_32*)arg, sizeof(n32)))
		return -EFAULT;
	if (n32.version != NCP_GET_FS_INFO_VERSION_V2)
		return -EINVAL;
	n.version = NCP_GET_FS_INFO_VERSION_V2;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, NCP_IOC_GET_FS_INFO_V2, (unsigned long)&n);
	set_fs(old_fs);

	if (!err) {
		n32.version = n.version;
		n32.mounted_uid = n.mounted_uid;
		n32.connection = n.connection;
		n32.buffer_size = n.buffer_size;
		n32.volume_number = n.volume_number;
		n32.directory_id = n.directory_id;
		n32.dummy1 = n.dummy1;
		n32.dummy2 = n.dummy2;
		n32.dummy3 = n.dummy3;
		err = copy_to_user((struct ncp_fs_info_v2_32*)arg, &n32, sizeof(n32)) ? -EFAULT : 0;
	}
	return err;
}

static int do_ncp_getobjectname(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_objectname_ioctl_32 n32;
	struct ncp_objectname_ioctl n;
	mm_segment_t old_fs;
	int err;
	size_t tl;

	if (copy_from_user(&n32, (struct ncp_objectname_ioctl_32*)arg,
	    sizeof(n32)))
		return -EFAULT;

	n.object_name_len = tl = n32.object_name_len;
	if (tl) {
		n.object_name = kmalloc(tl, GFP_KERNEL);
		if (!n.object_name)
			return -ENOMEM;
	} else {
		n.object_name = NULL;
	}

	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, NCP_IOC_GETOBJECTNAME, (unsigned long)&n);
	set_fs (old_fs);
        if(err)
		goto out;
		
	if (tl > n.object_name_len)
		tl = n.object_name_len;

	err = -EFAULT;
	if (tl && copy_to_user((void *)A(n32.object_name), n.object_name, tl))
		goto out;

	n32.auth_type = n.auth_type;
	n32.object_name_len = n.object_name_len;
	
	if (copy_to_user((struct ncp_objectname_ioctl_32*)arg, &n32, sizeof(n32)))
		goto out;
	
	err = 0;
 out:
 	if (n.object_name)
		kfree(n.object_name);

	return err;
}

static int do_ncp_setobjectname(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_objectname_ioctl_32 n32;
	struct ncp_objectname_ioctl n;
	mm_segment_t old_fs;
	int err;
	size_t tl;

	if (copy_from_user(&n32, (struct ncp_objectname_ioctl_32*)arg,
	    sizeof(n32)))
		return -EFAULT;

	n.auth_type = n32.auth_type;
	n.object_name_len = tl = n32.object_name_len;
	if (tl) {
		n.object_name = kmalloc(tl, GFP_KERNEL);
		if (!n.object_name)
			return -ENOMEM;
		err = -EFAULT;
		if (copy_from_user(n.object_name, (void *)A(n32.object_name), tl))
			goto out;
	} else {
		n.object_name = NULL;
	}
	
	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, NCP_IOC_SETOBJECTNAME, (unsigned long)&n);
	set_fs (old_fs);
		
 out:
	if (n.object_name)
		kfree(n.object_name);

	return err;
}

static int do_ncp_getprivatedata(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_privatedata_ioctl_32 n32;
	struct ncp_privatedata_ioctl n;
	mm_segment_t old_fs;
	int err;
	size_t tl;

	if (copy_from_user(&n32, (struct ncp_privatedata_ioctl_32*)arg,
	    sizeof(n32)))
		return -EFAULT;

	n.len = tl = n32.len;
	if (tl) {
		n.data = kmalloc(tl, GFP_KERNEL);
		if (!n.data)
			return -ENOMEM;
	} else {
		n.data = NULL;
	}

	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, NCP_IOC_GETPRIVATEDATA, (unsigned long)&n);
	set_fs (old_fs);
        if(err)
		goto out;
		
	if (tl > n.len)
		tl = n.len;

	err = -EFAULT;
	if (tl && copy_to_user((void *)A(n32.data), n.data, tl))
		goto out;

	n32.len = n.len;
	
	if (copy_to_user((struct ncp_privatedata_ioctl_32*)arg, &n32, sizeof(n32)))
		goto out;
	
	err = 0;
 out:
 	if (n.data)
		kfree(n.data);

	return err;
}

static int do_ncp_setprivatedata(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_privatedata_ioctl_32 n32;
	struct ncp_privatedata_ioctl n;
	mm_segment_t old_fs;
	int err;
	size_t tl;

	if (copy_from_user(&n32, (struct ncp_privatedata_ioctl_32*)arg,
	    sizeof(n32)))
		return -EFAULT;

	n.len = tl = n32.len;
	if (tl) {
		n.data = kmalloc(tl, GFP_KERNEL);
		if (!n.data)
			return -ENOMEM;
		err = -EFAULT;
		if (copy_from_user(n.data, (void *)A(n32.data), tl))
			goto out;
	} else {
		n.data = NULL;
	}
	
	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, NCP_IOC_SETPRIVATEDATA, (unsigned long)&n);
	set_fs (old_fs);
		
 out:
	if (n.data)
		kfree(n.data);

	return err;
}

struct usbdevfs_ctrltransfer32 {
	__u8 bRequestType;
	__u8 bRequest;
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;
	__u32 timeout;  /* in milliseconds */
	__u32 data;
};

#define USBDEVFS_CONTROL32           _IOWR('U', 0, struct usbdevfs_ctrltransfer32)

static int do_usbdevfs_control(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct usbdevfs_ctrltransfer kctrl;
	struct usbdevfs_ctrltransfer32 *uctrl;
	mm_segment_t old_fs;
	__u32 udata;
	void *uptr, *kptr;
	int err;

	uctrl = (struct usbdevfs_ctrltransfer32 *) arg;

	if (copy_from_user(&kctrl, uctrl,
			   (sizeof(struct usbdevfs_ctrltransfer) -
			    sizeof(void *))))
		return -EFAULT;

	if (get_user(udata, &uctrl->data))
		return -EFAULT;
	uptr = (void *) A(udata);

	/* In usbdevice_fs, it limits the control buffer to a page,
	 * for simplicity so do we.
	 */
	if (!uptr || kctrl.wLength > PAGE_SIZE)
		return -EINVAL;

	kptr = (void *)__get_free_page(GFP_KERNEL);

	if ((kctrl.bRequestType & 0x80) == 0) {
		err = -EFAULT;
		if (copy_from_user(kptr, uptr, kctrl.wLength))
			goto out;
	}

	kctrl.data = kptr;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, USBDEVFS_CONTROL, (unsigned long)&kctrl);
	set_fs(old_fs);

	if (err >= 0 &&
	    ((kctrl.bRequestType & 0x80) != 0)) {
		if (copy_to_user(uptr, kptr, kctrl.wLength))
			err = -EFAULT;
	}

out:
	free_page((unsigned long) kptr);
	return err;
}

struct usbdevfs_bulktransfer32 {
	unsigned int ep;
	unsigned int len;
	unsigned int timeout; /* in milliseconds */
	__u32 data;
};

#define USBDEVFS_BULK32              _IOWR('U', 2, struct usbdevfs_bulktransfer32)

static int do_usbdevfs_bulk(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct usbdevfs_bulktransfer kbulk;
	struct usbdevfs_bulktransfer32 *ubulk;
	mm_segment_t old_fs;
	__u32 udata;
	void *uptr, *kptr;
	int err;

	ubulk = (struct usbdevfs_bulktransfer32 *) arg;

	if (get_user(kbulk.ep, &ubulk->ep) ||
	    get_user(kbulk.len, &ubulk->len) ||
	    get_user(kbulk.timeout, &ubulk->timeout) ||
	    get_user(udata, &ubulk->data))
		return -EFAULT;

	uptr = (void *) A(udata);

	/* In usbdevice_fs, it limits the control buffer to a page,
	 * for simplicity so do we.
	 */
	if (!uptr || kbulk.len > PAGE_SIZE)
		return -EINVAL;

	kptr = (void *) __get_free_page(GFP_KERNEL);

	if ((kbulk.ep & 0x80) == 0) {
		err = -EFAULT;
		if (copy_from_user(kptr, uptr, kbulk.len))
			goto out;
	}

	kbulk.data = kptr;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, USBDEVFS_BULK, (unsigned long) &kbulk);
	set_fs(old_fs);

	if (err >= 0 &&
	    ((kbulk.ep & 0x80) != 0)) {
		if (copy_to_user(uptr, kptr, kbulk.len))
			err = -EFAULT;
	}

out:
	free_page((unsigned long) kptr);
	return err;
}

/* This needs more work before we can enable it.  Unfortunately
 * because of the fancy asynchronous way URB status/error is written
 * back to userspace, we'll need to fiddle with USB devio internals
 * and/or reimplement entirely the frontend of it ourselves. -DaveM
 *
 * The issue is:
 *
 *	When an URB is submitted via usbdevicefs it is put onto an
 *	asynchronous queue.  When the URB completes, it may be reaped
 *	via another ioctl.  During this reaping the status is written
 *	back to userspace along with the length of the transfer.
 *
 *	We must translate into 64-bit kernel types so we pass in a kernel
 *	space copy of the usbdevfs_urb structure.  This would mean that we
 *	must do something to deal with the async entry reaping.  First we
 *	have to deal somehow with this transitory memory we've allocated.
 *	This is problematic since there are many call sites from which the
 *	async entries can be destroyed (and thus when we'd need to free up
 *	this kernel memory).  One of which is the close() op of usbdevicefs.
 *	To handle that we'd need to make our own file_operations struct which
 *	overrides usbdevicefs's release op with our own which runs usbdevicefs's
 *	real release op then frees up the kernel memory.
 *
 *	But how to keep track of these kernel buffers?  We'd need to either
 *	keep track of them in some table _or_ know about usbdevicefs internals
 *	(ie. the exact layout of its file private, which is actually defined
 *	in linux/usbdevice_fs.h, the layout of the async queues are private to
 *	devio.c)
 *
 * There is one possible other solution I considered, also involving knowledge
 * of usbdevicefs internals:
 *
 *	After an URB is submitted, we "fix up" the address back to the user
 *	space one.  This would work if the status/length fields written back
 *	by the async URB completion lines up perfectly in the 32-bit type with
 *	the 64-bit kernel type.  Unfortunately, it does not because the iso
 *	frame descriptors, at the end of the struct, can be written back.
 *
 * I think we'll just need to simply duplicate the devio URB engine here.
 */
#if 0
struct usbdevfs_urb32 {
	__u8 type;
	__u8 endpoint;
	__s32 status;
	__u32 flags;
	__u32 buffer;
	__s32 buffer_length;
	__s32 actual_length;
	__s32 start_frame;
	__s32 number_of_packets;
	__s32 error_count;
	__u32 signr;
	__u32 usercontext; /* unused */
	struct usbdevfs_iso_packet_desc iso_frame_desc[0];
};

#define USBDEVFS_SUBMITURB32       _IOR('U', 10, struct usbdevfs_urb32)

static int get_urb32(struct usbdevfs_urb *kurb,
		     struct usbdevfs_urb32 *uurb)
{
	if (get_user(kurb->type, &uurb->type) ||
	    __get_user(kurb->endpoint, &uurb->endpoint) ||
	    __get_user(kurb->status, &uurb->status) ||
	    __get_user(kurb->flags, &uurb->flags) ||
	    __get_user(kurb->buffer_length, &uurb->buffer_length) ||
	    __get_user(kurb->actual_length, &uurb->actual_length) ||
	    __get_user(kurb->start_frame, &uurb->start_frame) ||
	    __get_user(kurb->number_of_packets, &uurb->number_of_packets) ||
	    __get_user(kurb->error_count, &uurb->error_count) ||
	    __get_user(kurb->signr, &uurb->signr))
		return -EFAULT;

	kurb->usercontext = 0; /* unused currently */

	return 0;
}

/* Just put back the values which usbdevfs actually changes. */
static int put_urb32(struct usbdevfs_urb *kurb,
		     struct usbdevfs_urb32 *uurb)
{
	if (put_user(kurb->status, &uurb->status) ||
	    __put_user(kurb->actual_length, &uurb->actual_length) ||
	    __put_user(kurb->error_count, &uurb->error_count))
		return -EFAULT;

	if (kurb->number_of_packets != 0) {
		int i;

		for (i = 0; i < kurb->number_of_packets; i++) {
			if (__put_user(kurb->iso_frame_desc[i].actual_length,
				       &uurb->iso_frame_desc[i].actual_length) ||
			    __put_user(kurb->iso_frame_desc[i].status,
				       &uurb->iso_frame_desc[i].status))
				return -EFAULT;
		}
	}

	return 0;
}

static int get_urb32_isoframes(struct usbdevfs_urb *kurb,
			       struct usbdevfs_urb32 *uurb)
{
	unsigned int totlen;
	int i;

	if (kurb->type != USBDEVFS_URB_TYPE_ISO) {
		kurb->number_of_packets = 0;
		return 0;
	}

	if (kurb->number_of_packets < 1 ||
	    kurb->number_of_packets > 128)
		return -EINVAL;

	if (copy_from_user(&kurb->iso_frame_desc[0],
			   &uurb->iso_frame_desc[0],
			   sizeof(struct usbdevfs_iso_packet_desc) *
			   kurb->number_of_packets))
		return -EFAULT;

	totlen = 0;
	for (i = 0; i < kurb->number_of_packets; i++) {
		unsigned int this_len;

		this_len = kurb->iso_frame_desc[i].length;
		if (this_len > 1023)
			return -EINVAL;

		totlen += this_len;
	}

	if (totlen > 32768)
		return -EINVAL;

	kurb->buffer_length = totlen;

	return 0;
}

static int do_usbdevfs_urb(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct usbdevfs_urb *kurb;
	struct usbdevfs_urb32 *uurb;
	mm_segment_t old_fs;
	__u32 udata;
	void *uptr, *kptr;
	unsigned int buflen;
	int err;

	uurb = (struct usbdevfs_urb32 *) arg;

	err = -ENOMEM;
	kurb = kmalloc(sizeof(struct usbdevfs_urb) +
		       (sizeof(struct usbdevfs_iso_packet_desc) * 128),
		       GFP_KERNEL);
	if (!kurb)
		goto out;

	err = -EFAULT;
	if (get_urb32(kurb, uurb))
		goto out;

	err = get_urb32_isoframes(kurb, uurb);
	if (err)
		goto out;

	err = -EFAULT;
	if (__get_user(udata, &uurb->buffer))
		goto out;
	uptr = (void *) A(udata);

	err = -ENOMEM;
	buflen = kurb->buffer_length;
	kptr = kmalloc(buflen, GFP_KERNEL);
	if (!kptr)
		goto out;

	kurb->buffer = kptr;

	err = -EFAULT;
	if (copy_from_user(kptr, uptr, buflen))
		goto out_kptr;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, USBDEVFS_SUBMITURB, (unsigned long) kurb);
	set_fs(old_fs);

	if (err >= 0) {
		/* XXX Shit, this doesn't work for async URBs :-( XXX */
		if (put_urb32(kurb, uurb)) {
			err = -EFAULT;
		} else if ((kurb->endpoint & USB_DIR_IN) != 0) {
			if (copy_to_user(uptr, kptr, buflen))
				err = -EFAULT;
		}
	}

out_kptr:
	kfree(kptr);

out:
	kfree(kurb);
	return err;
}
#endif

#define USBDEVFS_REAPURB32         _IOW('U', 12, u32)
#define USBDEVFS_REAPURBNDELAY32   _IOW('U', 13, u32)

static int do_usbdevfs_reapurb(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs;
	void *kptr;
	int err;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd,
			(cmd == USBDEVFS_REAPURB32 ?
			 USBDEVFS_REAPURB :
			 USBDEVFS_REAPURBNDELAY),
			(unsigned long) &kptr);
	set_fs(old_fs);

	if (err >= 0 &&
	    put_user(((u32)(long)kptr), (u32 *) A(arg)))
		err = -EFAULT;

	return err;
}

struct usbdevfs_disconnectsignal32 {
	unsigned int signr;
	u32 context;
};

#define USBDEVFS_DISCSIGNAL32      _IOR('U', 14, struct usbdevfs_disconnectsignal32)

static int do_usbdevfs_discsignal(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct usbdevfs_disconnectsignal kdis;
	struct usbdevfs_disconnectsignal32 *udis;
	mm_segment_t old_fs;
	u32 uctx;
	int err;

	udis = (struct usbdevfs_disconnectsignal32 *) arg;

	if (get_user(kdis.signr, &udis->signr) ||
	    __get_user(uctx, &udis->context))
		return -EFAULT;

	kdis.context = (void *) (long)uctx;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, USBDEVFS_DISCSIGNAL, (unsigned long) &kdis);
	set_fs(old_fs);

	return err;
}

#define HANDLE_IOCTL(cmd,handler) { cmd, (ioctl_trans_handler_t)handler, 0 },
#define COMPATIBLE_IOCTL(cmd) HANDLE_IOCTL(cmd,sys_ioctl)

#define IOCTL_TABLE_START \
	struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END \
	}; struct ioctl_trans ioctl_end[0];

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>
#define DECLARES
#include "compat_ioctl.c"
COMPATIBLE_IOCTL(TCSBRKP)
COMPATIBLE_IOCTL(TIOCSTART)
COMPATIBLE_IOCTL(TIOCSTOP)
COMPATIBLE_IOCTL(TIOCGSERIAL)
COMPATIBLE_IOCTL(TIOCSSERIAL)
COMPATIBLE_IOCTL(TIOCSLTC)
#if 0
COMPATIBLE_IOCTL(FBIOBLANK)
#endif
/* Little p (/dev/rtc, /dev/envctrl, etc.) */
COMPATIBLE_IOCTL(_IOR('p', 20, int[7])) /* RTCGET */
COMPATIBLE_IOCTL(_IOW('p', 21, int[7])) /* RTCSET */

/* And these ioctls need translation */
HANDLE_IOCTL(HDIO_GETGEO_BIG_RAW, hdio_getgeo_big)

/* NCPFS */
HANDLE_IOCTL(NCP_IOC_NCPREQUEST_32, do_ncp_ncprequest)
HANDLE_IOCTL(NCP_IOC_GETMOUNTUID2_32, do_ncp_getmountuid2)
HANDLE_IOCTL(NCP_IOC_GET_FS_INFO_V2_32, do_ncp_getfsinfo2)
HANDLE_IOCTL(NCP_IOC_GETOBJECTNAME_32, do_ncp_getobjectname)
HANDLE_IOCTL(NCP_IOC_SETOBJECTNAME_32, do_ncp_setobjectname)
HANDLE_IOCTL(NCP_IOC_GETPRIVATEDATA_32, do_ncp_getprivatedata)
HANDLE_IOCTL(NCP_IOC_SETPRIVATEDATA_32, do_ncp_setprivatedata)

/* USB devfs */
HANDLE_IOCTL(USBDEVFS_CONTROL32, do_usbdevfs_control)
HANDLE_IOCTL(USBDEVFS_BULK32, do_usbdevfs_bulk)
/*HANDLE_IOCTL(USBDEVFS_SUBMITURB32, do_usbdevfs_urb)*/
HANDLE_IOCTL(USBDEVFS_REAPURB32, do_usbdevfs_reapurb)
HANDLE_IOCTL(USBDEVFS_REAPURBNDELAY32, do_usbdevfs_reapurb)
HANDLE_IOCTL(USBDEVFS_DISCSIGNAL32, do_usbdevfs_discsignal)
IOCTL_TABLE_END
