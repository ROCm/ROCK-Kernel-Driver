#ifndef __UM_A_OUT_H
#define __UM_A_OUT_H

#include "asm/arch/a.out.h"

#undef STACK_TOP

extern unsigned long stacksizelim;

extern unsigned long host_task_size;

extern int honeypot;

#define STACK_ROOM (stacksizelim)

#define STACK_TOP (honeypot ? host_task_size : task_size)

#endif
