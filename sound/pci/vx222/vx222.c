/*
 * Driver for Digigram VX222 V2/Mic PCI soundcards
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
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
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <sound/core.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include "vx222.h"

#define chip_t vx_core_t

#define CARD_NAME "VX222"

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("Digigram VX222 V2/Mic");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Digigram," CARD_NAME "}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int mic[SNDRV_CARDS]; /* microphone */
static int ibl[SNDRV_CARDS]; /* microphone */

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for Digigram " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for Digigram " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable Digigram " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(mic, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(mic, "Enable Microphone.");
MODULE_PARM_SYNTAX(mic, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);
MODULE_PARM(ibl, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(ibl, "Capture IBL size.");
MODULE_PARM_SYNTAX(ibl, SNDRV_ENABLED);

/*
 */

enum {
	VX_PCI_VX222_OLD,
	VX_PCI_VX222_NEW
};

static struct pci_device_id snd_vx222_ids[] = {
	{ 0x10b5, 0x9050, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VX_PCI_VX222_OLD, },   /* PLX */
	{ 0x10b5, 0x9030, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VX_PCI_VX222_NEW, },   /* PLX */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_vx222_ids);


/*
 */

static struct snd_vx_hardware vx222_old_hw = {

	.name = "VX222/Old",
	.type = VX_TYPE_BOARD,
	/* hw specs */
	.num_codecs = 1,
	.num_ins = 1,
	.num_outs = 1,
	.output_level_max = VX_ANALOG_OUT_LEVEL_MAX,
};

static struct snd_vx_hardware vx222_v2_hw = {

	.name = "VX222/v2",
	.type = VX_TYPE_V2,
	/* hw specs */
	.num_codecs = 1,
	.num_ins = 1,
	.num_outs = 1,
	.output_level_max = VX2_AKM_LEVEL_MAX,
};

static struct snd_vx_hardware vx222_mic_hw = {

	.name = "VX222/Mic",
	.type = VX_TYPE_MIC,
	/* hw specs */
	.num_codecs = 1,
	.num_ins = 1,
	.num_outs = 1,
	.output_level_max = VX2_AKM_LEVEL_MAX,
};


/*
 */
static int snd_vx222_free(vx_core_t *chip)
{
	int i;
	struct snd_vx222 *vx = (struct snd_vx222 *)chip;

	if (chip->irq >= 0)
		free_irq(chip->irq, (void*)chip);
	for (i = 0; i < 2; i++) {
		if (vx->port_res[i]) {
			release_resource(vx->port_res[i]);
			kfree_nocheck(vx->port_res[i]);
		}
	}
	snd_magic_kfree(chip);
	return 0;
}

static int snd_vx222_dev_free(snd_device_t *device)
{
	vx_core_t *chip = snd_magic_cast(vx_core_t, device->device_data, return -ENXIO);
	return snd_vx222_free(chip);
}


static int __devinit snd_vx222_create(snd_card_t *card, struct pci_dev *pci,
				      struct snd_vx_hardware *hw,
				      struct snd_vx222 **rchip)
{
	vx_core_t *chip;
	struct snd_vx222 *vx;
	int i, err;
	static snd_device_ops_t ops = {
		.dev_free =	snd_vx222_dev_free,
	};
	struct snd_vx_ops *vx_ops;

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	pci_set_master(pci);

	vx_ops = hw->type == VX_TYPE_BOARD ? &vx222_old_ops : &vx222_ops;
	chip = snd_vx_create(card, hw, vx_ops,
			     sizeof(struct snd_vx222) - sizeof(vx_core_t));
	if (! chip)
		return -ENOMEM;
	vx = (struct snd_vx222 *)chip;

	for (i = 0; i < 2; i++) {
		if (!(pci_resource_flags(pci, i + 1) & IORESOURCE_IO)) {
			snd_printk(KERN_ERR "invalid i/o resource %d\n", i + 1);
			snd_vx222_free(chip);
			return -ENOMEM;
		}
		vx->port[i] = pci_resource_start(pci, i + 1);
		if ((vx->port_res[i] = request_region(vx->port[i], 0x60,
						      CARD_NAME)) == NULL) {
			snd_printk(KERN_ERR "unable to grab port 0x%lx\n", vx->port[i]);
			snd_vx222_free(chip);
			return -EBUSY;
		}
	}

	if (request_irq(pci->irq, snd_vx_irq_handler, SA_INTERRUPT|SA_SHIRQ,
			CARD_NAME, (void *) chip)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		snd_vx222_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_vx222_free(chip);
		return err;
	}

	*rchip = vx;
	return 0;
}


static int __devinit snd_vx222_probe(struct pci_dev *pci,
				     const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	struct snd_vx_hardware *hw;
	struct snd_vx222 *vx;
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

	switch ((int)pci_id->driver_data) {
	case VX_PCI_VX222_OLD:
		hw = &vx222_old_hw;
		break;
	case VX_PCI_VX222_NEW:
	default:
		if (mic[dev])
			hw = &vx222_mic_hw;
		else
			hw = &vx222_v2_hw;
		break;
	}
	if ((err = snd_vx222_create(card, pci, hw, &vx)) < 0) {
		snd_card_free(card);
		return err;
	}
	vx->core.ibl.size = ibl[dev];

	sprintf(card->longname, "%s at 0x%lx & 0x%lx, irq %i",
		card->shortname, vx->port[0], vx->port[1], vx->core.irq);
	snd_printdd("%s at 0x%lx & 0x%lx, irq %i\n",
		    card->shortname, vx->port[0], vx->port[1], vx->core.irq);

	if ((err = snd_vx_hwdep_new(&vx->core)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_vx222_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "Digigram VX222",
	.id_table = snd_vx222_ids,
	.probe = snd_vx222_probe,
	.remove = __devexit_p(snd_vx222_remove),
};

static int __init alsa_card_vx222_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		printk(KERN_ERR "Digigram VX222 soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_vx222_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_vx222_init)
module_exit(alsa_card_vx222_exit)

#ifndef MODULE

/* format is: snd-vx222=enable,index,id */

static int __init alsa_card_vx222_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-vx222=", alsa_card_vx222_setup);

#endif /* ifndef MODULE */
