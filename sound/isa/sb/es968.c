
/*
    card-es968.c - driver for ESS AudioDrive ES968 based soundcards.
    Copyright (C) 1999 by Massimo Piccioni <dafastidio@libero.it>

    Thanks to Pierfrancesco 'qM2' Passerini.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
*/

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/time.h>
#ifndef LINUX_ISAPNP_H
#include <linux/isapnp.h>
#define isapnp_card pci_bus
#define isapnp_dev pci_dev
#endif
#include <sound/core.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include <sound/sb.h>

#define chip_t sb_t

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("ESS AudioDrive ES968");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{ESS,AudioDrive ES968}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* Pnp setup */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for es968 based soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for es968 based soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable es968 based soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for es968 driver.");
MODULE_PARM_SYNTAX(port, SNDRV_PORT12_DESC);
MODULE_PARM(irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(irq, "IRQ # for es968 driver.");
MODULE_PARM_SYNTAX(irq, SNDRV_IRQ_DESC);
MODULE_PARM(dma8, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma8, "8-bit DMA # for es968 driver.");
MODULE_PARM_SYNTAX(dma8, SNDRV_DMA8_DESC);

struct snd_card_es968 {
#ifdef __ISAPNP__
	struct isapnp_dev *dev;
#endif	/* __ISAPNP__ */
};

static snd_card_t *snd_es968_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef __ISAPNP__
static struct isapnp_card *snd_es968_isapnp_cards[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;
static const struct isapnp_card_id *snd_es968_isapnp_id[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;

static struct isapnp_card_id snd_es968_pnpids[] __devinitdata = {
        {
                ISAPNP_CARD_ID('E','S','S',0x0968),
                .devs = { ISAPNP_DEVICE_ID('E','S','S',0x0968), }
        },
        { ISAPNP_CARD_END, }
};

ISAPNP_CARD_TABLE(snd_es968_pnpids);

#endif	/* __ISAPNP__ */

#define	DRIVER_NAME	"snd-card-es968"


static void snd_card_es968_interrupt(int irq, void *dev_id,
				     struct pt_regs *regs)
{
	sb_t *chip = snd_magic_cast(sb_t, dev_id, return);

	if (chip->open & SB_OPEN_PCM) {
		snd_sb8dsp_interrupt(chip);
	} else {
		snd_sb8dsp_midi_interrupt(chip);
	}
}

#ifdef __ISAPNP__
static int __init snd_card_es968_isapnp(int dev, struct snd_card_es968 *acard)
{
	const struct isapnp_card_id *id = snd_es968_isapnp_id[dev];
	struct isapnp_card *card = snd_es968_isapnp_cards[dev];
	struct isapnp_dev *pdev;

	acard->dev = isapnp_find_dev(card, id->devs[0].vendor, id->devs[0].function, NULL);
	if (acard->dev->active) {
		acard->dev = NULL;
		return -EBUSY;
	}

	pdev = acard->dev;
	if (pdev->prepare(pdev)<0)
		return -EAGAIN;

	if (port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], port[dev], 16);
	if (dma8[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[0], dma8[dev],
			1);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], irq[dev], 1);

	if (pdev->activate(pdev)<0) {
		snd_printk("AUDIO isapnp configure failure\n");
		return -EBUSY;
	}

	port[dev] = pdev->resource[0].start;
	dma8[dev] = pdev->dma_resource[0].start;
	irq[dev] = pdev->irq_resource[0].start;

	return 0;
}

static void snd_card_es968_deactivate(struct snd_card_es968 *acard)
{
	if (acard->dev) {
		acard->dev->deactivate(acard->dev);
		acard->dev = NULL;
	}
}
#endif	/* __ISAPNP__ */

static void snd_card_es968_free(snd_card_t *card)
{
	struct snd_card_es968 *acard = (struct snd_card_es968 *)card->private_data;

	if (acard) {
#ifdef __ISAPNP__
		snd_card_es968_deactivate(acard);
#endif	/* __ISAPNP__ */
	}
}

static int __init snd_card_es968_probe(int dev)
{
	int error;
	sb_t *chip;
	snd_card_t *card;
	struct snd_card_es968 *acard;

	if ((card = snd_card_new(index[dev], id[dev], THIS_MODULE,
				 sizeof(struct snd_card_es968))) == NULL)
		return -ENOMEM;
	acard = (struct snd_card_es968 *)card->private_data;
	card->private_free = snd_card_es968_free;

#ifdef __ISAPNP__
	if ((error = snd_card_es968_isapnp(dev, acard))) {
		snd_card_free(card);
		return error;
	}
#else
	snd_printk("you have to enable PnP support ...\n");
	snd_card_free(card);
	return -ENOSYS;
#endif	/* __ISAPNP__ */

	if ((error = snd_sbdsp_create(card, port[dev],
				      irq[dev],
				      snd_card_es968_interrupt,
				      dma8[dev],
				      -1,
				      SB_HW_AUTO, &chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sb8dsp_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sbmixer_new(chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sb8dsp_midi(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}

	strcpy(card->driver, "ES968");
	strcpy(card->shortname, "ESS ES968");
	sprintf(card->longname, "%s soundcard, %s at 0x%lx, irq %d, dma %d",
		card->shortname, chip->name, chip->port, irq[dev], dma8[dev]);

	if ((error = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return error;
	}
	snd_es968_cards[dev] = card;
	return 0;
}

#ifdef __ISAPNP__
static int __init snd_es968_isapnp_detect(struct isapnp_card *card,
                                          const struct isapnp_card_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		snd_es968_isapnp_cards[dev] = card;
		snd_es968_isapnp_id[dev] = id;
		res = snd_card_es968_probe(dev);
		if (res < 0)
			return res;
		dev++;
		return 0;
        }
        return -ENODEV;
}
#endif /* __ISAPNP__ */

static int __init alsa_card_es968_init(void)
{
	int cards = 0;

#ifdef __ISAPNP__
	cards += isapnp_probe_cards(snd_es968_pnpids, snd_es968_isapnp_detect);
#else
	snd_printk("you have to enable ISA PnP support.\n");
#endif
#ifdef MODULE
	if (!cards)
		snd_printk("no ES968 based soundcards found\n");
#endif
	return cards ? 0 : -ENODEV;
}

static void __exit alsa_card_es968_exit(void)
{
	int dev;

	for (dev = 0; dev < SNDRV_CARDS; dev++)
		snd_card_free(snd_es968_cards[dev]);
}

module_init(alsa_card_es968_init)
module_exit(alsa_card_es968_exit)

#ifndef MODULE

/* format is: snd-es968=enable,index,id,
			port,irq,dma1 */

static int __init alsa_card_es968_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&port[nr_dev]) == 2 &&
	       get_option(&str,&irq[nr_dev]) == 2 &&
	       get_option(&str,&dma8[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-es968=", alsa_card_es968_setup);

#endif /* ifndef MODULE */
