/*
 *  Initialization routines
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>

int snd_cards_count = 0;
static unsigned int snd_cards_lock = 0;	/* locked for registering/using */
snd_card_t *snd_cards[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = NULL};
rwlock_t snd_card_rwlock = RW_LOCK_UNLOCKED;

#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
int (*snd_mixer_oss_notify_callback)(snd_card_t *card, int free_flag);
#endif

static void snd_card_id_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	snd_iprintf(buffer, "%s\n", entry->card->id);
}

snd_card_t *snd_card_new(int idx, const char *xid,
			 struct module *module, int extra_size)
{
	snd_card_t *card;
	snd_info_entry_t *entry;
	int err;

	if (extra_size < 0)
		extra_size = 0;
	card = (snd_card_t *) snd_kcalloc(sizeof(snd_card_t) + extra_size, GFP_KERNEL);
	if (card == NULL)
		return NULL;
	if (xid) {
		if (!snd_info_check_reserved_words(xid))
			goto __error;
		strncpy(card->id, xid, sizeof(card->id) - 1);
	}
	write_lock(&snd_card_rwlock);
	if (idx < 0) {
		int idx2;
		for (idx2 = 0; idx2 < snd_ecards_limit; idx2++)
			if (!(snd_cards_lock & (1 << idx2))) {
				idx = idx2;
				break;
			}
	} else if (idx < snd_ecards_limit) {
		if (snd_cards_lock & (1 << idx))
			idx = -1;	/* invalid */
	}
	if (idx < 0 || idx >= snd_ecards_limit) {
		write_unlock(&snd_card_rwlock);
		if (idx >= snd_ecards_limit)
			snd_printk(KERN_ERR "card %i is out of range (0-%i)\n", idx, snd_ecards_limit-1);
		goto __error;
	}
	snd_cards_lock |= 1 << idx;		/* lock it */
	write_unlock(&snd_card_rwlock);
	card->number = idx;
	if (!card->id[0])
		sprintf(card->id, "card%i", card->number);
	card->module = module;
	INIT_LIST_HEAD(&card->devices);
	rwlock_init(&card->control_rwlock);
	rwlock_init(&card->control_owner_lock);
	INIT_LIST_HEAD(&card->controls);
	INIT_LIST_HEAD(&card->ctl_files);
#ifdef CONFIG_PM
	init_MUTEX(&card->power_lock);
	init_waitqueue_head(&card->power_sleep);
#endif
	/* the control interface cannot be accessed from the user space until */
	/* snd_cards_bitmask and snd_cards are set with snd_card_register */
	if ((err = snd_ctl_register(card)) < 0) {
		snd_printd("unable to register control minors\n");
		goto __error;
	}
	if ((err = snd_info_card_register(card)) < 0) {
		snd_printd("unable to register card info\n");
		goto __error_ctl;
	}
	if ((entry = snd_info_create_card_entry(card, "id", card->proc_root)) == NULL) {
		snd_printd("unable to create card entry\n");
		goto __error_info;
	}
	entry->content = SNDRV_INFO_CONTENT_TEXT;
	entry->c.text.read_size = PAGE_SIZE;
	entry->c.text.read = snd_card_id_read;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		goto __error_info;
	}
	card->proc_id = entry;
	if (extra_size > 0)
		card->private_data = (char *)card + sizeof(snd_card_t);
	return card;

      __error_info:
      	snd_info_card_unregister(card);
      __error_ctl:
	snd_ctl_unregister(card);
      __error:
	kfree(card);
      	return NULL;
}

int snd_card_free(snd_card_t * card)
{
	if (card == NULL)
		return -EINVAL;
	write_lock(&snd_card_rwlock);
	snd_cards[card->number] = NULL;
	snd_cards_count--;
	write_unlock(&snd_card_rwlock);
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
	if (snd_mixer_oss_notify_callback)
		snd_mixer_oss_notify_callback(card, 1);
#endif
	if (snd_device_free_all(card, SNDRV_DEV_CMD_PRE) < 0) {
		snd_printk(KERN_ERR "unable to free all devices (pre)\n");
		/* Fatal, but this situation should never occur */
	}
	if (snd_device_free_all(card, SNDRV_DEV_CMD_NORMAL) < 0) {
		snd_printk(KERN_ERR "unable to free all devices (normal)\n");
		/* Fatal, but this situation should never occur */
	}
	if (snd_ctl_unregister(card) < 0) {
		snd_printk(KERN_ERR "unable to unregister control minors\n");
		/* Not fatal error */
	}
	if (snd_device_free_all(card, SNDRV_DEV_CMD_POST) < 0) {
		snd_printk(KERN_ERR "unable to free all devices (post)\n");
		/* Fatal, but this situation should never occur */
	}
	if (card->private_free)
		card->private_free(card);
	snd_info_free_entry(card->proc_id);
	if (snd_info_card_unregister(card) < 0) {
		snd_printk(KERN_WARNING "unable to unregister card info\n");
		/* Not fatal error */
	}
	write_lock(&snd_card_rwlock);
	snd_cards_lock &= ~(1 << card->number);
	write_unlock(&snd_card_rwlock);
	kfree(card);
	return 0;
}

