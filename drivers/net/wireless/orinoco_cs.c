/* orinoco_cs.c 0.11b	- (formerly known as dldwd_cs.c)
 *
 * A driver for "Hermes" chipset based PCMCIA wireless adaptors, such
 * as the Lucent WavelanIEEE/Orinoco cards and their OEM (Cabletron/
 * EnteraSys RoamAbout 802.11, ELSA Airlancer, Melco Buffalo and others).
 * It should also be usable on various Prism II based cards such as the
 * Linksys, D-Link and Farallon Skyline. It should also work on Symbol
 * cards such as the 3Com AirConnect and Ericsson WLAN.
 * 
 * Copyright notice & release notes in file orinoco.c
 */

#include <linux/config.h>
#ifdef  __IN_PCMCIA_PACKAGE__
#include <pcmcia/k_compat.h>
#endif /* __IN_PCMCIA_PACKAGE__ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/list.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include <pcmcia/bus_ops.h>

#include "hermes.h"
#include "orinoco.h"

/*====================================================================*/

static char version[] __initdata = "orinoco_cs.c 0.11b (David Gibson <hermes@gibson.dropbear.id.au> and others)";

MODULE_AUTHOR("David Gibson <hermes@gibson.dropbear.id.au>");
MODULE_DESCRIPTION("Driver for PCMCIA Lucent Orinoco, Prism II based and similar wireless cards");
#ifdef MODULE_LICENSE
MODULE_LICENSE("Dual MPL/GPL");
#endif

/* Parameters that can be set with 'insmod' */

/* The old way: bit map of interrupts to choose from */
/* This means pick from 15, 14, 12, 11, 10, 9, 7, 5, 4, and 3 */
static uint irq_mask = 0xdeb8;
/* Newer, simpler way of listing specific interrupts */
static int irq_list[4] = { -1 };
/* Do a Pcmcia soft reset (may help some cards) */
static int reset_cor = -1;
/* Some D-Link cards have buggy CIS. They do work at 5v properly, but
 * don't have any CIS entry for it. This workaround it... */
static int ignore_cis_vcc; /* = 0 */

MODULE_PARM(irq_mask, "i");
MODULE_PARM(irq_list, "1-4i");
MODULE_PARM(reset_cor, "i");
MODULE_PARM(ignore_cis_vcc, "i");

/* Pcmcia specific structure */
struct orinoco_pccard {
	dev_link_t link;
	dev_node_t node;
};

/*
 * Function prototypes
 */

/* struct net_device methods */
static int orinoco_cs_open(struct net_device *dev);
static int orinoco_cs_stop(struct net_device *dev);

/* PCMCIA gumpf */
static void orinoco_cs_config(dev_link_t * link);
static void orinoco_cs_release(u_long arg);
static int orinoco_cs_event(event_t event, int priority,
		       event_callback_args_t * args);

static dev_link_t *orinoco_cs_attach(void);
static void orinoco_cs_detach(dev_link_t *);

/*
   The dev_info variable is the "key" that is used to match up this
   device driver with appropriate cards, through the card configuration
   database.
*/
static dev_info_t dev_info = "orinoco_cs";

/*
   A linked list of "instances" of the dummy device.  Each actual
   PCMCIA card corresponds to one device instance, and is described
   by one dev_link_t structure (defined in ds.h).

   You may not want to use a linked list for this -- for example, the
   memory card driver uses an array of dev_link_t pointers, where minor
   device numbers are used to derive the corresponding array index.
*/

static dev_link_t *dev_list; /* = NULL */

/*====================================================================*/

static void
cs_error(client_handle_t handle, int func, int ret)
{
	error_info_t err = { func, ret };
	CardServices(ReportError, handle, &err);
}

static int
orinoco_cs_open(struct net_device *dev)
{
	struct orinoco_private *priv = (struct orinoco_private *)dev->priv;
	struct orinoco_pccard* card = (struct orinoco_pccard *)priv->card;
	dev_link_t *link = &card->link;
	int err;
	
	TRACE_ENTER(dev->name);

	link->open++;
	netif_device_attach(dev);
	
	err = orinoco_reset(priv);
	if (err)
		orinoco_cs_stop(dev);
	else
		netif_start_queue(dev);

	TRACE_EXIT(dev->name);

	return err;
}

