#ifndef _ASM_ALPHA_TOPOLOGY_H
#define _ASM_ALPHA_TOPOLOGY_H

#ifdef CONFIG_NUMA
#ifdef CONFIG_ALPHA_WILDFIRE
/* With wildfire assume 4 CPUs per node */
#define __cpu_to_node(cpu)		((cpu) >> 2)
#endif /* CONFIG_ALPHA_WILDFIRE */
#endif /* CONFIG_NUMA */

#if !defined(CONFIG_NUMA) || !defined(CONFIG_ALPHA_WILDFIRE)
#define __cpu_to_node(cpu)		(0)
#define __memblk_to_node(memblk)	(0)
#define __parent_node(nid)		(0)
#define __node_to_first_cpu(node)	(0)
#define __node_to_cpu_mask(node)	(cpu_online_map)
#define __node_to_memblk(node)		(0)
#endif /* !CONFIG_NUMA || !CONFIG_ALPHA_WILDFIRE */

#endif /* _ASM_ALPHA_TOPOLOGY_H */
