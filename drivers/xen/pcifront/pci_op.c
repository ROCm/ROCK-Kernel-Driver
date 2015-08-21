/*
 * PCI Frontend Operations - Communicates with frontend
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <asm/bitops.h>
#include <xen/evtchn.h>
#include "pcifront.h"

static bool verbose_request;
module_param(verbose_request, bool, 0644);

static void pcifront_init_sd(struct pcifront_sd *sd,
			     unsigned int domain, unsigned int bus,
			     struct pcifront_device *pdev)
{
#ifdef __ia64__
	int err, i, j, k, len, root_num, res_count;
	struct acpi_resource res;
	unsigned int d, b, byte;
	unsigned long magic;
	char str[64], tmp[3];
	unsigned char *buf, *bufp;
	u8 *ptr;

	memset(sd, 0, sizeof(*sd));

	sd->segment = domain;
	sd->node = -1;	/* Revisit for NUMA */
	sd->platform_data = pdev;

	/* Look for resources for this controller in xenbus. */
	err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, "root_num",
			   "%d", &root_num);
	if (err != 1)
		return;

	for (i = 0; i < root_num; i++) {
		len = snprintf(str, sizeof(str), "root-%d", i);
		if (unlikely(len >= (sizeof(str) - 1)))
			return;

		err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend,
				   str, "%x:%x", &d, &b);
		if (err != 2)
			return;

		if (d == domain && b == bus)
			break;
	}

	if (i == root_num)
		return;

	len = snprintf(str, sizeof(str), "root-resource-magic");

	err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend,
			   str, "%lx", &magic);

	if (err != 1)
		return; /* No resources, nothing to do */

	if (magic != (sizeof(res) * 2) + 1) {
		dev_warn(&pdev->xdev->dev,
			 "pcifront: resource magic mismatch\n");
		return;
	}

	len = snprintf(str, sizeof(str), "root-%d-resources", i);
	if (unlikely(len >= (sizeof(str) - 1)))
		return;

	err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend,
			   str, "%d", &res_count);

	if (err != 1)
		return; /* No resources, nothing to do */

	sd->window = kzalloc(sizeof(*sd->window) * res_count, GFP_KERNEL);
	if (!sd->window)
		return;

	/* magic is also the size of the byte stream in xenbus */
	buf = kmalloc(magic, GFP_KERNEL);
	if (!buf) {
		kfree(sd->window);
		sd->window = NULL;
		return;
	}

	/* Read the resources out of xenbus */
	for (j = 0; j < res_count; j++) {
		memset(&res, 0, sizeof(res));
		memset(buf, 0, magic);

		len = snprintf(str, sizeof(str), "root-%d-resource-%d", i, j);
		if (unlikely(len >= (sizeof(str) - 1)))
			return;

		err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, str,
				   "%s", buf);
		if (err != 1) {
			dev_warn(&pdev->xdev->dev,
				 "pcifront: error reading resource %d on bus %04x:%02x\n",
				 j, domain, bus);
			continue;
		}

		bufp = buf;
		ptr = (u8 *)&res;
		memset(tmp, 0, sizeof(tmp));

		/* Copy ASCII byte stream into structure */
		for (k = 0; k < magic - 1; k += 2) {
			memcpy(tmp, bufp, 2);
			bufp += 2;

			sscanf(tmp, "%02x", &byte);
			*ptr = byte;
			ptr++;
		}

		xen_add_resource(sd, domain, bus, &res);
		sd->windows++;
	}
	kfree(buf);
#else
	sd->domain = domain;
	sd->pdev = pdev;
#endif
}

static int errno_to_pcibios_err(int errno)
{
	switch (errno) {
	case XEN_PCI_ERR_success:
		return PCIBIOS_SUCCESSFUL;

	case XEN_PCI_ERR_dev_not_found:
		return PCIBIOS_DEVICE_NOT_FOUND;

	case XEN_PCI_ERR_invalid_offset:
	case XEN_PCI_ERR_op_failed:
		return PCIBIOS_BAD_REGISTER_NUMBER;

	case XEN_PCI_ERR_not_implemented:
		return PCIBIOS_FUNC_NOT_SUPPORTED;

	case XEN_PCI_ERR_access_denied:
		return PCIBIOS_SET_FAILED;
	}
	return errno;
}

