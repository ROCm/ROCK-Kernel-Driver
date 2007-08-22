/*
 * Driver for the PSC of the Freescale MPC52xx configured as AC97 interface
 *
 *
 * Copyright (C) 2006 Sylvain Munaut <tnt@246tNt.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/ac97_codec.h>

#include <asm/of_platform.h>
#include <asm/mpc52xx_psc.h>


#define DRV_NAME "mpc52xx-psc-ac97"


/* ======================================================================== */
/* Structs / Defines                                                        */
/* ======================================================================== */

/* Private structure */
struct mpc52xx_ac97_priv {
	struct device *dev;
	resource_size_t	mem_start;
	resource_size_t mem_len;
	int irq;
	struct mpc52xx_psc __iomem *psc;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_ac97 *ac97;

	struct snd_pcm_substream *substream_playback;
	unsigned int buf_pos;
};

/* Register bit definition (AC97 mode specific) */
#define PSC_AC97_SLOT_BIT(n)		(1<<(12-n))
#define PSC_AC97_SLOTS_XMIT_SHIFT	16
#define PSC_AC97_SLOTS_RECV_SHIFT	 0



/* ======================================================================== */
/* ISR routine                                                              */
/* ======================================================================== */

static irqreturn_t
mpc52xx_ac97_irq(int irq, void *dev_id)
{
	struct mpc52xx_ac97_priv *priv = dev_id;

	static int icnt = 0;
	#if 0
	{
	unsigned int val;
//	val = in_be32(&priv->psc->ac97_data);
	printk(KERN_INFO "mpc52xx_ac97_irq fired (isr=%04x, status=%04x) %08x\n", in_be16(&priv->psc->mpc52xx_psc_imr), in_be16(&priv->psc->mpc52xx_psc_status), val);
	out_8(&priv->psc->command,MPC52xx_PSC_RST_ERR_STAT);
	}
	#endif

	/* Anti Crash during dev ;) */
	#if 0
	if ((icnt++) > 50000)
		out_be16(&priv->psc->mpc52xx_psc_imr, 0);
	#endif

	/* Copy 64 data into the buffer */
	if (in_be16(&priv->psc->mpc52xx_psc_imr) & 0x0100) {
		if (priv->substream_playback) {
			struct snd_pcm_runtime *rt;

			rt = priv->substream_playback->runtime;

			if (snd_pcm_playback_hw_avail(rt) < bytes_to_frames(rt,128)) {
				int i;
				/* Push silence */
				for (i=0; i<64; i++)
					out_be32(&priv->psc->mpc52xx_psc_buffer_32, 0x00000800);
				printk(KERN_DEBUG "pushed silence ...\n");
			} else {
				int i;
				unsigned short *data;

				data = (unsigned short *)
					(&rt->dma_area[frames_to_bytes(rt, priv->buf_pos)]);

				for (i=0; i<64; i++)
					out_be32(&priv->psc->mpc52xx_psc_buffer_32,
						(((unsigned int)data[i]) << 16) | 0x00000000);
							/* Setting the sof bit looks useless */

				priv->buf_pos += bytes_to_frames(rt,128);;
				if (priv->buf_pos >= rt->buffer_size)
					priv->buf_pos = 0;

				snd_pcm_period_elapsed(priv->substream_playback);
			}
		} else {
			out_be16(&priv->psc->mpc52xx_psc_imr, 0);
			printk(KERN_DEBUG "Interrupt with no stream ...\n");
		}
	} else {
		printk(KERN_ERR "Spurious int\n");
	}

	return IRQ_HANDLED;
}


/* ======================================================================== */
/* PCM interface                                                            */
/* ======================================================================== */

/* HW desc */

static struct snd_pcm_hardware mpc52xx_ac97_hw = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED		|
					SNDRV_PCM_INFO_MMAP		|
					SNDRV_PCM_INFO_MMAP_VALID,
	.formats		= SNDRV_PCM_FMTBIT_S16_BE,
	.rates			= SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min		= 1,
	.channels_max		= 2,	/* Support for more ? */
	.buffer_bytes_max	= 128*1024,
	.period_bytes_min	= 128, /* 32, */
	.period_bytes_max	= 128, /* 16*1024, */
	.periods_min		= 8,
	.periods_max		= 256,
	.fifo_size		= 512,
};


/* Playback */

