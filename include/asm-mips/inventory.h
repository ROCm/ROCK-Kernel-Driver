/*
 * $Id:$
 */
#ifndef __ASM_MIPS_INVENTORY_H
#define __ASM_MIPS_INVENTORY_H

#include <linux/config.h>

#ifdef CONFIG_BINFMT_IRIX
typedef struct inventory_s {
	struct inventory_s *inv_next;
	int    inv_class;
	int    inv_type;
	int    inv_controller;
	int    inv_unit;
	int    inv_state;
} inventory_t;

extern int inventory_items;
void add_to_inventory (int class, int type, int controller, int unit, int state);
int dump_inventory_to_user (void *userbuf, int size);
void init_inventory (void);

#else
#define add_to_inventory(c,t,o,u,s)
#define init_inventory()
#endif
#endif /* defined(CONFIG_BINFMT_IRIX) */
