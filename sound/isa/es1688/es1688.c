/*
 *  Driver for generic ESS AudioDrive ESx688 soundcards
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
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <sound/core.h>
#include <sound/es1688.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#define SNDRV_GET_ID
#include <sound/initval.h>

#define chip_t es1688_t

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("ESS ESx688 AudioDrive");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{ESS,ES688 PnP AudioDrive,pnp:ESS0100},"
	        "{ESS,ES1688 PnP AudioDrive,pnp:ESS0102},"
	        "{ESS,ES688 AudioDrive,pnp:ESS6881},"
	        "{ESS,ES1688 AudioDrive,pnp:ESS1681}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260 */
static long snd_mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int snd_mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int snd_dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for ESx688 soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for ESx688 soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable ESx688 soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for ESx688 driver.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_mpu_port, "MPU-401 port # for ESx688 driver.");
MODULE_PARM_SYNTAX(snd_mpu_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for ESx688 driver.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_IRQ_DESC);
MODULE_PARM(snd_mpu_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_mpu_irq, "MPU-401 IRQ # for ESx688 driver.");
MODULE_PARM_SYNTAX(snd_mpu_irq, SNDRV_IRQ_DESC);
MODULE_PARM(snd_dma8, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma8, "8-bit DMA # for ESx688 driver.");
MODULE_PARM_SYNTAX(snd_dma8, SNDRV_DMA8_DESC);

static snd_card_t *snd_audiodrive_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int __init snd_audiodrive_probe(int dev)
{
	static int possible_irqs[] = {5, 9, 10, 7, -1};
	static int possible_dmas[] = {1, 3, 0, -1};
	int irq, dma, mpu_irq;
	snd_card_t *card;
	es1688_t *chip;
	opl3_t *opl3;
	snd_pcm_t *pcm;
	int err;

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	irq = snd_irq[dev];
	if (irq == SNDRV_AUTO_IRQ) {
		if ((irq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	mpu_irq = snd_mpu_irq[dev];
	dma = snd_dma8[dev];
	if (dma == SNDRV_AUTO_DMA) {
		if ((dma = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free DMA\n");
			return -EBUSY;
		}
	}

	if ((err = snd_es1688_create(card, snd_port[dev], snd_mpu_port[dev],
				     irq, mpu_irq, dma,
				     ES1688_HW_AUTO, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_es1688_pcm(chip, 0, &pcm)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_es1688_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((snd_opl3_create(card, chip->port, chip->port + 2, OPL3_HW_OPL3, 0, &opl3)) < 0) {
		snd_printk("opl3 not detected at 0x%lx\n", chip->port);
	} else {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}

	if (mpu_irq >= 0) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_ES1688,
					       chip->mpu_port, 0,
					       mpu_irq,
					       SA_INTERRUPT,
					       NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	strcpy(card->driver, "ES1688");
	strcpy(card->shortname, pcm->name);
	sprintf(card->longname, "%s at 0x%lx, irq %i, dma %i", pcm->name, chip->port, irq, dma);
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_audiodrive_cards[dev] = card;
	return 0;

}

static int __init snd_audiodrive_legacy_auto_probe(unsigned long port)
{
	static int dev = 0;
	int res;
	
	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev] || snd_port[dev] != SNDRV_AUTO_PORT)
			continue;
		snd_port[dev] = port;
		res = snd_audiodrive_probe(dev);
		if (res < 0)
			snd_port[dev] = SNDRV_AUTO_PORT;
		return res;
	}
	return -ENODEV;
}

static int __init alsa_card_es1688_init(void)
{
	static unsigned long possible_ports[] = {0x220, 0x240, 0x260, -1};
	int dev, cards = 0;

	for (dev = cards = 0; dev < SNDRV_CARDS && snd_enable[dev]; dev++) {
		if (snd_port[dev] == SNDRV_AUTO_PORT)
			continue;
		if (snd_audiodrive_probe(dev) >= 0)
			cards++;
	}
	cards += snd_legacy_auto_probe(possible_ports, snd_audiodrive_legacy_auto_probe);
	if (!cards) {
#ifdef MODULE
		snd_printk("ESS AudioDrive ES1688 soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_es1688_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_audiodrive_cards[idx]);
}

module_init(alsa_card_es1688_init)
module_exit(alsa_card_es1688_exit)

#ifndef MODULE

/* format is: snd-es1688=snd_enable,snd_index,snd_id,
			 snd_port,snd_mpu_port,
			 snd_irq,snd_mpu_irq,
			 snd_dma8 */

static int __init alsa_card_es1688_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_mpu_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_mpu_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma8[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-es1688=", alsa_card_es1688_setup);

#endif /* ifndef MODULE */
