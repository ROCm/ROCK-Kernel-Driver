/*
 * Copyright 2002 Momentum Computer
 * Author: Matthew Dharm <mdharm@momenco.com>
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
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/pci.h>
#include <asm/io.h>
#include <asm/mv64340.h>

void mv64340_board_pcibios_fixup_bus(struct pci_bus* c);

struct pci_fixup pcibios_fixups[] = {
	{0}
};

void __init pcibios_fixup_bus(struct pci_bus *c)
{
	mv64340_board_pcibios_fixup_bus(c);
}

void __init pcibios_init(void)
{
	/* Reset PCI I/O and PCI MEM values */
	ioport_resource.start = 0xe0000000;
	ioport_resource.end   = 0xe0000000 + 0x20000000 - 1;
	iomem_resource.start  = 0xc0000000;
	iomem_resource.end    = 0xc0000000 + 0x20000000 - 1;

	pci_scan_bus(0, &mv64340_bus0_pci_ops, NULL);
	pci_scan_bus(1, &mv64340_bus1_pci_ops, NULL);
}