static int
orinoco_cs_stop(struct net_device *dev)
{
	struct orinoco_private *priv = (struct orinoco_private *)dev->priv;
	struct orinoco_pccard* card = (struct orinoco_pccard *)priv->card;
	dev_link_t *link = &card->link;

	TRACE_ENTER(dev->name);

	netif_stop_queue(dev);

	orinoco_shutdown(priv);
	
	link->open--;

	if (link->state & DEV_STALE_CONFIG)
		mod_timer(&link->release, jiffies + HZ/20);
	
	TRACE_EXIT(dev->name);
	
	return 0;
}

/*
 * Do a soft reset of the Pcmcia card using the Configuration Option Register
 * Can't do any harm, and actually may do some good on some cards...
 * In fact, this seem necessary for Spectrum cards...
 */
static int
orinoco_cs_cor_reset(struct orinoco_private *priv)
{
	struct orinoco_pccard* card = (struct orinoco_pccard *)priv->card;
	dev_link_t *link = &card->link;
	conf_reg_t reg;
	u_int default_cor; 

	TRACE_ENTER(priv->ndev->name);

	/* Doing it if hardware is gone is guaranteed crash */
	if(! (link->state & DEV_CONFIG) )
		return -ENODEV;

	/* Save original COR value */
	reg.Function = 0;
	reg.Action = CS_READ;
	reg.Offset = CISREG_COR;
	reg.Value = 0;
	CardServices(AccessConfigurationRegister, link->handle, &reg);
	default_cor = reg.Value;

	DEBUG(2, "orinoco : orinoco_cs_cor_reset() : cor=0x%X\n", default_cor);

	/* Soft-Reset card */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = (default_cor | COR_SOFT_RESET);
	CardServices(AccessConfigurationRegister, link->handle, &reg);

	/* Wait until the card has acknowledged our reset */
	/* FIXME: mdelay() is deprecated -dgibson */
	mdelay(1);

#if 0 /* This seems to help on Symbol cards, but we're not sure why,
       and we don't know what it will do to other cards */
	reg.Action = CS_READ;
	reg.Offset = CISREG_CCSR;
	CardServices(AccessConfigurationRegister, link->handle, &reg);

	/* Write 7 (RUN) to CCSR, but preserve the original bit 4 */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_CCSR;
	reg.Value = 7 | (reg.Value & 0x10);
	CardServices(AccessConfigurationRegister, link->handle, &reg);
	mdelay(1);
#endif

	/* Restore original COR configuration index */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = (default_cor & ~COR_SOFT_RESET);
	CardServices(AccessConfigurationRegister, link->handle, &reg);

	/* Wait until the card has finished restarting */
	/* FIXME: mdelay() is deprecated -dgibson */
	mdelay(1);

	TRACE_EXIT(priv->ndev->name);

	return 0;
}

static int
orinoco_cs_hard_reset(struct orinoco_private *priv)
{
	if (! priv->broken_cor_reset)
		return orinoco_cs_cor_reset(priv);
	else
		return 0;

#if 0 /* We'd like to use ResetCard, but we can't for the moment - it sleeps */
	/* Not sure what the second parameter is supposed to be - the 
	   PCMCIA code doesn't actually use it */
	if (in_interrupt()) {
		printk("Not resetting card, in_interrupt() is true\n");
		return 0;
	} else {
		printk("Doing ResetCard\n");
		return CardServices(ResetCard, link->handle, NULL);
	}
#endif
}

/* Remove zombie instances (card removed, detach pending) */
static void
flush_stale_links(void)
{
	dev_link_t *link, *next;
	TRACE_ENTER("orinoco");
	for (link = dev_list; link; link = next) {
		next = link->next;
		if (link->state & DEV_STALE_LINK)
			orinoco_cs_detach(link);
	}
	TRACE_EXIT("orinoco");
}

/*======================================================================
  orinoco_cs_attach() creates an "instance" of the driver, allocating
  local data structures for one device.  The device is registered
  with Card Services.
  
  The dev_link structure is initialized, but we don't actually
  configure the card at this point -- we wait until we receive a
  card insertion event.
  ======================================================================*/

