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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/ac97_codec.h>

#include <linux/of_platform.h>
#include <asm/of_platform.h>
#include <linux/dma-mapping.h>
#include <asm/mpc52xx_psc.h>

#include <sysdev/bestcomm/bestcomm.h>
#include <sysdev/bestcomm/gen_bd.h>


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
	struct mpc52xx_psc_fifo __iomem *fifo;

	struct bcom_task *tsk_tx;
	spinlock_t dma_lock;

	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_ac97 *ac97;

	struct snd_pcm_substream *substream_playback;

	int period_byte_size;
	u32 period_start, period_end, period_next_p;
};

/* Register bit definition (AC97 mode specific) */
#define PSC_AC97_SLOT_BIT(n)		(1<<(12-n))
#define PSC_AC97_SLOTS_XMIT_SHIFT	16
#define PSC_AC97_SLOTS_RECV_SHIFT	 0

/* Bestcomm options */
#define AC97_TX_NUM_BD	32
#define AC97_RX_NUM_BD	32

static int mpc52xx_ac97_tx_fill(struct mpc52xx_ac97_priv *priv)
{
	struct snd_pcm_runtime *rt;

	u32 dma_data_ptr;

	rt = priv->substream_playback->runtime;

	dma_data_ptr = virt_to_phys(rt->dma_area);

	priv->period_byte_size	= frames_to_bytes(rt, rt->period_size);
	priv->period_start	= dma_data_ptr;
	priv->period_end	= dma_data_ptr + priv->period_byte_size * rt->periods;
	priv->period_next_p	= dma_data_ptr;

	spin_lock(&priv->dma_lock);
	while (!bcom_queue_full(priv->tsk_tx)) {
		struct bcom_gen_bd *bd;

		/* Submit a new one */
		bd = (struct bcom_gen_bd *) bcom_prepare_next_buffer(priv->tsk_tx);
		bd->status = priv->period_byte_size;
		bd->buf_pa = priv->period_next_p;
		bcom_submit_next_buffer(priv->tsk_tx, NULL);

		/* Next pointer */
		priv->period_next_p += priv->period_byte_size;
		if (priv->period_next_p >= priv->period_end)
			priv->period_next_p = priv->period_start;
	}
	spin_unlock(&priv->dma_lock);

	return 0;
}


/* ======================================================================== */
/* ISR routine                                                              */
/* ======================================================================== */

static irqreturn_t mpc52xx_ac97_tx_irq(int irq, void *dev_id)
{
	struct mpc52xx_ac97_priv *priv = dev_id;
	struct snd_pcm_runtime *rt;
	struct bcom_gen_bd *bd;

	rt = priv->substream_playback->runtime;

	if (!bcom_buffer_done(priv->tsk_tx)) {
		dev_dbg(priv->dev, "tx mismatch? Check correct output PSC\n");
		bcom_disable(priv->tsk_tx);
	}

	spin_lock(&priv->dma_lock);
	while (bcom_buffer_done(priv->tsk_tx)) {
		/* Get the buffer back */
		bcom_retrieve_buffer(priv->tsk_tx, NULL, NULL);

		/* Submit a new one */
		bd = (struct bcom_gen_bd *) bcom_prepare_next_buffer(priv->tsk_tx);
		bd->status = priv->period_byte_size;
		bd->buf_pa = priv->period_next_p;
		bcom_submit_next_buffer(priv->tsk_tx, NULL);
		bcom_enable(priv->tsk_tx);

		/* Next pointer */
		priv->period_next_p += priv->period_byte_size;
		if (priv->period_next_p >= priv->period_end)
			priv->period_next_p = priv->period_start;
	}
	spin_unlock(&priv->dma_lock);

	snd_pcm_period_elapsed(priv->substream_playback);

	return IRQ_HANDLED;
}


