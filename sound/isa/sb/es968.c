
/*
    card-es968.c - driver for ESS AudioDrive ES968 based soundcards.
    Copyright (C) 1999 by Massimo Piccioni <dafastidio@libero.it>

    Thanks to Pierfrancesco 'qM2' Passerini.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
*/

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/pnp.h>
#include <sound/core.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include <sound/sb.h>

#define chip_t sb_t

#define PFX "es968: "

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("ESS AudioDrive ES968");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{ESS,AudioDrive ES968}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* Pnp setup */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for es968 based soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for es968 based soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable es968 based soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for es968 driver.");
MODULE_PARM_SYNTAX(port, SNDRV_PORT12_DESC);
MODULE_PARM(irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(irq, "IRQ # for es968 driver.");
MODULE_PARM_SYNTAX(irq, SNDRV_IRQ_DESC);
MODULE_PARM(dma8, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma8, "8-bit DMA # for es968 driver.");
MODULE_PARM_SYNTAX(dma8, SNDRV_DMA8_DESC);

struct snd_card_es968 {
	struct pnp_dev *dev;
};

static struct pnp_card_device_id snd_es968_pnpids[] = {
	{ .id = "ESS0968", .devs = { { "@@@0968" }, } },
	{ .id = "", } /* end */
};

MODULE_DEVICE_TABLE(pnp_card, snd_es968_pnpids);

#define	DRIVER_NAME	"snd-card-es968"

static irqreturn_t snd_card_es968_interrupt(int irq, void *dev_id,
					    struct pt_regs *regs)
{
	sb_t *chip = snd_magic_cast(sb_t, dev_id, return IRQ_NONE);

	if (chip->open & SB_OPEN_PCM) {
		return snd_sb8dsp_interrupt(chip);
	} else {
		return snd_sb8dsp_midi_interrupt(chip);
	}
}

static int __devinit snd_card_es968_pnp(int dev, struct snd_card_es968 *acard,
					struct pnp_card_link *card,
					const struct pnp_card_device_id *id)
{
	struct pnp_dev *pdev;
	struct pnp_resource_table *cfg = kmalloc(sizeof(*cfg), GFP_KERNEL);
	int err;
	if (!cfg)
		return -ENOMEM;
	acard->dev = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (acard->dev == NULL) {
		kfree(cfg);
		return -ENODEV;
	}

	pdev = acard->dev;

	pnp_init_resource_table(cfg);

	/* override resources */
	if (port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[0], port[dev], 16);
	if (dma8[dev] != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[0], dma8[dev], 1);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		pnp_resource_change(&cfg->irq_resource[0], irq[dev], 1);
	if ((pnp_manual_config_dev(pdev, cfg, 0)) < 0)
		snd_printk(KERN_ERR PFX "AUDIO the requested resources are invalid, using auto config\n");
	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR PFX "AUDIO pnp configure failure\n");
		kfree(cfg);
		return err;
	}
	port[dev] = pnp_port_start(pdev, 0);
	dma8[dev] = pnp_dma(pdev, 1);
	irq[dev] = pnp_irq(pdev, 0);

	kfree(cfg);
	return 0;
}

static int __init snd_card_es968_probe(int dev,
					struct pnp_card_link *pcard,
					const struct pnp_card_device_id *pid)
{
	int error;
	sb_t *chip;
	snd_card_t *card;
	struct snd_card_es968 *acard;

	if ((card = snd_card_new(index[dev], id[dev], THIS_MODULE,
				 sizeof(struct snd_card_es968))) == NULL)
		return -ENOMEM;
	acard = (struct snd_card_es968 *)card->private_data;
	if ((error = snd_card_es968_pnp(dev, acard, pcard, pid))) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sbdsp_create(card, port[dev],
				      irq[dev],
				      snd_card_es968_interrupt,
				      dma8[dev],
				      -1,
				      SB_HW_AUTO, &chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sb8dsp_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sbmixer_new(chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sb8dsp_midi(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}

	strcpy(card->driver, "ES968");
	strcpy(card->shortname, "ESS ES968");
	sprintf(card->longname, "%s soundcard, %s at 0x%lx, irq %d, dma %d",
		card->shortname, chip->name, chip->port, irq[dev], dma8[dev]);

	if ((error = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return error;
	}
	pnp_set_card_drvdata(pcard, card);
	return 0;
}

static int __devinit snd_es968_pnp_detect(struct pnp_card_link *card,
                                          const struct pnp_card_device_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		res = snd_card_es968_probe(dev, card, id);
		if (res < 0)
			return res;
		dev++;
		return 0;
        }
        return -ENODEV;
}

static void __devexit snd_es968_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_t *card = (snd_card_t *) pnp_get_card_drvdata(pcard);

	snd_card_disconnect(card);
	snd_card_free_in_thread(card);
}

static struct pnp_card_driver es968_pnpc_driver = {
	.flags		= PNP_DRIVER_RES_DISABLE,
	.name		= "es968",
	.id_table	= snd_es968_pnpids,
	.probe		= snd_es968_pnp_detect,
	.remove		= __devexit_p(snd_es968_pnp_remove),
};

static int __init alsa_card_es968_init(void)
{
	int res = pnp_register_card_driver(&es968_pnpc_driver);
	if (res == 0)
	{
		pnp_unregister_card_driver(&es968_pnpc_driver);
#ifdef MODULE
		snd_printk(KERN_ERR "no ES968 based soundcards found\n");
#endif
	}
	return res < 0 ? res : 0;
}

static void __exit alsa_card_es968_exit(void)
{
	pnp_unregister_card_driver(&es968_pnpc_driver);
}

module_init(alsa_card_es968_init)
module_exit(alsa_card_es968_exit)

#ifndef MODULE

/* format is: snd-es968=enable,index,id,
			port,irq,dma1 */

static int __init alsa_card_es968_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&port[nr_dev]) == 2 &&
	       get_option(&str,&irq[nr_dev]) == 2 &&
	       get_option(&str,&dma8[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-es968=", alsa_card_es968_setup);

#endif /* ifndef MODULE */
