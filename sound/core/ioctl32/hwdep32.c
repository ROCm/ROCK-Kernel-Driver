/*
 *   32bit -> 64bit ioctl wrapper for hwdep API
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <sound/core.h>
#include <sound/hwdep.h>
#include <asm/uaccess.h>
#include "ioctl32.h"

struct sndrv_hwdep_dsp_image32 {
	u32 index;
	unsigned char name[64];
	u32 image;	/* pointer */
	u32 length;
	u32 driver_data;
} /* don't set packed attribute here */;

static int _snd_ioctl32_hwdep_dsp_image(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file, unsigned int native_ctl)
{
	struct sndrv_hwdep_dsp_image data;
	struct sndrv_hwdep_dsp_image32 data32;
	mm_segment_t oldseg;
	int err;

	if (copy_from_user(&data32, (void __user *)arg, sizeof(data32)))
		return -EFAULT;
	memset(&data, 0, sizeof(data));
	data.index = data32.index;
	memcpy(data.name, data32.name, sizeof(data.name));
	data.image = compat_ptr(data32.image);
	data.length = data32.length;
	data.driver_data = data32.driver_data;
	oldseg = get_fs();
	set_fs(KERNEL_DS);
	err = file->f_op->ioctl(file->f_dentry->d_inode, file, native_ctl, (unsigned long)&data);
	set_fs(oldseg);
	return err;
}

DEFINE_ALSA_IOCTL_ENTRY(hwdep_dsp_image, hwdep_dsp_image, SNDRV_HWDEP_IOCTL_DSP_LOAD);

#define AP(x) snd_ioctl32_##x

enum {
	SNDRV_HWDEP_IOCTL_DSP_LOAD32   = _IOW('H', 0x03, struct sndrv_hwdep_dsp_image32)
};

struct ioctl32_mapper hwdep_mappers[] = {
	MAP_COMPAT(SNDRV_HWDEP_IOCTL_PVERSION),
	MAP_COMPAT(SNDRV_HWDEP_IOCTL_INFO),
	MAP_COMPAT(SNDRV_HWDEP_IOCTL_DSP_STATUS),
	{ SNDRV_HWDEP_IOCTL_DSP_LOAD32, AP(hwdep_dsp_image) },
	{ 0 },
};
