/*
 * BRIEF MODULE DESCRIPTION
 * Galileo Evaluation Boards PCI support.
 *
 * The general-purpose functions to read/write and configure the GT64120A's
 * PCI registers (function names start with pci0 or pci1) are either direct
 * copies of functions written by Galileo Technology, or are modifications
 * of their functions to work with Linux 2.4 vs Linux 2.2.  These functions
 * are Copyright - Galileo Technology.
 *
 * Other functions are derived from other MIPS PCI implementations, or were
 * written by RidgeRun, Inc,  Copyright (C) 2000 RidgeRun, Inc.
 *   glonnon@ridgerun.com, skranz@ridgerun.com, stevej@ridgerun.com
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
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
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/cache.h>
#include <asm/pci.h>
#include <asm/io.h>
#include <asm/gt64120/gt64120.h>

#include <linux/init.h>

#define SELF 0

/*
 * These functions and structures provide the BIOS scan and mapping of the PCI
 * devices.
 */

#define MAX_PCI_DEVS 10

struct pci_device {
	u32 slot;
	u32 BARtype[6];
	u32 BARsize[6];
};

static void __init scan_and_initialize_pci(void);
static u32 __init scan_pci_bus(struct pci_device *pci_devices);
static void __init allocate_pci_space(struct pci_device *pci_devices);

/*
 * The functions that actually read and write to the controller.
 *
 *  Copied from or modified from Galileo Technology code.
 */
static unsigned int pci0ReadConfigReg(int offset, struct pci_dev *device);
static void pci0WriteConfigReg(unsigned int offset,
			       struct pci_dev *device, unsigned int data);
static unsigned int pci1ReadConfigReg(int offset, struct pci_dev *device);
static void pci1WriteConfigReg(unsigned int offset,
			       struct pci_dev *device, unsigned int data);

static void pci0MapIOspace(unsigned int pci0IoBase,
			   unsigned int pci0IoLength);
static void pci1MapIOspace(unsigned int pci1IoBase,
			   unsigned int pci1IoLength);
static void pci0MapMemory0space(unsigned int pci0Mem0Base,
				unsigned int pci0Mem0Length);
static void pci1MapMemory0space(unsigned int pci1Mem0Base,
				unsigned int pci1Mem0Length);
static void pci0MapMemory1space(unsigned int pci0Mem1Base,
				unsigned int pci0Mem1Length);
static void pci1MapMemory1space(unsigned int pci1Mem1Base,
				unsigned int pci1Mem1Length);
static unsigned int pci0GetIOspaceBase(void);
static unsigned int pci0GetIOspaceSize(void);
static unsigned int pci0GetMemory0Base(void);
static unsigned int pci0GetMemory0Size(void);
static unsigned int pci0GetMemory1Base(void);
static unsigned int pci0GetMemory1Size(void);
static unsigned int pci1GetIOspaceBase(void);
static unsigned int pci1GetIOspaceSize(void);
static unsigned int pci1GetMemory0Base(void);
static unsigned int pci1GetMemory0Size(void);
static unsigned int pci1GetMemory1Base(void);
static unsigned int pci1GetMemory1Size(void);


/*  Functions to implement "pci ops"  */
static int galileo_pcibios_read(struct pci_bus *bus, unsigned int devfn,
				int offset, int size, u32 * val);
static int galileo_pcibios_write(struct pci_bus *bus, unsigned int devfn,
				 int offset, int size, u32 val);
static void galileo_pcibios_set_master(struct pci_dev *dev);

/*
 *  General-purpose PCI functions.
 */

/*
 * pci0MapIOspace - Maps PCI0 IO space for the master.
 * Inputs: base and length of pci0Io
 */

static void pci0MapIOspace(unsigned int pci0IoBase,
			   unsigned int pci0IoLength)
{
	unsigned int pci0IoTop =
	    (unsigned int) (pci0IoBase + pci0IoLength);

	if (pci0IoLength == 0)
		pci0IoTop++;

	pci0IoBase = (unsigned int) (pci0IoBase >> 21);
	pci0IoTop = (unsigned int) (((pci0IoTop - 1) & 0x0fffffff) >> 21);
	GT_WRITE(GT_PCI0IOLD_OFS, pci0IoBase);
	GT_WRITE(GT_PCI0IOHD_OFS, pci0IoTop);
}

/*
 * pci1MapIOspace - Maps PCI1 IO space for the master.
 * Inputs: base and length of pci1Io
 */

