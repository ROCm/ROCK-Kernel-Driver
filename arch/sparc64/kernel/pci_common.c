/* $Id: pci_common.c,v 1.12 2000/05/01 06:32:49 davem Exp $
 * pci_common.c: PCI controller common support.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/pbm.h>

/* Find the OBP PROM device tree node for a PCI device.
 * Return zero if not found.
 */
static int __init find_device_prom_node(struct pci_pbm_info *pbm,
					struct pci_dev *pdev,
					int bus_prom_node,
					struct linux_prom_pci_registers *pregs,
					int *nregs)
{
	int node;

	/*
	 * Return the PBM's PROM node in case we are it's PCI device,
	 * as the PBM's reg property is different to standard PCI reg
	 * properties. We would delete this device entry otherwise,
	 * which confuses XFree86's device probing...
	 */
	if ((pdev->bus->number == pbm->pci_bus->number) && (pdev->devfn == 0) &&
	    (pdev->vendor == PCI_VENDOR_ID_SUN) &&
	    (pdev->device == PCI_DEVICE_ID_SUN_PBM)) {
		*nregs = 0;
		return bus_prom_node;
	}

	node = prom_getchild(bus_prom_node);
	while (node != 0) {
		int err = prom_getproperty(node, "reg",
					   (char *)pregs,
					   sizeof(*pregs) * PROMREG_MAX);
		if (err == 0 || err == -1)
			goto do_next_sibling;
		if (((pregs[0].phys_hi >> 8) & 0xff) == pdev->devfn) {
			*nregs = err / sizeof(*pregs);
			return node;
		}

	do_next_sibling:
		node = prom_getsibling(node);
	}
	return 0;
}

/* Remove a PCI device from the device trees, then
 * free it up.  Note that this must run before
 * the device's resources are registered because we
 * do not handle unregistering them here.
 */
static void pci_device_delete(struct pci_dev *pdev)
{
	list_del(&pdev->global_list);
	list_del(&pdev->bus_list);

	/* Ok, all references are gone, free it up. */
	kfree(pdev);
}

/* Older versions of OBP on PCI systems encode 64-bit MEM
 * space assignments incorrectly, this fixes them up.
 */
static void __init fixup_obp_assignments(struct pcidev_cookie *pcp)
{
	int i;

	for (i = 0; i < pcp->num_prom_assignments; i++) {
		struct linux_prom_pci_registers *ap;
		int space;

		ap = &pcp->prom_assignments[i];
		space = ap->phys_hi >> 24;
		if ((space & 0x3) == 2 &&
		    (space & 0x4) != 0) {
			ap->phys_hi &= ~(0x7 << 24);
			ap->phys_hi |= 0x3 << 24;
		}
	}
}

/* Fill in the PCI device cookie sysdata for the given
 * PCI device.  This cookie is the means by which one
 * can get to OBP and PCI controller specific information
 * for a PCI device.
 */
