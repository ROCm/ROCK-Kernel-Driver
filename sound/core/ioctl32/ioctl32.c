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

#define __NO_VERSION__
#include <sound/driver.h>
#include <linux/smp_lock.h>
#include <linux/time.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/control.h>
#include <asm/uaccess.h>
#include "ioctl32.h"

/*
 * register/unregister mappers
 * exported for other modules
 */

int register_ioctl32_conversion(unsigned int cmd, int (*handler)(unsigned int, unsigned int, unsigned long, struct file *));
int unregister_ioctl32_conversion(unsigned int cmd);


int snd_ioctl32_register(struct ioctl32_mapper *mappers)
{
	int err;
	struct ioctl32_mapper *m;

	lock_kernel();
	for (m = mappers; m->cmd; m++) {
		err = register_ioctl32_conversion(m->cmd, m->handler);
		if (err < 0) {
			unlock_kernel();
			return err;
		}
		m->registered++;
	}
	return 0;
}

void snd_ioctl32_unregister(struct ioctl32_mapper *mappers)
{
	struct ioctl32_mapper *m;

	lock_kernel();
	for (m = mappers; m->cmd; m++) {
		if (m->registered) {
			unregister_ioctl32_conversion(m->cmd);
			m->registered = 0;
		}
	}
	unlock_kernel();
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
};

#define CVT_sndrv_ctl_elem_list()\
{\
	COPY(offset);\
	COPY(space);\
	COPY(used);\
	COPY(count);\
	CPTR(pids);\
}

DEFINE_ALSA_IOCTL(ctl_elem_list);


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
};

static int snd_ioctl32_ctl_elem_info(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file)
{
	struct sndrv_ctl_elem_info data;
	struct sndrv_ctl_elem_info32 data32;
	int err;

	if (copy_from_user(&data32, (void*)arg, sizeof(data32)))
		return -EFAULT;
	memset(&data, 0, sizeof(data));
	data.id = data32.id;
	err = file->f_op->ioctl(file->f_dentry->d_inode, file, cmd, (unsigned long)&data);
	if (err < 0)
		return err;
	/* restore info to 32bit */
	data32.type = data.type;
	data32.access = data.access;
	data32.count = data.count;
	data32.owner = data.owner;
	switch (data.type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		data32.value.integer.min = data.value.integer.min;
		data32.value.integer.max = data.value.integer.min;
		data32.value.integer.step = data.value.integer.step;
		break;
	case SNDRV_CTL_ELEM_TYPE_INTEGER64:
		data32.value.integer64.min = data.value.integer64.min;
		data32.value.integer64.max = data.value.integer64.min;
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
	if (copy_to_user((void*)arg, &data32, sizeof(data32)))
		return -EFAULT;
	return err;
}


struct sndrv_ctl_elem_value32 {
	struct sndrv_ctl_elem_id id;
	unsigned int indirect: 1;
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

	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return -ENXIO);

	read_lock(&ctl->card->control_rwlock);
	kctl = snd_ctl_find_id(ctl->card, id);
	if (! kctl) {
		read_unlock(&ctl->card->control_rwlock);
		return -ENXIO;
	}
	info.id = *id;
	err = kctl->info(kctl, &info);
	if (err >= 0)
		err = info.type;
	read_unlock(&ctl->card->control_rwlock);
	return err;
}