static dev_link_t *
orinoco_cs_attach(void)
{
	struct net_device *dev;
	struct orinoco_private *priv;
	struct orinoco_pccard *card;
	dev_link_t *link;
	client_reg_t client_reg;
	int ret, i;

	TRACE_ENTER("orinoco");
	/* A bit of cleanup */
	flush_stale_links();

	dev = alloc_orinocodev(sizeof(*card));
	if (! dev)
		return NULL;
	priv = dev->priv;
	card = priv->card;
	/* Overrides */
	dev->open = orinoco_cs_open;
	dev->stop = orinoco_cs_stop;
	priv->hard_reset = orinoco_cs_hard_reset;

	/* Link both structures together */
	link = &card->link;
	link->priv = priv;

	/* Initialize the dev_link_t structure */
	link->release.function = &orinoco_cs_release;
	link->release.data = (u_long) link;

	/* Interrupt setup */
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
	link->irq.IRQInfo1 = IRQ_INFO2_VALID | IRQ_LEVEL_ID;
	if (irq_list[0] == -1)
		link->irq.IRQInfo2 = irq_mask;
	else
		for (i = 0; i < 4; i++)
			link->irq.IRQInfo2 |= 1 << irq_list[i];
	link->irq.Handler = NULL;

	/*
	   General socket configuration defaults can go here.  In this
	   client, we assume very little, and rely on the CIS for almost
	   everything.  In most clients, many details (i.e., number, sizes,
	   and attributes of IO windows) are fixed by the nature of the
	   device, and can be hard-wired here.
	 */
	link->conf.Attributes = 0;
	link->conf.IntType = INT_MEMORY_AND_IO;

	/* Register with Card Services */
	link->next = dev_list;
	dev_list = link;
	client_reg.dev_info = &dev_info;
	client_reg.Attributes = INFO_IO_CLIENT | INFO_CARD_SHARE;
	client_reg.EventMask =
	    CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
	    CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
	    CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
	client_reg.event_handler = &orinoco_cs_event;
	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = link;
	ret = CardServices(RegisterClient, &link->handle, &client_reg);
	if (ret != CS_SUCCESS) {
		cs_error(link->handle, RegisterClient, ret);
		orinoco_cs_detach(link);
		link = NULL;
		goto out;
	}

 out:
	TRACE_EXIT("orinoco");
	return link;
}				/* orinoco_cs_attach */

/*======================================================================
  This deletes a driver "instance".  The device is de-registered
  with Card Services.  If it has been released, all local data
  structures are freed.  Otherwise, the structures will be freed
  when the device is released.
  ======================================================================*/

static void
orinoco_cs_detach(dev_link_t * link)
{
	dev_link_t **linkp;
	struct orinoco_private *priv = link->priv;
	struct net_device *dev = priv->ndev;

	TRACE_ENTER("orinoco");

	/* Locate device structure */
	for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
		if (*linkp == link)
			break;
	if (*linkp == NULL)
		goto out;

	/*
	   If the device is currently configured and active, we won't
	   actually delete it yet.  Instead, it is marked so that when
	   the release() function is called, that will trigger a proper
	   detach().
	 */
	if (link->state & DEV_CONFIG) {
#ifdef PCMCIA_DEBUG
		printk(KERN_DEBUG "orinoco_cs: detach postponed, '%s' "
		       "still locked\n", link->dev->dev_name);
#endif
		link->state |= DEV_STALE_LINK;
		goto out;
	}

	/* Break the link with Card Services */
	if (link->handle)
		CardServices(DeregisterClient, link->handle);

	/* Unlink device structure, and free it */
	*linkp = link->next;
	DEBUG(0, "orinoco_cs: detach: link=%p link->dev=%p\n", link, link->dev);
	if (link->dev) {
		DEBUG(0, "orinoco_cs: About to unregister net device %p\n",
		      priv->ndev);
		unregister_netdev(dev);
	}
	kfree(dev);

 out:
	TRACE_EXIT("orinoco");
}				/* orinoco_cs_detach */

/*======================================================================
  orinoco_cs_config() is scheduled to run after a CARD_INSERTION event
  is received, to configure the PCMCIA socket, and to make the
  device available to the system.
  ======================================================================*/

#define CS_CHECK(fn, args...) \
while ((last_ret=CardServices(last_fn=(fn),args))!=0) goto cs_failed

#define CFG_CHECK(fn, args...) \
if (CardServices(fn, args) != 0) goto next_entry