static void __init pdev_cookie_fillin(struct pci_pbm_info *pbm,
				      struct pci_dev *pdev,
				      int bus_prom_node)
{
	struct linux_prom_pci_registers pregs[PROMREG_MAX];
	struct pcidev_cookie *pcp;
	int device_prom_node, nregs, err;

	device_prom_node = find_device_prom_node(pbm, pdev, bus_prom_node,
						 pregs, &nregs);
	if (device_prom_node == 0) {
		/* If it is not in the OBP device tree then
		 * there must be a damn good reason for it.
		 *
		 * So what we do is delete the device from the
		 * PCI device tree completely.  This scenerio
		 * is seen, for example, on CP1500 for the
		 * second EBUS/HappyMeal pair if the external
		 * connector for it is not present.
		 */
		pci_device_delete(pdev);
		return;
	}

	pcp = kmalloc(sizeof(*pcp), GFP_ATOMIC);
	if (pcp == NULL) {
		prom_printf("PCI_COOKIE: Fatal malloc error, aborting...\n");
		prom_halt();
	}
	pcp->pbm = pbm;
	pcp->prom_node = device_prom_node;
	memcpy(pcp->prom_regs, pregs, sizeof(pcp->prom_regs));
	pcp->num_prom_regs = nregs;
	err = prom_getproperty(device_prom_node, "name",
			       pcp->prom_name, sizeof(pcp->prom_name));
	if (err > 0)
		pcp->prom_name[err] = 0;
	else
		pcp->prom_name[0] = 0;
	if (strcmp(pcp->prom_name, "ebus") == 0) {
		struct linux_prom_ebus_ranges erng[PROM_PCIRNG_MAX];
		int iter;

		/* EBUS is special... */
		err = prom_getproperty(device_prom_node, "ranges",
				       (char *)&erng[0], sizeof(erng));
		if (err == 0 || err == -1) {
			prom_printf("EBUS: Fatal error, no range property\n");
			prom_halt();
		}
		err = (err / sizeof(erng[0]));
		for(iter = 0; iter < err; iter++) {
			struct linux_prom_ebus_ranges *ep = &erng[iter];
			struct linux_prom_pci_registers *ap;

			ap = &pcp->prom_assignments[iter];

			ap->phys_hi = ep->parent_phys_hi;
			ap->phys_mid = ep->parent_phys_mid;
			ap->phys_lo = ep->parent_phys_lo;
			ap->size_hi = 0;
			ap->size_lo = ep->size;
		}
		pcp->num_prom_assignments = err;
	} else {
		err = prom_getproperty(device_prom_node,
				       "assigned-addresses",
				       (char *)pcp->prom_assignments,
				       sizeof(pcp->prom_assignments));
		if (err == 0 || err == -1)
			pcp->num_prom_assignments = 0;
		else
			pcp->num_prom_assignments =
				(err / sizeof(pcp->prom_assignments[0]));
	}

	fixup_obp_assignments(pcp);

	pdev->sysdata = pcp;
}

void __init pci_fill_in_pbm_cookies(struct pci_bus *pbus,
				    struct pci_pbm_info *pbm,
				    int prom_node)
{
	struct list_head *walk = &pbus->devices;

	/* This loop is coded like this because the cookie
	 * fillin routine can delete devices from the tree.
	 */
	walk = walk->next;
	while (walk != &pbus->devices) {
		struct pci_dev *pdev = pci_dev_b(walk);
		struct list_head *walk_next = walk->next;

		pdev_cookie_fillin(pbm, pdev, prom_node);

		walk = walk_next;
	}

	walk = &pbus->children;
	walk = walk->next;
	while (walk != &pbus->children) {
		struct pci_bus *this_pbus = pci_bus_b(walk);
		struct pcidev_cookie *pcp = this_pbus->self->sysdata;
		struct list_head *walk_next = walk->next;

		pci_fill_in_pbm_cookies(this_pbus, pbm, pcp->prom_node);

		walk = walk_next;
	}
}

static void __init bad_assignment(struct linux_prom_pci_registers *ap,
				  struct resource *res,
				  int do_prom_halt)
{
	prom_printf("PCI: Bogus PROM assignment.\n");
	if (ap)
		prom_printf("PCI: phys[%08x:%08x:%08x] size[%08x:%08x]\n",
			    ap->phys_hi, ap->phys_mid, ap->phys_lo,
			    ap->size_hi, ap->size_lo);
	if (res)
		prom_printf("PCI: RES[%016lx-->%016lx:(%lx)]\n",
			    res->start, res->end, res->flags);
	prom_printf("Please email this information to davem@redhat.com\n");
	if (do_prom_halt)
		prom_halt();
}

static struct resource *
__init get_root_resource(struct linux_prom_pci_registers *ap,
			 struct pci_pbm_info *pbm)
{
	int space = (ap->phys_hi >> 24) & 3;

	switch (space) {
	case 0:
		/* Configuration space, silently ignore it. */
		return NULL;

	case 1:
		/* 16-bit IO space */
		return &pbm->io_space;

	case 2:
		/* 32-bit MEM space */
		return &pbm->mem_space;

	case 3:
		/* 64-bit MEM space, these are allocated out of
		 * the 32-bit mem_space range for the PBM, ie.
		 * we just zero out the upper 32-bits.
		 */
		return &pbm->mem_space;

	default:
		printk("PCI: What is resource space %x? "
		       "Tell davem@redhat.com about it!\n", space);
		return NULL;
	};
}

static struct resource *
__init get_device_resource(struct linux_prom_pci_registers *ap,
			   struct pci_dev *pdev)
{
	struct resource *res;
	int breg = (ap->phys_hi & 0xff);
	int space = (ap->phys_hi >> 24) & 3;

