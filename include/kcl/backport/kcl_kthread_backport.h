/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_KTHREAD_BACKPORT_H
#define AMDKCL_KTHREAD_BACKPORT_H
#include <linux/sched.h>
#include <linux/kthread.h>
#include <kcl/kcl_kthread.h>

#if !defined(HAVE___KTHREAD_SHOULD_PARK)
#define __kthread_should_park __kcl_kthread_should_park
#endif

#endif
