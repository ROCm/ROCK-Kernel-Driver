#include <asm/timer.h>

static int init_none(void)
{
	return 0;
}

static void mark_offset_none(void)
{
	/* nothing needed */
}

static unsigned long get_offset_none(void)
{
	return 0;
}


/* tsc timer_opts struct */
struct timer_opts timer_none = {
	.init =		init_none, 
	.mark_offset =	mark_offset_none, 
	.get_offset =	get_offset_none,
};
