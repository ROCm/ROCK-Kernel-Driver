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

struct iovec32 { unsigned int iov_base; int iov_len; };

static int alloc_sg_iovec(sg_io_hdr_t *sgp, int uptr32)
{
	struct iovec32 *uiov = (struct iovec32 *) P(uptr32);
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
	struct iovec32 *uiov = (struct iovec32 *) P(uptr32);
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

asmlinkage long
sys32_ioctl (unsigned int fd, unsigned int cmd, unsigned int arg)
{
	long ret;

	switch (IOCTL_NR(cmd)) {
	      case IOCTL_NR(VFAT_IOCTL_READDIR_SHORT):
	      case IOCTL_NR(VFAT_IOCTL_READDIR_BOTH): {
		      struct linux32_dirent *d32 = P(arg);
		      struct dirent d[2];

		      ret = DO_IOCTL(fd, _IOR('r', _IOC_NR(cmd),
					      struct dirent [2]),
				     (unsigned long) d);
		      if (ret < 0)
			  return ret;

		      if (put_dirent32(d, d32) || put_dirent32(d + 1, d32 + 1))
			  return -EFAULT;

		      return ret;
	      }
		case IOCTL_NR(SIOCGIFCONF):
		{
			struct ifconf32 {
				int		ifc_len;
				unsigned int	ifc_ptr;
			} ifconf32;
			struct ifconf ifconf;
			int i, n;
			char *p32, *p64;
			char buf[32];	/* sizeof IA32 ifreq structure */

			if (copy_from_user(&ifconf32, P(arg), sizeof(ifconf32)))
				return -EFAULT;
			ifconf.ifc_len = ifconf32.ifc_len;
			ifconf.ifc_req = P(ifconf32.ifc_ptr);
			ret = DO_IOCTL(fd, SIOCGIFCONF, &ifconf);
			ifconf32.ifc_len = ifconf.ifc_len;
			if (copy_to_user(P(arg), &ifconf32, sizeof(ifconf32)))
				return -EFAULT;
			n = ifconf.ifc_len / sizeof(struct ifreq);
			p32 = P(ifconf32.ifc_ptr);
			p64 = P(ifconf32.ifc_ptr);
			for (i = 0; i < n; i++) {
				if (copy_from_user(buf, p64, sizeof(struct ifreq)))
					return -EFAULT;
				if (copy_to_user(p32, buf, sizeof(buf)))
					return -EFAULT;
				p32 += sizeof(buf);
				p64 += sizeof(struct ifreq);
			}
			return ret;
		}

	      case IOCTL_NR(DRM_IOCTL_VERSION):
	      {
		      drm_version_t ver;
		      struct {
			      int	version_major;
			      int	version_minor;
			      int	version_patchlevel;
			      unsigned int name_len;
			      unsigned int name; /* pointer */
			      unsigned int date_len;
			      unsigned int date; /* pointer */
			      unsigned int desc_len;
			      unsigned int desc; /* pointer */
		      } ver32;

		      if (copy_from_user(&ver32, P(arg), sizeof(ver32)))
			      return -EFAULT;
		      ver.name_len = ver32.name_len;
		      ver.name = P(ver32.name);
		      ver.date_len = ver32.date_len;
		      ver.date = P(ver32.date);
		      ver.desc_len = ver32.desc_len;
		      ver.desc = P(ver32.desc);
		      ret = DO_IOCTL(fd, DRM_IOCTL_VERSION, &ver);
		      if (ret >= 0) {
			      ver32.version_major = ver.version_major;
			      ver32.version_minor = ver.version_minor;
			      ver32.version_patchlevel = ver.version_patchlevel;
			      ver32.name_len = ver.name_len;
			      ver32.date_len = ver.date_len;
			      ver32.desc_len = ver.desc_len;
			      if (copy_to_user(P(arg), &ver32, sizeof(ver32)))
				      return -EFAULT;
		      }
		      return ret;
	      }

	      case IOCTL_NR(DRM_IOCTL_GET_UNIQUE):
	      {
		      drm_unique_t un;
		      struct {
			      unsigned int unique_len;
			      unsigned int unique;
		      } un32;

		      if (copy_from_user(&un32, P(arg), sizeof(un32)))
			      return -EFAULT;
		      un.unique_len = un32.unique_len;
		      un.unique = P(un32.unique);
		      ret = DO_IOCTL(fd, DRM_IOCTL_GET_UNIQUE, &un);
		      if (ret >= 0) {
			      un32.unique_len = un.unique_len;
			      if (copy_to_user(P(arg), &un32, sizeof(un32)))
				      return -EFAULT;
		      }
		      return ret;
	      }
	      case IOCTL_NR(DRM_IOCTL_SET_UNIQUE):
	      case IOCTL_NR(DRM_IOCTL_ADD_MAP):
	      case IOCTL_NR(DRM_IOCTL_ADD_BUFS):
	      case IOCTL_NR(DRM_IOCTL_MARK_BUFS):
	      case IOCTL_NR(DRM_IOCTL_INFO_BUFS):
	      case IOCTL_NR(DRM_IOCTL_MAP_BUFS):
	      case IOCTL_NR(DRM_IOCTL_FREE_BUFS):
	      case IOCTL_NR(DRM_IOCTL_ADD_CTX):
	      case IOCTL_NR(DRM_IOCTL_RM_CTX):
	      case IOCTL_NR(DRM_IOCTL_MOD_CTX):
	      case IOCTL_NR(DRM_IOCTL_GET_CTX):
	      case IOCTL_NR(DRM_IOCTL_SWITCH_CTX):
	      case IOCTL_NR(DRM_IOCTL_NEW_CTX):
	      case IOCTL_NR(DRM_IOCTL_RES_CTX):

	      case IOCTL_NR(DRM_IOCTL_AGP_ACQUIRE):
	      case IOCTL_NR(DRM_IOCTL_AGP_RELEASE):
	      case IOCTL_NR(DRM_IOCTL_AGP_ENABLE):
	      case IOCTL_NR(DRM_IOCTL_AGP_INFO):
	      case IOCTL_NR(DRM_IOCTL_AGP_ALLOC):
	      case IOCTL_NR(DRM_IOCTL_AGP_FREE):
	      case IOCTL_NR(DRM_IOCTL_AGP_BIND):
	      case IOCTL_NR(DRM_IOCTL_AGP_UNBIND):

		/* Mga specific ioctls */

	      case IOCTL_NR(DRM_IOCTL_MGA_INIT):

		/* I810 specific ioctls */

	      case IOCTL_NR(DRM_IOCTL_I810_GETBUF):
	      case IOCTL_NR(DRM_IOCTL_I810_COPY):

	      case IOCTL_NR(MTIOCGET):
	      case IOCTL_NR(MTIOCPOS):
	      case IOCTL_NR(MTIOCGETCONFIG):
	      case IOCTL_NR(MTIOCSETCONFIG):
	      case IOCTL_NR(PPPIOCSCOMPRESS):
	      case IOCTL_NR(PPPIOCGIDLE):
	      case IOCTL_NR(NCP_IOC_GET_FS_INFO_V2):
	      case IOCTL_NR(NCP_IOC_GETOBJECTNAME):
	      case IOCTL_NR(NCP_IOC_SETOBJECTNAME):
	      case IOCTL_NR(NCP_IOC_GETPRIVATEDATA):
	      case IOCTL_NR(NCP_IOC_SETPRIVATEDATA):
	      case IOCTL_NR(NCP_IOC_GETMOUNTUID2):
	      case IOCTL_NR(CAPI_MANUFACTURER_CMD):
	      case IOCTL_NR(VIDIOCGTUNER):
	      case IOCTL_NR(VIDIOCSTUNER):
	      case IOCTL_NR(VIDIOCGWIN):
	      case IOCTL_NR(VIDIOCSWIN):
	      case IOCTL_NR(VIDIOCGFBUF):
	      case IOCTL_NR(VIDIOCSFBUF):
	      case IOCTL_NR(MGSL_IOCSPARAMS):
	      case IOCTL_NR(MGSL_IOCGPARAMS):
	      case IOCTL_NR(ATM_GETNAMES):
	      case IOCTL_NR(ATM_GETLINKRATE):
	      case IOCTL_NR(ATM_GETTYPE):
	      case IOCTL_NR(ATM_GETESI):
	      case IOCTL_NR(ATM_GETADDR):
	      case IOCTL_NR(ATM_RSTADDR):
	      case IOCTL_NR(ATM_ADDADDR):
	      case IOCTL_NR(ATM_DELADDR):
	      case IOCTL_NR(ATM_GETCIRANGE):
	      case IOCTL_NR(ATM_SETCIRANGE):
	      case IOCTL_NR(ATM_SETESI):
	      case IOCTL_NR(ATM_SETESIF):
	      case IOCTL_NR(ATM_GETSTAT):
	      case IOCTL_NR(ATM_GETSTATZ):
	      case IOCTL_NR(ATM_GETLOOP):
	      case IOCTL_NR(ATM_SETLOOP):
	      case IOCTL_NR(ATM_QUERYLOOP):
	      case IOCTL_NR(ENI_SETMULT):
	      case IOCTL_NR(NS_GETPSTAT):
		/* case IOCTL_NR(NS_SETBUFLEV): This is a duplicate case with ZATM_GETPOOLZ */
	      case IOCTL_NR(ZATM_GETPOOLZ):
	      case IOCTL_NR(ZATM_GETPOOL):
	      case IOCTL_NR(ZATM_SETPOOL):
	      case IOCTL_NR(ZATM_GETTHIST):
	      case IOCTL_NR(IDT77105_GETSTAT):
	      case IOCTL_NR(IDT77105_GETSTATZ):
	      case IOCTL_NR(IXJCTL_TONE_CADENCE):
	      case IOCTL_NR(IXJCTL_FRAMES_READ):
	      case IOCTL_NR(IXJCTL_FRAMES_WRITTEN):
	      case IOCTL_NR(IXJCTL_READ_WAIT):
	      case IOCTL_NR(IXJCTL_WRITE_WAIT):
	      case IOCTL_NR(IXJCTL_DRYBUFFER_READ):
	      case IOCTL_NR(I2OHRTGET):
	      case IOCTL_NR(I2OLCTGET):
	      case IOCTL_NR(I2OPARMSET):
	      case IOCTL_NR(I2OPARMGET):
	      case IOCTL_NR(I2OSWDL):
	      case IOCTL_NR(I2OSWUL):
	      case IOCTL_NR(I2OSWDEL):
	      case IOCTL_NR(I2OHTML):
		break;
	      default:
		return sys_ioctl(fd, cmd, (unsigned long)arg);

		case IOCTL_NR(SG_IO):
			return(sg_ioctl_trans(fd, cmd, arg));

	}
	printk("%x:unimplemented IA32 ioctl system call\n", cmd);
	return -EINVAL;
}
