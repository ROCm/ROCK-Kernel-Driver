/*
 * names.c - a very simple name database for PnP devices
 *
 * Some code is based on names.c from linux pci
 * Copyright 1993--1999 Drew Eckhardt, Frederic Potter,
 * David Mosberger-Tang, Martin Mares
 *
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/string.h>
#include <linux/pnp.h>

#include "base.h"

#ifdef CONFIG_PNP_NAMES

static char *pnp_id_eisaid[] = {
#define ID(x,y) x,
#include "idlist.h"
};

static char *pnp_id_names[] = {
#define ID(x,y) y,
#include "idlist.h"
};

void
pnp_name_device(struct pnp_dev *dev)
{
	int i;
	char *name = dev->dev.name;
	for(i=0; i<sizeof(pnp_id_eisaid)/sizeof(pnp_id_eisaid[0]); i++){
		if (compare_pnp_id(dev->id,pnp_id_eisaid[i])){
			snprintf(name, DEVICE_NAME_SIZE, "%s", pnp_id_names[i]);
			return;
		}
	}
}

#else

void
pnp_name_device(struct pnp_dev *dev)
{
	return;
}

#endif /* CONFIG_PNP_NAMES */
