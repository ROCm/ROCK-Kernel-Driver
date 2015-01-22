/*
 * Simple, generic PCI host controller driver targetting firmware-initialised
 * systems and virtual machines (e.g. the PCI emulation provided by kvmtool).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>

struct gen_pci_cfg_bus_ops {
	u32 bus_shift;
	void __iomem *(*map_bus)(struct pci_bus *, unsigned int, int);
};

struct gen_pci_cfg_windows {
	struct resource				res;
	struct resource				*bus_range;
	void __iomem				**win;

	const struct gen_pci_cfg_bus_ops	*ops;
};

struct gen_pci {
	struct pci_host_bridge			host;
	struct gen_pci_cfg_windows		cfg;
	struct list_head			resources;
};

/* fake sysdata for cheating ARCH's pcibios code */
static char	gen_sysdata[256];

static struct gen_pci *gen_pci_get_drvdata(struct pci_bus *bus)
{
	struct device *dev = bus->dev.parent->parent;
	struct gen_pci *pci;

	while (dev) {
		pci = dev_get_drvdata(dev);
		if (pci)
			return pci;
		dev = dev->parent;
	}

	return NULL;
}

static void __iomem *gen_pci_map_cfg_bus_cam(struct pci_bus *bus,
					     unsigned int devfn,
					     int where)
{
	struct gen_pci *pci = gen_pci_get_drvdata(bus);
	resource_size_t idx = bus->number - pci->cfg.bus_range->start;

	return pci->cfg.win[idx] + ((devfn << 8) | where);
}

static struct gen_pci_cfg_bus_ops gen_pci_cfg_cam_bus_ops = {
	.bus_shift	= 16,
	.map_bus	= gen_pci_map_cfg_bus_cam,
};

static void __iomem *gen_pci_map_cfg_bus_ecam(struct pci_bus *bus,
					      unsigned int devfn,
					      int where)
{
	struct gen_pci *pci = gen_pci_get_drvdata(bus);
	resource_size_t idx = bus->number - pci->cfg.bus_range->start;

	return pci->cfg.win[idx] + ((devfn << 12) | where);
}

static struct gen_pci_cfg_bus_ops gen_pci_cfg_ecam_bus_ops = {
	.bus_shift	= 20,
	.map_bus	= gen_pci_map_cfg_bus_ecam,
};

static int gen_pci_config_read(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
	void __iomem *addr;
	struct gen_pci *pci = gen_pci_get_drvdata(bus);

	WARN_ON(!pci);
	if (!pci)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = pci->cfg.ops->map_bus(bus, devfn, where);

	switch (size) {
	case 1:
		*val = readb(addr);
		break;
	case 2:
		*val = readw(addr);
		break;
	default:
		*val = readl(addr);
	}

	return PCIBIOS_SUCCESSFUL;
}

