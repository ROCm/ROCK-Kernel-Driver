#ifndef _PPC64_PROM_H
#define _PPC64_PROM_H

/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996 Paul Mackerras.
 *
 * Updates for PPC64 by Peter Bergner & David Engebretsen, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/proc_fs.h>
#include <asm/atomic.h>

#define PTRRELOC(x)     ((typeof(x))((unsigned long)(x) - offset))
#define PTRUNRELOC(x)   ((typeof(x))((unsigned long)(x) + offset))
#define RELOC(x)        (*PTRRELOC(&(x)))

#define LONG_LSW(X) (((unsigned long)X) & 0xffffffff)
#define LONG_MSW(X) (((unsigned long)X) >> 32)

typedef u32 phandle;
typedef u32 ihandle;
typedef u32 phandle32;
typedef u32 ihandle32;

extern char *prom_display_paths[];
extern unsigned int prom_num_displays;

struct address_range {
	unsigned long space;
	unsigned long address;
	unsigned long size;
};

struct interrupt_info {
	int	line;
	int	sense;		/* +ve/-ve logic, edge or level, etc. */
};

struct pci_address {
	u32 a_hi;
	u32 a_mid;
	u32 a_lo;
};

struct isa_address {
	u32 a_hi;
	u32 a_lo;
};

struct isa_range {
	struct isa_address isa_addr;
	struct pci_address pci_addr;
	unsigned int size;
};

struct pci_range32 {
	struct pci_address child_addr;
	unsigned int  parent_addr;
  	unsigned long size; 
};

struct pci_range64 {
	struct pci_address child_addr;
  	unsigned long parent_addr;
        unsigned long size; 
};

union pci_range {
	struct {
		struct pci_address addr;
		u32 phys;
		u32 size_hi;
	} pci32;
	struct {
		struct pci_address addr;
		u32 phys_hi;
		u32 phys_lo;
		u32 size_hi;
		u32 size_lo;
	} pci64;
};

struct of_tce_table {
	phandle node;
	unsigned long base;
	unsigned long size;
};
extern struct of_tce_table of_tce_table[];

struct reg_property {
	unsigned long address;
	unsigned long size;
};

struct reg_property32 {
	unsigned int address;
	unsigned int size;
};

struct reg_property64 {
	unsigned long address;
	unsigned long size;
};

struct reg_property_pmac {
	unsigned int address_hi;
	unsigned int address_lo;
	unsigned int size;
};

struct translation_property {
	unsigned long virt;
	unsigned long size;
	unsigned long phys;
	unsigned int flags;
};

struct property {
	char	*name;
	int	length;
	unsigned char *value;
	struct property *next;
};

/* NOTE: the device_node contains PCI specific info for pci devices.
 * This perhaps could be hung off the device_node with another struct,
 * but for now it is directly in the node.  The phb ptr is a good
 * indication of a real PCI node.  Other nodes leave these fields zeroed.
 */
struct pci_controller;
struct iommu_table;
struct device_node {
	char	*name;
	char	*type;
	phandle	node;
	phandle linux_phandle;
	int	n_addrs;
	struct	address_range *addrs;
	int	n_intrs;
	struct	interrupt_info *intrs;
	char	*full_name;

	/* PCI stuff probably doesn't belong here */
	int	busno;			/* for pci devices */
	int	bussubno;		/* for pci devices */
	int	devfn;			/* for pci devices */
#define DN_STATUS_BIST_FAILED (1<<0)
	int	status;			/* Current device status (non-zero is bad) */
	int	eeh_mode;		/* See eeh.h for possible EEH_MODEs */
	int	eeh_config_addr;
	struct  pci_controller *phb;	/* for pci devices */
	struct	iommu_table *iommu_table;	/* for phb's or bridges */

	struct	property *properties;
	struct	device_node *parent;
	struct	device_node *child;
	struct	device_node *sibling;
	struct	device_node *next;	/* next device of same type */
	struct	device_node *allnext;	/* next in list of all nodes */
	struct  proc_dir_entry *pde;       /* this node's proc directory */
	struct  proc_dir_entry *name_link; /* name symlink */
	struct  proc_dir_entry *addr_link; /* addr symlink */
	atomic_t _users;                 /* reference count */
	unsigned long _flags;
};