static void
orinoco_cs_config(dev_link_t * link)
{
	client_handle_t handle = link->handle;
	struct orinoco_private *priv = link->priv;
	struct orinoco_pccard *card = (struct orinoco_pccard *)priv->card;
	hermes_t *hw = &priv->hw;
	struct net_device *ndev = priv->ndev;
	tuple_t tuple;
	cisparse_t parse;
	int last_fn, last_ret;
	u_char buf[64];
	config_info_t conf;
	cistpl_cftable_entry_t dflt = { 0 };
	cisinfo_t info;

	TRACE_ENTER("orinoco");

	CS_CHECK(ValidateCIS, handle, &info);

	/*
	   This reads the card's CONFIG tuple to find its configuration
	   registers.
	 */
	tuple.DesiredTuple = CISTPL_CONFIG;
	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	CS_CHECK(GetFirstTuple, handle, &tuple);
	CS_CHECK(GetTupleData, handle, &tuple);
	CS_CHECK(ParseTuple, handle, &tuple, &parse);
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

	/* Configure card */
	link->state |= DEV_CONFIG;

	/* Look up the current Vcc */
	CS_CHECK(GetConfigurationInfo, handle, &conf);
	link->conf.Vcc = conf.Vcc;

	DEBUG(0, "orinoco_cs_config: ConfigBase = 0x%x link->conf.Vcc = %d\n", 
	      link->conf.ConfigBase, link->conf.Vcc);

	/*
	   In this loop, we scan the CIS for configuration table entries,
	   each of which describes a valid card configuration, including
	   voltage, IO window, memory window, and interrupt settings.

	   We make no assumptions about the card to be configured: we use
	   just the information available in the CIS.  In an ideal world,
	   this would work for any PCMCIA card, but it requires a complete
	   and accurate CIS.  In practice, a driver usually "knows" most of
	   these things without consulting the CIS, and most client drivers
	   will only use the CIS to fill in implementation-defined details.
	 */
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	CS_CHECK(GetFirstTuple, handle, &tuple);
	while (1) {
		cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
		CFG_CHECK(GetTupleData, handle, &tuple);
		CFG_CHECK(ParseTuple, handle, &tuple, &parse);

		DEBUG(0, "orinoco_cs_config: index = 0x%x, flags = 0x%x\n",
		      cfg->index, cfg->flags);

		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		if (cfg->index == 0)
			goto next_entry;
		link->conf.ConfigIndex = cfg->index;

		/* Does this card need audio output? */
		if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
			link->conf.Attributes |= CONF_ENABLE_SPKR;
			link->conf.Status = CCSR_AUDIO_ENA;
		}

		/* Use power settings for Vcc and Vpp if present */
		/*  Note that the CIS values need to be rescaled */
		if (cfg->vcc.present & (1 << CISTPL_POWER_VNOM)) {
			if (conf.Vcc != cfg->vcc.param[CISTPL_POWER_VNOM] / 10000) {
				DEBUG(2, "orinoco_cs_config: Vcc mismatch (conf.Vcc = %d, CIS = %d)\n",  conf.Vcc, cfg->vcc.param[CISTPL_POWER_VNOM] / 10000);
				if (!ignore_cis_vcc)
					goto next_entry;
			}
		} else if (dflt.vcc.present & (1 << CISTPL_POWER_VNOM)) {
			if (conf.Vcc != dflt.vcc.param[CISTPL_POWER_VNOM] / 10000) {
				DEBUG(2, "orinoco_cs_config: Vcc mismatch (conf.Vcc = %d, CIS = %d)\n",  conf.Vcc, dflt.vcc.param[CISTPL_POWER_VNOM] / 10000);
				if(!ignore_cis_vcc)
					goto next_entry;
			}
		}

		if (cfg->vpp1.present & (1 << CISTPL_POWER_VNOM))
			link->conf.Vpp1 = link->conf.Vpp2 =
			    cfg->vpp1.param[CISTPL_POWER_VNOM] / 10000;
		else if (dflt.vpp1.present & (1 << CISTPL_POWER_VNOM))
			link->conf.Vpp1 = link->conf.Vpp2 =
			    dflt.vpp1.param[CISTPL_POWER_VNOM] / 10000;
		
		DEBUG(0, "orinoco_cs_config: We seem to have configured Vcc and Vpp\n");

		/* Do we need to allocate an interrupt? */
		if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1)
			link->conf.Attributes |= CONF_ENABLE_IRQ;

		/* IO window settings */
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io =
			    (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			if (!(io->flags & CISTPL_IO_8BIT))
				link->io.Attributes1 =
				    IO_DATA_PATH_WIDTH_16;
			if (!(io->flags & CISTPL_IO_16BIT))
				link->io.Attributes1 =
				    IO_DATA_PATH_WIDTH_8;
			link->io.IOAddrLines =
			    io->flags & CISTPL_IO_LINES_MASK;
			link->io.BasePort1 = io->win[0].base;
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				link->io.Attributes2 =
				    link->io.Attributes1;
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}

			/* This reserves IO space but doesn't actually enable it */
			CFG_CHECK(RequestIO, link->handle, &link->io);
		}


		/* If we got this far, we're cool! */

		break;
		
	next_entry:
		if (link->io.NumPorts1)
			CardServices(ReleaseIO, link->handle, &link->io);
		last_ret = CardServices(GetNextTuple, handle, &tuple);
		if (last_ret  == CS_NO_MORE_ITEMS) {
			printk(KERN_ERR "GetNextTuple().  No matching CIS configuration, "
			       "maybe you need the ignore_cis_vcc=1 parameter.\n");
			goto cs_failed;
		}
	}

	/*
	   Allocate an interrupt line.  Note that this does not assign a
	   handler to the interrupt, unless the 'Handler' member of the
	   irq structure is initialized.
	 */
	if (link->conf.Attributes & CONF_ENABLE_IRQ) {
		int i;

		link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
		link->irq.IRQInfo1 = IRQ_INFO2_VALID | IRQ_LEVEL_ID;
		if (irq_list[0] == -1)
			link->irq.IRQInfo2 = irq_mask;
		else
			for (i=0; i<4; i++)
				link->irq.IRQInfo2 |= 1 << irq_list[i];
		
  		link->irq.Handler = orinoco_interrupt; 
  		link->irq.Instance = priv; 
		
		CS_CHECK(RequestIRQ, link->handle, &link->irq);
	}

	/* We initialize the hermes structure before completing PCMCIA
	   configuration just in case the interrupt handler gets
	   called. */
	hermes_struct_init(hw, link->io.BasePort1,
				HERMES_IO, HERMES_16BIT_REGSPACING);

	/*
	   This actually configures the PCMCIA socket -- setting up
	   the I/O windows and the interrupt mapping, and putting the
	   card and host interface into "Memory and IO" mode.
	 */
	CS_CHECK(RequestConfiguration, link->handle, &link->conf);

	ndev->base_addr = link->io.BasePort1;
	ndev->irq = link->irq.AssignedIRQ;

	/* register_netdev will give us an ethX name */
	ndev->name[0] = '\0';
	/* Tell the stack we exist */
	if (register_netdev(ndev) != 0) {
		printk(KERN_ERR "orinoco_cs: register_netdev() failed\n");
		goto failed;
	}
	strcpy(card->node.dev_name, ndev->name);

	/* Finally, report what we've done */
	printk(KERN_DEBUG "%s: index 0x%02x: Vcc %d.%d",
	       ndev->name, link->conf.ConfigIndex,
	       link->conf.Vcc / 10, link->conf.Vcc % 10);
	if (link->conf.Vpp1)
		printk(", Vpp %d.%d", link->conf.Vpp1 / 10,
		       link->conf.Vpp1 % 10);
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		printk(", irq %d", link->irq.AssignedIRQ);
	if (link->io.NumPorts1)
		printk(", io 0x%04x-0x%04x", link->io.BasePort1,
		       link->io.BasePort1 + link->io.NumPorts1 - 1);
	if (link->io.NumPorts2)
		printk(" & 0x%04x-0x%04x", link->io.BasePort2,
		       link->io.BasePort2 + link->io.NumPorts2 - 1);
	printk("\n");

	/* And give us the proc nodes for debugging */
	if (orinoco_proc_dev_init(priv) != 0) {
		printk(KERN_ERR "orinoco_cs: Failed to create /proc node for %s\n",
		       ndev->name);
		goto failed;
	}
	
	/* Note to myself : this replace MOD_INC_USE_COUNT/MOD_DEC_USE_COUNT */
	SET_MODULE_OWNER(ndev);
	
	/* Let reset_cor parameter override determine_firmware()'s guess */
	if (reset_cor != -1)
		priv->broken_cor_reset = ! reset_cor;

	/*
	   At this point, the dev_node_t structure(s) need to be
	   initialized and arranged in a linked list at link->dev.
	 */
	card->node.major = card->node.minor = 0;
	link->dev = &card->node;
	link->state &= ~DEV_CONFIG_PENDING;

	TRACE_EXIT("orinoco");

	return;

 cs_failed:
	cs_error(link->handle, last_fn, last_ret);
 failed:
	orinoco_cs_release((u_long) link);

	TRACE_EXIT("orinoco");
}				/* orinoco_cs_config */

