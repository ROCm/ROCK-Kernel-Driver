/*
 **********************************************************************
 *     main.c - Creative EMU10K1 audio driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999     Alan Cox        cleaned up stuff
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 *
 *      Supported devices:
 *      /dev/dsp:     Standard /dev/dsp device, OSS-compatible
 *      /dev/mixer:   Standard /dev/mixer device, OSS-compatible
 *      /dev/midi:    Raw MIDI UART device, mostly OSS-compatible
 *
 *      Revision history:
 *      0.1 beta Initial release
 *      0.2 Lowered initial mixer vol. Improved on stuttering wave playback. Added MIDI UART support.
 *      0.3 Fixed mixer routing bug, added APS, joystick support.
 *      0.4 Added rear-channel, SPDIF support.
 *	0.5 Source cleanup, SMP fixes, multiopen support, 64 bit arch fixes,
 *	    moved bh's to tasklets, moved to the new PCI driver initialization style.
 *	0.6 Make use of pci_alloc_consistent, improve compatibility layer for 2.2 kernels,
 *	    code reorganization and cleanup.
 *      0.7 Support for the Emu-APS. Bug fixes for voice cache setup, mmaped sound + poll().
 *	    Support for setting external TRAM size.
 *	
 **********************************************************************
 */

/* These are only included once per module */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/delay.h>

#include "hwaccess.h"
#include "8010.h"
#include "efxmgr.h"
#include "cardwo.h"
#include "cardwi.h"
#include "cardmo.h"
#include "cardmi.h"
#include "recmgr.h"
#include "ecard.h"

#define DRIVER_VERSION "0.7"

/* FIXME: is this right? */
/* does the card support 32 bit bus master?*/
#define EMU10K1_DMA_MASK                0xffffffff	/* DMA buffer mask for pci_alloc_consist */

#ifndef PCI_VENDOR_ID_CREATIVE
#define PCI_VENDOR_ID_CREATIVE 0x1102
#endif

#ifndef PCI_DEVICE_ID_CREATIVE_EMU10K1
#define PCI_DEVICE_ID_CREATIVE_EMU10K1 0x0002
#endif

#define EMU_APS_SUBID	0x40011102
 
enum {
	EMU10K1 = 0,
};

static char *card_names[] __devinitdata = {
	"EMU10K1",
};

static struct pci_device_id emu10k1_pci_tbl[] = {
	{PCI_VENDOR_ID_CREATIVE, PCI_DEVICE_ID_CREATIVE_EMU10K1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, EMU10K1},
	{0,}
};

MODULE_DEVICE_TABLE(pci, emu10k1_pci_tbl);

/* Global var instantiation */

LIST_HEAD(emu10k1_devs);

extern struct file_operations emu10k1_audio_fops;
extern struct file_operations emu10k1_mixer_fops;
extern struct file_operations emu10k1_midi_fops;

extern void emu10k1_interrupt(int, void *, struct pt_regs *s);
extern int emu10k1_mixer_wrch(struct emu10k1_card *, unsigned int, int);

static void __devinit audio_init(struct emu10k1_card *card)
{
	/* Assign default playback voice parameters */
	/* mono voice */
	card->waveout.send_a[0] = 0x00;
	card->waveout.send_b[0] = 0xff;
	card->waveout.send_c[0] = 0xff;
	card->waveout.send_d[0] = 0x00;
	card->waveout.send_routing[0] = 0xd01c;

	/* stereo voice */
	card->waveout.send_a[1] = 0x00;
        card->waveout.send_b[1] = 0x00;
        card->waveout.send_c[1] = 0xff;
        card->waveout.send_d[1] = 0x00;
        card->waveout.send_routing[1] = 0xd01c;

	card->waveout.send_a[2] = 0x00;
        card->waveout.send_b[2] = 0xff;
        card->waveout.send_c[2] = 0x00;
        card->waveout.send_d[2] = 0x00;
        card->waveout.send_routing[2] = 0xd01c;

	/* Assign default recording parameters */
	if(card->isaps)
		card->wavein.recsrc = WAVERECORD_FX;
	else
		card->wavein.recsrc = WAVERECORD_AC97;

	card->wavein.fxwc = 0x0003;

	return;
}