static void pci1MapIOspace(unsigned int pci1IoBase,
			   unsigned int pci1IoLength)
{
	unsigned int pci1IoTop =
	    (unsigned int) (pci1IoBase + pci1IoLength);

	if (pci1IoLength == 0)
		pci1IoTop++;

	pci1IoBase = (unsigned int) (pci1IoBase >> 21);
	pci1IoTop = (unsigned int) (((pci1IoTop - 1) & 0x0fffffff) >> 21);
	GT_WRITE(GT_PCI1IOLD_OFS, pci1IoBase);
	GT_WRITE(GT_PCI1IOHD_OFS, pci1IoTop);
}

/*
 * pci0MapMemory0space - Maps PCI0 memory0 space for the master.
 * Inputs: base and length of pci0Mem0
 */

static void pci0MapMemory0space(unsigned int pci0Mem0Base,
				unsigned int pci0Mem0Length)
{
	unsigned int pci0Mem0Top = pci0Mem0Base + pci0Mem0Length;

	if (pci0Mem0Length == 0)
		pci0Mem0Top++;

	pci0Mem0Base = pci0Mem0Base >> 21;
	pci0Mem0Top = ((pci0Mem0Top - 1) & 0x0fffffff) >> 21;
	GT_WRITE(GT_PCI0M0LD_OFS, pci0Mem0Base);
	GT_WRITE(GT_PCI0M0HD_OFS, pci0Mem0Top);
}

/*
 * pci1MapMemory0space - Maps PCI1 memory0 space for the master.
 * Inputs: base and length of pci1Mem0
 */

static void pci1MapMemory0space(unsigned int pci1Mem0Base,
				unsigned int pci1Mem0Length)
{
	unsigned int pci1Mem0Top = pci1Mem0Base + pci1Mem0Length;

	if (pci1Mem0Length == 0)
		pci1Mem0Top++;

	pci1Mem0Base = pci1Mem0Base >> 21;
	pci1Mem0Top = ((pci1Mem0Top - 1) & 0x0fffffff) >> 21;
	GT_WRITE(GT_PCI1M0LD_OFS, pci1Mem0Base);
	GT_WRITE(GT_PCI1M0HD_OFS, pci1Mem0Top);
}

/*
 * pci0MapMemory1space - Maps PCI0 memory1 space for the master.
 * Inputs: base and length of pci0Mem1
 */

static void pci0MapMemory1space(unsigned int pci0Mem1Base,
				unsigned int pci0Mem1Length)
{
	unsigned int pci0Mem1Top = pci0Mem1Base + pci0Mem1Length;

	if (pci0Mem1Length == 0)
		pci0Mem1Top++;

	pci0Mem1Base = pci0Mem1Base >> 21;
	pci0Mem1Top = ((pci0Mem1Top - 1) & 0x0fffffff) >> 21;
	GT_WRITE(GT_PCI0M1LD_OFS, pci0Mem1Base);
	GT_WRITE(GT_PCI0M1HD_OFS, pci0Mem1Top);

}

/*
 * pci1MapMemory1space - Maps PCI1 memory1 space for the master.
 * Inputs: base and length of pci1Mem1
 */

static void pci1MapMemory1space(unsigned int pci1Mem1Base,
				unsigned int pci1Mem1Length)
{
	unsigned int pci1Mem1Top = pci1Mem1Base + pci1Mem1Length;

	if (pci1Mem1Length == 0)
		pci1Mem1Top++;

	pci1Mem1Base = pci1Mem1Base >> 21;
	pci1Mem1Top = ((pci1Mem1Top - 1) & 0x0fffffff) >> 21;
	GT_WRITE(GT_PCI1M1LD_OFS, pci1Mem1Base);
	GT_WRITE(GT_PCI1M1HD_OFS, pci1Mem1Top);
}

/*
 * pci0GetIOspaceBase - Return PCI0 IO Base Address.
 * Inputs: N/A
 * Returns: PCI0 IO Base Address.
 */

static unsigned int pci0GetIOspaceBase(void)
{
	unsigned int base;
	GT_READ(GT_PCI0IOLD_OFS, &base);
	base = base << 21;
	return base;
}

/*
 * pci0GetIOspaceSize - Return PCI0 IO Bar Size.
 * Inputs: N/A
 * Returns: PCI0 IO Bar Size.
 */

