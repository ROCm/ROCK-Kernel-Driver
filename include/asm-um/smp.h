#ifndef __UM_SMP_H
#define __UM_SMP_H

extern unsigned long cpu_online_map;

#ifdef CONFIG_SMP

#include "linux/config.h"
#include "asm/current.h"

#define smp_processor_id() (current->processor)
#define cpu_logical_map(n) (n)
#define cpu_number_map(n) (n)
#define PROC_CHANGE_PENALTY	15 /* Pick a number, any number */
extern int hard_smp_processor_id(void);
#define NO_PROC_ID -1

#endif

#endif
