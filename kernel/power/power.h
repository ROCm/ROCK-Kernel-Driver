

/* With SUSPEND_CONSOLE defined, it suspend looks *really* cool, but
   we probably do not take enough locks for switching consoles, etc,
   so bad things might happen.
*/
#if defined(CONFIG_VT) && defined(CONFIG_VT_CONSOLE)
#define SUSPEND_CONSOLE	(MAX_NR_CONSOLES-1)
#endif


#ifdef CONFIG_SOFTWARE_SUSPEND
extern int swsusp_save(void);
extern int swsusp_write(void);
extern int swsusp_read(void);
extern int swsusp_restore(void);
extern int swsusp_free(void);
#else
static inline int swsusp_save(void) 
{
	return 0;
}
static inline int swsusp_write(void)
{
	return 0;
}
static inline int swsusp_read(void)
{
	return 0;
}
static inline int swsusp_restore(void)
{
	return 0;
}
static inline int swsusp_free(void)
{
	return 0;
}
#endif
