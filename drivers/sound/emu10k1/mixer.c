/*
 **********************************************************************
 *     mixer.c - /dev/mixer interface for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999     Alan Cox        cleaned up stuff
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#define __NO_VERSION__		/* Kernel version only defined once */
#include <linux/module.h>
#include <linux/version.h>
#include <asm/uaccess.h>
#include <linux/fs.h>

#include "hwaccess.h"
#include "8010.h"
#include "recmgr.h"

//FIXME: SOUND_MIXER_VOLUME should be selectable 5 or 6 bit
const struct oss_scaling volume_params[SOUND_MIXER_NRDEVICES]= {
/* Used by the ac97 driver */
	[SOUND_MIXER_VOLUME]	=	{VOL_6BIT},
	[SOUND_MIXER_BASS]	=	{VOL_4BIT},
	[SOUND_MIXER_TREBLE]	=	{VOL_4BIT},
	[SOUND_MIXER_PCM]	=	{VOL_5BIT},
	[SOUND_MIXER_SPEAKER]	=	{VOL_4BIT},
	[SOUND_MIXER_LINE]	=	{VOL_5BIT},
	[SOUND_MIXER_MIC]	=	{VOL_5BIT},
	[SOUND_MIXER_CD]	=	{VOL_5BIT},
	[SOUND_MIXER_ALTPCM]	=	{VOL_6BIT},
	[SOUND_MIXER_IGAIN]	=	{VOL_4BIT},
	[SOUND_MIXER_LINE1]	=	{VOL_5BIT},
	[SOUND_MIXER_PHONEIN]	= 	{VOL_5BIT},
	[SOUND_MIXER_PHONEOUT]	= 	{VOL_6BIT},
	[SOUND_MIXER_VIDEO]	=	{VOL_5BIT},
/* Not used by the ac97 driver */
	[SOUND_MIXER_SYNTH]	=	{VOL_5BIT},
	[SOUND_MIXER_IMIX]	=	{VOL_5BIT},
	[SOUND_MIXER_RECLEV]	=	{VOL_5BIT},
	[SOUND_MIXER_OGAIN]	=	{VOL_5BIT},
	[SOUND_MIXER_LINE2]	=	{VOL_5BIT},
	[SOUND_MIXER_LINE3]	=	{VOL_5BIT},
	[SOUND_MIXER_DIGITAL1]	=	{VOL_5BIT},
	[SOUND_MIXER_DIGITAL2]	=	{VOL_5BIT},
	[SOUND_MIXER_DIGITAL3]	=	{VOL_5BIT},
	[SOUND_MIXER_RADIO]	=	{VOL_5BIT},
	[SOUND_MIXER_MONITOR]	=	{VOL_5BIT}
};
static loff_t emu10k1_mixer_llseek(struct file *file, loff_t offset, int origin)
{
	DPF(2, "sblive_mixer_llseek() called\n");
	return -ESPIPE;
}

/* Mixer file operations */

