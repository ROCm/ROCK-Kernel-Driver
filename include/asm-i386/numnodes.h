#ifndef _ASM_MAX_NUMNODES_H
#define _ASM_MAX_NUMNODES_H

#include <linux/config.h>

#ifdef CONFIG_X86_NUMAQ
#include <asm/numaq.h>
#elif CONFIG_NUMA
#include <asm/srat.h>
#else
#define MAX_NUMNODES	1
#endif /* CONFIG_X86_NUMAQ */

#endif /* _ASM_MAX_NUMNODES_H */