/*======================================================================
  After a card is removed, orinoco_cs_release() will unregister the
  device, and release the PCMCIA configuration.  If the device is
  still open, this will be postponed until it is closed.
  ======================================================================*/

static void
orinoco_cs_release(u_long arg)
{
	dev_link_t *link = (dev_link_t *) arg;
	struct orinoco_private *priv = link->priv;

	TRACE_ENTER(link->dev->dev_name);

	/*
	   If the device is currently in use, we won't release until it
	   is actually closed, because until then, we can't be sure that
	   no one will try to access the device or its data structures.
	 */
	if (link->open) {
		DEBUG(0, "orinoco_cs: release postponed, '%s' still open\n",
		      link->dev->dev_name);
		link->state |= DEV_STALE_CONFIG;
		return;
	}

	/* Unregister proc entry */
	orinoco_proc_dev_cleanup(priv);

	/* Don't bother checking to see if these succeed or not */
	CardServices(ReleaseConfiguration, link->handle);
	if (link->io.NumPorts1)
		CardServices(ReleaseIO, link->handle, &link->io);
	if (link->irq.AssignedIRQ)
		CardServices(ReleaseIRQ, link->handle, &link->irq);
	link->state &= ~DEV_CONFIG;

	TRACE_EXIT(link->dev->dev_name);
}				/* orinoco_cs_release */