static unsigned int pci0GetIOspaceSize(void)
{
	unsigned int top, base, size;
	GT_READ(GT_PCI0IOLD_OFS, &base);
	base = base << 21;
	GT_READ(GT_PCI0IOHD_OFS, &top);
	top = (top << 21);
	size = ((top - base) & 0xfffffff);
	size = size | 0x1fffff;
	return (size + 1);
}

/*
 * pci0GetMemory0Base - Return PCI0 Memory 0 Base Address.
 * Inputs: N/A
 * Returns: PCI0 Memory 0 Base Address.
 */

static unsigned int pci0GetMemory0Base(void)
{
	unsigned int base;
	GT_READ(GT_PCI0M0LD_OFS, &base);
	base = base << 21;
	return base;
}

/*
 * pci0GetMemory0Size - Return PCI0 Memory 0 Bar Size.
 * Inputs: N/A
 * Returns: PCI0 Memory 0 Bar Size.
 */

static unsigned int pci0GetMemory0Size(void)
{
	unsigned int top, base, size;
	GT_READ(GT_PCI0M0LD_OFS, &base);
	base = base << 21;
	GT_READ(GT_PCI0M0HD_OFS, &top);
	top = (top << 21);
	size = ((top - base) & 0xfffffff);
	size = size | 0x1fffff;
	return (size + 1);
}

/*
 * pci0GetMemory1Base - Return PCI0 Memory 1 Base Address.
 * Inputs: N/A
 * Returns: PCI0 Memory 1 Base Address.
 */

static unsigned int pci0GetMemory1Base(void)
{
	unsigned int base;
	GT_READ(GT_PCI0M1LD_OFS, &base);
	base = base << 21;
	return base;
}

/*
 * pci0GetMemory1Size - Return PCI0 Memory 1 Bar Size.
 * Inputs: N/A
 * Returns: PCI0 Memory 1 Bar Size.
 */

static unsigned int pci0GetMemory1Size(void)
{
	unsigned int top, base, size;
	GT_READ(GT_PCI0M1LD_OFS, &base);
	base = base << 21;
	GT_READ(GT_PCI0M1HD_OFS, &top);
	top = (top << 21);
	size = ((top - base) & 0xfffffff);
	size = size | 0x1fffff;
	return (size + 1);
}

/*
 * pci1GetIOspaceBase - Return PCI1 IO Base Address.
 * Inputs: N/A
 * Returns: PCI1 IO Base Address.
 */

static unsigned int pci1GetIOspaceBase(void)
{
	unsigned int base;
	GT_READ(GT_PCI1IOLD_OFS, &base);
	base = base << 21;
	return base;
}

/*
 * pci1GetIOspaceSize - Return PCI1 IO Bar Size.
 * Inputs: N/A
 * Returns: PCI1 IO Bar Size.
 */

static unsigned int pci1GetIOspaceSize(void)
{
	unsigned int top, base, size;
	GT_READ(GT_PCI1IOLD_OFS, &base);
	base = base << 21;
	GT_READ(GT_PCI1IOHD_OFS, &top);
	top = (top << 21);
	size = ((top - base) & 0xfffffff);
	size = size | 0x1fffff;
	return (size + 1);
}

/*
 * pci1GetMemory0Base - Return PCI1 Memory 0 Base Address.
 * Inputs: N/A
 * Returns: PCI1 Memory 0 Base Address.
 */

static unsigned int pci1GetMemory0Base(void)
{
	unsigned int base;
	GT_READ(GT_PCI1M0LD_OFS, &base);
	base = base << 21;
	return base;
}

/*
 * pci1GetMemory0Size - Return PCI1 Memory 0 Bar Size.
 * Inputs: N/A
 * Returns: PCI1 Memory 0 Bar Size.
 */

static unsigned int pci1GetMemory0Size(void)
{
	unsigned int top, base, size;
	GT_READ(GT_PCI1M1LD_OFS, &base);
	base = base << 21;
	GT_READ(GT_PCI1M1HD_OFS, &top);
	top = (top << 21);
	size = ((top - base) & 0xfffffff);
	size = size | 0x1fffff;
	return (size + 1);
}

/*
 * pci1GetMemory1Base - Return PCI1 Memory 1 Base Address.
 * Inputs: N/A
 * Returns: PCI1 Memory 1 Base Address.
 */

static unsigned int pci1GetMemory1Base(void)
{
	unsigned int base;
	GT_READ(GT_PCI1M1LD_OFS, &base);
	base = base << 21;
	return base;
}