static void __devinit mixer_init(struct emu10k1_card *card)
{
	int count;
	struct initvol {
		int mixch;
		int vol;
	} initvol[] = {
		{
		SOUND_MIXER_VOLUME, 0x5050}, {
		SOUND_MIXER_OGAIN, 0x3232}, {
		SOUND_MIXER_SPEAKER, 0x3232}, {
		SOUND_MIXER_PHONEIN, 0x3232}, {
		SOUND_MIXER_MIC, 0x0000}, {
		SOUND_MIXER_LINE, 0x0000}, {
		SOUND_MIXER_CD, 0x4b4b}, {
		SOUND_MIXER_LINE1, 0x4b4b}, {
		SOUND_MIXER_LINE3, 0x3232}, {
		SOUND_MIXER_DIGITAL1, 0x6464}, {
		SOUND_MIXER_DIGITAL2, 0x6464}, {
		SOUND_MIXER_PCM, 0x6464}, {
		SOUND_MIXER_RECLEV, 0x0404}, {
		SOUND_MIXER_TREBLE, 0x3232}, {
		SOUND_MIXER_BASS, 0x3232}, {
		SOUND_MIXER_LINE2, 0x4b4b}};

	int initdig[] = { 0, 1, 2, 3, 6, 7, 18, 19, 20, 21, 24, 25, 72, 73, 74, 75, 78, 79,
		94, 95
	};

	for (count = 0; count < sizeof(card->digmix) / sizeof(card->digmix[0]); count++) {
		card->digmix[count] = 0x80000000;
		sblive_writeptr(card, FXGPREGBASE + 0x10 + count, 0, 0);
	}

	card->modcnt = 0;       // Should this be here or in open() ?

	if (!card->isaps) {

		for (count = 0; count < sizeof(initdig) / sizeof(initdig[0]); count++) {
			card->digmix[initdig[count]] = 0x7fffffff;
			sblive_writeptr(card, FXGPREGBASE + 0x10 + initdig[count], 0, 0x7fffffff);
		}

		/* Reset */
		sblive_writeac97(card, AC97_RESET, 0);

#if 0
		/* Check status word */
		{
			u16 reg;

			sblive_readac97(card, AC97_RESET, &reg);
			DPD(2, "RESET 0x%x\n", reg);
			sblive_readac97(card, AC97_MASTERTONE, &reg);
			DPD(2, "MASTER_TONE 0x%x\n", reg);
		}
#endif

		/* Set default recording source to mic in */
		sblive_writeac97(card, AC97_RECORDSELECT, 0);

		/* Set default AC97 "PCM" volume to acceptable max */
		//sblive_writeac97(card, AC97_PCMOUTVOLUME, 0);
		//sblive_writeac97(card, AC97_LINE2, 0);
	}

	/* Set default volumes for all mixer channels */

	for (count = 0; count < sizeof(initvol) / sizeof(initvol[0]); count++) {
		emu10k1_mixer_wrch(card, initvol[count].mixch, initvol[count].vol);
	}

	return;
}

