#include <linux/init.h>
#include <linux/console.h>

#include "chan_user.h"

/* ----------------------------------------------------------------------------- */
/* trivial console driver -- simply dump everything to stderr                    */

static void stderr_console_write(struct console *console, const char *string, 
				 unsigned len)
{
	generic_write(2 /* stderr */, string, len, NULL);
}

static struct console stderr_console = {
	.name		"stderr",
	.write		stderr_console_write,
	.flags		CON_PRINTBUFFER,
};

static int __init stderr_console_init(void)
{
	register_console(&stderr_console);
	return 0;
}
console_initcall(stderr_console_init);

