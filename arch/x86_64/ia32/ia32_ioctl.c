/* $Id: ia32_ioctl.c,v 1.25 2002/10/11 07:17:06 ak Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#define INCLUDES
#include "compat_ioctl.c"
#include <asm/mtrr.h>
#include <asm/ia32.h>

extern asmlinkage long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

#define CODE
#include "compat_ioctl.c"
  
#ifndef TIOCGDEV
#define TIOCGDEV       _IOR('T',0x32, unsigned int)
#endif
static int tiocgdev(unsigned fd, unsigned cmd,  unsigned int *ptr) 
{ 

	struct file *file = fget(fd);
	struct tty_struct *real_tty;

	if (!fd)
		return -EBADF;
	if (file->f_op->ioctl != tty_ioctl)
		return -EINVAL; 
	real_tty = (struct tty_struct *)file->private_data;
	if (!real_tty) 	
		return -EINVAL; 
	return put_user(real_tty->device, ptr); 
} 


struct raw32_config_request 
{
	compat_int_t	raw_minor;
	__u64	block_major;
	__u64	block_minor;
} __attribute__((packed));

static int raw_ioctl(unsigned fd, unsigned cmd,  void *ptr) 
{ 
	int ret;
	switch (cmd) { 
	case RAW_SETBIND:
	case RAW_GETBIND: {
		struct raw_config_request req; 
		struct raw32_config_request *user_req = ptr;
		mm_segment_t oldfs = get_fs(); 

		if (get_user(req.raw_minor, &user_req->raw_minor) ||
		    get_user(req.block_major, &user_req->block_major) ||
		    get_user(req.block_minor, &user_req->block_minor))
			return -EFAULT;
		set_fs(KERNEL_DS); 
		ret = sys_ioctl(fd,cmd,(unsigned long)&req); 
		set_fs(oldfs); 
		break;
	}
	default:
		ret = sys_ioctl(fd,cmd,(unsigned long)ptr);
		break;
	} 
	return ret; 		
} 


#define REISERFS_IOC_UNPACK32               _IOW(0xCD,1,int)

static int reiserfs_ioctl32(unsigned fd, unsigned cmd, unsigned long ptr) 
{ 
	if (cmd == REISERFS_IOC_UNPACK32) 
		cmd = REISERFS_IOC_UNPACK; 
	return sys_ioctl(fd,cmd,ptr); 
} 

struct dirent32 {
	compat_int_t	d_ino;
	compat_off_t	d_off;
	unsigned short	d_reclen;
	char		d_name[256]; /* We must not include limits.h! */
};

#define	VFAT_IOCTL_READDIR_BOTH32	_IOR('r', 1, struct dirent32 [2])
#define	VFAT_IOCTL_READDIR_SHORT32	_IOR('r', 2, struct dirent32 [2])

static int put_dirent32(struct dirent *src, struct dirent32 *dst)
{
	int ret; 
	ret = put_user(src->d_ino, &dst->d_ino); 
	ret |= __put_user(src->d_off, &dst->d_off); 
	ret |= __put_user(src->d_reclen, &dst->d_reclen); 
	if (__copy_to_user(&dst->d_name, src->d_name, src->d_reclen))
		ret |= -EFAULT;
	return ret;
} 

static int vfat_ioctl32(unsigned fd, unsigned cmd,  void *ptr) 
{
	int ret;
	mm_segment_t oldfs = get_fs();
	struct dirent d[2]; 

	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd,cmd,(unsigned long)&d); 
	set_fs(oldfs); 
	if (!ret) { 
		ret |= put_dirent32(&d[0], (struct dirent32 *)ptr); 
		ret |= put_dirent32(&d[1], ((struct dirent32 *)ptr) + 1); 
	}
	return ret; 
} 

#define RTC_IRQP_READ32	_IOR('p', 0x0b, unsigned int)	 /* Read IRQ rate   */
#define RTC_IRQP_SET32	_IOW('p', 0x0c, unsigned int)	 /* Set IRQ rate    */
#define RTC_EPOCH_READ32	_IOR('p', 0x0d, unsigned)	 /* Read epoch      */
#define RTC_EPOCH_SET32		_IOW('p', 0x0e, unsigned)	 /* Set epoch       */

