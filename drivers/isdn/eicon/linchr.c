
/*
 *
 * Copyright (C) Eicon Technology Corporation, 2000.
 *
 * This source file is supplied for the exclusive use with Eicon
 * Technology Corporation's range of DIVA Server Adapters.
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


#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/malloc.h>

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
	dia_load_t *pDivaLoad;
	dia_start_t *pDivaStart;
	dia_config_t *pDivaConfig;
	dia_log_t *pDivaLog;
	byte *pUserCards, card_i;
	word wCardNum;
	mem_block_t *mem_block;

	switch (command)
	{
		case DIA_IOCTL_CONFIG:
			pDivaConfig = (dia_config_t *) arg;
			
			if (!verify_area(VERIFY_READ, pDivaConfig, sizeof(dia_config_t)))
			{
				DivasCardConfig(pDivaConfig);
			}
			else
			{
				printk(KERN_WARNING "Divas: Unable to complete CONFIG ioctl (verify area failed)\n");
				return -1;
			}
				return 0;

		case DIA_IOCTL_DETECT:
			pUserCards = (byte *) arg;

			if (!verify_area(VERIFY_WRITE, pUserCards, 20))
			{
				put_user(DivasCardNext, pUserCards++);

				for (card_i=1; card_i < 20; card_i++)
				{
					put_user((byte) DivasCards[card_i - 1].cfg.card_type, pUserCards++);
				}
			}
			else
			{
				printk(KERN_WARNING "Divas: Unable to complete DETECT ioctl (verify area failed)\n");
				return -1;
			}
			return 0;

		case DIA_IOCTL_START:
			pDivaStart = (dia_start_t *) arg;
			
			if (!verify_area(VERIFY_READ, pDivaStart, sizeof(dia_start_t)))
			{
				return DivasCardStart(pDivaStart->card_id);
			}
			else
			{
				printk(KERN_WARNING "Divas: Unable to complete START ioctl (verify area failed)\n");
				return -1;
			}


		case DIA_IOCTL_FLAVOUR:
			return 0;

		case DIA_IOCTL_LOAD:
			pDivaLoad = (dia_load_t *) arg;
			if (!verify_area(VERIFY_READ, pDivaLoad->code,pDivaLoad->length))
			{
				if (DivasCardLoad(pDivaLoad))
				{
					printk(KERN_WARNING "Divas: Error loading DIVA Server adapter\n");
					return -EINVAL;
				}
			}
			else
			{
				printk(KERN_WARNING "Divas: Error in LOAD parameters (verify failed)\n");
				return -EINVAL;
			}
			return 0;

		case DIA_IOCTL_LOG:
			pDivaLog = (dia_log_t *) arg;
			
			if (!verify_area(VERIFY_READ, pDivaLog, sizeof(dia_log_t)))
			{
				DivasLog(pDivaLog);
			}
			else
			{
				printk(KERN_WARNING "Divas: Unable to complete LOG ioctl (verify area failed)\n");
				return -1;
			}
			return 0;

		case DIA_IOCTL_XLOG_REQ:
			
			if (!verify_area(VERIFY_READ, (void *)arg, sizeof(word)))
			{
				wCardNum = * (word *) arg;
				DivasXlogReq(wCardNum);
			}
			else
			{
				printk(KERN_WARNING "Divas: Unable to complete XLOG_REQ ioctl (verify area failed)\n");
				return -1;
			}
			return 0;

		case DIA_IOCTL_GET_NUM:
			
			if (!verify_area(VERIFY_WRITE, (void *)arg, sizeof(int)))
			{
				* (int *) arg = DivasCardNext;
			}
			else
			{
				printk(KERN_WARNING "Divas: Unable to complete GET_NUM ioctl (verify area failed)\n");
				return -1;
			}
			return 0;

		case DIA_IOCTL_GET_LIST:
			DPRINTF(("divas: DIA_IOCTL_GET_LIST"));
			
			if (!verify_area(VERIFY_WRITE, (void *)arg, sizeof(dia_card_list_t)))
			{
				DivasGetList((dia_card_list_t *)arg);
			}
			else
			{
				printk(KERN_WARNING "Divas: Unable to complete GET_LIST ioctl (verify area failed)\n");
				return -1;
			}
			return 0;

		case DIA_IOCTL_GET_MEM:
			mem_block = (mem_block_t *) arg;
			
			if (!verify_area(VERIFY_WRITE, mem_block, sizeof(mem_block_t)))
			{
				DivasGetMem(mem_block);
			}
			else
			{
				printk(KERN_WARNING "Divas: Unable to complete GET_MEM ioctl (verify area failed)\n");
				return -1;
			}
			return 0;

		case DIA_IOCTL_UNLOCK:
			UnlockDivas();
			return 0;

		default:
			printk(KERN_WARNING "Divas: Unknown IOCTL Received by DIVA Server Driver(%d)\n", command);
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
int private_usage_count;
extern void mod_inc_use_count(void);
extern void mod_dec_use_count(void);

int do_open(struct inode *pInode, struct file *pFile)
{
#if defined(MODULE)
	mod_inc_use_count();
	private_usage_count++;
#endif
	return 0;
}

int do_release(struct inode *pInode, struct file *pFile)
{
#if defined(MODULE)
	mod_dec_use_count();
	private_usage_count--;
#endif
	return 0;
}

void UnlockDivas(void)
{
	while (private_usage_count > 0)
	{
		private_usage_count--;
#if defined(MODULE)
		mod_dec_use_count();
#endif
	}
}
