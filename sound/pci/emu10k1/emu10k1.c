/*
 *  The driver for the EMU10K1 (SB Live!) based soundcards
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
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/emu10k1.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("EMU10K1");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Creative Labs,SB Live!/PCI512/E-mu APS},"
	       "{Creative Labs,SB Audigy}}");

#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
#define ENABLE_SYNTH
#include <sound/emu10k1_synth.h>
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int extin[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
static int extout[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
static int seq_ports[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4};
static int max_synth_voices[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 64};
static int max_buffer_size[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 128};
static int enable_ir[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
static int boot_devs;

module_param_array(index, int, boot_devs, 0444);
MODULE_PARM_DESC(index, "Index value for the EMU10K1 soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
module_param_array(id, charp, boot_devs, 0444);
MODULE_PARM_DESC(id, "ID string for the EMU10K1 soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
module_param_array(enable, bool, boot_devs, 0444);
MODULE_PARM_DESC(enable, "Enable the EMU10K1 soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
module_param_array(extin, int, boot_devs, 0444);
MODULE_PARM_DESC(extin, "Available external inputs for FX8010. Zero=default.");
MODULE_PARM_SYNTAX(extin, SNDRV_ENABLED "allows:{{0,0x0ffff}},base:16");
module_param_array(extout, int, boot_devs, 0444);
MODULE_PARM_DESC(extout, "Available external outputs for FX8010. Zero=default.");
MODULE_PARM_SYNTAX(extout, SNDRV_ENABLED "allows:{{0,0x0ffff}},base:16");
module_param_array(seq_ports, int, boot_devs, 0444);
MODULE_PARM_DESC(seq_ports, "Allocated sequencer ports for internal synthesizer.");
MODULE_PARM_SYNTAX(seq_ports, SNDRV_ENABLED "allows:{{0,32}}");
module_param_array(max_synth_voices, int, boot_devs, 0444);
MODULE_PARM_DESC(max_synth_voices, "Maximum number of voices for WaveTable.");
MODULE_PARM_SYNTAX(max_synth_voices, SNDRV_ENABLED);
module_param_array(max_buffer_size, int, boot_devs, 0444);
MODULE_PARM_DESC(max_buffer_size, "Maximum sample buffer size in MB.");
MODULE_PARM_SYNTAX(max_buffer_size, SNDRV_ENABLED);
module_param_array(enable_ir, bool, boot_devs, 0444);
MODULE_PARM_DESC(enable_ir, "Enable IR.");
MODULE_PARM_SYNTAX(enable_ir, SNDRV_ENABLE_DESC);

static struct pci_device_id snd_emu10k1_ids[] = {
	{ 0x1102, 0x0002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* EMU10K1 */
#if 0 /* FIXME: not working! */
	{ 0x1102, 0x0006, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* Dell OEM version (EMU10K1) */
#endif
	{ 0x1102, 0x0004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1 },	/* Audigy */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_emu10k1_ids);

static int __devinit snd_card_emu10k1_probe(struct pci_dev *pci,
					    const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	emu10k1_t *emu;
#ifdef ENABLE_SYNTH
	snd_seq_device_t *wave = NULL;
#endif
	int err;

	if (dev >= SNDRV_CARDS)
        	return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	if (max_buffer_size[dev] < 32)
		max_buffer_size[dev] = 32;
	else if (max_buffer_size[dev] > 1024)
		max_buffer_size[dev] = 1024;
	if ((err = snd_emu10k1_create(card, pci, extin[dev], extout[dev],
				      (long)max_buffer_size[dev] * 1024 * 1024,
				      enable_ir[dev],
				      &emu)) < 0) {
		snd_card_free(card);
		return err;
	}		
	if ((err = snd_emu10k1_pcm(emu, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}		
	if ((err = snd_emu10k1_pcm_mic(emu, 1, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}		
	if ((err = snd_emu10k1_pcm_efx(emu, 2, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}		
	if ((err = snd_emu10k1_fx8010_pcm(emu, 3, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}		
	if ((err = snd_emu10k1_mixer(emu)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (emu->audigy) {
		if ((err = snd_emu10k1_audigy_midi(emu)) < 0) {
			snd_card_free(card);
			return err;
		}
	} else {
		if ((err = snd_emu10k1_midi(emu)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	if ((err = snd_emu10k1_fx8010_new(emu, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
#ifdef ENABLE_SYNTH
	if (snd_seq_device_new(card, 1, SNDRV_SEQ_DEV_ID_EMU10K1_SYNTH,
			       sizeof(snd_emu10k1_synth_arg_t), &wave) < 0 ||
	    wave == NULL) {
		snd_printk("can't initialize Emu10k1 wavetable synth\n");
	} else {
		snd_emu10k1_synth_arg_t *arg;
		arg = SNDRV_SEQ_DEVICE_ARGPTR(wave);
		strcpy(wave->name, "Emu-10k1 Synth");
		arg->hwptr = emu;
		arg->index = 1;
		arg->seq_ports = seq_ports[dev];
		arg->max_voices = max_synth_voices[dev];
	}
#endif
 
	if (emu->audigy && (emu->revision == 4) ) {
		strcpy(card->driver, "Audigy2");
		strcpy(card->shortname, "Sound Blaster Audigy2");
	} else if (emu->audigy) {
		strcpy(card->driver, "Audigy");
		strcpy(card->shortname, "Sound Blaster Audigy");
	} else if (emu->APS) {
		strcpy(card->driver, "E-mu APS");
		strcpy(card->shortname, "E-mu APS");
	} else {
		strcpy(card->driver, "EMU10K1");
		strcpy(card->shortname, "Sound Blaster Live!");
	}
	sprintf(card->longname, "%s (rev.%d) at 0x%lx, irq %i", card->shortname, emu->revision, emu->port, emu->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_card_emu10k1_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "EMU10K1_Audigy",
	.id_table = snd_emu10k1_ids,
	.probe = snd_card_emu10k1_probe,
	.remove = __devexit_p(snd_card_emu10k1_remove),
};

static int __init alsa_card_emu10k1_init(void)
{
	return pci_module_init(&driver);
}

static void __exit alsa_card_emu10k1_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_emu10k1_init)
module_exit(alsa_card_emu10k1_exit)