static int rtc32_ioctl(unsigned fd, unsigned cmd, unsigned long arg) 
{ 
	unsigned long val;
	mm_segment_t oldfs = get_fs(); 
	int ret; 
	
	switch (cmd) { 
	case RTC_IRQP_READ32: 
		set_fs(KERNEL_DS); 
		ret = sys_ioctl(fd, RTC_IRQP_READ, (unsigned long)&val); 
		set_fs(oldfs); 
		if (!ret)
			ret = put_user(val, (unsigned int*) arg); 
		return ret; 

	case RTC_IRQP_SET32: 
		cmd = RTC_EPOCH_SET; 
		break; 

	case RTC_EPOCH_READ32:
		set_fs(KERNEL_DS); 
		ret = sys_ioctl(fd, RTC_EPOCH_READ, (unsigned long) &val); 
		set_fs(oldfs); 
		if (!ret)
			ret = put_user(val, (unsigned int*) arg); 
		return ret; 

	case RTC_EPOCH_SET32:
		cmd = RTC_EPOCH_SET; 
		break; 
	} 
	return sys_ioctl(fd,cmd,arg); 
} 

struct serial_struct32 {
	compat_int_t	type;
	compat_int_t	line;
	compat_uint_t	port;
	compat_int_t	irq;
	compat_int_t	flags;
	compat_int_t	xmit_fifo_size;
	compat_int_t	custom_divisor;
	compat_int_t	baud_base;
	unsigned short	close_delay;
	char	io_type;
	char	reserved_char[1];
	compat_int_t	hub6;
	unsigned short	closing_wait; /* time to wait before closing */
	unsigned short	closing_wait2; /* no longer used... */
	compat_uint_t	iomem_base;
	unsigned short	iomem_reg_shift;
	unsigned int	port_high;
	compat_int_t	reserved[1];
};

static int serial_struct_ioctl(unsigned fd, unsigned cmd,  void *ptr) 
{
	typedef struct serial_struct SS;
	struct serial_struct32 *ss32 = ptr; 
	int err;
	struct serial_struct ss; 
	mm_segment_t oldseg = get_fs(); 
	if (cmd == TIOCSSERIAL) { 
		if (copy_from_user(&ss, ss32, sizeof(struct serial_struct32)))
			return -EFAULT;
		memmove(&ss.iomem_reg_shift, ((char*)&ss.iomem_base)+4, 
			sizeof(SS)-offsetof(SS,iomem_reg_shift)); 
		ss.iomem_base = (void *)((unsigned long)ss.iomem_base & 0xffffffff);
	}
	set_fs(KERNEL_DS);
		err = sys_ioctl(fd,cmd,(unsigned long)(&ss)); 
	set_fs(oldseg);
	if (cmd == TIOCGSERIAL && err >= 0) { 
		if (__copy_to_user(ss32,&ss,offsetof(SS,iomem_base)) ||
		    __put_user((unsigned long)ss.iomem_base  >> 32 ? 
			       0xffffffff : (unsigned)(unsigned long)ss.iomem_base,
			       &ss32->iomem_base) ||
		    __put_user(ss.iomem_reg_shift, &ss32->iomem_reg_shift) ||
		    __put_user(ss.port_high, &ss32->port_high))
			return -EFAULT;
	} 
	return err;	
}



struct usbdevfs_ctrltransfer32 {
	u8 bRequestType;
	u8 bRequest;
	u16 wValue;
	u16 wIndex;
	u16 wLength;
	u32 timeout;  /* in milliseconds */
	compat_caddr_t data;
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
	uptr = compat_ptr(udata);

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
	compat_uint_t ep;
	compat_uint_t len;
	compat_uint_t timeout; /* in milliseconds */
	compat_caddr_t data;
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

	uptr = compat_ptr(udata);

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
	unsigned char type;
	unsigned char endpoint;
	compat_int_t status;
	compat_uint_t flags;
	compat_caddr_t buffer;
	compat_int_t buffer_length;
	compat_int_t actual_length;
	compat_int_t start_frame;
	compat_int_t number_of_packets;
	compat_int_t error_count;
	compat_uint_t signr;
	compat_caddr_t usercontext; /* unused */
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
	uptr = compat_ptr(udata);

	buflen = kurb->buffer_length;
	err = verify_area(VERIFY_WRITE, uptr, buflen);
	if (err) 
		goto out;


	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, USBDEVFS_SUBMITURB, (unsigned long) kurb);
	set_fs(old_fs);

	if (err >= 0) {
		/* RED-PEN Shit, this doesn't work for async URBs :-( XXX */
		if (put_urb32(kurb, uurb)) {
			err = -EFAULT;
		}
	}

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
	    put_user(((u32)(long)kptr), compat_ptr(arg)))
		err = -EFAULT;

	return err;
}

