
/*
 *
 * Copyright (C) Eicon Technology Corporation, 2000.
 *
 * Eicon File Revision :    1.12  
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY 
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/slab.h>

#undef N_DATA

#include "adapter.h"
#include "divas.h"
#include "divalog.h"

extern int DivasCardNext;
void UxPause(long ms);
void bcopy(void *pSource, void *pDest, dword dwLength);
int DivasGetMem(mem_block_t *);

#define DIA_IOCTL_UNLOCK 12
void UnlockDivas(void);

int do_ioctl(struct inode *pDivasInode, struct file *pDivasFile, 
			 unsigned int command, unsigned long arg)
{
	byte *pUserCards, card_i;
	word wCardNum;

	switch (command)
	{
		case DIA_IOCTL_CONFIG:
		{
			dia_config_t DivaConfig;
			if (copy_from_user(&DivaConfig, (void *)arg, sizeof(dia_config_t)))
				return -EFAULT;
			DivasCardConfig(&DivaConfig);
			return 0;
		}

		case DIA_IOCTL_DETECT:
			pUserCards = (byte *) arg;

			if (!verify_area(VERIFY_WRITE, pUserCards, 20))
			{
				if(__put_user(DivasCardNext, pUserCards++))
					return -EFAULT;

				for (card_i=1; card_i < 20; card_i++)
				{
					if(__put_user((byte) DivasCards[card_i - 1].cfg.card_type, pUserCards++))
						return -EFAULT;
				}
			}
			else return -EFAULT;

			return 0;

		case DIA_IOCTL_START:
		{
			dia_start_t DivaStart;
			if (copy_from_user(&DivaStart, (void *)arg, sizeof(dia_start_t)))
				return -EFAULT;
			return DivasCardStart(DivaStart.card_id);
		}

		case DIA_IOCTL_FLAVOUR:
			return 0;

		case DIA_IOCTL_LOAD:
		{
			dia_load_t DivaLoad;
			if(copy_from_user(&DivaLoad, (void *)arg, sizeof(dia_load_t)))
				return -EFAULT;
			if (!verify_area(VERIFY_READ, DivaLoad.code,DivaLoad.length))
			{
				if (DivasCardLoad(&DivaLoad))
				{
					printk(KERN_WARNING "Divas: Error loading DIVA Server adapter\n");
					return -EINVAL;
				}
				return 0;
			}
			return -EFAULT;
		}
		case DIA_IOCTL_LOG:
		{
			dia_log_t DivaLog;
			if (copy_from_user(&DivaLog, (void *) arg, sizeof(dia_log_t)))
				return -EFAULT;
			DivasLog(&DivaLog);
			return 0;
		}

		case DIA_IOCTL_XLOG_REQ:
			if(get_user(wCardNum, (word *) arg))
				return -EFAULT;
			DivasXlogReq(wCardNum);
			return 0;

		case DIA_IOCTL_GET_NUM:
			if(put_user(DivasCardNext, (int *)arg))
				return -EFAULT;
			return 0;

		case DIA_IOCTL_GET_LIST:
		{
			dia_card_list_t cards;
			DPRINTF(("divas: DIA_IOCTL_GET_LIST"));
			DivasGetList(&cards);
			if(copy_to_user((void *)arg, &cards, sizeof(cards)))
				return -EFAULT;
			return 0;
		}
		case DIA_IOCTL_GET_MEM:
		{
			mem_block_t mem_block;
			if (copy_from_user(&mem_block, (void *)arg, sizeof(mem_block_t)))
				return -EFAULT;
			DivasGetMem(&mem_block);
			return 0;
		}

		case DIA_IOCTL_UNLOCK:
			UnlockDivas();
			return 0;

		default:
			return -EINVAL;
	}
	return -EINVAL;
}

unsigned int do_poll(struct file *pFile, struct poll_table_struct *pPollTable)
{
	word wMask = 0;

    if (!DivasLogFifoEmpty())
    {
		wMask |= POLLIN | POLLRDNORM;
	}

	return wMask;
}

ssize_t do_read(struct file *pFile, char *pUserBuffer, size_t BufferSize, loff_t *pOffset)
{
	klog_t *pClientLogBuffer = (klog_t *) pUserBuffer;
	klog_t *pHeadItem;

	if (BufferSize < sizeof(klog_t))
	{
		printk(KERN_WARNING "Divas: Divalog buffer specifed a size that is too small (%d - %d required)\n",
			BufferSize, sizeof(klog_t));
		return 0;
	}

	pHeadItem = (klog_t *) DivasLogFifoRead();

	if (pHeadItem)
	{
		bcopy(pHeadItem, pClientLogBuffer, sizeof(klog_t));
		kfree(pHeadItem);
		return sizeof(klog_t);
	}

	return 0;
}
static int private_usage_count;

int do_open(struct inode *pInode, struct file *pFile)
{
	MOD_INC_USE_COUNT;
#ifdef MODULE
	private_usage_count++;
#endif
	return 0;
}

int do_release(struct inode *pInode, struct file *pFile)
{
	MOD_DEC_USE_COUNT;
#ifdef MODULE
	private_usage_count--;
#endif
	return 0;
}

void UnlockDivas(void)
{
	while (private_usage_count > 0)
	{
		private_usage_count--;
		MOD_DEC_USE_COUNT;
	}
}
