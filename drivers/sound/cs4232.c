/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 *	cs4232.c
 *
 * The low level driver for Crystal CS4232 based cards. The CS4232 is
 * a PnP compatible chip which contains a CS4231A codec, SB emulation,
 * a MPU401 compatible MIDI port, joystick and synthesizer and IDE CD-ROM 
 * interfaces. This is just a temporary driver until full PnP support
 * gets implemented. Just the WSS codec, FM synth and the MIDI ports are
 * supported. Other interfaces are left uninitialized.
 *
 * ifdef ...WAVEFRONT...
 * 
 *   Support is provided for initializing the WaveFront synth
 *   interface as well, which is logical device #4. Note that if
 *   you have a Tropez+ card, you probably don't need to setup
 *   the CS4232-supported MIDI interface, since it corresponds to
 *   the internal 26-pin header that's hard to access. Using this
 *   requires an additional IRQ, a resource none too plentiful in
 *   this environment. Just don't set module parameters mpuio and
 *   mpuirq, and the MIDI port will be left uninitialized. You can
 *   still use the ICS2115 hosted MIDI interface which corresponds
 *   to the 9-pin D connector on the back of the card.
 *
 * endif  ...WAVEFRONT...
 *
 * Supported chips are:
 *      CS4232
 *      CS4236
 *      CS4236B
 *
 * Note: You will need a PnP config setup to initialise some CS4232 boards
 * anyway.
 *
 * Changes
 *	Alan Cox		Modularisation, Basic cleanups.
 *      Paul Barton-Davis	Separated MPU configuration, added
 *                                       Tropez+ (WaveFront) support
 *	Christoph Hellwig	Adapted to module_init/module_exit,
 * 					simple cleanups
 * 	Arnaldo C. de Melo	got rid of attach_uart401
 *	Bartlomiej Zolnierkiewicz
 *				Added some __init/__initdata/__exit
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#include "sound_config.h"

#include "cs4232.h"
#include "ad1848.h"
#include "mpu401.h"

#define KEY_PORT	0x279	/* Same as LPT1 status port */
#define CSN_NUM		0x99	/* Just a random number */

static void CS_OUT(unsigned char a)
{
	outb(a, KEY_PORT);
}

#define CS_OUT2(a, b)		{CS_OUT(a);CS_OUT(b);}
#define CS_OUT3(a, b, c)	{CS_OUT(a);CS_OUT(b);CS_OUT(c);}

static int mpu_base = 0, mpu_irq = 0;
static int synth_base = 0, synth_irq = 0;
static int mpu_detected = 0;

int __init probe_cs4232_mpu(struct address_info *hw_config)
{
	/*
	 *	Just write down the config values.
	 */

	mpu_base = hw_config->io_base;
	mpu_irq = hw_config->irq;

	return 1;
}

static unsigned char crystal_key[] __initdata =	/* A 32 byte magic key sequence */
{
	0x96, 0x35, 0x9a, 0xcd, 0xe6, 0xf3, 0x79, 0xbc,
	0x5e, 0xaf, 0x57, 0x2b, 0x15, 0x8a, 0xc5, 0xe2,
	0xf1, 0xf8, 0x7c, 0x3e, 0x9f, 0x4f, 0x27, 0x13,
	0x09, 0x84, 0x42, 0xa1, 0xd0, 0x68, 0x34, 0x1a
};

static void sleep(unsigned howlong)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(howlong);
}

