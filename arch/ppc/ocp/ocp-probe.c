/*
 * FILE NAME: ocp-probe.c
 *
 * BRIEF MODULE DESCRIPTION:
 * Device scanning & bus set routines
 * Based on drivers/pci/probe, Copyright (c) 1997--1999 Martin Mares
 *
 * Maintained by: Armin <akuster@mvista.com>
 *
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/device.h>
#include <asm/ocp.h>

LIST_HEAD(ocp_devices);
struct device *ocp_bus;

static struct ocp_device * __devinit
ocp_setup_dev(struct ocp_def *odef, unsigned int index)
{
	struct ocp_device *dev;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
	memset(dev, 0, sizeof(*dev));

	dev->vendor = odef->vendor;
	dev->device = odef->device;
	dev->num = ocp_get_num(dev->device);
	dev->paddr = odef->paddr;
	dev->irq = odef->irq;
	dev->pm = odef->pm;
	dev->current_state = 4;

	sprintf(dev->name, "OCP device %04x:%04x", dev->vendor, dev->device);

	DBG("%s %s 0x%lx irq:%d pm:0x%lx \n", dev->slot_name, dev->name,
	    (unsigned long) dev->paddr, dev->irq, dev->pm);

	/* now put in global tree */
	sprintf(dev->dev.bus_id, "%d", index);
	dev->dev.parent = ocp_bus;
	dev->dev.bus = &ocp_bus_type;
	device_register(&dev->dev);

	return dev;
}

static struct device * __devinit ocp_alloc_primary_bus(void)
{
	struct device *b;

	b = kmalloc(sizeof(struct device), GFP_KERNEL);
	if (b == NULL)
		return NULL;
	memset(b, 0, sizeof(struct device));
	strcpy(b->bus_id, "ocp");

	device_register(b);

	return b;
}

void __devinit ocp_setup_devices(struct ocp_def *odef)
{
	int index;
	struct ocp_device *dev;

	if (ocp_bus == NULL)
		ocp_bus = ocp_alloc_primary_bus();
	for (index = 0; odef->vendor != OCP_VENDOR_INVALID; ++index, ++odef) {
		dev = ocp_setup_dev(odef, index);
		if (dev != NULL)
			list_add_tail(&dev->global_list, &ocp_devices);
	}
}

extern struct ocp_def core_ocp[];

static int __init
ocparch_init(void)
{
	ocp_setup_devices(core_ocp);
	return 0;
}

subsys_initcall(ocparch_init);

EXPORT_SYMBOL(ocp_devices);
