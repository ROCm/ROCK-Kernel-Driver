#ifndef __ASM_SN_HUB_H
#define __ASM_SN_HUB_H

#include <asm/sn/types.h>
#include <asm/sn/io.h>
#include <asm/sn/klkernvars.h>

struct hub_data {
	kern_vars_t	kern_vars;
};

extern struct hub_data *hub_data[];
#define HUB_DATA(n)		(hub_data[(n)])

#endif /* __ASM_SN_HUB_H */
