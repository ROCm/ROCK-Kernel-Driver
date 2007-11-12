/* 
 *   Creation Date: <1999/02/22 23:22:17 samuel>
 *   Time-stamp: <2003/06/02 16:17:36 samuel>
 *   
 *	<prom.h>
 *	
 *	OF device tree structs
 *   
 *   Copyright (C) 1999, 2000, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_PROM
#define _H_PROM

typedef void *p_phandle_t;

typedef struct {
	int			nirq;
	int			irq[5];
	unsigned long		controller[5];
} irq_info_t;

typedef struct p_property {
	char			*name;
	int			length;
	unsigned char 		*value;
	struct p_property	*next;
} p_property_t;

typedef struct mol_device_node {
	p_phandle_t		node;
	struct p_property 	*properties;
	struct mol_device_node	*parent;
	struct mol_device_node	*child;
	struct mol_device_node	*sibling;
	struct mol_device_node	*next;		/* next device of same type */
	struct mol_device_node	*allnext;	/* next in list of all nodes */
	char			*unit_string;
} mol_device_node_t;

#endif   /* _H_PROM */