/*
 * pci1GetMemory1Size - Return PCI1 Memory 1 Bar Size.
 * Inputs: N/A
 * Returns: PCI1 Memory 1 Bar Size.
 */

static unsigned int pci1GetMemory1Size(void)
{
	unsigned int top, base, size;
	GT_READ(GT_PCI1M1LD_OFS, &base);
	base = base << 21;
	GT_READ(GT_PCI1M1HD_OFS, &top);
	top = (top << 21);
	size = ((top - base) & 0xfffffff);
	size = size | 0x1fffff;
	return (size + 1);
}



/*
 * pci_range_ck -
 *
 * Check if the pci device that are trying to access does really exists
 * on the evaluation board.
 *
 * Inputs :
 * bus - bus number (0 for PCI 0 ; 1 for PCI 1)
 * dev - number of device on the specific pci bus
 *
 * Outpus :
 * 0 - if OK , 1 - if failure
 */
static __inline__ int pci_range_ck(unsigned char bus, unsigned char dev)
{
	/*
	 * We don't even pretend to handle other busses than bus 0 correctly.
	 * Accessing device 31 crashes the CP7000 for some reason.
	 */
	if ((bus == 0) && (dev != 31))
		return 0;
	return -1;
}

/*
 * pciXReadConfigReg  - Read from a PCI configuration register
 *                    - Make sure the GT is configured as a master before
 *                      reading from another device on the PCI.
 *                   - The function takes care of Big/Little endian conversion.
 * INPUTS:   regOffset: The register offset as it apears in the GT spec (or PCI
 *                        spec)
 *           pciDevNum: The device number needs to be addressed.
 * RETURNS: data , if the data == 0xffffffff check the master abort bit in the
 *                 cause register to make sure the data is valid
 *
 *  Configuration Address 0xCF8:
 *
 *       31 30    24 23  16 15  11 10     8 7      2  0     <=bit Number
 *  |congif|Reserved|  Bus |Device|Function|Register|00|
 *  |Enable|        |Number|Number| Number | Number |  |    <=field Name
 *
 */
static unsigned int pci0ReadConfigReg(int offset, struct pci_dev *device)
{
	unsigned int DataForRegCf8;
	unsigned int data;

	DataForRegCf8 = ((PCI_SLOT(device->devfn) << 11) |
			 (PCI_FUNC(device->devfn) << 8) |
			 (offset & ~0x3)) | 0x80000000;
	GT_WRITE(GT_PCI0_CFGADDR_OFS, DataForRegCf8);

	/*
	 * The casual observer might wonder why the READ is duplicated here,
	 * rather than immediately following the WRITE, and just have the swap
	 * in the "if".  That's because there is a latency problem with trying
	 * to read immediately after setting up the address register.  The "if"
	 * check gives enough time for the address to stabilize, so the READ
	 * can work.
	 */
	if (PCI_SLOT(device->devfn) == SELF) {	/* This board */
		GT_READ(GT_PCI0_CFGDATA_OFS, &data);
		return data;
	} else {		/* The PCI is working in LE Mode so swap the Data. */
		GT_READ(GT_PCI0_CFGDATA_OFS, &data);
		return cpu_to_le32(data);
	}
}

static unsigned int pci1ReadConfigReg(int offset, struct pci_dev *device)
{
	unsigned int DataForRegCf8;
	unsigned int data;

	DataForRegCf8 = ((PCI_SLOT(device->devfn) << 11) |
			 (PCI_FUNC(device->devfn) << 8) |
			 (offset & ~0x3)) | 0x80000000;
	/*
	 * The casual observer might wonder why the READ is duplicated here,
	 * rather than immediately following the WRITE, and just have the
	 * swap in the "if".  That's because there is a latency problem
	 * with trying to read immediately after setting up the address
	 * register.  The "if" check gives enough time for the address
	 * to stabilize, so the READ can work.
	 */
	if (PCI_SLOT(device->devfn) == SELF) {	/* This board */
		/* when configurating our own PCI 1 L-unit the access is through
		   the PCI 0 interface with reg number = reg number + 0x80 */
		DataForRegCf8 |= 0x80;
		GT_WRITE(GT_PCI0_CFGADDR_OFS, DataForRegCf8);
	} else {		/* The PCI is working in LE Mode so swap the Data. */
		GT_WRITE(GT_PCI1_CFGADDR_OFS, DataForRegCf8);
	}
	if (PCI_SLOT(device->devfn) == SELF) {	/* This board */
		GT_READ(GT_PCI0_CFGDATA_OFS, &data);
		return data;
	} else {
		GT_READ(GT_PCI1_CFGDATA_OFS, &data);
		return cpu_to_le32(data);
	}
}



