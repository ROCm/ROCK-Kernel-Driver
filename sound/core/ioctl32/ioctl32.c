/*
 *   32bit -> 64bit ioctl wrapper for control API
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
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <sound/core.h>
#include <sound/control.h>
#include <asm/uaccess.h>
#include "ioctl32.h"

/*
 * register/unregister mappers
 * exported for other modules
 */

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("ioctl32 wrapper for ALSA");
MODULE_LICENSE("GPL");

int register_ioctl32_conversion(unsigned int cmd, int (*handler)(unsigned int, unsigned int, unsigned long, struct file *));
int unregister_ioctl32_conversion(unsigned int cmd);


int snd_ioctl32_register(struct ioctl32_mapper *mappers)
{
	int err;
	struct ioctl32_mapper *m;

	for (m = mappers; m->cmd; m++) {
		err = register_ioctl32_conversion(m->cmd, m->handler);
		if (err >= 0)
			m->registered++;
	}
	return 0;
}

void snd_ioctl32_unregister(struct ioctl32_mapper *mappers)
{
	struct ioctl32_mapper *m;

	for (m = mappers; m->cmd; m++) {
		if (m->registered) {
			unregister_ioctl32_conversion(m->cmd);
			m->registered = 0;
		}
	}
}


/*
 * compatible wrapper
 */
int snd_ioctl32_compat(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *filp)
{
	if (! filp->f_op || ! filp->f_op->ioctl)
		return -ENOTTY;
	return filp->f_op->ioctl(filp->f_dentry->d_inode, filp, cmd, arg);
}


/*
 * Controls
 */

struct sndrv_ctl_elem_list32 {
	u32 offset;
	u32 space;
	u32 used;
	u32 count;
	u32 pids;
	unsigned char reserved[50];
} /* don't set packed attribute here */;

#define CVT_sndrv_ctl_elem_list()\
{\
	COPY(offset);\
	COPY(space);\
	COPY(used);\
	COPY(count);\
	CPTR(pids);\
}

static inline int _snd_ioctl32_ctl_elem_list(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file, unsigned int native_ctl)
{
	struct sndrv_ctl_elem_list32 data32;
	struct sndrv_ctl_elem_list data;
	mm_segment_t oldseg;
	int err;

	if (copy_from_user(&data32, (void __user *)arg, sizeof(data32)))
		return -EFAULT;
	memset(&data, 0, sizeof(data));
	data.offset = data32.offset;
	data.space = data32.space;
	data.used = data32.used;
	data.count = data32.count;
	data.pids = compat_ptr(data32.pids);
	oldseg = get_fs();
	set_fs(KERNEL_DS);
	err = file->f_op->ioctl(file->f_dentry->d_inode, file, native_ctl, (unsigned long)&data);
	set_fs(oldseg);
	if (err < 0)
		return err;
	/* copy the result */
	data32.offset = data.offset;
	data32.space = data.space;
	data32.used = data.used;
	data32.count = data.count;
	//data.pids = data.pids;
	if (copy_to_user((void __user *)arg, &data32, sizeof(data32)))
		return -EFAULT;
	return 0;
}

DEFINE_ALSA_IOCTL_ENTRY(ctl_elem_list, ctl_elem_list, SNDRV_CTL_IOCTL_ELEM_LIST);

/*
 * control element info
 * it uses union, so the things are not easy..
 */

struct sndrv_ctl_elem_info32 {
	struct sndrv_ctl_elem_id id; // the size of struct is same
	s32 type;
	u32 access;
	u32 count;
	s32 owner;
	union {
		struct {
			s32 min;
			s32 max;
			s32 step;
		} integer;
		struct {
			u64 min;
			u64 max;
			u64 step;
		} integer64;
		struct {
			u32 items;
			u32 item;
			char name[64];
		} enumerated;
		unsigned char reserved[128];
	} value;
	unsigned char reserved[64];
} __attribute__((packed));

