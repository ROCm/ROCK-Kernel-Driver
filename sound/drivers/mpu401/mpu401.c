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

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* MPU-401 port number */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* MPU-401 IRQ */
#ifdef CONFIG_X86_PC9800
static int pc98ii[SNDRV_CARDS];				/* PC98-II dauther board */
#endif

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for MPU-401 device.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for MPU-401 device.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable MPU-401 device.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for MPU-401 device.");
MODULE_PARM_SYNTAX(port, SNDRV_PORT12_DESC);
MODULE_PARM(irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(irq, "IRQ # for MPU-401 device.");
MODULE_PARM_SYNTAX(irq, SNDRV_IRQ_DESC);
#ifdef CONFIG_X86_PC9800
MODULE_PARM(pc98ii, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(pc98ii, "Roland MPU-PC98II support.");
MODULE_PARM_SYNTAX(pc98ii, SNDRV_BOOLEAN_FALSE_DESC);
#endif

static snd_card_t *snd_mpu401_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

static int __init snd_card_mpu401_probe(int dev)
{
	snd_card_t *card;
	int err;

	if (port[dev] == SNDRV_AUTO_PORT) {
		snd_printk("specify port\n");
		return -EINVAL;
	}
	if (irq[dev] == SNDRV_AUTO_IRQ) {
		snd_printk("specify or disable IRQ port\n");
		return -EINVAL;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	if (snd_mpu401_uart_new(card, 0,
#ifdef CONFIG_X86_PC9800
				pc98ii[dev] ? MPU401_HW_PC98II :
#endif
				MPU401_HW_MPU401,
				port[dev], 0,
				irq[dev], irq[dev] >= 0 ? SA_INTERRUPT : 0, NULL) < 0) {
		printk(KERN_ERR "MPU401 not detected at 0x%lx\n", port[dev]);
		snd_card_free(card);
		return -ENODEV;
	}
	strcpy(card->driver, "MPU-401 UART");
	strcpy(card->shortname, card->driver);
	sprintf(card->longname, "%s at 0x%lx, ", card->shortname, port[dev]);
	if (irq[dev] >= 0) {
		sprintf(card->longname + strlen(card->longname), "IRQ %d", irq[dev]);
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
		if (!enable[dev])
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

/* format is: snd-mpu401=enable,index,id,port,irq */

static int __init alsa_card_mpu401_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
#ifdef CONFIG_X86_PC9800
	       get_option(&str,&pc98ii[nr_dev]) == 2 &&
#endif
	       get_option(&str,(int *)&port[nr_dev]) == 2 &&
	       get_option(&str,&irq[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-mpu401=", alsa_card_mpu401_setup);

#endif /* ifndef MODULE */
