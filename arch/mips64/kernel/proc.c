/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1999, 2001 Ralf Baechle
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/watch.h>

extern unsigned long unaligned_instructions;
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
	size_t len;

	len = sprintf(buffer, "cpu\t\t\t: MIPS\n");
#if 0
	len += sprintf(buffer + len, "cpu model\t\t: %s V%d.%d\n",
	               cpu_name[mips_cputype <= CPU_LAST ?
	                        mips_cputype :
	                        CPU_UNKNOWN],
	               (version >> 4) & 0x0f,
	               version & 0x0f);
	len += sprintf(buffer + len, "system type\t\t: %s %s\n",
		       mach_group_names[mips_machgroup],
		       mach_group_to_name[mips_machgroup][mips_machtype]);
#endif
	len += sprintf(buffer + len, "BogoMIPS\t\t: %lu.%02lu\n",
		       (loops_per_jiffy + 2500) / (500000/HZ),
	               ((loops_per_jiffy + 2500) / (5000/HZ)) % 100);
	len += sprintf(buffer + len, "Number of cpus\t\t: %d\n", smp_num_cpus);
#if defined (__MIPSEB__)
	len += sprintf(buffer + len, "byteorder\t\t: big endian\n");
#endif
#if defined (__MIPSEL__)
	len += sprintf(buffer + len, "byteorder\t\t: little endian\n");
#endif
	len += sprintf(buffer + len, "unaligned accesses\t: %lu\n",
		       unaligned_instructions);
	len += sprintf(buffer + len, "wait instruction\t: %s\n",
	               wait_available ? "yes" : "no");
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
