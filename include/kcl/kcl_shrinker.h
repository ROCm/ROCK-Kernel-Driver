/* SPDX-License-Identifier: GPL-2.0 */

#ifndef AMDKCL_SHRINKER_H
#define AMDKCL_SHRINKER_H

#ifndef HAVE_SYNCHRONIZE_SHRINKERS
extern void synchronize_shrinkers(void);
#endif

#endif