static int
mpc52xx_ac97_playback_open(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;

	dev_dbg(priv->dev, "mpc52xx_ac97_playback_open(%p)\n", substream);

	substream->runtime->hw = mpc52xx_ac97_hw;

	priv->substream_playback = substream;
	priv->buf_pos = 0;	/* FIXME Do that where ? */

	return 0;	/* FIXME */
}

static int
mpc52xx_ac97_playback_close(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;
	dev_dbg(priv->dev, "mpc52xx_ac97_playback_close(%p)\n", substream);
	priv->substream_playback = NULL;
	return 0;	/* FIXME */
}

static int
mpc52xx_ac97_playback_prepare(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;

	dev_dbg(priv->dev, "mpc52xx_ac97_playback_prepare(%p)\n", substream);

	/* FIXME, need a spinlock to protect access */
	if (substream->runtime->channels == 1)
		out_be32(&priv->psc->ac97_slots, 0x01000000);
	else
		out_be32(&priv->psc->ac97_slots, 0x03000000);

	snd_ac97_set_rate(priv->ac97, AC97_PCM_FRONT_DAC_RATE, substream->runtime->rate);

	return 0;	/* FIXME */
}


/* Capture */

static int
mpc52xx_ac97_capture_open(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;
	return 0;	/* FIXME */
}

static int
mpc52xx_ac97_capture_close(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;
	return 0;	/* FIXME */
}

static int
mpc52xx_ac97_capture_prepare(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;
	return 0;	/* FIXME */
}


/* Common */

static int
mpc52xx_ac97_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;
	int rv;

	dev_dbg(priv->dev, "mpc52xx_ac97_hw_params(%p)\n", substream);

	rv = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(params));
	if (rv < 0) {
		printk(KERN_ERR "hw params failes\n");	/* FIXME */
		return rv;
	}

	printk(KERN_DEBUG "%d %d %d\n", params_buffer_bytes(params), params_period_bytes(params), params_periods(params));


	return 0;
}

static int
mpc52xx_ac97_hw_free(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;

	dev_dbg(priv->dev, "mpc52xx_ac97_hw_free(%p)\n", substream);

	return snd_pcm_lib_free_pages(substream);
}

static int
mpc52xx_ac97_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;
	int rv = 0;

	dev_dbg(priv->dev, "mpc52xx_ac97_trigger(%p,%d)\n", substream, cmd);

	switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
			/* Enable TX interrupt */
			out_be16(&priv->psc->mpc52xx_psc_imr, 0x0100); // 0x0100

			break;

		case SNDRV_PCM_TRIGGER_STOP:
			/* Disable TX interrupt */
			out_be16(&priv->psc->mpc52xx_psc_imr, 0x0000);

			break;

		default:
			rv = -EINVAL;
	}

	/* FIXME */
	return rv;
}

static snd_pcm_uframes_t
mpc52xx_ac97_pointer(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;

//	dev_dbg(priv->dev, "mpc52xx_ac97_pointer(%p)\n", substream);

	if (substream->runtime->channels == 1)
		return priv->buf_pos;	/* FIXME */
	else
		return priv->buf_pos >> 1;	/* FIXME */
}


/* Ops */

static struct snd_pcm_ops mpc52xx_ac97_playback_ops = {
	.open		= mpc52xx_ac97_playback_open,
	.close		= mpc52xx_ac97_playback_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= mpc52xx_ac97_hw_params,
	.hw_free	= mpc52xx_ac97_hw_free,
	.prepare	= mpc52xx_ac97_playback_prepare,
	.trigger	= mpc52xx_ac97_trigger,
	.pointer	= mpc52xx_ac97_pointer,
};

static struct snd_pcm_ops mpc52xx_ac97_capture_ops = {
	.open		= mpc52xx_ac97_capture_open,
	.close		= mpc52xx_ac97_capture_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= mpc52xx_ac97_hw_params,
	.hw_free	= mpc52xx_ac97_hw_free,
	.prepare	= mpc52xx_ac97_capture_prepare,
	.trigger	= mpc52xx_ac97_trigger,
	.pointer	= mpc52xx_ac97_pointer,
};


/* ======================================================================== */
/* AC97 Bus interface                                                       */
/* ======================================================================== */