	switch (breg) {
	case  PCI_ROM_ADDRESS:
		/* It had better be MEM space. */
		if (space != 2)
			bad_assignment(ap, NULL, 0);

		res = &pdev->resource[PCI_ROM_RESOURCE];
		break;

	case PCI_BASE_ADDRESS_0:
	case PCI_BASE_ADDRESS_1:
	case PCI_BASE_ADDRESS_2:
	case PCI_BASE_ADDRESS_3:
	case PCI_BASE_ADDRESS_4:
	case PCI_BASE_ADDRESS_5:
		res = &pdev->resource[(breg - PCI_BASE_ADDRESS_0) / 4];
		break;

	default:
		bad_assignment(ap, NULL, 0);
		res = NULL;
		break;
	};

	return res;
}

static void __init pdev_record_assignments(struct pci_pbm_info *pbm,
					   struct pci_dev *pdev)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	int i;

	for (i = 0; i < pcp->num_prom_assignments; i++) {
		struct linux_prom_pci_registers *ap;
		struct resource *root, *res;

		/* The format of this property is specified in
		 * the PCI Bus Binding to IEEE1275-1994.
		 */
		ap = &pcp->prom_assignments[i];
		root = get_root_resource(ap, pbm);
		res = get_device_resource(ap, pdev);
		if (root == NULL || res == NULL)
			continue;

		/* Ok we know which resource this PROM assignment is
		 * for, sanity check it.
		 */
		if ((res->start & 0xffffffffUL) != ap->phys_lo)
			bad_assignment(ap, res, 1);

		/* If it is a 64-bit MEM space assignment, verify that
		 * the resource is too and that the upper 32-bits match.
		 */
		if (((ap->phys_hi >> 24) & 3) == 3) {
			if (((res->flags & IORESOURCE_MEM) == 0) ||
			    ((res->flags & PCI_BASE_ADDRESS_MEM_TYPE_MASK)
			     != PCI_BASE_ADDRESS_MEM_TYPE_64))
				bad_assignment(ap, res, 1);
			if ((res->start >> 32) != ap->phys_mid)
				bad_assignment(ap, res, 1);

			/* PBM cannot generate cpu initiated PIOs
			 * to the full 64-bit space.  Therefore the
			 * upper 32-bits better be zero.  If it is
			 * not, just skip it and we will assign it
			 * properly ourselves.
			 */
			if ((res->start >> 32) != 0UL) {
				printk(KERN_ERR "PCI: OBP assigns out of range MEM address "
				       "%016lx for region %ld on device %s\n",
				       res->start, (res - &pdev->resource[0]), pdev->name);
				continue;
			}
		}

		/* Adjust the resource into the physical address space
		 * of this PBM.
		 */
		pbm->parent->resource_adjust(pdev, res, root);

		if (request_resource(root, res) < 0) {
			/* OK, there is some conflict.  But this is fine
			 * since we'll reassign it in the fixup pass.
			 * Nevertheless notify the user that OBP made
			 * an error.
			 */
			printk(KERN_ERR "PCI: Address space collision on region %ld "
			       "of device %s\n",
			       (res - &pdev->resource[0]), pdev->name);
		}
	}
}

void __init pci_record_assignments(struct pci_pbm_info *pbm,
				   struct pci_bus *pbus)
{
	struct list_head *walk = &pbus->devices;

	for (walk = walk->next; walk != &pbus->devices; walk = walk->next)
		pdev_record_assignments(pbm, pci_dev_b(walk));

	walk = &pbus->children;
	for (walk = walk->next; walk != &pbus->children; walk = walk->next)
		pci_record_assignments(pbm, pci_bus_b(walk));
}

static void __init pdev_assign_unassigned(struct pci_pbm_info *pbm,
					  struct pci_dev *pdev)
{
	u32 reg;
	u16 cmd;
	int i, io_seen, mem_seen;

	io_seen = mem_seen = 0;
	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *root, *res;
		unsigned long size, min, max, align;

		res = &pdev->resource[i];

		if (res->flags & IORESOURCE_IO)
			io_seen++;
		else if (res->flags & IORESOURCE_MEM)
			mem_seen++;

		/* If it is already assigned or the resource does
		 * not exist, there is nothing to do.
		 */
		if (res->parent != NULL || res->flags == 0UL)
			continue;

