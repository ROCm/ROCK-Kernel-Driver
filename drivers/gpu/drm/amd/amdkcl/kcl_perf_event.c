#include <kcl/kcl_perf_event.h>
#include "kcl_common.h"

#if !defined(HAVE_PERF_EVENT_UPDATE_USERPAGE)
void (*_kcl_perf_event_update_userpage)(struct perf_event *event);
EXPORT_SYMBOL(_kcl_perf_event_update_userpage);
#endif

void amdkcl_perf_event_init(void)
{
#if !defined(HAVE_PERF_EVENT_UPDATE_USERPAGE)
	_kcl_perf_event_update_userpage = amdkcl_fp_setup("perf_event_update_userpage",NULL);
#endif
}

