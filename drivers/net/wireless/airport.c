/* airport.c 0.06f
 *
 * A driver for "Hermes" chipset based Apple Airport wireless
 * card.
 *
 * Copyright notice & release notes in file orinoco.c
 * 
 * Note specific to airport stub:
 * 
 *  0.05 : first version of the new split driver
 *  0.06 : fix possible hang on powerup, add sleep support
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
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
#include <linux/adb.h>
#include <linux/pmu.h>

#include <asm/prom.h>
#include <asm/feature.h>
#include <asm/irq.h>

#include "hermes.h"
#include "orinoco.h"

static const char version[] __initdata = "airport.c 0.06f (Benjamin Herrenschmidt <benh@kernel.crashing.org>)";
MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("Driver for the Apple Airport wireless card.");

typedef struct dldwd_card {
	struct device_node* node;
	int irq_requested;
	int ndev_registered;
	int open;
	/* Common structure (fully included), see orinoco.h */
	struct dldwd_priv priv;
} dldwd_card_t;

#ifdef CONFIG_PMAC_PBOOK
static int airport_sleep_notify(struct pmu_sleep_notifier *self, int when);
static struct pmu_sleep_notifier airport_sleep_notifier = {
	airport_sleep_notify, SLEEP_LEVEL_NET,
};
#endif

/*
 * Function prototypes
 */

static dldwd_priv_t* airport_attach(struct device_node *of_node);
static void airport_detach(dldwd_priv_t* priv);
static int airport_init(struct net_device *dev);
static int airport_open(struct net_device *dev);
static int airport_stop(struct net_device *dev);

/*
   A linked list of "instances" of the dummy device.  Each actual
   PCMCIA card corresponds to one device instance, and is described
   by one dev_link_t structure (defined in ds.h).

   You may not want to use a linked list for this -- for example, the
   memory card driver uses an array of dev_link_t pointers, where minor
   device numbers are used to derive the corresponding array index.
*/

static dldwd_priv_t *airport_dev;

static int airport_init(struct net_device *dev)
{
	dldwd_priv_t *priv = dev->priv;
	int rc;
	
	TRACE_ENTER(priv->ndev.name);

	MOD_INC_USE_COUNT;

	rc = dldwd_init(dev);
	if (!rc)
		priv->hw_ready = 1;

	MOD_DEC_USE_COUNT;

	return rc;
}

static int
airport_open(struct net_device *dev)
{
	dldwd_priv_t *priv = dev->priv;
	dldwd_card_t* card = (dldwd_card_t *)priv->card;
	int rc;

	TRACE_ENTER(priv->ndev.name);

	rc = dldwd_reset(priv);
	if (rc)
		airport_stop(dev);
	else {
		card->open = 1;
		netif_device_attach(dev);
	}

//	TRACE_EXIT(priv->ndev.name);

	return rc;
}

static int
airport_stop(struct net_device *dev)
{
	dldwd_priv_t *priv = dev->priv;
	dldwd_card_t* card = (dldwd_card_t *)priv->card;

	TRACE_ENTER(priv->ndev.name);

	netif_stop_queue(dev);
	dldwd_shutdown(priv);
	card->open = 0;

	TRACE_EXIT(priv->ndev.name);

	return 0;
}

#ifdef CONFIG_PMAC_PBOOK
static int
airport_sleep_notify(struct pmu_sleep_notifier *self, int when)
{
	dldwd_priv_t *priv;
	struct net_device *ndev;
	dldwd_card_t* card;
	int rc;
	
	if (!airport_dev)
		return PBOOK_SLEEP_OK;
	priv = airport_dev;
	ndev = &priv->ndev;
	card = (dldwd_card_t *)priv->card;

	switch (when) {
	case PBOOK_SLEEP_REQUEST:
		break;
	case PBOOK_SLEEP_REJECT:
		break;
	case PBOOK_SLEEP_NOW:
		printk(KERN_INFO "%s: Airport entering sleep mode\n", ndev->name);
		netif_device_detach(ndev);
		if (card->open)
			dldwd_shutdown(priv);
		disable_irq(ndev->irq);
		feature_set_airport_power(card->node, 0);
		priv->hw_ready = 0;
		break;
	case PBOOK_WAKE:
		printk(KERN_INFO "%s: Airport waking up\n", ndev->name);
		feature_set_airport_power(card->node, 1);
		mdelay(200);
		hermes_reset(&priv->hw);
		priv->hw_ready = 1;		
		rc = dldwd_reset(priv);
		if (rc)
			printk(KERN_ERR "airport: Error %d re-initing card !\n", rc);
		else if (card->open)
			netif_device_attach(ndev);
		enable_irq(ndev->irq);
		break;
	}
	return PBOOK_SLEEP_OK;
}
#endif /* CONFIG_PMAC_PBOOK */

