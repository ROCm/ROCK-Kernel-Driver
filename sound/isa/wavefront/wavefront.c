/*
 *  ALSA card-level driver for Turtle Beach Wavefront cards 
 *                                              (Maui,Tropez,Tropez+)
 *
 *  Copyright (c) 1997-1999 by Paul Barton-Davis <pbd@op.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
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
#define SNDRV_GET_ID
#include <sound/initval.h>
#include <sound/opl3.h>
#include <sound/snd_wavefront.h>

#define chip_t cs4231_t

MODULE_AUTHOR("Paul Barton-Davis <pbd@op.net>");
MODULE_DESCRIPTION("Turtle Beach Wavefront");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Turtle Beach,Maui/Tropez/Tropez+}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	    /* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	    /* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	    /* Enable this card */
static int snd_isapnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
static long snd_cs4232_pcm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int snd_cs4232_pcm_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ; /* 5,7,9,11,12,15 */
static long snd_cs4232_mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT; /* PnP setup */
static int snd_cs4232_mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ; /* 9,11,12,15 */
static long snd_ics2115_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT; /* PnP setup */
static int snd_ics2115_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;    /* 2,9,11,12,15 */
static long snd_fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	    /* PnP setup */
static int snd_dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	    /* 0,1,3,5,6,7 */
static int snd_dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	    /* 0,1,3,5,6,7 */
static int snd_use_cs4232_midi[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0}; 

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for WaveFront soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for WaveFront soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable WaveFront soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
#ifdef __ISAPNP__
MODULE_PARM(snd_isapnp, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_isapnp, "ISA PnP detection for WaveFront soundcards.");
MODULE_PARM_SYNTAX(snd_isapnp, SNDRV_ISAPNP_DESC);
#endif
MODULE_PARM(snd_cs4232_pcm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_cs4232_pcm_port, "Port # for CS4232 PCM interface.");
MODULE_PARM_SYNTAX(snd_cs4232_pcm_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_cs4232_pcm_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_cs4232_pcm_irq, "IRQ # for CS4232 PCM interface.");
MODULE_PARM_SYNTAX(snd_cs4232_pcm_irq, SNDRV_ENABLED ",allows:{{5},{7},{9},{11},{12},{15}},dialog:list");
MODULE_PARM(snd_dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma1, "DMA1 # for CS4232 PCM interface.");
MODULE_PARM_SYNTAX(snd_dma1, SNDRV_DMA_DESC);
MODULE_PARM(snd_dma2, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma2, "DMA2 # for CS4232 PCM interface.");
MODULE_PARM_SYNTAX(snd_dma2, SNDRV_DMA_DESC);
MODULE_PARM(snd_cs4232_mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_cs4232_mpu_port, "port # for CS4232 MPU-401 interface.");
MODULE_PARM_SYNTAX(snd_cs4232_mpu_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_cs4232_mpu_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_cs4232_mpu_irq, "IRQ # for CS4232 MPU-401 interface.");
MODULE_PARM_SYNTAX(snd_cs4232_mpu_irq, SNDRV_ENABLED ",allows:{{9},{11},{12},{15}},dialog:list");
MODULE_PARM(snd_ics2115_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_ics2115_irq, "IRQ # for ICS2115.");
MODULE_PARM_SYNTAX(snd_ics2115_irq, SNDRV_ENABLED ",allows:{{9},{11},{12},{15}},dialog:list");
MODULE_PARM(snd_ics2115_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_ics2115_port, "Port # for ICS2115.");
MODULE_PARM_SYNTAX(snd_ics2115_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_fm_port, "FM port #.");
MODULE_PARM_SYNTAX(snd_fm_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_use_cs4232_midi, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_use_cs4232_midi, "Use CS4232 MPU-401 interface (inaccessibly located inside your computer)");
MODULE_PARM_SYNTAX(snd_use_cs4232_midi, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);

static snd_card_t *snd_wavefront_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef __ISAPNP__

