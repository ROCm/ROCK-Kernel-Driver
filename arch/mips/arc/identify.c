/*
 * identify.c: identify machine by looking up system identifier
 *
 * Copyright (C) 1998 Thomas Bogendoerfer
 * 
 * This code is based on arch/mips/sgi/kernel/system.c, which is
 * 
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: identify.c,v 1.2 1999/02/25 21:04:13 tsbogend Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>

#include <asm/sgi/sgi.h>
#include <asm/sgialib.h>
#include <asm/bootinfo.h>

struct smatch {
    char *name;
    int group;
    int type;
    int flags;
};

static struct smatch mach_table[] = {
    { "SGI-IP22", MACH_GROUP_SGI, MACH_SGI_INDY, PROM_FLAG_ARCS },
    { "Microsoft-Jazz", MACH_GROUP_JAZZ, MACH_MIPS_MAGNUM_4000, 0 },
    { "PICA-61", MACH_GROUP_JAZZ, MACH_ACER_PICA_61, 0 },
    { "RM200PCI", MACH_GROUP_SNI_RM, MACH_SNI_RM200_PCI, 0 }
};

int prom_flags;

static struct smatch * __init string_to_mach(char *s)
{
    int i;
    
    for (i = 0; i < sizeof (mach_table); i++) {
	if(!strcmp(s, mach_table[i].name))
	    return &mach_table[i];
    }
    prom_printf("\nYeee, could not determine architecture type <%s>\n", s);
    prom_printf("press a key to reboot\n");
    prom_getchar();
    romvec->imode();
    return NULL;
}

void __init prom_identify_arch(void)
{
    pcomponent *p;
    struct smatch *mach;
    
    /* The root component tells us what machine architecture we
     * have here.
     */
    p = prom_getchild(PROM_NULL_COMPONENT);
    printk("ARCH: %s\n", p->iname);
    mach = string_to_mach(p->iname);

    mips_machgroup = mach->group;
    mips_machtype = mach->type;
    prom_flags = mach->flags;
}

