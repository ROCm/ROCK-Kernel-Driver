/*
 * Copyright (c) 2008, 2009 NEC Corporation.
 * Copyright (c) 2009 Isaku Yamahata
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/acpi.h>
#include <asm/setup.h>

#define HID_LEN 8
#define UID_LEN 8
#define DEV_LEN 2
#define FUNC_LEN 1
#define DEV_NUM_MAX 31
#define FUNC_NUM_MAX 7
#define INVALID_SEG (-1)
#define INVALID_BBN (-1)
#define GUESTDEV_STR_MAX 128

#define GUESTDEV_FLAG_TYPE_MASK 0x3
#define GUESTDEV_FLAG_DEVICEPATH 0x1
#define GUESTDEV_FLAG_SBDF 0x2

#define GUESTDEV_OPT_IOMUL	0x1

struct guestdev {
	int flags;
	int options;
	struct list_head root_list;
	union {
		struct devicepath {
			char hid[HID_LEN + 1];
			char uid[UID_LEN + 1];
			int seg;
			int bbn;
			struct devicepath_node *child;
		} devicepath;
		struct sbdf {
			int seg;
			int bus;
			int dev;
			int func;
		} sbdf;
	} u;
};

struct devicepath_node {
	int dev;
	int func;
	struct devicepath_node *child;
};

struct pcidev_sbdf {
	int seg;
	int bus;
	struct pcidev_sbdf_node *child;
};

struct pcidev_sbdf_node {
	int dev;
	int func;
	struct pcidev_sbdf_node *child;
};

static char guestdev_param[COMMAND_LINE_SIZE];
static LIST_HEAD(guestdev_list);

/* Get hid and uid */
static int __init pci_get_hid_uid(char *str, char *hid, char *uid)
{
	char *sp, *ep;
	int len;

	sp = str;
	ep = strchr(sp, ':');
	if (!ep) {
		ep = strchr(sp, '-');
		if (!ep)
			goto format_err_end;
	}
	/* hid length */
	len = ep - sp;
	if (len <= 0 || HID_LEN < len)
		goto format_err_end;

	strlcpy(hid, sp, len);

	if (*ep == '-') { /* no uid */
		uid[0] = '\0';
		return TRUE;
	}

	sp = ep + 1;
	ep = strchr(sp, '-');
	if (!ep)
		ep = strchr(sp, '\0');

	/* uid length */
	len = ep - sp;
	if (len <= 0 || UID_LEN < len)
		goto format_err_end;

	strlcpy(uid, sp, len);
	return TRUE;

format_err_end:
	return FALSE;
}

/* Get device and function */
static int __init pci_get_dev_func(char *str, int *dev, int *func)
{
	if (sscanf(str, "%02x.%01x", dev, func) != 2)
		goto format_err_end;

	if (*dev < 0 || DEV_NUM_MAX < *dev)
		goto format_err_end;

	if (*func < 0 || FUNC_NUM_MAX < *func)
		goto format_err_end;

	return TRUE;

format_err_end:
	return FALSE;
}

/* Check extended guestdev parameter format error */
static int __init pci_check_extended_guestdev_format(char *str)
{
	int flg;
	char *p;

	/* Check extended format */
	if (strpbrk(str, "(|)") == NULL)
		return TRUE;

	flg = 0;
	p = str;
	while (*p) {
		switch (*p) {
		case '(':
			/* Check nesting error */
			if (flg != 0)
				goto format_err_end;
			flg = 1;
			/* Check position of '(' is head or
			   previos charactor of '(' is not '-'. */
			if (p == str || *(p - 1) != '-')
				goto format_err_end;
			break;
		case ')':
			/* Check nesting error */
			if (flg != 1)
				goto format_err_end;
			flg = 0;
			/* Check next charactor of ')' is not '\0' */
			if (*(p + 1) != '\0')
				goto format_err_end;
			break;
		case '|':
			/* Check position of '|' is outside of '(' and ')' */
			if (flg != 1)
				goto format_err_end;
			break;
		default:
			break;
		}
		p++;
	}
	/* Check number of '(' and ')' are not equal */
	if (flg != 0)
		goto format_err_end;
	return TRUE;

format_err_end:
	printk(KERN_ERR
		"PCI: The format of the guestdev parameter is illegal. [%s]\n",
		str);
	return FALSE;
}

