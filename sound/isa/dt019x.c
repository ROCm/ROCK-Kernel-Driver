
/*
    dt019x.c - driver for Diamond Technologies DT-0197H based soundcards.
    Copyright (C) 1999, 2002 by Massimo Piccioni <dafastidio@libero.it>

    Generalised for soundcards based on DT-0196 and ALS-007 chips 
    by Jonathan Woithe <jwoithe@physics.adelaide.edu.au>: June 2002.

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
#include <linux/sched.h>
#include <linux/wait.h>
#ifndef LINUX_ISAPNP_H
#include <linux/isapnp.h>
#define isapnp_card pci_bus
#define isapnp_dev pci_dev
#endif
#include <sound/core.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/sb.h>

#define chip_t sb_t

#define PFX "dt019x: "

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("Diamond Technologies DT-019X / Avance Logic ALS-007");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Diamond Technologies DT-019X},"
	       "{Avance Logic ALS-007}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* PnP setup */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* PnP setup */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for DT-019X based soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for DT-019X based soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable DT-019X based soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for dt019x driver.");
MODULE_PARM_SYNTAX(port, SNDRV_PORT12_DESC);
MODULE_PARM(mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for dt019x driver.");
MODULE_PARM_SYNTAX(mpu_port, SNDRV_PORT12_DESC);
MODULE_PARM(fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(fm_port, "FM port # for dt019x driver.");
MODULE_PARM_SYNTAX(fm_port, SNDRV_PORT12_DESC);
MODULE_PARM(irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(irq, "IRQ # for dt019x driver.");
MODULE_PARM_SYNTAX(irq, SNDRV_IRQ_DESC);
MODULE_PARM(mpu_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for dt019x driver.");
MODULE_PARM_SYNTAX(mpu_irq, SNDRV_IRQ_DESC);
MODULE_PARM(dma8, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma8, "8-bit DMA # for dt019x driver.");
MODULE_PARM_SYNTAX(dma8, SNDRV_DMA8_DESC);

struct snd_card_dt019x {
#ifdef __ISAPNP__
	struct isapnp_dev *dev;
	struct isapnp_dev *devmpu;
	struct isapnp_dev *devopl;
#endif	/* __ISAPNP__ */
};

static snd_card_t *snd_dt019x_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef __ISAPNP__
static struct isapnp_card *snd_dt019x_isapnp_cards[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;
static const struct isapnp_card_id *snd_dt019x_isapnp_id[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;

static struct isapnp_card_id snd_dt019x_pnpids[] __devinitdata = {
	/* DT197A30 */
	{
		ISAPNP_CARD_ID('R','W','B',0x1688),
		.devs = { ISAPNP_DEVICE_ID('@','@','@',0x0001),
			ISAPNP_DEVICE_ID('@','X','@',0x0001),
			ISAPNP_DEVICE_ID('@','H','@',0x0001) }
	},
	/* DT0196 / ALS-007 */
	{
		ISAPNP_CARD_ID('A','L','S',0x0007),
		.devs = { ISAPNP_DEVICE_ID('@','@','@',0x0001),
			ISAPNP_DEVICE_ID('@','X','@',0x0001),
			ISAPNP_DEVICE_ID('@','H','@',0x0001) }
	},
	{ ISAPNP_CARD_END, }
};

ISAPNP_CARD_TABLE(snd_dt019x_pnpids);

#endif	/* __ISAPNP__ */

#define DRIVER_NAME	"snd-card-dt019x"


#ifdef __ISAPNP__
static int __init snd_card_dt019x_isapnp(int dev, struct snd_card_dt019x *acard)
{
	const struct isapnp_card_id *id = snd_dt019x_isapnp_id[dev];
	struct isapnp_card *card = snd_dt019x_isapnp_cards[dev];
	struct isapnp_dev *pdev;

	acard->dev = isapnp_find_dev(card, id->devs[0].vendor, id->devs[0].function, NULL);
	if (acard->dev->active) {
		acard->dev = NULL;
		return -EBUSY;
	}
	acard->devmpu = isapnp_find_dev(card, id->devs[1].vendor, id->devs[1].function, NULL);
	if (acard->devmpu->active) {
		acard->dev = acard->devmpu = NULL;
		return -EBUSY;
	}
	acard->devopl = isapnp_find_dev(card, id->devs[2].vendor, id->devs[2].function, NULL);
	if (acard->devopl->active) {
		acard->dev = acard->devmpu = acard->devopl = NULL;
		return -EBUSY;
	}

	pdev = acard->dev;
	if (!pdev || pdev->prepare(pdev)<0)
		return -EAGAIN;

	if (port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], port[dev], 16);
	if (dma8[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[0], dma8[dev],
			1);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], irq[dev], 1);

	if (pdev->activate(pdev)<0) {
		printk(KERN_ERR PFX "DT-019X AUDIO isapnp configure failure\n");
		return -EBUSY;
	}
	port[dev] = pdev->resource[0].start;
	dma8[dev] = pdev->dma_resource[0].start;
	irq[dev] = pdev->irq_resource[0].start;
	snd_printdd("dt019x: found audio interface: port=0x%lx, irq=0x%lx, dma=0x%lx\n",
			port[dev],irq[dev],dma8[dev]);

	pdev = acard->devmpu;
	if (!pdev || pdev->prepare(pdev)<0) 
		return 0;

	if (mpu_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], mpu_port[dev],
			2);
	if (mpu_irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], mpu_irq[dev],
			1);

	if (pdev->activate(pdev)<0) {
		printk(KERN_ERR PFX "DT-019X MPU-401 isapnp configure failure\n");
		mpu_port[dev] = -1;
		acard->devmpu = NULL;
	} else {
		mpu_port[dev] = pdev->resource[0].start;
		mpu_irq[dev] = pdev->irq_resource[0].start;
		snd_printdd("dt019x: found MPU-401: port=0x%lx, irq=0x%lx\n",
			 	mpu_port[dev],mpu_irq[dev]);
	}

	pdev = acard->devopl;
	if (!pdev || pdev->prepare(pdev)<0)
		return 0;

	if (fm_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], fm_port[dev], 4);

	if (pdev->activate(pdev)<0) {
		printk(KERN_ERR PFX "DT-019X OPL3 isapnp configure failure\n");
		fm_port[dev] = -1;
		acard->devopl = NULL;
	} else {
		fm_port[dev] = pdev->resource[0].start;
		snd_printdd("dt019x: found OPL3 synth: port=0x%lx\n",fm_port[dev]);
	}

	return 0;
}

static void snd_card_dt019x_deactivate(struct snd_card_dt019x *acard)
{
	if (acard->dev) {
		acard->dev->deactivate(acard->dev);
		acard->dev = NULL;
	}
	if (acard->devmpu) {
		acard->devmpu->deactivate(acard->devmpu);
		acard->devmpu = NULL;
	}
	if (acard->devopl) {
		acard->devopl->deactivate(acard->devopl);
		acard->devopl = NULL;
	}
}
#endif	/* __ISAPNP__ */

static void snd_card_dt019x_free(snd_card_t *card)
{
	struct snd_card_dt019x *acard = (struct snd_card_dt019x *)card->private_data;

	if (acard != NULL) {
#ifdef __ISAPNP__
		snd_card_dt019x_deactivate(acard);
#endif	/* __ISAPNP__ */
	}
}

static int __init snd_card_dt019x_probe(int dev)
{
	int error;
	sb_t *chip;
	snd_card_t *card;
	struct snd_card_dt019x *acard;
	opl3_t *opl3;

	if ((card = snd_card_new(index[dev], id[dev], THIS_MODULE,
				 sizeof(struct snd_card_dt019x))) == NULL)
		return -ENOMEM;
	acard = (struct snd_card_dt019x *)card->private_data;
	card->private_free = snd_card_dt019x_free;

#ifdef __ISAPNP__
	if ((error = snd_card_dt019x_isapnp(dev, acard))) {
		snd_card_free(card);
		return error;
	}
#else
	printk(KERN_ERR PFX "you have to enable PnP support ...\n");
	snd_card_free(card);
	return -ENOSYS;
#endif	/* __ISAPNP__ */

	if ((error = snd_sbdsp_create(card, port[dev],
				      irq[dev],
				      snd_sb16dsp_interrupt,
				      dma8[dev],
				      -1,
				      SB_HW_DT019X,
				      &chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sb16dsp_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_sbmixer_new(chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if (mpu_port[dev] > 0) {
		if (snd_mpu401_uart_new(card, 0,
/*					MPU401_HW_SB,*/
					MPU401_HW_MPU401,
					mpu_port[dev], 0,
					mpu_irq[dev],
					SA_INTERRUPT,
					NULL) < 0)
			printk(KERN_ERR PFX "no MPU-401 device at 0x%lx ?\n",
				mpu_port[dev]);
	}

	if (fm_port[dev] > 0) {
		if (snd_opl3_create(card,
				    fm_port[dev],
				    fm_port[dev] + 2,
				    OPL3_HW_AUTO, 0, &opl3) < 0) {
			printk(KERN_ERR PFX "no OPL device at 0x%lx-0x%lx ?\n",
				fm_port[dev], fm_port[dev] + 2);
		} else {
			if ((error = snd_opl3_timer_new(opl3, 0, 1)) < 0) {
				snd_card_free(card);
				return error;
			}
			if ((error = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
				snd_card_free(card);
				return error;
			}
		}
	}

	strcpy(card->driver, "DT-019X");
	strcpy(card->shortname, "Diamond Tech. DT-019X");
	sprintf(card->longname, "%s soundcard, %s at 0x%lx, irq %d, dma %d",
		card->shortname, chip->name, chip->port,
		irq[dev], dma8[dev]);
	if ((error = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return error;
	}
	snd_dt019x_cards[dev] = card;
	return 0;
}

#ifdef __ISAPNP__
static int __init snd_dt019x_isapnp_detect(struct isapnp_card *card,
					    const struct isapnp_card_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		snd_dt019x_isapnp_cards[dev] = card;
		snd_dt019x_isapnp_id[dev] = id;
		res = snd_card_dt019x_probe(dev);
		if (res < 0)
			return res;
		dev++;
		return 0;
        }
        return -ENODEV;
}
#endif /* __ISAPNP__ */

static int __init alsa_card_dt019x_init(void)
{
	int cards = 0;

#ifdef __ISAPNP__
	cards += isapnp_probe_cards(snd_dt019x_pnpids, snd_dt019x_isapnp_detect);
#else
	printk(KERN_ERR PFX "you have to enable ISA PnP support.\n");
#endif
#ifdef MODULE
	if (!cards)
		printk(KERN_ERR "no DT-019X / ALS-007 based soundcards found\n");
#endif
	return cards ? 0 : -ENODEV;
}

static void __exit alsa_card_dt019x_exit(void)
{
	int dev;

	for (dev = 0; dev < SNDRV_CARDS; dev++)
		snd_card_free(snd_dt019x_cards[dev]);
}

module_init(alsa_card_dt019x_init)
module_exit(alsa_card_dt019x_exit)

#ifndef MODULE

/* format is: snd-dt019x=enable,index,id,
			  port,mpu_port,fm_port,
			  irq,mpu_irq,dma8,dma8_size */

static int __init alsa_card_dt019x_setup(char *str)
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
	       get_option(&str,&dma8[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-dt019x=", alsa_card_dt019x_setup);

#endif /* ifndef MODULE */
