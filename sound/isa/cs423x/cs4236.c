/*
 *  Driver for generic CS4232/CS4235/CS4236/CS4236B/CS4237B/CS4238B/CS4239 chips
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
#ifndef LINUX_ISAPNP_H
#include <linux/isapnp.h>
#define isapnp_card pci_bus
#define isapnp_dev pci_dev
#endif
#include <sound/core.h>
#include <sound/cs4231.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#define chip_t cs4231_t

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
#ifdef CS4232
MODULE_DESCRIPTION("Cirrus Logic CS4232");
MODULE_DEVICES("{{Turtle Beach,TBS-2000},"
		"{Turtle Beach,Tropez Plus},"
		"{SIC CrystalWave 32},"
		"{Hewlett Packard,Omnibook 5500},"
		"{TerraTec,Maestro 32/96},"
		"{Philips,PCA70PS}}");
#else
MODULE_DESCRIPTION("Cirrus Logic CS4235-9");
MODULE_DEVICES("{{Crystal Semiconductors,CS4235},"
		"{Crystal Semiconductors,CS4236},"
		"{Crystal Semiconductors,CS4237},"
		"{Crystal Semiconductors,CS4238},"
		"{Crystal Semiconductors,CS4239},"
		"{Acer,AW37},"
		"{Acer,AW35/Pro},"
		"{Crystal,3D},"
		"{Crystal Computer,TidalWave128},"
		"{Dell,Optiplex GX1},"
		"{Dell,Workstation 400 sound},"
		"{EliteGroup,P5TX-LA sound},"
		"{Gallant,SC-70P},"
		"{Gateway,E1000 Onboard CS4236B},"
		"{Genius,Sound Maker 3DJ},"
		"{Hewlett Packard,HP6330 sound},"
		"{IBM,PC 300PL sound},"
		"{IBM,Aptiva 2137 E24},"
		"{IBM,IntelliStation M Pro},"
		"{Intel,Marlin Spike Mobo CS4235},"
		"{Intel PR440FX Onboard},"
		"{Guillemot,MaxiSound 16 PnP},"
		"{NewClear,3D},"
		"{TerraTec,AudioSystem EWS64L/XL},"
		"{Typhoon Soundsystem,CS4236B},"
		"{Turtle Beach,Malibu},"
		"{Unknown,Digital PC 5000 Onboard}}");
#endif

#ifdef CS4232
#define IDENT "CS4232"
#else
#define IDENT "CS4236+"
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
#ifdef __ISAPNP__
static int isapnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#endif
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long cport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;/* PnP setup */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long sb_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,11,12,15 */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 9,11,12,15 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for " IDENT " soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for " IDENT " soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable " IDENT " soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
#ifdef __ISAPNP__
MODULE_PARM(isapnp, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(isapnp, "ISA PnP detection for specified soundcard.");
MODULE_PARM_SYNTAX(isapnp, SNDRV_ISAPNP_DESC);
#endif
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for " IDENT " driver.");
MODULE_PARM_SYNTAX(port, SNDRV_PORT12_DESC);
MODULE_PARM(cport, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(cport, "Control port # for " IDENT " driver.");
MODULE_PARM_SYNTAX(cport, SNDRV_PORT12_DESC);
MODULE_PARM(mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for " IDENT " driver.");
MODULE_PARM_SYNTAX(mpu_port, SNDRV_PORT12_DESC);
MODULE_PARM(fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(fm_port, "FM port # for " IDENT " driver.");
MODULE_PARM_SYNTAX(fm_port, SNDRV_PORT12_DESC);
MODULE_PARM(sb_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(sb_port, "SB port # for " IDENT " driver (optional).");
MODULE_PARM_SYNTAX(sb_port, SNDRV_PORT12_DESC);
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

struct snd_card_cs4236 {
	struct resource *res_sb_port;
#ifdef __ISAPNP__
	struct isapnp_dev *wss;
	struct isapnp_dev *ctrl;
	struct isapnp_dev *mpu;
#endif
};

static snd_card_t *snd_cs4236_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef __ISAPNP__

static struct isapnp_card *snd_cs4236_isapnp_cards[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;
static const struct isapnp_card_id *snd_cs4236_isapnp_id[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;

#define ISAPNP_CS4232(_va, _vb, _vc, _device, _wss, _ctrl, _mpu401) \
	{ \
		ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
		.devs = { ISAPNP_DEVICE_ID(_va, _vb, _vc, _wss), \
                          ISAPNP_DEVICE_ID(_va, _vb, _vc, _ctrl), \
			  ISAPNP_DEVICE_ID(_va, _vb, _vc, _mpu401) } \
        }
#define ISAPNP_CS4232_1(_va, _vb, _vc, _device, _wss, _ctrl, _mpu401) \
	{ \
		ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
		.devs = { ISAPNP_DEVICE_ID(_va, _vb, _vc, _wss), \
                          ISAPNP_DEVICE_ID(_va, _vb, _vc, _ctrl), \
		 	  ISAPNP_DEVICE_ID('P', 'N', 'P', _mpu401) } \
        }
#define ISAPNP_CS4232_WOMPU(_va, _vb, _vc, _device, _wss, _ctrl) \
	{ \
		ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
		.devs = { ISAPNP_DEVICE_ID(_va, _vb, _vc, _wss), \
                          ISAPNP_DEVICE_ID(_va, _vb, _vc, _ctrl) } \
        }


#ifdef CS4232
static struct isapnp_card_id snd_card_pnpids[] __devinitdata = {
	/* Philips PCA70PS */
	ISAPNP_CS4232_1('C','S','C',0x0d32,0x0000,0x0010,0xb006),
	/* TerraTec Maestro 32/96 (CS4232) */
	ISAPNP_CS4232('C','S','C',0x1a32,0x0000,0x0010,0x0003),
	/* HP Omnibook 5500 onboard */
	ISAPNP_CS4232('C','S','C',0x4232,0x0000,0x0002,0x0003),
	/* Unnamed CS4236 card (Made in Taiwan) */
	ISAPNP_CS4232('C','S','C',0x4236,0x0000,0x0010,0x0003),
	/* Turtle Beach TBS-2000 (CS4232) */
	ISAPNP_CS4232('C','S','C',0x7532,0x0000,0x0010,0xb006),
	/* Turtle Beach Tropez Plus (CS4232) */
	ISAPNP_CS4232_1('C','S','C',0x7632,0x0000,0x0010,0xb006),
	/* SIC CrystalWave 32 (CS4232) */
	ISAPNP_CS4232('C','S','C',0xf032,0x0000,0x0010,0x0003),
	/* --- */
	{ ISAPNP_CARD_END, }	/* end */
};
#else /* CS4236 */
static struct isapnp_card_id snd_card_pnpids[] __devinitdata = {
	/* Intel Marlin Spike Motherboard - CS4235 */
	ISAPNP_CS4232('C','S','C',0x0225,0x0000,0x0010,0x0003),
	/* Intel Marlin Spike Motherboard (#2) - CS4235 */
	ISAPNP_CS4232('C','S','C',0x0225,0x0100,0x0110,0x0103),
	/* Genius Sound Maker 3DJ - CS4237B */
	ISAPNP_CS4232('C','S','C',0x0437,0x0000,0x0010,0x0003),
	/* Digital PC 5000 Onboard - CS4236B */
	ISAPNP_CS4232_WOMPU('C','S','C',0x0735,0x0000,0x0010),
	/* some uknown CS4236B */
	ISAPNP_CS4232('C','S','C',0x0b35,0x0000,0x0010,0x0003),
	/* Intel PR440FX Onboard sound */
	ISAPNP_CS4232('C','S','C',0x0b36,0x0000,0x0010,0x0003),
	/* CS4235 on mainboard without MPU */
	ISAPNP_CS4232_WOMPU('C','S','C',0x1425,0x0100,0x0110),
	/* Gateway E1000 Onboard CS4236B */
	ISAPNP_CS4232('C','S','C',0x1335,0x0000,0x0010,0x0003),
	/* HP 6330 Onboard sound */
	ISAPNP_CS4232('C','S','C',0x1525,0x0100,0x0110,0x0103),
	/* Crystal Computer TidalWave128 */
	ISAPNP_CS4232('C','S','C',0x1e37,0x0000,0x0010,0x0003),
	/* ACER AW37 - CS4235 */
	ISAPNP_CS4232('C','S','C',0x4236,0x0000,0x0010,0x0003),
	/* build-in soundcard in EliteGroup P5TX-LA motherboard - CS4237B */
	ISAPNP_CS4232('C','S','C',0x4237,0x0000,0x0010,0x0003),
	/* Crystal 3D - CS4237B */
	ISAPNP_CS4232('C','S','C',0x4336,0x0000,0x0010,0x0003),
	/* Typhoon Soundsystem PnP - CS4236B */
	ISAPNP_CS4232('C','S','C',0x4536,0x0000,0x0010,0x0003),
	/* Crystal CX4235-XQ3 EP - CS4235 */
	ISAPNP_CS4232('C','S','C',0x4625,0x0100,0x0110,0x0103),
	/* TerraTec AudioSystem EWS64XL - CS4236B */
	ISAPNP_CS4232('C','S','C',0xa836,0xa800,0xa810,0xa803),
	/* TerraTec AudioSystem EWS64XL - CS4236B */
	ISAPNP_CS4232_WOMPU('C','S','C',0xa836,0xa800,0xa810),
	/* Crystal Semiconductors CS4237B */
	ISAPNP_CS4232('C','S','C',0x4637,0x0000,0x0010,0x0003),
	/* NewClear 3D - CX4237B-XQ3 */
	ISAPNP_CS4232('C','S','C',0x4837,0x0000,0x0010,0x0003),
	/* Dell Optiplex GX1 - CS4236B */
	ISAPNP_CS4232('C','S','C',0x6835,0x0000,0x0010,0x0003),
	/* Dell P410 motherboard - CS4236B */
	ISAPNP_CS4232_WOMPU('C','S','C',0x6835,0x0000,0x0010),
	/* Dell Workstation 400 Onboard - CS4236B */
	ISAPNP_CS4232('C','S','C',0x6836,0x0000,0x0010,0x0003),
	/* Turtle Beach Malibu - CS4237B */
	ISAPNP_CS4232('C','S','C',0x7537,0x0000,0x0010,0x0003),
	/* CS4235 - onboard */
	ISAPNP_CS4232('C','S','C',0x8025,0x0100,0x0110,0x0103),
	/* IBM PC 300PL Onboard - CS4236B */
	ISAPNP_CS4232_WOMPU('C','S','C',0xe836,0x0000,0x0010),
	/* IBM Aptiva 2137 E24 Onboard - CS4237B */
	ISAPNP_CS4232('C','S','C',0x8037,0x0000,0x0010,0x0003),
	/* IBM IntelliStation M Pro motherboard */
	ISAPNP_CS4232_WOMPU('C','S','C',0xc835,0x0000,0x0010),
	/* Guillemot MaxiSound 16 PnP - CS4236B */
	ISAPNP_CS4232('C','S','C',0x9836,0x0000,0x0010,0x0003),
	/* Gallant SC-70P */
	ISAPNP_CS4232('C','S','C',0x9837,0x0000,0x0010,0x0003),
	/* ACER AW37/Pro - CS4235 */
	ISAPNP_CS4232('C','S','C',0xd925,0x0000,0x0010,0x0003),
	/* ACER AW35/Pro - CS4237B */
	ISAPNP_CS4232('C','S','C',0xd937,0x0000,0x0010,0x0003),
	/* CS4235 without MPU401 */
	ISAPNP_CS4232_WOMPU('C','S','C',0xe825,0x0100,0x0110),
	/* IBM IntelliStation M Pro 6898 11U - CS4236B */
	ISAPNP_CS4232_WOMPU('C','S','C',0xe835,0x0000,0x0010),
	/* Some noname CS4236 based card */
	ISAPNP_CS4232('C','S','C',0xe936,0x0000,0x0010,0x0003),
	/* CS4236B */
	ISAPNP_CS4232('C','S','C',0xf235,0x0000,0x0010,0x0003),
	/* CS4236B */
	ISAPNP_CS4232('C','S','C',0xf238,0x0000,0x0010,0x0003),
	/* --- */
	{ ISAPNP_CARD_END, }	/* end */
};
#endif

ISAPNP_CARD_TABLE(snd_card_pnpids);

static int __init snd_card_cs4236_isapnp(int dev, struct snd_card_cs4236 *acard)
{
	const struct isapnp_card_id *id = snd_cs4236_isapnp_id[dev];
	struct isapnp_card *card = snd_cs4236_isapnp_cards[dev];
	struct isapnp_dev *pdev;
	
	acard->wss = isapnp_find_dev(card, id->devs[0].vendor, id->devs[0].function, NULL);
	if (acard->wss->active) {
		acard->wss = NULL;
		return -EBUSY;
	}
	acard->ctrl = isapnp_find_dev(card, id->devs[1].vendor, id->devs[1].function, NULL);
	if (acard->ctrl->active) {
		acard->wss = acard->ctrl = NULL;
		return -EBUSY;
	}
	if (id->devs[2].vendor && id->devs[2].function) {
		acard->mpu = isapnp_find_dev(card, id->devs[2].vendor, id->devs[2].function, NULL);
		if (acard->mpu->active) {
			acard->wss = acard->ctrl = acard->mpu = NULL;
			return -EBUSY;
		}
	}

	/* WSS initialization */
	pdev = acard->wss;
	if (pdev->prepare(pdev) < 0)
		return -EAGAIN;
	if (port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], port[dev], 4);
	if (fm_port[dev] != SNDRV_AUTO_PORT && fm_port[dev] >= 0)
		isapnp_resource_change(&pdev->resource[1], fm_port[dev], 4);
	if (sb_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[2], sb_port[dev], 16);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], irq[dev], 1);
	if (dma1[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[0], dma1[dev], 1);
	if (dma2[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[1], dma2[dev] < 0 ? 4 : dma2[dev], 1);
	if (pdev->activate(pdev)<0) {
		printk(KERN_ERR IDENT " isapnp configure failed for WSS (out of resources?)\n");
		return -EBUSY;
	}
	port[dev] = pdev->resource[0].start;
	if (fm_port[dev] >= 0)
		fm_port[dev] = pdev->resource[1].start;
	sb_port[dev] = pdev->resource[2].start;
	irq[dev] = pdev->irq_resource[0].start;
	dma1[dev] = pdev->dma_resource[0].start;
	dma2[dev] = pdev->dma_resource[1].start == 4 ? -1 : (int)pdev->dma_resource[1].start;
	snd_printdd("isapnp WSS: wss port=0x%lx, fm port=0x%lx, sb port=0x%lx\n",
			port[dev], fm_port[dev], sb_port[dev]);
	snd_printdd("isapnp WSS: irq=%i, dma1=%i, dma2=%i\n",
			irq[dev], dma1[dev], dma2[dev]);
	/* CTRL initialization */
	if (acard->ctrl && cport[dev] >= 0) {
		pdev = acard->ctrl;
		if (pdev->prepare(pdev) < 0) {
			acard->wss->deactivate(acard->wss);
			return -EAGAIN;
		}
		if (cport[dev] != SNDRV_AUTO_PORT)
			isapnp_resource_change(&pdev->resource[0], cport[dev], 8);
		if (pdev->activate(pdev)<0) {
			printk(KERN_ERR IDENT " isapnp configure failed for control (out of resources?)\n");
			acard->wss->deactivate(acard->wss);
			return -EBUSY;
		}
		cport[dev] = pdev->resource[0].start;
		snd_printdd("isapnp CTRL: control port=0x%lx\n", cport[dev]);
	}
	/* MPU initialization */
	if (acard->mpu && mpu_port[dev] >= 0) {
		pdev = acard->mpu;
		if (pdev->prepare(pdev) < 0) {
			acard->wss->deactivate(acard->wss);
			acard->ctrl->deactivate(acard->ctrl);
			return -EAGAIN;
		}
		if (mpu_port[dev] != SNDRV_AUTO_PORT)
			isapnp_resource_change(&pdev->resource[0], mpu_port[dev], 2);
		if (mpu_irq[dev] != SNDRV_AUTO_IRQ && mpu_irq[dev] >= 0)
			isapnp_resource_change(&pdev->irq_resource[0], mpu_irq[dev], 1);
		if (pdev->activate(pdev)<0) {
			mpu_port[dev] = SNDRV_AUTO_PORT;
			mpu_irq[dev] = SNDRV_AUTO_IRQ;
			printk(KERN_ERR IDENT " isapnp configure failed for MPU (out of resources?)\n");
		} else {
			mpu_port[dev] = pdev->resource[0].start;
			if ((pdev->irq_resource[0].flags & IORESOURCE_IRQ) &&
			    mpu_irq[dev] >= 0) {
				mpu_irq[dev] = pdev->irq_resource[0].start;
			} else {
				mpu_irq[dev] = -1;	/* disable interrupt */
			}
		}
		snd_printdd("isapnp MPU: port=0x%lx, irq=%i\n", mpu_port[dev], mpu_irq[dev]);
	}
	return 0;
}

static void snd_card_cs4236_deactivate(struct snd_card_cs4236 *acard)
{
	if (acard->wss) {
		acard->wss->deactivate(acard->wss);
		acard->wss = NULL;
	}
	if (acard->ctrl) {
		acard->ctrl->deactivate(acard->ctrl);
		acard->ctrl = NULL;
	}
	if (acard->mpu) {
		acard->mpu->deactivate(acard->mpu);
		acard->mpu = NULL;
	}
}
#endif

static void snd_card_cs4236_free(snd_card_t *card)
{
	struct snd_card_cs4236 *acard = (struct snd_card_cs4236 *)card->private_data;

	if (acard) {
#ifdef __ISAPNP__
		snd_card_cs4236_deactivate(acard);
#endif
		if (acard->res_sb_port) {
			release_resource(acard->res_sb_port);
			kfree_nocheck(acard->res_sb_port);
		}
	}
}

static int __init snd_card_cs4236_probe(int dev)
{
	snd_card_t *card;
	struct snd_card_cs4236 *acard;
	snd_pcm_t *pcm = NULL;
	cs4231_t *chip;
	opl3_t *opl3;
	int err;

#ifdef __ISAPNP__
	if (!isapnp[dev]) {
#endif
		if (port[dev] == SNDRV_AUTO_PORT) {
			snd_printk("specify port\n");
			return -EINVAL;
		}
		if (cport[dev] == SNDRV_AUTO_PORT) {
			snd_printk("specify cport\n");
			return -EINVAL;
		}
#ifdef __ISAPNP__
	}
#endif
	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(struct snd_card_cs4236));
	if (card == NULL)
		return -ENOMEM;
	acard = (struct snd_card_cs4236 *)card->private_data;
	card->private_free = snd_card_cs4236_free;
#ifdef __ISAPNP__
	if (isapnp[dev] && (err = snd_card_cs4236_isapnp(dev, acard))<0) {
		printk(KERN_ERR "isapnp detection failed and probing for " IDENT " is not supported\n");
		snd_card_free(card);
		return -ENXIO;
	}
#endif
	if (mpu_port[dev] < 0)
		mpu_port[dev] = SNDRV_AUTO_PORT;
	if (fm_port[dev] < 0)
		fm_port[dev] = SNDRV_AUTO_PORT;
	if (sb_port[dev] < 0)
		sb_port[dev] = SNDRV_AUTO_PORT;
	if (sb_port[dev] != SNDRV_AUTO_PORT)
		if ((acard->res_sb_port = request_region(sb_port[dev], 16, IDENT " SB")) == NULL) {
			printk(KERN_ERR IDENT ": unable to register SB port at 0x%lx\n", sb_port[dev]);
			snd_card_free(card);
			return -ENOMEM;
		}

#ifdef CS4232
	if ((err = snd_cs4231_create(card,
				     port[dev],
				     cport[dev],
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

#else /* CS4236 */
	if ((err = snd_cs4236_create(card,
				     port[dev],
				     cport[dev],
				     irq[dev],
				     dma1[dev],
				     dma2[dev],
				     CS4231_HW_DETECT,
				     0,
				     &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4236_pcm(chip, 0, &pcm)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4236_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
#endif

	if ((err = snd_cs4231_timer(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}

	if (fm_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_opl3_create(card,
				    fm_port[dev], fm_port[dev] + 2,
				    OPL3_HW_OPL3_CS, 0, &opl3) < 0) {
			printk(KERN_ERR IDENT ": OPL3 not detected\n");
		} else {
			if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
				snd_card_free(card);
				return err;
			}
		}
	}

	if (mpu_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_CS4232,
					mpu_port[dev], 0,
					mpu_irq[dev],
					mpu_irq[dev] >= 0 ? SA_INTERRUPT : 0, NULL) < 0)
			printk(KERN_ERR IDENT ": MPU401 not detected\n");
	}
	strcpy(card->driver, pcm->name);
	strcpy(card->shortname, pcm->name);
	sprintf(card->longname, "%s at 0x%lx, irq %i, dma %i",
		pcm->name,
		chip->port,
		irq[dev],
		dma1[dev]);
	if (dma1[dev] >= 0)
		sprintf(card->longname + strlen(card->longname), "&%d", dma2[dev]);
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_cs4236_cards[dev] = card;
	return 0;
}

#ifdef __ISAPNP__
static int __init snd_cs4236_isapnp_detect(struct isapnp_card *card,
                                           const struct isapnp_card_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		snd_cs4236_isapnp_cards[dev] = card;
		snd_cs4236_isapnp_id[dev] = id;
		res = snd_card_cs4236_probe(dev);
		if (res < 0)
			return res;
		dev++;
		return 0;
	}
	return -ENODEV;
}
#endif /* __ISAPNP__ */

static int __init alsa_card_cs423x_init(void)
{
	int dev, cards = 0;

	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
#ifdef __ISAPNP__
		if (isapnp[dev])
			continue;
#endif
		if (snd_card_cs4236_probe(dev) >= 0)
			cards++;
	}
#ifdef __ISAPNP__
	cards += isapnp_probe_cards(snd_card_pnpids, snd_cs4236_isapnp_detect);
#endif
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR IDENT " soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_cs423x_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_cs4236_cards[idx]);
}

module_init(alsa_card_cs423x_init)
module_exit(alsa_card_cs423x_exit)

#ifndef MODULE

/* format is: snd-cs4232=enable,index,id,isapnp,port,
			 cport,mpu_port,fm_port,sb_port,
			 irq,mpu_irq,dma1,dma2 */
/* format is: snd-cs4236=enable,index,id,isapnp,port,
			 cport,mpu_port,fm_port,sb_port,
			 irq,mpu_irq,dma1,dma2 */

static int __init alsa_card_cs423x_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;
	int __attribute__ ((__unused__)) pnp = INT_MAX;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,&pnp) == 2 &&
	       get_option(&str,(int *)&port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&cport[nr_dev]) == 2 &&
	       get_option(&str,(int *)&mpu_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&fm_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&sb_port[nr_dev]) == 2 &&
	       get_option(&str,&irq[nr_dev]) == 2 &&
	       get_option(&str,&mpu_irq[nr_dev]) == 2 &&
	       get_option(&str,&dma1[nr_dev]) == 2 &&
	       get_option(&str,&dma2[nr_dev]) == 2);
#ifdef __ISAPNP__
	if (pnp != INT_MAX)
		isapnp[nr_dev] = pnp;
#endif
	nr_dev++;
	return 1;
}

#ifdef CS4232
__setup("snd-cs4232=", alsa_card_cs423x_setup);
#else /* CS4236 */
__setup("snd-cs4236=", alsa_card_cs423x_setup);
#endif

#endif /* ifndef MODULE */
