/*
 *  Driver for Aztech Sound Galaxy cards
 *  Copyright (c) by Christopher Butler <chrisb@sandy.force9.co.uk.
 *
 *  I don't have documentation for this card, I based this driver on the
 *  driver for OSS/Free included in the kernel source (drivers/sound/sgalaxy.c)
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
#include <linux/irq.h>
#include <sound/core.h>
#include <sound/sb.h>
#include <sound/ad1848.h>
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#define SNDRV_GET_ID
#include <sound/initval.h>

MODULE_AUTHOR("Christopher Butler <chrisb@sandy.force9.co.uk>");
MODULE_DESCRIPTION("Aztech Sound Galaxy");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Aztech Systems,Sound Galaxy}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long snd_sbport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240 */
static long snd_wssport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x530,0xe80,0xf40,0x604 */
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 7,9,10,11 */
static int snd_dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for Sound Galaxy soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for Sound Galaxy soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_sbport, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_sbport, "Port # for Sound Galaxy SB driver.");
MODULE_PARM_SYNTAX(snd_sbport, SNDRV_ENABLED ",allows:{{0x220},{0x240}},dialog:list");
MODULE_PARM(snd_wssport, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_wssport, "Port # for Sound Galaxy WSS driver.");
MODULE_PARM_SYNTAX(snd_wssport, SNDRV_ENABLED ",allows:{{0x530},{0xe80},{0xf40},{0x604}},dialog:list");
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for Sound Galaxy driver.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_ENABLED ",allows:{{7},{9},{10},{11}},dialog:list");
MODULE_PARM(snd_dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma1, "DMA1 # for Sound Galaxy driver.");
MODULE_PARM_SYNTAX(snd_dma1, SNDRV_DMA8_DESC);

#define SGALAXY_AUXC_LEFT 18
#define SGALAXY_AUXC_RIGHT 19

static snd_card_t *snd_sgalaxy_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

/*

 */

#define AD1848P1( port, x ) ( port + c_d_c_AD1848##x )

/* from lowlevel/sb/sb.c - to avoid having to allocate a sb_t for the */
/* short time we actually need it.. */

static int snd_sgalaxy_sbdsp_reset(unsigned long port)
{
	int i;

	outb(1, SBP1(port, RESET));
	udelay(10);
	outb(0, SBP1(port, RESET));
	udelay(30);
	for (i = 0; i < 1000 && !(inb(SBP1(port, DATA_AVAIL)) & 0x80); i++);
	if (inb(SBP1(port, READ)) != 0xaa) {
		snd_printd("sb_reset: failed at 0x%lx!!!\n", port);
		return -ENODEV;
	}
	return 0;
}

static int __init snd_sgalaxy_sbdsp_command(unsigned long port, unsigned char val)
{
	int i;
       	
	for (i = 10000; i; i--)
		if ((inb(SBP1(port, STATUS)) & 0x80) == 0) {
			outb(val, SBP1(port, COMMAND));
			return 1;
		}

	return 0;
}

static void snd_sgalaxy_dummy_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
}

static int __init snd_sgalaxy_setup_wss(unsigned long port, int irq, int dma)
{
	static int interrupt_bits[] = {-1, -1, -1, -1, -1, -1, -1, 0x08, -1, 
				       0x10, 0x18, 0x20, -1, -1, -1, -1};
	static int dma_bits[] = {1, 2, 0, 3};
	int tmp, tmp1;

	if ((tmp = inb(port + 3)) == 0xff)
	{
		snd_printdd("I/O address dead (0x%lx)\n", port);
		return 0;
	}
#if 0
	snd_printdd("WSS signature = 0x%x\n", tmp);
#endif

        if ((tmp & 0x3f) != 0x04 &&
            (tmp & 0x3f) != 0x0f &&
            (tmp & 0x3f) != 0x00) {
		snd_printdd("No WSS signature detected on port 0x%lx\n",
			    port + 3);
		return 0;
	}

#if 0
	snd_printdd("sgalaxy - setting up IRQ/DMA for WSS\n");
#endif

        /* initialize IRQ for WSS codec */
        tmp = interrupt_bits[irq % 16];
        if (tmp < 0)
                return -EINVAL;

	if (request_irq(irq, snd_sgalaxy_dummy_interrupt, SA_INTERRUPT, "sgalaxy", NULL))
		return -EIO;

        outb(tmp | 0x40, port);
        tmp1 = dma_bits[dma % 4];
        outb(tmp | tmp1, port);

	free_irq(irq, NULL);

	return 0;
}

