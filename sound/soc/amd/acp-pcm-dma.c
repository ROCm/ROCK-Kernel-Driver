/*
 * AMD ALSA SoC PCM Driver
 *
 * Copyright 2014-2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pci.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "amd_acp.h"
#include "amd_gnb_bus.h"

#define PLAYBACK_MIN_NUM_PERIODS    2
#define PLAYBACK_MAX_NUM_PERIODS    2
#define PLAYBACK_MAX_PERIOD_SIZE    16384
#define PLAYBACK_MIN_PERIOD_SIZE    1024
#define CAPTURE_MIN_NUM_PERIODS     2
#define CAPTURE_MAX_NUM_PERIODS     2
#define CAPTURE_MAX_PERIOD_SIZE     16384
#define CAPTURE_MIN_PERIOD_SIZE     1024

#define NUM_DSCRS_PER_CHANNEL 2

#define MAX_BUFFER (PLAYBACK_MAX_PERIOD_SIZE * PLAYBACK_MAX_NUM_PERIODS)
#define MIN_BUFFER MAX_BUFFER

#define TWO_CHANNEL_SUPPORT     2	/* up to 2.0 */
#define FOUR_CHANNEL_SUPPORT    4	/* up to 3.1 */
#define SIX_CHANNEL_SUPPORT     6	/* up to 5.1 */
#define EIGHT_CHANNEL_SUPPORT   8	/* up to 7.1 */


static const struct snd_pcm_hardware acp_pcm_hardware_playback = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	/* formats,rates,channels  based on i2s doc. */
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 1,
	.channels_max = 8,
	.rates = SNDRV_PCM_RATE_8000_96000,
	.rate_min = 8000,
	.rate_max = 96000,
	.buffer_bytes_max = PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE,
	.period_bytes_min = PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max = PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min = PLAYBACK_MIN_NUM_PERIODS,
	.periods_max = PLAYBACK_MAX_NUM_PERIODS,
	.fifo_size = 0,
};

static const struct snd_pcm_hardware acp_pcm_hardware_capture = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BATCH |
	    SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	/* formats,rates,channels  based on i2s doc. */
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 1,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_8000_48000,
	.rate_min = 8000,
	.rate_max = 48000,
	.buffer_bytes_max = CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min = CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max = CAPTURE_MAX_PERIOD_SIZE,
	.periods_min = CAPTURE_MIN_NUM_PERIODS,
	.periods_max = CAPTURE_MAX_NUM_PERIODS,
	.fifo_size = 0,
};

struct audio_drv_data {
	struct snd_pcm_substream *play_stream;
	struct snd_pcm_substream *capture_stream;
	struct amd_acp_device *acp_dev;
	struct acp_irq_prv *iprv;
};

struct audio_substream_data {
	struct amd_acp_device *acp_dev;
	struct page *pg;
	struct acp_dma_config *dma_config;
	struct acp_i2s_config  *i2s_config;
	unsigned int order;
};

static const struct snd_soc_component_driver dw_i2s_component = {
	.name = "dw-i2s.0",
};

static void acp_pcm_period_elapsed(struct device *dev, u16 play_intr,
							u16 capture_intr)
{
	struct snd_pcm_substream *substream;
	struct audio_drv_data *irq_data =
	    (struct audio_drv_data *)dev_get_drvdata(dev);

	/* Inform ALSA about the period elapsed (one out of two periods) */
	if (play_intr)
		substream = irq_data->play_stream;
	else if (capture_intr)
		substream = irq_data->capture_stream;

	if (substream->runtime && snd_pcm_running(substream))
		snd_pcm_period_elapsed(substream);
}

static int acp_dma_open(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *prtd = substream->private_data;
	struct audio_drv_data *intr_data =
	    (struct audio_drv_data *)dev_get_drvdata(prtd->platform->dev);

	struct audio_substream_data *adata =
		kzalloc(sizeof(struct audio_substream_data), GFP_KERNEL);
	if (adata == NULL)
		return -ENOMEM;

	adata->dma_config =
			kzalloc(sizeof(struct acp_dma_config), GFP_KERNEL);
	if (adata->dma_config == NULL) {
		kfree(adata);
		return -ENOMEM;
	}

	adata->i2s_config =
			kzalloc(sizeof(struct acp_i2s_config), GFP_KERNEL);
	if (adata->i2s_config == NULL) {
		kfree(adata->dma_config);
		kfree(adata);
		return -ENOMEM;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = acp_pcm_hardware_playback;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		runtime->hw = acp_pcm_hardware_capture;
	else {
		pr_err("Error in stream type\n");
		return -EINVAL;
	}

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		pr_err("snd_pcm_hw_constraint_integer failed\n");
		return ret;
	}

	adata->acp_dev = intr_data->acp_dev;
	runtime->private_data = adata;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		intr_data->play_stream = substream;
	else
		intr_data->capture_stream = substream;

	return 0;
}