		/* Determine the root we allocate from. */
		if (res->flags & IORESOURCE_IO) {
			root = &pbm->io_space;
			min = root->start + 0x400UL;
			max = root->end;
		} else {
			root = &pbm->mem_space;
			min = root->start;
			max = min + 0x80000000UL;
		}

		size = res->end - res->start;
		align = size + 1;
		if (allocate_resource(root, res, size + 1, min, max, align, NULL, NULL) < 0) {
			/* uh oh */
			prom_printf("PCI: Failed to allocate resource %d for %s\n",
				    i, pdev->name);
			prom_halt();
		}

		/* Update PCI config space. */
		pbm->parent->base_address_update(pdev, i);
	}

	/* Special case, disable the ROM.  Several devices
	 * act funny (ie. do not respond to memory space writes)
	 * when it is left enabled.  A good example are Qlogic,ISP
	 * adapters.
	 */
	pci_read_config_dword(pdev, PCI_ROM_ADDRESS, &reg);
	reg &= ~PCI_ROM_ADDRESS_ENABLE;
	pci_write_config_dword(pdev, PCI_ROM_ADDRESS, reg);

	/* If we saw I/O or MEM resources, enable appropriate
	 * bits in PCI command register.
	 */
	if (io_seen || mem_seen) {
		pci_read_config_word(pdev, PCI_COMMAND, &cmd);
		if (io_seen)
			cmd |= PCI_COMMAND_IO;
		if (mem_seen)
			cmd |= PCI_COMMAND_MEMORY;
		pci_write_config_word(pdev, PCI_COMMAND, cmd);
	}

	/* If this is a PCI bridge or an IDE controller,
	 * enable bus mastering.  In the former case also
	 * set the cache line size correctly.
	 */
	if (((pdev->class >> 8) == PCI_CLASS_BRIDGE_PCI) ||
	    (((pdev->class >> 8) == PCI_CLASS_STORAGE_IDE) &&
	     ((pdev->class & 0x80) != 0))) {
		pci_read_config_word(pdev, PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MASTER;
		pci_write_config_word(pdev, PCI_COMMAND, cmd);

		if ((pdev->class >> 8) == PCI_CLASS_BRIDGE_PCI)
			pci_write_config_byte(pdev,
					      PCI_CACHE_LINE_SIZE,
					      (64 / sizeof(u32)));
	}
}

void __init pci_assign_unassigned(struct pci_pbm_info *pbm,
				  struct pci_bus *pbus)
{
	struct list_head *walk = &pbus->devices;

	for (walk = walk->next; walk != &pbus->devices; walk = walk->next)
		pdev_assign_unassigned(pbm, pci_dev_b(walk));

	walk = &pbus->children;
	for (walk = walk->next; walk != &pbus->children; walk = walk->next)
		pci_assign_unassigned(pbm, pci_bus_b(walk));
}

