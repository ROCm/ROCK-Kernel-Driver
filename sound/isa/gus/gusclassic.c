/*
 *  Driver for Gravis UltraSound Classic soundcard
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
#include <linux/delay.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/gus.h>
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#define SNDRV_GET_ID
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Gravis UltraSound Classic");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Gravis,UltraSound Classic}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x230,0x240,0x250,0x260 */
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 3,5,9,11,12,15 */
static int snd_dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
static int snd_dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
static int snd_joystick_dac[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 29};
				/* 0 to 31, (0.59V-4.52V or 0.389V-2.98V) */
static int snd_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 24};
static int snd_pcm_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for GUS Classic soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for GUS Classic soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable GUS Classic soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for GUS Classic driver.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_ENABLED ",allows:{{0x220,0x260,0x10}},dialog:list");
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for GUS Classic driver.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_ENABLED ",allows:{{3},{5},{9},{11},{12},{15}},dialog:list");
MODULE_PARM(snd_dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma1, "DMA1 # for GUS Classic driver.");
MODULE_PARM_SYNTAX(snd_dma1, SNDRV_ENABLED ",allows:{{1},{3},{5},{6},{7}},dialog:list");
MODULE_PARM(snd_dma2, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma2, "DMA2 # for GUS Classic driver.");
MODULE_PARM_SYNTAX(snd_dma2, SNDRV_ENABLED ",allows:{{1},{3},{5},{6},{7}},dialog:list");
MODULE_PARM(snd_joystick_dac, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_joystick_dac, "Joystick DAC level 0.59V-4.52V or 0.389V-2.98V for GUS Classic driver.");
MODULE_PARM_SYNTAX(snd_joystick_dac, SNDRV_ENABLED ",allows:{{0,31}}");
MODULE_PARM(snd_channels, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_channels, "GF1 channels for GUS Classic driver.");
MODULE_PARM_SYNTAX(snd_channels,  SNDRV_ENABLED ",allows:{{14,32}}");
MODULE_PARM(snd_pcm_channels, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_pcm_channels, "Reserved PCM channels for GUS Classic driver.");
MODULE_PARM_SYNTAX(snd_pcm_channels, SNDRV_ENABLED ",allows:{{2,16}}");

static snd_card_t *snd_gusclassic_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int __init snd_gusclassic_detect(snd_gus_card_t * gus)
{
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 0);	/* reset GF1 */
#ifdef CONFIG_SND_DEBUG_DETECT
	{
		unsigned char d;

		if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 0) {
			snd_printk("[0x%lx] check 1 failed - 0x%x\n", gus->gf1.port, d);
			return -ENODEV;
		}
	}
#else
	if ((snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET) & 0x07) != 0)
		return -ENODEV;
#endif
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 1);	/* release reset */
	udelay(160);
#ifdef CONFIG_SND_DEBUG_DETECT
	{
		unsigned char d;

		if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 1) {
			snd_printk("[0x%lx] check 2 failed - 0x%x\n", gus->gf1.port, d);
			return -ENODEV;
		}
	}
#else
	if ((snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET) & 0x07) != 1)
		return -ENODEV;
#endif

	return 0;
}

static void __init snd_gusclassic_init(int dev, snd_gus_card_t * gus)
{
	gus->equal_irq = 0;
	gus->codec_flag = 0;
	gus->max_flag = 0;
	gus->joystick_dac = snd_joystick_dac[dev];
}

static int __init snd_gusclassic_probe(int dev)
{
	static int possible_irqs[] = {5, 11, 12, 9, 7, 15, 3, 4, -1};
	static int possible_dmas[] = {5, 6, 7, 1, 3, -1};
	int irq, dma1, dma2;
	snd_card_t *card;
	struct snd_gusclassic *guscard;
	snd_gus_card_t *gus = NULL;
	int err;

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	guscard = (struct snd_gusclassic *)card->private_data;
	if (snd_pcm_channels[dev] < 2)
		snd_pcm_channels[dev] = 2;

	irq = snd_irq[dev];
	if (irq == SNDRV_AUTO_IRQ) {
		if ((irq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	dma1 = snd_dma1[dev];
	if (dma1 == SNDRV_AUTO_DMA) {
		if ((dma1 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free DMA1\n");
			return -EBUSY;
		}
	}
	dma2 = snd_dma2[dev];
	if (dma2 == SNDRV_AUTO_DMA) {
		if ((dma2 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free DMA2\n");
			return -EBUSY;
		}
	}


	if ((err = snd_gus_create(card,
				  snd_port[dev],
				  irq, dma1, dma2,
			          0, snd_channels[dev], snd_pcm_channels[dev],
			          0, &gus)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_gusclassic_detect(gus)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_gusclassic_init(dev, gus);
	if ((err = snd_gus_initialize(gus)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (gus->max_flag || gus->ess_flag) {
		snd_card_free(card);
		snd_printdd("GUS Classic or ACE soundcard was not detected at 0x%lx\n", gus->gf1.port);
		return -ENODEV;
	}
	if ((err = snd_gf1_new_mixer(gus)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_gf1_pcm_new(gus, 0, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (!gus->ace_flag) {
		if ((err = snd_gf1_rawmidi_new(gus, 0, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	sprintf(card->longname + strlen(card->longname), " at 0x%lx, irq %d, dma %d", gus->gf1.port, irq, dma1);
	if (dma2 >= 0)
		sprintf(card->longname + strlen(card->longname), "&%d", dma2);
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_gusclassic_cards[dev] = card;
	return 0;
}

static int __init snd_gusclassic_legacy_auto_probe(unsigned long port)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev] || snd_port[dev] != SNDRV_AUTO_PORT)
			continue;
		snd_port[dev] = port;
		res = snd_gusclassic_probe(dev);
		if (res < 0)
			snd_port[dev] = SNDRV_AUTO_PORT;
		return res;
	}
	return -ENODEV;
}

static int __init alsa_card_gusclassic_init(void)
{
	static unsigned long possible_ports[] = {0x220, 0x230, 0x240, 0x250, 0x260, -1};
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && snd_enable[dev]; dev++) {
		if (snd_port[dev] == SNDRV_AUTO_PORT)
			continue;
		if (snd_gusclassic_probe(dev) >= 0)
			cards++;
	}
	cards += snd_legacy_auto_probe(possible_ports, snd_gusclassic_legacy_auto_probe);
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "GUS Classic soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_gusclassic_exit(void)
{
	int idx;
	
	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_gusclassic_cards[idx]);
}

module_init(alsa_card_gusclassic_init)
module_exit(alsa_card_gusclassic_exit)

#ifndef MODULE

/* format is: snd-gusclassic=snd_enable,snd_index,snd_id,
			     snd_port,snd_irq,
			     snd_dma1,snd_dma2,
			     snd_joystick_dac,
			     snd_channels,snd_pcm_channels */

static int __init alsa_card_gusclassic_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma1[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma2[nr_dev]) == 2 &&
	       get_option(&str,&snd_joystick_dac[nr_dev]) == 2 &&
	       get_option(&str,&snd_channels[nr_dev]) == 2 &&
	       get_option(&str,&snd_pcm_channels[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-gusclassic=", alsa_card_gusclassic_setup);

#endif /* ifndef MODULE */