static int __devinit midi_init(struct emu10k1_card *card)
{
	if ((card->mpuout = kmalloc(sizeof(struct emu10k1_mpuout), GFP_KERNEL))
	    == NULL) {
		printk(KERN_WARNING "emu10k1: Unable to allocate emu10k1_mpuout: out of memory\n");
		return -1;
	}

	memset(card->mpuout, 0, sizeof(struct emu10k1_mpuout));

	card->mpuout->intr = 1;
	card->mpuout->status = FLAGS_AVAILABLE;
	card->mpuout->state = CARDMIDIOUT_STATE_DEFAULT;

	tasklet_init(&card->mpuout->tasklet, emu10k1_mpuout_bh, (unsigned long) card);

	spin_lock_init(&card->mpuout->lock);

	if ((card->mpuin = kmalloc(sizeof(struct emu10k1_mpuin), GFP_KERNEL)) == NULL) {
		kfree(card->mpuout);
		printk(KERN_WARNING "emu10k1: Unable to allocate emu10k1_mpuin: out of memory\n");
		return -1;
	}

	memset(card->mpuin, 0, sizeof(struct emu10k1_mpuin));

	card->mpuin->status = FLAGS_AVAILABLE;

	tasklet_init(&card->mpuin->tasklet, emu10k1_mpuin_bh, (unsigned long) card->mpuin);

	spin_lock_init(&card->mpuin->lock);

	/* Reset the MPU port */
	if (emu10k1_mpu_reset(card) < 0) {
		ERROR();
		return -1;
	}

	return 0;
}

static void __devinit voice_init(struct emu10k1_card *card)
{
	int i;

	for (i = 0; i < NUM_G; i++)
		card->voicetable[i] = VOICE_USAGE_FREE;

	return;
}

static void __devinit timer_init(struct emu10k1_card *card)
{
	INIT_LIST_HEAD(&card->timers);
	card->timer_delay = TIMER_STOPPED;
	card->timer_lock = SPIN_LOCK_UNLOCKED;

	return;
}

static void __devinit addxmgr_init(struct emu10k1_card *card)
{
	u32 count;

	for (count = 0; count < MAXPAGES; count++)
		card->emupagetable[count] = 0;

	/* Mark first page as used */
	/* This page is reserved by the driver */
	card->emupagetable[0] = 0x8001;
	card->emupagetable[1] = MAXPAGES - 1;

	return;
}

static void __devinit fx_init(struct emu10k1_card *card)
{
	int i, j, k;
#ifdef TONE_CONTROL
	int l;
#endif	
	u32 pc = 0;

	for (i = 0; i < 512; i++)
		OP(6, 0x40, 0x40, 0x40, 0x40);

	for (i = 0; i < 256; i++)
		sblive_writeptr_tag(card, 0,
				    FXGPREGBASE + i, 0,
				    TANKMEMADDRREGBASE + i, 0,
				    TAGLIST_END);

	pc = 0;

	for (j = 0; j < 2; j++) {

		OP(4, 0x100, 0x40, j, 0x44);
		OP(4, 0x101, 0x40, j + 2, 0x44);

		for (i = 0; i < 6; i++) {
			k = i * 18 + j;
			OP(0, 0x102, 0x40, 0x110 + k, 0x100);
			OP(0, 0x102, 0x102, 0x112 + k, 0x101);
			OP(0, 0x102, 0x102, 0x114 + k, 0x10 + j);
			OP(0, 0x102, 0x102, 0x116 + k, 0x12 + j);
			OP(0, 0x102, 0x102, 0x118 + k, 0x14 + j);
			OP(0, 0x102, 0x102, 0x11a + k, 0x16 + j);
			OP(0, 0x102, 0x102, 0x11c + k, 0x18 + j);
			OP(0, 0x102, 0x102, 0x11e + k, 0x1a + j);
#ifdef TONE_CONTROL
			OP(0, 0x102, 0x102, 0x120 + k, 0x1c + j);

			k = 0x1a0 + i * 8 + j * 4;
			OP(0, 0x40, 0x40, 0x102, 0x180 + j);
			OP(7, k + 1, k, k + 1, 0x184 + j);
			OP(7, k, 0x102, k, 0x182 + j);
			OP(7, k + 3, k + 2, k + 3, 0x188 + j);
			OP(0, k + 2, 0x56, k + 2, 0x186 + j);
			OP(6, k + 2, k + 2, k + 2, 0x40);

			l = 0x1d0 + i * 8 + j * 4;
			OP(0, 0x40, 0x40, k + 2, 0x190 + j);
			OP(7, l + 1, l, l + 1, 0x194 + j);
			OP(7, l, k + 2, l, 0x192 + j);
			OP(7, l + 3, l + 2, l + 3, 0x198 + j);
			OP(0, l + 2, 0x56, l + 2, 0x196 + j);
			OP(4, l + 2, 0x40, l + 2, 0x46);

			if ((i == 0) && !card->isaps)
				OP(4, 0x20 + (i * 2) + j, 0x40, l + 2, 0x50);	/* FIXME: Is this really needed? */
			else
				OP(6, 0x20 + (i * 2) + j, l + 2, 0x40, 0x40);
#else
			OP(0, 0x20 + (i * 2) + j, 0x102, 0x120 + k, 0x1c + j);
#endif
		}
	}
	sblive_writeptr(card, DBG, 0, 0);

	return;
}