static struct isapnp_card *snd_wavefront_isapnp_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;
static const struct isapnp_card_id *snd_wavefront_isapnp_id[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

static struct isapnp_card_id snd_wavefront_pnpids[] __devinitdata = {
	{
		ISAPNP_CARD_ID('C','S','C',0x7532),	/* Tropez */
		.devs = { ISAPNP_DEVICE_ID('C','S','C',0x0000),	/* WSS */
			ISAPNP_DEVICE_ID('C','S','C',0x0010),	/* CTRL */
			ISAPNP_DEVICE_ID('P','n','P',0xb006),	/* MPU */
			ISAPNP_DEVICE_ID('C','S','C',000004), }, /* SYNTH */
	},
	{
		ISAPNP_CARD_ID('C','S','C',0x7632),	/* Tropez+ */
		.devs = { ISAPNP_DEVICE_ID('C','S','C',0x0000),	/* WSS */
			ISAPNP_DEVICE_ID('C','S','C',0x0010),	/* CTRL */
			ISAPNP_DEVICE_ID('P','n','P',0xb006),	/* MPU */
			ISAPNP_DEVICE_ID('C','S','C',000004), }, /* SYNTH */
	},
	{ ISAPNP_CARD_END, }
};

ISAPNP_CARD_TABLE(snd_wavefront_pnpids);

static int __init
snd_wavefront_isapnp (int dev, snd_wavefront_card_t *acard)
{
	const struct isapnp_card_id *id = snd_wavefront_isapnp_id[dev];
	struct isapnp_card *card = snd_wavefront_isapnp_cards[dev];
	struct isapnp_dev *pdev;
	int tmp;

	/* Check for each logical device. */

	/* CS4232 chip (aka "windows sound system") is logical device 0 */

	acard->wss = isapnp_find_dev(card, id->devs[0].vendor, id->devs[0].function, NULL);
	if (acard->wss->active) {
		acard->wss = NULL;
		return -EBUSY;
	}

	/* there is a game port at logical device 1, but we ignore it completely */

	/* the control interface is logical device 2, but we ignore it
	   completely. in fact, nobody even seems to know what it
	   does.
	*/

	/* Only configure the CS4232 MIDI interface if its been
	   specifically requested. It is logical device 3.
	*/

	if (snd_use_cs4232_midi[dev]) {
		acard->mpu = isapnp_find_dev(card, id->devs[2].vendor, id->devs[2].function, NULL);
		if (acard->mpu->active) {
			acard->wss = acard->synth = acard->mpu = NULL;
			return -EBUSY;
		}
	}

	/* The ICS2115 synth is logical device 4 */

	acard->synth = isapnp_find_dev(card, id->devs[3].vendor, id->devs[3].function, NULL);
	if (acard->synth->active) {
		acard->wss = acard->synth = NULL;
		return -EBUSY;
	}

	/* PCM/FM initialization */

	pdev = acard->wss;

	if ((tmp = pdev->prepare (pdev)) < 0) {
		if (tmp == -EBUSY) {
			snd_printk ("ISA PnP configuration appears to have "
				    "been done. Restart the isapnp module.\n");
			return 0;
		}
		snd_printk ("isapnp WSS preparation failed\n");
		return -EAGAIN;
	}

	/* An interesting note from the Tropez+ FAQ:

	   Q. [Ports] Why is the base address of the WSS I/O ports off by 4?

	   A. WSS I/O requires a block of 8 I/O addresses ("ports"). Of these, the first
	   4 are used to identify and configure the board. With the advent of PnP,
	   these first 4 addresses have become obsolete, and software applications
	   only use the last 4 addresses to control the codec chip. Therefore, the
	   base address setting "skips past" the 4 unused addresses.

	*/

	if (snd_cs4232_pcm_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], snd_cs4232_pcm_port[dev], 4);
	if (snd_fm_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[1], snd_fm_port[dev], 4);
	if (snd_dma1[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[0], snd_dma1[dev], 1);
	if (snd_dma2[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[1], snd_dma2[dev], 1);
	if (snd_cs4232_pcm_irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], snd_cs4232_pcm_irq[dev], 1);

	if (pdev->activate(pdev)<0) {
		snd_printk ("isapnp WSS activation failed\n");
		return -EBUSY;
	}

	snd_cs4232_pcm_port[dev] = pdev->resource[0].start;
	snd_fm_port[dev] = pdev->resource[1].start;
	snd_dma1[dev] = pdev->dma_resource[0].start;
	snd_dma2[dev] = pdev->dma_resource[1].start;
	snd_cs4232_pcm_irq[dev] = pdev->irq_resource[0].start;

	/* Synth initialization */

	pdev = acard->synth;
	
	if ((tmp = pdev->prepare(pdev))<0) {
		if (tmp == -EBUSY) {
			snd_printk ("ISA PnP configuration appears to have "
				    "been done. Restart the isapnp module.\n");
		}
		acard->wss->deactivate(acard->wss);
		snd_printk ("ICS2115 synth preparation failed\n");
		return -EAGAIN;
	}
	if (snd_ics2115_port[dev] != SNDRV_AUTO_PORT) {
		isapnp_resource_change(&pdev->resource[0], snd_ics2115_port[dev], 16);
	}
		
	if (snd_ics2115_port[dev] != SNDRV_AUTO_IRQ) {
		isapnp_resource_change(&pdev->irq_resource[0], snd_ics2115_irq[dev], 1);
	}

	if (pdev->activate(pdev)<0) {
		snd_printk("isapnp activation for ICS2115 failed\n");
		acard->wss->deactivate(acard->wss);
		return -EBUSY;
	}

	snd_ics2115_port[dev] = pdev->resource[0].start;
	snd_ics2115_irq[dev] = pdev->irq_resource[0].start;

	/* CS4232 MPU initialization. Configure this only if
	   explicitly requested, since its physically inaccessible and
	   consumes another IRQ.
	*/

	if (snd_use_cs4232_midi[dev]) {

		pdev = acard->mpu;

		if (pdev->prepare(pdev)<0) {
			acard->wss->deactivate(acard->wss);
			if (acard->synth)
				acard->synth->deactivate(acard->synth);
			snd_printk ("CS4232 MPU preparation failed\n");
			return -EAGAIN;
		}

		if (snd_cs4232_mpu_port[dev] != SNDRV_AUTO_PORT)
			isapnp_resource_change(&pdev->resource[0], snd_cs4232_mpu_port[dev], 2);
		if (snd_cs4232_mpu_irq[dev] != SNDRV_AUTO_IRQ)
			isapnp_resource_change(&pdev->resource[0], snd_cs4232_mpu_irq[dev], 1);

		if (pdev->activate(pdev)<0) {
			snd_printk("isapnp CS4232 MPU activation failed\n");
			snd_cs4232_mpu_port[dev] = SNDRV_AUTO_PORT;
		} else {
			snd_cs4232_mpu_port[dev] = pdev->resource[0].start;
			snd_cs4232_mpu_irq[dev] = pdev->irq_resource[0].start;
		}

		snd_printk ("CS4232 MPU: port=0x%lx, irq=%i\n", 
			    snd_cs4232_mpu_port[dev], 
			    snd_cs4232_mpu_irq[dev]);
	}

	snd_printdd ("CS4232: pcm port=0x%lx, fm port=0x%lx, dma1=%i, dma2=%i, irq=%i\nICS2115: port=0x%lx, irq=%i\n", 
		    snd_cs4232_pcm_port[dev], 
		    snd_fm_port[dev],
		    snd_dma1[dev], 
		    snd_dma2[dev], 
		    snd_cs4232_pcm_irq[dev],
		    snd_ics2115_port[dev], 
		    snd_ics2115_irq[dev]);
	
	return 0;
}

static void 
snd_wavefront_deactivate (snd_wavefront_card_t *acard)
{
	snd_printk ("deactivating PnP devices\n");
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
	if (acard->synth) {
		acard->synth->deactivate(acard->synth);
		acard->synth = NULL;
	}
}

#endif /* __ISAPNP__ */

static void snd_wavefront_ics2115_interrupt(int irq, 
					    void *dev_id, 
					    struct pt_regs *regs)
{
	snd_wavefront_card_t *acard;

	acard = (snd_wavefront_card_t *) dev_id;

	if (acard == NULL) 
		return;

	if (acard->wavefront.interrupts_are_midi) {
		snd_wavefront_midi_interrupt (acard);
	} else {
		snd_wavefront_internal_interrupt (acard);
	}
}

snd_hwdep_t * __init
snd_wavefront_new_synth (snd_card_t *card,
			 int hw_dev,
			 snd_wavefront_card_t *acard)
{
	snd_hwdep_t *wavefront_synth;

	if (snd_wavefront_detect (acard) < 0) {
		return NULL;
	}

	if (snd_wavefront_start (&acard->wavefront) < 0) {
		return NULL;
	}

	if (snd_hwdep_new(card, "WaveFront", hw_dev, &wavefront_synth) < 0)
		return NULL;
	strcpy (wavefront_synth->name, 
		"WaveFront (ICS2115) wavetable synthesizer");
	wavefront_synth->ops.open = snd_wavefront_synth_open;
	wavefront_synth->ops.release = snd_wavefront_synth_release;
	wavefront_synth->ops.ioctl = snd_wavefront_synth_ioctl;

	return wavefront_synth;
}

snd_hwdep_t * __init
snd_wavefront_new_fx (snd_card_t *card,
		      int hw_dev,
		      snd_wavefront_card_t *acard,
		      unsigned long port)

{
	snd_hwdep_t *fx_processor;

	if (snd_wavefront_fx_start (&acard->wavefront)) {
		snd_printk ("cannot initialize YSS225 FX processor");
		return NULL;
	}

	if (snd_hwdep_new (card, "YSS225", hw_dev, &fx_processor) < 0)
		return NULL;
	sprintf (fx_processor->name, "YSS225 FX Processor at 0x%lx", port);
	fx_processor->ops.open = snd_wavefront_fx_open;
	fx_processor->ops.release = snd_wavefront_fx_release;
	fx_processor->ops.ioctl = snd_wavefront_fx_ioctl;
	
	return fx_processor;
}

static snd_wavefront_mpu_id internal_id = internal_mpu;
static snd_wavefront_mpu_id external_id = external_mpu;

snd_rawmidi_t * __init
snd_wavefront_new_midi (snd_card_t *card,
			int midi_dev,
			snd_wavefront_card_t *acard,
			unsigned long port,
			snd_wavefront_mpu_id mpu)

{
	snd_rawmidi_t *rmidi;
	static int first = 1;

	if (first) {
		first = 0;
		acard->wavefront.midi.base = port;
		if (snd_wavefront_midi_start (acard)) {
			snd_printk ("cannot initialize MIDI interface\n");
			return NULL;
		}
	}

	if (snd_rawmidi_new (card, "WaveFront MIDI", midi_dev, 1, 1, &rmidi) < 0)
		return NULL;

	if (mpu == internal_mpu) {
		strcpy(rmidi->name, "WaveFront MIDI (Internal)");
		rmidi->private_data = &internal_id;
	} else {
		strcpy(rmidi->name, "WaveFront MIDI (External)");
		rmidi->private_data = &external_id;
	}

	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_wavefront_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_wavefront_midi_input);

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
	                     SNDRV_RAWMIDI_INFO_INPUT |
	                     SNDRV_RAWMIDI_INFO_DUPLEX;

	return rmidi;
}

