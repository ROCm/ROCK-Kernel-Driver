/*
 *	PCI defines and function prototypes
 *	Copyright 2003 Dell Computer Corporation
 *        by Matt Domsch <Matt_Domsch@dell.com>
 */

#ifndef LINUX_PCI_DYNIDS_H
#define LINUX_PCI_DYNIDS_H

#include <linux/mod_devicetable.h>
#include <linux/types.h>
#include <linux/config.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/pci.h>

struct pci_driver;
struct pci_device_id;

struct dynid_attribute {
	struct attribute	attr;
	struct list_head        node;
	struct pci_device_id   *id;
	ssize_t (*show)(struct pci_driver * pdrv, struct dynid_attribute *dattr, char * buf);
	ssize_t (*store)(struct pci_driver * pdrv, struct dynid_attribute *dattr, const char * buf, size_t count);
	char name[KOBJ_NAME_LEN];
};

struct pci_dynamic_id_kobj {
	ssize_t (*show_new_id)(struct pci_driver * pdrv, char * buf);
	ssize_t (*store_new_id)(struct pci_driver * pdrv, const char * buf, size_t count);
	ssize_t (*show_id)(struct pci_device_id * id, struct dynid_attribute *dattr, char * buf);

	spinlock_t lock;            /* protects list, index */
	struct list_head list;      /* for IDs added at runtime */
	struct kobject kobj;        /* for displaying the list in sysfs */
	unsigned int nextname;     /* name of next dynamic ID twhen created */
};

#endif
