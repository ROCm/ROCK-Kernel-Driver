/*
 *  include/asm-s390/smp.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 */
#ifndef __ASM_SMP_H
#define __ASM_SMP_H
#include <linux/config.h>
#ifdef CONFIG_SMP
#ifndef __ASSEMBLY__

#include <asm/lowcore.h>
#include <linux/tasks.h>    // FOR NR_CPUS definition only.
#include <linux/kernel.h>   // FOR FASTCALL definition

#define smp_processor_id() (current->processor)
#define NO_PROC_ID		0xFF		/* No processor magic marker */

/*
 *	This magic constant controls our willingness to transfer
 *	a process across CPUs. Such a transfer incurs misses on the L1
 *	cache, and on a P6 or P5 with multiple L2 caches L2 hits. My
 *	gut feeling is this will vary by board in value. For a board
 *	with separate L2 cache it probably depends also on the RSS, and
 *	for a board with shared L2 cache it ought to decay fast as other
 *	processes are run.
 */
 
#define PROC_CHANGE_PENALTY	20		/* Schedule penalty */

extern unsigned long ipi_count;
extern void count_cpus(void);

extern __inline__ int cpu_logical_map(int cpu)
{
        return cpu;
}

extern __inline__ int cpu_number_map(int cpu)
{
        return cpu;
}

extern __inline__ __u16 hard_smp_processor_id(void)
{
        __u16 cpu_address;
 
        __asm__ ("stap %0\n" : "=m" (cpu_address));
        return cpu_address;
}

#define cpu_logical_map(cpu) (cpu)

void smp_local_timer_interrupt(struct pt_regs * regs);

/*
  s390 specific smp.c headers
 */
typedef struct
{
	int        intresting;
	sigp_ccode ccode; 
	__u32      status;
	__u16      cpu;
} sigp_info;

sigp_ccode smp_ext_call_sync(int cpu, ec_cmd_sig cmd,void *parms);
sigp_ccode smp_ext_call_async(int cpu, ec_bit_sig sig);
void smp_ext_call_sync_others(ec_cmd_sig cmd, void *parms);
void smp_ext_call_async_others(ec_bit_sig sig);

int smp_signal_others(sigp_order_code order_code,__u32 parameter,
                      int spin,sigp_info *info);
#endif
#endif
#endif
