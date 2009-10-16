/*
 * PCI Frontend Xenbus Setup - handles setup with backend (imports page/evtchn)
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <xen/xenbus.h>
#include <xen/evtchn.h>
#include <xen/gnttab.h>
#include "pcifront.h"

#define INVALID_GRANT_REF (0)
#define INVALID_EVTCHN    (-1)

static struct pcifront_device *alloc_pdev(struct xenbus_device *xdev)
{
	struct pcifront_device *pdev;

	pdev = kzalloc(sizeof(struct pcifront_device), GFP_KERNEL);
	if (pdev == NULL)
		goto out;

	pdev->sh_info =
	    (struct xen_pci_sharedinfo *)__get_free_page(GFP_KERNEL);
	if (pdev->sh_info == NULL) {
		kfree(pdev);
		pdev = NULL;
		goto out;
	}
	pdev->sh_info->flags = 0;

	/*Flag for registering PV AER handler*/
	set_bit(_XEN_PCIB_AERHANDLER, (void*)&pdev->sh_info->flags);

	dev_set_drvdata(&xdev->dev, pdev);
	pdev->xdev = xdev;

	INIT_LIST_HEAD(&pdev->root_buses);

	spin_lock_init(&pdev->dev_lock);
	spin_lock_init(&pdev->sh_info_lock);

	pdev->evtchn = INVALID_EVTCHN;
	pdev->gnt_ref = INVALID_GRANT_REF;

	INIT_WORK(&pdev->op_work, pcifront_do_aer);

	dev_dbg(&xdev->dev, "Allocated pdev @ 0x%p pdev->sh_info @ 0x%p\n",
		pdev, pdev->sh_info);
      out:
	return pdev;
}

static void free_pdev(struct pcifront_device *pdev)
{
	dev_dbg(&pdev->xdev->dev, "freeing pdev @ 0x%p\n", pdev);

	pcifront_free_roots(pdev);

	/*For PCIE_AER error handling job*/
	flush_scheduled_work();
	unbind_from_irqhandler(pdev->evtchn, pdev);

	if (pdev->evtchn != INVALID_EVTCHN)
		xenbus_free_evtchn(pdev->xdev, pdev->evtchn);

	if (pdev->gnt_ref != INVALID_GRANT_REF)
		gnttab_end_foreign_access(pdev->gnt_ref,
					  (unsigned long)pdev->sh_info);

	dev_set_drvdata(&pdev->xdev->dev, NULL);

	kfree(pdev);
}

static int pcifront_publish_info(struct pcifront_device *pdev)
{
	int err = 0;
	struct xenbus_transaction trans;

	err = xenbus_grant_ring(pdev->xdev, virt_to_mfn(pdev->sh_info));
	if (err < 0)
		goto out;

	pdev->gnt_ref = err;

	err = xenbus_alloc_evtchn(pdev->xdev, &pdev->evtchn);
	if (err)
		goto out;

	bind_caller_port_to_irqhandler(pdev->evtchn, pcifront_handler_aer, 
		IRQF_SAMPLE_RANDOM, "pcifront", pdev);

      do_publish:
	err = xenbus_transaction_start(&trans);
	if (err) {
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error writing configuration for backend "
				 "(start transaction)");
		goto out;
	}

	err = xenbus_printf(trans, pdev->xdev->nodename,
			    "pci-op-ref", "%u", pdev->gnt_ref);
	if (!err)
		err = xenbus_printf(trans, pdev->xdev->nodename,
				    "event-channel", "%u", pdev->evtchn);
	if (!err)
		err = xenbus_printf(trans, pdev->xdev->nodename,
				    "magic", XEN_PCI_MAGIC);

	if (err) {
		xenbus_transaction_end(trans, 1);
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error writing configuration for backend");
		goto out;
	} else {
		err = xenbus_transaction_end(trans, 0);
		if (err == -EAGAIN)
			goto do_publish;
		else if (err) {
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error completing transaction "
					 "for backend");
			goto out;
		}
	}

	xenbus_switch_state(pdev->xdev, XenbusStateInitialised);

	dev_dbg(&pdev->xdev->dev, "publishing successful!\n");

      out:
	return err;
}