static int acp_dma_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	int status;
	uint64_t size;
	struct snd_dma_buffer *dma_buffer;
	struct page *pg;
	u16 num_of_pages;

	struct snd_pcm_runtime *runtime;
	struct audio_substream_data *rtd;
	struct amd_acp_device *acp_dev;

	if (WARN_ON(!substream))
		return -EINVAL;

	dma_buffer = &substream->dma_buffer;

	runtime = substream->runtime;
	rtd = runtime->private_data;

	if (WARN_ON(!rtd))
		return -EINVAL;
	acp_dev = rtd->acp_dev;

	size = params_buffer_bytes(params);
	status = snd_pcm_lib_malloc_pages(substream, size);
	if (status < 0)
		return status;

	memset(substream->runtime->dma_area, 0, params_buffer_bytes(params));
	pg = virt_to_page(substream->dma_buffer.area);

	if (NULL != pg) {
		/* Save for runtime private data */
		rtd->pg = pg;
		rtd->order = get_order(size);

		/*Let ACP know the Allocated memory */
		num_of_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;

		/* Fill the page table entries in ACP SRAM */

		rtd->dma_config->pg = pg;
		rtd->dma_config->size = size;
		rtd->dma_config->num_of_pages = num_of_pages;
		rtd->dma_config->direction = substream->stream;

		acp_dev->config_dma(acp_dev, rtd->dma_config);

		status = 0;
	} else {
		status = -ENOMEM;
	}
	return status;
}

static int acp_dma_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static snd_pcm_uframes_t acp_dma_pointer(struct snd_pcm_substream *substream)
{
	u32 pos = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;
	struct amd_acp_device *acp_dev = rtd->acp_dev;

	pos = acp_dev->update_dma_pointer(acp_dev, substream->stream,
				frames_to_bytes(runtime, runtime->period_size));
	return bytes_to_frames(runtime, pos);

}

static int acp_dma_mmap(struct snd_pcm_substream *substream,
			struct vm_area_struct *vma)
{
	return snd_pcm_lib_default_mmap(substream, vma);
}

static int acp_dma_prepare(struct snd_pcm_substream *substream)
{
	int ret;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;
	struct amd_acp_device *acp_dev = rtd->acp_dev;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {

		acp_dev->config_dma_channel(acp_dev, SYSRAM_TO_ACP_CH_NUM,
					PLAYBACK_START_DMA_DESCR_CH12,
					NUM_DSCRS_PER_CHANNEL, 0);
		acp_dev->config_dma_channel(acp_dev, ACP_TO_I2S_DMA_CH_NUM,
					PLAYBACK_START_DMA_DESCR_CH13,
					NUM_DSCRS_PER_CHANNEL, 0);
		/* Fill ACP SRAM with zeros from System RAM which is zero-ed
		 * in hw_params */
		ret = acp_dev->dma_start(rtd->acp_dev,
						SYSRAM_TO_ACP_CH_NUM, false);
		if (ret < 0)
			ret = -EFAULT;

		/* Now configure DMA to transfer only first half of System RAM
		 * buffer before playback is triggered. This will overwrite
		 * zero-ed second half of SRAM buffer */
		acp_dev->config_dma_channel(acp_dev, SYSRAM_TO_ACP_CH_NUM,
					PLAYBACK_START_DMA_DESCR_CH12,
					1, 0);
	} else {
		acp_dev->config_dma_channel(acp_dev, ACP_TO_SYSRAM_CH_NUM,
					CAPTURE_START_DMA_DESCR_CH14,
					NUM_DSCRS_PER_CHANNEL, 0);
		acp_dev->config_dma_channel(acp_dev, I2S_TO_ACP_DMA_CH_NUM,
					CAPTURE_START_DMA_DESCR_CH15,
					NUM_DSCRS_PER_CHANNEL, 0);
	}
	return 0;
}

