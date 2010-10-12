/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright (c) 2009 Isaku Yamahata
 *                    VA Linux Systems Japan K.K.
 */

#include "iomulti.h"
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <xen/public/iomulti.h>

struct pci_iomul_data {
	struct mutex lock;

	struct pci_dev *pdev;
	struct pci_iomul_switch *sw;
	struct pci_iomul_slot *slot;	/* slot::kref */
	struct pci_iomul_func **func;	/* when dereferencing,
					   sw->lock is necessary */
};

static int pci_iomul_func_ioport(struct pci_iomul_func *func,
				 uint8_t bar, uint64_t offset, int *port)
{
	if (!(func->io_bar & (1 << bar)))
		return -EINVAL;

	*port = func->resource[bar].start + offset;
	if (*port < func->resource[bar].start ||
	    *port > func->resource[bar].end)
		return -EINVAL;

	return 0;
}

static inline int pci_iomul_valid(struct pci_iomul_data *iomul)
{
	BUG_ON(!mutex_is_locked(&iomul->lock));
	BUG_ON(!mutex_is_locked(&iomul->sw->lock));
	return pci_iomul_switch_io_allocated(iomul->sw) &&
		*iomul->func != NULL;
}

static void __pci_iomul_enable_io(struct pci_dev *pdev)
{
	uint16_t cmd;

	pci_dev_get(pdev);
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_IO;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);
}

static void __pci_iomul_disable_io(struct pci_iomul_data *iomul,
				   struct pci_dev *pdev)
{
	uint16_t cmd;

	if (!pci_iomul_valid(iomul))
		return;

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd &= ~PCI_COMMAND_IO;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);
	pci_dev_put(pdev);
}

static int pci_iomul_open(struct inode *inode, struct file *filp)
{
	struct pci_iomul_data *iomul;
	iomul = kmalloc(sizeof(*iomul), GFP_KERNEL);
	if (iomul == NULL)
		return -ENOMEM;

	mutex_init(&iomul->lock);
	iomul->pdev = NULL;
	iomul->sw = NULL;
	iomul->slot = NULL;
	iomul->func = NULL;
	filp->private_data = (void*)iomul;

	return nonseekable_open(inode, filp);
}

static int pci_iomul_release(struct inode *inode, struct file *filp)
{
	struct pci_iomul_data *iomul =
		(struct pci_iomul_data*)filp->private_data;
	struct pci_iomul_switch *sw;
	struct pci_iomul_slot *slot = NULL;

	mutex_lock(&iomul->lock);
	sw = iomul->sw;
	slot = iomul->slot;
	if (iomul->pdev != NULL) {
		if (sw != NULL) {
			mutex_lock(&sw->lock);
			if (sw->current_pdev == iomul->pdev) {
				__pci_iomul_disable_io(iomul,
						       sw->current_pdev);
				sw->current_pdev = NULL;
			}
			sw->count--;
			if (sw->count == 0) {
				release_region(sw->io_region->start, sw->io_region->end - sw->io_region->start + 1);
				sw->io_region = NULL;
			}
			mutex_unlock(&sw->lock);
		}
		pci_dev_put(iomul->pdev);
	}
	mutex_unlock(&iomul->lock);

	if (slot != NULL)
		pci_iomul_slot_put(slot);
	if (sw != NULL)
		pci_iomul_switch_put(sw);
	kfree(iomul);
	return 0;
}

static long pci_iomul_setup(struct pci_iomul_data *iomul,
			    struct pci_iomul_setup __user *arg)
{
	long error = 0;
	struct pci_iomul_setup setup;
	struct pci_iomul_switch *sw = NULL;
	struct pci_iomul_slot *slot;
	struct pci_bus *pbus;
	struct pci_dev *pdev;

	if (copy_from_user(&setup, arg, sizeof(setup)))
		return -EFAULT;

	pbus = pci_find_bus(setup.segment, setup.bus);
	if (pbus == NULL)
		return -ENODEV;
	pdev = pci_get_slot(pbus, setup.dev);
	if (pdev == NULL)
		return -ENODEV;

	mutex_lock(&iomul->lock);
	if (iomul->sw != NULL) {
		error = -EBUSY;
		goto out0;
	}

	pci_iomul_get_lock_switch(pdev, &sw, &slot);
	if (sw == NULL || slot == NULL) {
		error = -ENODEV;
		goto out0;
	}
	if (!pci_iomul_switch_io_allocated(sw)) {
		error = -ENODEV;
		goto out;
	}

	if (slot->func[setup.func] == NULL) {
		error = -ENODEV;
		goto out;
	}

	if (sw->count == 0) {
		BUG_ON(sw->io_region != NULL);
		sw->io_region =
			request_region(sw->io_base,
				       sw->io_limit - sw->io_base + 1,
				       "PCI IO Multiplexer driver");
		if (sw->io_region == NULL) {
			mutex_unlock(&sw->lock);
			error = -EBUSY;
			goto out;
		}
	}
	sw->count++;
	pci_iomul_slot_get(slot);

	iomul->pdev = pdev;
	iomul->sw = sw;
	iomul->slot = slot;
	iomul->func = &slot->func[setup.func];

out:
	mutex_unlock(&sw->lock);
out0:
	mutex_unlock(&iomul->lock);
	if (error != 0) {
		if (sw != NULL)
			pci_iomul_switch_put(sw);
		pci_dev_put(pdev);
	}
	return error;
}