/* Make guestdev strings */
static void pci_make_guestdev_str(struct guestdev *gdev,
					char *gdev_str, int buf_size)
{
	struct devicepath_node *node;
	int count;

	switch (gdev->flags & GUESTDEV_FLAG_TYPE_MASK) {
	case GUESTDEV_FLAG_DEVICEPATH:
		memset(gdev_str, 0, buf_size);

		if (strlen(gdev->u.devicepath.uid))
			count = snprintf(gdev_str, buf_size, "%s:%s",
						gdev->u.devicepath.hid,
						gdev->u.devicepath.uid);
		else
			count = snprintf(gdev_str, buf_size, "%s",
						 gdev->u.devicepath.hid);
		if (count < 0)
			return;

		node = gdev->u.devicepath.child;
		while (node) {
			gdev_str += count;
			buf_size -= count;
			if (buf_size <= 0)
				return;
			count = snprintf(gdev_str, buf_size, "-%02x.%01x",
				node->dev, node->func);
			if (count < 0)
				return;
			node = node->child;
		}
		break;
	case GUESTDEV_FLAG_SBDF:
		snprintf(gdev_str, buf_size, "%04x:%02x:%02x.%01x",
					gdev->u.sbdf.seg, gdev->u.sbdf.bus,
					gdev->u.sbdf.dev, gdev->u.sbdf.func);
		break;
	default:
		BUG();
	}
}

/* Free guestdev and nodes */
static void __init pci_free_guestdev(struct guestdev *gdev)
{
	struct devicepath_node *node, *next;

	if (!gdev)
		return;
	if (gdev->flags & GUESTDEV_FLAG_DEVICEPATH) {
		node = gdev->u.devicepath.child;
		while (node) {
			next = node->child;
			kfree(node);
			node = next;
		}
	}
	list_del(&gdev->root_list);
	kfree(gdev);
}

/* Copy guestdev and nodes */
struct guestdev __init *pci_copy_guestdev(struct guestdev *gdev_src)
{
	struct guestdev *gdev;
	struct devicepath_node *node, *node_src, *node_upper;

	BUG_ON(!(gdev_src->flags & GUESTDEV_FLAG_DEVICEPATH));

