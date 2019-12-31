#include <asm/io.h>
#include <asm/archrandom.h>

#define I8254_PORT_CONTROL	0x43
#define I8254_PORT_COUNTER0	0x40
#define I8254_CMD_READBACK	0xC0
#define I8254_SELECT_COUNTER0	0x02
#define I8254_STATUS_NOTREADY	0x40
static inline u16 i8254(void)
{
	u16 status, timer;

	do {
		outb(I8254_CMD_READBACK | I8254_SELECT_COUNTER0,
		     I8254_PORT_CONTROL);
		status = inb(I8254_PORT_COUNTER0);
		timer  = inb(I8254_PORT_COUNTER0);
		timer |= inb(I8254_PORT_COUNTER0) << 8;
	} while (status & I8254_STATUS_NOTREADY);

	return timer;
}

static unsigned long get_random_long(const char *purpose)
{
#ifdef CONFIG_X86_64
	const unsigned long mix_const = 0x5d6008cbf3848dd3UL;
#else
	const unsigned long mix_const = 0x3f39e593UL;
#endif
	unsigned long raw, random = get_boot_seed();
	bool use_i8254 = true;

	debug_putstr(purpose);

	if (has_cpuflag(X86_FEATURE_RDRAND)) {
		debug_putstr(" RDRAND");
		if (rdrand_long(&raw)) {
			random ^= raw;
			use_i8254 = false;
		}
	}

	if (has_cpuflag(X86_FEATURE_TSC)) {
		debug_putstr(" RDTSC");
		raw = rdtsc();

		random ^= raw;
		use_i8254 = false;
	}

	if (use_i8254) {
		debug_putstr(" i8254");
		random ^= i8254();
	}

	/* Circular multiply for better bit diffusion */
	asm(_ASM_MUL "%3"
	    : "=a" (random), "=d" (raw)
	    : "a" (random), "rm" (mix_const));
	random += raw;

	debug_putstr("...\n");

	return random;
}
