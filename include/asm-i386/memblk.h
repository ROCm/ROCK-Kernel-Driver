#ifndef _ASM_I386_MEMBLK_H_
#define _ASM_I386_MEMBLK_H_

#include <linux/device.h>
#include <linux/mmzone.h>
#include <linux/memblk.h>
#include <linux/topology.h>

#include <asm/node.h>

struct i386_memblk {
	struct memblk memblk;
};
extern struct i386_memblk memblk_devices[MAX_NR_MEMBLKS];

static inline int arch_register_memblk(int num){
	int p_node = memblk_to_node(num);

	return register_memblk(&memblk_devices[num].memblk, num, 
				&node_devices[p_node].node);
}

#endif /* _ASM_I386_MEMBLK_H_ */
