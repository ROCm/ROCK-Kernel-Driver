/*
 * IA32 Architecture-specific ioctl shim code
 *
 * Copyright (C) 2000 VA Linux Co
 * Copyright (C) 2000 Don Dugger <n0ano@valinux.com>
 * Copyright (C) 2001-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/fs.h>		/* argh, msdos_fs.h isn't self-contained... */

#include <linux/msdos_fs.h>
#include <linux/mtio.h>
#include <linux/ncp_fs.h>
#include <linux/capi.h>
#include <linux/videodev.h>
#include <linux/synclink.h>
#include <linux/atmdev.h>
#include <linux/atm_eni.h>
#include <linux/atm_nicstar.h>
#include <linux/atm_zatm.h>
#include <linux/atm_idt77105.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/ixjuser.h>
#include <linux/i2o-dev.h>
#include <scsi/scsi.h>
/* Ugly hack. */
#undef	__KERNEL__
#include <scsi/scsi_ioctl.h>
#define	__KERNEL__
#include <scsi/sg.h>

#include <asm/ia32.h>

#include <../drivers/char/drm/drm.h>
#include <../drivers/char/drm/mga_drm.h>
#include <../drivers/char/drm/i810_drm.h>


#define IOCTL_NR(a)	((a) & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT))

#define DO_IOCTL(fd, cmd, arg) ({			\
	int _ret;					\
	mm_segment_t _old_fs = get_fs();		\
							\
	set_fs(KERNEL_DS);				\
	_ret = sys_ioctl(fd, cmd, (unsigned long)arg);	\
	set_fs(_old_fs);				\
	_ret;						\
})

#define P(i)	((void *)(unsigned long)(i))

asmlinkage long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

static long
put_dirent32 (struct dirent *d, struct linux32_dirent *d32)
{
	size_t namelen = strlen(d->d_name);

	return (put_user(d->d_ino, &d32->d_ino)
		|| put_user(d->d_off, &d32->d_off)
		|| put_user(d->d_reclen, &d32->d_reclen)
		|| copy_to_user(d32->d_name, d->d_name, namelen + 1));
}
/*
 *  The transform code for the SG_IO ioctl was brazenly lifted from
 *  the Sparc64 port in the file `arch/sparc64/kernel/ioctl32.c'.
 *  Thanks to Jakub Jelinek & Eddie C. Dost.
 */
typedef struct sg_io_hdr32 {
	int interface_id;	/* [i] 'S' for SCSI generic (required) */
	int dxfer_direction;	/* [i] data transfer direction  */
	char  cmd_len;		/* [i] SCSI command length ( <= 16 bytes) */
	char  mx_sb_len;		/* [i] max length to write to sbp */
	short iovec_count;	/* [i] 0 implies no scatter gather */
	int dxfer_len;		/* [i] byte count of data transfer */
	int dxferp;		/* [i], [*io] points to data transfer memory
					      or scatter gather list */
	int cmdp;		/* [i], [*i] points to command to perform */
	int sbp;		/* [i], [*o] points to sense_buffer memory */
	int timeout;		/* [i] MAX_UINT->no timeout (unit: millisec) */
	int flags;		/* [i] 0 -> default, see SG_FLAG... */
	int pack_id;		/* [i->o] unused internally (normally) */
	int usr_ptr;		/* [i->o] unused internally */
	char  status;		/* [o] scsi status */
	char  masked_status;	/* [o] shifted, masked scsi status */
	char  msg_status;	/* [o] messaging level data (optional) */
	char  sb_len_wr;	/* [o] byte count actually written to sbp */
	short host_status;	/* [o] errors from host adapter */
	short driver_status;	/* [o] errors from software driver */
	int resid;		/* [o] dxfer_len - actual_transferred */
	int duration;		/* [o] time taken by cmd (unit: millisec) */
	int info;		/* [o] auxiliary information */
} sg_io_hdr32_t;  /* 64 bytes long (on IA32) */

static int alloc_sg_iovec(sg_io_hdr_t *sgp, int uptr32)
{
	struct compat_iovec *uiov = (struct compat_iovec *) P(uptr32);
	sg_iovec_t *kiov;
	int i;

	sgp->dxferp = kmalloc(sgp->iovec_count *
			      sizeof(sg_iovec_t), GFP_KERNEL);
	if (!sgp->dxferp)
		return -ENOMEM;
	memset(sgp->dxferp, 0,
	       sgp->iovec_count * sizeof(sg_iovec_t));

	kiov = (sg_iovec_t *) sgp->dxferp;
	for (i = 0; i < sgp->iovec_count; i++) {
		int iov_base32;
		if (__get_user(iov_base32, &uiov->iov_base) ||
		    __get_user(kiov->iov_len, &uiov->iov_len))
			return -EFAULT;

		kiov->iov_base = kmalloc(kiov->iov_len, GFP_KERNEL);
		if (!kiov->iov_base)
			return -ENOMEM;
		if (copy_from_user(kiov->iov_base,
				   (void *) P(iov_base32),
				   kiov->iov_len))
			return -EFAULT;

		uiov++;
		kiov++;
	}

	return 0;
}

static int copy_back_sg_iovec(sg_io_hdr_t *sgp, int uptr32)
{
	struct compat_iovec *uiov = (struct compat_iovec *) P(uptr32);
	sg_iovec_t *kiov = (sg_iovec_t *) sgp->dxferp;
	int i;

	for (i = 0; i < sgp->iovec_count; i++) {
		int iov_base32;

		if (__get_user(iov_base32, &uiov->iov_base))
			return -EFAULT;

		if (copy_to_user((void *) P(iov_base32),
				 kiov->iov_base,
				 kiov->iov_len))
			return -EFAULT;

		uiov++;
		kiov++;
	}

	return 0;
}