static int __devinit hw_init(struct emu10k1_card *card)
{
	int nCh;
	u32 pagecount; /* tmp */

	/* Disable audio and lock cache */
	emu10k1_writefn0(card, HCFG, HCFG_LOCKSOUNDCACHE | HCFG_LOCKTANKCACHE_MASK | HCFG_MUTEBUTTONENABLE);

	/* Reset recording buffers */
	sblive_writeptr_tag(card, 0,
			    MICBS, ADCBS_BUFSIZE_NONE,
			    MICBA, 0,
			    FXBS, ADCBS_BUFSIZE_NONE,
			    FXBA, 0,
			    ADCBS, ADCBS_BUFSIZE_NONE,
			    ADCBA, 0,
			    TAGLIST_END);

	/* Disable channel interrupt */
	emu10k1_writefn0(card, INTE, 0);
	sblive_writeptr_tag(card, 0,
			    CLIEL, 0,
			    CLIEH, 0,
			    SOLEL, 0,
			    SOLEH, 0,
			    TAGLIST_END);

	/* Init envelope engine */
	for (nCh = 0; nCh < NUM_G; nCh++) {
		sblive_writeptr_tag(card, nCh,
				    DCYSUSV, 0,
				    IP, 0,
				    VTFT, 0xffff,
				    CVCF, 0xffff,
				    PTRX, 0,
				    CPF, 0,
				    CCR, 0,

				    PSST, 0,
				    DSL, 0x10,
				    CCCA, 0,
				    Z1, 0,
				    Z2, 0,
				    FXRT, 0xd01c0000,

				    ATKHLDM, 0,
				    DCYSUSM, 0,
				    IFATN, 0xffff,
				    PEFE, 0,
				    FMMOD, 0,
				    TREMFRQ, 24,	/* 1 Hz */
				    FM2FRQ2, 24,	/* 1 Hz */
				    TEMPENV, 0,

				    /*** These are last so OFF prevents writing ***/
				    LFOVAL2, 0,
				    LFOVAL1, 0,
				    ATKHLDV, 0,
				    ENVVOL, 0,
				    ENVVAL, 0,
                                    TAGLIST_END);
	}

	/*
	 ** Init to 0x02109204 :
	 ** Clock accuracy    = 0     (1000ppm)
	 ** Sample Rate       = 2     (48kHz)
	 ** Audio Channel     = 1     (Left of 2)
	 ** Source Number     = 0     (Unspecified)
	 ** Generation Status = 1     (Original for Cat Code 12)
	 ** Cat Code          = 12    (Digital Signal Mixer)
	 ** Mode              = 0     (Mode 0)
	 ** Emphasis          = 0     (None)
	 ** CP                = 1     (Copyright unasserted)
	 ** AN                = 0     (Digital audio)
	 ** P                 = 0     (Consumer)
	 */

	sblive_writeptr_tag(card, 0,

			    /* SPDIF0 */
			    SPCS0, (SPCS_CLKACCY_1000PPM | 0x002000000 |
				    SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS | 0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT),

			    /* SPDIF1 */
			    SPCS1, (SPCS_CLKACCY_1000PPM | 0x002000000 |
				    SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS | 0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT),

			    /* SPDIF2 & SPDIF3 */
			    SPCS2, (SPCS_CLKACCY_1000PPM | 0x002000000 |
				    SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS | 0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT),

			    TAGLIST_END);

	fx_init(card);		/* initialize effects engine */

	card->tankmem.size = 0;

	card->virtualpagetable.size = MAXPAGES * sizeof(u32);

	if ((card->virtualpagetable.addr = pci_alloc_consistent(card->pci_dev, card->virtualpagetable.size, &card->virtualpagetable.dma_handle)) ==
	    NULL) {
		ERROR();
		return -1;
	}

	card->silentpage.size = EMUPAGESIZE;

	if ((card->silentpage.addr = pci_alloc_consistent(card->pci_dev, card->silentpage.size, &card->silentpage.dma_handle)) == NULL) {
		ERROR();
		pci_free_consistent(card->pci_dev, card->virtualpagetable.size, card->virtualpagetable.addr, card->virtualpagetable.dma_handle);
		return -1;
	}

	for (pagecount = 0; pagecount < MAXPAGES; pagecount++)
		((u32 *) card->virtualpagetable.addr)[pagecount] = (card->silentpage.dma_handle * 2) | pagecount;

	/* Init page table & tank memory base register */
	sblive_writeptr_tag(card, 0,
			    PTB, card->virtualpagetable.dma_handle,
			    TCB, 0,
			    TCBS, 0,
			    TAGLIST_END);

	for (nCh = 0; nCh < NUM_G; nCh++) {
		sblive_writeptr_tag(card, nCh,
				    MAPA, MAP_PTI_MASK | (card->silentpage.dma_handle * 2),
				    MAPB, MAP_PTI_MASK | (card->silentpage.dma_handle * 2),
				    TAGLIST_END);
	}

	/* Hokay, now enable the AUD bit */
	/* Enable Audio = 1 */
	/* Mute Disable Audio = 0 */
	/* Lock Tank Memory = 1 */
	/* Lock Sound Memory = 0 */
	/* Auto Mute = 1 */

	sblive_rmwac97(card, AC97_MASTERVOLUME, 0x8000, 0x8000);

	sblive_writeac97(card, AC97_MASTERVOLUME, 0);
	sblive_writeac97(card, AC97_PCMOUTVOLUME, 0);

	if(card->model == 0x20 || card->model == 0xc400 ||
	  (card->model == 0x21 && card->chiprev < 6))
	        emu10k1_writefn0(card, HCFG, HCFG_AUDIOENABLE  | HCFG_LOCKTANKCACHE_MASK | HCFG_AUTOMUTE);
	else
		emu10k1_writefn0(card, HCFG, HCFG_AUDIOENABLE  | HCFG_LOCKTANKCACHE_MASK | HCFG_AUTOMUTE | HCFG_JOYENABLE);

	/* FIXME: TOSLink detection */
	card->has_toslink = 0;

/*	tmp = sblive_readfn0(card, HCFG);
	if (tmp & (HCFG_GPINPUT0 | HCFG_GPINPUT1)) {
		sblive_writefn0(card, HCFG, tmp | 0x800);

		udelay(512);

		if (tmp != (sblive_readfn0(card, HCFG) & ~0x800)) {
			card->has_toslink = 1;
			sblive_writefn0(card, HCFG, tmp);
		}
	}
*/
	return 0;
}