static int __init snd_sgalaxy_detect(int dev, int irq, int dma)
{
#if 0
	snd_printdd("sgalaxy - switching to WSS mode\n");
#endif

	/* switch to WSS mode */
	snd_sgalaxy_sbdsp_reset(snd_sbport[dev]);

	snd_sgalaxy_sbdsp_command(snd_sbport[dev], 9);
	snd_sgalaxy_sbdsp_command(snd_sbport[dev], 0);

	udelay(400);
	return snd_sgalaxy_setup_wss(snd_wssport[dev], irq, dma);
}

#define SGALAXY_CONTROLS 2

static snd_kcontrol_new_t snd_sgalaxy_controls[2] = {
AD1848_DOUBLE("Aux Playback Switch", 0, SGALAXY_AUXC_LEFT, SGALAXY_AUXC_RIGHT, 7, 7, 1, 1),
AD1848_DOUBLE("Aux Playback Volume", 0, SGALAXY_AUXC_LEFT, SGALAXY_AUXC_RIGHT, 0, 0, 31, 0)
};

static int __init snd_sgalaxy_mixer(ad1848_t *chip)
{
	snd_card_t *card = chip->card;
	snd_ctl_elem_id_t id1, id2;
	int idx, err;

	memset(&id1, 0, sizeof(id1));
	memset(&id2, 0, sizeof(id2));
	id1.iface = id2.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	/* reassign AUX0 to LINE */
	strcpy(id1.name, "Aux Playback Switch");
	strcpy(id2.name, "Line Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "Line Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	/* reassign AUX1 to FM */
	strcpy(id1.name, "Aux Playback Switch"); id1.index = 1;
	strcpy(id2.name, "FM Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "FM Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	/* build AUX2 input */
	for (idx = 0; idx < SGALAXY_CONTROLS; idx++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_sgalaxy_controls[idx], chip))) < 0)
			return err;
	}
	return 0;
}

static int __init snd_sgalaxy_probe(int dev)
{
	static int possible_irqs[] = {7, 9, 10, 11, -1};
	static int possible_dmas[] = {1, 3, 0, -1};
	int err, irq, dma1;
	snd_card_t *card;
	ad1848_t *chip;

	if (snd_sbport[dev] == SNDRV_AUTO_PORT) {
		snd_printk("specify SB port\n");
		return -EINVAL;
	}
	if (snd_wssport[dev] == SNDRV_AUTO_PORT) {
		snd_printk("specify WSS port\n");
		return -EINVAL;
	}
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
	dma1 = snd_dma1[dev];
        if (dma1 == SNDRV_AUTO_DMA) {
		if ((dma1 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free DMA\n");
			return -EBUSY;
		}
	}

	if ((err = snd_sgalaxy_detect(dev, irq, dma1)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_ad1848_create(card, snd_wssport[dev] + 4,
				     irq, dma1,
				     AD1848_HW_DETECT, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_ad1848_pcm(chip, 0, NULL)) < 0) {
		snd_printdd("sgalaxy - error creating new ad1848 PCM device\n");
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ad1848_mixer(chip)) < 0) {
		snd_printdd("sgalaxy - error creating new ad1848 mixer\n");
		snd_card_free(card);
		return err;
	}
	if (snd_sgalaxy_mixer(chip) < 0) {
		snd_printdd("sgalaxy - the mixer rewrite failed\n");
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, "Sound Galaxy");
	strcpy(card->shortname, "Sound Galaxy");
	sprintf(card->longname, "Sound Galaxy at 0x%lx, irq %d, dma %d",
		snd_wssport[dev], irq, dma1);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_sgalaxy_cards[dev] = card;
	return 0;
}

static int __init alsa_card_sgalaxy_init(void)
{
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && snd_enable[dev]; dev++) {
		if (snd_sgalaxy_probe(dev) >= 0)
			cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "Sound Galaxy soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}

	return 0;
}

static void __exit alsa_card_sgalaxy_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_sgalaxy_cards[idx]);
}

module_init(alsa_card_sgalaxy_init)
module_exit(alsa_card_sgalaxy_exit)

#ifndef MODULE

/* format is: snd-sgalaxy=snd_enable,snd_index,snd_id,
			  snd_sbport,snd_wssport,
			  snd_irq,snd_dma1 */

static int __init alsa_card_sgalaxy_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_sbport[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_wssport[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_dma1[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-sgalaxy=", alsa_card_sgalaxy_setup);

#endif /* ifndef MODULE */