	gdev = kmalloc(sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		goto allocate_err_end;

	memset(gdev, 0, sizeof(*gdev));
	INIT_LIST_HEAD(&gdev->root_list);
	gdev->flags = gdev_src->flags;
	gdev->options = gdev_src->options;
	strcpy(gdev->u.devicepath.hid, gdev_src->u.devicepath.hid);
	strcpy(gdev->u.devicepath.uid, gdev_src->u.devicepath.uid);
	gdev->u.devicepath.seg = gdev_src->u.devicepath.seg;
	gdev->u.devicepath.bbn = gdev_src->u.devicepath.bbn;

	node_upper = NULL;

	node_src = gdev_src->u.devicepath.child;
	while (node_src) {
		node = kmalloc(sizeof(*node), GFP_KERNEL);
		if (!node)
			goto allocate_err_end;
		memset(node, 0, sizeof(*node));
		node->dev = node_src->dev;
		node->func = node_src->func;
		if (!node_upper)
			gdev->u.devicepath.child = node;
		else
			node_upper->child = node;
		node_upper = node;
		node_src = node_src->child;
	}

	return gdev;

allocate_err_end:
	if (gdev)
		pci_free_guestdev(gdev);
	printk(KERN_ERR "PCI: Failed to allocate memory.\n");
	return NULL;
}

/* Make guestdev from path strings */
static int __init pci_make_devicepath_guestdev(char *path_str, int options)
{
	char hid[HID_LEN + 1], uid[UID_LEN + 1];
	char *sp, *ep;
	struct guestdev *gdev, *gdev_org;
	struct devicepath_node *node, *node_tmp;
	int dev, func, ret_val;

	ret_val = 0;
	gdev = gdev_org = NULL;
	sp = path_str;
	/* Look for end of hid:uid'-' */
	ep = strchr(sp, '-');
	/* Only hid, uid. (No dev, func) */
	if (!ep)
		goto format_err_end;

	memset(hid, 0 ,sizeof(hid));
	memset(uid, 0, sizeof(uid));
	if (!pci_get_hid_uid(sp, hid, uid))
		goto format_err_end;

	gdev_org = kmalloc(sizeof(*gdev_org), GFP_KERNEL);
	if (!gdev_org)
		goto allocate_err_end;
	memset(gdev_org, 0, sizeof(*gdev_org));
	INIT_LIST_HEAD(&gdev_org->root_list);
	gdev_org->flags = GUESTDEV_FLAG_DEVICEPATH;
	gdev_org->options = options;
	strcpy(gdev_org->u.devicepath.hid, hid);
	strcpy(gdev_org->u.devicepath.uid, uid);
	gdev_org->u.devicepath.seg = INVALID_SEG;
	gdev_org->u.devicepath.bbn = INVALID_BBN;

	gdev = gdev_org;

	sp = ep + 1;
	ep = sp;
	do {
		if (*sp == '(') {
			sp++;
			if (strchr(sp, '|')) {
				gdev = pci_copy_guestdev(gdev_org);
				if (!gdev) {
					ret_val = -ENOMEM;
					goto end;
				}
			}
			continue;
		}
		if (gdev && pci_get_dev_func(sp, &dev, &func)) {
			node = kmalloc(sizeof(*node), GFP_KERNEL);
			if (!node)
				goto allocate_err_end;
			memset(node, 0, sizeof(*node));
			node->dev = dev;
			node->func = func;
			/* add node to end of guestdev */
			if (gdev->u.devicepath.child) {
				node_tmp = gdev->u.devicepath.child;
				while (node_tmp->child) {
					node_tmp = node_tmp->child;
				}
				node_tmp->child = node;
			} else
				gdev->u.devicepath.child = node;
		} else if (gdev) {
			printk(KERN_ERR
				"PCI: Can't obtain dev# and #func# from %s.\n",
				sp);
			ret_val = -EINVAL;
			if (gdev == gdev_org)
				goto end;
			pci_free_guestdev(gdev);
			gdev = NULL;
		}

		ep = strpbrk(sp, "-|)");
		if (!ep)
			ep = strchr(sp, '\0');
		/* Is *ep '|' OR ')' OR '\0' ? */
		if (*ep != '-') {
			if (gdev)
				list_add_tail(&gdev->root_list, &guestdev_list);
			if (*ep == '|') {
				/* Between '|' and '|' ? */
				if (strchr(ep + 1, '|')) {
					gdev = pci_copy_guestdev(gdev_org);
					if (!gdev) {
						ret_val = -ENOMEM;
						goto end;
					}
				} else {
					gdev = gdev_org;
					gdev_org = NULL;
				}
			} else {
				gdev_org = NULL;
				gdev = NULL;
			}
		}
		if (*ep == ')')
			ep++;
		sp = ep + 1;
	} while (*ep != '\0');

	goto end;

format_err_end:
	printk(KERN_ERR
		"PCI: The format of the guestdev parameter is illegal. [%s]\n",
		path_str);
	ret_val = -EINVAL;
	goto end;

allocate_err_end:
	printk(KERN_ERR "PCI: Failed to allocate memory.\n");
	ret_val = -ENOMEM;
	goto end;

end:
	if (gdev_org && (gdev_org != gdev))
		pci_free_guestdev(gdev_org);
	if (gdev)
		pci_free_guestdev(gdev);
	return ret_val;
}

static int __init pci_make_sbdf_guestdev(char* str, int options)
{
	struct guestdev *gdev;
	int seg, bus, dev, func;

	if (sscanf(str, "%x:%x:%x.%x", &seg, &bus, &dev, &func) != 4) {
		seg = 0;
		if (sscanf(str, "%x:%x.%x", &bus, &dev, &func) != 3)
			return -EINVAL;
	}
	gdev = kmalloc(sizeof(*gdev), GFP_KERNEL);
	if (!gdev) {
		printk(KERN_ERR "PCI: Failed to allocate memory.\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&gdev->root_list);
	gdev->flags = GUESTDEV_FLAG_SBDF;
	gdev->options = options;
	gdev->u.sbdf.seg = seg;
	gdev->u.sbdf.bus = bus;
	gdev->u.sbdf.dev = dev;
	gdev->u.sbdf.func = func;
	list_add_tail(&gdev->root_list, &guestdev_list);
	return 0;
}

static int __init pci_parse_options(const char *str)
{
	int options = 0;
	char *ep;

	while (str) {
		str++;
		ep = strchr(str, '+');
		if (ep)
			ep = '\0';	/* Chop */

		if (!strcmp(str, "iomul"))
			options |= GUESTDEV_OPT_IOMUL;

		str = ep;
	}
	return options;
}

/* Parse guestdev parameter */
static int __init pci_parse_guestdev(void)
{
	int len;
	char *sp, *ep, *op;
	int options;
	struct list_head *head;
	struct guestdev *gdev;
	char path_str[GUESTDEV_STR_MAX];
	int ret_val = 0;

	len = strlen(guestdev_param);
	if (len == 0)
		return 0;

	sp = guestdev_param;

	do {
		ep = strchr(sp, ',');
		/* Chop */
		if (ep)
			*ep = '\0';
		options = 0;
		op = strchr(sp, '+');
		if (op && (!ep || op < ep)) {
			options = pci_parse_options(op);
			*op = '\0';	/* Chop */
		}
		ret_val = pci_make_sbdf_guestdev(sp, options);
		if (ret_val == -EINVAL) {
			if (pci_check_extended_guestdev_format(sp)) {
				ret_val = pci_make_devicepath_guestdev(
					sp, options);
				if (ret_val && ret_val != -EINVAL)
					break;
			}
		} else if (ret_val)
			break;

		if (ep)
			ep++;
		sp = ep;
	} while (ep);

	list_for_each(head, &guestdev_list) {
		gdev = list_entry(head, struct guestdev, root_list);
		pci_make_guestdev_str(gdev, path_str, GUESTDEV_STR_MAX);
		printk(KERN_DEBUG
			"PCI: %s has been reserved for guest domain.\n",
			path_str);
	}
	return 0;
}

arch_initcall(pci_parse_guestdev);

/* Get command line */
static int __init pci_guestdev_setup(char *str)
{
	if (strlen(str) >= COMMAND_LINE_SIZE)
		return 0;
	strlcpy(guestdev_param, str, sizeof(guestdev_param));
	return 1;
}

__setup("guestdev=", pci_guestdev_setup);

/* Free sbdf and nodes */
static void pci_free_sbdf(struct pcidev_sbdf *sbdf)
{
	struct pcidev_sbdf_node *node, *next;

	node = sbdf->child;
	while (node) {
		next = node->child;
		kfree(node);
		node = next;
	}
	/* Skip kfree(sbdf) */
}

/* Does PCI device belong to sub tree specified by guestdev with device path? */
typedef int (*pci_node_match_t)(const struct devicepath_node *gdev_node,
				const struct pcidev_sbdf_node *sbdf_node,
				int options);

static int pci_node_match(const struct devicepath_node *gdev_node,
			  const struct pcidev_sbdf_node *sbdf_node,
			  int options_unused)
{
	return (gdev_node->dev == sbdf_node->dev &&
		gdev_node->func == sbdf_node->func);
}

static int pci_is_in_devicepath_sub_tree(struct guestdev *gdev,
					 struct pcidev_sbdf *sbdf,
					 pci_node_match_t match)
{
	int seg, bbn;
	struct devicepath_node *gdev_node;
	struct pcidev_sbdf_node *sbdf_node;

	if (!gdev || !sbdf)
		return FALSE;

	BUG_ON(!(gdev->flags & GUESTDEV_FLAG_DEVICEPATH));

	/* Compare seg and bbn */
	if (gdev->u.devicepath.seg == INVALID_SEG ||
	    gdev->u.devicepath.bbn == INVALID_BBN) {
		if (acpi_pci_get_root_seg_bbn(gdev->u.devicepath.hid,
		    gdev->u.devicepath.uid, &seg, &bbn)) {
			gdev->u.devicepath.seg = seg;
			gdev->u.devicepath.bbn = bbn;
		} else
			return FALSE;
	}

	if (gdev->u.devicepath.seg != sbdf->seg ||
	    gdev->u.devicepath.bbn != sbdf->bus)
		return FALSE;

	gdev_node = gdev->u.devicepath.child;
	sbdf_node = sbdf->child;

	/* Compare dev and func */
	while (gdev_node) {
		if (!sbdf_node)
			return FALSE;
		if (!match(gdev_node, sbdf_node, gdev->options))
			return FALSE;
		gdev_node = gdev_node->child;
		sbdf_node = sbdf_node->child;
	}
	return TRUE;
}

/* Get sbdf from device */
static int pci_get_sbdf_from_pcidev(
	struct pci_dev *dev, struct pcidev_sbdf *sbdf)
{
	struct pcidev_sbdf_node *node;

	if (!dev)
		return FALSE;

	for(;;) {
		node = kmalloc(sizeof(*node), GFP_KERNEL);
		if (!node) {
			printk(KERN_ERR "PCI: Failed to allocate memory.\n");
			goto err_end;
		}
		memset(node, 0, sizeof(*node));
		node->dev = PCI_SLOT(dev->devfn);
		node->func = PCI_FUNC(dev->devfn);

		if (!sbdf->child)
			sbdf->child = node;
		else {
			node->child = sbdf->child;
			sbdf->child = node;
		}
		if (!dev->bus)
			goto err_end;
		if (!dev->bus->self)
			break;
		dev = dev->bus->self;
	}
	if (sscanf(dev_name(&dev->dev), "%04x:%02x", &sbdf->seg, &sbdf->bus) != 2)
		goto err_end;
	return TRUE;

err_end:
	pci_free_sbdf(sbdf);
	return FALSE;
}

/* Does PCI device belong to sub tree specified by guestdev with sbdf? */
typedef int (*pci_sbdf_match_t)(const struct guestdev *gdev,
				const  struct pci_dev *dev);

static int pci_sbdf_match(const struct guestdev *gdev,
			  const struct pci_dev *dev)
{
	int seg, bus;

	if (sscanf(dev_name(&dev->dev), "%04x:%02x", &seg, &bus) != 2)
		return FALSE;

	return gdev->u.sbdf.seg == seg &&
		gdev->u.sbdf.bus == bus &&
		gdev->u.sbdf.dev == PCI_SLOT(dev->devfn) &&
		gdev->u.sbdf.func == PCI_FUNC(dev->devfn);
}

static int pci_is_in_sbdf_sub_tree(struct guestdev *gdev, struct pci_dev *dev,
				   pci_sbdf_match_t match)
{
	BUG_ON(!(gdev->flags & GUESTDEV_FLAG_SBDF));
	for (;;) {
		if (match(gdev, dev))
			return TRUE;
		if (!dev->bus || !dev->bus->self)
			break;
		dev = dev->bus->self;
	}
	return FALSE;
}

/* Does PCI device belong to sub tree specified by guestdev parameter? */
static int __pci_is_guestdev(struct pci_dev *dev, pci_node_match_t node_match,
			     pci_sbdf_match_t sbdf_match)
{
	struct guestdev *gdev;
	struct pcidev_sbdf pcidev_sbdf, *sbdf = NULL;
	struct list_head *head;
	int result = FALSE;

	if (!dev)
		return FALSE;

	list_for_each(head, &guestdev_list) {
		gdev = list_entry(head, struct guestdev, root_list);
		switch (gdev->flags & GUESTDEV_FLAG_TYPE_MASK) {
		case GUESTDEV_FLAG_DEVICEPATH:
			if (sbdf == NULL) {
				sbdf = &pcidev_sbdf;
				memset(sbdf, 0 ,sizeof(*sbdf));
				if (!pci_get_sbdf_from_pcidev(dev, sbdf))
					goto out;
			}
			if (pci_is_in_devicepath_sub_tree(gdev, sbdf,
							  node_match)) {
				result = TRUE;
				goto out;
			}
			break;
		case GUESTDEV_FLAG_SBDF:
			if (pci_is_in_sbdf_sub_tree(gdev, dev, sbdf_match)) {
				result = TRUE;
				goto out;
			}
			break;
		default:
			BUG();
		}
	}
out:
	if (sbdf)
		pci_free_sbdf(sbdf);
	return result;
}

int pci_is_guestdev(struct pci_dev *dev)
{
	return __pci_is_guestdev(dev, pci_node_match, pci_sbdf_match);
}
EXPORT_SYMBOL_GPL(pci_is_guestdev);

static int reassign_resources;

static int __init pci_set_reassign_resources(char *str)
{
	if (str && !strcmp(str, "all"))
		reassign_resources = -1;
	else
		reassign_resources = 1;

	return 1;
}
__setup("reassign_resources", pci_set_reassign_resources);

int pci_is_guestdev_to_reassign(struct pci_dev *dev)
{
	if (reassign_resources < 0)
		return TRUE;
	if (reassign_resources)
		return pci_is_guestdev(dev);
	return FALSE;
}

#ifdef CONFIG_PCI_IOMULTI
static int pci_iomul_node_match(const struct devicepath_node *gdev_node,
				const struct pcidev_sbdf_node *sbdf_node,
				int options)
{
	return (options & GUESTDEV_OPT_IOMUL) &&
		((gdev_node->child != NULL &&
		  sbdf_node->child != NULL &&
		  gdev_node->dev == sbdf_node->dev &&
		  gdev_node->func == sbdf_node->func) ||
		 (gdev_node->child == NULL &&
		  sbdf_node->child == NULL &&
		  gdev_node->dev == sbdf_node->dev));
}

static int pci_iomul_sbdf_match(const struct guestdev *gdev,
				const struct pci_dev *dev)
{
	int seg, bus;

	if (sscanf(dev_name(&dev->dev), "%04x:%02x", &seg, &bus) != 2)
		return FALSE;

	return (gdev->options & GUESTDEV_OPT_IOMUL) &&
		gdev->u.sbdf.seg == seg &&
		gdev->u.sbdf.bus == bus &&
		gdev->u.sbdf.dev == PCI_SLOT(dev->devfn);
}

int pci_is_iomuldev(struct pci_dev *dev)
{
	return __pci_is_guestdev(dev,
				 pci_iomul_node_match, pci_iomul_sbdf_match);
}
#endif /* CONFIG_PCI_IOMULTI */

/* Check whether the devicepath exists under the pci root bus */
static int __init pci_check_devicepath_exists(
		struct guestdev *gdev, struct pci_bus *bus)
{
	struct devicepath_node *node;
	struct pci_dev *dev;

	BUG_ON(!(gdev->flags & GUESTDEV_FLAG_DEVICEPATH));

	node = gdev->u.devicepath.child;
	while (node) {
		if (!bus)
			return FALSE;
		dev = pci_get_slot(bus, PCI_DEVFN(node->dev, node->func));
		if (!dev)
			return FALSE;
		bus = dev->subordinate;
		node = node->child;
		pci_dev_put(dev);
	}
	return TRUE;
}

/* Check whether the guestdev exists in the PCI device tree */
static int __init pci_check_guestdev_exists(void)
{
	struct list_head *head;
	struct guestdev *gdev;
	int seg, bbn;
	struct pci_bus *bus;
	struct pci_dev *dev;
	char path_str[GUESTDEV_STR_MAX];

	list_for_each(head, &guestdev_list) {
		gdev = list_entry(head, struct guestdev, root_list);
		switch (gdev->flags & GUESTDEV_FLAG_TYPE_MASK) {
		case GUESTDEV_FLAG_DEVICEPATH:
			if (gdev->u.devicepath.seg == INVALID_SEG ||
				gdev->u.devicepath.bbn == INVALID_BBN) {
				if (acpi_pci_get_root_seg_bbn(
					gdev->u.devicepath.hid,
					gdev->u.devicepath.uid, &seg, &bbn)) {
					gdev->u.devicepath.seg = seg;
					gdev->u.devicepath.bbn = bbn;
				} else {
					pci_make_guestdev_str(gdev,
						path_str, GUESTDEV_STR_MAX);
					printk(KERN_INFO
					"PCI: Device does not exist. %s\n",
					path_str);
					continue;
				}
			}

			bus = pci_find_bus(gdev->u.devicepath.seg,
						gdev->u.devicepath.bbn);
			if (!bus ||
				!pci_check_devicepath_exists(gdev, bus)) {
				pci_make_guestdev_str(gdev, path_str,
					GUESTDEV_STR_MAX);
				printk(KERN_INFO
					"PCI: Device does not exist. %s\n",
					path_str);
			}
			break;
		case GUESTDEV_FLAG_SBDF:
			bus = pci_find_bus(gdev->u.sbdf.seg, gdev->u.sbdf.bus);
			if (bus) {
				dev = pci_get_slot(bus,
					PCI_DEVFN(gdev->u.sbdf.dev,
							gdev->u.sbdf.func));
				if (dev) {
					pci_dev_put(dev);
					continue;
				}
			}
			pci_make_guestdev_str(gdev, path_str, GUESTDEV_STR_MAX);
			printk(KERN_INFO "PCI: Device does not exist. %s\n",
								path_str);
			break;
		default:
			BUG();
		}
	}
	return 0;
}

fs_initcall(pci_check_guestdev_exists);