static int __init pci_intmap_match(struct pci_dev *pdev, unsigned int *interrupt)
{
	struct pcidev_cookie *dev_pcp = pdev->sysdata;
	struct pci_pbm_info *pbm = dev_pcp->pbm;
	struct linux_prom_pci_registers *pregs = dev_pcp->prom_regs;
	unsigned int hi, mid, lo, irq;
	int i;

	if (pbm->num_pbm_intmap == 0)
		return 0;

	/* If we are underneath a PCI bridge, use PROM register
	 * property of the parent bridge which is closest to
	 * the PBM.
	 */
	if (pdev->bus->number != pbm->pci_first_busno) {
		struct pcidev_cookie *bus_pcp;
		struct pci_dev *pwalk;
		int offset;

		pwalk = pdev->bus->self;
		while (pwalk->bus &&
		       pwalk->bus->number != pbm->pci_first_busno)
			pwalk = pwalk->bus->self;

		bus_pcp = pwalk->sysdata;
		pregs = bus_pcp->prom_regs;

		offset = prom_getint(dev_pcp->prom_node,
				     "fcode-rom-offset");

		/* Did PROM know better and assign an interrupt other
		 * than #INTA to the device? - We test here for presence of
		 * FCODE on the card, in this case we assume PROM has set
		 * correct 'interrupts' property, unless it is quadhme.
		 */
		if (offset == -1 ||
		    !strcmp(dev_pcp->prom_name, "SUNW,qfe") ||
		    !strcmp(dev_pcp->prom_name, "qfe")) {
			/*
			 * No, use low slot number bits of child as IRQ line.
			 */
			*interrupt = ((*interrupt - 1 + PCI_SLOT(pdev->devfn)) & 3) + 1;
		}
	}

	hi   = pregs->phys_hi & pbm->pbm_intmask.phys_hi;
	mid  = pregs->phys_mid & pbm->pbm_intmask.phys_mid;
	lo   = pregs->phys_lo & pbm->pbm_intmask.phys_lo;
	irq  = *interrupt & pbm->pbm_intmask.interrupt;

	for (i = 0; i < pbm->num_pbm_intmap; i++) {
		if (pbm->pbm_intmap[i].phys_hi  == hi	&&
		    pbm->pbm_intmap[i].phys_mid == mid	&&
		    pbm->pbm_intmap[i].phys_lo  == lo	&&
		    pbm->pbm_intmap[i].interrupt == irq) {
			*interrupt = pbm->pbm_intmap[i].cinterrupt;
			return 1;
		}
	}

	prom_printf("pbm_intmap_match: bus %02x, devfn %02x: ",
		    pdev->bus->number, pdev->devfn);
	prom_printf("IRQ [%08x.%08x.%08x.%08x] not found in interrupt-map\n",
		    pregs->phys_hi, pregs->phys_mid, pregs->phys_lo, *interrupt);
	prom_printf("Please email this information to davem@redhat.com\n");
	prom_halt();
}

static void __init pdev_fixup_irq(struct pci_dev *pdev)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_pbm_info *pbm = pcp->pbm;
	struct pci_controller_info *p = pbm->parent;
	unsigned int portid = p->portid;
	unsigned int prom_irq;
	int prom_node = pcp->prom_node;
	int err;

	err = prom_getproperty(prom_node, "interrupts",
			       (char *)&prom_irq, sizeof(prom_irq));
	if (err == 0 || err == -1) {
		pdev->irq = 0;
		return;
	}

	/* Fully specified already? */
	if (((prom_irq & PCI_IRQ_IGN) >> 6) == portid) {
		pdev->irq = p->irq_build(p, pdev, prom_irq);
		goto have_irq;
	}

	/* An onboard device? (bit 5 set) */
	if ((prom_irq & PCI_IRQ_INO) & 0x20) {
		pdev->irq = p->irq_build(p, pdev, (portid << 6 | prom_irq));
		goto have_irq;
	}

	/* Can we find a matching entry in the interrupt-map? */
	if (pci_intmap_match(pdev, &prom_irq)) {
		pdev->irq = p->irq_build(p, pdev, (portid << 6) | prom_irq);
		goto have_irq;
	}

	/* Ok, we have to do it the hard way. */
	{
		unsigned int bus, slot, line;

		bus = (pbm == &pbm->parent->pbm_B) ? (1 << 4) : 0;

		/* If we have a legal interrupt property, use it as
		 * the IRQ line.
		 */
		if (prom_irq > 0 && prom_irq < 5) {
			line = ((prom_irq - 1) & 3);
		} else {
			u8 pci_irq_line;

			/* Else just directly consult PCI config space. */
			pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &pci_irq_line);
			line = ((pci_irq_line - 1) & 3);
		}

		/* Now figure out the slot. */
		if (pdev->bus->number == pbm->pci_first_busno) {
			if (pbm == &pbm->parent->pbm_A)
				slot = (pdev->devfn >> 3) - 1;
			else
				slot = (pdev->devfn >> 3) - 2;
		} else {
			if (pbm == &pbm->parent->pbm_A)
				slot = (pdev->bus->self->devfn >> 3) - 1;
			else
				slot = (pdev->bus->self->devfn >> 3) - 2;
		}
		slot = slot << 2;

		pdev->irq = p->irq_build(p, pdev,
					 ((portid << 6) & PCI_IRQ_IGN) |
					 (bus | slot | line));
	}

have_irq:
	pci_write_config_byte(pdev, PCI_INTERRUPT_LINE,
			      pdev->irq & PCI_IRQ_INO);
}

void __init pci_fixup_irq(struct pci_pbm_info *pbm,
			  struct pci_bus *pbus)
{
	struct list_head *walk = &pbus->devices;