static int __devinit pcifront_try_connect(struct pcifront_device *pdev)
{
	int err = -EFAULT;
	int i, num_roots, len;
	char str[64];
	unsigned int domain, bus;

	spin_lock(&pdev->dev_lock);

	/* Only connect once */
	if (xenbus_read_driver_state(pdev->xdev->nodename) !=
	    XenbusStateInitialised)
		goto out;

	err = pcifront_connect(pdev);
	if (err) {
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error connecting PCI Frontend");
		goto out;
	}

	err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend,
			   "root_num", "%d", &num_roots);
	if (err == -ENOENT) {
		xenbus_dev_error(pdev->xdev, err,
				 "No PCI Roots found, trying 0000:00");
		err = pcifront_scan_root(pdev, 0, 0);
		num_roots = 0;
	} else if (err != 1) {
		if (err == 0)
			err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error reading number of PCI roots");
		goto out;
	}

	for (i = 0; i < num_roots; i++) {
		len = snprintf(str, sizeof(str), "root-%d", i);
		if (unlikely(len >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}

		err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, str,
				   "%x:%x", &domain, &bus);
		if (err != 2) {
			if (err >= 0)
				err = -EINVAL;
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error reading PCI root %d", i);
			goto out;
		}

		err = pcifront_scan_root(pdev, domain, bus);
		if (err) {
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error scanning PCI root %04x:%02x",
					 domain, bus);
			goto out;
		}
	}

	err = xenbus_switch_state(pdev->xdev, XenbusStateConnected);
	if (err)
		goto out;

      out:
	spin_unlock(&pdev->dev_lock);
	return err;
}

static int pcifront_try_disconnect(struct pcifront_device *pdev)
{
	int err = 0;
	enum xenbus_state prev_state;

	spin_lock(&pdev->dev_lock);

	prev_state = xenbus_read_driver_state(pdev->xdev->nodename);

	if (prev_state >= XenbusStateClosing)
		goto out;

	if(prev_state == XenbusStateConnected) {
		pcifront_free_roots(pdev);
		pcifront_disconnect(pdev);
	}

	err = xenbus_switch_state(pdev->xdev, XenbusStateClosed);

      out:
	spin_unlock(&pdev->dev_lock);

	return err;
}

static int __devinit pcifront_attach_devices(struct pcifront_device *pdev)
{
	int err = -EFAULT;
	int i, num_roots, len;
	unsigned int domain, bus;
	char str[64];

	spin_lock(&pdev->dev_lock);

	if (xenbus_read_driver_state(pdev->xdev->nodename) !=
	    XenbusStateReconfiguring)
		goto out;

	err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend,
			   "root_num", "%d", &num_roots);
	if (err == -ENOENT) {
		xenbus_dev_error(pdev->xdev, err,
				 "No PCI Roots found, trying 0000:00");
		err = pcifront_rescan_root(pdev, 0, 0);
		num_roots = 0;
	} else if (err != 1) {
		if (err == 0)
			err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error reading number of PCI roots");
		goto out;
	}

	for (i = 0; i < num_roots; i++) {
		len = snprintf(str, sizeof(str), "root-%d", i);
		if (unlikely(len >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}

		err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, str,
				   "%x:%x", &domain, &bus);
		if (err != 2) {
			if (err >= 0)
				err = -EINVAL;
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error reading PCI root %d", i);
			goto out;
		}

		err = pcifront_rescan_root(pdev, domain, bus);
		if (err) {
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error scanning PCI root %04x:%02x",
					 domain, bus);
			goto out;
		}
	}

	xenbus_switch_state(pdev->xdev, XenbusStateConnected);

      out:
	spin_unlock(&pdev->dev_lock);
	return err;
}