static inline int _snd_ioctl32_ctl_elem_info(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file, unsigned int native_ctl)
{
	struct sndrv_ctl_elem_info data;
	struct sndrv_ctl_elem_info32 data32;
	int err;
	mm_segment_t oldseg;

	if (copy_from_user(&data32, (void __user *)arg, sizeof(data32)))
		return -EFAULT;
	memset(&data, 0, sizeof(data));
	data.id = data32.id;
	/* we need to copy the item index.
	 * hope this doesn't break anything..
	 */
	data.value.enumerated.item = data32.value.enumerated.item;
	oldseg = get_fs();
	set_fs(KERNEL_DS);
	err = file->f_op->ioctl(file->f_dentry->d_inode, file, native_ctl, (unsigned long)&data);
	set_fs(oldseg);
	if (err < 0)
		return err;
	/* restore info to 32bit */
	data32.id = data.id;
	data32.type = data.type;
	data32.access = data.access;
	data32.count = data.count;
	data32.owner = data.owner;
	switch (data.type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		data32.value.integer.min = data.value.integer.min;
		data32.value.integer.max = data.value.integer.max;
		data32.value.integer.step = data.value.integer.step;
		break;
	case SNDRV_CTL_ELEM_TYPE_INTEGER64:
		data32.value.integer64.min = data.value.integer64.min;
		data32.value.integer64.max = data.value.integer64.max;
		data32.value.integer64.step = data.value.integer64.step;
		break;
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
		data32.value.enumerated.items = data.value.enumerated.items;
		data32.value.enumerated.item = data.value.enumerated.item;
		memcpy(data32.value.enumerated.name, data.value.enumerated.name,
		       sizeof(data.value.enumerated.name));
		break;
	default:
		break;
	}
	if (copy_to_user((void __user *)arg, &data32, sizeof(data32)))
		return -EFAULT;
	return 0;
}

DEFINE_ALSA_IOCTL_ENTRY(ctl_elem_info, ctl_elem_info, SNDRV_CTL_IOCTL_ELEM_INFO);

struct sndrv_ctl_elem_value32 {
	struct sndrv_ctl_elem_id id;
	unsigned int indirect;	/* bit-field causes misalignment */
        union {
		union {
			s32 value[128];
			u32 value_ptr;
		} integer;
		union {
			s64 value[64];
			u32 value_ptr;
		} integer64;
		union {
			u32 item[128];
			u32 item_ptr;
		} enumerated;
		union {
			unsigned char data[512];
			u32 data_ptr;
		} bytes;
		struct sndrv_aes_iec958 iec958;
        } value;
        unsigned char reserved[128];
};


/* hmm, it's so hard to retrieve the value type from the control id.. */
static int get_ctl_type(struct file *file, snd_ctl_elem_id_t *id)
{
	snd_ctl_file_t *ctl;
	snd_kcontrol_t *kctl;
	snd_ctl_elem_info_t info;
	int err;

	ctl = file->private_data;

	down_read(&ctl->card->controls_rwsem);
	kctl = snd_ctl_find_id(ctl->card, id);
	if (! kctl) {
		up_read(&ctl->card->controls_rwsem);
		return -ENXIO;
	}
	info.id = *id;
	err = kctl->info(kctl, &info);
	up_read(&ctl->card->controls_rwsem);
	if (err >= 0)
		err = info.type;
	return err;
}


