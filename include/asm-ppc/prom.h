/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#ifdef __KERNEL__
#ifndef _PPC_PROM_H
#define _PPC_PROM_H

#include <linux/config.h>

typedef void *phandle;
typedef void *ihandle;

extern char *prom_display_paths[];
extern unsigned int prom_num_displays;
#ifndef CONFIG_MACH_SPECIFIC
extern int have_of;
#endif

struct address_range {
	unsigned int space;
	unsigned int address;
	unsigned int size;
};

struct interrupt_info {
	int	line;
	int	sense;		/* +ve/-ve logic, edge or level, etc. */
};

struct reg_property {
	unsigned int address;
	unsigned int size;
};

struct property {
	char	*name;
	int	length;
	unsigned char *value;
	struct property *next;
};

struct device_node {
	char	*name;
	char	*type;
	phandle	node;
	int	n_addrs;
	struct	address_range *addrs;
	int	n_intrs;
	struct	interrupt_info *intrs;
	char	*full_name;
	struct	property *properties;
	struct	device_node *parent;
	struct	device_node *child;
	struct	device_node *sibling;
	struct	device_node *next;	/* next device of same type */
	struct	device_node *allnext;	/* next in list of all nodes */
#if 0 /* Don't change this structure for now or you'll break BootX ! */
	int	n_addr_cells;
	int	n_size_cells;
#endif	
};

struct prom_args;
typedef void (*prom_entry)(struct prom_args *);

/* Prototypes */
extern void abort(void);
extern unsigned long prom_init(int, int, prom_entry);
extern void prom_print(const char *msg);
extern void relocate_nodes(void);
extern void finish_device_tree(void);
extern struct device_node *find_devices(const char *name);
extern struct device_node *find_type_devices(const char *type);
extern struct device_node *find_path_device(const char *path);
extern struct device_node *find_compatible_devices(const char *type,
						   const char *compat);
extern struct device_node *find_phandle(phandle);
extern struct device_node *find_all_nodes(void);
extern int device_is_compatible(struct device_node *device, const char *);
extern int machine_is_compatible(const char *compat);
extern unsigned char *get_property(struct device_node *node, const char *name,
				   int *lenp);
extern void prom_add_property(struct device_node* np, struct property* prop);
extern void prom_get_irq_senses(unsigned char *, int, int);
extern int prom_n_addr_cells(struct device_node* np);
extern int prom_n_size_cells(struct device_node* np);

extern void print_properties(struct device_node *node);
extern int call_rtas(const char *service, int nargs, int nret,
		     unsigned long *outputs, ...);
extern void prom_drawstring(const char *c);
extern void prom_drawhex(unsigned long v);
extern void prom_drawchar(char c);

extern void map_bootx_text(void);
extern void bootx_update_display(unsigned long phys, int width, int height,
				 int depth, int pitch);

#endif /* _PPC_PROM_H */
#endif /* __KERNEL__ */
