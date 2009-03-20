/*
 * Copyright (c) 2008, NEC Corporation.
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
#define PATH_STR_MAX 128

struct guestdev {
	struct list_head root_list;
	char hid[HID_LEN + 1];
	char uid[UID_LEN + 1];
	int seg;
	int bbn;
	struct guestdev_node *child;
};

struct guestdev_node {
	int dev;
	int func;
	struct guestdev_node *child;
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
static int pci_get_hid_uid(char *str, char *hid, char *uid)
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

	strncpy(hid, sp, len);
	hid[len] = '\0';

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

	strncpy(uid, sp, len);
	uid[len] = '\0';
	return TRUE;

format_err_end:
	return FALSE;
}

/* Get device and function */
static int pci_get_dev_func(char *str, int *dev, int *func)
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
static int pci_check_extended_guestdev_format(char *str)
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
static void pci_make_guestdev_path_str(struct guestdev *gdev,
					char *gdev_str, int buf_size)
{
	struct guestdev_node *node;
	/* max length for "HID:UID" (hid+uid+':'+'\0') */
	const int hid_uid_len = HID_LEN + UID_LEN + 2;
	/* max length for "-DEV#.FUNC#" (dev+func+'-'+'.'+'\0') */
	const int dev_func_len = DEV_LEN + FUNC_LEN + 3;

	/* check buffer size for HID:UID */
	if (buf_size < hid_uid_len)
		return;

	memset(gdev_str, 0, buf_size);

	if (strlen(gdev->uid))
		sprintf(gdev_str, "%s:%s", gdev->hid, gdev->uid);
	else
		sprintf(gdev_str, "%s", gdev->hid);
	buf_size -= strlen(gdev_str);

	node = gdev->child;
	while (node) {
		/* check buffer size for -DEV#.FUNC# */
		if (buf_size < dev_func_len)
			return;
		sprintf(gdev_str + strlen(gdev_str), "-%02x.%01x",
			node->dev, node->func);
		buf_size -= dev_func_len;
		node = node->child;
	}
}

/* Free guestdev and nodes */
static void pci_free_guestdev(struct guestdev *gdev)
{
	struct guestdev_node *node, *next;

	if (!gdev)
		return;

	node = gdev->child;
	while (node) {
		next = node->child;
		kfree(node);
		node = next;
	}
	list_del(&gdev->root_list);
	kfree(gdev);
}

/* Free guestdev_list */
static void pci_free_guestdev_list(void)
{
	struct list_head *head, *tmp;
	struct guestdev *gdev;

	list_for_each_safe(head, tmp, &guestdev_list) {
		gdev = list_entry(head, struct guestdev, root_list);
		pci_free_guestdev(gdev);
	}
}

/* Copy guestdev and nodes */
static struct guestdev *pci_copy_guestdev(struct guestdev *gdev_src)
{
	struct guestdev *gdev;
	struct guestdev_node *node, *node_src, *node_upper;

	gdev = kmalloc(sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		goto allocate_err_end;

	memset(gdev, 0, sizeof(*gdev));
	INIT_LIST_HEAD(&gdev->root_list);
	strcpy(gdev->hid, gdev_src->hid);
	strcpy(gdev->uid, gdev_src->uid);
	gdev->seg = gdev_src->seg;
	gdev->bbn = gdev_src->bbn;

	node_upper = NULL;

	node_src = gdev_src->child;
	while (node_src) {
		node = kmalloc(sizeof(*node), GFP_KERNEL);
		if (!node)
			goto allocate_err_end;
		memset(node, 0, sizeof(*node));
		node->dev = node_src->dev;
		node->func = node_src->func;
		if (!node_upper)
			gdev->child = node;
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
static int pci_make_guestdev(char *path_str)
{
	char hid[HID_LEN + 1], uid[UID_LEN + 1];
	char *sp, *ep;
	struct guestdev *gdev, *gdev_org;
	struct guestdev_node *node, *node_tmp;
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
	strcpy(gdev_org->hid, hid);
	strcpy(gdev_org->uid, uid);
	gdev_org->seg = INVALID_SEG;
	gdev_org->bbn = INVALID_BBN;

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
					goto err_end;
				}
			}
			continue;
		}
		if (pci_get_dev_func(sp, &dev, &func)) {
			node = kmalloc(sizeof(*node), GFP_KERNEL);
			if (!node)
				goto allocate_err_end;
			memset(node, 0, sizeof(*node));
			node->dev = dev;
			node->func = func;
			/* add node to end of guestdev */
			if (gdev->child) {
				node_tmp = gdev->child;
				while (node_tmp->child) {
					node_tmp = node_tmp->child;
				}
				node_tmp->child = node;
			} else
				gdev->child = node;
		} else
			goto format_err_end;