int snd_card_register(snd_card_t * card)
{
	int err;

	snd_runtime_check(card != NULL, return -EINVAL);
	if ((err = snd_device_register_all(card)) < 0)
		return err;
	write_lock(&snd_card_rwlock);
	if (snd_cards[card->number]) {
		/* already registered */
		write_unlock(&snd_card_rwlock);
		return 0;
	}
	snd_cards[card->number] = card;
	snd_cards_count++;
	write_unlock(&snd_card_rwlock);
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
	if (snd_mixer_oss_notify_callback)
		snd_mixer_oss_notify_callback(card, 0);
#endif
	return 0;
}

static snd_info_entry_t *snd_card_info_entry = NULL;

static void snd_card_info_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	int idx, count;
	snd_card_t *card;

	for (idx = count = 0; idx < SNDRV_CARDS; idx++) {
		read_lock(&snd_card_rwlock);
		if ((card = snd_cards[idx]) != NULL) {
			count++;
			snd_iprintf(buffer, "%i [%-15s]: %s - %s\n",
					idx,
					card->id,
					card->driver,
					card->shortname);
			snd_iprintf(buffer, "                     %s\n",
					card->longname);
		}
		read_unlock(&snd_card_rwlock);
	}
	if (!count)
		snd_iprintf(buffer, "--- no soundcards ---\n");
}

#ifdef CONFIG_SND_OSSEMUL

void snd_card_info_read_oss(snd_info_buffer_t * buffer)
{
	int idx, count;
	snd_card_t *card;

	for (idx = count = 0; idx < SNDRV_CARDS; idx++) {
		read_lock(&snd_card_rwlock);
		if ((card = snd_cards[idx]) != NULL) {
			count++;
			snd_iprintf(buffer, "%s\n", card->longname);
		}
		read_unlock(&snd_card_rwlock);
	}
	if (!count) {
		snd_iprintf(buffer, "--- no soundcards ---\n");
	}
}

#endif

int __init snd_card_info_init(void)
{
	snd_info_entry_t *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "cards", NULL);
	snd_runtime_check(entry != NULL, return -ENOMEM);
	entry->content = SNDRV_INFO_CONTENT_TEXT;
	entry->c.text.read_size = PAGE_SIZE;
	entry->c.text.read = snd_card_info_read;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return -ENOMEM;
	}
	snd_card_info_entry = entry;
	
	return 0;
}

int __exit snd_card_info_done(void)
{
	if (snd_card_info_entry)
		snd_info_unregister(snd_card_info_entry);
	return 0;
}

int snd_component_add(snd_card_t *card, const char *component)
{
	char *ptr;
	int len = strlen(component);

	ptr = strstr(card->components, component);
	if (ptr != NULL) {
		if (ptr[len] == '\0' || ptr[len] == ' ')	/* already there */
			return 1;
	}
	if (strlen(card->components) + 1 + len + 1 > sizeof(card->components)) {
		snd_BUG();
		return -ENOMEM;
	}
	if (card->components[0] != '\0')
		strcat(card->components, " ");
	strcat(card->components, component);
	return 0;
}

#ifdef CONFIG_PM
/* the power lock must be active before call */
void snd_power_wait(snd_card_t *card)
{
	wait_queue_t wait;

	init_waitqueue_entry(&wait, current);
	add_wait_queue(&card->power_sleep, &wait);
	snd_power_unlock(card);
	schedule_timeout(30 * HZ);
	remove_wait_queue(&card->power_sleep, &wait);
	snd_power_lock(card);
}
#endif /* CONFIG_PM */