/* flag descriptions */
#define OF_STALE   0 /* node is slated for deletion */
#define OF_DYNAMIC 1 /* node and properties were allocated via kmalloc */

#define OF_IS_STALE(x) test_bit(OF_STALE, &x->_flags)
#define OF_MARK_STALE(x) set_bit(OF_STALE, &x->_flags)
#define OF_IS_DYNAMIC(x) test_bit(OF_DYNAMIC, &x->_flags)
#define OF_MARK_DYNAMIC(x) set_bit(OF_DYNAMIC, &x->_flags)

/*
 * Until 32-bit ppc can add proc_dir_entries to its device_node
 * definition, we cannot refer to pde, name_link, and addr_link
 * in arch-independent code.
 */
#define HAVE_ARCH_DEVTREE_FIXUPS

static inline void set_node_proc_entry(struct device_node *dn, struct proc_dir_entry *de)
{
	dn->pde = de;
}

static void inline set_node_name_link(struct device_node *dn, struct proc_dir_entry *de)
{
	dn->name_link = de;
}

static void inline set_node_addr_link(struct device_node *dn, struct proc_dir_entry *de)
{
	dn->addr_link = de;
}

typedef u32 prom_arg_t;

struct prom_args {
        u32 service;
        u32 nargs;
        u32 nret;
        prom_arg_t args[10];
        prom_arg_t *rets;     /* Pointer to return values in args[16]. */
};

struct prom_t {
	unsigned long entry;
	ihandle root;
	ihandle chosen;
	int cpu;
	ihandle stdout;
	ihandle disp_node;
	struct prom_args args;
	unsigned long version;
	unsigned long encode_phys_size;
	struct bi_record *bi_recs;
};

extern struct prom_t prom;
extern char *of_stdout_device;

extern int boot_cpuid;

/* OBSOLETE: Old stlye node lookup */
extern struct device_node *find_devices(const char *name);
extern struct device_node *find_type_devices(const char *type);
extern struct device_node *find_path_device(const char *path);
extern struct device_node *find_compatible_devices(const char *type,
						   const char *compat);
extern struct device_node *find_all_nodes(void);

/* New style node lookup */
extern struct device_node *of_find_node_by_name(struct device_node *from,
	const char *name);
extern struct device_node *of_find_node_by_type(struct device_node *from,
	const char *type);
extern struct device_node *of_find_compatible_node(struct device_node *from,
	const char *type, const char *compat);
extern struct device_node *of_find_node_by_path(const char *path);
extern struct device_node *of_find_all_nodes(struct device_node *prev);
extern struct device_node *of_get_parent(const struct device_node *node);
extern struct device_node *of_get_next_child(const struct device_node *node,
					     struct device_node *prev);
extern struct device_node *of_node_get(struct device_node *node);
extern void of_node_put(struct device_node *node);

/* For updating the device tree at runtime */
extern int of_add_node(const char *path, struct property *proplist);
extern int of_remove_node(struct device_node *np);

/* Other Prototypes */
extern unsigned long prom_init(unsigned long, unsigned long, unsigned long,
	unsigned long, unsigned long);
extern void relocate_nodes(void);
extern void finish_device_tree(void);
extern int device_is_compatible(struct device_node *device, const char *);
extern int machine_is_compatible(const char *compat);
extern unsigned char *get_property(struct device_node *node, const char *name,
				   int *lenp);
extern void print_properties(struct device_node *node);
extern int prom_n_addr_cells(struct device_node* np);
extern int prom_n_size_cells(struct device_node* np);
extern int prom_n_intr_cells(struct device_node* np);
extern void prom_get_irq_senses(unsigned char *senses, int off, int max);
extern void prom_add_property(struct device_node* np, struct property* prop);

#endif /* _PPC64_PROM_H */
