/*
 * arch/ppc/platforms/spruce_pci.c
 * 
 * PCI support for IBM Spruce
 *
 * Author: Johnnie Peters
 *         jpeters@mvista.com
 *
 * Copyright 2000 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <platforms/spruce.h>

#include <syslib/cpc700.h>

static inline int
spruce_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
		/*
		 * 	PCI IDSEL/INTPIN->INTLINE 
		 * 	A	B	C	D
		 */
	{
		{23, 24, 25, 26},	/* IDSEL 1 - PCI slot 3 */
		{24, 25, 26, 23},	/* IDSEL 2 - PCI slot 2 */
		{25, 26, 23, 24},	/* IDSEL 3 - PCI slot 1 */
		{26, 23, 24, 25},	/* IDSEL 4 - PCI slot 0 */
	};

	const long min_idsel = 1, max_idsel = 4, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

void __init
spruce_setup_hose(void)
{
	struct pci_controller *hose;

	/* Setup hose */
	hose = pcibios_alloc_controller();
	if (!hose) 
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	pci_init_resource(&hose->io_resource,
			SPRUCE_PCI_LOWER_IO,
			SPRUCE_PCI_UPPER_IO,
			IORESOURCE_IO,
			"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
			SPRUCE_PCI_LOWER_MEM,
			SPRUCE_PCI_UPPER_MEM,
			IORESOURCE_MEM,
			"PCI host bridge");

	hose->io_space.start = SPRUCE_PCI_LOWER_IO;
	hose->io_space.end = SPRUCE_PCI_UPPER_IO;
	hose->mem_space.start = SPRUCE_PCI_LOWER_MEM;
	hose->mem_space.end = SPRUCE_PCI_UPPER_MEM;
	hose->io_base_virt = (void *)SPRUCE_ISA_IO_BASE;

	setup_indirect_pci(hose,
			SPRUCE_PCI_CONFIG_ADDR,
			SPRUCE_PCI_CONFIG_DATA);

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = spruce_map_irq;
}
