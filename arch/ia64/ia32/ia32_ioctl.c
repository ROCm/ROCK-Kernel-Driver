/*
 * IA32 Architecture-specific ioctl shim code
 *
 * Copyright (C) 2000 VA Linux Co
 * Copyright (C) 2000 Don Dugger <n0ano@valinux.com>
 */

#include <linux/types.h>
#include <linux/dirent.h>
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
#include <../drivers/char/drm/drm.h>

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

#define P(i)	((void *)(long)(i))


asmlinkage long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

asmlinkage long ia32_ioctl(unsigned int fd, unsigned int cmd, unsigned int arg)
{
	long ret;

	switch (IOCTL_NR(cmd)) {

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
			ret = DO_IOCTL(fd, cmd, &ver);
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
			return(ret);
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
			ret = DO_IOCTL(fd, cmd, &un);
			if (ret >= 0) {
				un32.unique_len = un.unique_len;
				if (copy_to_user(P(arg), &un32, sizeof(un32)))
					return -EFAULT;
			}
			return(ret);
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

	/* Rage 128 specific ioctls */

	case IOCTL_NR(DRM_IOCTL_R128_PACKET):

	case IOCTL_NR(VFAT_IOCTL_READDIR_BOTH):
	case IOCTL_NR(VFAT_IOCTL_READDIR_SHORT):
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
		return(sys_ioctl(fd, cmd, (unsigned long)arg));

	}
	printk("%x:unimplemented IA32 ioctl system call\n", cmd);
	return(-EINVAL);
}
