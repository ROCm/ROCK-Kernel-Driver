/* orinoco_plx.c 0.01
 * 
 * Driver for Prism II devices which would usually be driven by orinoco_cs,
 * but are connected to the PCI bus by a PLX9052. 
 *
 * Specifically here we're talking about the SMC2602W (EZConnect
 * Wireless PCI Adaptor)
 *
 * The actual driving is done by orinoco.c, this is just resource
 * allocation stuff.  The explanation below is courtesy of Ryan Niemi
 * on the linux-wlan-ng list at
 * http://archives.neohapsis.com/archives/dev/linux-wlan/2001-q1/0026.html
 *
 * Copyright (C) 2001 Daniel Barlow <dan@telent.net>
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.

The PLX9052-based cards (WL11000 and several others) are a different
beast than the usual PCMCIA-based PRISM2 configuration expected by
wlan-ng. Here's the general details on how the WL11000 PCI adapter
works:

 - Two PCI I/O address spaces, one 0x80 long which contains the PLX9052
   registers, and one that's 0x40 long mapped to the PCMCIA slot I/O
   address space.

 - One PCI memory address space, mapped to the PCMCIA memory space
   (containing the CIS).

After identifying the I/O and memory space, you can read through the
memory space to confirm the CIS's device ID or manufacturer ID to make
sure it's the expected card. Keep in mind that the PCMCIA spec specifies
the CIS as the lower 8 bits of each word read from the CIS, so to read the
bytes of the CIS, read every other byte (0,2,4,...). Passing that test,
you need to enable the I/O address space on the PCMCIA card via the PCMCIA
COR register. This is the first byte following the CIS. In my case
(which may not have any relation to what's on the PRISM2 cards), COR was
at offset 0x800 within the PCI memory space. Write 0x41 to the COR
register to enable I/O mode and to select level triggered interrupts. To
confirm you actually succeeded, read the COR register back and make sure
it actually got set to 0x41, incase you have an unexpected card inserted.

Following that, you can treat the second PCI I/O address space (the one
that's not 0x80 in length) as the PCMCIA I/O space.

Note that in the Eumitcom's source for their drivers, they register the
interrupt as edge triggered when registering it with the Windows kernel. I
don't recall how to register edge triggered on Linux (if it can be done at
all). But in some experimentation, I don't see much operational
difference between using either interrupt mode. Don't mess with the
interrupt mode in the COR register though, as the PLX9052 wants level
triggers with the way the serial EEPROM configures it on the WL11000.

There's some other little quirks related to timing that I bumped into, but
I don't recall right now. Also, there's two variants of the WL11000 I've
seen, revision A1 and T2. These seem to differ slightly in the timings
configured in the wait-state generator in the PLX9052. There have also
been some comments from Eumitcom that cards shouldn't be hot swapped,
apparently due to risk of cooking the PLX9052. I'm unsure why they
believe this, as I can't see anything in the design that would really
cause a problem, except for crashing drivers not written to expect it. And
having developed drivers for the WL11000, I'd say it's quite tricky to
write code that will successfully deal with a hot unplug. Very odd things
happen on the I/O side of things. But anyway, be warned. Despite that,
I've hot-swapped a number of times during debugging and driver development
for various reasons (stuck WAIT# line after the radio card's firmware
locks up).

Hope this is enough info for someone to add PLX9052 support to the wlan-ng
card. In the case of the WL11000, the PCI ID's are 0x1639/0x0200, with
matching subsystem ID's. Other PLX9052-based manufacturers other than
Eumitcom (or on cards other than the WL11000) may have different PCI ID's.

If anyone needs any more specific info, let me know. I haven't had time
to implement support myself yet, and with the way things are going, might
not have time for a while..

---end of mail---

  Bus  0, device   4, function  0:
    Network controller: Unknown vendor Unknown device (rev 2).
      Vendor id=1638. Device id=1100.
      Medium devsel.  Fast back-to-back capable.  IRQ 10.  
      I/O at 0x1000 [0x1001].
      Non-prefetchable 32 bit memory at 0x40000000 [0x40000000].
      I/O at 0x10c0 [0x10c1].
*/

#include <linux/config.h>

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
#include <linux/pci.h>
#include <linux/wireless.h>
#include <linux/fcntl.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include <pcmcia/bus_ops.h>

#include "hermes.h"
#include "orinoco.h"

MODULE_AUTHOR("Daniel Barlow <dan@telent.net>");
MODULE_DESCRIPTION("Driver for wireless LAN cards using the PLX9052 PCI bridge");
MODULE_LICENSE("Dual MPL/GPL");

static dev_info_t dev_info = "orinoco_plx";

#define COR_OFFSET    0x3e0	/* COR attribute offset of Prism2 PC card */
#define COR_VALUE     0x41	/* Enable PC card with interrupt in level trigger */

