/*
 *  Driver for Gravis UltraSound Extreme soundcards
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
#include <sound/core.h>
#include <sound/gus.h>
#include <sound/es1688.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#define SNDRV_GET_ID
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Gravis UltraSound Extreme");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Gravis,UltraSound Extreme}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260 */
static long snd_gf1_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS) - 1] = -1}; /* 0x210,0x220,0x230,0x240,0x250,0x260,0x270 */
static long snd_mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS) - 1] = -1}; /* 0x300,0x310,0x320 */
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int snd_mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int snd_gf1_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 2,3,5,9,11,12,15 */
static int snd_dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */
static int snd_dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;
static int snd_joystick_dac[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 29};
				/* 0 to 31, (0.59V-4.52V or 0.389V-2.98V) */
static int snd_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 24};
static int snd_pcm_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for GUS Extreme soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for GUS Extreme soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable GUS Extreme soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for GUS Extreme driver.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_ENABLED ",allows:{{0x220,0x260,0x20}},dialog:list");
MODULE_PARM(snd_gf1_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_gf1_port, "GF1 port # for GUS Extreme driver (optional).");
MODULE_PARM_SYNTAX(snd_gf1_port, SNDRV_ENABLED ",allows:{{0x210,0x270,0x10}},dialog:list");
MODULE_PARM(snd_mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_mpu_port, "MPU-401 port # for GUS Extreme driver.");
MODULE_PARM_SYNTAX(snd_mpu_port, SNDRV_ENABLED ",allows:{{0x300,0x320,0x10}},dialog:list");
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for GUS Extreme driver.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_ENABLED ",allows:{{5},{7},{9},{10}},dialog:list");
MODULE_PARM(snd_mpu_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_mpu_irq, "MPU-401 IRQ # for GUS Extreme driver.");
MODULE_PARM_SYNTAX(snd_mpu_irq, SNDRV_ENABLED ",allows:{{5},{7},{9},{10}},dialog:list");
MODULE_PARM(snd_gf1_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_gf1_irq, "GF1 IRQ # for GUS Extreme driver.");
MODULE_PARM_SYNTAX(snd_gf1_irq, SNDRV_ENABLED ",allows:{{2},{3},{5},{9},{11},{12},{15}},dialog:list");
MODULE_PARM(snd_dma8, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma8, "8-bit DMA # for GUS Extreme driver.");
MODULE_PARM_SYNTAX(snd_dma8, SNDRV_DMA8_DESC);
MODULE_PARM(snd_dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma1, "GF1 DMA # for GUS Extreme driver.");
MODULE_PARM_SYNTAX(snd_dma1, SNDRV_DMA_DESC);
MODULE_PARM(snd_joystick_dac, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_joystick_dac, "Joystick DAC level 0.59V-4.52V or 0.389V-2.98V for GUS Extreme driver.");
MODULE_PARM_SYNTAX(snd_joystick_dac, SNDRV_ENABLED ",allows:{{0,31}}");
MODULE_PARM(snd_channels, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_channels, "GF1 channels for GUS Extreme driver.");
MODULE_PARM_SYNTAX(snd_channels, SNDRV_ENABLED ",allows:{{14,32}}");
MODULE_PARM(snd_pcm_channels, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_pcm_channels, "Reserved PCM channels for GUS Extreme driver.");
MODULE_PARM_SYNTAX(snd_pcm_channels, SNDRV_ENABLED ",allows:{{2,16}}");

static snd_card_t *snd_gusextreme_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int __init snd_gusextreme_detect(int dev,
					snd_card_t * card,
					snd_gus_card_t * gus,
					es1688_t *es1688)
{
	unsigned long flags;

	/*
	 * This is main stuff - enable access to GF1 chip...
	 * I'm not sure, if this will work for card which have
	 * ES1688 chip in another place than 0x220.
         *
         * I used reverse-engineering in DOSEMU. [--jk]
	 *
	 * ULTRINIT.EXE:
	 * 0x230 = 0,2,3
	 * 0x240 = 2,0,1
	 * 0x250 = 2,0,3
	 * 0x260 = 2,2,1
	 */

	spin_lock_irqsave(&es1688->mixer_lock, flags);
	snd_es1688_mixer_write(es1688, 0x40, 0x0b);	/* don't change!!! */
	spin_unlock_irqrestore(&es1688->mixer_lock, flags);
	spin_lock_irqsave(&es1688->reg_lock, flags);
	outb(snd_gf1_port[dev] & 0x040 ? 2 : 0, ES1688P(es1688, INIT1));
	outb(0, 0x201);
	outb(snd_gf1_port[dev] & 0x020 ? 2 : 0, ES1688P(es1688, INIT1));
	outb(0, 0x201);
	outb(snd_gf1_port[dev] & 0x010 ? 3 : 1, ES1688P(es1688, INIT1));
	spin_unlock_irqrestore(&es1688->reg_lock, flags);

	udelay(100);

	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 0);	/* reset GF1 */