static int __devinit emu10k1_init(struct emu10k1_card *card)
{
	/* Init Card */
	if (hw_init(card) < 0)
		return -1;

	voice_init(card);
	timer_init(card);
	addxmgr_init(card);

	DPD(2, "  hw control register -> 0x%x\n", emu10k1_readfn0(card, HCFG));

	return 0;
}

static void __devexit midi_exit(struct emu10k1_card *card)
{
	tasklet_unlock_wait(&card->mpuout->tasklet);
	kfree(card->mpuout);

	tasklet_unlock_wait(&card->mpuin->tasklet);
	kfree(card->mpuin);

	return;
}

static void __devinit emu10k1_exit(struct emu10k1_card *card)
{
	int ch;

	emu10k1_writefn0(card, INTE, 0);

	/** Shutdown the chip **/
	for (ch = 0; ch < NUM_G; ch++)
		sblive_writeptr(card, DCYSUSV, ch, 0);

	for (ch = 0; ch < NUM_G; ch++) {
		sblive_writeptr_tag(card, ch,
				    VTFT, 0,
				    CVCF, 0,
				    PTRX, 0,
				    CPF, 0,
				    TAGLIST_END);
	}

	/* Disable audio and lock cache */
	emu10k1_writefn0(card, HCFG, HCFG_LOCKSOUNDCACHE | HCFG_LOCKTANKCACHE_MASK | HCFG_MUTEBUTTONENABLE);

	sblive_writeptr_tag(card, 0,
                            PTB, 0,

			    /* Reset recording buffers */
			    MICBS, ADCBS_BUFSIZE_NONE,
			    MICBA, 0,
			    FXBS, ADCBS_BUFSIZE_NONE,
			    FXBA, 0,
			    FXWC, 0,
			    ADCBS, ADCBS_BUFSIZE_NONE,
			    ADCBA, 0,
			    TCBS, 0,
			    TCB, 0,
			    DBG, 0x8000,

			    /* Disable channel interrupt */
			    CLIEL, 0,
			    CLIEH, 0,
			    SOLEL, 0,
			    SOLEH, 0,
			    TAGLIST_END);


	pci_free_consistent(card->pci_dev, card->virtualpagetable.size, card->virtualpagetable.addr, card->virtualpagetable.dma_handle);
	pci_free_consistent(card->pci_dev, card->silentpage.size, card->silentpage.addr, card->silentpage.dma_handle);
	
	if(card->tankmem.size != 0)
		pci_free_consistent(card->pci_dev, card->tankmem.size, card->tankmem.addr, card->tankmem.dma_handle);

	return;
}

