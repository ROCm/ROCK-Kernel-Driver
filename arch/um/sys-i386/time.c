/*
 * sys-i386/time.c
 * Created 		25.9.2002	Sapan Bhatia
 *
 */

unsigned long long time_stamp(void)
{
	unsigned long low, high;

	asm("rdtsc" : "=a" (low), "=d" (high));
	return((((unsigned long long) high) << 32) + low);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