static inline void schedule_pcifront_aer_op(struct pcifront_device *pdev)
{
	if (test_bit(_XEN_PCIB_active, (unsigned long *)&pdev->sh_info->flags)
		&& !test_and_set_bit(_PDEVB_op_active, &pdev->flags)) {
		dev_dbg(&pdev->xdev->dev, "schedule aer frontend job\n");
		schedule_work(&pdev->op_work);
	}
}

static int do_pci_op(struct pcifront_device *pdev, struct xen_pci_op *op)
{
	int err = 0;
	struct xen_pci_op *active_op = &pdev->sh_info->op;
	unsigned long irq_flags;
	evtchn_port_t port = pdev->evtchn;
	s64 ns, ns_timeout;

	spin_lock_irqsave(&pdev->sh_info_lock, irq_flags);

	memcpy(active_op, op, sizeof(struct xen_pci_op));

	/* Go */
	wmb();
	set_bit(_XEN_PCIF_active, (unsigned long *)&pdev->sh_info->flags);
	notify_remote_via_evtchn(port);

	/*
	 * We set a poll timeout of 3 seconds but give up on return after
	 * 2 seconds. It is better to time out too late rather than too early
	 * (in the latter case we end up continually re-executing poll() with a
	 * timeout in the past). 1s difference gives plenty of slack for error.
	 */
	ns_timeout = ktime_get_ns() + 2 * (s64)NSEC_PER_SEC;

	clear_evtchn(port);

	while (test_bit(_XEN_PCIF_active,
			(unsigned long *)&pdev->sh_info->flags)) {
		if (HYPERVISOR_poll(&port, 1, jiffies + 3*HZ))
			BUG();
		clear_evtchn(port);
		ns = ktime_get_ns();
		if (ns > ns_timeout) {
			dev_err(&pdev->xdev->dev,
				"pciback not responding!!!\n");
			clear_bit(_XEN_PCIF_active,
				  (unsigned long *)&pdev->sh_info->flags);
			err = XEN_PCI_ERR_dev_not_found;
			goto out;
		}
	}

	/*
	* We might lose backend service request since we 
	* reuse same evtchn with pci_conf backend response. So re-schedule
	* aer pcifront service.
	*/
	if (test_bit(_XEN_PCIB_active, 
			(unsigned long*)&pdev->sh_info->flags)) {
		dev_info(&pdev->xdev->dev, "schedule aer pcifront service\n");
		schedule_pcifront_aer_op(pdev);
	}

	memcpy(op, active_op, sizeof(struct xen_pci_op));

	err = op->err;
      out:
	spin_unlock_irqrestore(&pdev->sh_info_lock, irq_flags);
	return err;
}

/* Access to this function is spinlocked in drivers/pci/access.c */
static int pcifront_bus_read(struct pci_bus *bus, unsigned int devfn,
			     int where, int size, u32 *val)
{
	int err;
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_conf_read,
		.domain = pci_domain_nr(bus),
		.bus    = bus->number,
		.devfn  = devfn,
		.offset = where,
		.size   = size,
	};
	struct pcifront_sd *sd = bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	if (verbose_request)
		dev_info(&pdev->xdev->dev, "read %02x.%u offset %x size %d\n",
			 PCI_SLOT(devfn), PCI_FUNC(devfn), where, size);

	err = do_pci_op(pdev, &op);

	if (likely(!err)) {
		if (verbose_request)
			dev_info(&pdev->xdev->dev, "read %02x.%u = %x\n",
				 PCI_SLOT(devfn), PCI_FUNC(devfn), op.value);

		*val = op.value;
	} else if (err == -ENODEV) {
		/* No device here, pretend that it just returned 0 */
		err = 0;
		*val = 0;
	} else if (verbose_request)
		dev_info(&pdev->xdev->dev, "read %02x.%u -> %d\n",
			 PCI_SLOT(devfn), PCI_FUNC(devfn), err);

	return errno_to_pcibios_err(err);
}

