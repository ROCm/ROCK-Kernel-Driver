/*
 * allow user mode linux kernels dump core on kernel panics,
 * for later analysis of the crash.
 *
 * (c) 2004 Gerd Knorr + D. Bahi
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/notifier.h>

#include "init.h"
#include "os.h"

static int core_on_panic_func(struct notifier_block *self, unsigned long unused1,
		              void *unused2)
{
	static int recurse = 0;

        /* cleanup so we have less to cleanup [linux] on the host */
	/* could panic in uml_cleanup though so we need a check */
        if (0 == recurse++)
		uml_cleanup();

	/* to prevent keyboard from causing terminal to spew during dump */
	block_signals();

	/* actually dump ... */
	abort();

	/* not reached */
	return 0;
}

static struct notifier_block core_on_panic_notifier = {
	.notifier_call 		= core_on_panic_func,
	.priority 		= 1,
};

static int core_on_panic = 0;

static int __init core_on_panic_setup(char *str)
{
	core_on_panic = 1;
	return 0;
}

static int __init core_on_panic_init(void)
{
	if (core_on_panic)
		notifier_chain_register(&panic_notifier_list, &core_on_panic_notifier);
	return 0;
}

__initcall(core_on_panic_init);
__setup("coreonpanic", core_on_panic_setup);
__uml_help(core_on_panic_setup,
	"coreonpanic\n"
	"    This flag make it so that UML will dump core on a kernel panic or segfault.\n"
	"    Shell environment restrictions on cores (limit or ulimit) still apply.\n"
	"\n"
	"    Beware that your UML will not reboot with this flag.\n"
	"\n"
);

