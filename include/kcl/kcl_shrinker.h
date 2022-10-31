/* SPDX-License-Identifier: GPL-2.0 */

#ifndef AMDKCL_SHRINKER_H
#define AMDKCL_SHRINKER_H

#ifndef HAVE_SYNCHRONIZE_SHRINKERS
extern void synchronize_shrinkers(void);
#endif

static inline int __printf(2, 3) kcl_register_shrinker(struct shrinker *shrinker,
                                            const char *fmt, ...)
{
#if defined(HAVE_REGISTER_SHRINKER_WITH_TWO_ARGUMENTS)
        return register_shrinker(shrinker, fmt);
#else
        return register_shrinker(shrinker);
#endif
}

#endif
