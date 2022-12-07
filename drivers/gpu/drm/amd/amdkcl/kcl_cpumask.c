// SPDX-License-Identifier: GPL-2.0-only

#include <linux/cpumask.h>
#ifndef for_each_cpu_wrap
/* copied from lib/cpumask.c */
/**
 * cpumask_next_wrap - helper to implement for_each_cpu_wrap
 * @n: the cpu prior to the place to search
 * @mask: the cpumask pointer
 * @start: the start point of the iteration
 * @wrap: assume @n crossing @start terminates the iteration
 *
 * Returns >= nr_cpu_ids on completion
 *
 * Note: the @wrap argument is required for the start condition when
 * we cannot assume @start is set in @mask.
 */
int _kcl_cpumask_next_wrap(int n, const struct cpumask *mask, int start, bool wrap)
{
        int next;

again:
        next = cpumask_next(n, mask);

        if (wrap && n < start && next >= start) {
                return nr_cpumask_bits;

        } else if (next >= nr_cpumask_bits) {
                wrap = true;
                n = -1;
                goto again;
        }

        return next;
}
EXPORT_SYMBOL(_kcl_cpumask_next_wrap);
#endif

