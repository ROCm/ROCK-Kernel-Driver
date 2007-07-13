/*
 * crasher.c, it breaks things
 */


#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/moduleparam.h>

static int module_exiting;
static struct completion startup = COMPLETION_INITIALIZER(startup);
static unsigned long rand_seed = 152L;
static unsigned long seed = 152L;
static int threads = 1;
static int call_panic;
static int call_bug;
static int trap_null, call_null, jump_null;
static long trap_read, trap_write, call_bad, jump_bad;

module_param(seed, ulong, 0);
module_param(call_panic, bool, 0);
module_param(call_bug, bool, 0);
module_param(trap_null, bool, 0);
module_param(trap_read, long, 0);
module_param(trap_write, long, 0);
module_param(call_null, bool, 0);
module_param(call_bad, long, 0);
module_param(jump_null, bool, 0);
module_param(jump_bad, long, 0);
module_param(threads, int, 0);
MODULE_PARM_DESC(seed, "random seed for memory tests");
MODULE_PARM_DESC(call_panic, "test option. call panic() and render the system unusable.");
MODULE_PARM_DESC(call_bug, "test option. call BUG() and render the system unusable.");
MODULE_PARM_DESC(trap_null, "test option. dereference a NULL pointer to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(trap_read, "test option. read from an invalid address to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(trap_write, "test option. write to an invalid address to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(call_null, "test option. call a NULL pointer to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(call_read, "test option. call an invalid address to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(jump_null, "test option. jump to a NULL pointer to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(jump_read, "test option. jump to an invalid address to simulate a crash and render the system unusable.");
MODULE_PARM_DESC(threads, "number of threads to run");
MODULE_LICENSE("GPL");

#define NUM_ALLOC 24
#define NUM_SIZES 8
static int sizes[]  = { 32, 64, 128, 192, 256, 1024, 2048, 4096 };

struct mem_buf {
    char *buf;
    int size;
};

static unsigned long crasher_random(void)
{
        rand_seed = rand_seed*69069L+1;
        return rand_seed^jiffies;
}

void crasher_srandom(unsigned long entropy)
{
        rand_seed ^= entropy;
        crasher_random();
}

static char *mem_alloc(int size) {
	char *p = kmalloc(size, GFP_KERNEL);
	int i;
	if (!p)
		return p;
	for (i = 0 ; i < size; i++)
		p[i] = (i % 119) + 8;
	return p;
}

static void mem_check(char *p, int size) {
	int i;
	if (!p)
		return;
	for (i = 0 ; i < size; i++) {
        	if (p[i] != ((i % 119) + 8)) {
			printk(KERN_CRIT "verify error at %lX offset %d "
			       " wanted %d found %d size %d\n",
			       (unsigned long)(p + i), i, (i % 119) + 8,
			       p[i], size);
		}
	}
	// try and trigger slab poisoning for people using this buffer
	// wrong
	memset(p, 0, size);
}

static void mem_verify(void) {
	struct mem_buf bufs[NUM_ALLOC];
	struct mem_buf *b;
	int index;
	int size;
	unsigned long sleep;
	memset(bufs, 0, sizeof(struct mem_buf) * NUM_ALLOC);
	while(!module_exiting) {
		index = crasher_random() % NUM_ALLOC;
		b = bufs + index;
		if (b->size) {
			mem_check(b->buf, b->size);
			kfree(b->buf);
			b->buf = NULL;
			b->size = 0;
		} else {
			size = crasher_random() % NUM_SIZES;
			size = sizes[size];
			b->buf = mem_alloc(size);
			b->size = size;
		}
		sleep = crasher_random() % (HZ / 10);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(sleep);
		set_current_state(TASK_RUNNING);
	}
	for (index = 0 ; index < NUM_ALLOC ; index++) {
		b = bufs + index;
		if (b->size) {
			mem_check(b->buf, b->size);
			kfree(b->buf);
		}
	}
}

static int crasher_thread(void *unused)
{
	daemonize("crasher");
	complete(&startup);
	mem_verify();
	complete(&startup);
	return 0;
}

static int __init crasher_init(void)
{
	int i;
	init_completion(&startup);
	crasher_srandom(seed);

	if (call_panic) {
		panic("test panic from crasher module. Good Luck.\n");
		return -EFAULT;
	}
	if (call_bug) {
		printk("triggering BUG\n");
		BUG_ON(1);
		return -EFAULT;
	}

	if (trap_null) {
		volatile char *p = NULL;
		printk("dereferencing NULL pointer.\n");
		p[0] = '\n';
		return -EFAULT;
	}
	if (trap_read) {
		const volatile char *p = (char *)trap_read;
		printk("reading from invalid(?) address %p.\n", p);
		return p[0] ? -EFAULT : -EACCES;
	}
	if (trap_write) {
		volatile char *p = (char *)trap_write;
		printk("writing to invalid(?) address %p.\n", p);
		p[0] = ' ';
		return -EFAULT;
	}

	if (call_null) {
		void(*f)(void) = NULL;
		printk("calling NULL pointer.\n");
		f();
		return -EFAULT;
	}
	if (call_bad) {
		void(*f)(void) = (void(*)(void))call_bad;
		printk("calling invalid(?) address %p.\n", f);
		f();
		return -EFAULT;
	}

	/* These two depend on the compiler doing tail call optimization. */
	if (jump_null) {
		int(*f)(void) = NULL;
		printk("jumping to NULL.\n");
		return f();
	}
	if (jump_bad) {
		int(*f)(void) = (int(*)(void))jump_bad;
		printk("jumping to invalid(?) address %p.\n", f);
		return f();
	}

	printk("crasher module (%d threads).  Testing sizes: ", threads);
	for (i = 0 ; i < NUM_SIZES ; i++)
		printk("%d ", sizes[i]);
	printk("\n");

	for (i = 0 ; i < threads ; i++)
		kernel_thread(crasher_thread, crasher_thread,
			      CLONE_FS | CLONE_FILES);
	for (i = 0 ; i < threads ; i++)
		wait_for_completion(&startup);
	return 0;
}

static void __exit crasher_exit(void)
{
	int i;
	module_exiting = 1;
	for (i = 0 ; i < threads ; i++)
		wait_for_completion(&startup);
	printk("all crasher threads done\n");
	return;
}

module_init(crasher_init);
module_exit(crasher_exit);
