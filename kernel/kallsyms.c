/*
 * kallsyms.c: in-kernel printing of symbolic oopses and stack traces.
 *
 * Rewritten and vastly simplified by Rusty Russell for in-kernel
 * module loader:
 *   Copyright 2002 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 * Stem compression by Andi Kleen.
 */
#include <linux/kallsyms.h>
#include <linux/module.h>

/* These will be re-linked against their real values during the second link stage */
extern unsigned long kallsyms_addresses[] __attribute__((weak));
extern unsigned long kallsyms_num_syms __attribute__((weak));
extern char kallsyms_names[] __attribute__((weak));

/* Defined by the linker script. */
extern char _stext[], _etext[], _sinittext[], _einittext[];

static inline int is_kernel_inittext(unsigned long addr)
{
	if (addr >= (unsigned long)_sinittext
	    && addr <= (unsigned long)_einittext)
		return 1;
	return 0;
}

static inline int is_kernel_text(unsigned long addr)
{
	if (addr >= (unsigned long)_stext && addr <= (unsigned long)_etext)
		return 1;
	return 0;
}

/* Lookup an address.  modname is set to NULL if it's in the kernel. */
const char *kallsyms_lookup(unsigned long addr,
			    unsigned long *symbolsize,
			    unsigned long *offset,
			    char **modname, char *namebuf)
{
	unsigned long i, best = 0;

	/* This kernel should never had been booted. */
	BUG_ON(!kallsyms_addresses);

	namebuf[127] = 0;
	namebuf[0] = 0;

	if (is_kernel_text(addr) || is_kernel_inittext(addr)) {
		unsigned long symbol_end;
		char *name = kallsyms_names;

		/* They're sorted, we could be clever here, but who cares? */
		for (i = 0; i < kallsyms_num_syms; i++) {
			if (kallsyms_addresses[i] > kallsyms_addresses[best] &&
			    kallsyms_addresses[i] <= addr)
				best = i;
		}

		/* Grab name */
		for (i = 0; i <= best; i++) { 
			unsigned prefix = *name++;
			strncpy(namebuf + prefix, name, 127 - prefix);
			name += strlen(name) + 1;
		}

		/* Base symbol size on next symbol. */
		if (best + 1 < kallsyms_num_syms)
			symbol_end = kallsyms_addresses[best + 1];
		else if (is_kernel_inittext(addr))
			symbol_end = (unsigned long)_einittext;
		else
			symbol_end = (unsigned long)_etext;

		*symbolsize = symbol_end - kallsyms_addresses[best];
		*modname = NULL;
		*offset = addr - kallsyms_addresses[best];
		return namebuf;
	}

	return module_address_lookup(addr, symbolsize, offset, modname);
}

/* Replace "%s" in format with address, or returns -errno. */
void __print_symbol(const char *fmt, unsigned long address)
{
	char *modname;
	const char *name;
	unsigned long offset, size;
	char namebuf[128];

	name = kallsyms_lookup(address, &size, &offset, &modname, namebuf);

	if (!name) {
		char addrstr[sizeof("0x%lx") + (BITS_PER_LONG*3/10)];

		sprintf(addrstr, "0x%lx", address);
		printk(fmt, addrstr);
		return;
	}

	if (modname) {
		/* This is pretty small. */
		char buffer[sizeof("%s+%#lx/%#lx [%s]")
			   + strlen(name) + 2*(BITS_PER_LONG*3/10)
			   + strlen(modname)];

		sprintf(buffer, "%s+%#lx/%#lx [%s]",
			name, offset, size, modname);
		printk(fmt, buffer);
	} else {
		char buffer[sizeof("%s+%#lx/%#lx")
			   + strlen(name) + 2*(BITS_PER_LONG*3/10)];

		sprintf(buffer, "%s+%#lx/%#lx", name, offset, size);
		printk(fmt, buffer);
	}
}

EXPORT_SYMBOL(kallsyms_lookup);
EXPORT_SYMBOL(__print_symbol);
