/*======================================================================

    CardBus device enabler

    cb_enabler.c 1.31 2000/06/12 21:29:36

    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
    The general idea:

    A client driver registers using register_driver().  This module
    then creates a Card Services pseudo-client and registers it, and
    configures the socket if this is the first client.  It then
    invokes the appropriate PCI client routines in response to Card
    Services events.  

======================================================================*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/timer.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
MODULE_PARM(pc_debug, "i");
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version =
"cb_enabler.c 1.31 2000/06/12 21:29:36 (David Hinds)";
#else
#define DEBUG(n, args...) do { } while (0)
#endif

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("CardBus stub enabler module");

/*====================================================================*/

/* Parameters that can be set with 'insmod' */

/*====================================================================*/

typedef struct driver_info_t {
    dev_link_t		*(*attach)(void);
    dev_info_t		dev_info;
    driver_operations	*ops;
    dev_link_t		*dev_list;
} driver_info_t;

static dev_link_t *cb_attach(int n);
#define MK_ENTRY(fn, n) \
static dev_link_t *fn(void) { return cb_attach(n); }

#define MAX_DRIVER	4

MK_ENTRY(attach_0, 0);
MK_ENTRY(attach_1, 1);
MK_ENTRY(attach_2, 2);
MK_ENTRY(attach_3, 3);

static driver_info_t driver[4] = {
    { attach_0 }, { attach_1 }, { attach_2 }, { attach_3 }
};

typedef struct bus_info_t {
    u_char		bus;
    int			flags, ncfg, nuse;
    dev_link_t		*owner;
} bus_info_t;

#define DID_REQUEST	1
#define DID_CONFIG	2

static void cb_release(u_long arg);
static int cb_event(event_t event, int priority,
		    event_callback_args_t *args);

static void cb_detach(dev_link_t *);

static bus_info_t bus_table[MAX_DRIVER];

/*====================================================================*/

static void cs_error(client_handle_t handle, int func, int ret)
{
    error_info_t err = { func, ret };
    pcmcia_report_error(handle, &err);
}

/*====================================================================*/

struct dev_link_t *cb_attach(int n)
{
    client_reg_t client_reg;
    dev_link_t *link;
    int ret;
    
    DEBUG(0, "cb_attach(%d)\n", n);

    link = kmalloc(sizeof(struct dev_link_t), GFP_KERNEL);
    if (!link) return NULL;

    MOD_INC_USE_COUNT;
    memset(link, 0, sizeof(struct dev_link_t));
    link->conf.IntType = INT_CARDBUS;
    link->conf.Vcc = 33;

    /* Insert into instance chain for this driver */
    link->priv = &driver[n];
    link->next = driver[n].dev_list;
    driver[n].dev_list = link;
    
    /* Register with Card Services */
    client_reg.dev_info = &driver[n].dev_info;
    client_reg.Attributes = INFO_IO_CLIENT | INFO_CARD_SHARE;
    client_reg.event_handler = &cb_event;
    client_reg.EventMask = CS_EVENT_RESET_PHYSICAL |
	CS_EVENT_RESET_REQUEST | CS_EVENT_CARD_RESET |
	CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
	CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
    client_reg.Version = 0x0210;
    client_reg.event_callback_args.client_data = link;
    ret = pcmcia_register_client(&link->handle, &client_reg);
    if (ret != 0) {
	cs_error(link->handle, RegisterClient, ret);
	cb_detach(link);
	return NULL;
    }
    return link;
}

/*====================================================================*/

static void cb_detach(dev_link_t *link)
{
    driver_info_t *dev = link->priv;
    dev_link_t **linkp;
    bus_info_t *b = (void *)link->win;

    DEBUG(0, "cb_detach(0x%p)\n", link);

    /* Locate device structure */
    for (linkp = &dev->dev_list; *linkp; linkp = &(*linkp)->next)
	if (*linkp == link) break;
    if (*linkp == NULL)
	return;

    if (link->state & DEV_CONFIG)
	cb_release((u_long)link);

    /* Don't drop Card Services connection if we are the bus owner */
    if (b && (b->flags != 0) && (link == b->owner)) {
	link->state |= DEV_STALE_LINK;
	return;
    }

    if (link->handle)
	pcmcia_deregister_client(link->handle);

    *linkp = link->next;
    kfree(link);
    MOD_DEC_USE_COUNT;
}

/*====================================================================*/

static void cb_config(dev_link_t *link)
{
    client_handle_t handle = link->handle;
    driver_info_t *drv = link->priv;
    dev_locator_t loc;
    bus_info_t *b;
    config_info_t config;
    u_char bus, devfn;
    int i;
    
    DEBUG(0, "cb_config(0x%p)\n", link);
    link->state |= DEV_CONFIG;

    /* Get PCI bus info */
    pcmcia_get_configuration_info(handle, &config);
    bus = config.Option; devfn = config.Function;

    /* Is this a new bus? */
    for (i = 0; i < MAX_DRIVER; i++)
	if (bus == bus_table[i].bus) break;
    if (i == MAX_DRIVER) {
	for (i = 0; i < MAX_DRIVER; i++)
	    if (bus_table[i].bus == 0) break;
	b = &bus_table[i]; link->win = (void *)b;
	b->bus = bus;
	b->flags = 0;
	b->ncfg = b->nuse = 1;

	/* Special hook: CS know what to do... */
	i = pcmcia_request_io(handle, NULL);
	if (i != CS_SUCCESS) {
	    cs_error(handle, RequestIO, i);
	    return;
	}
	b->flags |= DID_REQUEST;
	b->owner = link;
	i = pcmcia_request_configuration(handle, &link->conf);
	if (i != CS_SUCCESS) {
	    cs_error(handle, RequestConfiguration, i);
	    return;
	}
	b->flags |= DID_CONFIG;
    } else {
	b = &bus_table[i]; link->win = (void *)b;
	if (b->flags & DID_CONFIG) {
	    b->ncfg++; b->nuse++;
	}
    }
    loc.bus = LOC_PCI;
    loc.b.pci.bus = bus;
    loc.b.pci.devfn = devfn;
    link->dev = drv->ops->attach(&loc);
    
    link->state &= ~DEV_CONFIG_PENDING;
}

