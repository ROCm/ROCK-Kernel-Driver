/*
 *    Copyright (c) 2004 Manish Ahuja <mahuja@us.ibm.com> IBM CORP.
 *
 *    Module name: vpurr.h
 *
 *    Description:
 *      Architecture- / platform-specific boot-time initialization code for
 *      tracking purr utilization and other performace features in coming 
 * 	releases for splpar/smt machines.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

DECLARE_PER_CPU(struct cpu_util_store, cpu_util_sampler);

struct cpu_util_store {
        struct timer_list cpu_util_timer;
        u64 start_purr;
        u64 current_purr;
        u64 tb;
};