/* Access to this function is spinlocked in drivers/pci/access.c */
static int pcifront_bus_write(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 val)
{
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_conf_write,
		.domain = pci_domain_nr(bus),
		.bus    = bus->number,
		.devfn  = devfn,
		.offset = where,
		.size   = size,
		.value  = val,
	};
	struct pcifront_sd *sd = bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	if (verbose_request)
		dev_info(&pdev->xdev->dev,
			 "write %02x.%u offset %x size %d val %x\n",
			 PCI_SLOT(devfn), PCI_FUNC(devfn), where, size, val);

	return errno_to_pcibios_err(do_pci_op(pdev, &op));
}

static struct pci_ops pcifront_bus_ops = {
	.read = pcifront_bus_read,
	.write = pcifront_bus_write,
};

#ifdef CONFIG_PCI_MSI
int pci_frontend_enable_msix(struct pci_dev *dev,
		struct msix_entry *entries,
		int nvec)
{
	int err;
	int i;
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_enable_msix,
		.domain = pci_domain_nr(dev->bus),
		.bus = dev->bus->number,
		.devfn = dev->devfn,
		.value = nvec,
	};
	struct pcifront_sd *sd = dev->bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	if (nvec < 0 || nvec > SH_INFO_MAX_VEC) {
		dev_err(&dev->dev, "too many (%d) vectors for pci frontend\n",
			nvec);
		return -EINVAL;
	}

	for (i = 0; i < nvec; i++) {
		op.msix_entries[i].entry = entries[i].entry;
		op.msix_entries[i].vector = entries[i].vector;
	}

	err = do_pci_op(pdev, &op);

	if (!err) {
		if (!op.value) {
			/* we get the result */
			for (i = 0; i < nvec; i++)
				entries[i].vector = op.msix_entries[i].vector;
		} else {
			dev_err(&dev->dev, "enable MSI-X => %#x\n", op.value);
			err = op.value;
		}
	} else {
		dev_err(&dev->dev, "enable MSI-X -> %d\n", err);
		err = -EINVAL;
	}
	return err;
}

void pci_frontend_disable_msix(struct pci_dev *dev)
{
	int err;
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_disable_msix,
		.domain = pci_domain_nr(dev->bus),
		.bus = dev->bus->number,
		.devfn = dev->devfn,
	};
	struct pcifront_sd *sd = dev->bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	err = do_pci_op(pdev, &op);

	/* What should do for error ? */
	if (err)
		dev_err(&dev->dev, "disable MSI-X -> %d\n", err);
}

int pci_frontend_enable_msi(struct pci_dev *dev, unsigned int nvec)
{
	int err;
	struct xen_pci_op op = {
		.cmd    = nvec > 1 ? XEN_PCI_OP_enable_multi_msi
				   : XEN_PCI_OP_enable_msi,
		.domain = pci_domain_nr(dev->bus),
		.bus = dev->bus->number,
		.devfn = dev->devfn,
		.info = nvec,
	};
	struct pcifront_sd *sd = dev->bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	err = do_pci_op(pdev, &op);
	if (likely(!err))
		dev->irq = op.value;
	else if (nvec > 1)
		err = op.info > 1 && op.info < nvec ? op.info : 1;
	else {
		dev_err(&dev->dev, "enable MSI -> %d\n", err);
		err = -EINVAL;
	}
	return err;
}

void pci_frontend_disable_msi(struct pci_dev *dev)
{
	int err;
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_disable_msi,
		.domain = pci_domain_nr(dev->bus),
		.bus = dev->bus->number,
		.devfn = dev->devfn,
	};
	struct pcifront_sd *sd = dev->bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	err = do_pci_op(pdev, &op);
	if (likely(!err))
		dev->irq = op.value;
	else
		dev_err(&dev->dev, "disable MSI -> %d\n", err);
}
#endif /* CONFIG_PCI_MSI */

/* Claim resources for the PCI frontend as-is, backend won't allow changes */
static int pcifront_claim_resource(struct pci_dev *dev, void *data)
{
	struct pcifront_device *pdev = data;
	int i;
	struct resource *r;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		r = &dev->resource[i];

		if (!r->parent && r->start && r->flags) {
			dev_dbg(&pdev->xdev->dev, "claiming resource %s/%d\n",
				pci_name(dev), i);
			pci_claim_resource(dev, i);
		}
	}

	return 0;
}