		ep = strpbrk(sp, "-|)");
		if (!ep)
			ep = strchr(sp, '\0');
		/* *ep is '|' OR ')' OR '\0' ? */
		if (*ep != '-') {
			list_add_tail(&gdev->root_list, &guestdev_list);
			if (*ep == '|') {
				/* Between '|' and '|' ? */
				if (strchr(ep + 1, '|')) {
					gdev = pci_copy_guestdev(gdev_org);
					if (!gdev) {
						ret_val = -ENOMEM;
						goto err_end;
					}
				} else
					gdev = gdev_org;
			}
		}
		if (*ep == ')')
			ep++;
		sp = ep + 1;
	} while (*ep != '\0');

	return ret_val;

format_err_end:
	printk(KERN_ERR
		"PCI: The format of the guestdev parameter is illegal. [%s]\n",
		path_str);
	ret_val = -EINVAL;
	goto err_end;

allocate_err_end:
	printk(KERN_ERR "PCI: Failed to allocate memory.\n");
	ret_val = -ENOMEM;
	goto err_end;

err_end:
	if (gdev_org && (gdev_org != gdev))
		pci_free_guestdev(gdev_org);
	if (gdev)
		pci_free_guestdev(gdev);
	return ret_val;
}

/* Parse guestdev parameter */
static int __init pci_parse_guestdev(void)
{
	int len, ret_val;
	char *sp, *ep;
	struct list_head *head;
	struct guestdev *gdev;
	char path_str[PATH_STR_MAX];

	ret_val = 0;

	len = strlen(guestdev_param);
	if (len == 0)
		goto end;

	sp = guestdev_param;

	do {
		ep = strchr(sp, ',');
		/* Chop */
		if (ep)
			*ep = '\0';
		if (!pci_check_extended_guestdev_format(sp)) {
			pci_free_guestdev_list();
			return -EINVAL;
		}

		ret_val = pci_make_guestdev(sp);
		if (ret_val) {
			pci_free_guestdev_list();
			return ret_val;
		}
		sp = ep + 1;
	} while (ep);

	list_for_each(head, &guestdev_list) {
		gdev = list_entry(head, struct guestdev, root_list);
		pci_make_guestdev_path_str(gdev, path_str, PATH_STR_MAX);
		printk(KERN_DEBUG
			"PCI: %s has been reserved for guest domain.\n",
			path_str);
	}

end:
	return ret_val;
}

arch_initcall(pci_parse_guestdev);

