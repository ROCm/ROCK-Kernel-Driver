// SPDX-License-Identifier: GPL-2.0
#if defined(CONFIG_RANDOMIZE_BASE) || defined(CONFIG_EFI_SECRET_KEY)

#include "../cpuflags.c"

bool has_cpuflag(int flag)
{
	get_cpuflags();

	return test_bit(flag, cpu.flags);
}

#endif
