/*
 * Driver for PowerMac AWACS
 * Copyright (c) 2001 by Takashi Iwai <tiwai@suse.de>
 *   based on dmasound.c.
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
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <sound/core.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include "pmac.h"
#include "awacs.h"
#include "burgundy.h"

#define CHIP_NAME "PMac"

MODULE_DESCRIPTION("PowerMac");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Apple,PowerMac}}");
MODULE_LICENSE("GPL");

static int snd_index = SNDRV_DEFAULT_IDX1;		/* Index 0-MAX */
static char *snd_id = SNDRV_DEFAULT_STR1;		/* ID for this card */
static int snd_enable = 1;
static int snd_enable_beep = 1;

MODULE_PARM(snd_index, "i");
MODULE_PARM_DESC(snd_index, "Index value for " CHIP_NAME " soundchip.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "s");
MODULE_PARM_DESC(snd_id, "ID string for " CHIP_NAME " soundchip.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "i");
MODULE_PARM_DESC(snd_enable, "Enable this soundchip.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_enable_beep, "i");
MODULE_PARM_DESC(snd_enable_beep, "Enable beep using PCM.");
MODULE_PARM_SYNTAX(snd_enable_beep, SNDRV_ENABLED "," SNDRV_BOOLEAN_TRUE_DESC);


/*
 * card entry
 */

static snd_card_t *snd_pmac_card = NULL;

/*
 */

static int __init snd_pmac_probe(void)
{
	snd_card_t *card;
	pmac_t *chip;
	char *name_ext;
	int err;

	card = snd_card_new(snd_index, snd_id, THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	if ((err = snd_pmac_new(card, &chip)) < 0)
		goto __error;

	switch (chip->model) {
	case PMAC_BURGUNDY:
		strcpy(card->driver, "PMac Burgundy");
		strcpy(card->shortname, "PowerMac Burgundy");
		sprintf(card->longname, "%s (Dev %d) Sub-frame %d",
			card->shortname, chip->device_id, chip->subframe);
		if ((err = snd_pmac_burgundy_init(chip)) < 0)
			goto __error;
		break;
	case PMAC_DACA:
		strcpy(card->driver, "PMac DACA");
		strcpy(card->shortname, "PowerMac DACA");
		sprintf(card->longname, "%s (Dev %d) Sub-frame %d",
			card->shortname, chip->device_id, chip->subframe);
		if ((err = snd_pmac_daca_init(chip)) < 0)
			goto __error;
		break;
	case PMAC_TUMBLER:
	case PMAC_SNAPPER:
		name_ext = chip->model == PMAC_TUMBLER ? "Tumbler" : "Snapper";
		sprintf(card->driver, "PMac %s", name_ext);
		sprintf(card->shortname, "PowerMac %s", name_ext);
		sprintf(card->longname, "%s (Dev %d) Sub-frame %d",
			card->shortname, chip->device_id, chip->subframe);
		if ((err = snd_pmac_tumbler_init(chip)) < 0)
			goto __error;
		break;
	case PMAC_AWACS:
	case PMAC_SCREAMER:
		name_ext = chip->model == PMAC_SCREAMER ? "Screamer" : "AWACS";
		sprintf(card->driver, "PMac %s", name_ext);
		sprintf(card->shortname, "PowerMac %s", name_ext);
		if (chip->is_pbook_3400)
			name_ext = " [PB3400]";
		else if (chip->is_pbook_G3)
			name_ext = " [PBG3]";
		else
			name_ext = "";
		sprintf(card->longname, "%s%s Rev %d",
			card->shortname, name_ext, chip->revision);
		if ((err = snd_pmac_awacs_init(chip)) < 0)
			goto __error;
		break;
	default:
		snd_printk("unsupported hardware %d\n", chip->model);
		err = -EINVAL;
		goto __error;
	}

	if ((err = snd_pmac_pcm_new(chip)) < 0)
		goto __error;

	chip->initialized = 1;
	if (snd_enable_beep)
		snd_pmac_attach_beep(chip);

	if ((err = snd_card_register(card)) < 0)
		goto __error;

	snd_pmac_card = card;
	return 0;

__error:
	snd_card_free(card);
	return err;
}


/*
 * MODULE sutff
 */

static int __init alsa_card_pmac_init(void)
{
	int err;
	if ((err = snd_pmac_probe() < 0)) {
#ifdef MODULE
		printk(KERN_ERR "no PMac soundchip found\n");
#endif
		return err;
	}
	return 0;

}

static void __exit alsa_card_pmac_exit(void)
{
	if (snd_pmac_card)
		snd_card_free(snd_pmac_card);
}

module_init(alsa_card_pmac_init)
module_exit(alsa_card_pmac_exit)

#ifndef MODULE

/* format is: snd-pmac=snd_enable,snd_index,snd_id,snd_enable_beep
 */

static int __init alsa_card_pmac_setup(char *str)
{
	(void)(get_option(&str,&snd_enable) == 2 &&
	       get_option(&str,&snd_index) == 2 &&
	       get_id(&str,&snd_id) == 2 &&
	       get_option(&str,&snd_enable_beep) == 2
	       );
	return 1;
}

__setup("snd-pmac=", alsa_card_pmac_setup);

#endif /* ifndef MODULE */
