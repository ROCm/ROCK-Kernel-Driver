/*
 *  Driver for SoundBlaster 1.0/2.0/Pro soundcards and compatible
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
#include <sound/core.h>
#include <sound/sb.h>
#include <sound/opl3.h>
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_GET_ID
#include <sound/initval.h>

#define chip_t sb_t

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Sound Blaster 1.0/2.0/Pro");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Creative Labs,SB 1.0/SB 2.0/SB Pro}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260 */
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int snd_dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3 */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for Sound Blaster soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for Sound Blaster soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable Sound Blaster soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for SB8 driver.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for SB8 driver.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_IRQ_DESC);
MODULE_PARM(snd_dma8, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma8, "8-bit DMA # for SB8 driver.");
MODULE_PARM_SYNTAX(snd_dma8, SNDRV_DMA8_DESC);

struct snd_sb8 {
	struct resource *fm_res;	/* used to block FM i/o region for legacy cards */
};

static snd_card_t *snd_sb8_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

static void snd_sb8_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	sb_t *chip = snd_magic_cast(sb_t, dev_id, return);

	if (chip->open & SB_OPEN_PCM) {
		snd_sb8dsp_interrupt(chip);
	} else {
		snd_sb8dsp_midi_interrupt(chip);
	}
}

static void snd_sb8_free(snd_card_t *card)
{
	struct snd_sb8 *acard = (struct snd_sb8 *)card->private_data;

	if (acard == NULL)
		return;
	if (acard->fm_res) {
		release_resource(acard->fm_res);
		kfree_nocheck(acard->fm_res);
	}
}

static int __init snd_sb8_probe(int dev)
{
	sb_t *chip;
	snd_card_t *card;
	struct snd_sb8 *acard;
	opl3_t *opl3;
	int err;

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE,
			    sizeof(struct snd_sb8));
	if (card == NULL)
		return -ENOMEM;
	acard = (struct snd_sb8 *)card->private_data;
	card->private_free = snd_sb8_free;

	/* block the 0x388 port to avoid PnP conflicts */
	acard->fm_res = request_region(0x388, 4, "SoundBlaster FM");

	if ((err = snd_sbdsp_create(card, snd_port[dev], snd_irq[dev],
				    snd_sb8_interrupt,
				    snd_dma8[dev],
				    -1,
				    SB_HW_AUTO,
				    &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (chip->hardware >= SB_HW_16) {
		snd_card_free(card);
		if (chip->hardware == SB_HW_ALS100)
			snd_printdd("ALS100 chip detected at 0x%lx, try snd-card-als100 module\n",
				    snd_port[dev]);
		else
			snd_printdd("SB 16 chip detected at 0x%lx, try snd-card-sb16 module\n",
				    snd_port[dev]);
		return -ENODEV;
	}

	if ((err = snd_sb8dsp_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_sbmixer_new(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (chip->hardware == SB_HW_10 || chip->hardware == SB_HW_20) {
		if ((err = snd_opl3_create(card, chip->port + 8, 0,
					   OPL3_HW_AUTO, 1,
					   &opl3)) < 0) {
			snd_printk("no OPL device at 0x%lx\n", chip->port + 8);
		}
	} else {
		if ((err = snd_opl3_create(card, chip->port, chip->port + 2,
					   OPL3_HW_AUTO, 1,
					   &opl3)) < 0) {
			snd_printk("no OPL device at 0x%lx-0x%lx\n",
				   chip->port, chip->port + 2);
		}
	}
	if (err >= 0) {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}

	if ((err = snd_sb8dsp_midi(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, chip->hardware == SB_HW_PRO ? "SB Pro" : "SB8");
	strcpy(card->shortname, chip->name);
	sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d",
		chip->name,
		chip->port,
		snd_irq[dev], snd_dma8[dev]);
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_sb8_cards[dev] = card;
	return 0;
}

static int __init snd_card_sb8_legacy_auto_probe(unsigned long port)
{
        static int dev = 0;
        int res;

        for ( ; dev < SNDRV_CARDS; dev++) {
                if (!snd_enable[dev] || snd_port[dev] != SNDRV_AUTO_PORT)
                        continue;
                snd_port[dev] = port;
                res = snd_sb8_probe(dev);
                if (res < 0)
                        snd_port[dev] = SNDRV_AUTO_PORT;
                return res;
        }
        return -ENODEV;
}

static int __init alsa_card_sb8_init(void)
{
	static unsigned long possible_ports[] = {0x220, 0x240, 0x260, -1};
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && snd_enable[dev]; dev++) {
		if (snd_port[dev] == SNDRV_AUTO_PORT)
			continue;
		if (snd_sb8_probe(dev) >= 0)
			cards++;
	}
	cards += snd_legacy_auto_probe(possible_ports, snd_card_sb8_legacy_auto_probe);
	if (!cards) {
#ifdef MODULE
		snd_printk("Sound Blaster soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_sb8_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_sb8_cards[idx]);
}

module_init(alsa_card_sb8_init)
module_exit(alsa_card_sb8_exit)

#ifndef MODULE

/* format is: snd-sb8=snd_enable,snd_index,snd_id,
		      snd_port,snd_irq,snd_dma8 */

static int __init alsa_card_sb8_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma8[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-sb8=", alsa_card_sb8_setup);

#endif /* ifndef MODULE */
