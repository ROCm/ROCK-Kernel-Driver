
/*
    card-azt2320.c - driver for Aztech Systems AZT2320 based soundcards.
    Copyright (C) 1999-2000 by Massimo Piccioni <dafastidio@libero.it>

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

/*
    This driver should provide support for most Aztech AZT2320 based cards.
    Several AZT2316 chips are also supported/tested, but autoprobe doesn't
    work: all module option have to be set.

    No docs available for us at Aztech headquarters !!!   Unbelievable ...
    No other help obtained.

    Thanks to Rainer Wiesner <rainer.wiesner@01019freenet.de> for the WSS
    activation method (full-duplex audio!).
*/

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/wait.h>
#ifndef LINUX_ISAPNP_H
#include <linux/isapnp.h>
#define isapnp_card pci_bus
#define isapnp_dev pci_dev
#endif
#include <sound/core.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include <sound/cs4231.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>

#define chip_t cs4231_t

#define PFX "azt2320: "

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("Aztech Systems AZT2320");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Aztech Systems,PRO16V},"
		"{Aztech Systems,AZT2320},"
		"{Aztech Systems,AZT3300},"
		"{Aztech Systems,AZT2320},"
		"{Aztech Systems,AZT3000}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long wss_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* Pnp setup */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* Pnp setup */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for azt2320 based soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for azt2320 based soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable azt2320 based soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for azt2320 driver.");
