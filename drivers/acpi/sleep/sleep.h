
extern u8 sleep_states[];

extern acpi_status acpi_suspend (u32 state);

#ifdef CONFIG_PROC_FS
extern int acpi_sleep_proc_init(void);
#else
static inline int acpi_sleep_proc_init(void)
{
	return 0;
}
#endif