int pcifront_scan_root(struct pcifront_device *pdev,
		       unsigned int domain, unsigned int bus)
{
	struct pci_bus *b;
	LIST_HEAD(resources);
	struct pcifront_sd *sd;
	struct pci_bus_entry *bus_entry;
	int err = 0;
	static struct resource busn_res = {
		.start = 0,
		.end = 255,
		.flags = IORESOURCE_BUS,
	};

#ifndef CONFIG_PCI_DOMAINS
	if (domain != 0) {
		dev_err(&pdev->xdev->dev,
			"PCI root in non-zero domain %x!\n", domain);
		dev_err(&pdev->xdev->dev,
			"Please compile with CONFIG_PCI_DOMAINS\n");
		return -EINVAL;
	}
#endif

	dev_info(&pdev->xdev->dev, "Creating PCI Frontend Bus %04x:%02x\n",
		 domain, bus);

	bus_entry = kmalloc(sizeof(*bus_entry), GFP_KERNEL);
	sd = kmalloc(sizeof(*sd), GFP_KERNEL);
	if (!bus_entry || !sd) {
		err = -ENOMEM;
		goto err_out;
	}
	pci_add_resource(&resources, &ioport_resource);
	pci_add_resource(&resources, &iomem_resource);
	pci_add_resource(&resources, &busn_res);
	pcifront_init_sd(sd, domain, bus, pdev);

	pci_lock_rescan_remove();

	b = pci_scan_root_bus(&pdev->xdev->dev, bus,
			      &pcifront_bus_ops, sd, &resources);
	if (!b) {
		pci_unlock_rescan_remove();
		pci_free_resource_list(&resources);
		dev_err(&pdev->xdev->dev,
			"Error creating PCI Frontend Bus!\n");
		err = -ENOMEM;
		goto err_out;
	}

	pcifront_setup_root_resources(b, sd);
	bus_entry->bus = b;

	list_add(&bus_entry->list, &pdev->root_buses);

	/* Claim resources before going "live" with our devices */
	pci_walk_bus(b, pcifront_claim_resource, pdev);

	pci_bus_add_devices(b);
	pci_unlock_rescan_remove();

	return 0;

      err_out:
	kfree(bus_entry);
	kfree(sd);

	return err;
}

int pcifront_rescan_root(struct pcifront_device *pdev,
			 unsigned int domain, unsigned int bus)
{
	struct pci_bus *b;
	struct pci_dev *d;
	unsigned int devfn;

#ifndef CONFIG_PCI_DOMAINS
	if (domain != 0) {
		dev_err(&pdev->xdev->dev,
			"PCI root in non-zero domain %x\n", domain);
		dev_err(&pdev->xdev->dev,
			"Please compile with CONFIG_PCI_DOMAINS\n");
		return -EINVAL;
	}
#endif

	dev_info(&pdev->xdev->dev, "Rescanning PCI Frontend Bus %04x:%02x\n",
		 domain, bus);

	b = pci_find_bus(domain, bus);
	if(!b)
		/* If the bus is unknown, create it. */
		return pcifront_scan_root(pdev, domain, bus);

	/* Rescan the bus for newly attached functions and add.
	 * We omit handling of PCI bridge attachment because pciback prevents
	 * bridges from being exported.
	 */ 
	for (devfn = 0; devfn < 0x100; devfn++) {
		d = pci_get_slot(b, devfn);
		if(d) {
			/* Device is already known. */
			pci_dev_put(d);
			continue;
		}

		d = pci_scan_single_device(b, devfn);
		if (d)
			dev_info(&pdev->xdev->dev,
				 "New device on %04x:%02x:%02x.%u\n",
				 domain, bus,
				 PCI_SLOT(devfn), PCI_FUNC(devfn));
	}

	/* Claim resources before going "live" with our devices */
	pci_walk_bus(b, pcifront_claim_resource, pdev);

	/* Create SysFS and notify udev of the devices. Aka: "going live" */
	pci_bus_add_devices(b);

	return 0;
}