static int pci_iomul_lock(struct pci_iomul_data *iomul,
			  struct pci_iomul_switch **sw,
			  struct pci_iomul_func **func)
{
	mutex_lock(&iomul->lock);
	*sw = iomul->sw;
	if (*sw == NULL) {
		mutex_unlock(&iomul->lock);
		return -ENODEV;
	}
	mutex_lock(&(*sw)->lock);
	if (!pci_iomul_valid(iomul)) {
		mutex_unlock(&(*sw)->lock);
		mutex_unlock(&iomul->lock);
		return -ENODEV;
	}
	*func = *iomul->func;

	return 0;
}

static long pci_iomul_disable_io(struct pci_iomul_data *iomul)
{
	long error = 0;
	struct pci_iomul_switch *sw;
	struct pci_iomul_func *dummy_func;
	struct pci_dev *pdev;

	if (pci_iomul_lock(iomul, &sw, &dummy_func) < 0)
		return -ENODEV;

	pdev = iomul->pdev;
	if (pdev == NULL)
		error = -ENODEV;

	if (pdev != NULL && sw->current_pdev == pdev) {
		__pci_iomul_disable_io(iomul, pdev);
		sw->current_pdev = NULL;
	}

	mutex_unlock(&sw->lock);
	mutex_unlock(&iomul->lock);
	return error;
}

static void pci_iomul_switch_to(
	struct pci_iomul_data *iomul, struct pci_iomul_switch *sw,
	struct pci_dev *next_pdev)
{
	if (sw->current_pdev == next_pdev)
		/* nothing to do */
		return;

	if (sw->current_pdev != NULL)
		__pci_iomul_disable_io(iomul, sw->current_pdev);

	__pci_iomul_enable_io(next_pdev);
	sw->current_pdev = next_pdev;
}

static long pci_iomul_in(struct pci_iomul_data *iomul,
			 struct pci_iomul_in __user *arg)
{
	struct pci_iomul_in in;
	struct pci_iomul_switch *sw;
	struct pci_iomul_func *func;

	long error = 0;
	int port;
	uint32_t value = 0;

	if (copy_from_user(&in, arg, sizeof(in)))
		return -EFAULT;

	if (pci_iomul_lock(iomul, &sw, &func) < 0)
		return -ENODEV;

	error = pci_iomul_func_ioport(func, in.bar, in.offset, &port);
	if (error)
		goto out;

	pci_iomul_switch_to(iomul, sw, iomul->pdev);
	switch (in.size) {
	case 4:
		value = inl(port);
		break;
	case 2:
		value = inw(port);
		break;
	case 1:
		value = inb(port);
		break;
	default:
		error = -EINVAL;
		break;
	}

out:
	mutex_unlock(&sw->lock);
	mutex_unlock(&iomul->lock);

	if (error == 0 && put_user(value, &arg->value))
		return -EFAULT;
	return error;
}

static long pci_iomul_out(struct pci_iomul_data *iomul,
			  struct pci_iomul_out __user *arg)
{
	struct pci_iomul_in out;
	struct pci_iomul_switch *sw;
	struct pci_iomul_func *func;

	long error = 0;
	int port;

	if (copy_from_user(&out, arg, sizeof(out)))
		return -EFAULT;

	if (pci_iomul_lock(iomul, &sw, &func) < 0)
		return -ENODEV;

	error = pci_iomul_func_ioport(func, out.bar, out.offset, &port);
	if (error)
		goto out;

	pci_iomul_switch_to(iomul, sw, iomul->pdev);
	switch (out.size) {
	case 4:
		outl(out.value, port);
		break;
	case 2:
		outw(out.value, port);
		break;
	case 1:
		outb(out.value, port);
		break;
	default:
		error = -EINVAL;
		break;
	}

out:
	mutex_unlock(&sw->lock);
	mutex_unlock(&iomul->lock);
	return error;
}

static long pci_iomul_ioctl(struct file *filp,
			    unsigned int cmd, unsigned long arg)
{
	long error;
	struct pci_iomul_data *iomul =
		(struct pci_iomul_data*)filp->private_data;

	if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
		return -EPERM;

	switch (cmd) {
	case PCI_IOMUL_SETUP:
		error = pci_iomul_setup(iomul,
					(struct pci_iomul_setup __user *)arg);
		break;
	case PCI_IOMUL_DISABLE_IO:
		error = pci_iomul_disable_io(iomul);
		break;
	case PCI_IOMUL_IN:
		error = pci_iomul_in(iomul, (struct pci_iomul_in __user *)arg);
		break;
	case PCI_IOMUL_OUT:
		error = pci_iomul_out(iomul,
				      (struct pci_iomul_out __user *)arg);
		break;
	default:
		error = -ENOSYS;
		break;
	}

	return error;
}

static const struct file_operations pci_iomul_fops = {
	.owner = THIS_MODULE,

	.open = pci_iomul_open,
	.release = pci_iomul_release,

	.unlocked_ioctl = pci_iomul_ioctl,
};

static struct miscdevice pci_iomul_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "pci_iomul",
	.nodename = "xen/pci_iomul",
	.fops = &pci_iomul_fops,
};

static int __init pci_iomul_init(void)
{
	int error;

	error = misc_register(&pci_iomul_miscdev);
	if (error != 0) {
		pr_alert("Couldn't register /dev/xen/pci_iomul");
		return error;
	}
	pr_info("PCI IO multiplexer device installed\n");
	return 0;
}

#ifdef MODULE
static void __exit pci_iomul_cleanup(void)
{
	misc_deregister(&pci_iomul_miscdev);
}
module_exit(pci_iomul_cleanup);
#endif

/*
 * This must be called after pci fixup final which is called by
 * device_initcall(pci_init).
 */
late_initcall(pci_iomul_init);

MODULE_ALIAS("devname:xen/pci_iomul");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Isaku Yamahata <yamahata@valinux.co.jp>");
MODULE_DESCRIPTION("PCI IO space multiplexing driver");
