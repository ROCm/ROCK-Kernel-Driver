#ifndef _IPT_PHYSDEV_H
#define _IPT_PHYSDEV_H

#ifdef __KERNEL__
#include <linux/if.h>
#endif

#define IPT_PHYSDEV_OP_MATCH_IN 0x01
#define IPT_PHYSDEV_OP_MATCH_OUT 0x02

struct ipt_physdev_info {
	u_int8_t invert;
	char physindev[IFNAMSIZ];
	char in_mask[IFNAMSIZ];
	char physoutdev[IFNAMSIZ];
	char out_mask[IFNAMSIZ];
};

#endif /*_IPT_PHYSDEV_H*/
