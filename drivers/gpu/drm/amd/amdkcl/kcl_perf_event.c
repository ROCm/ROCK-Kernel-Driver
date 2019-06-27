#include <kcl/kcl_perf_event.h>
#include "kcl_common.h"

void (*_kcl_perf_event_update_userpage)(struct perf_event *event);
EXPORT_SYMBOL(_kcl_perf_event_update_userpage);

void amdkcl_perf_event_init(void)
{
	_kcl_perf_event_update_userpage = amdkcl_fp_setup("perf_event_update_userpage",NULL);
}