#ifdef CONFIG_SND_DEBUG_DETECT
	{
		unsigned char d;

		if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 0) {
			snd_printk("[0x%lx] check 1 failed - 0x%x\n", gus->gf1.port, d);
			return -EIO;
		}
	}
#else
	if ((snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET) & 0x07) != 0)
		return -EIO;
#endif
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 1);	/* release reset */
	udelay(160);
#ifdef CONFIG_SND_DEBUG_DETECT
	{
		unsigned char d;

		if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 1) {
			snd_printk("[0x%lx] check 2 failed - 0x%x\n", gus->gf1.port, d);
			return -EIO;
		}
	}
#else
	if ((snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET) & 0x07) != 1)
		return -EIO;
#endif

	return 0;
}

static void __init snd_gusextreme_init(int dev, snd_gus_card_t * gus)
{
	gus->joystick_dac = snd_joystick_dac[dev];
}

static int __init snd_gusextreme_mixer(es1688_t *chip)
{
	snd_card_t *card = chip->card;
	snd_ctl_elem_id_t id1, id2;
	int err;

	memset(&id1, 0, sizeof(id1));
	memset(&id2, 0, sizeof(id2));
	id1.iface = id2.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	/* reassign AUX to SYNTHESIZER */
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "Synth Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	/* reassign Master Playback Switch to Synth Playback Switch */
	strcpy(id1.name, "Master Playback Switch");
	strcpy(id2.name, "Synth Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	return 0;
}

static int __init snd_gusextreme_probe(int dev)
{
	static int possible_ess_irqs[] = {5, 9, 10, 7, -1};
	static int possible_ess_dmas[] = {1, 3, 0, -1};
	static int possible_gf1_irqs[] = {5, 11, 12, 9, 7, 15, 3, -1};
	static int possible_gf1_dmas[] = {5, 6, 7, 1, 3, -1};
	int gf1_irq, gf1_dma, ess_irq, mpu_irq, ess_dma;
	snd_card_t *card;
	struct snd_gusextreme *acard;
	snd_gus_card_t *gus;
	es1688_t *es1688;
	opl3_t *opl3;
	int err;

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	acard = (struct snd_gusextreme *)card->private_data;

	gf1_irq = snd_gf1_irq[dev];
	if (gf1_irq == SNDRV_AUTO_IRQ) {
		if ((gf1_irq = snd_legacy_find_free_irq(possible_gf1_irqs)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free IRQ for GF1\n");
			return -EBUSY;
		}
	}
	ess_irq = snd_irq[dev];
	if (ess_irq == SNDRV_AUTO_IRQ) {
		if ((ess_irq = snd_legacy_find_free_irq(possible_ess_irqs)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free IRQ for ES1688\n");
			return -EBUSY;
		}
	}
	if (snd_mpu_port[dev] == SNDRV_AUTO_PORT)
		snd_mpu_port[dev] = 0;
	mpu_irq = snd_mpu_irq[dev];
	if (mpu_irq > 15)
		mpu_irq = -1;
	gf1_dma = snd_dma1[dev];
	if (gf1_dma == SNDRV_AUTO_DMA) {
		if ((gf1_dma = snd_legacy_find_free_dma(possible_gf1_dmas)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free DMA for GF1\n");
			return -EBUSY;
		}
	}
	ess_dma = snd_dma8[dev];
	if (ess_dma == SNDRV_AUTO_DMA) {
		if ((ess_dma = snd_legacy_find_free_dma(possible_ess_dmas)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free DMA for ES1688\n");
			return -EBUSY;
		}
	}

	if ((err = snd_es1688_create(card, snd_port[dev], snd_mpu_port[dev],
				     ess_irq, mpu_irq, ess_dma,
				     ES1688_HW_1688, &es1688)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (snd_gf1_port[dev] < 0)
		snd_gf1_port[dev] = snd_port[dev] + 0x20;
	if ((err = snd_gus_create(card,
				  snd_gf1_port[dev],
				  gf1_irq,
				  gf1_dma,
				  -1,
				  0, snd_channels[dev],
				  snd_pcm_channels[dev], 0,
				  &gus)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_gusextreme_detect(dev, card, gus, es1688)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_gusextreme_init(dev, gus);
	if ((err = snd_gus_initialize(gus)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (!gus->ess_flag) {
		snd_card_free(card);
		snd_printdd("GUS Extreme soundcard was not detected at 0x%lx\n", gus->gf1.port);
		return -ENODEV;
	}
	if ((err = snd_es1688_pcm(es1688, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_es1688_mixer(es1688)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_component_add(card, "ES1688");
	if (snd_pcm_channels[dev] > 0) {
		if ((err = snd_gf1_pcm_new(gus, 1, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	if ((err = snd_gf1_new_mixer(gus)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_gusextreme_mixer(es1688)) < 0) {
		snd_card_free(card);
		return err;
	}

	if (snd_opl3_create(card, es1688->port, es1688->port + 2,
			    OPL3_HW_OPL3, 0, &opl3) < 0) {
		snd_printk("opl3 not detected at 0x%lx\n", es1688->port);
	} else {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 2, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}

	if (es1688->mpu_port >= 0x300) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_ES1688,
					       es1688->mpu_port, 0,
					       mpu_irq,
					       SA_INTERRUPT,
					       NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}

	sprintf(card->longname, "Gravis UltraSound Extreme at 0x%lx, irq %i&%i, dma %i&%i",
		es1688->port, gf1_irq, ess_irq, gf1_dma, ess_dma);
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_gusextreme_cards[dev] = card;
	return 0;
}

static int __init snd_gusextreme_legacy_auto_probe(unsigned long port)
{
        static int dev = 0;
        int res;

        for ( ; dev < SNDRV_CARDS; dev++) {
                if (!snd_enable[dev] || snd_port[dev] != SNDRV_AUTO_PORT)
                        continue;
                snd_port[dev] = port;
                res = snd_gusextreme_probe(dev);
                if (res < 0)
                        snd_port[dev] = SNDRV_AUTO_PORT;
                return res;
        }
        return -ENODEV;
}

static int __init alsa_card_gusextreme_init(void)
{
	static unsigned long possible_ports[] = {0x220, 0x240, 0x260, -1};
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && snd_enable[dev] > 0; dev++) {
		if (snd_port[dev] == SNDRV_AUTO_PORT)
			continue;
		if (snd_gusextreme_probe(dev) >= 0)
			cards++;
	}
	cards += snd_legacy_auto_probe(possible_ports, snd_gusextreme_legacy_auto_probe);
	if (!cards) {
#ifdef MODULE
		snd_printk("GUS Extreme soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_gusextreme_exit(void)
{
	int idx;
	snd_card_t *card;
	struct snd_gusextreme *acard;

	for (idx = 0; idx < SNDRV_CARDS; idx++) {
		card = snd_gusextreme_cards[idx];
		if (card == NULL)
			continue;
		acard = (struct snd_gusextreme *)card->private_data;
		snd_card_free(snd_gusextreme_cards[idx]);
	}
}

module_init(alsa_card_gusextreme_init)
module_exit(alsa_card_gusextreme_exit)

#ifndef MODULE

/* format is: snd-gusextreme=snd_enable,snd_index,snd_id,
			     snd_port,snd_gf1_port,snd_mpu_port,
			     snd_irq,snd_gf1_irq,snd_mpu_irq,
			     snd_dma8,snd_dma1,
			     snd_joystick_dac,
			     snd_channels,snd_pcm_channels */

static int __init alsa_card_gusextreme_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_gf1_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_mpu_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_gf1_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_mpu_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma8[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma1[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-gusextreme=", alsa_card_gusextreme_setup);

#endif /* ifndef MODULE */
