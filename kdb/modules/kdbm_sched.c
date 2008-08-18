/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2005 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

MODULE_AUTHOR("SGI");
MODULE_DESCRIPTION("Debug scheduler information");
MODULE_LICENSE("GPL");

static int
kdbm_runqueues(int argc, const char **argv)
{
	unsigned long cpu;
	int ret = 0;

	if (argc == 1) {
		ret = kdbgetularg((char *)argv[1], &cpu);
		if (!ret) {
			if (!cpu_online(cpu)) {
				kdb_printf("Invalid cpu number\n");
			} else
				kdb_runqueue(cpu, kdb_printf);
		}
	} else if (argc == 0) {
		for_each_online_cpu(cpu)
			kdb_runqueue(cpu, kdb_printf);
	} else {
		/* More than one arg */
		kdb_printf("Specify one cpu number\n");
	}
	return ret;
}

static int __init kdbm_sched_init(void)
{
	kdb_register("rq",  kdbm_runqueues, "<cpunum>", "Display runqueue for <cpunum>", 0);
	kdb_register("rqa", kdbm_runqueues, "", "Display all runqueues", 0);
	return 0;
}

static void __exit kdbm_sched_exit(void)
{
	kdb_unregister("rq");
	kdb_unregister("rqa");
}

module_init(kdbm_sched_init)
module_exit(kdbm_sched_exit)
