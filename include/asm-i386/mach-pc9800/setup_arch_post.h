/**
 * machine_specific_memory_setup - Hook for machine specific memory setup.
 *
 * Description:
 *	This is included late in kernel/setup.c so that it can make
 *	use of all of the static functions.
 **/

static inline char * __init machine_specific_memory_setup(void)
{
	char *who;
	unsigned long low_mem_size, lower_high, higher_high;


	who = "BIOS (common area)";

	low_mem_size = ((*(unsigned char *)__va(PC9800SCA_BIOS_FLAG) & 7) + 1) << 17;
	add_memory_region(0, low_mem_size, 1);
	lower_high = (__u32) *(__u8 *) bus_to_virt(PC9800SCA_EXPMMSZ) << 17;
	higher_high = (__u32) *(__u16 *) bus_to_virt(PC9800SCA_MMSZ16M) << 20;
	if (lower_high != 0x00f00000UL) {
		add_memory_region(HIGH_MEMORY, lower_high, 1);
		add_memory_region(0x01000000UL, higher_high, 1);
	}
	else
		add_memory_region(HIGH_MEMORY, lower_high + higher_high, 1);

	return who;
}