static void free_sg_iovec(sg_io_hdr_t *sgp)
{
	sg_iovec_t *kiov = (sg_iovec_t *) sgp->dxferp;
	int i;

	for (i = 0; i < sgp->iovec_count; i++) {
		if (kiov->iov_base) {
			kfree(kiov->iov_base);
			kiov->iov_base = NULL;
		}
		kiov++;
	}
	kfree(sgp->dxferp);
	sgp->dxferp = NULL;
}

static int sg_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	sg_io_hdr32_t *sg_io32;
	sg_io_hdr_t sg_io64;
	int dxferp32, cmdp32, sbp32;
	mm_segment_t old_fs;
	int err = 0;

	sg_io32 = (sg_io_hdr32_t *)arg;
	err = __get_user(sg_io64.interface_id, &sg_io32->interface_id);
	err |= __get_user(sg_io64.dxfer_direction, &sg_io32->dxfer_direction);
	err |= __get_user(sg_io64.cmd_len, &sg_io32->cmd_len);
	err |= __get_user(sg_io64.mx_sb_len, &sg_io32->mx_sb_len);
	err |= __get_user(sg_io64.iovec_count, &sg_io32->iovec_count);
	err |= __get_user(sg_io64.dxfer_len, &sg_io32->dxfer_len);
	err |= __get_user(sg_io64.timeout, &sg_io32->timeout);
	err |= __get_user(sg_io64.flags, &sg_io32->flags);
	err |= __get_user(sg_io64.pack_id, &sg_io32->pack_id);

	sg_io64.dxferp = NULL;
	sg_io64.cmdp = NULL;
	sg_io64.sbp = NULL;

	err |= __get_user(cmdp32, &sg_io32->cmdp);
	sg_io64.cmdp = kmalloc(sg_io64.cmd_len, GFP_KERNEL);
	if (!sg_io64.cmdp) {
		err = -ENOMEM;
		goto out;
	}
	if (copy_from_user(sg_io64.cmdp,
			   (void *) P(cmdp32),
			   sg_io64.cmd_len)) {
		err = -EFAULT;
		goto out;
	}

	err |= __get_user(sbp32, &sg_io32->sbp);
	sg_io64.sbp = kmalloc(sg_io64.mx_sb_len, GFP_KERNEL);
	if (!sg_io64.sbp) {
		err = -ENOMEM;
		goto out;
	}
	if (copy_from_user(sg_io64.sbp,
			   (void *) P(sbp32),
			   sg_io64.mx_sb_len)) {
		err = -EFAULT;
		goto out;
	}

	err |= __get_user(dxferp32, &sg_io32->dxferp);
	if (sg_io64.iovec_count) {
		int ret;

		if ((ret = alloc_sg_iovec(&sg_io64, dxferp32))) {
			err = ret;
			goto out;
		}
	} else {
		sg_io64.dxferp = kmalloc(sg_io64.dxfer_len, GFP_KERNEL);
		if (!sg_io64.dxferp) {
			err = -ENOMEM;
			goto out;
		}
		if (copy_from_user(sg_io64.dxferp,
				   (void *) P(dxferp32),
				   sg_io64.dxfer_len)) {
			err = -EFAULT;
			goto out;
		}
	}

	/* Unused internally, do not even bother to copy it over. */
	sg_io64.usr_ptr = NULL;

	if (err)
		return -EFAULT;

	old_fs = get_fs();
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, cmd, (unsigned long) &sg_io64);
	set_fs (old_fs);

	if (err < 0)
		goto out;

	err = __put_user(sg_io64.pack_id, &sg_io32->pack_id);
	err |= __put_user(sg_io64.status, &sg_io32->status);
	err |= __put_user(sg_io64.masked_status, &sg_io32->masked_status);
	err |= __put_user(sg_io64.msg_status, &sg_io32->msg_status);
	err |= __put_user(sg_io64.sb_len_wr, &sg_io32->sb_len_wr);
	err |= __put_user(sg_io64.host_status, &sg_io32->host_status);
	err |= __put_user(sg_io64.driver_status, &sg_io32->driver_status);
	err |= __put_user(sg_io64.resid, &sg_io32->resid);
	err |= __put_user(sg_io64.duration, &sg_io32->duration);
	err |= __put_user(sg_io64.info, &sg_io32->info);
	err |= copy_to_user((void *)P(sbp32), sg_io64.sbp, sg_io64.mx_sb_len);
	if (sg_io64.dxferp) {
		if (sg_io64.iovec_count)
			err |= copy_back_sg_iovec(&sg_io64, dxferp32);
		else
			err |= copy_to_user((void *)P(dxferp32),
					    sg_io64.dxferp,
					    sg_io64.dxfer_len);
	}
	if (err)
		err = -EFAULT;

out:
	if (sg_io64.cmdp)
		kfree(sg_io64.cmdp);
	if (sg_io64.sbp)
		kfree(sg_io64.sbp);
	if (sg_io64.dxferp) {
		if (sg_io64.iovec_count) {
			free_sg_iovec(&sg_io64);
		} else {
			kfree(sg_io64.dxferp);
		}
	}
	return err;
}
