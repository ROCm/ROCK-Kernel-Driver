/*
 *  Driver for CS4232 on NEC PC9800 series
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Osamu Tomita <tomita@cinet.co.jp>
 *                   Takashi Iwai <tiwai@suse.de>
 *                   Hideaki Okubo <okubo@msh.biglobe.ne.jp>
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
#include <sound/cs4231.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include "sound_pc9800.h"

#define chip_t cs4231_t

MODULE_AUTHOR("Osamu Tomita <tomita@cinet.co.jp>");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DESCRIPTION("NEC PC9800 CS4232");
MODULE_DEVICES("{{NEC,PC9800}}");

#define IDENT "PC98-CS4232"

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
#if 0 /* NOT USED */
static long cport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
#endif
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;/* PnP setup */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,11,12,15 */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 9,11,12,15 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */
static int pc98ii[SNDRV_CARDS];				/* PC98II */

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for " IDENT " soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for " IDENT " soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable " IDENT " soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for " IDENT " driver.");
MODULE_PARM_SYNTAX(port, SNDRV_PORT12_DESC);
#if 0 /* NOT USED */
MODULE_PARM(cport, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(cport, "Control port # for " IDENT " driver.");
MODULE_PARM_SYNTAX(cport, SNDRV_PORT12_DESC);
#endif
MODULE_PARM(mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for " IDENT " driver.");
MODULE_PARM_SYNTAX(mpu_port, SNDRV_PORT12_DESC);
MODULE_PARM(fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(fm_port, "FM port # for " IDENT " driver.");
MODULE_PARM_SYNTAX(fm_port, SNDRV_PORT12_DESC);
MODULE_PARM(irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(irq, "IRQ # for " IDENT " driver.");
MODULE_PARM_SYNTAX(irq, SNDRV_IRQ_DESC);
MODULE_PARM(mpu_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for " IDENT " driver.");
MODULE_PARM_SYNTAX(mpu_irq, SNDRV_IRQ_DESC);
MODULE_PARM(dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma1, "DMA1 # for " IDENT " driver.");
MODULE_PARM_SYNTAX(dma1, SNDRV_DMA_DESC);
MODULE_PARM(dma2, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma2, "DMA2 # for " IDENT " driver.");
MODULE_PARM_SYNTAX(dma2, SNDRV_DMA_DESC);
MODULE_PARM(pc98ii, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(pc98ii, "Roland MPU-PC98II support.");
MODULE_PARM_SYNTAX(pc98ii, SNDRV_BOOLEAN_FALSE_DESC);


static snd_card_t *snd_pc98_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

/*
 * initialize MPU401-UART
 */

static int __init pc98_mpu401_init(int irq)
{
#include "pc9801_118_magic.h"
#define outp118(reg,data) outb((reg),0x148e);outb((data),0x148f)
#define WAIT118 outb(0x00,0x5f)
	int	mpu_intr, count;
#ifdef OOKUBO_ORIGINAL
	int	err = 0;
#endif /* OOKUBO_ORIGINAL */

	switch (irq) {
	case 3:
		mpu_intr = 3;
		break;
	case 5:
		mpu_intr = 2;
		break;
	case 6:
		mpu_intr = 1;
		break;
	case 10:
		mpu_intr = 0;
		break;
	default:
		snd_printk(KERN_ERR IDENT ": Bad IRQ %d\n", irq);
		return -EINVAL;
	}

	outp118(0x21, mpu_intr);
	WAIT118;
	outb(0x00, 0x148e);
	if (inb(0x148f) & 0x08) {
		snd_printk(KERN_INFO IDENT ": No MIDI daughter board found\n");
		return 0;
	}

	outp118(0x20, 0x00);
	outp118(0x05, 0x04);
	for (count = 0; count < 35000; count ++)
		WAIT118;
	outb(0x05, 0x148e);
	for (count = 0; count < 65000; count ++)
		if (inb(0x148f) == 0x04)
			goto set_mode_118;
	snd_printk(KERN_ERR IDENT ": MIDI daughter board initialize failed at stage1\n\n");
	return -EINVAL;

 set_mode_118:
	outp118(0x05, 0x0c);
	outb(0xaa, 0x485);
	outb(0x99, 0x485);
	outb(0x2a, 0x485);
	for (count = 0; count < sizeof(Data0485_99); count ++) {
		outb(Data0485_99[count], 0x485);
		WAIT118;
	}

	outb(0x00, 0x486);
	outb(0xaa, 0x485);
	outb(0x9e, 0x485);
	outb(0x2a, 0x485);
	for (count = 0; count < sizeof(Data0485_9E); count ++)
		if (inb(0x485) != Data0485_9E[count]) {
#ifdef OOKUBO_ORIGINAL
			err = 1;
#endif /* OOKUBO_ORIGINAL */
			break;
		}
	outb(0x00, 0x486);
	for (count = 0; count < 2000; count ++)
		WAIT118;
#ifdef OOKUBO_ORIGINAL
	if (!err) {
		outb(0xaa, 0x485);
		outb(0x36, 0x485);
		outb(0x28, 0x485);
		for (count = 0; count < sizeof(Data0485_36); count ++)
			outb(Data0485_36[count], 0x485);
		outb(0x00, 0x486);
		for (count = 0; count < 1500; count ++)
			WAIT118;
		outp118(0x05, inb(0x148f) | 0x08);
		outb(0xff, 0x148c);
		outp118(0x05, inb(0x148f) & 0xf7);
		for (count = 0; count < 1500; count ++)
			WAIT118;
	}
#endif /* OOKUBO_ORIGINAL */

	outb(0xaa, 0x485);
	outb(0xa9, 0x485);
	outb(0x21, 0x485);
	for (count = 0; count < sizeof(Data0485_A9); count ++) {
		outb(Data0485_A9[count], 0x485);
		WAIT118;
	}

	outb(0x00, 0x486);
	outb(0xaa, 0x485);
	outb(0x0c, 0x485);
	outb(0x20, 0x485);
	for (count = 0; count < sizeof(Data0485_0C); count ++) {
		outb(Data0485_0C[count], 0x485);
		WAIT118;
	}

	outb(0x00, 0x486);
	outb(0xaa, 0x485);
	outb(0x66, 0x485);
	outb(0x20, 0x485);
	for (count = 0; count < sizeof(Data0485_66); count ++) {
		outb(Data0485_66[count], 0x485);
		WAIT118;
	}

	outb(0x00, 0x486);
	outb(0xaa, 0x485);
	outb(0x60, 0x485);
	outb(0x20, 0x485);
	for (count = 0; count < sizeof(Data0485_60); count ++) {
		outb(Data0485_60[count], 0x485);
		WAIT118;
	}

	outb(0x00, 0x486);
	outp118(0x05, 0x04);
	outp118(0x05, 0x00);
	for (count = 0; count < 35000; count ++)
		WAIT118;
	outb(0x05, 0x148e);
	for (count = 0; count < 65000; count ++)
		if (inb(0x148f) == 0x00)
			goto end_mode_118;
	snd_printk(KERN_ERR IDENT ": MIDI daughter board initialize failed at stage2\n");
	return -EINVAL;

 end_mode_118:
	outb(0x3f, 0x148d);
	snd_printk(KERN_INFO IDENT ": MIDI daughter board initialized\n");
	return 0;
}

static int __init pc98_cs4231_chip_init(int dev)
{
	int intr_bits, intr_bits2, dma_bits;

	switch (irq[dev]) {
	case 3:
		intr_bits = 0x08;
		intr_bits2 = 0x03;
		break;
	case 5:
		intr_bits = 0x10;
		intr_bits2 = 0x08;
		break;
	case 10:
		intr_bits = 0x18;
		intr_bits2 = 0x02;
		break;
	case 12:
		intr_bits = 0x20;
		intr_bits2 = 0x00;
		break;
	default:
		snd_printk(KERN_ERR IDENT ": Bad IRQ %d\n", irq[dev]);
		return -EINVAL;
	}

	switch (dma1[dev]) {
	case 0:
		dma_bits = 0x01;
		break;
	case 1:
		dma_bits = 0x02;
		break;
	case 3:
		dma_bits = 0x03;
		break;
	default:
		snd_printk(KERN_ERR IDENT ": Bad DMA %d\n", dma1[dev]);
		return -EINVAL;
	}

	if (dma2[dev] >= 2) {
		snd_printk(KERN_ERR IDENT ": Bad DMA %d\n", dma2[dev]);
		return -EINVAL;
	}

	outb(dma1[dev], 0x29);		/* dma1 boundary 64KB */
	if (dma1[dev] != dma2[dev] && dma2[dev] >= 0) {
		outb(0, 0x5f);		/* wait */
		outb(dma2[dev], 0x29);	/* dma2 boundary 64KB */
		intr_bits |= 0x04;
	}

	if (PC9800_SOUND_ID() == PC9800_SOUND_ID_118) {
		/* Set up CanBe control registers. */
		snd_printd(KERN_INFO "Setting up CanBe Sound System\n");
		outb(inb(PC9800_SOUND_IO_ID) | 0x03, PC9800_SOUND_IO_ID);
		outb(0x01, 0x0f4a);
		outb(intr_bits2, 0x0f4b);
	}

	outb(intr_bits | dma_bits, 0xf40);
	return 0;
}


static int __init snd_card_pc98_probe(int dev)
{
	snd_card_t *card;
	snd_pcm_t *pcm = NULL;
	cs4231_t *chip;
	opl3_t *opl3;
	int err;

	if (port[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR IDENT ": specify port\n");
		return -EINVAL;
	}
	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	if (mpu_port[dev] < 0 || mpu_irq[dev] < 0)
		mpu_port[dev] = SNDRV_AUTO_PORT;
	if (fm_port[dev] < 0)
		fm_port[dev] = SNDRV_AUTO_PORT;

	if ((err = pc98_cs4231_chip_init(dev)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_cs4231_create(card,
				     port[dev],
				     -1,
				     irq[dev],
				     dma1[dev],
				     dma2[dev],
				     CS4231_HW_DETECT,
				     0,
				     &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_pcm(chip, 0, &pcm)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_cs4231_timer(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}

	if (fm_port[dev] != SNDRV_AUTO_PORT) {
		/* ??? */
		outb(0x00, fm_port[dev] + 6);
		inb(fm_port[dev] + 7);
		/* Enable OPL-3 Function */
		outb(inb(PC9800_SOUND_IO_ID) | 0x03, PC9800_SOUND_IO_ID);
		if (snd_opl3_create(card,
				    fm_port[dev], fm_port[dev] + 2,
				    OPL3_HW_OPL3_PC98, 0, &opl3) < 0) {
			printk(KERN_ERR IDENT ": OPL3 not detected\n");
		} else {
			if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
				snd_card_free(card);
				return err;
			}
		}
	}

	if (mpu_port[dev] != SNDRV_AUTO_PORT) {
		err = pc98_mpu401_init(mpu_irq[dev]);
		if (! err) {
			err = snd_mpu401_uart_new(card, 0,
						  pc98ii[dev] ? MPU401_HW_PC98II : MPU401_HW_MPU401,
						  mpu_port[dev], 0,
						  mpu_irq[dev], SA_INTERRUPT, NULL);
			if (err < 0)
				snd_printk(KERN_INFO IDENT ": MPU401 not detected\n");
		}
	}

	strcpy(card->driver, pcm->name);
	strcpy(card->shortname, pcm->name);
	sprintf(card->longname, "%s at 0x%lx, irq %i, dma %i",
		pcm->name,
		chip->port,
		irq[dev],
		dma1[dev]);
	if (dma2[dev] >= 0)
		sprintf(card->longname + strlen(card->longname), "&%d", dma2[dev]);
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_pc98_cards[dev] = card;
	return 0;
}

static int __init alsa_card_pc98_init(void)
{
	int dev, cards = 0;

	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		if (snd_card_pc98_probe(dev) >= 0)
			cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR IDENT " soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_pc98_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_pc98_cards[idx]);
}

module_init(alsa_card_pc98_init)
module_exit(alsa_card_pc98_exit)

#ifndef MODULE

/* format is: snd-pc98-cs4232=enable,index,id,port,
			 mpu_port,fm_port,
			 irq,mpu_irq,dma1,dma2,pc98ii */

static int __init alsa_card_pc98_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&mpu_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&fm_port[nr_dev]) == 2 &&
	       get_option(&str,&irq[nr_dev]) == 2 &&
	       get_option(&str,&mpu_irq[nr_dev]) == 2 &&
	       get_option(&str,&dma1[nr_dev]) == 2 &&
	       get_option(&str,&dma2[nr_dev]) == 2 &&
	       get_option(&str,&pc98ii[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-pc98-cs4232=", alsa_card_pc98_setup);

#endif /* ifndef MODULE */