static int emu10k1_private_mixer(struct emu10k1_card *card, unsigned int cmd, unsigned long arg)
{
	struct mixer_private_ioctl *ctl;
	struct dsp_patch *patch;
	u32 size, page;
	int addr, size_reg, i, ret;
	unsigned int id, ch;

	switch (cmd) {

	case SOUND_MIXER_PRIVATE3:

		ctl = (struct mixer_private_ioctl *) kmalloc(sizeof(struct mixer_private_ioctl), GFP_KERNEL);
		if (ctl == NULL)
			return -ENOMEM;

		if (copy_from_user(ctl, (void *) arg, sizeof(struct mixer_private_ioctl))) {
			kfree(ctl);
			return -EFAULT;
		}

		ret = 0;
		switch (ctl->cmd) {
#ifdef DBGEMU
		case CMD_WRITEFN0:
			emu10k1_writefn0(card, ctl->val[0], ctl->val[1]);
			break;

		case CMD_WRITEPTR:
			if (ctl->val[1] >= 0x40 || ctl->val[0] > 0xff) {
				ret = -EINVAL;
				break;
			}

			if ((ctl->val[0] & 0x7ff) > 0x3f)
				ctl->val[1] = 0x00;

			sblive_writeptr(card, ctl->val[0], ctl->val[1], ctl->val[2]);

			break;
#endif
		case CMD_READFN0:
			ctl->val[2] = emu10k1_readfn0(card, ctl->val[0]);

			if (copy_to_user((void *) arg, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_READPTR:
			if (ctl->val[1] >= 0x40 || (ctl->val[0] & 0x7ff) > 0xff) {
				ret = -EINVAL;
				break;
			}

			if ((ctl->val[0] & 0x7ff) > 0x3f)
				ctl->val[1] = 0x00;

			ctl->val[2] = sblive_readptr(card, ctl->val[0], ctl->val[1]);

			if (copy_to_user((void *) arg, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_SETRECSRC:
			switch (ctl->val[0]) {
			case WAVERECORD_AC97:
				if (card->isaps) {
					ret = -EINVAL;
					break;
				}
				card->wavein.recsrc = WAVERECORD_AC97;
				break;
			case WAVERECORD_MIC:
				card->wavein.recsrc = WAVERECORD_MIC;
				break;
			case WAVERECORD_FX:
				card->wavein.recsrc = WAVERECORD_FX;
				card->wavein.fxwc = ctl->val[1] & 0xffff;
				if (!card->wavein.fxwc)
					ret = -EINVAL;
				break;
			default:
				ret = -EINVAL;
				break;
			}
			break;

		case CMD_GETRECSRC:
			ctl->val[0] = card->wavein.recsrc;
			ctl->val[1] = card->wavein.fxwc;
			if (copy_to_user((void *) arg, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_GETVOICEPARAM:
			ctl->val[0] = card->waveout.send_routing[0];
			ctl->val[1] = card->waveout.send_a[0] | card->waveout.send_b[0] << 8 |
			    	      card->waveout.send_c[0] << 16 | card->waveout.send_d[0] << 24;

			ctl->val[2] = card->waveout.send_routing[1];
			ctl->val[3] = card->waveout.send_a[1] | card->waveout.send_b[1] << 8 |
				      card->waveout.send_c[1] << 16 | card->waveout.send_d[1] << 24;

			ctl->val[4] = card->waveout.send_routing[2];
			ctl->val[5] = card->waveout.send_a[2] | card->waveout.send_b[2] << 8 |
				     card->waveout.send_c[2] << 16 | card->waveout.send_d[2] << 24;

			if (copy_to_user((void *) arg, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_SETVOICEPARAM:
			card->waveout.send_routing[0] = ctl->val[0] & 0xffff;
			card->waveout.send_a[0] = ctl->val[1] & 0xff;
			card->waveout.send_b[0] = (ctl->val[1] >> 8) & 0xff;
			card->waveout.send_c[0] = (ctl->val[1] >> 16) & 0xff;
			card->waveout.send_d[0] = (ctl->val[1] >> 24) & 0xff;

			card->waveout.send_routing[1] = ctl->val[2] & 0xffff;
			card->waveout.send_a[1] = ctl->val[3] & 0xff;
			card->waveout.send_b[1] = (ctl->val[3] >> 8) & 0xff;
			card->waveout.send_c[1] = (ctl->val[3] >> 16) & 0xff;
			card->waveout.send_d[1] = (ctl->val[3] >> 24) & 0xff;

			card->waveout.send_routing[2] = ctl->val[4] & 0xffff;
			card->waveout.send_a[2] = ctl->val[5] & 0xff;
			card->waveout.send_b[2] = (ctl->val[5] >> 8) & 0xff;
			card->waveout.send_c[2] = (ctl->val[5] >> 16) & 0xff;
			card->waveout.send_d[2] = (ctl->val[5] >> 24) & 0xff;

			break;
		
		case CMD_SETMCH_FX:
			card->mchannel_fx = ctl->val[0] & 0x000f;
			break;
		
		case CMD_GETPATCH:
			if (ctl->val[0] == 0) {
				if (copy_to_user((void *) arg, &card->mgr.rpatch, sizeof(struct dsp_rpatch)))
                                	ret = -EFAULT;
			} else {
				if ((ctl->val[0] - 1) / PATCHES_PER_PAGE >= card->mgr.current_pages) {
					ret = -EINVAL;
					break;
				}

				if (copy_to_user((void *) arg, PATCH(&card->mgr, ctl->val[0] - 1), sizeof(struct dsp_patch)))
					ret = -EFAULT;
			}

			break;

		case CMD_GETGPR:
			id = ctl->val[0];

			if (id > NUM_GPRS) {
				ret = -EINVAL;
				break;
			}

			if (copy_to_user((void *) arg, &card->mgr.gpr[id], sizeof(struct dsp_gpr)))
				ret = -EFAULT;

			break;

		case CMD_GETCTLGPR:
			addr = emu10k1_find_control_gpr(&card->mgr, (char *) ctl->val, &((char *) ctl->val)[PATCH_NAME_SIZE]);
			ctl->val[0] = sblive_readptr(card, addr, 0);

			if (copy_to_user((void *) arg, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_SETPATCH:
			if (ctl->val[0] == 0)
				memcpy(&card->mgr.rpatch, &ctl->val[1], sizeof(struct dsp_rpatch));
			else {
				page = (ctl->val[0] - 1) / PATCHES_PER_PAGE;
				if (page > MAX_PATCHES_PAGES) {
					ret = -EINVAL;
					break;
				}

				if (page >= card->mgr.current_pages) {
					for(i = card->mgr.current_pages; i < page + 1; i++) {
				                card->mgr.patch[i] = (void *)__get_free_pages(GFP_KERNEL, 1);
						if(card->mgr.patch[i] == NULL) {
							card->mgr.current_pages = i;
							ret = -ENOMEM;
							break;
						}
						memset(card->mgr.patch[i], 0, PAGE_SIZE);
					}
					card->mgr.current_pages = page + 1;
				}

				patch = PATCH(&card->mgr, ctl->val[0] - 1);

				memcpy(patch, &ctl->val[1], sizeof(struct dsp_patch));

				if(patch->code_size == 0) {
					for(i = page + 1; i < card->mgr.current_pages; i++)
                                                free_page((unsigned long) card->mgr.patch[i]);

					card->mgr.current_pages = page + 1;
				}
			}
			break;

		case CMD_SETGPR:
			if (ctl->val[0] > NUM_GPRS) {
				ret = -EINVAL;
				break;
			}

			memcpy(&card->mgr.gpr[ctl->val[0]], &ctl->val[1], sizeof(struct dsp_gpr));
			break;

		case CMD_SETCTLGPR:
			addr = emu10k1_find_control_gpr(&card->mgr, (char *) ctl->val, (char *) ctl->val + PATCH_NAME_SIZE);
			emu10k1_set_control_gpr(card, addr, *((s32 *)((char *) ctl->val + 2 * PATCH_NAME_SIZE)), 0);
			break;


		case CMD_SETGPOUT:
			if( ctl->val[0]>2 || ctl->val[1]>1){
				ret= -EINVAL;
				break;
			}
			emu10k1_writefn0(card, (1<<24)| (((ctl->val[0])+10)<<16 ) | HCFG ,ctl->val[1]);
			break;

		case CMD_GETGPR2OSS:
			id = ctl->val[0];
			ch = ctl->val[1];

			if (id >= SOUND_MIXER_NRDEVICES || ch >= 2) {
				ret = -EINVAL;
				break;
			}

			ctl->val[2] = card->mgr.ctrl_gpr[id][ch];

			if (copy_to_user((void *) arg, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;
			break;

		case CMD_SETGPR2OSS:
			id = ctl->val[0];
			ch = ctl->val[1];
			addr = ctl->val[2];

			if (id >= SOUND_MIXER_NRDEVICES || ch >= 2) {
				ret = -EINVAL;
				break;
			}

			card->mgr.ctrl_gpr[id][ch] = addr;

			if (card->isaps)
				break;

			if (addr >= 0) {
				unsigned int state = card->ac97.mixer_state[id];

				if (ch) {
					state >>= 8;
					card->ac97.stereo_mixers |= (1<<id);
				} else {
					card->ac97.supported_mixers |= (1<<id);
				}

				emu10k1_set_volume_gpr(card, addr, state & 0xff,
						       volume_params[id].scale,
						       volume_params[id].muting);
			} else {
				if (ch) {
					card->ac97.stereo_mixers &= ~(1<<id);
					card->ac97.stereo_mixers |= card->ac97_stereo_mixers;
				} else {
					card->ac97.supported_mixers &= ~(1<<id);
					card->ac97.supported_mixers |= card->ac97_supported_mixers;
				}
			}
			break;
		case CMD_SETPASSTHROUGH:
			card->pt.selected = ctl->val[0] ? 1 : 0;
			if (card->pt.state != PT_STATE_INACTIVE)
				break;
			card->pt.spcs_to_use = ctl->val[0] & 0x07;
			break;
		default:
			ret = -EINVAL;
			break;
		}

		kfree(ctl);
		return ret;
		break;

	case SOUND_MIXER_PRIVATE4:

		if (copy_from_user(&size, (void *) arg, sizeof(size)))
			return -EFAULT;

		DPD(2, "External tram size %#x\n", size);

		if (size > 0x1fffff)
			return -EINVAL;

		size_reg = 0;

		if (size != 0) {
			size = (size - 1) >> 14;

			while (size) {
				size >>= 1;
				size_reg++;
			}

			size = 0x4000 << size_reg;
		}

		DPD(2, "External tram size %#x %#x\n", size, size_reg);

		if (size != card->tankmem.size) {
			if (card->tankmem.size > 0) {
				emu10k1_writefn0(card, HCFG_LOCKTANKCACHE, 1);

				sblive_writeptr_tag(card, 0, TCB, 0, TCBS, 0, TAGLIST_END);

				pci_free_consistent(card->pci_dev, card->tankmem.size, card->tankmem.addr, card->tankmem.dma_handle);

				card->tankmem.size = 0;
			}

			if (size != 0) {
				card->tankmem.addr = pci_alloc_consistent(card->pci_dev, size, &card->tankmem.dma_handle);
				if (card->tankmem.addr == NULL)
					return -ENOMEM;

				card->tankmem.size = size;

				sblive_writeptr_tag(card, 0, TCB, card->tankmem.dma_handle, TCBS, size_reg, TAGLIST_END);

				emu10k1_writefn0(card, HCFG_LOCKTANKCACHE, 0);
			}
		}
		return 0;
		break;

	default:
		break;
	}

	return -EINVAL;
}


	
static int emu10k1_mixer_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;
	struct emu10k1_card *card = file->private_data;

	ret = -EINVAL;
	if (!card->isaps)
		ret = card->ac97.mixer_ioctl(&card->ac97, cmd, arg);

	if (ret < 0)
		ret = emu10k1_private_mixer(card, cmd, arg);
	else{
		unsigned int oss_mixer, left, right;

		oss_mixer = _IOC_NR(cmd);

		if ((_IOC_DIR(cmd) == (_IOC_WRITE|_IOC_READ)) && oss_mixer<=SOUND_MIXER_NRDEVICES ) {

			left = card->ac97.mixer_state[oss_mixer] & 0xff;
			right = (card->ac97.mixer_state[oss_mixer] >> 8) & 0xff;
			if(card->ac97.supported_mixers|(1<<oss_mixer))
				emu10k1_set_volume_gpr(card, card->mgr.ctrl_gpr[oss_mixer][0], left,
						       volume_params[oss_mixer].scale,
						       volume_params[oss_mixer].muting);
			if(card->ac97.stereo_mixers |(1<<oss_mixer))
				emu10k1_set_volume_gpr(card, card->mgr.ctrl_gpr[oss_mixer][1], right,
						       volume_params[oss_mixer].scale,
						       volume_params[oss_mixer].muting);
		}
		
	}
	
	
		
	return ret;
}

static int emu10k1_mixer_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct emu10k1_card *card = NULL;
	struct list_head *entry;

	DPF(4, "emu10k1_mixer_open()\n");

	list_for_each(entry, &emu10k1_devs) {
		card = list_entry(entry, struct emu10k1_card, list);

		if (card->ac97.dev_mixer == minor)
			goto match;
	}

	return -ENODEV;

      match:
	file->private_data = card;
	return 0;
}

static int emu10k1_mixer_release(struct inode *inode, struct file *file)
{
	DPF(4, "emu10k1_mixer_release()\n");
	return 0;
}

struct file_operations emu10k1_mixer_fops = {
	owner:		THIS_MODULE,
	llseek:		emu10k1_mixer_llseek,
	ioctl:		emu10k1_mixer_ioctl,
	open:		emu10k1_mixer_open,
	release:	emu10k1_mixer_release,
};