/*
 * pciXWriteConfigReg - Write to a PCI configuration register
 *                    - Make sure the GT is configured as a master before
 *                      writingto another device on the PCI.
 *                    - The function takes care of Big/Little endian conversion.
 * Inputs:   unsigned int regOffset: The register offset as it apears in the
 *           GT spec
 *                   (or any other PCI device spec)
 *           pciDevNum: The device number needs to be addressed.
 *
 *  Configuration Address 0xCF8:
 *
 *       31 30    24 23  16 15  11 10     8 7      2  0     <=bit Number
 *  |congif|Reserved|  Bus |Device|Function|Register|00|
 *  |Enable|        |Number|Number| Number | Number |  |    <=field Name
 *
 */
static void pci0WriteConfigReg(unsigned int offset,
			       struct pci_dev *device, unsigned int data)
{
	unsigned int DataForRegCf8;

	DataForRegCf8 = ((PCI_SLOT(device->devfn) << 11) |
			 (PCI_FUNC(device->devfn) << 8) |
			 (offset & ~0x3)) | 0x80000000;
	GT_WRITE(GT_PCI0_CFGADDR_OFS, DataForRegCf8);
	if (PCI_SLOT(device->devfn) == SELF) {	/* This board */
		GT_WRITE(GT_PCI0_CFGDATA_OFS, data);
	} else {		/* configuration Transaction over the pci. */
		/* The PCI is working in LE Mode so swap the Data. */
		GT_WRITE(GT_PCI0_CFGDATA_OFS, le32_to_cpu(data));
	}
}

static void pci1WriteConfigReg(unsigned int offset,
			       struct pci_dev *device, unsigned int data)
{
	unsigned int DataForRegCf8;

	DataForRegCf8 = ((PCI_SLOT(device->devfn) << 11) |
			 (PCI_FUNC(device->devfn) << 8) |
			 (offset & ~0x3)) | 0x80000000;
	/*
	 * There is a latency problem
	 * with trying to read immediately after setting up the address
	 * register.  The "if" check gives enough time for the address
	 * to stabilize, so the WRITE can work.
	 */
	if (PCI_SLOT(device->devfn) == SELF) {	/* This board */
		/*
		 * when configurating our own PCI 1 L-unit the access is through
		 * the PCI 0 interface with reg number = reg number + 0x80
		 */
		DataForRegCf8 |= 0x80;
		GT_WRITE(GT_PCI0_CFGADDR_OFS, DataForRegCf8);
	} else {		/* configuration Transaction over the pci. */
		/* The PCI is working in LE Mode so swap the Data. */
		GT_WRITE(GT_PCI1_CFGADDR_OFS, DataForRegCf8);
	}
	if (PCI_SLOT(device->devfn) == SELF) {	/* This board */
		GT_WRITE(GT_PCI0_CFGDATA_OFS, data);
	} else {		/* configuration Transaction over the pci. */
		GT_WRITE(GT_PCI1_CFGADDR_OFS, le32_to_cpu(data));
	}
}


/*
 * galileo_pcibios_(read/write) -
 *
 * reads/write a dword/word/byte register from the configuration space
 * of a device.
 *
 * Inputs :
 * bus - bus number
 * devfn - device function index
 * offset - register offset in the configuration space
 * size - size of value (1=byte,2=word,4-dword)
 * val - value to be written / read
 *
 * Outputs :
 * PCIBIOS_SUCCESSFUL when operation was succesfull
 * PCIBIOS_DEVICE_NOT_FOUND when the bus or dev is errorneous
 * PCIBIOS_BAD_REGISTER_NUMBER when accessing non aligned
 */