int __init probe_cs4232(struct address_info *hw_config)
{
	int i, n;
	int base = hw_config->io_base, irq = hw_config->irq;
	int dma1 = hw_config->dma, dma2 = hw_config->dma2;

	/*
	 * Verify that the I/O port range is free.
	 */

	if (check_region(base, 4))
	{
		printk(KERN_ERR "cs4232.c: I/O port 0x%03x not free\n", base);
		return 0;
	}
	if (ad1848_detect(hw_config->io_base, NULL, hw_config->osp))
		return 1;	/* The card is already active */

	/*
	 * This version of the driver doesn't use the PnP method when configuring
	 * the card but a simplified method defined by Crystal. This means that
	 * just one CS4232 compatible device can exist on the system. Also this
	 * method conflicts with possible PnP support in the OS. For this reason 
	 * driver is just a temporary kludge.
	 *
	 * Also the Cirrus/Crystal method doesnt always work. Try ISA PnP first ;)
	 */

	/*
	 * Repeat initialization few times since it doesn't always succeed in
	 * first time.
	 */

	for (n = 0; n < 4; n++)
	{	
		/*
		 *	Wake up the card by sending a 32 byte Crystal key to the key port.
		 */
		
		for (i = 0; i < 32; i++)
			CS_OUT(crystal_key[i]);

		sleep(HZ / 10);

		/*
		 *	Now set the CSN (Card Select Number).
		 */

		CS_OUT2(0x06, CSN_NUM);

		/*
		 *	Then set some config bytes. First logical device 0 
		 */

		CS_OUT2(0x15, 0x00);	/* Select logical device 0 (WSS/SB/FM) */
		CS_OUT3(0x47, (base >> 8) & 0xff, base & 0xff);	/* WSS base */

		if (check_region(0x388, 4))	/* Not free */
			CS_OUT3(0x48, 0x00, 0x00)	/* FM base off */
		else
			CS_OUT3(0x48, 0x03, 0x88);	/* FM base 0x388 */

		CS_OUT3(0x42, 0x00, 0x00);	/* SB base off */
		CS_OUT2(0x22, irq);		/* SB+WSS IRQ */
		CS_OUT2(0x2a, dma1);		/* SB+WSS DMA */

		if (dma2 != -1)
			CS_OUT2(0x25, dma2)	/* WSS DMA2 */
		else
			CS_OUT2(0x25, 4);	/* No WSS DMA2 */

		CS_OUT2(0x33, 0x01);	/* Activate logical dev 0 */

		sleep(HZ / 10);

		/*
		 * Initialize logical device 3 (MPU)
		 */

		if (mpu_base != 0 && mpu_irq != 0)
		{
			CS_OUT2(0x15, 0x03);	/* Select logical device 3 (MPU) */
			CS_OUT3(0x47, (mpu_base >> 8) & 0xff, mpu_base & 0xff);	/* MPU base */
			CS_OUT2(0x22, mpu_irq);	/* MPU IRQ */
			CS_OUT2(0x33, 0x01);	/* Activate logical dev 3 */
		}

		if(synth_base != 0)
		{
		    CS_OUT2 (0x15, 0x04);	        /* logical device 4 (WaveFront) */
		    CS_OUT3 (0x47, (synth_base >> 8) & 0xff,
			     synth_base & 0xff);	/* base */
		    CS_OUT2 (0x22, synth_irq);     	/* IRQ */
		    CS_OUT2 (0x33, 0x01);	        /* Activate logical dev 4 */
		}

		/*
		 * Finally activate the chip
		 */
		
		CS_OUT(0x79);

		sleep(HZ / 5);

		/*
		 * Then try to detect the codec part of the chip
		 */

		if (ad1848_detect(hw_config->io_base, NULL, hw_config->osp))
			return 1;
		
		sleep(HZ);
	}
	return 0;
}

void __init attach_cs4232(struct address_info *hw_config)
{
	int base = hw_config->io_base,
		irq = hw_config->irq,
		dma1 = hw_config->dma,
		dma2 = hw_config->dma2;

	if (base == -1 || irq == -1 || dma1 == -1) {
		printk(KERN_ERR "cs4232: dma, irq and io must be set.\n");
		return;
	}

	if (dma2 == -1)
		dma2 = dma1;

	hw_config->slots[0] = ad1848_init("Crystal audio controller", base,
					  irq,
					  dma1,		/* Playback DMA */
					  dma2,		/* Capture DMA */
					  0,
					  hw_config->osp,
					  THIS_MODULE);

	if (hw_config->slots[0] != -1 &&
		audio_devs[hw_config->slots[0]]->mixer_dev!=-1)
	{	
		/* Assume the mixer map is as suggested in the CS4232 databook */
		AD1848_REROUTE(SOUND_MIXER_LINE1, SOUND_MIXER_LINE);
		AD1848_REROUTE(SOUND_MIXER_LINE2, SOUND_MIXER_CD);
		AD1848_REROUTE(SOUND_MIXER_LINE3, SOUND_MIXER_SYNTH);		/* FM synth */
	}
	if (mpu_base != 0 && mpu_irq != 0)
	{
		static struct address_info hw_config2 = {
			0
		};		/* Ensure it's initialized */

		hw_config2.io_base = mpu_base;
		hw_config2.irq = mpu_irq;
		hw_config2.dma = -1;
		hw_config2.dma2 = -1;
		hw_config2.always_detect = 0;
		hw_config2.name = NULL;
		hw_config2.driver_use_1 = 0;
		hw_config2.driver_use_2 = 0;
		hw_config2.card_subtype = 0;

		if (probe_uart401(&hw_config2, THIS_MODULE))
		{
			mpu_detected = 1;
		}
		else
		{
			mpu_base = mpu_irq = 0;
		}
		hw_config->slots[1] = hw_config2.slots[1];
	}
}