	for (walk = walk->next; walk != &pbus->devices; walk = walk->next)
		pdev_fixup_irq(pci_dev_b(walk));

	walk = &pbus->children;
	for (walk = walk->next; walk != &pbus->children; walk = walk->next)
		pci_fixup_irq(pbm, pci_bus_b(walk));
}

#undef DEBUG_BUSMASTERING

static void pdev_setup_busmastering(struct pci_dev *pdev, int is_66mhz)
{
	u16 cmd;
	u8 hdr_type, min_gnt, ltimer;

#ifdef DEBUG_BUSMASTERING
	printk("PCI: Checking DEV(%s), ", pdev->name);
#endif

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_MASTER;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);

	/* Read it back, if the mastering bit did not
	 * get set, the device does not support bus
	 * mastering so we have nothing to do here.
	 */
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if ((cmd & PCI_COMMAND_MASTER) == 0) {
#ifdef DEBUG_BUSMASTERING
		printk("no bus mastering...\n");
#endif
		return;
	}

	/* Set correct cache line size, 64-byte on all
	 * Sparc64 PCI systems.  Note that the value is
	 * measured in 32-bit words.
	 */
#ifdef DEBUG_BUSMASTERING
	printk("set cachelinesize, ");
#endif
	pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE,
			      64 / sizeof(u32));

	pci_read_config_byte(pdev, PCI_HEADER_TYPE, &hdr_type);
	hdr_type &= ~0x80;
	if (hdr_type != PCI_HEADER_TYPE_NORMAL) {
#ifdef DEBUG_BUSMASTERING
		printk("hdr_type=%x, exit\n", hdr_type);
#endif
		return;
	}

	/* If the latency timer is already programmed with a non-zero
	 * value, assume whoever set it (OBP or whoever) knows what
	 * they are doing.
	 */
	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &ltimer);
	if (ltimer != 0) {
#ifdef DEBUG_BUSMASTERING
		printk("ltimer was %x, exit\n", ltimer);
#endif
		return;
	}

	/* XXX Since I'm tipping off the min grant value to
	 * XXX choose a suitable latency timer value, I also
	 * XXX considered making use of the max latency value
	 * XXX as well.  Unfortunately I've seen too many bogusly
	 * XXX low settings for it to the point where it lacks
	 * XXX any usefulness.  In one case, an ethernet card
	 * XXX claimed a min grant of 10 and a max latency of 5.
	 * XXX Now, if I had two such cards on the same bus I
	 * XXX could not set the desired burst period (calculated
	 * XXX from min grant) without violating the max latency
	 * XXX bound.  Duh...
	 * XXX
	 * XXX I blame dumb PC bios implementors for stuff like
	 * XXX this, most of them don't even try to do something
	 * XXX sensible with latency timer values and just set some
	 * XXX default value (usually 32) into every device.
	 */

	pci_read_config_byte(pdev, PCI_MIN_GNT, &min_gnt);

	if (min_gnt == 0) {
		/* If no min_gnt setting then use a default
		 * value.
		 */
		if (is_66mhz)
			ltimer = 16;
		else
			ltimer = 32;
	} else {
		int shift_factor;

		if (is_66mhz)
			shift_factor = 2;
		else
			shift_factor = 3;

		/* Use a default value when the min_gnt value
		 * is erroneously high.
		 */
		if (((unsigned int) min_gnt << shift_factor) > 512 ||
		    ((min_gnt << shift_factor) & 0xff) == 0) {
			ltimer = 8 << shift_factor;
		} else {
			ltimer = min_gnt << shift_factor;
		}
	}

	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, ltimer);
#ifdef DEBUG_BUSMASTERING
	printk("set ltimer to %x\n", ltimer);
#endif
}

void pci_determine_66mhz_disposition(struct pci_pbm_info *pbm,
				     struct pci_bus *pbus)
{
	struct list_head *walk;
	int all_are_66mhz;
	u16 status;

	if (pbm->is_66mhz_capable == 0) {
		all_are_66mhz = 0;
		goto out;
	}

	walk = &pbus->devices;
	all_are_66mhz = 1;
	for (walk = walk->next; walk != &pbus->devices; walk = walk->next) {
		struct pci_dev *pdev = pci_dev_b(walk);

		pci_read_config_word(pdev, PCI_STATUS, &status);
		if (!(status & PCI_STATUS_66MHZ)) {
			all_are_66mhz = 0;
			break;
		}
	}
out:
	pbm->all_devs_66mhz = all_are_66mhz;