/* Get command line */
static int __init pci_guestdev_setup(char *str)
{
	if (strlen(str) >= COMMAND_LINE_SIZE)
		return 0;
	strcpy(guestdev_param, str);
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

/* Is sbdf within guestdev */
static int pci_sbdf_in_guestdev_sub_tree(struct guestdev *gdev,
					struct pcidev_sbdf *sbdf)
{
	int seg, bbn;
	struct guestdev_node *gdev_node;
	struct pcidev_sbdf_node *sbdf_node;

	if (!gdev || !sbdf)
		return FALSE;

	/* Compare seg and bbn */
	if (gdev->seg == INVALID_SEG ||
	    gdev->bbn == INVALID_BBN) {
		if (acpi_pci_get_root_seg_bbn(gdev->hid,
		    gdev->uid, &seg, &bbn)) {
			gdev->seg = seg;
			gdev->bbn = bbn;
		} else
			return FALSE;
	}

	if (gdev->seg != sbdf->seg || gdev->bbn != sbdf->bus)
		return FALSE;

	gdev_node = gdev->child;
	sbdf_node = sbdf->child;

	/* Compare dev and func */
	while (gdev_node) {
		if (!sbdf_node)
			return FALSE;
		if (gdev_node->dev != sbdf_node->dev ||
		    gdev_node->func != sbdf_node->func)
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
	if (sscanf(dev->dev.bus_id, "%04x:%02x", &sbdf->seg, &sbdf->bus) != 2)
		goto err_end;
	return TRUE;

err_end:
	pci_free_sbdf(sbdf);
	return FALSE;
}

/* Is PCI device belongs to the subtree of the guestdev parameter */
int pci_is_guestdev(struct pci_dev *dev)
{
	struct guestdev *gdev;
	struct pcidev_sbdf sbdf;
	struct list_head *head;
	int result;

	if (!dev)
		return FALSE;
	memset(&sbdf, 0 ,sizeof(sbdf));
	if (!pci_get_sbdf_from_pcidev(dev, &sbdf))
		return FALSE;

	result = FALSE;
	list_for_each(head, &guestdev_list) {
		gdev = list_entry(head, struct guestdev, root_list);
		if (pci_sbdf_in_guestdev_sub_tree(gdev, &sbdf)) {
			result = TRUE;
			break;
		}
	}
	pci_free_sbdf(&sbdf);
	return result;
}
EXPORT_SYMBOL(pci_is_guestdev);

#ifdef CONFIG_PCI_REASSIGN
static int reassign_resources;

static int __init pci_set_reassign_resources(char *str)
{
	reassign_resources = 1;

	return 1;
}
__setup("reassign_resources", pci_set_reassign_resources);

int pci_is_guestdev_to_reassign(struct pci_dev *dev)
{
	if (reassign_resources)
		return pci_is_guestdev(dev);
	return FALSE;
}
#endif

/* Check whether the guestdev exists under the pci root bus */
static int __init pci_check_guestdev_path_exists(
		struct guestdev *gdev, struct pci_bus *bus)
{
	struct guestdev_node *node;
	struct pci_dev *dev;

	node = gdev->child;
	while (node) {
		if (!bus)
			return FALSE;
		dev = pci_get_slot(bus, PCI_DEVFN(node->dev, node->func));
		if (!dev) {
			pci_dev_put(dev);
			return FALSE;
		}
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
	char path_str[PATH_STR_MAX];

	list_for_each(head, &guestdev_list) {
		gdev = list_entry(head, struct guestdev, root_list);
		if (gdev->seg == INVALID_SEG ||
			gdev->bbn == INVALID_BBN) {
			if (acpi_pci_get_root_seg_bbn(gdev->hid,
				gdev->uid, &seg, &bbn)) {
				gdev->seg = seg;
				gdev->bbn = bbn;
			} else {
				pci_make_guestdev_path_str(gdev, path_str,
					PATH_STR_MAX);
				printk(KERN_INFO
					"PCI: Device does not exist. %s\n",
					path_str);
				continue;
			}
		}

		bus = pci_find_bus(gdev->seg, gdev->bbn);
		if (!bus || !pci_check_guestdev_path_exists(gdev, bus)) {
			pci_make_guestdev_path_str(gdev, path_str,
				PATH_STR_MAX);
			printk(KERN_INFO
				"PCI: Device does not exist. %s\n", path_str);
		}
	}
	return 0;
}

fs_initcall(pci_check_guestdev_exists);