static int acp_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;
	struct amd_acp_device *acp_dev = rtd->acp_dev;

	int ret = -EIO;

	if (!rtd)
		return -EINVAL;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			ret = acp_dev->dma_start(rtd->acp_dev,
						SYSRAM_TO_ACP_CH_NUM, false);
			if (ret < 0)
				ret = -EFAULT;
			else
				acp_dev->prebuffer_audio(rtd->acp_dev);

			ret = acp_dev->dma_start(acp_dev,
					    ACP_TO_I2S_DMA_CH_NUM, true);
		} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			ret = acp_dev->dma_start(acp_dev,
					    I2S_TO_ACP_DMA_CH_NUM, true);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			ret = acp_dev->dma_stop(acp_dev, SYSRAM_TO_ACP_CH_NUM);
			if (0 == ret)
				ret = acp_dev->dma_stop(acp_dev,
						   ACP_TO_I2S_DMA_CH_NUM);
		} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
			ret = acp_dev->dma_stop(acp_dev, I2S_TO_ACP_DMA_CH_NUM);
			if (0 == ret)
				ret = acp_dev->dma_stop(acp_dev,
						ACP_TO_SYSRAM_CH_NUM);
		}
		break;
	default:
		ret = -EINVAL;

	}
	return ret;
}

static int acp_dma_new(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_pcm *pcm;

	pcm = rtd->pcm;
	ret = snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					NULL, MIN_BUFFER, MAX_BUFFER);
	return ret;
}

static int acp_dma_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;

	kfree(rtd->dma_config);
	kfree(rtd->i2s_config);
	kfree(rtd);

	return 0;
}

static int acp_dai_i2s_hwparams(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *prtd = substream->private_data;

	struct audio_substream_data *rtd = runtime->private_data;
	struct amd_acp_device *acp_dev = rtd->acp_dev;
	struct device *dev = prtd->platform->dev;

	u32 chan_nr;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		rtd->i2s_config->xfer_resolution = 0x02;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		rtd->i2s_config->xfer_resolution = 0x04;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		rtd->i2s_config->xfer_resolution = 0x05;
		break;

	default:
		dev_err(dev, "designware-i2s: unsuppted PCM fmt");
		return -EINVAL;
	}

	chan_nr = params_channels(params);

	switch (chan_nr) {
	case EIGHT_CHANNEL_SUPPORT:
		rtd->i2s_config->ch_reg = 3;
		break;
	case SIX_CHANNEL_SUPPORT:
		rtd->i2s_config->ch_reg = 2;
		break;
	case FOUR_CHANNEL_SUPPORT:
		rtd->i2s_config->ch_reg = 1;
		break;
	case TWO_CHANNEL_SUPPORT:
		rtd->i2s_config->ch_reg = 0;
		break;
	default:
		dev_err(dev, "channel not supported\n");
		return -EINVAL;
	}

	rtd->i2s_config->direction = substream->stream;

	acp_dev->config_i2s(acp_dev, rtd->i2s_config);

	return 0;
}

