/*
 * crasher.c, it breaks things
 */

#include <linux/completion.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/types.h>

static DECLARE_BITMAP(module_exiting, 1);
static DECLARE_COMPLETION(startup);
static unsigned int threads = 1;
static unsigned int timer;
static bool call_panic, call_bug, call_warn;
static bool trap_null, call_null, jump_null;
static unsigned long trap_read, trap_write, call_bad, jump_bad;

module_param(timer, uint, 0);
module_param(call_panic, bool, 0);
module_param(call_bug, bool, 0);
module_param(call_warn, bool, 0);
module_param(trap_null, bool, 0);
module_param(trap_read, ulong, 0);
module_param(trap_write, ulong, 0);
module_param(call_null, bool, 0);
module_param(call_bad, ulong, 0);
module_param(jump_null, bool, 0);
module_param(jump_bad, ulong, 0);
module_param(threads, uint, 0);
MODULE_PARM_DESC(timer, "perform the selected crash action from the timer context in the specified number of ms");
MODULE_PARM_DESC(call_panic, "test option. call panic() and render the system unusable.");
MODULE_PARM_DESC(call_bug, "test option. call BUG() and render the system unusable.");
MODULE_PARM_DESC(call_warn, "test option. call WARN() and leave the system usable.");
MODULE_PARM_DESC(trap_null, "test option. dereference a NULL pointer to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(trap_read, "test option. read from an invalid address to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(trap_write, "test option. write to an invalid address to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(call_null, "test option. call a NULL pointer to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(call_bad, "test option. call an invalid address to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(jump_null, "test option. jump to a NULL pointer to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(jump_bad, "test option. jump to an invalid address to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(threads, "number of threads to run");
MODULE_LICENSE("GPL");

#define NUM_ALLOC 24
static const unsigned int sizes[] = { 32, 64, 128, 192, 256, 1024, 2048, 4096 };
#define NUM_SIZES ARRAY_SIZE(sizes)

struct mem_buf {
	char *buf;
	unsigned int size;
};

static inline char crasher_hash(unsigned int i)
{
	return (i % 119) + 8;
}

static void mem_alloc(struct mem_buf *b)
{
	unsigned int i, size = sizes[prandom_u32_max(NUM_SIZES)];

	b->buf = kmalloc(size, GFP_KERNEL);
	if (!b->buf)
		return;

	b->size = size;

	for (i = 0 ; i < size; i++)
		b->buf[i] = crasher_hash(i);
}

static void mem_check_free(struct mem_buf *b)
{
	unsigned int i;

	for (i = 0 ; i < b->size; i++) {
		if (b->buf[i] != crasher_hash(i)) {
			pr_crit("crasher: verify error at %p, offset %u, wanted %d, found %d, size %u\n",
					&b->buf[i], i, crasher_hash(i),
					b->buf[i], b->size);
		}
	}
	kfree(b->buf);
	b->buf = NULL;
	b->size = 0;
}

static void mem_verify(void)
{
	struct mem_buf *b, bufs[NUM_ALLOC] = {};

	while (!test_bit(0, module_exiting)) {
		b = &bufs[prandom_u32_max(NUM_ALLOC)];
		if (b->size)
			mem_check_free(b);
		else
			mem_alloc(b);
		schedule_timeout_interruptible(prandom_u32_max(HZ / 10));
	}

	for (b = bufs; b < &bufs[NUM_ALLOC]; b++)
		if (b->size)
			mem_check_free(b);
}

static int crasher_thread(void *unused)
{
	complete(&startup);
	mem_verify();
	complete(&startup);

	return 0;
}

static int crash_now(void)
{
	if (call_panic) {
		panic("test panic from crasher module. Good luck!");
		return -EFAULT;
	}
	if (call_bug) {
		pr_crit("crasher: triggering BUG\n");
		BUG();
		return -EFAULT;
	}
	if (WARN(call_warn, "crasher: triggering WARN\n"))
		return -EFAULT;

	if (trap_null) {
		volatile char *p = NULL;
		pr_crit("crasher: dereferencing NULL pointer\n");
		p[0] = '\n';
		return -EFAULT;
	}
	if (trap_read) {
		const volatile char *p = (char *)trap_read;
		pr_crit("crasher: reading from invalid(?) address %p\n", p);
		return p[0] ? -EFAULT : -EACCES;
	}
	if (trap_write) {
		volatile char *p = (char *)trap_write;
		pr_crit("crasher: writing to invalid(?) address %p\n", p);
		p[0] = ' ';
		return -EFAULT;
	}

	if (call_null) {
		void (*f)(void) = NULL;
		pr_crit("crasher: calling NULL pointer\n");
		f();
		return -EFAULT;
	}
	if (call_bad) {
		void (*f)(void) = (void(*)(void))call_bad;
		pr_crit("crasher: calling invalid(?) address %p\n", f);
		f();
		return -EFAULT;
	}

	/* These two depend on the compiler doing tail call optimization */
	if (jump_null) {
		int (*f)(void) = NULL;
		pr_crit("crasher: jumping to NULL\n");
		return f();
	}
	if (jump_bad) {
		int (*f)(void) = (int(*)(void))jump_bad;
		pr_crit("crasher: jumping to invalid(?) address %p\n", f);
		return f();
	}

	return 0;
}

static void crash_timer_cb(struct timer_list *unused)
{
	crash_now();
}

static DEFINE_TIMER(crash_timer, crash_timer_cb);

static int __init crasher_init(void)
{
	int ret, i;

	if (timer) {
		pr_info("crasher: crashing in %u miliseconds...\n", timer);
		mod_timer(&crash_timer, jiffies + msecs_to_jiffies(timer));
		return 0;
	}

	ret = crash_now();
	if (ret)
		return ret;

	pr_info("crasher: running %u threads. Testing sizes: ", threads);
	for (i = 0; i < NUM_SIZES; i++)
		pr_cont("%u ", sizes[i]);
	pr_cont("\n");

	for (i = 0; i < threads; i++)
		kthread_run(crasher_thread, NULL, "crasher");
	for (i = 0; i < threads; i++)
		wait_for_completion(&startup);

	return 0;
}

static void __exit crasher_exit(void)
{
	unsigned int i;

	if (timer) {
		del_timer_sync(&crash_timer);
		return;
	}

	set_bit(0, module_exiting);

	for (i = 0; i < threads; i++)
		wait_for_completion(&startup);

	pr_info("crasher: all threads done\n");
}

module_init(crasher_init);
module_exit(crasher_exit);