static dldwd_priv_t*
airport_attach(struct device_node* of_node)
{
	dldwd_priv_t *priv;
	struct net_device *ndev;
	dldwd_card_t* card;
	hermes_t *hw;

	TRACE_ENTER("dldwd");

	if (of_node->n_addrs < 1 || of_node->n_intrs < 1) {
		printk(KERN_ERR "airport: wrong interrupt/addresses in OF tree\n");
		return NULL;
	}

	/* Allocate space for private device-specific data */
	card = kmalloc(sizeof(*card), GFP_KERNEL);
	if (!card) {
		printk(KERN_ERR "airport: can't allocate device datas\n");
		return NULL;
	}
	memset(card, 0, sizeof(*card));

	priv = &(card->priv);
	priv->card = card;
	ndev = &priv->ndev;
	hw = &priv->hw;
	card->node = of_node;

	/* Setup the common part */
	if (dldwd_setup(priv) < 0) {
		kfree(card);
		return NULL;
	}

	/* Overrides */
	ndev->init = airport_init;
	ndev->open = airport_open;
	ndev->stop = airport_stop;

	/* Setup interrupts & base address */
	ndev->irq = of_node->intrs[0].line;
	ndev->base_addr = (unsigned long)ioremap(of_node->addrs[0].address, 0x1000) - _IO_BASE;

	hermes_struct_init(hw, ndev->base_addr);
		
	/* Power up card */
	feature_set_airport_power(card->node, 1);
	current->state = TASK_UNINTERRUPTIBLE;
	schedule_timeout(HZ);

	/* Reset it before we get the interrupt */
	hermes_reset(hw);

	if (request_irq(ndev->irq, dldwd_interrupt, 0, "Airport", (void *)priv)) {
		printk(KERN_ERR "airport: Couldn't get IRQ %d\n", ndev->irq);
		goto failed;
	}
	card->irq_requested = 1;
	
	/* register_netdev will give us an ethX name */
	ndev->name[0] = '\0';
	/* Tell the stack we exist */
	if (register_netdev(ndev) != 0) {
		printk(KERN_ERR "airport: register_netdev() failed\n");
		goto failed;
	}
	printk(KERN_DEBUG "airport: card registered for interface %s\n", ndev->name);
	card->ndev_registered = 1;

	SET_MODULE_OWNER(ndev);

	/* And give us the proc nodes for debugging */
	if (dldwd_proc_dev_init(priv) != 0)
		printk(KERN_ERR "airport: Failed to create /proc node for %s\n",
		       ndev->name);

#ifdef CONFIG_PMAC_PBOOK
	pmu_register_sleep_notifier(&airport_sleep_notifier);
#endif
	return priv;
	
failed:
	airport_detach(priv);
	return NULL;
}				/* airport_attach */

/*======================================================================
  This deletes a driver "instance".  
  ======================================================================*/

static void
airport_detach(dldwd_priv_t *priv)
{
	dldwd_card_t* card = (dldwd_card_t *)priv->card;

	priv->hw_ready = 0;
	
	/* Unregister proc entry */
	dldwd_proc_dev_cleanup(priv);

#ifdef CONFIG_PMAC_PBOOK
	pmu_unregister_sleep_notifier(&airport_sleep_notifier);
#endif
	if (card->ndev_registered)
		unregister_netdev(&priv->ndev);
	card->ndev_registered = 0;
	
	if (card->irq_requested)
		free_irq(priv->ndev.irq, priv);
	card->irq_requested = 0;

// FIXME
//	if (ndev->base_addr)
//		iounmap(ndev->base_addr + _IO_BASE);
//	ndev->base_addr = 0;
	
	feature_set_airport_power(card->node, 0);
	current->state = TASK_UNINTERRUPTIBLE;
	schedule_timeout(HZ);
	
	kfree(card);
}				/* airport_detach */

static int __init
init_airport(void)
{
	struct device_node* airport_node;

	printk(KERN_DEBUG "%s\n", version);

	MOD_INC_USE_COUNT;

	/* Lookup card in device tree */
	airport_node = find_devices("radio");
	if (airport_node && !strcmp(airport_node->parent->name, "mac-io"))
		airport_dev = airport_attach(airport_node);

	MOD_DEC_USE_COUNT;

	return airport_dev ? 0 : -ENODEV;
}

static void __exit
exit_airport(void)
{
	if (airport_dev)
		airport_detach(airport_dev);
	airport_dev = NULL;
}

module_init(init_airport);
module_exit(exit_airport);