MODULE_PARM_SYNTAX(port, SNDRV_PORT12_DESC);
MODULE_PARM(wss_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(wss_port, "WSS Port # for azt2320 driver.");
MODULE_PARM_SYNTAX(wss_port, SNDRV_PORT12_DESC);
MODULE_PARM(mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for azt2320 driver.");
MODULE_PARM_SYNTAX(mpu_port, SNDRV_PORT12_DESC);
MODULE_PARM(fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(fm_port, "FM port # for azt2320 driver.");
MODULE_PARM_SYNTAX(fm_port, SNDRV_PORT12_DESC);
MODULE_PARM(irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(irq, "IRQ # for azt2320 driver.");
MODULE_PARM_SYNTAX(irq, SNDRV_IRQ_DESC);
MODULE_PARM(mpu_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for azt2320 driver.");
MODULE_PARM_SYNTAX(mpu_irq, SNDRV_IRQ_DESC);
MODULE_PARM(dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma1, "1st DMA # for azt2320 driver.");
MODULE_PARM_SYNTAX(dma1, SNDRV_DMA_DESC);
MODULE_PARM(dma2, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma2, "2nd DMA # for azt2320 driver.");
MODULE_PARM_SYNTAX(dma2, SNDRV_DMA_DESC);

struct snd_card_azt2320 {
#ifdef __ISAPNP__
	struct isapnp_dev *dev;
	struct isapnp_dev *devmpu;
#endif	/* __ISAPNP__ */
};

static snd_card_t *snd_azt2320_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef __ISAPNP__

static struct isapnp_card *snd_azt2320_isapnp_cards[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;
static const struct isapnp_card_id *snd_azt2320_isapnp_id[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;

#define ISAPNP_AZT2320(_va, _vb, _vc, _device, _audio, _mpu401) \
	{ \
		ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
		.devs = { ISAPNP_DEVICE_ID(_va, _vb, _vc, _audio), \
			  ISAPNP_DEVICE_ID(_va, _vb, _vc, _mpu401), } \
	}

static struct isapnp_card_id snd_azt2320_pnpids[] __devinitdata = {
	/* PRO16V */
	ISAPNP_AZT2320('A','Z','T',0x1008,0x1008,0x2001),
	/* Aztech Sound Galaxy 16 */
	ISAPNP_AZT2320('A','Z','T',0x2320,0x0001,0x0002),
	/* Packard Bell Sound III 336 AM/SP */
	ISAPNP_AZT2320('A','Z','T',0x3000,0x1003,0x2001),
	/* AT3300 */
	ISAPNP_AZT2320('A','Z','T',0x3002,0x1004,0x2001),
	/* --- */
	ISAPNP_AZT2320('A','Z','T',0x3005,0x1003,0x2001),
	/* --- */
	ISAPNP_AZT2320('A','Z','T',0x3011,0x1003,0x2001),
	{ ISAPNP_CARD_END, }	/* end */
};

ISAPNP_CARD_TABLE(snd_azt2320_pnpids);

#endif	/* __ISAPNP__ */

#define	DRIVER_NAME	"snd-card-azt2320"


#ifdef __ISAPNP__
static int __init snd_card_azt2320_isapnp(int dev, struct snd_card_azt2320 *acard)
{
	const struct isapnp_card_id *id = snd_azt2320_isapnp_id[dev];
	struct isapnp_card *card = snd_azt2320_isapnp_cards[dev];
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

	pdev = acard->dev;
	if (pdev->prepare(pdev) < 0)
		return -EAGAIN;

	if (port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], port[dev], 16);
	if (fm_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[1], fm_port[dev], 4);
	if (wss_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[2], wss_port[dev],
			4);
	if (dma1[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[0], dma1[dev],
			1);
	if (dma2[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[1], dma2[dev],
			1);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], irq[dev], 1);

	if (pdev->activate(pdev) < 0) {
		printk(KERN_ERR PFX "AUDIO isapnp configure failure\n");
		return -EBUSY;
	}

	port[dev] = pdev->resource[0].start;
	fm_port[dev] = pdev->resource[1].start;
	wss_port[dev] = pdev->resource[2].start;
	dma1[dev] = pdev->dma_resource[0].start;
	dma2[dev] = pdev->dma_resource[1].start;
	irq[dev] = pdev->irq_resource[0].start;

	pdev = acard->devmpu;
	if (pdev == NULL || pdev->prepare(pdev) < 0) {
		mpu_port[dev] = -1;
		return 0;
	}

	if (mpu_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], mpu_port[dev],
			2);
	if (mpu_irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], mpu_irq[dev],
			1);

	if (pdev->activate(pdev) < 0) {
		/* not fatal error */
		printk(KERN_ERR PFX "MPU-401 isapnp configure failure\n");
		mpu_port[dev] = -1;
		acard->devmpu = NULL;
	} else {
		mpu_port[dev] = pdev->resource[0].start;
		mpu_irq[dev] = pdev->irq_resource[0].start;
	}

	return 0;
}

static void snd_card_azt2320_deactivate(struct snd_card_azt2320 *acard)
{
	if (acard->dev)
		acard->dev->deactivate(acard->dev);
	if (acard->devmpu)
		acard->devmpu->deactivate(acard->devmpu);
}
#endif	/* __ISAPNP__ */

/* same of snd_sbdsp_command by Jaroslav Kysela */
static int __init snd_card_azt2320_command(unsigned long port, unsigned char val)
{
	int i;
	unsigned long limit;

	limit = jiffies + HZ / 10;
	for (i = 50000; i && (limit - jiffies) > 0; i--)
		if (!(inb(port + 0x0c) & 0x80)) {
			outb(val, port + 0x0c);
			return 0;
		}
	return -EBUSY;
}

static int __init snd_card_azt2320_enable_wss(unsigned long port)
{
	int error;

	if ((error = snd_card_azt2320_command(port, 0x09)))
		return error;
	if ((error = snd_card_azt2320_command(port, 0x00)))
		return error;

	mdelay(5);
	return 0;
}

static void snd_card_azt2320_free(snd_card_t *card)
{
	struct snd_card_azt2320 *acard = (struct snd_card_azt2320 *)card->private_data;

	if (acard) {
#ifdef __ISAPNP__
		snd_card_azt2320_deactivate(acard);
#endif	/* __ISAPNP__ */
	}
}

static int __init snd_card_azt2320_probe(int dev)
{
	int error;
	snd_card_t *card;
	struct snd_card_azt2320 *acard;
	cs4231_t *chip;
	opl3_t *opl3;

	if ((card = snd_card_new(index[dev], id[dev], THIS_MODULE,
				 sizeof(struct snd_card_azt2320))) == NULL)
		return -ENOMEM;
	acard = (struct snd_card_azt2320 *)card->private_data;
	card->private_free = snd_card_azt2320_free;

#ifdef __ISAPNP__
	if ((error = snd_card_azt2320_isapnp(dev, acard))) {
		snd_card_free(card);
		return error;
	}
#endif	/* __ISAPNP__ */

	if ((error = snd_card_azt2320_enable_wss(port[dev]))) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_cs4231_create(card, wss_port[dev], -1,
				       irq[dev],
				       dma1[dev],
				       dma2[dev],
				       CS4231_HW_DETECT, 0, &chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_cs4231_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_cs4231_mixer(chip)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_cs4231_timer(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}

	if (mpu_port[dev] > 0) {
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_AZT2320,
				mpu_port[dev], 0,
				mpu_irq[dev], SA_INTERRUPT,
				NULL) < 0)
			printk(KERN_ERR PFX "no MPU-401 device at 0x%lx\n",
				mpu_port[dev]);
	}

	if (fm_port[dev] > 0) {
		if (snd_opl3_create(card,
				    fm_port[dev], fm_port[dev] + 2,
				    OPL3_HW_AUTO, 0, &opl3) < 0) {
			printk(KERN_ERR PFX "no OPL device at 0x%lx-0x%lx\n",
				fm_port[dev], fm_port[dev] + 2);
		} else {
			if ((error = snd_opl3_timer_new(opl3, 1, 2)) < 0) {
				snd_card_free(card);
				return error;
			}
			if ((error = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
				snd_card_free(card);
				return error;
			}
		}
	}

	strcpy(card->driver, "AZT2320");
	strcpy(card->shortname, "Aztech AZT2320");
	sprintf(card->longname, "%s soundcard, WSS at 0x%lx, irq %i, dma %i&%i",
		card->shortname, chip->port, irq[dev], dma1[dev], dma2[dev]);

	if ((error = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return error;
	}
	snd_azt2320_cards[dev] = card;
	return 0;
}

#ifdef __ISAPNP__
static int __init snd_azt2320_isapnp_detect(struct isapnp_card *card,
                                            const struct isapnp_card_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		snd_azt2320_isapnp_cards[dev] = card;
		snd_azt2320_isapnp_id[dev] = id;
                res = snd_card_azt2320_probe(dev);
		if (res < 0)
			return res;
		dev++;
		return 0;
	}
        return -ENODEV;
}
#endif

static int __init alsa_card_azt2320_init(void)
{
	int cards = 0;

#ifdef __ISAPNP__
	cards += isapnp_probe_cards(snd_azt2320_pnpids, snd_azt2320_isapnp_detect);
#else
	printk(KERN_ERR PFX "you have to enable ISA PnP support.\n");
#endif
#ifdef MODULE
	if (!cards)
		printk(KERN_ERR "no AZT2320 based soundcards found\n");
#endif
	return cards ? 0 : -ENODEV;
}

static void __exit alsa_card_azt2320_exit(void)
{
	int dev;

	for (dev = 0; dev < SNDRV_CARDS; dev++)
		snd_card_free(snd_azt2320_cards[dev]);
}

module_init(alsa_card_azt2320_init)
module_exit(alsa_card_azt2320_exit)

#ifndef MODULE

/* format is: snd-azt2320=enable,index,id,port,
			  wss_port,mpu_port,fm_port,
			  irq,mpu_irq,dma1,dma2 */

static int __init alsa_card_azt2320_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&wss_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&mpu_port[nr_dev]) == 2 &&
	       get_option(&str,&irq[nr_dev]) == 2 &&
	       get_option(&str,&mpu_irq[nr_dev]) == 2 &&
	       get_option(&str,&dma1[nr_dev]) == 2 &&
	       get_option(&str,&dma2[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-azt2320=", alsa_card_azt2320_setup);

#endif /* ifndef MODULE */