/*====================================================================*/

static void cb_release(u_long arg)
{
    dev_link_t *link = (dev_link_t *)arg;
    driver_info_t *drv = link->priv;
    bus_info_t *b = (void *)link->win;

    DEBUG(0, "cb_release(0x%p)\n", link);

    if (link->dev != NULL) {
	drv->ops->detach(link->dev);
	link->dev = NULL;
    }
    if (link->state & DEV_CONFIG) {
	/* If we're suspended, config was already released */
	if (link->state & DEV_SUSPEND)
	    b->flags &= ~DID_CONFIG;
	else if ((b->flags & DID_CONFIG) && (--b->ncfg == 0)) {
	    pcmcia_release_configuration(b->owner->handle);
	    b->flags &= ~DID_CONFIG;
	}
	if ((b->flags & DID_REQUEST) && (--b->nuse == 0)) {
	    pcmcia_release_io(b->owner->handle, NULL);
	    b->flags &= ~DID_REQUEST;
	}
	if (b->flags == 0) {
	    if (b->owner && (b->owner->state & DEV_STALE_LINK))
		cb_detach(b->owner);
	    b->bus = 0; b->owner = NULL;
	}
    }
    link->state &= ~DEV_CONFIG;
}

/*====================================================================*/

static int cb_event(event_t event, int priority,
		    event_callback_args_t *args)
{
    dev_link_t *link = args->client_data;
    driver_info_t *drv = link->priv;
    bus_info_t *b = (void *)link->win;
    
    DEBUG(0, "cb_event(0x%06x)\n", event);
    
    switch (event) {
    case CS_EVENT_CARD_REMOVAL:
	link->state &= ~DEV_PRESENT;
	if (link->state & DEV_CONFIG)
	    cb_release((u_long)link);
	break;
    case CS_EVENT_CARD_INSERTION:
	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	cb_config(link);
	break;
    case CS_EVENT_PM_SUSPEND:
	link->state |= DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_RESET_PHYSICAL:
	if (link->state & DEV_CONFIG) {
	    if (drv->ops->suspend != NULL)
		drv->ops->suspend(link->dev);
	    b->ncfg--;
	    if (b->ncfg == 0)
		pcmcia_release_configuration(link->handle);
	}
	break;
    case CS_EVENT_PM_RESUME:
	link->state &= ~DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_CARD_RESET:
	if (link->state & DEV_CONFIG) {
	    b->ncfg++;
	    if (b->ncfg == 1)
		pcmcia_request_configuration(link->handle,
			     &link->conf);
	    if (drv->ops->resume != NULL)
		drv->ops->resume(link->dev);
	}
	break;
    }
    return 0;
}

/*====================================================================*/

int register_driver(struct driver_operations *ops)
{
    int i;
    
    DEBUG(0, "register_driver('%s')\n", ops->name);

    for (i = 0; i < MAX_DRIVER; i++)
	if (driver[i].ops == NULL) break;
    if (i == MAX_DRIVER)
	return -1;

    MOD_INC_USE_COUNT;
    driver[i].ops = ops;
    strcpy(driver[i].dev_info, ops->name);
    register_pccard_driver(&driver[i].dev_info, driver[i].attach,
			   &cb_detach);
    return 0;
}

void unregister_driver(struct driver_operations *ops)
{
    int i;

    DEBUG(0, "unregister_driver('%s')\n", ops->name);
    for (i = 0; i < MAX_DRIVER; i++)
	if (driver[i].ops == ops) break;
    if (i < MAX_DRIVER) {
	unregister_pccard_driver(&driver[i].dev_info);
	driver[i].ops = NULL;
	MOD_DEC_USE_COUNT;
    }
}

/*====================================================================*/

EXPORT_SYMBOL(register_driver);
EXPORT_SYMBOL(unregister_driver);

static int __init init_cb_enabler(void)
{
    servinfo_t serv;
    DEBUG(0, "%s\n", version);
    pcmcia_get_card_services_info(&serv);
    if (serv.Revision != CS_RELEASE_CODE) {
	printk(KERN_NOTICE "cb_enabler: Card Services release "
	       "does not match!\n");
	return -1;
    }
    return 0;
}

static void __exit exit_cb_enabler(void)
{
    DEBUG(0, "cb_enabler: unloading\n");
}

module_init(init_cb_enabler);
module_exit(exit_cb_enabler);

/*====================================================================*/