static unsigned short
mpc52xx_ac97_bus_read(struct snd_ac97 *ac97, unsigned short reg)
{
	struct mpc52xx_ac97_priv *priv = ac97->private_data;
	int timeout;
	unsigned int val;

	dev_dbg(priv->dev, "ac97 read: reg %04x\n", reg);

	/* Wait for it to be ready */
	timeout = 1000;
	while ((--timeout) && (in_be16(&priv->psc->mpc52xx_psc_status) &
						MPC52xx_PSC_SR_CMDSEND) )
		udelay(10);

	if (!timeout) {
		printk(KERN_ERR DRV_NAME ": timeout on ac97 bus (rdy)\n");
		return 0xffff;
	}

	/* Do the read */
	out_be32(&priv->psc->ac97_cmd, (1<<31) | ((reg & 0x7f) << 24));

	/* Wait for the answer */
	timeout = 1000;
	while ((--timeout) && !(in_be16(&priv->psc->mpc52xx_psc_status) &
						MPC52xx_PSC_SR_DATA_VAL) )
		udelay(10);

	if (!timeout) {
		printk(KERN_ERR DRV_NAME ": timeout on ac97 read (val)\n");
		return 0xffff;
	}

	/* Get the data */
	val = in_be32(&priv->psc->ac97_data);
	if ( ((val>>24) & 0x7f) != reg ) {
		printk(KERN_ERR DRV_NAME ": reg echo error on ac97 read\n");
		return 0xffff;
	}
	val = (val >> 8) & 0xffff;

	dev_dbg(priv->dev, "ac97 read ok: reg %04x  val %04x\n",
				reg, val);

	return (unsigned short) val;
}

static void
mpc52xx_ac97_bus_write(struct snd_ac97 *ac97,
			unsigned short reg, unsigned short val)
{
	struct mpc52xx_ac97_priv *priv = ac97->private_data;
	int timeout;

	dev_dbg(priv->dev, "ac97 write: reg %04x  val %04x\n",
				reg, val);

	/* Wait for it to be ready */
	timeout = 1000;
	while ((--timeout) && (in_be16(&priv->psc->mpc52xx_psc_status) &
						MPC52xx_PSC_SR_CMDSEND) )
		udelay(10);

	if (!timeout) {
		printk(KERN_ERR DRV_NAME ": timeout on ac97 write\n");
		return;
	}

	/* Write data */
	out_be32(&priv->psc->ac97_cmd, ((reg & 0x7f) << 24) | (val << 8));
}

static void
mpc52xx_ac97_bus_reset(struct snd_ac97 *ac97)
{
	struct mpc52xx_ac97_priv *priv = ac97->private_data;

	dev_dbg(priv->dev, "ac97 codec reset\n");

	/* Do a cold reset */
	out_8(&priv->psc->op1, 0x03);
	udelay(10);
	out_8(&priv->psc->op0, 0x02);
	udelay(50);

	/* PSC recover from cold reset (cfr user manual, not sure if useful) */
	out_be32(&priv->psc->sicr, in_be32(&priv->psc->sicr));
}


static struct snd_ac97_bus_ops mpc52xx_ac97_bus_ops = {
	.read	= mpc52xx_ac97_bus_read,
	.write	= mpc52xx_ac97_bus_write,
	.reset	= mpc52xx_ac97_bus_reset,
};


/* ======================================================================== */
/* Sound driver setup                                                       */
/* ======================================================================== */

static int
mpc52xx_ac97_setup_pcm(struct mpc52xx_ac97_priv *priv)
{
	int rv;

	rv = snd_pcm_new(priv->card, DRV_NAME "-pcm", 0, 1, 1, &priv->pcm);
	if (rv) {
		printk(KERN_ERR DRV_NAME ": snd_pcm_new failed\n");
		return rv;
	}

	rv = snd_pcm_lib_preallocate_pages_for_all(priv->pcm,
		SNDRV_DMA_TYPE_CONTINUOUS, snd_dma_continuous_data(GFP_KERNEL),
		128*1024, 128*1024);
	if (rv) {
		printk(KERN_ERR DRV_NAME
			": snd_pcm_lib_preallocate_pages_for_all  failed\n");
		return rv;
	}

	snd_pcm_set_ops(priv->pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&mpc52xx_ac97_playback_ops);
	snd_pcm_set_ops(priv->pcm, SNDRV_PCM_STREAM_CAPTURE,
			&mpc52xx_ac97_capture_ops);

	priv->pcm->private_data = priv;
	priv->pcm->info_flags = 0;

	strcpy(priv->pcm->name, "Freescale MPC52xx PSC-AC97 PCM");

	return 0;
}

