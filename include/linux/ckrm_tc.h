#include <linux/ckrm_rc.h>



#define TASK_CLASS_TYPE_NAME "taskclass"

typedef struct ckrm_task_class {
	struct ckrm_core_class core;   
} ckrm_task_class_t;


// Index into genmfdesc array, defined in rcfs/dir_modules.c,
// which has the mfdesc entry that taskclass wants to use
#define TC_MF_IDX  0


extern int ckrm_forced_reclassify_pid(int pid, struct ckrm_task_class *cls);