static irqreturn_t mpc52xx_ac97_irq(int irq, void *dev_id)
{
	struct mpc52xx_ac97_priv *priv = dev_id;

	static int icnt = 0;

#if 1
	/* Anti Crash during dev ;) */
	if ((icnt++) > 5000)
		out_be16(&priv->psc->mpc52xx_psc_imr, 0);
#endif

	/* Print statuts */
	dev_dbg(priv->dev, "isr: %04x", in_be16(&priv->psc->mpc52xx_psc_imr));
	out_8(&priv->psc->command,MPC52xx_PSC_RST_ERR_STAT);

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
	.formats		= SNDRV_PCM_FMTBIT_S32_BE,
	.rates			= SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min		= 1,
	.channels_max		= 2,	/* Support for more ? */
	.buffer_bytes_max	= 1024*1024,
	.period_bytes_min	= 512,
	.period_bytes_max	= 16*1024,
	.periods_min		= 8,
	.periods_max		= 1024,
	.fifo_size		= 512,
};


/* Playback */

static int mpc52xx_ac97_playback_open(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;

	dev_dbg(priv->dev, "mpc52xx_ac97_playback_open(%p)\n", substream);

	substream->runtime->hw = mpc52xx_ac97_hw;

	priv->substream_playback = substream;

	return 0;	/* FIXME */
}

static int mpc52xx_ac97_playback_close(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;
	dev_dbg(priv->dev, "mpc52xx_ac97_playback_close(%p)\n", substream);
	priv->substream_playback = NULL;
	return 0;	/* FIXME */
}

static int mpc52xx_ac97_playback_prepare(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;

	dev_dbg(priv->dev, "mpc52xx_ac97_playback_prepare(%p)\n", substream);

	/* FIXME, need a spinlock to protect access */
	if (substream->runtime->channels == 1)
		out_be32(&priv->psc->ac97_slots, 0x01000000);
	else
		out_be32(&priv->psc->ac97_slots, 0x03000000);

	snd_ac97_set_rate(priv->ac97, AC97_PCM_FRONT_DAC_RATE,
			substream->runtime->rate);

	return 0;	/* FIXME */
}


/* Capture */

static int mpc52xx_ac97_capture_open(struct snd_pcm_substream *substream)
{
/*	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data; */
	return 0;	/* FIXME */
}

static int mpc52xx_ac97_capture_close(struct snd_pcm_substream *substream)
{
/*	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data; */
	return 0;	/* FIXME */
}

static int
mpc52xx_ac97_capture_prepare(struct snd_pcm_substream *substream)
{
/*	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data; */
	return 0;	/* FIXME */
}


/* Common */

static int mpc52xx_ac97_hw_params(struct snd_pcm_substream *substream,
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

	dev_dbg(priv->dev, "%d %d %d\n", params_buffer_bytes(params),
		params_period_bytes(params), params_periods(params));

	return 0;
}

static int mpc52xx_ac97_hw_free(struct snd_pcm_substream *substream)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;

	dev_dbg(priv->dev, "mpc52xx_ac97_hw_free(%p)\n", substream);

	return snd_pcm_lib_free_pages(substream);
}

static int mpc52xx_ac97_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;
	int rv = 0;

	dev_dbg(priv->dev, "mpc52xx_ac97_trigger(%p,%d)\n", substream, cmd);

	switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
			/* Enable TX taks */
			bcom_gen_bd_tx_reset(priv->tsk_tx);
			mpc52xx_ac97_tx_fill(priv);
			bcom_enable(priv->tsk_tx);
/*
			out_be16(&priv->psc->mpc52xx_psc_imr, 0x0800); // 0x0100
			out_be16(&priv->psc->mpc52xx_psc_imr, 0x0100); // 0x0100
*/
				/* FIXME: Shouldn't we check for overrun too ? */
				/* also, shouldn't we just activate TX here ? */

			break;

		case SNDRV_PCM_TRIGGER_STOP:
			/* Disable TX task */
			bcom_disable(priv->tsk_tx);
			out_be16(&priv->psc->mpc52xx_psc_imr, 0x0000); // 0x0100

			break;

		default:
			rv = -EINVAL;
	}

	/* FIXME */
	return rv;
}

static snd_pcm_uframes_t mpc52xx_ac97_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mpc52xx_ac97_priv *priv = substream->pcm->private_data;
	u32 count;

	count = priv->tsk_tx->bd[priv->tsk_tx->outdex].data[0] - priv->period_start;

	return bytes_to_frames(runtime, count);
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