static void
snd_wavefront_free(snd_card_t *card)
{
	snd_wavefront_card_t *acard = (snd_wavefront_card_t *)card->private_data;
	
	if (acard) {
#ifdef __ISAPNP__
		snd_wavefront_deactivate(acard);
#endif
		if (acard->wavefront.res_base != NULL) {
			release_resource(acard->wavefront.res_base);
			kfree_nocheck(acard->wavefront.res_base);
		}
		if (acard->wavefront.irq > 0)
			free_irq(acard->wavefront.irq, (void *)acard);
	}
}

static int __init
snd_wavefront_probe (int dev)
{
	snd_card_t *card;
	snd_wavefront_card_t *acard;
	cs4231_t *chip;
	snd_hwdep_t *wavefront_synth;
	snd_rawmidi_t *ics2115_internal_rmidi = NULL;
	snd_rawmidi_t *ics2115_external_rmidi = NULL;
	snd_hwdep_t *fx_processor;
	int hw_dev = 0, midi_dev = 0, err;

	if (snd_cs4232_mpu_port[dev] < 0)
		snd_cs4232_mpu_port[dev] = SNDRV_AUTO_PORT;
	if (snd_fm_port[dev] < 0)
		snd_fm_port[dev] = SNDRV_AUTO_PORT;
	if (snd_ics2115_port[dev] < 0)
		snd_ics2115_port[dev] = SNDRV_AUTO_PORT;

#ifdef __ISAPNP__
	if (!snd_isapnp[dev]) {
#endif
		if (snd_cs4232_pcm_port[dev] == SNDRV_AUTO_PORT) {
			snd_printk("specify CS4232 port\n");
			return -EINVAL;
		}
		if (snd_ics2115_port[dev] == SNDRV_AUTO_PORT) {
			snd_printk("specify ICS2115 port\n");
			return -ENODEV;
		}
#ifdef __ISAPNP__
	}
#endif
	card = snd_card_new (snd_index[dev], 
			     snd_id[dev],
			     THIS_MODULE,
			     sizeof(snd_wavefront_card_t));

	if (card == NULL) {
		return -ENOMEM;
	}
	acard = (snd_wavefront_card_t *)card->private_data;
	acard->wavefront.irq = -1;
	init_waitqueue_head(&acard->wavefront.interrupt_sleeper);
	spin_lock_init(&acard->wavefront.midi.open);
	spin_lock_init(&acard->wavefront.midi.virtual);
	card->private_free = snd_wavefront_free;

#ifdef __ISAPNP__
	if (snd_isapnp[dev] && snd_wavefront_isapnp (dev, acard) < 0) {
		if (snd_cs4232_pcm_port[dev] == SNDRV_AUTO_PORT) {
			snd_printk ("isapnp detection failed\n");
			snd_card_free (card);
			return -ENODEV;
		}
	}
#endif /* __ISAPNP__ */

	/* --------- PCM --------------- */

	if ((err = snd_cs4231_create (card,
				      snd_cs4232_pcm_port[dev],
				      -1,
				      snd_cs4232_pcm_irq[dev],
				      snd_dma1[dev],
				      snd_dma2[dev],
				      CS4231_HW_DETECT, 0, &chip)) < 0) {
		snd_card_free(card);
		snd_printk ("can't allocate CS4231 device\n");
		return err;
	}

	if ((err = snd_cs4231_pcm (chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_timer (chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}

	/* ---------- OPL3 synth --------- */

	if (snd_fm_port[dev] != SNDRV_AUTO_PORT) {
		opl3_t *opl3;

	        if ((err = snd_opl3_create(card,
					   snd_fm_port[dev],
					   snd_fm_port[dev] + 2,
					   OPL3_HW_OPL3_CS,
					   0, &opl3)) < 0) {
			snd_printk ("can't allocate or detect OPL3 synth\n");
			snd_card_free(card);
			return err;
		}

		if ((err = snd_opl3_hwdep_new(opl3, hw_dev, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
		hw_dev++;
	}

	/* ------- ICS2115 Wavetable synth ------- */

	if ((acard->wavefront.res_base = request_region(snd_ics2115_port[dev], 16, "ICS2115")) == NULL) {
		snd_printk("unable to grab ICS2115 i/o region 0x%lx-0x%lx\n", snd_ics2115_port[dev], snd_ics2115_port[dev] + 16 - 1);
		snd_card_free(card);
		return -EBUSY;
	}
	if (request_irq(snd_ics2115_irq[dev], snd_wavefront_ics2115_interrupt, SA_INTERRUPT, "ICS2115", (void *)acard)) {
		snd_printk("unable to use ICS2115 IRQ %d\n", snd_ics2115_irq[dev]);
		snd_card_free(card);
		return -EBUSY;
	}
	
	acard->wavefront.irq = snd_ics2115_irq[dev];
	acard->wavefront.base = snd_ics2115_port[dev];

	if ((wavefront_synth = snd_wavefront_new_synth (card, hw_dev, acard)) == NULL) {
		snd_printk ("can't create WaveFront synth device\n");
		snd_card_free(card);
		return -ENOMEM;
	}

	strcpy (wavefront_synth->name, "ICS2115 Wavetable MIDI Synthesizer");
	wavefront_synth->iface = SNDRV_HWDEP_IFACE_ICS2115;
	hw_dev++;

	/* --------- Mixer ------------ */

	if ((err = snd_cs4231_mixer(chip)) < 0) {
		snd_printk ("can't allocate mixer device\n");
		snd_card_free(card);
		return err;
	}

	/* -------- CS4232 MPU-401 interface -------- */

	if (snd_cs4232_mpu_port[dev] > 0 && snd_cs4232_mpu_port[dev] != SNDRV_AUTO_PORT) {
		if ((err = snd_mpu401_uart_new(card, midi_dev, MPU401_HW_CS4232,
					       snd_cs4232_mpu_port[dev], 0,
					       snd_cs4232_mpu_irq[dev],
					       SA_INTERRUPT,
					       NULL)) < 0) {
			snd_printk ("can't allocate CS4232 MPU-401 device\n");
			snd_card_free(card);
			return err;
		}
		midi_dev++;
	}

	/* ------ ICS2115 internal MIDI ------------ */

	if (snd_ics2115_port[dev] >= 0 && snd_ics2115_port[dev] != SNDRV_AUTO_PORT) {
		ics2115_internal_rmidi = 
			snd_wavefront_new_midi (card, 
						midi_dev,
						acard,
						snd_ics2115_port[dev],
						internal_mpu);
		if (ics2115_internal_rmidi == NULL) {
			snd_printk ("can't setup ICS2115 internal MIDI device\n");
			snd_card_free(card);
			return -ENOMEM;
		}
		midi_dev++;
	}

	/* ------ ICS2115 external MIDI ------------ */

	if (snd_ics2115_port[dev] >= 0 && snd_ics2115_port[dev] != SNDRV_AUTO_PORT) {
		ics2115_external_rmidi = 
			snd_wavefront_new_midi (card, 
						midi_dev,
						acard,
						snd_ics2115_port[dev],
						external_mpu);
		if (ics2115_external_rmidi == NULL) {
			snd_printk ("can't setup ICS2115 external MIDI device\n");
			snd_card_free(card);
			return -ENOMEM;
		}
		midi_dev++;
	}

	/* FX processor for Tropez+ */

	if (acard->wavefront.has_fx) {
		fx_processor = snd_wavefront_new_fx (card,
						     hw_dev,
						     acard,
						     snd_ics2115_port[dev]);
		if (fx_processor == NULL) {
			snd_printk ("can't setup FX device\n");
			snd_card_free(card);
			return -ENOMEM;
		}

		hw_dev++;

		strcpy(card->driver, "Tropez+");
		strcpy(card->shortname, "Turtle Beach Tropez+");
	} else {
		/* Need a way to distinguish between Maui and Tropez */
		strcpy(card->driver, "WaveFront");
		strcpy(card->shortname, "Turtle Beach WaveFront");
	}

	/* ----- Register the card --------- */

	/* Not safe to include "Turtle Beach" in longname, due to 
	   length restrictions
	*/

	sprintf(card->longname, "%s PCM 0x%lx irq %d dma %d",
		card->driver,
		chip->port,
		snd_cs4232_pcm_irq[dev],
		snd_dma1[dev]);

	if (snd_dma2[dev] >= 0 && snd_dma2[dev] < 8)
		sprintf(card->longname + strlen(card->longname), "&%d", snd_dma2[dev]);

	if (snd_cs4232_mpu_port[dev] != SNDRV_AUTO_PORT) {
		sprintf (card->longname + strlen (card->longname), 
			 " MPU-401 0x%lx irq %d",
			 snd_cs4232_mpu_port[dev],
			 snd_cs4232_mpu_irq[dev]);
	}

	sprintf (card->longname + strlen (card->longname), 
		 " SYNTH 0x%lx irq %d",
		 snd_ics2115_port[dev],
		 snd_ics2115_irq[dev]);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_wavefront_cards[dev] = card;
	return 0;
}	

#ifdef __ISAPNP__

static int __init snd_wavefront_isapnp_detect(struct isapnp_card *card,
                                              const struct isapnp_card_id *id)
{
        static int dev;
        int res;

        for ( ; dev < SNDRV_CARDS; dev++) {
                if (!snd_enable[dev] || !snd_isapnp[dev])
                        continue;
                snd_wavefront_isapnp_cards[dev] = card;
                snd_wavefront_isapnp_id[dev] = id;
                res = snd_wavefront_probe(dev);
                if (res < 0)
                        return res;
                dev++;
                return 0;
        }

        return -ENODEV;
}

#endif /* __ISAPNP__ */

static int __init alsa_card_wavefront_init(void)
{
	int cards = 0;
	int dev;
	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev])
			continue;
#ifdef __ISAPNP__
		if (snd_isapnp[dev])
			continue;
#endif
		if (snd_wavefront_probe(dev) >= 0)
			cards++;
	}
#ifdef __ISAPNP__
	cards += isapnp_probe_cards(snd_wavefront_pnpids, snd_wavefront_isapnp_detect);
#endif
	if (!cards) {
#ifdef MODULE
		printk (KERN_ERR "No WaveFront cards found or devices busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_wavefront_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_wavefront_cards[idx]);
}

module_init(alsa_card_wavefront_init)
module_exit(alsa_card_wavefront_exit)

#ifndef MODULE

/* format is: snd-wavefront=snd_enable,snd_index,snd_id,snd_isapnp,
			    snd_cs4232_pcm_port,snd_cs4232_pcm_irq,
			    snd_cs4232_mpu_port,snd_cs4232_mpu_irq,
			    snd_ics2115_port,snd_ics2115_irq,
			    snd_fm_port,
			    snd_dma1,snd_dma2,
			    snd_use_cs4232_midi */

static int __init alsa_card_wavefront_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,&snd_isapnp[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_cs4232_pcm_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_cs4232_pcm_irq[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_cs4232_mpu_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_cs4232_mpu_irq[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_ics2115_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_ics2115_irq[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_fm_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma1[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma2[nr_dev]) == 2 &&
	       get_option(&str,&snd_use_cs4232_midi[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-wavefront=", alsa_card_wavefront_setup);

#endif /* ifndef MODULE */
