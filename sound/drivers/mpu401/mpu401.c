/*
 *  Driver for generic MPU-401 boards (UART mode only)
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
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/mpu401.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include <linux/delay.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("MPU-401 UART");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* MPU-401 port number */
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* MPU-401 IRQ */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for MPU-401 device.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for MPU-401 device.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable MPU-401 device.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for MPU-401 device.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for MPU-401 device.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_IRQ_DESC);

static snd_card_t *snd_mpu401_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

static int __init snd_card_mpu401_probe(int dev)
{
	snd_card_t *card;
	int err;

	if (snd_port[dev] == SNDRV_AUTO_PORT) {
		snd_printk("specify snd_port\n");
		return -EINVAL;
	}
	if (snd_irq[dev] == SNDRV_AUTO_IRQ) {
		snd_printk("specify or disable IRQ port\n");
		return -EINVAL;
	}

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	if (snd_mpu401_uart_new(card, 0, MPU401_HW_MPU401,
				snd_port[dev], 0,
				snd_irq[dev], snd_irq[dev] >= 0 ? SA_INTERRUPT : 0, NULL) < 0) {
		printk(KERN_ERR "MPU401 not detected at 0x%lx\n", snd_port[dev]);
		snd_card_free(card);
		return -ENODEV;
	}
	strcpy(card->driver, "MPU-401 UART");
	strcpy(card->shortname, card->driver);
	sprintf(card->longname, "%s at 0x%lx, ", card->shortname, snd_port[dev]);
	if (snd_irq[dev] >= 0) {
		sprintf(card->longname + strlen(card->longname), "IRQ %d", snd_irq[dev]);
	} else {
		strcat(card->longname, "polled");
	}
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_mpu401_cards[dev] = card;
	return 0;
}

static int __init alsa_card_mpu401_init(void)
{
	int dev, cards = 0;

	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev])
			continue;
		if (snd_card_mpu401_probe(dev) >= 0)
			cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "MPU-401 device not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_mpu401_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_mpu401_cards[idx]);
}

module_init(alsa_card_mpu401_init)
module_exit(alsa_card_mpu401_exit)

#ifndef MODULE

/* format is: snd-mpu401=snd_enable,snd_index,snd_id,snd_port,snd_irq */

static int __init alsa_card_mpu401_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-mpu401=", alsa_card_mpu401_setup);

#endif /* ifndef MODULE */