static unsigned short mpc52xx_ac97_bus_read(struct snd_ac97 *ac97,
						unsigned short reg)
{
	struct mpc52xx_ac97_priv *priv = ac97->private_data;
	int timeout;
	unsigned int val;

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

	return (unsigned short) val;
}

static void mpc52xx_ac97_bus_write(struct snd_ac97 *ac97,
			unsigned short reg, unsigned short val)
{
	struct mpc52xx_ac97_priv *priv = ac97->private_data;
	int timeout;

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

static void mpc52xx_ac97_bus_reset(struct snd_ac97 *ac97)
{
	struct mpc52xx_ac97_priv *priv = ac97->private_data;

	dev_dbg(priv->dev, "ac97 codec reset\n");

	/* Do a cold reset */
	/*
	 * Note: This could interfere with some external AC97 mixers, as it
	 * could switch them into test mode, when SYNC or SDATA_OUT are not
	 * low while RES is low!
	 */
	out_8(&priv->psc->op1, 0x02);
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

static int mpc52xx_ac97_setup_pcm(struct mpc52xx_ac97_priv *priv)
{
	int rv;

	rv = snd_pcm_new(priv->card, DRV_NAME "-pcm", 0, 1, 1, &priv->pcm);
	if (rv) {
		dev_dbg(priv->dev, "%s: snd_pcm_new failed\n", DRV_NAME);
		return rv;
	}

	rv = snd_pcm_lib_preallocate_pages_for_all(priv->pcm,
		SNDRV_DMA_TYPE_CONTINUOUS, snd_dma_continuous_data(GFP_KERNEL),
		128*1024, 128*1024);
	if (rv) {
		dev_dbg(priv->dev,
			"%s: snd_pcm_lib_preallocate_pages_for_all  failed\n",
			DRV_NAME);
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

static int mpc52xx_ac97_setup_mixer(struct mpc52xx_ac97_priv *priv)
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

static int mpc52xx_ac97_hwinit(struct mpc52xx_ac97_priv *priv)
{
	/* Reset everything first by safety */
	out_8(&priv->psc->command,MPC52xx_PSC_RST_RX);
	out_8(&priv->psc->command,MPC52xx_PSC_RST_TX);
	out_8(&priv->psc->command,MPC52xx_PSC_RST_ERR_STAT);

	/* Do a cold reset of codec */
	/*
	 * Note: This could interfere with some external AC97 mixers, as it
	 * could switch them into test mode, when SYNC or SDATA_OUT are not
	 * low while RES is low!
	 */
	out_8(&priv->psc->op1, 0x02);
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
	out_8(&priv->fifo->rfcntl, 0x07);
	out_8(&priv->fifo->tfcntl, 0x07);
	out_be16(&priv->fifo->rfalarm, 0x80);
	out_be16(&priv->fifo->tfalarm, 0x80);

	/* Go */
	out_8(&priv->psc->command,MPC52xx_PSC_TX_ENABLE);
	out_8(&priv->psc->command,MPC52xx_PSC_RX_ENABLE);

	return 0;
}

static int mpc52xx_ac97_hwshutdown(struct mpc52xx_ac97_priv *priv)
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
	int tx_initiator;
	int rv;
	const unsigned int *devno;

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
		dev_err(&op->dev, "%s: request_mem_region failed\n", DRV_NAME);
		rv = -EBUSY;
		goto err_early;
	}

	priv->psc = ioremap(priv->mem_start, priv->mem_len);
	if (!priv->psc) {
		dev_err(&op->dev, "%s: ioremap failed\n", DRV_NAME);
		rv = -ENOMEM;
		goto err_iomap;
	}
	/* the fifo starts right after psc ends */
	priv->fifo = (struct mpc52xx_psc_fifo*)&priv->psc[1];	/* FIXME */

	priv->irq = irq_of_parse_and_map(dn, 0);
	if (priv->irq == NO_IRQ) {
		dev_err(&op->dev, "%s: irq_of_parse_and_map failed\n",
			DRV_NAME);
		rv = -EBUSY;
		goto err_irqmap;
	}

	/* Setup Bestcomm tasks */
	spin_lock_init(&priv->dma_lock);

	/*
	 * PSC1 or PSC2 can be configured for AC97 usage. Select the right
	 * channel, to let the BCOMM unit does its job correctly.
	 */
	devno = of_get_property(dn, "cell-index", NULL);
	switch (*devno) {
	case 0:	/* PSC1 */
		tx_initiator = 14;
		break;
	case 1:	/* PSC2 */
		tx_initiator = 12;
		break;
	default:
		dev_dbg(priv->dev, "Unknown PSC unit for AC97 usage!\n");
		rv = -ENODEV;
		goto err_irq;
	}

	priv->tsk_tx = bcom_gen_bd_tx_init(AC97_TX_NUM_BD,
			priv->mem_start + sizeof(struct mpc52xx_psc) +
				offsetof(struct mpc52xx_psc_fifo, tfdata),
			tx_initiator,
			2);	/* ipr : FIXME */
	if (!priv->tsk_tx) {
		dev_err(&op->dev, "%s: bcom_gen_bd_tx_init failed\n",
			DRV_NAME);
		rv = -ENOMEM;
		goto err_bcomm;
	}

	/* Low level HW Init */
	mpc52xx_ac97_hwinit(priv);

	/* Request IRQ now that we're 'stable' */
	rv = request_irq(priv->irq, mpc52xx_ac97_irq, 0, DRV_NAME, priv);
	if (rv < 0) {
		dev_err(&op->dev, "%s: request_irq failed\n", DRV_NAME);
		goto err_irqreq;
	}

	rv = request_irq(bcom_get_task_irq(priv->tsk_tx),
				mpc52xx_ac97_tx_irq, 0, DRV_NAME "_tx", priv);
	if (rv < 0) {
		dev_err(&op->dev, "%s: request_irq failed\n", DRV_NAME);
		goto err_txirqreq;
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
		dev_err(&op->dev, "%s: snd_card_register failed\n", DRV_NAME);
		goto err_late;
	}

	dev_set_drvdata(&op->dev, priv);

	return 0;

err_late:
	free_irq(bcom_get_task_irq(priv->tsk_tx), priv);
err_txirqreq:
	free_irq(priv->irq, priv);
err_irqreq:
	bcom_gen_bd_tx_release(priv->tsk_tx);
err_bcomm:
	mpc52xx_ac97_hwshutdown(priv);
err_irq:
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

static int mpc52xx_ac97_remove(struct of_device *op)
{
	struct mpc52xx_ac97_priv *priv;

	dev_dbg(&op->dev, "removing MPC52xx PSC AC97 driver\n");

	priv = dev_get_drvdata(&op->dev);
	if (priv) {
		/* Sound subsys shutdown */
		snd_card_free(priv->card);

		/* Low level HW shutdown */
		mpc52xx_ac97_hwshutdown(priv);

		/* Release bestcomm tasks */
		free_irq(bcom_get_task_irq(priv->tsk_tx), priv);
		bcom_gen_bd_tx_release(priv->tsk_tx);

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
		.type		= "sound",
		.compatible	= "mpc5200b-psc-ac97",	/* B only for now */
	}, { }
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

static int __init mpc52xx_ac97_init(void)
{
	int rv;

	printk(KERN_INFO "Sound: MPC52xx PSC AC97 driver\n");

	rv = of_register_platform_driver(&mpc52xx_ac97_of_driver);
	if (rv) {
		printk(KERN_ERR DRV_NAME ": "
			"of_register_platform_driver failed (%i)\n", rv);
		return rv;
	}

	return 0;
}

static void __exit mpc52xx_ac97_exit(void)
{
	of_unregister_platform_driver(&mpc52xx_ac97_of_driver);
}

module_init(mpc52xx_ac97_init);
module_exit(mpc52xx_ac97_exit);

MODULE_AUTHOR("Sylvain Munaut <tnt@246tNt.com>");
MODULE_DESCRIPTION(DRV_NAME ": Freescale MPC52xx PSC AC97 driver");
MODULE_LICENSE("GPL");