static int
mpc52xx_ac97_setup_mixer(struct mpc52xx_ac97_priv *priv)
{
	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97_template ac97_template;
	int rv;

	rv = snd_ac97_bus(priv->card, 0, &mpc52xx_ac97_bus_ops, NULL, &ac97_bus);
	if (rv) {
		printk(KERN_ERR DRV_NAME ": snd_ac97_bus failed\n");
		return rv;
	}

	memset(&ac97_template, 0, sizeof(struct snd_ac97_template));
	ac97_template.private_data = priv;

	rv = snd_ac97_mixer(ac97_bus, &ac97_template, &priv->ac97);
	if (rv) {
		printk(KERN_ERR DRV_NAME ": snd_ac97_mixer failed\n");
		return rv;
	}

	return 0;
}


static int
mpc52xx_ac97_hwinit(struct mpc52xx_ac97_priv *priv)
{
	/* Reset everything first by safety */
	out_8(&priv->psc->command,MPC52xx_PSC_RST_RX);
	out_8(&priv->psc->command,MPC52xx_PSC_RST_TX);
	out_8(&priv->psc->command,MPC52xx_PSC_RST_ERR_STAT);

	/* Do a cold reset of codec */
	out_8(&priv->psc->op1, 0x03);
	udelay(10);
	out_8(&priv->psc->op0, 0x02);
	udelay(50);

	/* Configure AC97 enhanced mode */
	out_be32(&priv->psc->sicr, 0x03010000);

	/* No slots active */
	out_be32(&priv->psc->ac97_slots, 0x00000000);

	/* No IRQ */
	out_be16(&priv->psc->mpc52xx_psc_imr, 0x0000);

	/* FIFO levels */
	out_8(&priv->psc->rfcntl, 0x07);
	out_8(&priv->psc->tfcntl, 0x07);
	out_be16(&priv->psc->rfalarm, 0x80);
	out_be16(&priv->psc->tfalarm, 0x80);

	/* Go */
	out_8(&priv->psc->command,MPC52xx_PSC_TX_ENABLE);
	out_8(&priv->psc->command,MPC52xx_PSC_RX_ENABLE);

	return 0;
}

static int
mpc52xx_ac97_hwshutdown(struct mpc52xx_ac97_priv *priv)
{
	/* No IRQ */
	out_be16(&priv->psc->mpc52xx_psc_imr, 0x0000);

	/* Disable TB & RX */
	out_8(&priv->psc->command,MPC52xx_PSC_RST_RX);
	out_8(&priv->psc->command,MPC52xx_PSC_RST_TX);

	/* FIXME : Reset or put codec in low power ? */

	return 0;
}


/* ======================================================================== */
/* OF Platform Driver                                                       */
/* ======================================================================== */

static int __devinit
mpc52xx_ac97_probe(struct of_device *op, const struct of_device_id *match)
{
	struct device_node *dn = op->node;
	struct mpc52xx_ac97_priv *priv;
	struct snd_card *card;
	struct resource res;
	int rv;

	dev_dbg(&op->dev, "probing MPC52xx PSC AC97 driver\n");

	/* Get card structure */
	rv = -ENOMEM;
	card = snd_card_new(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
				THIS_MODULE, sizeof(struct mpc52xx_ac97_priv));
	if (!card)
		goto err_early;

	priv = card->private_data;

	/* Init our private structure */
	priv->card = card;
	priv->dev = &op->dev;

	/* Get resources (mem,irq,...) */
	rv = of_address_to_resource(dn, 0, &res);
	if (rv)
		goto err_early;

	priv->mem_start = res.start;
	priv->mem_len = res.end - res.start + 1;

	if (!request_mem_region(priv->mem_start, priv->mem_len, DRV_NAME)) {
		printk(KERN_ERR DRV_NAME ": request_mem_region failed\n");
		rv = -EBUSY;
		goto err_early;
	}

	priv->psc = ioremap(priv->mem_start, priv->mem_len);
	if (!priv->psc) {
		printk(KERN_ERR DRV_NAME ": ioremap failed\n");
		rv = -ENOMEM;
		goto err_iomap;
	}

	priv->irq = irq_of_parse_and_map(dn, 0);
	if (priv->irq == NO_IRQ) {
		printk(KERN_ERR DRV_NAME ": irq_of_parse_and_map failed\n");
		rv = -EBUSY;
		goto err_irqmap;
	}

	/* Low level HW Init */
	mpc52xx_ac97_hwinit(priv);

	/* Request IRQ now that we're 'stable' */
	rv = request_irq(priv->irq, mpc52xx_ac97_irq, 0, DRV_NAME, priv);
	if (rv < 0) {
		printk(KERN_ERR DRV_NAME ": request_irq failed\n");
		goto err_irqreq;
	}

	/* Prepare sound stuff */
	rv = mpc52xx_ac97_setup_mixer(priv);
	if (rv)
		goto err_late;

	rv = mpc52xx_ac97_setup_pcm(priv);
	if (rv)
		goto err_late;

	/* Finally register the card */
	snprintf(card->shortname, sizeof(card->shortname), DRV_NAME);
	snprintf(card->longname, sizeof(card->longname),
		"Freescale MPC52xx PSC-AC97 (%s)", card->mixername);

	rv = snd_card_register(card);
	if (rv) {
		printk(KERN_ERR DRV_NAME ": snd_card_register failed\n");
		goto err_late;
	}

	dev_set_drvdata(&op->dev, priv);

	return 0;

err_late:
	free_irq(priv->irq, priv);
err_irqreq:
	mpc52xx_ac97_hwshutdown(priv);
	irq_dispose_mapping(priv->irq);
err_irqmap:
	iounmap(priv->psc);
err_iomap:
	release_mem_region(priv->mem_start, priv->mem_len);
err_early:
	if (card)
		snd_card_free(card);
	return rv;
}

