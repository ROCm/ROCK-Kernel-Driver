#ifndef AMDKCL_MM_BACKPORT_H
#define AMDKCL_MM_BACKPORT_H
#include <kcl/kcl_mm.h>
#include <kcl/kcl_sched_mm_h.h>
#include <linux/mm.h>

#ifndef HAVE_MM_ACCESS
#define mm_access _kcl_mm_access
#endif

#endif