static int gen_pci_config_write(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	void __iomem *addr;
	struct gen_pci *pci = gen_pci_get_drvdata(bus);

	WARN_ON(!pci);
	if (!pci)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = pci->cfg.ops->map_bus(bus, devfn, where);

	switch (size) {
	case 1:
		writeb(val, addr);
		break;
	case 2:
		writew(val, addr);
		break;
	default:
		writel(val, addr);
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops gen_pci_ops = {
	.read	= gen_pci_config_read,
	.write	= gen_pci_config_write,
};

static const struct of_device_id gen_pci_of_match[] = {
	{ .compatible = "pci-host-cam-generic",
	  .data = &gen_pci_cfg_cam_bus_ops },

	{ .compatible = "pci-host-ecam-generic",
	  .data = &gen_pci_cfg_ecam_bus_ops },

	{ },
};
MODULE_DEVICE_TABLE(of, gen_pci_of_match);

static void gen_pci_release_of_pci_ranges(struct gen_pci *pci)
{
	pci_free_resource_list(&pci->resources);
}

static int gen_pci_parse_map_cfg_windows(struct gen_pci *pci)
{
	int err;
	u8 bus_max;
	resource_size_t busn;
	struct resource *bus_range;
	struct device *dev = pci->host.dev.parent;
	struct device_node *np = dev->of_node;

	err = of_address_to_resource(np, 0, &pci->cfg.res);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return err;
	}

	/* Limit the bus-range to fit within reg */
	bus_max = pci->cfg.bus_range->start +
		  (resource_size(&pci->cfg.res) >> pci->cfg.ops->bus_shift) - 1;
	pci->cfg.bus_range->end = min_t(resource_size_t,
					pci->cfg.bus_range->end, bus_max);

	pci->cfg.win = devm_kcalloc(dev, resource_size(pci->cfg.bus_range),
				    sizeof(*pci->cfg.win), GFP_KERNEL);
	if (!pci->cfg.win)
		return -ENOMEM;

	/* Map our Configuration Space windows */
	if (!devm_request_mem_region(dev, pci->cfg.res.start,
				     resource_size(&pci->cfg.res),
				     "Configuration Space"))
		return -ENOMEM;

	bus_range = pci->cfg.bus_range;
	for (busn = bus_range->start; busn <= bus_range->end; ++busn) {
		u32 idx = busn - bus_range->start;
		u32 sz = 1 << pci->cfg.ops->bus_shift;

		pci->cfg.win[idx] = devm_ioremap(dev,
						 pci->cfg.res.start + busn * sz,
						 sz);
		if (!pci->cfg.win[idx])
			return -ENOMEM;
	}

	return 0;
}

static int gen_pci_map_ranges(struct gen_pci *pci,
		resource_size_t io_base)
{
	struct list_head *res = &pci->resources;
	struct pci_host_bridge_window *window;
	int ret;

	list_for_each_entry(window, res, list) {
		struct resource *res = window->res;
		u64 restype = resource_type(res);

		switch (restype) {
		case IORESOURCE_IO:
			ret = pci_remap_iospace(res, io_base);
			if (ret < 0)
				return ret;
			break;
		case IORESOURCE_MEM:
			break;
		case IORESOURCE_BUS:
			pci->cfg.bus_range = res;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

static int gen_pci_probe(struct platform_device *pdev)
{
	int err;
	const char *type;
	const struct of_device_id *of_id;
	const int *prop;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	resource_size_t iobase = 0;
	struct gen_pci *pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	struct pci_bus *bus;
	struct pci_dev *pci_dev = NULL;
	bool probe_only = false;

	if (!pci)
		return -ENOMEM;

	type = of_get_property(np, "device_type", NULL);
	if (!type || strcmp(type, "pci")) {
		dev_err(dev, "invalid \"device_type\" %s\n", type);
		return -EINVAL;
	}

	prop = of_get_property(of_chosen, "linux,pci-probe-only", NULL);
	if (prop) {
		if (*prop)
			probe_only = true;
		else
			probe_only = false;
	}

	of_id = of_match_node(gen_pci_of_match, np);
	pci->cfg.ops = of_id->data;
	pci->host.dev.parent = dev;
	INIT_LIST_HEAD(&pci->host.windows);
	INIT_LIST_HEAD(&pci->resources);

	err = of_pci_get_host_bridge_resources(np, 0, 0xff,
			&pci->resources, &iobase);
	if (err)
		return err;

	err = gen_pci_map_ranges(pci, iobase);
	if (err)
		goto fail;

	/* Parse and map our Configuration Space windows */
	err = gen_pci_parse_map_cfg_windows(pci);
	if (err)
		goto fail;

	err = -ENOMEM;
	platform_set_drvdata(pdev, pci);
	bus = pci_scan_root_bus(dev, 0, &gen_pci_ops, gen_sysdata,
				&pci->resources);
	if (!bus)
		goto fail;

	for_each_pci_dev(pci_dev)
		pci_dev->irq = of_irq_parse_and_map_pci(pci_dev, 0, 0);

	if (!probe_only) {
		pci_bus_size_bridges(bus);
		pci_bus_assign_resources(bus);
		pci_bus_add_devices(bus);
	}

	return 0;
 fail:
	gen_pci_release_of_pci_ranges(pci);
	return err;
}

static struct platform_driver gen_pci_driver = {
	.driver = {
		.name = "pci-host-generic",
		.of_match_table = gen_pci_of_match,
	},
	.probe = gen_pci_probe,
};
module_platform_driver(gen_pci_driver);

MODULE_DESCRIPTION("Generic PCI host driver");
MODULE_AUTHOR("Will Deacon <will.deacon@arm.com>");
MODULE_LICENSE("GPL v2");
