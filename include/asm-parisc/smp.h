#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/config.h>

#ifdef CONFIG_SMP
extern volatile unsigned long cpu_online_map;  /* Bitmap of available cpu's */
#endif

#endif