void __exit unload_cs4232(struct address_info *hw_config)
{
	int base = hw_config->io_base, irq = hw_config->irq;
	int dma1 = hw_config->dma, dma2 = hw_config->dma2;

	if (dma2 == -1)
		dma2 = dma1;

	ad1848_unload(base,
		      irq,
		      dma1,	/* Playback DMA */
		      dma2,	/* Capture DMA */
		      0);

	sound_unload_audiodev(hw_config->slots[0]);
	if (mpu_base != 0 && mpu_irq != 0 && mpu_detected)
	{
		static struct address_info hw_config2 =
		{
			0
		};		/* Ensure it's initialized */

		hw_config2.io_base = mpu_base;
		hw_config2.irq = mpu_irq;
		hw_config2.dma = -1;
		hw_config2.dma2 = -1;
		hw_config2.always_detect = 0;
		hw_config2.name = NULL;
		hw_config2.driver_use_1 = 0;
		hw_config2.driver_use_2 = 0;
		hw_config2.card_subtype = 0;
		hw_config2.slots[1] = hw_config->slots[1];

		unload_uart401(&hw_config2);
	}
}

static struct address_info cfg;
static struct address_info cfg_mpu;

static int __initdata io	= -1;
static int __initdata irq	= -1;
static int __initdata dma	= -1;
static int __initdata dma2	= -1;
static int __initdata mpuio	= -1;
static int __initdata mpuirq	= -1;
static int __initdata synthio	= -1;
static int __initdata synthirq	= -1;


MODULE_PARM(io,"i");
MODULE_PARM(irq,"i");
MODULE_PARM(dma,"i");
MODULE_PARM(dma2,"i");
MODULE_PARM(mpuio,"i");
MODULE_PARM(mpuirq,"i");
MODULE_PARM(synthio,"i");
MODULE_PARM(synthirq,"i");

/*
 *	Install a CS4232 based card. Need to have ad1848 and mpu401
 *	loaded ready.
 */

static int __init init_cs4232(void)
{
#ifdef CONFIG_SOUND_WAVEFRONT_MODULE
	if(synthio == -1)
		printk(KERN_WARNING "cs4232: set synthio and synthirq to use the wavefront facilities.\n");
	else {
		synth_base = synthio;
		synth_irq =  synthirq;	
	}
#else
	if(synthio != -1)
		printk(KERN_WARNING "cs4232: wavefront support not enabled in this driver.\n");
#endif
	if(io==-1||irq==-1||dma==-1)
	{
		printk(KERN_ERR "cs4232: Must set io, irq and dma.\n");
		return -ENODEV;
	}

	cfg.io_base = io;
	cfg.irq = irq;
	cfg.dma = dma;
	cfg.dma2 = dma2;

	cfg_mpu.io_base = -1;
	cfg_mpu.irq = -1;

	if (mpuio != -1 && mpuirq != -1) {
		cfg_mpu.io_base = mpuio;
		cfg_mpu.irq = mpuirq;
		probe_cs4232_mpu(&cfg_mpu); /* Bug always returns 0 not OK -- AC */
	}

	if (probe_cs4232(&cfg) == 0)
		return -ENODEV;
	attach_cs4232(&cfg);
	
	return 0;
}

static void __exit cleanup_cs4232(void)
{
        unload_cs4232(&cfg); /* unloads MPU as well, if needed */
}

module_init(init_cs4232);
module_exit(cleanup_cs4232);

#ifndef MODULE
static int __init setup_cs4232(char *str)
{
	/* io, irq, dma, dma2 mpuio, mpuirq*/
	int ints[7];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma2	= ints[4];
	mpuio	= ints[5];
	mpuirq	= ints[6];

	return 1;
}

__setup("cs4232=", setup_cs4232);
#endif
