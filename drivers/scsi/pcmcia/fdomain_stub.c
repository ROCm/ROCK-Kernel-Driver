/*======================================================================

    A driver for Future Domain-compatible PCMCIA SCSI cards

    fdomain_cs.c 1.47 2001/10/13 00:08:52

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
    terms of the GNU General Public License version 2 (the "GPL"), in
    which case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
======================================================================*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <scsi/scsi.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <scsi/scsi_ioctl.h>

#include "scsi.h"
#include "hosts.h"

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

/*====================================================================*/

/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("Future Domain PCMCIA SCSI driver");
MODULE_LICENSE("Dual MPL/GPL");

/* Bit map of interrupts to choose from */
static int irq_mask = 0xdeb8;
MODULE_PARM(irq_mask, "i");
static int irq_list[4] = { -1 };
MODULE_PARM(irq_list, "1-4i");

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
MODULE_PARM(pc_debug, "i");
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version =
"fdomain_cs.c 1.47 2001/10/13 00:08:52 (David Hinds)";
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

typedef struct scsi_info_t {
    dev_link_t		link;
    dev_node_t		node;
    struct Scsi_Host	*host;
} scsi_info_t;

extern Scsi_Host_Template fdomain_driver_template;
extern void fdomain_setup(char *str, int *ints);
extern struct Scsi_Host *__fdomain_16x0_detect( Scsi_Host_Template *tpnt );
extern int fdomain_16x0_bus_reset(Scsi_Cmnd *SCpnt);

static void fdomain_release(dev_link_t *link);
static int fdomain_event(event_t event, int priority,
			event_callback_args_t *args);

static dev_link_t *fdomain_attach(void);
static void fdomain_detach(dev_link_t *);


static dev_link_t *dev_list = NULL;

static dev_info_t dev_info = "fdomain_cs";

static dev_link_t *fdomain_attach(void)
{
    scsi_info_t *info;
    client_reg_t client_reg;
    dev_link_t *link;
    int i, ret;
    
    DEBUG(0, "fdomain_attach()\n");

    /* Create new SCSI device */
    info = kmalloc(sizeof(*info), GFP_KERNEL);
    if (!info) return NULL;
    memset(info, 0, sizeof(*info));
    link = &info->link; link->priv = info;
    link->io.NumPorts1 = 0x10;
    link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
    link->io.IOAddrLines = 10;
    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
    link->irq.IRQInfo1 = IRQ_INFO2_VALID|IRQ_LEVEL_ID;
    if (irq_list[0] == -1)
	link->irq.IRQInfo2 = irq_mask;
    else
	for (i = 0; i < 4; i++)
	    link->irq.IRQInfo2 |= 1 << irq_list[i];
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.Vcc = 50;
    link->conf.IntType = INT_MEMORY_AND_IO;
    link->conf.Present = PRESENT_OPTION;

    /* Register with Card Services */
    link->next = dev_list;
    dev_list = link;
    client_reg.dev_info = &dev_info;
    client_reg.Attributes = INFO_IO_CLIENT | INFO_CARD_SHARE;
    client_reg.event_handler = &fdomain_event;
    client_reg.EventMask =
	CS_EVENT_RESET_REQUEST | CS_EVENT_CARD_RESET |
	CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
	CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
    client_reg.Version = 0x0210;
    client_reg.event_callback_args.client_data = link;
    ret = CardServices(RegisterClient, &link->handle, &client_reg);
    if (ret != 0) {
	cs_error(link->handle, RegisterClient, ret);
	fdomain_detach(link);
	return NULL;
    }
    
    return link;
} /* fdomain_attach */

/*====================================================================*/

static void fdomain_detach(dev_link_t *link)
{
    dev_link_t **linkp;

    DEBUG(0, "fdomain_detach(0x%p)\n", link);
    
    /* Locate device structure */
    for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
	if (*linkp == link) break;
    if (*linkp == NULL)
	return;

    if (link->state & DEV_CONFIG)
	fdomain_release(link);

    if (link->handle)
	CardServices(DeregisterClient, link->handle);
    
    /* Unlink device structure, free bits */
    *linkp = link->next;
    kfree(link->priv);
    
} /* fdomain_detach */

/*====================================================================*/

#define CS_CHECK(fn, args...) \
while ((last_ret=CardServices(last_fn=(fn), args))!=0) goto cs_failed

#define CFG_CHECK(fn, args...) \
if (CardServices(fn, args) != 0) goto next_entry