/* Driver initialization routine */
static int __devinit emu10k1_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	struct emu10k1_card *card;
	u32 subsysvid;

	if ((card = kmalloc(sizeof(struct emu10k1_card), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "emu10k1: out of memory\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(struct emu10k1_card));

	if (!pci_dma_supported(pci_dev, EMU10K1_DMA_MASK)) {
		printk(KERN_ERR "emu10k1: architecture does not support 32bit PCI busmaster DMA\n");
		kfree(card);
		return -ENODEV;
	}

	if (pci_enable_device(pci_dev)) {
		kfree(card);
		return -ENODEV;
	}

	pci_set_master(pci_dev);

	card->iobase = pci_resource_start(pci_dev, 0);
	card->length = pci_resource_len(pci_dev, 0); 

	if (request_region(card->iobase, card->length, card_names[pci_id->driver_data]) == NULL) {
		printk(KERN_ERR "emu10k1: IO space in use\n");
		kfree(card);
		return -ENODEV;
	}

	pci_set_drvdata(pci_dev, card);
	PCI_SET_DMA_MASK(pci_dev, EMU10K1_DMA_MASK);

	card->irq = pci_dev->irq;
	card->pci_dev = pci_dev;

	/* Reserve IRQ Line */
	if (request_irq(card->irq, emu10k1_interrupt, SA_SHIRQ, card_names[pci_id->driver_data], card)) {
		printk(KERN_ERR "emu10k1: IRQ in use\n");
		goto err_irq;
	}

	pci_read_config_byte(pci_dev, PCI_REVISION_ID, &card->chiprev);
	pci_read_config_word(pci_dev, PCI_SUBSYSTEM_ID, &card->model);

	printk(KERN_INFO "emu10k1: %s rev %d model 0x%x found, IO at 0x%04lx-0x%04lx, IRQ %d\n",
		card_names[pci_id->driver_data], card->chiprev, card->model, card->iobase,
		card->iobase + card->length - 1, card->irq);

	pci_read_config_dword(pci_dev, PCI_SUBSYSTEM_VENDOR_ID, &subsysvid);
	card->isaps = (subsysvid == EMU_APS_SUBID);

	spin_lock_init(&card->lock);
	init_MUTEX(&card->open_sem);
	card->open_mode = 0;
	init_waitqueue_head(&card->open_wait);

	/* Register devices */
	if ((card->audio_num = register_sound_dsp(&emu10k1_audio_fops, -1)) < 0) {
		printk(KERN_ERR "emu10k1: cannot register first audio device!\n");
		goto err_dev0;
	}

	if ((card->audio1_num = register_sound_dsp(&emu10k1_audio_fops, -1)) < 0) {
		printk(KERN_ERR "emu10k1: cannot register second audio device!\n");
		goto err_dev1;
	}

	if ((card->mixer_num = register_sound_mixer(&emu10k1_mixer_fops, -1)) < 0) {
		printk(KERN_ERR "emu10k1: cannot register mixer device!\n");
		goto err_dev2;
	}

	if ((card->midi_num = register_sound_midi(&emu10k1_midi_fops, -1)) < 0) {
		printk(KERN_ERR "emu10k1: cannot register midi device!\n");
		goto err_dev3;
	}

	if (emu10k1_init(card) < 0) {
		printk(KERN_ERR "emu10k1: cannot initialize device!\n");
		goto err_emu10k1_init;
	}

	if (midi_init(card) < 0) {
		printk(KERN_ERR "emu10k1: cannot initialize midi!\n");
		goto err_midi_init;
	}

	audio_init(card);
	mixer_init(card);

	if (card->isaps)
		emu10k1_ecard_init(card);

	list_add(&card->list, &emu10k1_devs);

	return 0;

      err_midi_init:
	emu10k1_exit(card);

      err_emu10k1_init:
	unregister_sound_midi(card->midi_num);

      err_dev3:
	unregister_sound_mixer(card->mixer_num);

      err_dev2:
	unregister_sound_dsp(card->audio1_num);

      err_dev1:
	unregister_sound_dsp(card->audio_num);

      err_dev0:
	free_irq(card->irq, card);

      err_irq:
	release_region(card->iobase, card->length);
	kfree(card);

	return -ENODEV;
}

static void __devexit emu10k1_remove(struct pci_dev *pci_dev)
{
	struct emu10k1_card *card = pci_get_drvdata(pci_dev);

	midi_exit(card);
	emu10k1_exit(card);

	unregister_sound_midi(card->midi_num);
	
	unregister_sound_mixer(card->mixer_num);

	unregister_sound_dsp(card->audio1_num);
	unregister_sound_dsp(card->audio_num);

	free_irq(card->irq, card);
	release_region(card->iobase, card->length);

	list_del(&card->list);

	kfree(card);

	pci_set_drvdata(pci_dev, NULL);
}

MODULE_AUTHOR("Bertrand Lee, Cai Ying. (Email to: emu10k1-devel@opensource.creative.com)");
MODULE_DESCRIPTION("Creative EMU10K1 PCI Audio Driver v" DRIVER_VERSION "\nCopyright (C) 1999 Creative Technology Ltd.");

static struct pci_driver emu10k1_pci_driver = {
	name:"emu10k1",
	id_table:emu10k1_pci_tbl,
	probe:emu10k1_probe,
	remove:emu10k1_remove,
};

static int __init emu10k1_init_module(void)
{
	printk(KERN_INFO "Creative EMU10K1 PCI Audio Driver, version " DRIVER_VERSION ", " __TIME__ " " __DATE__ "\n");

	return pci_module_init(&emu10k1_pci_driver);
}

static void __exit emu10k1_cleanup_module(void)
{
	pci_unregister_driver(&emu10k1_pci_driver);

	return;
}

module_init(emu10k1_init_module);
module_exit(emu10k1_cleanup_module);