/*======================================================================
  The card status event handler.  Mostly, this schedules other
  stuff to run after an event is received.

  When a CARD_REMOVAL event is received, we immediately set a
  private flag to block future accesses to this device.  All the
  functions that actually access the device should check this flag
  to make sure the card is still present.
  ======================================================================*/

static int
orinoco_cs_event(event_t event, int priority,
		       event_callback_args_t * args)
{
	dev_link_t *link = args->client_data;
	struct orinoco_private *priv = (struct orinoco_private *)link->priv;
	struct net_device *dev = priv->ndev;

	TRACE_ENTER("orinoco");

	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		link->state &= ~DEV_PRESENT;
		if (link->state & DEV_CONFIG) {
			netif_stop_queue(dev);
		}
		orinoco_shutdown(priv);
		if (link->state & DEV_CONFIG) {
			netif_device_detach(dev);
			mod_timer(&link->release, jiffies + HZ / 20);
		}
		break;
	case CS_EVENT_CARD_INSERTION:
		link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
		orinoco_cs_config(link);
		break;
	case CS_EVENT_PM_SUSPEND:

		link->state |= DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_RESET_PHYSICAL:
		orinoco_shutdown(priv);
		/* Mark the device as stopped, to block IO until later */

		if (link->state & DEV_CONFIG) {
			if (link->open) {
				netif_stop_queue(dev);
				netif_device_detach(dev);
			}
			CardServices(ReleaseConfiguration, link->handle);
		}
		break;
	case CS_EVENT_PM_RESUME:
		link->state &= ~DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_CARD_RESET:
		if (link->state & DEV_CONFIG) {
			CardServices(RequestConfiguration, link->handle,
				     &link->conf);

			if (link->open) {
				if (orinoco_reset(priv) == 0) {
					netif_device_attach(dev);
					netif_start_queue(dev);
				} else {
					printk(KERN_ERR "%s: Error resetting device on PCMCIA event\n",
					       dev->name);
					orinoco_cs_stop(dev);
				}
			}
		}
		/*
		   In a normal driver, additional code may go here to restore
		   the device state and restart IO. 
		 */
		break;
	}

	TRACE_EXIT("orinoco");

	return 0;
}				/* orinoco_cs_event */

static int __init
init_orinoco_cs(void)
{
	servinfo_t serv;

	TRACE_ENTER("orinoco");

	printk(KERN_DEBUG "%s\n", version);

	CardServices(GetCardServicesInfo, &serv);
	if (serv.Revision != CS_RELEASE_CODE) {
		printk(KERN_NOTICE "orinoco_cs: Card Services release "
		       "does not match!\n");
		return -1;
	}

	register_pccard_driver(&dev_info, &orinoco_cs_attach, &orinoco_cs_detach);


	TRACE_EXIT("orinoco");
	return 0;
}

static void __exit
exit_orinoco_cs(void)
{
	TRACE_ENTER("orinoco");

	unregister_pccard_driver(&dev_info);

	if (dev_list)
		DEBUG(0, "orinoco_cs: Removing leftover devices.\n");
	while (dev_list != NULL) {
		del_timer(&dev_list->release);
		if (dev_list->state & DEV_CONFIG)
			orinoco_cs_release((u_long) dev_list);
		orinoco_cs_detach(dev_list);
	}

	TRACE_EXIT("orinoco");
}

module_init(init_orinoco_cs);
module_exit(exit_orinoco_cs);
