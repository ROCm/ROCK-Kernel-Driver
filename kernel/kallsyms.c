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
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/proc_fs.h>

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

/* Lookup the address for this symbol. Returns 0 if not found. */
unsigned long kallsyms_lookup_name(const char *name)
{
	char namebuf[128];
	unsigned long i;
	char *knames;

	for (i = 0, knames = kallsyms_names; i < kallsyms_num_syms; i++) {
		unsigned prefix = *knames++;

		strlcpy(namebuf + prefix, knames, 127 - prefix);
		if (strcmp(namebuf, name) == 0)
			return kallsyms_addresses[i];

		knames += strlen(knames) + 1;
	}
	return module_kallsyms_lookup_name(name);
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

		/* At worst, symbol ends at end of section. */
		if (is_kernel_inittext(addr))
			symbol_end = (unsigned long)_einittext;
		else
			symbol_end = (unsigned long)_etext;

		/* Search for next non-aliased symbol */
		for (i = best+1; i < kallsyms_num_syms; i++) {
			if (kallsyms_addresses[i] > kallsyms_addresses[best]) {
				symbol_end = kallsyms_addresses[i];
				break;
			}
		}

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

/* To avoid O(n^2) iteration, we carry prefix along. */
struct kallsym_iter
{
	loff_t pos;
	struct module *owner;
	unsigned long value;
	unsigned int nameoff; /* If iterating in core kernel symbols */
	char type;
	char name[128];
};

/* Only label it "global" if it is exported. */
static void upcase_if_global(struct kallsym_iter *iter)
{
	if (is_exported(iter->name, iter->owner))
		iter->type += 'A' - 'a';
}

static int get_ksymbol_mod(struct kallsym_iter *iter)
{
	iter->owner = module_get_kallsym(iter->pos - kallsyms_num_syms,
					 &iter->value,
					 &iter->type, iter->name);
	if (iter->owner == NULL)
		return 0;

	upcase_if_global(iter);
	return 1;
}

/* Returns space to next name. */
static unsigned long get_ksymbol_core(struct kallsym_iter *iter)
{
	unsigned stemlen, off = iter->nameoff;

	/* First char of each symbol name indicates prefix length
	   shared with previous name (stem compression). */
	stemlen = kallsyms_names[off++];

	strlcpy(iter->name+stemlen, kallsyms_names + off, 128-stemlen);
	off += strlen(kallsyms_names + off) + 1;
	iter->owner = NULL;
	iter->value = kallsyms_addresses[iter->pos];
	if (is_kernel_text(iter->value) || is_kernel_inittext(iter->value))
		iter->type = 't';
	else
		iter->type = 'd';

	upcase_if_global(iter);
	return off - iter->nameoff;
}

static void reset_iter(struct kallsym_iter *iter)
{
	iter->name[0] = '\0';
	iter->nameoff = 0;
	iter->pos = 0;
}

/* Returns false if pos at or past end of file. */
static int update_iter(struct kallsym_iter *iter, loff_t pos)
{
	/* Module symbols can be accessed randomly. */
	if (pos >= kallsyms_num_syms) {
		iter->pos = pos;
		return get_ksymbol_mod(iter);
	}
	
	/* If we're past the desired position, reset to start. */
	if (pos < iter->pos)
		reset_iter(iter);

	/* We need to iterate through the previous symbols: can be slow */
	for (; iter->pos != pos; iter->pos++) {
		iter->nameoff += get_ksymbol_core(iter);
		cond_resched();
	}
	get_ksymbol_core(iter);
	return 1;
}

static void *s_next(struct seq_file *m, void *p, loff_t *pos)
{
	(*pos)++;

	if (!update_iter(m->private, *pos))
		return NULL;
	return p;
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	if (!update_iter(m->private, *pos))
		return NULL;
	return m->private;
}

static void s_stop(struct seq_file *m, void *p)
{
}

static int s_show(struct seq_file *m, void *p)
{
	struct kallsym_iter *iter = m->private;

	/* Some debugging symbols have no name.  Ignore them. */ 
	if (!iter->name[0])
		return 0;

	if (iter->owner)
		seq_printf(m, "%0*lx %c %s\t[%s]\n",
			   (int)(2*sizeof(void*)),
			   iter->value, iter->type, iter->name,
			   module_name(iter->owner));
	else
		seq_printf(m, "%0*lx %c %s\n",
			   (int)(2*sizeof(void*)),
			   iter->value, iter->type, iter->name);
	return 0;
}

struct seq_operations kallsyms_op = {
	.start = s_start,
	.next = s_next,
	.stop = s_stop,
	.show = s_show
};

static int kallsyms_open(struct inode *inode, struct file *file)
{
	/* We keep iterator in m->private, since normal case is to
	 * s_start from where we left off, so we avoid O(N^2). */
	struct kallsym_iter *iter;
	int ret;

	iter = kmalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;
	reset_iter(iter);

	ret = seq_open(file, &kallsyms_op);
	if (ret == 0)
		((struct seq_file *)file->private_data)->private = iter;
	else
		kfree(iter);
	return ret;
}

static int kallsyms_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	kfree(m->private);
	return seq_release(inode, file);
}

static struct file_operations kallsyms_operations = {
	.open = kallsyms_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = kallsyms_release,
};

int __init kallsyms_init(void)
{
	struct proc_dir_entry *entry;

	entry = create_proc_entry("kallsyms", 0444, NULL);
	if (entry)
		entry->proc_fops = &kallsyms_operations;
	return 0;
}
__initcall(kallsyms_init);

EXPORT_SYMBOL(kallsyms_lookup);
EXPORT_SYMBOL(__print_symbol);