	printk("PCI%d(PBM%c): Bus running at %dMHz\n",
	       pbm->parent->index,
	       (pbm == &pbm->parent->pbm_A) ? 'A' : 'B',
	       (all_are_66mhz ? 66 : 33));
}

void pci_setup_busmastering(struct pci_pbm_info *pbm,
			    struct pci_bus *pbus)
{
	struct list_head *walk = &pbus->devices;
	int is_66mhz;

	is_66mhz = pbm->is_66mhz_capable && pbm->all_devs_66mhz;

	for (walk = walk->next; walk != &pbus->devices; walk = walk->next)
		pdev_setup_busmastering(pci_dev_b(walk), is_66mhz);

	walk = &pbus->children;
	for (walk = walk->next; walk != &pbus->children; walk = walk->next)
		pci_setup_busmastering(pbm, pci_bus_b(walk));
}

/* Generic helper routines for PCI error reporting. */
void pci_scan_for_target_abort(struct pci_controller_info *p,
			       struct pci_pbm_info *pbm,
			       struct pci_bus *pbus)
{
	struct list_head *walk = &pbus->devices;

	for (walk = walk->next; walk != &pbus->devices; walk = walk->next) {
		struct pci_dev *pdev = pci_dev_b(walk);
		u16 status, error_bits;

		pci_read_config_word(pdev, PCI_STATUS, &status);
		error_bits =
			(status & (PCI_STATUS_SIG_TARGET_ABORT |
				   PCI_STATUS_REC_TARGET_ABORT));
		if (error_bits) {
			pci_write_config_word(pdev, PCI_STATUS, error_bits);
			printk("PCI%d(PBM%c): Device [%s] saw Target Abort [%016x]\n",
			       p->index, ((pbm == &p->pbm_A) ? 'A' : 'B'),
			       pdev->name, status);
		}
	}

	walk = &pbus->children;
	for (walk = walk->next; walk != &pbus->children; walk = walk->next)
		pci_scan_for_target_abort(p, pbm, pci_bus_b(walk));
}

void pci_scan_for_master_abort(struct pci_controller_info *p,
			       struct pci_pbm_info *pbm,
			       struct pci_bus *pbus)
{
	struct list_head *walk = &pbus->devices;

	for (walk = walk->next; walk != &pbus->devices; walk = walk->next) {
		struct pci_dev *pdev = pci_dev_b(walk);
		u16 status, error_bits;

		pci_read_config_word(pdev, PCI_STATUS, &status);
		error_bits =
			(status & (PCI_STATUS_REC_MASTER_ABORT));
		if (error_bits) {
			pci_write_config_word(pdev, PCI_STATUS, error_bits);
			printk("PCI%d(PBM%c): Device [%s] received Master Abort [%016x]\n",
			       p->index, ((pbm == &p->pbm_A) ? 'A' : 'B'),
			       pdev->name, status);
		}
	}

	walk = &pbus->children;
	for (walk = walk->next; walk != &pbus->children; walk = walk->next)
		pci_scan_for_master_abort(p, pbm, pci_bus_b(walk));
}

void pci_scan_for_parity_error(struct pci_controller_info *p,
			       struct pci_pbm_info *pbm,
			       struct pci_bus *pbus)
{
	struct list_head *walk = &pbus->devices;

	for (walk = walk->next; walk != &pbus->devices; walk = walk->next) {
		struct pci_dev *pdev = pci_dev_b(walk);
		u16 status, error_bits;

		pci_read_config_word(pdev, PCI_STATUS, &status);
		error_bits =
			(status & (PCI_STATUS_PARITY |
				   PCI_STATUS_DETECTED_PARITY));
		if (error_bits) {
			pci_write_config_word(pdev, PCI_STATUS, error_bits);
			printk("PCI%d(PBM%c): Device [%s] saw Parity Error [%016x]\n",
			       p->index, ((pbm == &p->pbm_A) ? 'A' : 'B'),
			       pdev->name, status);
		}
	}

	walk = &pbus->children;
	for (walk = walk->next; walk != &pbus->children; walk = walk->next)
		pci_scan_for_parity_error(p, pbm, pci_bus_b(walk));
}