static int snd_ioctl32_ctl_elem_value(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file)
{
	// too big?
	struct sndrv_ctl_elem_value data;
	struct sndrv_ctl_elem_value32 data32;
	int err, i;
	int type;
	/* FIXME: check the sane ioctl.. */

	if (copy_from_user(&data32, (void*)arg, sizeof(data32)))
		return -EFAULT;
	memset(&data, 0, sizeof(data));
	data.id = data32.id;
	data.indirect = data32.indirect;
	if (data.indirect) /* FIXME: this is not correct for long arrays */
		data.value.integer.value_ptr = (void*)TO_PTR(data32.value.integer.value_ptr);
	type = get_ctl_type(file, &data.id);
	if (type < 0)
		return type;
	if (! data.indirect) {
		switch (type) {
		case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		case SNDRV_CTL_ELEM_TYPE_INTEGER:
			for (i = 0; i < 128; i++)
				data.value.integer.value[i] = data32.value.integer.value[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_INTEGER64:
			for (i = 0; i < 64; i++)
				data.value.integer64.value[i] = data32.value.integer64.value[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
			for (i = 0; i < 128; i++)
				data.value.enumerated.item[i] = data32.value.enumerated.item[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_BYTES:
			memcpy(data.value.bytes.data, data32.value.bytes.data,
			       sizeof(data.value.bytes.data));
			break;
		case SNDRV_CTL_ELEM_TYPE_IEC958:
			data.value.iec958 = data32.value.iec958;
			break;
		default:
			break;
		}
	}

	err = file->f_op->ioctl(file->f_dentry->d_inode, file, cmd, (unsigned long)&data);
	if (err < 0)
		return err;
	/* restore info to 32bit */
	if (! data.indirect) {
		switch (type) {
		case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		case SNDRV_CTL_ELEM_TYPE_INTEGER:
			for (i = 0; i < 128; i++)
				data.value.integer.value[i] = data32.value.integer.value[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_INTEGER64:
			for (i = 0; i < 64; i++)
				data.value.integer64.value[i] = data32.value.integer64.value[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
			for (i = 0; i < 128; i++)
				data.value.enumerated.item[i] = data32.value.enumerated.item[i];
			break;
		case SNDRV_CTL_ELEM_TYPE_BYTES:
			memcpy(data.value.bytes.data, data32.value.bytes.data,
			       sizeof(data.value.bytes.data));
			break;
		case SNDRV_CTL_ELEM_TYPE_IEC958:
			data.value.iec958 = data32.value.iec958;
			break;
		default:
			break;
		}
	}
	if (copy_to_user((void*)arg, &data32, sizeof(data32)))
		return -EFAULT;
	return err;
}


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
	{ SNDRV_CTL_IOCTL_PVERSION, NULL },
	{ SNDRV_CTL_IOCTL_CARD_INFO , NULL },
	{ SNDRV_CTL_IOCTL_ELEM_LIST32, AP(ctl_elem_list) },
	{ SNDRV_CTL_IOCTL_ELEM_INFO32, AP(ctl_elem_info) },
	{ SNDRV_CTL_IOCTL_ELEM_READ32, AP(ctl_elem_value) },
	{ SNDRV_CTL_IOCTL_ELEM_WRITE32, AP(ctl_elem_value) },
	{ SNDRV_CTL_IOCTL_ELEM_LOCK, NULL },
	{ SNDRV_CTL_IOCTL_ELEM_UNLOCK, NULL },
	{ SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS, NULL },
	{ SNDRV_CTL_IOCTL_HWDEP_NEXT_DEVICE, NULL },
	{ SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE, NULL },
	{ SNDRV_CTL_IOCTL_PCM_INFO, NULL },
	{ SNDRV_CTL_IOCTL_PCM_PREFER_SUBDEVICE, NULL },
	{ SNDRV_CTL_IOCTL_POWER, NULL },
	{ SNDRV_CTL_IOCTL_POWER_STATE, NULL },
	{ 0 }
};


/*
 */

extern struct ioctl32_mapper pcm_mappers[];
extern struct ioctl32_mapper rawmidi_mappers[];
extern struct ioctl32_mapper timer_mappers[];
extern struct ioctl32_mapper hwdep_mappers[];
#ifdef CONFIG_SND_SEQUENCER
extern struct ioctl32_mapper seq_mappers[];
#endif

static void snd_ioctl32_done(void)
{
#ifdef CONFIG_SND_SEQUENCER
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
	int err;
	
	err = snd_ioctl32_register(control_mappers);
	if (err < 0)
		return err;
	err = snd_ioctl32_register(pcm_mappers);
	if (err < 0) {
		snd_ioctl32_done();
		return err;
	}
	err = snd_ioctl32_register(rawmidi_mappers);
	if (err < 0) {
		snd_ioctl32_done();
		return err;
	}
	err = snd_ioctl32_register(timer_mappers);
	if (err < 0) {
		snd_ioctl32_done();
		return err;
	}
	err = snd_ioctl32_register(hwdep_mappers);
	if (err < 0) {
		snd_ioctl32_done();
		return err;
	}
#ifdef CONFIG_SND_SEQUENCER
	err = snd_ioctl32_register(seq_mappers);
	if (err < 0) {
		snd_ioctl32_done();
		return err;
	}
#endif

	return 0;
}

module_init(snd_ioctl32_init)
module_exit(snd_ioctl32_done)