static int orinoco_plx_open(struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *) dev->priv;
	int err;

	netif_device_attach(dev);

	err = dldwd_reset(priv);
	if (err)
		printk(KERN_ERR "%s: dldwd_reset failed in orinoco_plx_open()",
		       dev->name);
	else
		netif_start_queue(dev);

	return err;
}

static int orinoco_plx_stop(struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *) dev->priv;
	netif_stop_queue(dev);
	dldwd_shutdown(priv);
	return 0;
}

static void
orinoco_plx_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	dldwd_interrupt(irq, ((struct net_device *) dev_id)->priv, regs);
}

static int orinoco_plx_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct net_device *dev;
	unsigned long pccard_ioaddr;
	int i;
	int reg;
	unsigned char *attr_mem;
	dldwd_priv_t *priv;

	if ((i = pci_enable_device(pdev)))
		return -EIO;

	/* Resource 2 is mapped to the PCMCIA space */
	attr_mem = ioremap(pci_resource_start(pdev, 2), 0x1000);
	/* and 3 to the PCMCIA slot I/O address space */
	pccard_ioaddr = pci_resource_start(pdev, 3);

	/* Verify whether PC card is present */
	if (attr_mem[0] != 0x01 || attr_mem[2] != 0x03 ||
	    attr_mem[4] != 0x00 || attr_mem[6] != 0x00 ||
	    attr_mem[8] != 0xFF || attr_mem[10] != 0x17 ||
	    attr_mem[12] != 0x04 || attr_mem[14] != 0x67) {
		printk(KERN_ERR "orinoco_plx: The CIS value of Prism2 PC card is invalid.\n");
		return -EIO;
	}
	/* PCMCIA COR is the first byte following CIS: this write should
	 * enable I/O mode and select level-triggered interrupts */
	attr_mem[COR_OFFSET] = COR_VALUE;
	reg = attr_mem[COR_OFFSET];
	/* assert(reg==COR_VALUE); doesn't work */
	iounmap(attr_mem);	/* done with this now, it seems */
	if (!request_region(pccard_ioaddr,
			    pci_resource_len(pdev, 3), dev_info)) {
		printk(KERN_ERR "orinoco_plx: I/O resource 0x%lx @ 0x%lx busy\n",
		       pci_resource_len(pdev, 3), pccard_ioaddr);
		return -EBUSY;
	}
	if (!(priv = kmalloc(sizeof(*priv), GFP_KERNEL)))
		return -ENOMEM;
	memset(priv, 0, sizeof(*priv));
	dev = &priv->ndev;

	dldwd_setup(priv);	/* XXX clean up if <0 */
	dev->irq = pdev->irq;
	dev->base_addr = pccard_ioaddr;
	dev->open = orinoco_plx_open;
	dev->stop = orinoco_plx_stop;
	priv->card_reset_handler = NULL; /* We have no reset handler */

	printk(KERN_DEBUG
	       "Detected Orinoco/Prism2 PCI device at %s, mem:0x%lx, irq:%d, io addr:0x%lx\n",
	       pdev->slot_name, (long) attr_mem, pdev->irq, pccard_ioaddr);

	hermes_struct_init(&(priv->hw), dev->base_addr);	/* XXX */
	dev->name[0] = '\0';	/* name defaults to ethX */
	register_netdev(dev);
	request_irq(pdev->irq, orinoco_plx_interrupt, SA_SHIRQ, dev->name,
		    dev);
	if (dldwd_proc_dev_init(priv) != 0) {
		printk(KERN_ERR "%s: Failed to create /proc node\n", dev->name);
		return -EIO;
	}

	SET_MODULE_OWNER(dev);
	priv->hw_ready = 1;

	/* if(reset_cor) dldwd_cs_cor_reset(priv); */
	return 0;		/* succeeded */
}

static void __devexit orinoco_plx_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	dldwd_priv_t *priv = dev->priv;

	if (!dev)
		BUG();

	dldwd_proc_dev_cleanup(priv);
	free_irq(dev->irq, dev);
	unregister_netdev(dev);
	release_region(dev->base_addr, 0x40);
	kfree(dev->priv);
	pci_set_drvdata(pdev, NULL);
}


static struct pci_device_id orinoco_plx_pci_id_table[] __devinitdata = {
	{0x1638, 0x1100, PCI_ANY_ID, PCI_ANY_ID,},
	{0,},
};

MODULE_DEVICE_TABLE(pci, orinoco_plx_pci_id_table);

static struct pci_driver orinoco_plx_driver = {
	name:"orinoco_plx",
	id_table:orinoco_plx_pci_id_table,
	probe:orinoco_plx_init_one,
	remove:__devexit_p(orinoco_plx_remove_one),
	suspend:0,
	resume:0
};

static int __init orinoco_plx_init(void)
{
	return pci_module_init(&orinoco_plx_driver);
}

extern void __exit orinoco_plx_exit(void)
{
	pci_unregister_driver(&orinoco_plx_driver);
}

module_init(orinoco_plx_init);
module_exit(orinoco_plx_exit);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
