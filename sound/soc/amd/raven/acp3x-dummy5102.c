/*
 * Machine driver for AMD ACP Audio engine using dummy codec
 *
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

static int acp3x_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)

{
      struct snd_soc_pcm_runtime *rtd = substream->private_data;
      struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
      unsigned int fmt;
      unsigned int slot_width;
      unsigned int channels;
      int ret = 0;

      fmt = params_format(params);
      switch (fmt) {
	case SNDRV_PCM_FORMAT_S16_LE:
		slot_width = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		slot_width = 32;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
                slot_width = 32;
		break;
	default:
		printk(KERN_WARNING "acp3x: unsupported PCM format");
		return -EINVAL;
      }

      channels = params_channels(params);

      if (channels == 0x04) {
                ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0x3, 0x3, 4, slot_width);
                if (ret < 0)
                       return ret;
      } else {
               ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0x3, 0x3, 2, slot_width);
               if (ret < 0)
                       return ret;
      }
      return 0;
}

static struct snd_soc_ops acp3x_wm5102_ops = {
	.hw_params = acp3x_hw_params,
};

static int acp3x_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static struct snd_soc_dai_link acp3x_dai_w5102[] = {
	{
		.name = "RV-W5102-PLAY",
		.stream_name = "Playback",
		.platform_name = "acp3x_rv_i2s.0",
		.cpu_dai_name = "acp3x_rv_i2s.0",
		.codec_dai_name = "dummy_w5102_dai",
		.codec_name = "dummy_w5102.0",
                .dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.ops = &acp3x_wm5102_ops,
		.init = acp3x_init,
	},
};

static const struct snd_soc_dapm_widget acp3x_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_MIC("Analog Mic", NULL),
};

static const struct snd_soc_dapm_route acp3x_audio_route[] = {
	{"Headphones", NULL, "HPO L"},
	{"Headphones", NULL, "HPO R"},
	{"MIC1", NULL, "Analog Mic"},
};

static struct snd_soc_card acp3x_card = {
	.name = "acp3x",
	.owner = THIS_MODULE,
	.dai_link = acp3x_dai_w5102,
	.num_links = 1,
};

static int acp3x_probe(struct platform_device *pdev)
{
	int ret;
	struct acp_wm5102 *machine = NULL;
	struct snd_soc_card *card;

	card = &acp3x_card;
	acp3x_card.dev = &pdev->dev;

	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev,
				"snd_soc_register_card(%s) failed: %d\n",
				acp3x_card.name, ret);
		return ret;
	}
	return 0;
}

static int acp3x_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card;

	card = platform_get_drvdata(pdev);
	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver acp3x_mach_driver = {
	.driver = {
		.name = "acp3x_w5102_mach",
		.pm = &snd_soc_pm_ops,
	},
	.probe = acp3x_probe,
	.remove = acp3x_remove,
};

static int __init acp3x_audio_init(void)
{
	platform_driver_register(&acp3x_mach_driver);
	return 0;
}

static void __exit acp3x_audio_exit(void)
{
	platform_driver_unregister(&acp3x_mach_driver);
}

module_init(acp3x_audio_init);
module_exit(acp3x_audio_exit);

MODULE_AUTHOR("Maruthi.Bayyavarapu@amd.com");
MODULE_LICENSE("GPL v2");