static void fdomain_config(dev_link_t *link)
{
    client_handle_t handle = link->handle;
    scsi_info_t *info = link->priv;
    tuple_t tuple;
    cisparse_t parse;
    int i, last_ret, last_fn, ints[3];
    u_char tuple_data[64];
    char str[16];
    struct Scsi_Host *host;

    DEBUG(0, "fdomain_config(0x%p)\n", link);

    tuple.DesiredTuple = CISTPL_CONFIG;
    tuple.TupleData = tuple_data;
    tuple.TupleDataMax = 64;
    tuple.TupleOffset = 0;
    CS_CHECK(GetFirstTuple, handle, &tuple);
    CS_CHECK(GetTupleData, handle, &tuple);
    CS_CHECK(ParseTuple, handle, &tuple, &parse);
    link->conf.ConfigBase = parse.config.base;

    /* Configure card */
    link->state |= DEV_CONFIG;
    
    tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
    CS_CHECK(GetFirstTuple, handle, &tuple);
    while (1) {
	CFG_CHECK(GetTupleData, handle, &tuple);
	CFG_CHECK(ParseTuple, handle, &tuple, &parse);
	link->conf.ConfigIndex = parse.cftable_entry.index;
	link->io.BasePort1 = parse.cftable_entry.io.win[0].base;
	i = CardServices(RequestIO, handle, &link->io);
	if (i == CS_SUCCESS) break;
    next_entry:
	CS_CHECK(GetNextTuple, handle, &tuple);
    }

    CS_CHECK(RequestIRQ, handle, &link->irq);
    CS_CHECK(RequestConfiguration, handle, &link->conf);
    
    /* A bad hack... */
    release_region(link->io.BasePort1, link->io.NumPorts1);

    /* Set configuration options for the fdomain driver */
    ints[0] = 2;
    ints[1] = link->io.BasePort1;
    ints[2] = link->irq.AssignedIRQ;
    sprintf(str, "%d,%d", link->io.BasePort1, link->irq.AssignedIRQ);
    fdomain_setup(str, ints);
    
    host = __fdomain_16x0_detect(&fdomain_driver_template);
    if (!host) {
        printk(KERN_INFO "fdomain_cs: no SCSI devices found\n");
	goto cs_failed;
    }
 
    scsi_add_host(host, NULL); /* XXX handle failure */
    scsi_scan_host(host);

    sprintf(info->node.dev_name, "scsi%d", host->host_no);
    link->dev = &info->node;
    info->host = host;
    
    link->state &= ~DEV_CONFIG_PENDING;
    return;
    
cs_failed:
    cs_error(link->handle, last_fn, last_ret);
    fdomain_release(link);
    return;
    
} /* fdomain_config */

/*====================================================================*/

static void fdomain_release(dev_link_t *link)
{
    scsi_info_t *info = link->priv;

    DEBUG(0, "fdomain_release(0x%p)\n", link);

    scsi_remove_host(info->host);
    link->dev = NULL;
    
    CardServices(ReleaseConfiguration, link->handle);
    CardServices(ReleaseIO, link->handle, &link->io);
    CardServices(ReleaseIRQ, link->handle, &link->irq);

    scsi_unregister(info->host);

    link->state &= ~DEV_CONFIG;
}

/*====================================================================*/

static int fdomain_event(event_t event, int priority,
			event_callback_args_t *args)
{
    dev_link_t *link = args->client_data;

    DEBUG(1, "fdomain_event(0x%06x)\n", event);
    
    switch (event) {
    case CS_EVENT_CARD_REMOVAL:
	link->state &= ~DEV_PRESENT;
	if (link->state & DEV_CONFIG)
	    fdomain_release(link);
	break;
    case CS_EVENT_CARD_INSERTION:
	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	fdomain_config(link);
	break;
    case CS_EVENT_PM_SUSPEND:
	link->state |= DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_RESET_PHYSICAL:
	if (link->state & DEV_CONFIG)
	    CardServices(ReleaseConfiguration, link->handle);
	break;
    case CS_EVENT_PM_RESUME:
	link->state &= ~DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_CARD_RESET:
	if (link->state & DEV_CONFIG) {
	    CardServices(RequestConfiguration, link->handle, &link->conf);
	    fdomain_16x0_bus_reset(NULL);
	}
	break;
    }
    return 0;
} /* fdomain_event */

static struct pcmcia_driver fdomain_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "fdomain_cs",
	},
	.attach		= fdomain_attach,
	.detach		= fdomain_detach,
};

static int __init init_fdomain_cs(void)
{
	return pcmcia_register_driver(&fdomain_cs_driver);
}

static void __exit exit_fdomain_cs(void)
{
	pcmcia_unregister_driver(&fdomain_cs_driver);

	/* XXX: this really needs to move into generic code.. */
	while (dev_list != NULL)
		fdomain_detach(dev_list);
}

module_init(init_fdomain_cs);
module_exit(exit_fdomain_cs);
