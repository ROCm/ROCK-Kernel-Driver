#ifndef AMD_KCL_PERF_EVENT_H
#define AMD_KCL_PERF_EVENT_H
#include <linux/perf_event.h>

#if !defined(HAVE_PERF_EVENT_UPDATE_USERPAGE)
extern void (*_kcl_perf_event_update_userpage)(struct perf_event *event);
#endif
#endif