static inline int _snd_ioctl32_ctl_elem_value(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file, unsigned int native_ctl)
{
	struct sndrv_ctl_elem_value *data;
	struct sndrv_ctl_elem_value32 *data32;
	int err, i;
	int type;
	mm_segment_t oldseg;

	/* FIXME: check the sane ioctl.. */

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	data32 = kmalloc(sizeof(*data32), GFP_KERNEL);
	if (data == NULL || data32 == NULL) {
		err = -ENOMEM;
		goto __end;
	}

	if (copy_from_user(data32, (void __user *)arg, sizeof(*data32))) {
		err = -EFAULT;
		goto __end;
	}
	memset(data, 0, sizeof(*data));
	data->id = data32->id;
	data->indirect = data32->indirect;
	if (data->indirect) /* FIXME: this is not correct for long arrays */
		data->value.integer.value_ptr = compat_ptr(data32->value.integer.value_ptr);
	type = get_ctl_type(file, &data->id);
	if (type < 0) {
		err = type;
		goto __end;
	}
	if (! data->indirect) {
		switch (type) {
		case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		case SNDRV_CTL_ELEM_TYPE_INTEGER:
			for (i = 0; i < 128; i++)
				data->value.integer.value[i] = data32->value.integer.value[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_INTEGER64:
			for (i = 0; i < 64; i++)
				data->value.integer64.value[i] = data32->value.integer64.value[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
			for (i = 0; i < 128; i++)
				data->value.enumerated.item[i] = data32->value.enumerated.item[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_BYTES:
			memcpy(data->value.bytes.data, data32->value.bytes.data,
			       sizeof(data->value.bytes.data));
			break;
		case SNDRV_CTL_ELEM_TYPE_IEC958:
			data->value.iec958 = data32->value.iec958;
			break;
		default:
			printk("unknown type %d\n", type);
			break;
		}
	}

	oldseg = get_fs();
	set_fs(KERNEL_DS);
	err = file->f_op->ioctl(file->f_dentry->d_inode, file, native_ctl, (unsigned long)data);
	set_fs(oldseg);
	if (err < 0)
		goto __end;
	/* restore info to 32bit */
	if (! data->indirect) {
		switch (type) {
		case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		case SNDRV_CTL_ELEM_TYPE_INTEGER:
			for (i = 0; i < 128; i++)
				data32->value.integer.value[i] = data->value.integer.value[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_INTEGER64:
			for (i = 0; i < 64; i++)
				data32->value.integer64.value[i] = data->value.integer64.value[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
			for (i = 0; i < 128; i++)
				data32->value.enumerated.item[i] = data->value.enumerated.item[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_BYTES:
			memcpy(data32->value.bytes.data, data->value.bytes.data,
			       sizeof(data->value.bytes.data));
			break;
		case SNDRV_CTL_ELEM_TYPE_IEC958:
			data32->value.iec958 = data->value.iec958;
			break;
		default:
			break;
		}
	}
	err = 0;
	if (copy_to_user((void __user *)arg, data32, sizeof(*data32)))
		err = -EFAULT;
      __end:
      	if (data32)
      		kfree(data32);
	if (data)
		kfree(data);
	return err;
}

DEFINE_ALSA_IOCTL_ENTRY(ctl_elem_read, ctl_elem_value, SNDRV_CTL_IOCTL_ELEM_READ);
DEFINE_ALSA_IOCTL_ENTRY(ctl_elem_write, ctl_elem_value, SNDRV_CTL_IOCTL_ELEM_WRITE);

/*
 */

#define AP(x) snd_ioctl32_##x

enum {
	SNDRV_CTL_IOCTL_ELEM_LIST32 = _IOWR('U', 0x10, struct sndrv_ctl_elem_list32),
	SNDRV_CTL_IOCTL_ELEM_INFO32 = _IOWR('U', 0x11, struct sndrv_ctl_elem_info32),
	SNDRV_CTL_IOCTL_ELEM_READ32 = _IOWR('U', 0x12, struct sndrv_ctl_elem_value32),
	SNDRV_CTL_IOCTL_ELEM_WRITE32 = _IOWR('U', 0x13, struct sndrv_ctl_elem_value32),
};

static struct ioctl32_mapper control_mappers[] = {
	/* controls (without rawmidi, hwdep, timer releated ones) */
	MAP_COMPAT(SNDRV_CTL_IOCTL_PVERSION),
	MAP_COMPAT(SNDRV_CTL_IOCTL_CARD_INFO),
	{ SNDRV_CTL_IOCTL_ELEM_LIST32, AP(ctl_elem_list) },
	{ SNDRV_CTL_IOCTL_ELEM_INFO32, AP(ctl_elem_info) },
	{ SNDRV_CTL_IOCTL_ELEM_READ32, AP(ctl_elem_read) },
	{ SNDRV_CTL_IOCTL_ELEM_WRITE32, AP(ctl_elem_write) },
	MAP_COMPAT(SNDRV_CTL_IOCTL_ELEM_LOCK),
	MAP_COMPAT(SNDRV_CTL_IOCTL_ELEM_UNLOCK),
	MAP_COMPAT(SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS),
	MAP_COMPAT(SNDRV_CTL_IOCTL_HWDEP_INFO),
	MAP_COMPAT(SNDRV_CTL_IOCTL_HWDEP_NEXT_DEVICE),
	MAP_COMPAT(SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE),
	MAP_COMPAT(SNDRV_CTL_IOCTL_PCM_INFO),
	MAP_COMPAT(SNDRV_CTL_IOCTL_PCM_PREFER_SUBDEVICE),
	MAP_COMPAT(SNDRV_CTL_IOCTL_POWER),
	MAP_COMPAT(SNDRV_CTL_IOCTL_POWER_STATE),
	{ 0 }
};


/*
 */

extern struct ioctl32_mapper pcm_mappers[];
extern struct ioctl32_mapper rawmidi_mappers[];
extern struct ioctl32_mapper timer_mappers[];
extern struct ioctl32_mapper hwdep_mappers[];
#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
extern struct ioctl32_mapper seq_mappers[];
#endif

static void snd_ioctl32_done(void)
{
#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
	snd_ioctl32_unregister(seq_mappers);
#endif
	snd_ioctl32_unregister(hwdep_mappers);
	snd_ioctl32_unregister(timer_mappers);
	snd_ioctl32_unregister(rawmidi_mappers);
	snd_ioctl32_unregister(pcm_mappers);
	snd_ioctl32_unregister(control_mappers);
}

static int __init snd_ioctl32_init(void)
{
	snd_ioctl32_register(control_mappers);
	snd_ioctl32_register(pcm_mappers);
	snd_ioctl32_register(rawmidi_mappers);
	snd_ioctl32_register(timer_mappers);
	snd_ioctl32_register(hwdep_mappers);
#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
	snd_ioctl32_register(seq_mappers);
#endif
	return 0;
}

module_init(snd_ioctl32_init)
module_exit(snd_ioctl32_done)
