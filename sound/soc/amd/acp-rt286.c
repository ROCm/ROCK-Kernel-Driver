/*
 * Machine driver for AMD ACP Audio engine using Realtek RT286 codec
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

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>

#include "../codecs/rt286.h"

#ifdef CONFIG_PINCTRL_AMD

#define CZ_HPJACK_GPIO  7
#define CZ_HPJACK_DEBOUNCE 150

#endif

#define CZ_CODEC_I2C_ADDR 0x1c
#define CZ_CODEC_I2C_ADAPTER_ID 3

struct i2c_client *i2c_client;

struct acp_rt286 {
	int gpio_hp_det;
};

static struct snd_soc_jack cz_jack;
static struct snd_soc_jack_pin cz_pins[] = {
	{
		.pin = "Analog Mic",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headphones",
		.mask = SND_JACK_HEADPHONE,
	},
};

static int carrizo_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	int sample_rate;
	int err;

	err = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
				SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM);
	if (err < 0) {
		dev_err(card->dev, "unable to set codec dai format\n");
		return err;
	}

	sample_rate = params_rate(params);

	err = snd_soc_dai_set_sysclk(codec_dai, RT286_SCLK_S_PLL, 24000000,
					SND_SOC_CLOCK_OUT);
	if (err < 0) {
		dev_err(card->dev, "unable to set codec dai clock\n");
		return err;
	}

	return 0;

}

static struct snd_soc_ops carrizo_rt286_ops = {
	.hw_params = carrizo_hw_params,
};

static int carrizo_init(struct snd_soc_pcm_runtime *rtd)
{
	/* TODO: check whether dapm widgets needs to be
	 * dsiconnected initially. */
	int ret;
	struct snd_soc_card *card;
	struct snd_soc_codec *codec;

	codec = rtd->codec;
	card = rtd->card;
	ret = snd_soc_card_jack_new(card, "Headset",
		SND_JACK_HEADSET, &cz_jack, cz_pins, ARRAY_SIZE(cz_pins));

	if (ret)
		return ret;

	rt286_mic_detect(codec, &cz_jack);
	return 0;
}


static struct snd_soc_dai_link carrizo_dai_rt286 = {
	.name = "amd-rt286",
	.stream_name = "RT286_AIF1",
	.platform_name = "acp_pcm_dev",
	.cpu_dai_name = "acp_pcm_dev",
	.codec_dai_name = "rt286-aif1",
	.codec_name = "rt286.3-001c",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM,
	.ignore_suspend = 1,
	.ops = &carrizo_rt286_ops,
	.init = carrizo_init,
};

static const struct snd_soc_dapm_widget cz_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_MIC("Analog Mic", NULL),
};

static const struct snd_soc_dapm_route cz_audio_route[] = {
	{"Headphones", NULL, "HPO L"},
	{"Headphones", NULL, "HPO R"},
	{"MIC1", NULL, "Analog Mic"},
};

static struct snd_soc_card carrizo_card = {
	.name = "acp-rt286",
	.owner = THIS_MODULE,
	.dai_link = &carrizo_dai_rt286,
	.num_links = 1,

	.dapm_widgets = cz_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cz_widgets),
	.dapm_routes = cz_audio_route,
	.num_dapm_routes = ARRAY_SIZE(cz_audio_route),
};

static int carrizo_probe(struct platform_device *pdev)
{
	int ret;
	struct acp_rt286 *machine;
	struct snd_soc_card *card;

	machine = devm_kzalloc(&pdev->dev, sizeof(struct acp_rt286),
				GFP_KERNEL);
	if (!machine)
		return -ENOMEM;

	card = &carrizo_card;
	carrizo_card.dev = &pdev->dev;

	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev,
				"snd_soc_register_card(%s) failed: %d\n",
				carrizo_card.name, ret);
		return ret;
	}
	return 0;
}

static int carrizo_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card;

	card = platform_get_drvdata(pdev);
	snd_soc_unregister_card(card);

	return 0;
}

static const struct acpi_device_id cz_audio_acpi_match[] = {
	{ "I2SC1002", 0 },
	{},
};

static struct platform_driver carrizo_pcm_driver = {
	.driver = {
		.name = "carrizo_i2s_audio",
		.acpi_match_table = ACPI_PTR(cz_audio_acpi_match),
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = carrizo_probe,
	.remove = carrizo_remove,
};

static int __init cz_audio_init(void)
{
	int ret;
	struct i2c_adapter *adapter;
	struct i2c_board_info cz_board_info;
	const char *codec_acpi_name = "rt288";

	adapter = i2c_get_adapter(CZ_CODEC_I2C_ADAPTER_ID);
	if (!adapter)
		return -ENODEV;

	memset(&cz_board_info, 0, sizeof(struct i2c_board_info));
	cz_board_info.addr = CZ_CODEC_I2C_ADDR;
	strlcpy(cz_board_info.type, codec_acpi_name, I2C_NAME_SIZE);

#ifdef CONFIG_PINCTRL_AMD
	if (gpio_is_valid(CZ_HPJACK_GPIO)) {
		ret = gpio_request_one(CZ_HPJACK_GPIO, GPIOF_DIR_IN |
						GPIOF_EXPORT, "hp-gpio");
		if (ret != 0)
			pr_err("gpio_request_one failed : err %d\n", ret);

		cz_board_info.irq = gpio_to_irq(CZ_HPJACK_GPIO);

		gpio_set_debounce(CZ_HPJACK_GPIO, CZ_HPJACK_DEBOUNCE);
	}
#endif
	i2c_client = i2c_new_device(adapter, &cz_board_info);
	i2c_put_adapter(adapter);
	if (!i2c_client)
		return -ENODEV;

	platform_driver_register(&carrizo_pcm_driver);
	return 0;
}

static void __exit cz_audio_exit(void)
{
#ifdef CONFIG_PINCTRL_AMD
	if (gpio_is_valid(CZ_HPJACK_GPIO))
		gpio_free(CZ_HPJACK_GPIO);
#endif
	i2c_unregister_device(i2c_client);

	platform_driver_unregister(&carrizo_pcm_driver);
}

module_init(cz_audio_init);
module_exit(cz_audio_exit);

MODULE_AUTHOR("Maruthi.Bayyavarapu@amd.com");
MODULE_DESCRIPTION("CZ-rt288 Audio Support");
MODULE_LICENSE("GPL and additional rights");
