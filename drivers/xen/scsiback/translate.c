/*
 * Xen SCSI backend driver
 *
 * Copyright (c) 2008, FUJITSU Limited
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/list.h>
#include <linux/gfp.h>

#include "common.h"

/*
  Initialize the translation entry list
*/
void scsiback_init_translation_table(struct vscsibk_info *info)
{
	INIT_LIST_HEAD(&info->v2p_entry_lists);
	spin_lock_init(&info->v2p_lock);
}


/*
  Add a new translation entry
*/
int scsiback_add_translation_entry(struct vscsibk_info *info,
			struct scsi_device *sdev, struct ids_tuple *v)
{
	int err = 0;
	struct v2p_entry *entry;
	struct v2p_entry *new;
	struct list_head *head = &(info->v2p_entry_lists);
	unsigned long flags;
	
	spin_lock_irqsave(&info->v2p_lock, flags);

	/* Check double assignment to identical virtual ID */
	list_for_each_entry(entry, head, l) {
		if ((entry->v.chn == v->chn) &&
		    (entry->v.tgt == v->tgt) &&
		    (entry->v.lun == v->lun)) {
			printk(KERN_WARNING "scsiback: Virtual ID is already used. "
			       "Assignment was not performed.\n");
			err = -EEXIST;
			goto out;
		}

	}

	/* Create a new translation entry and add to the list */
	if ((new = kmalloc(sizeof(struct v2p_entry), GFP_ATOMIC)) == NULL) {
		printk(KERN_ERR "scsiback: %s: kmalloc() error.\n", __FUNCTION__);
		err = -ENOMEM;
		goto out;
	}
	new->v = *v;
	new->sdev = sdev;
	list_add_tail(&new->l, head);

out:	
	spin_unlock_irqrestore(&info->v2p_lock, flags);
	return err;
}


/*
  Delete the translation entry specfied
*/
int scsiback_del_translation_entry(struct vscsibk_info *info,
				struct ids_tuple *v)
{
	struct v2p_entry *entry;
	struct list_head *head = &(info->v2p_entry_lists);
	unsigned long flags;

	spin_lock_irqsave(&info->v2p_lock, flags);
	/* Find out the translation entry specified */
	list_for_each_entry(entry, head, l) {
		if ((entry->v.chn == v->chn) &&
		    (entry->v.tgt == v->tgt) &&
		    (entry->v.lun == v->lun)) {
			goto found;
		}
	}

	spin_unlock_irqrestore(&info->v2p_lock, flags);
	return 1;

found:
	/* Delete the translation entry specfied */
	scsi_device_put(entry->sdev);
	list_del(&entry->l);
	kfree(entry);

	spin_unlock_irqrestore(&info->v2p_lock, flags);
	return 0;
}


/*
  Perform virtual to physical translation
*/
struct scsi_device *scsiback_do_translation(struct vscsibk_info *info,
			struct ids_tuple *v)
{
	struct v2p_entry *entry;
	struct list_head *head = &(info->v2p_entry_lists);
	struct scsi_device *sdev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&info->v2p_lock, flags);
	list_for_each_entry(entry, head, l) {
		if ((entry->v.chn == v->chn) &&
		    (entry->v.tgt == v->tgt) &&
		    (entry->v.lun == v->lun)) {
			sdev = entry->sdev;
			goto out;
		}
	}
out:
	spin_unlock_irqrestore(&info->v2p_lock, flags);
	return sdev;
}


/*
  Release the translation entry specfied
*/
void scsiback_release_translation_entry(struct vscsibk_info *info)
{
	struct v2p_entry *entry, *tmp;
	struct list_head *head = &(info->v2p_entry_lists);
	unsigned long flags;

	spin_lock_irqsave(&info->v2p_lock, flags);
	list_for_each_entry_safe(entry, tmp, head, l) {
		scsi_device_put(entry->sdev);
		list_del(&entry->l);
		kfree(entry);
	}

	spin_unlock_irqrestore(&info->v2p_lock, flags);
	return;

}