static int pcifront_detach_devices(struct pcifront_device *pdev)
{
	int err = 0;
	int i, num_devs;
	unsigned int domain, bus, slot, func;
	struct pci_bus *pci_bus;
	struct pci_dev *pci_dev;
	char str[64];

	spin_lock(&pdev->dev_lock);

	if (xenbus_read_driver_state(pdev->xdev->nodename) !=
	    XenbusStateConnected)
		goto out;

	err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, "num_devs", "%d",
			   &num_devs);
	if (err != 1) {
		if (err >= 0)
			err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error reading number of PCI devices");
		goto out;
	}

	/* Find devices being detached and remove them. */
	for (i = 0; i < num_devs; i++) {
		int l, state;
		l = snprintf(str, sizeof(str), "state-%d", i);
		if (unlikely(l >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}
		err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, str, "%d",
				   &state);
		if (err != 1)
			state = XenbusStateUnknown;

		if (state != XenbusStateClosing)
			continue;

		/* Remove device. */
		l = snprintf(str, sizeof(str), "vdev-%d", i);
		if (unlikely(l >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}
		err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, str,
			   	   "%x:%x:%x.%x", &domain, &bus, &slot, &func);
		if (err != 4) {
			if (err >= 0)
				err = -EINVAL;
			xenbus_dev_fatal(pdev->xdev, err,
				 	 "Error reading PCI device %d", i);
			goto out;
		}

		pci_bus = pci_find_bus(domain, bus);
		if(!pci_bus) {
			dev_dbg(&pdev->xdev->dev, "Cannot get bus %04x:%02x\n",
				domain, bus);
			continue;
		}
		pci_dev = pci_get_slot(pci_bus, PCI_DEVFN(slot, func));
		if(!pci_dev) {
			dev_dbg(&pdev->xdev->dev,
				"Cannot get PCI device %04x:%02x:%02x.%02x\n",
				domain, bus, slot, func);
			continue;
		}
		pci_remove_bus_device(pci_dev);
		pci_dev_put(pci_dev);

		dev_dbg(&pdev->xdev->dev,
			"PCI device %04x:%02x:%02x.%02x removed.\n",
			domain, bus, slot, func);
	}

	err = xenbus_switch_state(pdev->xdev, XenbusStateReconfiguring);

      out:
	spin_unlock(&pdev->dev_lock);
	return err;
}

static void __init_refok pcifront_backend_changed(struct xenbus_device *xdev,
						  enum xenbus_state be_state)
{
	struct pcifront_device *pdev = dev_get_drvdata(&xdev->dev);

	switch (be_state) {
	case XenbusStateUnknown:
	case XenbusStateInitialising:
	case XenbusStateInitWait:
	case XenbusStateInitialised:
	case XenbusStateClosed:
		break;

	case XenbusStateConnected:
		pcifront_try_connect(pdev);
		break;

	case XenbusStateClosing:
		dev_warn(&xdev->dev, "backend going away!\n");
		pcifront_try_disconnect(pdev);
		break;

	case XenbusStateReconfiguring:
		pcifront_detach_devices(pdev);
		break;

	case XenbusStateReconfigured:
		pcifront_attach_devices(pdev);
		break;
	}
}

static int pcifront_xenbus_probe(struct xenbus_device *xdev,
				 const struct xenbus_device_id *id)
{
	int err = 0;
	struct pcifront_device *pdev = alloc_pdev(xdev);

	if (pdev == NULL) {
		err = -ENOMEM;
		xenbus_dev_fatal(xdev, err,
				 "Error allocating pcifront_device struct");
		goto out;
	}

	err = pcifront_publish_info(pdev);

      out:
	return err;
}

static int pcifront_xenbus_remove(struct xenbus_device *xdev)
{
	if (dev_get_drvdata(&xdev->dev))
		free_pdev(dev_get_drvdata(&xdev->dev));

	return 0;
}

static const struct xenbus_device_id xenpci_ids[] = {
	{"pci"},
	{{0}},
};
MODULE_ALIAS("xen:pci");

static struct xenbus_driver xenbus_pcifront_driver = {
	.name 			= "pcifront",
	.ids 			= xenpci_ids,
	.probe 			= pcifront_xenbus_probe,
	.remove 		= pcifront_xenbus_remove,
	.otherend_changed 	= pcifront_backend_changed,
};

static int __init pcifront_init(void)
{
	if (!is_running_on_xen())
		return -ENODEV;

	return xenbus_register_frontend(&xenbus_pcifront_driver);
}

/* Initialize after the Xen PCI Frontend Stub is initialized */
subsys_initcall(pcifront_init);