static int acp_dai_i2s_trigger(struct snd_pcm_substream *substream,
			       int cmd, struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;
	struct amd_acp_device *acp_dev = rtd->acp_dev;

	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		acp_dev->i2s_start(acp_dev, substream->stream);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		acp_dev->i2s_stop(acp_dev, substream->stream);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int acp_dai_i2s_prepare(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;
	struct amd_acp_device *acp_dev = rtd->acp_dev;

	acp_dev->i2s_reset(acp_dev, substream->stream);
	return 0;
}

static struct snd_pcm_ops acp_dma_ops = {
	.open = acp_dma_open,
	.close = acp_dma_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = acp_dma_hw_params,
	.hw_free = acp_dma_hw_free,
	.trigger = acp_dma_trigger,
	.pointer = acp_dma_pointer,
	.mmap = acp_dma_mmap,
	.prepare = acp_dma_prepare,
};

struct snd_soc_dai_ops acp_dai_i2s_ops = {
	.prepare = acp_dai_i2s_prepare,
	.hw_params = acp_dai_i2s_hwparams,
	.trigger = acp_dai_i2s_trigger,
};

/* CZ i2s configuration */
static struct snd_soc_dai_driver i2s_dai_driver_cz = {
	.playback = {
		     .stream_name = "I2S Playback",
		     .channels_min = 2,
		     .channels_max = 2,
		     .rates = SNDRV_PCM_RATE_8000_96000,
		     .formats = SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S32_LE,

		     .rate_min = 8000,
		     .rate_max = 96000,
		     },
	.capture = {
		    .stream_name = "I2S Capture",
		    .channels_min = 2,
		    .channels_max = 2,
		    .rates = SNDRV_PCM_RATE_8000_48000,
		    .formats = SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		    .rate_min = 8000,
		    .rate_max = 48000,
		    },
	.ops = &acp_dai_i2s_ops,
};

static struct snd_soc_platform_driver acp_asoc_platform = {
	.ops = &acp_dma_ops,
	.pcm_new = acp_dma_new,
};

static int acp_alsa_register(struct device *dev, struct amd_acp_device *acp_dev,
				struct amd_gnb_bus_dev *adev)
{
	int status;

	status = snd_soc_register_platform(dev, &acp_asoc_platform);
	if (STATUS_SUCCESS != status) {
		dev_err(dev, "Unable to register ALSA platform device\n");
		goto exit_platform;
	} else {
		 /* SNDRV_PCM_FMTBIT_S16_LE is not supported in CZ */
		status = snd_soc_register_component(dev,
					&dw_i2s_component,
					&i2s_dai_driver_cz, 1);

		if (STATUS_SUCCESS != status) {
			dev_err(dev, "Unable to register i2s dai\n");
			goto exit_dai;
		} else {
			dev_info(dev, "ACP device registered with ALSA\n");
			return status;
		}
	}

exit_dai:
	snd_soc_unregister_platform(dev);
exit_platform:
	acp_dev->fini(acp_dev);
	return status;
}

static int acp_amdsoc_probe(struct amd_gnb_bus_dev *adev)
{
	int status;
	struct audio_drv_data *audio_drv_data;
	struct amd_acp_device *acp_dev = adev->private_data;

	if (AMD_GNB_IP_ACP_PCM != adev->ip) {
		dev_err(&adev->dev, "Not an ACP Device on AMD GNB bus\n");
		return -ENODEV;
	}

	audio_drv_data = devm_kzalloc(&adev->dev,
						sizeof(struct audio_drv_data),
						GFP_KERNEL);
	if (audio_drv_data == NULL)
		return -ENOMEM;

	audio_drv_data->iprv = devm_kzalloc(&adev->dev,
						sizeof(struct acp_irq_prv),
						GFP_KERNEL);
	if (audio_drv_data->iprv == NULL)
		return -ENOMEM;

	/* The following members gets populated in device 'open'
	 * function. Till then interrupts are disabled in 'acp_hw_init'
	 * and device doesn't generate any interrupts.
	 */

	audio_drv_data->play_stream = NULL;
	audio_drv_data->capture_stream = NULL;
	audio_drv_data->acp_dev = acp_dev;

	audio_drv_data->iprv->dev = &adev->dev;
	audio_drv_data->iprv->acp_dev = acp_dev;
	audio_drv_data->iprv->set_elapsed = acp_pcm_period_elapsed;

	dev_set_drvdata(&adev->dev, audio_drv_data);

	/* Initialize the ACP */
	status = acp_dev->init(acp_dev, audio_drv_data->iprv);

	if (STATUS_SUCCESS == status)
		status = acp_alsa_register(&adev->dev, acp_dev, adev);
	else
		pr_err("ACP initialization Failed\n");

	return status;
}

static int acp_amdsoc_remove(struct amd_gnb_bus_dev *adev)
{
	struct amd_acp_device *acp_dev = adev->private_data;

	snd_soc_unregister_component(&adev->dev);
	snd_soc_unregister_platform(&adev->dev);

	acp_dev->fini(acp_dev);

	return 0;
}

static struct amd_gnb_bus_driver acp_dma_driver = {
	.name = "acp-pcm-driver",
	.ip = AMD_GNB_IP_ACP_PCM,
	.probe = acp_amdsoc_probe,
	.remove = acp_amdsoc_remove,
};

static int __init amdsoc_bus_acp_dma_driver_init(void)
{
	int ret = 0;

	ret = amd_gnb_bus_register_driver(&acp_dma_driver,
					 THIS_MODULE, "acp-pcm-driver");
	if (ret) {
		pr_err("ACP: Unable to register with AMD GNB BUS!\n");
		return ret;
	}

	return 0;

}

static void __exit amdsoc_bus_acp_dma_driver_exit(void)
{
	pr_info("ACP: PCM driver exit\n");
	amd_gnb_bus_unregister_driver(&acp_dma_driver);
}

module_init(amdsoc_bus_acp_dma_driver_init);
module_exit(amdsoc_bus_acp_dma_driver_exit);

MODULE_AUTHOR("Maruthi.Bayyavarapu@amd.com");
MODULE_DESCRIPTION("AMD ACP PCM Driver");
MODULE_LICENSE("GPL and additional rights");