static int
mpc52xx_ac97_remove(struct of_device *op)
{
	struct mpc52xx_ac97_priv *priv;

	dev_dbg(&op->dev, "removing MPC52xx PSC AC97 driver\n");

	priv = dev_get_drvdata(&op->dev);
	if (priv) {
		/* Sound subsys shutdown */
		snd_card_free(priv->card);

		/* Low level HW shutdown */
		mpc52xx_ac97_hwshutdown(priv);

		/* Release resources */
		iounmap(priv->psc);
		free_irq(priv->irq, priv);
		irq_dispose_mapping(priv->irq);
		release_mem_region(priv->mem_start, priv->mem_len);
	}

	dev_set_drvdata(&op->dev, NULL);

	return 0;
}


static struct of_device_id mpc52xx_ac97_of_match[] = {
	{
/*		.type		= "ac97",	FIXME Efika ... */
		.compatible	= "mpc5200b-psc-ac97",	/* B only for now */
	},
};

MODULE_DEVICE_TABLE(of, mpc52xx_ac97_of_match);


static struct of_platform_driver mpc52xx_ac97_of_driver = {
	.owner		= THIS_MODULE,
	.name		= DRV_NAME,
	.match_table	= mpc52xx_ac97_of_match,
	.probe		= mpc52xx_ac97_probe,
	.remove		= mpc52xx_ac97_remove,
	.driver		= {
		.name	= DRV_NAME,
	},
};


/* ======================================================================== */
/* Module                                                                   */
/* ======================================================================== */

static int __init
mpc52xx_ac97_init(void)
{
	int rv;

	/* FIXME BIG FAT EFIKA HACK */
	{
		void *mbar;
		mbar = ioremap(0xf0000000, 0x100000);
		printk(KERN_INFO "EFIKA HACK: port_config %08x\n", in_be32(mbar + 0xb00));
		out_be32(mbar + 0xb00, 0x01051124);
		printk(KERN_INFO "EFIKA HACK: port_config %08x\n", in_be32(mbar + 0xb00));
		iounmap(mbar);
	}
	/* ------------------------ */

	printk(KERN_INFO "Sound: MPC52xx PSC AC97 driver\n");

	rv = of_register_platform_driver(&mpc52xx_ac97_of_driver);
	if (rv) {
		printk(KERN_ERR DRV_NAME ": "
			"of_register_platform_driver failed (%i)\n", rv);
		return rv;
	}

	return 0;
}

static void __exit
mpc52xx_ac97_exit(void)
{
	of_unregister_platform_driver(&mpc52xx_ac97_of_driver);
}

module_init(mpc52xx_ac97_init);
module_exit(mpc52xx_ac97_exit);

MODULE_AUTHOR("Sylvain Munaut <tnt@246tNt.com>");
MODULE_DESCRIPTION(DRV_NAME ": Freescale MPC52xx PSC AC97 driver");
MODULE_LICENSE("GPL");