struct usbdevfs_disconnectsignal32 {
	compat_int_t signr;
	compat_caddr_t context;
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
/* /proc/mtrr ioctls */


struct mtrr_sentry32
{
    compat_ulong_t base;    /*  Base address     */
    compat_uint_t size;    /*  Size of region   */
    compat_uint_t type;     /*  Type of region   */
};

struct mtrr_gentry32
{
    compat_ulong_t regnum;   /*  Register number  */
    compat_uint_t base;    /*  Base address     */
    compat_uint_t size;    /*  Size of region   */
    compat_uint_t type;     /*  Type of region   */
};

#define	MTRR_IOCTL_BASE	'M'

#define MTRRIOC32_ADD_ENTRY        _IOW(MTRR_IOCTL_BASE,  0, struct mtrr_sentry32)
#define MTRRIOC32_SET_ENTRY        _IOW(MTRR_IOCTL_BASE,  1, struct mtrr_sentry32)
#define MTRRIOC32_DEL_ENTRY        _IOW(MTRR_IOCTL_BASE,  2, struct mtrr_sentry32)
#define MTRRIOC32_GET_ENTRY        _IOWR(MTRR_IOCTL_BASE, 3, struct mtrr_gentry32)
#define MTRRIOC32_KILL_ENTRY       _IOW(MTRR_IOCTL_BASE,  4, struct mtrr_sentry32)
#define MTRRIOC32_ADD_PAGE_ENTRY   _IOW(MTRR_IOCTL_BASE,  5, struct mtrr_sentry32)
#define MTRRIOC32_SET_PAGE_ENTRY   _IOW(MTRR_IOCTL_BASE,  6, struct mtrr_sentry32)
#define MTRRIOC32_DEL_PAGE_ENTRY   _IOW(MTRR_IOCTL_BASE,  7, struct mtrr_sentry32)
#define MTRRIOC32_GET_PAGE_ENTRY   _IOWR(MTRR_IOCTL_BASE, 8, struct mtrr_gentry32)
#define MTRRIOC32_KILL_PAGE_ENTRY  _IOW(MTRR_IOCTL_BASE,  9, struct mtrr_sentry32)


static int mtrr_ioctl32(unsigned int fd, unsigned int cmd, unsigned long arg)
{ 
	struct mtrr_gentry g;
	struct mtrr_sentry s;
	int get = 0, err = 0; 
	struct mtrr_gentry32 *g32 = (struct mtrr_gentry32 *)arg; 
	mm_segment_t oldfs = get_fs(); 

	switch (cmd) { 
#define SET(x) case MTRRIOC32_ ## x ## _ENTRY: cmd = MTRRIOC_ ## x ## _ENTRY; break 
#define GET(x) case MTRRIOC32_ ## x ## _ENTRY: cmd = MTRRIOC_ ## x ## _ENTRY; get=1; break
		SET(ADD);
		SET(SET); 
		SET(DEL);
		GET(GET); 
		SET(KILL);
		SET(ADD_PAGE); 
		SET(SET_PAGE); 
		SET(DEL_PAGE); 
		GET(GET_PAGE); 
		SET(KILL_PAGE); 
	} 
	
	if (get) { 
		err = get_user(g.regnum, &g32->regnum);
		err |= get_user(g.base, &g32->base);
		err |= get_user(g.size, &g32->size);
		err |= get_user(g.type, &g32->type); 

		arg = (unsigned long)&g; 
	} else { 
		struct mtrr_sentry32 *s32 = (struct mtrr_sentry32 *)arg;
		err = get_user(s.base, &s32->base);
		err |= get_user(s.size, &s32->size);
		err |= get_user(s.type, &s32->type);

		arg = (unsigned long)&s; 
	} 
	if (err) return err;
	
	set_fs(KERNEL_DS); 
	err = sys_ioctl(fd, cmd, arg); 
	set_fs(oldfs); 
		
	if (!err && get) { 
		err = put_user(g.base, &g32->base);
		err |= put_user(g.size, &g32->size);
		err |= put_user(g.regnum, &g32->regnum);
		err |= put_user(g.type, &g32->type); 
	} 
	return err;
} 

#define REF_SYMBOL(handler) if (0) (void)handler;
#define HANDLE_IOCTL2(cmd,handler) REF_SYMBOL(handler);  asm volatile(".quad %P0, " #handler ",0"::"i" (cmd)); 
#define HANDLE_IOCTL(cmd,handler) HANDLE_IOCTL2(cmd,handler)
#define COMPATIBLE_IOCTL(cmd) HANDLE_IOCTL(cmd,sys_ioctl)
#define IOCTL_TABLE_START void ioctl_dummy(void) { asm volatile("\n.global ioctl_start\nioctl_start:\n\t" );
#define IOCTL_TABLE_END  asm volatile("\n.global ioctl_end;\nioctl_end:\n"); }

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>
#define DECLARES
#include "compat_ioctl.c"
COMPATIBLE_IOCTL(HDIO_SET_KEEPSETTINGS)
COMPATIBLE_IOCTL(HDIO_SCAN_HWIF)
COMPATIBLE_IOCTL(BLKRASET)
COMPATIBLE_IOCTL(BLKFRASET)
COMPATIBLE_IOCTL(0x4B50)   /* KDGHWCLK - not in the kernel, but don't complain */
COMPATIBLE_IOCTL(0x4B51)   /* KDSHWCLK - not in the kernel, but don't complain */
#ifdef CONFIG_AUTOFS_FS
COMPATIBLE_IOCTL(AUTOFS_IOC_READY)
COMPATIBLE_IOCTL(AUTOFS_IOC_FAIL)
COMPATIBLE_IOCTL(AUTOFS_IOC_CATATONIC)
COMPATIBLE_IOCTL(AUTOFS_IOC_PROTOVER)
COMPATIBLE_IOCTL(AUTOFS_IOC_SETTIMEOUT)
COMPATIBLE_IOCTL(AUTOFS_IOC_EXPIRE)
COMPATIBLE_IOCTL(AUTOFS_IOC_EXPIRE_MULTI)
#endif
#ifdef CONFIG_RTC
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
#endif
COMPATIBLE_IOCTL(HCIUARTSETPROTO)
COMPATIBLE_IOCTL(HCIUARTGETPROTO)
COMPATIBLE_IOCTL(RFCOMMCREATEDEV)
COMPATIBLE_IOCTL(RFCOMMRELEASEDEV)
COMPATIBLE_IOCTL(RFCOMMGETDEVLIST)
COMPATIBLE_IOCTL(RFCOMMGETDEVINFO)
COMPATIBLE_IOCTL(RFCOMMSTEALDLC)
COMPATIBLE_IOCTL(BNEPCONNADD)
COMPATIBLE_IOCTL(BNEPCONNDEL)
COMPATIBLE_IOCTL(BNEPGETCONNLIST)
COMPATIBLE_IOCTL(BNEPGETCONNINFO)

/* And these ioctls need translation */
HANDLE_IOCTL(TIOCGDEV, tiocgdev)
HANDLE_IOCTL(TIOCGSERIAL, serial_struct_ioctl)
HANDLE_IOCTL(TIOCSSERIAL, serial_struct_ioctl)
/* Raw devices */
HANDLE_IOCTL(RAW_SETBIND, raw_ioctl)
/* realtime device */
HANDLE_IOCTL(RTC_IRQP_READ,  rtc32_ioctl)
HANDLE_IOCTL(RTC_IRQP_READ32,rtc32_ioctl)
HANDLE_IOCTL(RTC_IRQP_SET32, rtc32_ioctl)
HANDLE_IOCTL(RTC_EPOCH_READ32, rtc32_ioctl)
HANDLE_IOCTL(RTC_EPOCH_SET32, rtc32_ioctl)
HANDLE_IOCTL(REISERFS_IOC_UNPACK32, reiserfs_ioctl32)
/* VFAT */
HANDLE_IOCTL(VFAT_IOCTL_READDIR_BOTH32, vfat_ioctl32)
HANDLE_IOCTL(VFAT_IOCTL_READDIR_SHORT32, vfat_ioctl32)

HANDLE_IOCTL(USBDEVFS_CONTROL32, do_usbdevfs_control)
HANDLE_IOCTL(USBDEVFS_BULK32, do_usbdevfs_bulk)
/*HANDLE_IOCTL(USBDEVFS_SUBMITURB32, do_usbdevfs_urb)*/
HANDLE_IOCTL(USBDEVFS_REAPURB32, do_usbdevfs_reapurb)
HANDLE_IOCTL(USBDEVFS_REAPURBNDELAY32, do_usbdevfs_reapurb)
HANDLE_IOCTL(USBDEVFS_DISCSIGNAL32, do_usbdevfs_discsignal)
/* take care of sizeof(sizeof()) breakage */
/* mtrr */
HANDLE_IOCTL(MTRRIOC32_ADD_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_SET_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_DEL_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_GET_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_KILL_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_ADD_PAGE_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_SET_PAGE_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_DEL_PAGE_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_GET_PAGE_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_KILL_PAGE_ENTRY, mtrr_ioctl32)
IOCTL_TABLE_END

