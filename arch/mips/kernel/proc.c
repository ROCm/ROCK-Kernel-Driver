/*
 *  linux/arch/mips/kernel/proc.c
 *
 *  Copyright (C) 1995, 1996  Ralf Baechle
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/watch.h>

unsigned long unaligned_instructions;
unsigned int vced_count, vcei_count;

/*
 * BUFFER is PAGE_SIZE bytes long.
 *
 * Currently /proc/cpuinfo is being abused to print data about the
 * number of date/instruction cacheflushes.
 */
int get_cpuinfo(char *buffer)
{
	char fmt [64];
	const char *cpu_name[] = CPU_NAMES;
	const char *mach_group_names[] = GROUP_NAMES;
	const char *mach_unknown_names[] = GROUP_UNKNOWN_NAMES;
	const char *mach_jazz_names[] = GROUP_JAZZ_NAMES;
	const char *mach_dec_names[] = GROUP_DEC_NAMES;
	const char *mach_arc_names[] = GROUP_ARC_NAMES;
	const char *mach_sni_rm_names[] = GROUP_SNI_RM_NAMES;
	const char *mach_acn_names[] = GROUP_ACN_NAMES;
	const char *mach_sgi_names[] = GROUP_SGI_NAMES;
	const char *mach_cobalt_names[] = GROUP_COBALT_NAMES;
	const char *mach_nec_ddb_names[] = GROUP_NEC_DDB_NAMES;
	const char *mach_baget_names[] = GROUP_BAGET_NAMES;
	const char **mach_group_to_name[] = { mach_unknown_names,
	                                      mach_jazz_names,
	                                      mach_dec_names,
	                                      mach_arc_names,
	                                      mach_sni_rm_names,
	                                      mach_acn_names,
	                                      mach_sgi_names,
					      mach_cobalt_names,
					      mach_nec_ddb_names,
					      mach_baget_names };
	unsigned int version = read_32bit_cp0_register(CP0_PRID);
	int len;

	len = sprintf(buffer, "cpu\t\t\t: MIPS\n");
	len += sprintf(buffer + len, "cpu model\t\t: %s V%d.%d\n",
	               cpu_name[mips_cputype <= CPU_LAST ?
	                        mips_cputype :
	                        CPU_UNKNOWN],
	               (version >> 4) & 0x0f,
	               version & 0x0f);
	len += sprintf(buffer + len, "system type\t\t: %s %s\n",
		       mach_group_names[mips_machgroup],
		       mach_group_to_name[mips_machgroup][mips_machtype]);
	len += sprintf(buffer + len, "BogoMIPS\t\t: %lu.%02lu\n",
		       (loops_per_sec + 2500) / 500000,
	               ((loops_per_sec + 2500) / 5000) % 100);
#if defined (__MIPSEB__)
	len += sprintf(buffer + len, "byteorder\t\t: big endian\n");
#endif
#if defined (__MIPSEL__)
	len += sprintf(buffer + len, "byteorder\t\t: little endian\n");
#endif
	len += sprintf(buffer + len, "unaligned accesses\t: %lu\n",
		       unaligned_instructions);
	len += sprintf(buffer + len, "wait instruction\t: %s\n",
	               cpu_wait ? "yes" : "no");
	len += sprintf(buffer + len, "microsecond timers\t: %s\n",
	               cyclecounter_available ? "yes" : "no");
	len += sprintf(buffer + len, "extra interrupt vector\t: %s\n",
	               dedicated_iv_available ? "yes" : "no");
	len += sprintf(buffer + len, "hardware watchpoint\t: %s\n",
	               watch_available ? "yes" : "no");

	sprintf(fmt, "VCE%%c exceptions\t\t: %s\n",
	        vce_available ? "%d" : "not available");
	len += sprintf(buffer + len, fmt, 'D', vced_count);
	len += sprintf(buffer + len, fmt, 'I', vcei_count);

	return len;
}

void init_irq_proc(void)
{
	/* Nothing, for now.  */
}
