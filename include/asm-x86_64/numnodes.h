#ifndef _ASM_X8664_NUMNODES_H
#define _ASM_X8664_NUMNODES_H 1

#include <linux/config.h>

#ifdef CONFIG_DISCONTIGMEM
#define MAX_NUMNODES 8	/* APIC limit currently */
#else
#define MAX_NUMNODES 1
#endif

#endif
