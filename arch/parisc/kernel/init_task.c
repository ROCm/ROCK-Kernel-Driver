#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/init_task.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);

EXPORT_SYMBOL(init_mm);

/*
 * Initial task structure.
 *
 * We need to make sure that this is 16384-byte aligned due to the
 * way process stacks are handled. This is done by having a special
 * "init_task" linker map entry..
 */
unsigned char interrupt_stack[ISTACK_SIZE] __attribute__ ((section("init_istack"), aligned(4096)));
union thread_union init_thread_union
	__attribute__((aligned(128))) __attribute__((__section__(".data.init_task"))) =
		{ INIT_THREAD_INFO(init_task) };

pgd_t swapper_pg_dir[PTRS_PER_PGD] __attribute__ ((aligned(4096))) = { {0}, };
#ifdef __LP64__
unsigned long pmd0[PTRS_PER_PMD] __attribute__ ((aligned(4096))) = { 0, };
#endif
unsigned long pg0[PT_INITIAL * PTRS_PER_PTE] __attribute__ ((aligned(4096))) = { 0, };

/*
 * Initial task structure.
 *
 * All other task structs will be allocated on slabs in fork.c
 */
EXPORT_SYMBOL(init_task);

__asm__(".data");
struct task_struct init_task = INIT_TASK(init_task);
