/*
 *  Driver for SoundBlaster 16/AWE32/AWE64 soundcards
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
#include <linux/slab.h>
#ifndef LINUX_ISAPNP_H
#include <linux/isapnp.h>
#define isapnp_card pci_bus
#define isapnp_dev pci_dev
#endif
#include <sound/core.h>
#include <sound/sb.h>
#include <sound/sb16_csp.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/emu8000.h>
#include <sound/seq_device.h>
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#define SNDRV_GET_ID
#include <sound/initval.h>

#define chip_t sb_t

#ifdef SNDRV_SBAWE
#define PFX "sbawe: "
#else
#define PFX "sb16: "
#endif

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
#ifndef SNDRV_SBAWE
MODULE_DESCRIPTION("Sound Blaster 16");
MODULE_DEVICES("{{Creative Labs,SB 16},"
		"{Creative Labs,SB Vibra16S},"
		"{Creative Labs,SB Vibra16C},"
		"{Creative Labs,SB Vibra16CL},"
		"{Creative Labs,SB Vibra16X}}");
#else
MODULE_DESCRIPTION("Sound Blaster AWE");
MODULE_DEVICES("{{Creative Labs,SB AWE 32},"
		"{Creative Labs,SB AWE 64},"
		"{Creative Labs,SB AWE 64 Gold}}");
#endif

#if 0
#define SNDRV_DEBUG_IRQ
#endif

#if defined(SNDRV_SBAWE) && (defined(CONFIG_SND_SEQUENCER) || defined(CONFIG_SND_SEQUENCER_MODULE))
#define SNDRV_SBAWE_EMU8000
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
#ifdef __ISAPNP__
static int isapnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#endif
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260,0x280 */
static long mpu_port[SNDRV_CARDS] = {0x330, 0x300,[2 ... (SNDRV_CARDS - 1)] = -1};
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
#ifdef SNDRV_SBAWE_EMU8000
static long awe_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
#endif
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */
static int dma16[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 5,6,7 */
static int mic_agc[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#ifdef CONFIG_SND_SB16_CSP
static int csp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
#endif
#ifdef SNDRV_SBAWE_EMU8000
static int seq_ports[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4};
#endif

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for SoundBlaster 16 soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for SoundBlaster 16 soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable SoundBlaster 16 soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
#ifdef __ISAPNP__
MODULE_PARM(isapnp, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(isapnp, "ISA PnP detection for specified soundcard.");
MODULE_PARM_SYNTAX(isapnp, SNDRV_ISAPNP_DESC);
#endif
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for SB16 driver.");
MODULE_PARM_SYNTAX(port, SNDRV_ENABLED ",allows:{{0x220},{0x240},{0x260},{0x280}},dialog:list");
MODULE_PARM(mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for SB16 driver.");
MODULE_PARM_SYNTAX(mpu_port, SNDRV_ENABLED ",allows:{{0x330},{0x300}},dialog:list");
MODULE_PARM(fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(fm_port, "FM port # for SB16 PnP driver.");
MODULE_PARM_SYNTAX(fm_port, SNDRV_ENABLED ",allows:{{0x388},{0x38c},{0x390},{0x394}},dialog:list");
#ifdef SNDRV_SBAWE_EMU8000
MODULE_PARM(awe_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(awe_port, "AWE port # for SB16 PnP driver.");
MODULE_PARM_SYNTAX(awe_port, SNDRV_ENABLED ",allows:{{0x620},{0x640},{0x660},{0x680}},dialog:list");
#endif
MODULE_PARM(irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(irq, "IRQ # for SB16 driver.");
MODULE_PARM_SYNTAX(irq, SNDRV_IRQ_DESC);
MODULE_PARM(dma8, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma8, "8-bit DMA # for SB16 driver.");
MODULE_PARM_SYNTAX(dma8, SNDRV_DMA8_DESC);
MODULE_PARM(dma16, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma16, "16-bit DMA # for SB16 driver.");
MODULE_PARM_SYNTAX(dma16, SNDRV_DMA16_DESC);
MODULE_PARM(mic_agc, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(mic_agc, "Mic Auto-Gain-Control switch.");
MODULE_PARM_SYNTAX(mic_agc, SNDRV_ENABLED "," SNDRV_BOOLEAN_TRUE_DESC);
#ifdef CONFIG_SND_SB16_CSP
MODULE_PARM(csp, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(csp, "ASP/CSP chip support.");
MODULE_PARM_SYNTAX(csp, SNDRV_ENABLED "," SNDRV_ENABLE_DESC);
#endif
#ifdef SNDRV_SBAWE_EMU8000
MODULE_PARM(seq_ports, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(seq_ports, "Number of sequencer ports for WaveTable synth.");
MODULE_PARM_SYNTAX(seq_ports, SNDRV_ENABLED ",allows:{{0,8}},skill:advanced");
#endif

struct snd_sb16 {
	struct resource *fm_res;	/* used to block FM i/o region for legacy cards */
#ifdef __ISAPNP__
	struct isapnp_dev *dev;
#ifdef SNDRV_SBAWE_EMU8000
	struct isapnp_dev *devwt;
#endif
#endif
};

static snd_card_t *snd_sb16_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef __ISAPNP__

static struct isapnp_card *snd_sb16_isapnp_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;
static const struct isapnp_card_id *snd_sb16_isapnp_id[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#define ISAPNP_SB16(_va, _vb, _vc, _device, _audio) \
	{ \
		ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
		.devs = { ISAPNP_DEVICE_ID(_va, _vb, _vc, _audio), } \
	}
#define ISAPNP_SBAWE(_va, _vb, _vc, _device, _audio, _awe) \
	{ \
		ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
		.devs = { ISAPNP_DEVICE_ID(_va, _vb, _vc, _audio), \
			 ISAPNP_DEVICE_ID(_va, _vb, _vc, _awe), } \
	}

static struct isapnp_card_id snd_sb16_pnpids[] __devinitdata = {
#ifndef SNDRV_SBAWE
	/* Sound Blaster 16 PnP */
	ISAPNP_SB16('C','T','L',0x0024,0x0031),
	/* Sound Blaster 16 PnP */
	ISAPNP_SB16('C','T','L',0x0025,0x0031),
	/* Sound Blaster 16 PnP */
	ISAPNP_SB16('C','T','L',0x0026,0x0031),
	/* Sound Blaster 16 PnP */
	ISAPNP_SB16('C','T','L',0x0027,0x0031),
	/* Sound Blaster 16 PnP */
	ISAPNP_SB16('C','T','L',0x0028,0x0031),
	/* Sound Blaster 16 PnP */
	ISAPNP_SB16('C','T','L',0x0029,0x0031),
	/* Sound Blaster 16 PnP */
	ISAPNP_SB16('C','T','L',0x002a,0x0031),
	/* Sound Blaster 16 PnP */
	/* Note: This card has also a CTL0051:StereoEnhance device!!! */
	ISAPNP_SB16('C','T','L',0x002b,0x0031),
	/* Sound Blaster 16 PnP */
	ISAPNP_SB16('C','T','L',0x002c,0x0031),	
	/* Sound Blaster Vibra16S */
	ISAPNP_SB16('C','T','L',0x0051,0x0001),
	/* Sound Blaster Vibra16C */
	ISAPNP_SB16('C','T','L',0x0070,0x0001),
	/* Sound Blaster Vibra16CL - added by ctm@ardi.com */
	ISAPNP_SB16('C','T','L',0x0080,0x0041),
	/* Sound Blaster 16 'value' PnP. It says model ct4130 on the pcb, */
	/* but ct4131 on a sticker on the board.. */
	ISAPNP_SB16('C','T','L',0x0086,0x0041),
	/* Sound Blaster Vibra16X */
	ISAPNP_SB16('C','T','L',0x00f0,0x0043),
#else  /* SNDRV_SBAWE defined */
	/* Sound Blaster AWE 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x0035,0x0031,0x0021),
	/* Sound Blaster AWE 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x0039,0x0031,0x0021),
	/* Sound Blaster AWE 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x0042,0x0031,0x0021),
	/* Sound Blaster AWE 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x0043,0x0031,0x0021),
	/* Sound Blaster AWE 32 PnP */
	/* Note: This card has also a CTL0051:StereoEnhance device!!! */
	ISAPNP_SBAWE('C','T','L',0x0044,0x0031,0x0021),
	/* Sound Blaster AWE 32 PnP */
	/* Note: This card has also a CTL0051:StereoEnhance device!!! */
	ISAPNP_SBAWE('C','T','L',0x0045,0x0031,0x0021),
	/* Sound Blaster AWE 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x0046,0x0031,0x0021),
	/* Sound Blaster AWE 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x0047,0x0031,0x0021),
	/* Sound Blaster AWE 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x0048,0x0031,0x0021),
	/* Sound Blaster AWE 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x0054,0x0031,0x0021),
	/* Sound Blaster AWE 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x009a,0x0041,0x0021),
	/* Sound Blaster AWE 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x009c,0x0041,0x0021),
	/* Sound Blaster 32 PnP */
	ISAPNP_SBAWE('C','T','L',0x009f,0x0041,0x0021),
	/* Sound Blaster AWE 64 PnP */
	ISAPNP_SBAWE('C','T','L',0x009d,0x0042,0x0022),
	/* Sound Blaster AWE 64 PnP Gold */
	ISAPNP_SBAWE('C','T','L',0x009e,0x0044,0x0023),
	/* Sound Blaster AWE 64 PnP Gold */
	ISAPNP_SBAWE('C','T','L',0x00b2,0x0044,0x0023),
	/* Sound Blaster AWE 64 PnP */
	ISAPNP_SBAWE('C','T','L',0x00c1,0x0042,0x0022),
	/* Sound Blaster AWE 64 PnP */
	ISAPNP_SBAWE('C','T','L',0x00c3,0x0045,0x0022),
	/* Sound Blaster AWE 64 PnP */
	ISAPNP_SBAWE('C','T','L',0x00c5,0x0045,0x0022),
	/* Sound Blaster AWE 64 PnP */
	ISAPNP_SBAWE('C','T','L',0x00c7,0x0045,0x0022),
	/* Sound Blaster AWE 64 PnP */
	ISAPNP_SBAWE('C','T','L',0x00e4,0x0045,0x0022),
	/* Sound Blaster AWE 64 PnP */
	ISAPNP_SBAWE('C','T','L',0x00e9,0x0045,0x0022),
	/* Sound Blaster 16 PnP (AWE) */
	ISAPNP_SBAWE('C','T','L',0x00ed,0x0041,0x0070),
	/* Generic entries */
	ISAPNP_SBAWE('C','T','L',ISAPNP_ANY_ID,0x0031,0x0021),
	ISAPNP_SBAWE('C','T','L',ISAPNP_ANY_ID,0x0041,0x0021),
	ISAPNP_SBAWE('C','T','L',ISAPNP_ANY_ID,0x0042,0x0022),
	ISAPNP_SBAWE('C','T','L',ISAPNP_ANY_ID,0x0044,0x0023),
	ISAPNP_SBAWE('C','T','L',ISAPNP_ANY_ID,0x0045,0x0022),
#endif /* SNDRV_SBAWE */
	{ ISAPNP_CARD_END, }
};

ISAPNP_CARD_TABLE(snd_sb16_pnpids);

static int __init snd_sb16_isapnp(int dev, struct snd_sb16 *acard)
{
	const struct isapnp_card_id *id = snd_sb16_isapnp_id[dev];
	struct isapnp_card *card = snd_sb16_isapnp_cards[dev];
	struct isapnp_dev *pdev;

	acard->dev = isapnp_find_dev(card, id->devs[0].vendor, id->devs[0].function, NULL);
	if (acard->dev->active) {
		acard->dev = NULL;
		return -EBUSY;
	}
#ifdef SNDRV_SBAWE_EMU8000
	acard->devwt = isapnp_find_dev(card, id->devs[1].vendor, id->devs[1].function, NULL);
	if (acard->devwt->active) {
		acard->dev = acard->devwt = NULL;
		return -EBUSY;
	}
#endif	
	/* Audio initialization */
	pdev = acard->dev;
	if (pdev->prepare(pdev) < 0)
		return -EAGAIN;
	if (port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], port[dev], 16);
	if (mpu_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[1], mpu_port[dev], 2);
	if (fm_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[2], fm_port[dev], 4);
	if (dma8[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[0], dma8[dev], 1);
	if (dma16[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[1], dma16[dev], 1);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], irq[dev], 1);
	if (pdev->activate(pdev) < 0) {
		printk(KERN_ERR PFX "isapnp configure failure (out of resources?)\n");
		return -EBUSY;
	}
	port[dev] = pdev->resource[0].start;
	mpu_port[dev] = pdev->resource[1].start;
	fm_port[dev] = pdev->resource[2].start;
	dma8[dev] = pdev->dma_resource[0].start;
	dma16[dev] = pdev->dma_resource[1].start;
	irq[dev] = pdev->irq_resource[0].start;
	snd_printdd("isapnp SB16: port=0x%lx, mpu port=0x%lx, fm port=0x%lx\n",
			port[dev], mpu_port[dev], fm_port[dev]);
	snd_printdd("isapnp SB16: dma1=%i, dma2=%i, irq=%i\n",
			dma8[dev], dma16[dev], irq[dev]);
#ifdef SNDRV_SBAWE_EMU8000
	/* WaveTable initialization */
	pdev = acard->devwt;
	if (pdev->prepare(pdev)<0) {
		acard->dev->deactivate(acard->dev);
		return -EAGAIN;
	}
	if (awe_port[dev] != SNDRV_AUTO_PORT) {
		isapnp_resource_change(&pdev->resource[0], awe_port[dev], 4);
		isapnp_resource_change(&pdev->resource[1], awe_port[dev] + 0x400, 4);
		isapnp_resource_change(&pdev->resource[2], awe_port[dev] + 0x800, 4);
	}
	if (pdev->activate(pdev)<0) {
		printk(KERN_ERR PFX "WaveTable isapnp configure failure (out of resources?)\n");
		acard->dev->deactivate(acard->dev);		
		return -EBUSY;
	}
	awe_port[dev] = pdev->resource[0].start;
	snd_printdd("isapnp SB16: wavetable port=0x%lx\n", pdev->resource[0].start);
#endif
	return 0;
}

static void snd_sb16_deactivate(struct snd_sb16 *acard)
{
	if (acard->dev) {
		acard->dev->deactivate(acard->dev);
		acard->dev = NULL;
	}
#ifdef SNDRV_SBAWE_EMU8000
	if (acard->devwt) {
		acard->devwt->deactivate(acard->devwt);
		acard->devwt = NULL;
	}
#endif
}

#endif /* __ISAPNP__ */

static void snd_sb16_free(snd_card_t *card)
{
	struct snd_sb16 *acard = (struct snd_sb16 *)card->private_data;

	if (acard == NULL)
		return;
	if (acard->fm_res) {
		release_resource(acard->fm_res);
		kfree_nocheck(acard->fm_res);
	}
#ifdef __ISAPNP__
	snd_sb16_deactivate(acard);
#endif
}

static int __init snd_sb16_probe(int dev)
{
	static int possible_irqs[] = {5, 9, 10, 7, -1};
	static int possible_dmas8[] = {1, 3, 0, -1};
	static int possible_dmas16[] = {5, 6, 7, -1};
	int xirq, xdma8, xdma16;
	sb_t *chip;
	snd_card_t *card;
	struct snd_sb16 *acard;
	opl3_t *opl3;
	snd_hwdep_t *synth = NULL;
#ifdef CONFIG_SND_SB16_CSP
	snd_hwdep_t *xcsp = NULL;
#endif
	unsigned long flags;
	int err;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(struct snd_sb16));
	if (card == NULL)
		return -ENOMEM;
	acard = (struct snd_sb16 *) card->private_data;
	card->private_free = snd_sb16_free;
#ifdef __ISAPNP__
	if (isapnp[dev] && snd_sb16_isapnp(dev, acard) < 0) {
		snd_card_free(card);
		return -EBUSY;
	}
#endif

	xirq = irq[dev];
	xdma8 = dma8[dev];
	xdma16 = dma16[dev];
#ifdef __ISAPNP__
	if (!isapnp[dev]) {
#endif
	if (xirq == SNDRV_AUTO_IRQ) {
		if ((xirq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_card_free(card);
			printk(KERN_ERR PFX "unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	if (xdma8 == SNDRV_AUTO_DMA) {
		if ((xdma8 = snd_legacy_find_free_dma(possible_dmas8)) < 0) {
			snd_card_free(card);
			printk(KERN_ERR PFX "unable to find a free 8-bit DMA\n");
			return -EBUSY;
		}
	}
	if (xdma16 == SNDRV_AUTO_DMA) {
		if ((xdma16 = snd_legacy_find_free_dma(possible_dmas16)) < 0) {
			snd_card_free(card);
			printk(KERN_ERR PFX "unable to find a free 16-bit DMA\n");
			return -EBUSY;
		}
	}
	/* non-PnP FM port address is hardwired with base port address */
	fm_port[dev] = port[dev];
	/* block the 0x388 port to avoid PnP conflicts */
	acard->fm_res = request_region(0x388, 4, "SoundBlaster FM");
#ifdef SNDRV_SBAWE_EMU8000
	/* non-PnP AWE port address is hardwired with base port address */
	awe_port[dev] = port[dev] + 0x400;
#endif
#ifdef __ISAPNP__
	}
#endif

	if ((err = snd_sbdsp_create(card,
				    port[dev],
				    xirq,
				    snd_sb16dsp_interrupt,
				    xdma8,
				    xdma16,
				    SB_HW_AUTO,
				    &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (chip->hardware != SB_HW_16) {
		snd_card_free(card);
		snd_printdd("SB 16 chip was not detected at 0x%lx\n", port[dev]);
		return -ENODEV;
	}
	chip->mpu_port = mpu_port[dev];
#ifdef __ISAPNP__
	if (!isapnp[dev] && (err = snd_sb16dsp_configure(chip)) < 0) {
#else
	if ((err = snd_sb16dsp_configure(chip)) < 0) {
#endif
		snd_card_free(card);
		return -ENXIO;
	}
	if ((err = snd_sb16dsp_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return -ENXIO;
	}

	if (chip->mpu_port) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_SB,
					       chip->mpu_port, 0,
					       xirq, 0, &chip->rmidi)) < 0) {
			snd_card_free(card);
			return -ENXIO;
		}
		chip->rmidi_callback = snd_mpu401_uart_interrupt;
	}

	if (fm_port[dev] > 0) {
		if (snd_opl3_create(card, fm_port[dev], fm_port[dev] + 2,
				    OPL3_HW_OPL3, fm_port[dev] == port[dev],
				    &opl3) < 0) {
			printk(KERN_ERR PFX "no OPL device at 0x%lx-0x%lx\n",
				   fm_port[dev], fm_port[dev] + 2);
		} else {
#ifdef SNDRV_SBAWE_EMU8000
			int seqdev = awe_port[dev] > 0 ? 2 : 1;
#else
			int seqdev = 1;
#endif
			if ((err = snd_opl3_hwdep_new(opl3, 0, seqdev, &synth)) < 0) {
				snd_card_free(card);
				return -ENXIO;
			}
		}
	}

	if ((err = snd_sbmixer_new(chip)) < 0) {
		snd_card_free(card);
		return -ENXIO;
	}

#ifdef CONFIG_SND_SB16_CSP
	/* CSP chip on SB16ASP/AWE32 */
	if ((chip->hardware == SB_HW_16) && csp[dev]) {
		snd_sb_csp_new(chip, synth != NULL ? 1 : 0, &xcsp);
		if (xcsp) {
			chip->csp = xcsp->private_data;
			chip->hardware = SB_HW_16CSP;
		} else {
			printk(KERN_INFO PFX "warning - CSP chip not detected on soundcard #%i\n", dev + 1);
		}
	}
#endif
#ifdef SNDRV_SBAWE_EMU8000
	if (awe_port[dev] > 0) {
		if (snd_emu8000_new(card, 1, awe_port[dev],
				    seq_ports[dev], NULL) < 0) {
			printk(KERN_ERR PFX "fatal error - EMU-8000 synthesizer not detected at 0x%lx\n", awe_port[dev]);
			snd_card_free(card);
			return -ENXIO;
		}
	}
#endif

	/* setup Mic AGC */
	spin_lock_irqsave(&chip->mixer_lock, flags);
	snd_sbmixer_write(chip, SB_DSP4_MIC_AGC,
		(snd_sbmixer_read(chip, SB_DSP4_MIC_AGC) & 0x01) |
		(mic_agc[dev] ? 0x00 : 0x01));
	spin_unlock_irqrestore(&chip->mixer_lock, flags);

	strcpy(card->driver, 
#ifdef SNDRV_SBAWE_EMU8000
			awe_port[dev] > 0 ? "SB AWE" :
#endif
			"SB16");
	strcpy(card->shortname, chip->name);
	sprintf(card->longname, "%s at 0x%lx, irq %i, dma ",
		chip->name,
		chip->port,
		xirq);
	if (xdma8 >= 0)
		sprintf(card->longname + strlen(card->longname), "%d", xdma8);
	if (xdma16 >= 0)
		sprintf(card->longname + strlen(card->longname), "%s%d",
			xdma8 >= 0 ? "&" : "", xdma16);
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_sb16_cards[dev] = card;
	return 0;
}

static int __init snd_sb16_probe_legacy_port(unsigned long xport)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || port[dev] != SNDRV_AUTO_PORT)
			continue;
#ifdef __ISAPNP__
		if (isapnp[dev])
			continue;
#endif
		port[dev] = xport;
		res = snd_sb16_probe(dev);
		if (res < 0)
			port[dev] = SNDRV_AUTO_PORT;
		return res;
	}
	return -ENODEV;
}

#ifdef __ISAPNP__

static int __init snd_sb16_isapnp_detect(struct isapnp_card *card,
					 const struct isapnp_card_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || !isapnp[dev])
			continue;
		snd_sb16_isapnp_cards[dev] = card;
		snd_sb16_isapnp_id[dev] = id;
		res = snd_sb16_probe(dev);
		if (res < 0)
			return res;
		dev++;
		return 0;
	}

	return -ENODEV;
}

#endif /* __ISAPNP__ */

static int __init alsa_card_sb16_init(void)
{
	int dev, cards = 0;
	static unsigned long possible_ports[] = {0x220, 0x240, 0x260, 0x280, -1};

	/* legacy non-auto cards at first */
	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || port[dev] == SNDRV_AUTO_PORT)
			continue;
#ifdef __ISAPNP__
		if (isapnp[dev])
			continue;
#endif
		if (!snd_sb16_probe(dev)) {
			cards++;
			continue;
		}
#ifdef MODULE
		printk(KERN_ERR "Sound Blaster 16+ soundcard #%i not found at 0x%lx or device busy\n", dev, port[dev]);
#endif			
	}
	/* legacy auto configured cards */
	cards += snd_legacy_auto_probe(possible_ports, snd_sb16_probe_legacy_port);
#ifdef __ISAPNP__
	/* ISA PnP cards at last */
	cards += isapnp_probe_cards(snd_sb16_pnpids, snd_sb16_isapnp_detect);
#endif

	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "Sound Blaster 16 soundcard not found or device busy\n");
#ifdef SNDRV_SBAWE_EMU8000
		printk(KERN_ERR "In case, if you have non-AWE card, try snd-sb16 module\n");
#else
		printk(KERN_ERR "In case, if you have AWE card, try snd-sbawe module\n");
#endif
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_sb16_exit(void)
{
	int dev;

	for (dev = 0; dev < SNDRV_CARDS; dev++)
		snd_card_free(snd_sb16_cards[dev]);
}

module_init(alsa_card_sb16_init)
module_exit(alsa_card_sb16_exit)

#ifndef MODULE

/* format is: snd-sb16=enable,index,id,isapnp,
		       port,mpu_port,fm_port,
		       irq,dma8,dma16,
		       mic_agc,csp,
		       [awe_port,seq_ports] */

static int __init alsa_card_sb16_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;
	int __attribute__ ((__unused__)) pnp = INT_MAX;
	int __attribute__ ((__unused__)) xcsp = INT_MAX;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,&pnp) == 2 &&
	       get_option(&str,(int *)&port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&mpu_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&fm_port[nr_dev]) == 2 &&
	       get_option(&str,&irq[nr_dev]) == 2 &&
	       get_option(&str,&dma8[nr_dev]) == 2 &&
	       get_option(&str,&dma16[nr_dev]) == 2 &&
	       get_option(&str,&mic_agc[nr_dev]) == 2
#ifdef CONFIG_SND_SB16_CSP
	       &&
	       get_option(&str,&xcsp) == 2
#endif
#ifdef SNDRV_SBAWE_EMU8000
	       &&
	       get_option(&str,(int *)&awe_port[nr_dev]) == 2 &&
	       get_option(&str,&seq_ports[nr_dev]) == 2
#endif
	       );
#ifdef __ISAPNP__
	if (pnp != INT_MAX)
		isapnp[nr_dev] = pnp;
#endif
#ifdef CONFIG_SND_SB16_CSP
	if (xcsp != INT_MAX)
		csp[nr_dev] = xcsp;
#endif
	nr_dev++;
	return 1;
}

#ifndef SNDRV_SBAWE_EMU8000
__setup("snd-sb16=", alsa_card_sb16_setup);
#else
__setup("snd-sbawe=", alsa_card_sb16_setup);
#endif

#endif /* ifndef MODULE */
