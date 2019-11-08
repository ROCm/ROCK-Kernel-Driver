#ifndef AMDKCL_MM_BACKPORT_H
#define AMDKCL_MM_BACKPORT_H
#include <kcl/kcl_mm.h>
#if defined(HAVE_MM_H)
#include <linux/sched/mm.h>
#else
#include <linux/sched.h>
#endif
#include <linux/mm.h>

#ifndef HAVE_MM_ACCESS
#define mm_access _kcl_mm_access
#endif
#endif
