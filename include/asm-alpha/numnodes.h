#ifndef _ASM_MAX_NUMNODES_H
#define _ASM_MAX_NUMNODES_H

/*
 * Currently the Wildfire is the only discontigmem/NUMA capable Alpha core.
 */
#if defined(CONFIG_ALPHA_WILDFIRE) || defined(CONFIG_ALPHA_GENERIC)
# include <asm/core_wildfire.h>
# define MAX_NUMNODES		WILDFIRE_MAX_QBB
#endif

#endif /* _ASM_MAX_NUMNODES_H */