static int galileo_pcibios_read(struct pci_bus *bus, unsigned int devfn,
				int offset, int size, u32 * val)
{
	int dev, busnum;

	busnum = bus->number;
	dev = PCI_SLOT(devfn);

	if (pci_range_ck(busnum, dev)) {
		if (size == 1)
			*val = (u8) 0xff;
		else if (size == 2)
			*val = (u16) 0xffff;
		else if (size == 4)
			*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	if ((size == 2) && (offset & 0x1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (offset & 0x3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (busnum == 0) {
		if (size == 1) {
			*val = (u8) (pci0ReadConfigReg(offset, bus->dev) >>
				     ((offset & ~0x3) * 8));
		} else if (size == 2) {
			*val =
			    (u16) (pci0ReadConfigReg(offset, bus->dev) >>
				   ((offset & ~0x3) * 8));
		} else if (size == 4) {
			*val = pci0ReadConfigReg(offset, bus->dev);
		}
	}

	/*
	 *  This is so that the upper PCI layer will get the correct return
	 * value if we're not attached to anything.
	 */
	switch (size) {
	case 1:
		if ((offset == 0xe) && (*val == (u8) 0xff)) {
			u32 MasterAbort;
			GT_READ(GT_INTRCAUSE_OFS, &MasterAbort);
			if (MasterAbort & 0x40000) {
				GT_WRITE(GT_INTRCAUSE_OFS,
					 (MasterAbort & 0xfffbffff));
				return PCIBIOS_DEVICE_NOT_FOUND;
			}
		}
		break;
	case 4:
		if ((offset == 0) && (*val == 0xffffffff)) {
			return PCIBIOS_DEVICE_NOT_FOUND;
		}
	break}
	return PCIBIOS_SUCCESSFUL;
}

static int galileo_pcibios_write(struct pci_bus *bus, unsigned int devfn,
				 int offset, int size, u32 val)
{
	int dev, busnum;
	unsigned long tmp;

	busnum = bus->number;
	dev = PCI_SLOT(devfn);

	if (pci_range_ck(busnum, dev))
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (size == 4) {
		if (offset & 0x3)
			return PCIBIOS_BAD_REGISTER_NUMBER;
		if (busnum == 0)
			pci0WriteConfigReg(offset, bus->dev, val);
		//if (busnum == 1) pci1WriteConfigReg (offset,bus->dev,val);
		return PCIBIOS_SUCCESSFUL;
	}
	if ((size == 2) && (offset & 0x1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (busnum == 0) {
		tmp = pci0ReadConfigReg(offset, bus->dev);
		//if (busnum == 1) tmp = pci1ReadConfigReg (offset,bus->dev);
		if (size == 1) {
			if ((offset % 4) == 0)
				tmp =
				    (tmp & 0xffffff00) | (val & (u8) 0xff);
			if ((offset % 4) == 1)
				tmp =
				    (tmp & 0xffff00ff) | ((val & (u8) 0xff)
							  << 8);
			if ((offset % 4) == 2)
				tmp =
				    (tmp & 0xff00ffff) | ((val & (u8) 0xff)
							  << 16);
			if ((offset % 4) == 3)
				tmp =
				    (tmp & 0x00ffffff) | ((val & (u8) 0xff)
							  << 24);
		} else if (size == 2) {
			if ((offset % 4) == 0)
				tmp =
				    (tmp & 0xffff0000) | (val & (u16)
							  0xffff);
			if ((offset % 4) == 2)
				tmp =
				    (tmp & 0x0000ffff) |
				    ((val & (u16) 0xffff) << 16);
		}
		if (busnum == 0)
			pci0WriteConfigReg(offset, bus->dev, tmp);
		//if (busnum == 1) pci1WriteConfigReg (offset,bus->dev,tmp);
	}
	return PCIBIOS_SUCCESSFUL;
}

static void galileo_pcibios_set_master(struct pci_dev *dev)
{
	u16 cmd;

	galileo_pcibios_read(dev->bus, dev->devfn, PCI_COMMAND, 2, &cmd);
	cmd |= PCI_COMMAND_MASTER;
	galileo_pcibios_write(dev->bus, dev->devfn, PCI_COMMAND, 2, cmd);
}

/*  Externally-expected functions.  Do not change function names  */

int pcibios_enable_resources(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	u8 tmp1;
	int idx;
	struct resource *r;

	galileo_pcibios_read(dev->bus, dev->devfn, PCI_COMMAND, 2, &cmd);
	old_cmd = cmd;
	for (idx = 0; idx < 6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR
			       "PCI: Device %s not available because of "
			       "resource collisions\n", pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		galileo_pcibios_write(dev->bus, dev->devfn, PCI_COMMAND, 2,
				      cmd);
	}

	/*
	 * Let's fix up the latency timer and cache line size here.  Cache
	 * line size = 32 bytes / sizeof dword (4) = 8.
	 * Latency timer must be > 8.  32 is random but appears to work.
	 */
	galileo_pcibios_read(dev->bus, dev->devfn, PCI_CACHE_LINE_SIZE, 1,
			     &tmp1);
	if (tmp1 != 8) {
		printk(KERN_WARNING
		       "PCI setting cache line size to 8 from " "%d\n",
		       tmp1);
		galileo_pcibios_write(dev->bus, dev->devfn,
				      PCI_CACHE_LINE_SIZE, 1, 8);
	}
	galileo_pcibios_read(dev->bus, dev->devfn, PCI_LATENCY_TIMER, 1,
			     &tmp1);
	if (tmp1 < 32) {
		printk(KERN_WARNING
		       "PCI setting latency timer to 32 from %d\n", tmp1);
		galileo_pcibios_write(dev->bus, dev->devfn,
				      PCI_LATENCY_TIMER, 1, 32);
	}

	return 0;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	return pcibios_enable_resources(dev);
}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
{
	struct pci_dev *dev = data;

	if (res->flags & IORESOURCE_IO) {
		unsigned long start = res->start;

		/* We need to avoid collisions with `mirrored' VGA ports
		   and other strange ISA hardware, so we always want the
		   addresses kilobyte aligned.  */
		if (size > 0x100) {
			printk(KERN_ERR "PCI: I/O Region %s/%d too large"
			       " (%ld bytes)\n", pci_name(dev),
			       dev->resource - res, size);
		}

		start = (start + 1024 - 1) & ~(1024 - 1);
		res->start = start;
	}
}

struct pci_ops galileo_pci_ops = {
	.read = galileo_pcibios_read,
	.write = galileo_pcibios_write,
};

struct pci_fixup pcibios_fixups[] = {
	{0}
};

void __devinit pcibios_fixup_bus(struct pci_bus *c)
{
	gt64120_board_pcibios_fixup_bus(c);
}

/*
 * This code was derived from Galileo Technology's example
 * and significantly reworked.
 *
 * This is very simple.  It does not scan multiple function devices.  It does
 * not scan behind bridges.  Those would be simple to implement, but we don't
 * currently need this.
 */

static void __init scan_and_initialize_pci(void)
{
	struct pci_device pci_devices[MAX_PCI_DEVS];

	if (scan_pci_bus(pci_devices)) {
		allocate_pci_space(pci_devices);
	}
}

/*
 * This is your basic PCI scan.  It goes through each slot and checks to
 * see if there's something that responds.  If so, then get the size and
 * type of each of the responding BARs.  Save them for later.
 */

static u32 __init scan_pci_bus(struct pci_device *pci_devices)
{
	u32 arrayCounter = 0;
	u32 memType;
	u32 memSize;
	u32 pci_slot, bar;
	u32 id;
	u32 c18RegValue;
	struct pci_dev device;

	/*
	 * According to PCI REV 2.1 MAX agents on the bus are 21.
	 * We don't bother scanning ourselves (slot 0).
	 */
	for (pci_slot = 1; pci_slot < 22; pci_slot++) {

		device.devfn = PCI_DEVFN(pci_slot, 0);
		id = pci0ReadConfigReg(PCI_VENDOR_ID, &device);

		/*
		 *  Check for a PCI Master Abort (nothing responds in the
		 * slot)
		 */
		GT_READ(GT_INTRCAUSE_OFS, &c18RegValue);
		/*
		 * Clearing bit 18 of in the Cause Register 0xc18 by
		 * writting 0.
		 */
		GT_WRITE(GT_INTRCAUSE_OFS, (c18RegValue & 0xfffbffff));
		if ((id != 0xffffffff) && !(c18RegValue & 0x40000)) {
			pci_devices[arrayCounter].slot = pci_slot;
			for (bar = 0; bar < 6; bar++) {
				memType =
				    pci0ReadConfigReg(PCI_BASE_ADDRESS_0 +
						      (bar * 4), &device);
				pci_devices[arrayCounter].BARtype[bar] =
				    memType & 1;
				pci0WriteConfigReg(PCI_BASE_ADDRESS_0 +
						   (bar * 4), &device,
						   0xffffffff);
				memSize =
				    pci0ReadConfigReg(PCI_BASE_ADDRESS_0 +
						      (bar * 4), &device);
				if (memType & 1) {	/*  IO space  */
					pci_devices[arrayCounter].
					    BARsize[bar] =
					    ~(memSize & 0xfffffffc) + 1;
				} else {	/*  memory space */
					pci_devices[arrayCounter].
					    BARsize[bar] =
					    ~(memSize & 0xfffffff0) + 1;
				}
			}	/*  BAR counter  */

			arrayCounter++;
		}
		/*  found a device  */
	}			/*  slot counter  */

	if (arrayCounter < MAX_PCI_DEVS)
		pci_devices[arrayCounter].slot = -1;

	return arrayCounter;
}

/*
 * This function goes through the list of devices and allocates the BARs in
 * either IO or MEM space.  It does it in order of size, which will limit the
 * amount of fragmentation we have in the IO and MEM spaces.
 */

static void __init allocate_pci_space(struct pci_device *pci_devices)
{
	u32 count, maxcount, bar;
	u32 maxSize, maxDevice, maxBAR;
	u32 alignto;
	u32 base;
	u32 pci0_mem_base = pci0GetMemory0Base();
	u32 pci0_io_base = pci0GetIOspaceBase();
	struct pci_dev device;

	/*  How many PCI devices do we have?  */
	maxcount = MAX_PCI_DEVS;
	for (count = 0; count < MAX_PCI_DEVS; count++) {
		if (pci_devices[count].slot == -1) {
			maxcount = count;
			break;
		}
	}

	do {
		/*  Find the largest size BAR we need to allocate  */
		maxSize = 0;
		for (count = 0; count < maxcount; count++) {
			for (bar = 0; bar < 6; bar++) {
				if (pci_devices[count].BARsize[bar] >
				    maxSize) {
					maxSize =
					    pci_devices[count].
					    BARsize[bar];
					maxDevice = count;
					maxBAR = bar;
				}
			}
		}

		/*
		 * We've found the largest BAR.  Allocate it into IO or
		 * mem space.  We don't idiot check the bases to make
		 * sure they haven't overflowed the current size for that
		 * aperture.
		 * Don't bother to enable the device's IO or MEM space here.
		 * That will be done in pci_enable_resources if the device is
		 * activated by a driver.
		 */
		if (maxSize) {
			device.devfn =
			    PCI_DEVFN(pci_devices[maxDevice].slot, 0);
			if (pci_devices[maxDevice].BARtype[maxBAR] == 1) {
				alignto = max(0x1000U, maxSize);
				base = ALIGN(pci0_io_base, alignto);
				pci0WriteConfigReg(PCI_BASE_ADDRESS_0 +
						   (maxBAR * 4), &device,
						   base | 0x1);
				pci0_io_base = base + alignto;
			} else {
				alignto = max(0x1000U, maxSize);
				base = ALIGN(pci0_mem_base, alignto);
				pci0WriteConfigReg(PCI_BASE_ADDRESS_0 +
						   (maxBAR * 4), &device,
						   base);
				pci0_mem_base = base + alignto;
			}
			/*
			 * This entry is finished.  Remove it from the list
			 * we'll scan.
			 */
			pci_devices[maxDevice].BARsize[maxBAR] = 0;
		}
	} while (maxSize);
}

static int __init pcibios_init(void)
{
	u32 tmp;
	struct pci_dev controller;

	controller.devfn = SELF;

	GT_READ(GT_PCI0_CMD_OFS, &tmp);
	GT_READ(GT_PCI0_BARE_OFS, &tmp);

	/*
	 * You have to enable bus mastering to configure any other
	 * card on the bus.
	 */
	tmp = pci0ReadConfigReg(PCI_COMMAND, &controller);
	tmp |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	pci0WriteConfigReg(PCI_COMMAND, &controller, tmp);

	/*  This scans the PCI bus and sets up initial values.  */
	scan_and_initialize_pci();

	/*
	 *  Reset PCI I/O and PCI MEM values to ones supported by EVM.
	 */
	ioport_resource.start = GT_PCI_IO_BASE;
	ioport_resource.end = GT_PCI_IO_BASE + GT_PCI_IO_SIZE - 1;
	iomem_resource.start = GT_PCI_MEM_BASE;
	iomem_resource.end = GT_PCI_MEM_BASE + GT_PCI_MEM_BASE - 1;

	pci_scan_bus(0, &galileo_pci_ops, NULL);

	return 0;
}

subsys_initcall(pcibios_init);

/*
 * for parsing "pci=" kernel boot arguments.
 */
char *pcibios_setup(char *str)
{
	printk(KERN_INFO "rr: pcibios_setup\n");
	/* Nothing to do for now.  */

	return str;
}

unsigned __init int pcibios_assign_all_busses(void)
{
	return 1;
}
