#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

static struct vm_area_struct init_mmap = INIT_MMAP;
static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS;
struct mm_struct init_mm = INIT_MM(init_mm);

/* .text section in head.S is aligned at 2 page boundry and this gets linked
 * right after that so that the init_task_union is aligned properly as well.
 * We really don't need this special alignment like the Intel does, but
 * I do it anyways for completeness.
 */
union task_union init_task_union
	__attribute__((__section__(".text"))) =
		{ INIT_TASK(init_task_union.task) };