static void free_root_bus_devs(struct pci_bus *bus)
{
	struct pci_dev *dev;

	while (!list_empty(&bus->devices)) {
		dev = container_of(bus->devices.next, struct pci_dev,
				   bus_list);
		dev_dbg(&dev->dev, "removing device\n");
		pci_stop_and_remove_bus_device(dev);
	}
}

void pcifront_free_roots(struct pcifront_device *pdev)
{
	struct pci_bus_entry *bus_entry, *t;

	dev_dbg(&pdev->xdev->dev, "cleaning up root buses\n");

	pci_lock_rescan_remove();
	list_for_each_entry_safe(bus_entry, t, &pdev->root_buses, list) {
		list_del(&bus_entry->list);

		free_root_bus_devs(bus_entry->bus);

		kfree(bus_entry->bus->sysdata);

		device_unregister(bus_entry->bus->bridge);
		pci_remove_bus(bus_entry->bus);

		kfree(bus_entry);
	}
	pci_unlock_rescan_remove();
}

static pci_ers_result_t pcifront_common_process( int cmd, struct pcifront_device *pdev,
	pci_channel_state_t state)
{
	pci_ers_result_t result = PCI_ERS_RESULT_NONE;
	struct pci_driver *pdrv;
	int domain = pdev->sh_info->aer_op.domain;
	int bus = pdev->sh_info->aer_op.bus;
	int devfn = pdev->sh_info->aer_op.devfn;
	struct pci_dev *pcidev;

	dev_dbg(&pdev->xdev->dev, 
		"pcifront AER process: cmd %x (%04x:%02x:%02x.%u)",
		cmd, domain, bus, PCI_SLOT(devfn), PCI_FUNC(devfn));

	pcidev = pci_get_domain_bus_and_slot(domain, bus, devfn);
	if (!pcidev || !pcidev->driver) {
		pci_dev_put(pcidev);
		dev_err(&pdev->xdev->dev, "AER device or driver is NULL\n");
		return result;
	}
	pdrv = pcidev->driver;

	if (pdrv->err_handler) {
		dev_dbg(&pcidev->dev, "trying to call AER service\n");
		switch(cmd) {
		case XEN_PCI_OP_aer_detected:
			if (pdrv->err_handler->error_detected)
				result = pdrv->err_handler->error_detected(pcidev, state);
			break;
		case XEN_PCI_OP_aer_mmio:
			if (pdrv->err_handler->mmio_enabled)
				result = pdrv->err_handler->mmio_enabled(pcidev);
			break;
		case XEN_PCI_OP_aer_slotreset:
			if (pdrv->err_handler->slot_reset)
				result = pdrv->err_handler->slot_reset(pcidev);
			break;
		case XEN_PCI_OP_aer_resume:
			if (pdrv->err_handler->resume)
				pdrv->err_handler->resume(pcidev);
			break;
		default:
			dev_err(&pdev->xdev->dev,
				"bad request %x in aer recovery operation!\n",
				cmd);
			break;
		}
	}

	return result;
}


void pcifront_do_aer(struct work_struct *data)
{
	struct pcifront_device *pdev = container_of(data, struct pcifront_device, op_work);
	int cmd = pdev->sh_info->aer_op.cmd;
	pci_channel_state_t state = 
		(pci_channel_state_t)pdev->sh_info->aer_op.err;

	/*If a pci_conf op is in progress, 
		we have to wait until it is done before service aer op*/
	dev_dbg(&pdev->xdev->dev, 
		"pcifront service aer bus %x devfn %x\n", pdev->sh_info->aer_op.bus,
		pdev->sh_info->aer_op.devfn);

	pdev->sh_info->aer_op.err = pcifront_common_process(cmd, pdev, state);

	wmb();
	clear_bit(_XEN_PCIB_active, (unsigned long*)&pdev->sh_info->flags);
	notify_remote_via_evtchn(pdev->evtchn);

	/*in case of we lost an aer request in four lines time_window*/
	smp_mb__before_atomic();
	clear_bit( _PDEVB_op_active, &pdev->flags);
	smp_mb__after_atomic();

	schedule_pcifront_aer_op(pdev);

}

irqreturn_t pcifront_handler_aer(int irq, void *dev)
{
	struct pcifront_device *pdev = dev;
	schedule_pcifront_aer_op(pdev);
	return IRQ_HANDLED;
}
