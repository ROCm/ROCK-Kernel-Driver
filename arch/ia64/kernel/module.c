/*
 * IA-64-specific support for kernel module loader.
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Loosely based on patch by Rusty Russell.
 */

#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/elf.h>
#include <linux/moduleloader.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_ITANIUM
# define USE_BRL	0
#else
# define USE_BRL	1
#endif

#define ARCH_MODULE_DEBUG	0

#if ARCH_MODULE_DEBUG
# define DEBUGP printk
#else
# define DEBUGP(fmt , a...)
#endif

struct got_entry {
	uint64_t val;
};

struct fdesc {
	uint64_t ip;
	uint64_t gp;
};

/* Opaque struct for insns, to protect against derefs. */
struct insn;

static inline void *
bundle (const struct insn *insn)
{
	return (void *) ((uint64_t) insn & ~0xfUL);
}

static inline int
slot (const struct insn *insn)
{
	return (uint64_t) insn & 0x3;
}

/* Patch instruction with "val" where "mask" has 1 bits. */
static void
apply (struct insn *insn, uint64_t mask, uint64_t val)
{
	uint64_t m0, m1, v0, v1, b0, b1, *b = bundle(insn);
#	define insn_mask ((1UL << 41) - 1)
	unsigned long shift;

	b0 = b[0]; b1 = b[1];
	shift = 5 + 41 * slot(insn); /* 5 bits of template, then 3 x 41-bit instructions */
	if (shift >= 64) {
		m1 = mask << (shift - 64);
		v1 = val << (shift - 64);
	} else {
		m0 = mask << shift; m1 = mask >> (64 - shift);
		v0 = val  << shift; v1 = val >> (64 - shift);
		b[0] = (b0 & ~m0) | (v0 & m0);
	}
	b[1] = (b1 & ~m1) | (v1 & m1);
}

static int
apply_imm64 (struct insn *insn, uint64_t val)
{
	BUG_ON(slot(insn) != 2);
	apply(insn, 0x01fffefe000, (  ((val & 0x8000000000000000) >> 27) /* bit 63 -> 36 */
				    | ((val & 0x0000000000200000) <<  0) /* bit 21 -> 21 */
				    | ((val & 0x00000000001f0000) <<  6) /* bit 16 -> 22 */
				    | ((val & 0x000000000000ff80) << 20) /* bit  7 -> 27 */
				    | ((val & 0x000000000000007f) << 13) /* bit  0 -> 13 */));
	apply((void *) insn - 1, 0x1ffffffffff, val >> 22);
	return 1;
}

static int
apply_imm60 (struct insn *insn, uint64_t val)
{
	BUG_ON(slot(insn) != 2);
	apply(insn, 0x011ffffe000, (  ((val & 0x1000000000000000) >> 24) /* bit 60 -> 36 */
				    | ((val & 0x00000000000fffff) << 13) /* bit  0 -> 13 */));
	apply((void *) insn - 1, 0x1fffffffffc, val >> 18);
	return 1;
}

static int
apply_imm22 (struct module *mod, struct insn *insn, uint64_t val)
{
	if (val + (1 << 21) >= (1 << 22)) {
		printk(KERN_ERR "%s: value %li out of range\n", mod->name, (int64_t)val);
		return 0;
	}
	apply(insn, 0x01fffcfe000, (  ((val & 0x200000) << 15) /* bit 21 -> 36 */
				    | ((val & 0x1f0000)	<<  6) /* bit 16 -> 22 */
				    | ((val & 0x00ff80) << 20) /* bit  7 -> 27 */
				    | ((val & 0x00007f)	<< 13) /* bit  0 -> 13 */));
	return 1;
}

static int
apply_imm21b (struct module *mod, struct insn *insn, uint64_t val)
{
	if (val + (1 << 20) >= (1 << 21)) {
		printk(KERN_ERR "%s: value %li out of range\n", mod->name, (int64_t)val);
		return 0;
	}
	apply(insn, 0x11ffffe000, (  ((val & 0x100000) << 16) /* bit 20 -> 36 */
				   | ((val & 0x0fffff) << 13) /* bit  0 -> 13 */));
	return 1;
}

#if USE_BRL

struct plt_entry {
	/* Three instruction bundles in PLT. */
 	unsigned char bundle[2][16];
};

static const struct plt_entry ia64_plt_template = {
	{
		{
			0x04, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MLX] nop.m 0 */
			0x00, 0x00, 0x00, 0x00, 0x00, 0x20, /*	     movl gp=TARGET_GP */
			0x00, 0x00, 0x00, 0x60
		},
		{
			0x05, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MLX] nop.m 0 */
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*	     brl.many gp=TARGET_GP */
			0x08, 0x00, 0x00, 0xc0
		}
	}
};

static void
patch_plt (struct plt_entry *plt, long target_ip, unsigned long target_gp)
{
	apply_imm64((struct insn *) (plt->bundle[0] + 2), target_gp);
	apply_imm60((struct insn *) (plt->bundle[1] + 2),
		    (target_ip - (long) plt->bundle[1]) / 16);
}

unsigned long
plt_target (struct plt_entry *plt)
{
	uint64_t b0, b1, *b = (uint64_t *) plt->bundle[1];
	long off;

	b0 = b[0]; b1 = b[1];
	off = (  ((b1 & 0x00fffff000000000) >> 36)		/* imm20b -> bit 0 */
	       | ((b0 >> 48) << 20) | ((b1 & 0x7fffff) << 36)	/* imm39 -> bit 20 */
	       | ((b1 & 0x0800000000000000) << 1));		/* i -> bit 60 */
	return (long) plt->bundle[1] + 16*off;
}

#else /* !USE_BRL */

struct plt_entry {
	/* Three instruction bundles in PLT. */
 	unsigned char bundle[3][16];
};

static const struct plt_entry ia64_plt_template = {
	{
		{
			0x05, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MLX] nop.m 0 */
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*	     movl r16=TARGET_IP */
			0x02, 0x00, 0x00, 0x60
		},
		{
			0x04, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MLX] nop.m 0 */
			0x00, 0x00, 0x00, 0x00, 0x00, 0x20, /*	     movl gp=TARGET_GP */
			0x00, 0x00, 0x00, 0x60
		},
		{
			0x11, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MIB] nop.m 0 */
			0x60, 0x80, 0x04, 0x80, 0x03, 0x00, /*	     mov b6=r16 */
			0x60, 0x00, 0x80, 0x00		    /*	     br.few b6 */
		}
	}
};

static void
patch_plt (struct plt_entry *plt, unsigned long target_ip, unsigned long target_gp)
{
	apply_imm64((struct insn *) (plt->bundle[0] + 2), target_ip);
	apply_imm64((struct insn *) (plt->bundle[1] + 2), target_gp);
}

unsigned long
plt_target (struct plt_entry *plt)
{
	uint64_t b0, b1, *b = (uint64_t *) plt->bundle[0];

	b0 = b[0]; b1 = b[1];
	return (  ((b1 & 0x000007f000000000) >> 36)		/* imm7b -> bit 0 */
		| ((b1 & 0x07fc000000000000) >> 43)		/* imm9d -> bit 7 */
		| ((b1 & 0x0003e00000000000) >> 29)		/* imm5c -> bit 16 */
		| ((b1 & 0x0000100000000000) >> 23)		/* ic -> bit 21 */
		| ((b0 >> 46) << 22) | ((b1 & 0x7fffff) << 40)	/* imm41 -> bit 22 */
		| ((b1 & 0x0800000000000000) <<  4));		/* i -> bit 63 */
}

#endif /* !USE_BRL */

void *
module_alloc (unsigned long size)
{
	if (!size)
		return NULL;
	return vmalloc(size);
}

void
module_free (struct module *mod, void *module_region)
{
	vfree(module_region);
}

/* Have we already seen one of these relocations? */
/* FIXME: we could look in other sections, too --RR */
static int
duplicate_reloc (const Elf64_Rela *rela, unsigned int num)
{
	unsigned int i;

	for (i = 0; i < num; i++) {
		if (rela[i].r_info == rela[num].r_info && rela[i].r_addend == rela[num].r_addend)
			return 1;
	}
	return 0;
}

/* Count how many GOT entries we may need */
static unsigned int
count_gots (const Elf64_Rela *rela, unsigned int num)
{
	unsigned int i, ret = 0;

	/* Sure, this is order(n^2), but it's usually short, and not
           time critical */
	for (i = 0; i < num; i++) {
		switch (ELF64_R_TYPE(rela[i].r_info)) {
		      case R_IA64_LTOFF22:
			if (!duplicate_reloc(rela, i))
				ret++;
			break;
		}
	}
	return ret;
}

/* Count how many PLT entries we may need */
static unsigned int
count_plts (const Elf64_Rela *rela, unsigned int num)
{
	unsigned int i, ret = 0;

	/* Sure, this is order(n^2), but it's usually short, and not
           time critical */
	for (i = 0; i < num; i++) {
		switch (ELF64_R_TYPE(rela[i].r_info)) {
		      case R_IA64_PCREL21B:
			if (!duplicate_reloc(rela, i))
				ret++;
			break;
		}
	}
	return ret;
}

/* We need to create an function-descriptors for any internal function
   which is referenced. */
static unsigned int
count_fdescs (const Elf64_Rela *rela, unsigned int num)
{
	unsigned int i, ret = 0;

	/* Sure, this is order(n^2), but it's usually short, and not time critical.  */
	for (i = 0; i < num; i++) {
		switch (ELF64_R_TYPE(rela[i].r_info)) {
		      case R_IA64_FPTR64LSB:
			/*
			 * Jumps to static functions sometimes go straight to their
			 * offset.  Of course, that may not be possible if the jump is
			 * from init -> core or vice. versa, so we need to generate an
			 * FDESC (and PLT etc) for that.
			 */
		      case R_IA64_PCREL21B:
			if (!duplicate_reloc(rela, i))
				ret++;
			break;
		}
	}
	return ret;
}

int
module_frob_arch_sections (Elf_Ehdr *ehdr, Elf_Shdr *sechdrs, char *secstrings,
			   struct module *mod)
{
	unsigned long core_plts = 0, init_plts = 0, gots = 0, fdescs = 0;
	Elf64_Shdr *s, *sechdrs_end = sechdrs + ehdr->e_shnum;
	Elf64_Shdr *core_text = NULL, *init_text = NULL;
	size_t size;

	/*
	 * To store the PLTs and function-descriptors, we expand the .text section for
	 * core module-code and the .init.text section for initialization code.
	 */
	for (s = sechdrs; s < sechdrs_end; ++s)
		if (strcmp(".text", secstrings + s->sh_name) == 0)
			core_text = mod->arch.core_text_sec = s;
		else if (strcmp(".init.text", secstrings + s->sh_name) == 0)
			init_text = mod->arch.init_text_sec = s;

	/* GOT and PLTs can occur in any relocated section... */
	for (s = sechdrs + 1; s < sechdrs_end; ++s) {
		const Elf64_Rela *rels = (void *)ehdr + s->sh_offset;
		unsigned long numrels = s->sh_size/sizeof(Elf64_Rela);

		if (s->sh_type != SHT_RELA)
			continue;

		gots += count_gots(rels, numrels);
		fdescs += count_fdescs(rels, numrels);
		if (strstr(secstrings + s->sh_name, ".init"))
			init_plts += count_plts(rels, numrels);
		else
			core_plts += count_plts(rels, numrels);
	}

	if ((core_plts | fdescs | gots) > 0 && !core_text) {
		printk(KERN_ERR "module %s: no .text section\n", mod->name);
		return -ENOEXEC;
	}
	if (init_plts > 0 && !init_text) {
		printk(KERN_ERR "module %s: no .init.text section\n", mod->name);
		return -ENOEXEC;
	}

	/*
	 * Note: text sections must be at leasts 16-byte aligned, so we should be fine
	 * here without any extra work.
	 */
	size = core_text->sh_size;
	mod->arch.core_plt_offset = size;	size += core_plts * sizeof(struct plt_entry);
	mod->arch.fdesc_offset = size;		size += fdescs * sizeof(struct fdesc);
	mod->arch.got_offset = size;		size += gots * sizeof(struct got_entry);
	core_text->sh_size = size;
	DEBUGP("%s: core: sz=%lu, plt=+%lx, fdesc=+%lx, got=+%lx\n",
	       __FUNCTION__, size, mod->arch.core_plt_offset, mod->arch.fdesc_offset,
	       mod->arch.got_offset);

	size = init_text->sh_size;
	mod->arch.init_plt_offset = size;	size += init_plts * sizeof(struct plt_entry);
	init_text->sh_size = size;
	DEBUGP("%s: init: sz=%lu, plt=+%lx\n", __FUNCTION__, size, mod->arch.init_plt_offset);

	return 0;
}

static inline int
in_init (const struct module *mod, void *addr)
{
	return (uint64_t) (addr - mod->module_init) < mod->init_size;
}

static inline int
in_core (const struct module *mod, void *addr)
{
	return (uint64_t) (addr - mod->module_core) < mod->core_size;
}

/*
 * Get gp-relative GOT entry for this value (gp points to start of GOT).  Returns -1 on
 * failure.
 */
static uint64_t
get_got (struct module *mod, uint64_t value)
{
	struct got_entry *got;
	unsigned int i;

	if (value == 0) {
		printk(KERN_ERR "%s: zero value in GOT\n", mod->name);
		return (uint64_t) -1;
	}

	got = (void *) (mod->arch.core_text_sec->sh_addr + mod->arch.got_offset);
	for (i = 0; got[i].val; i++)
		if (got[i].val == value)
			return i * sizeof(struct got_entry);

	/* Not enough GOT entries? */
	if (got + i >= (struct got_entry *) (mod->arch.core_text_sec->sh_addr
					     + mod->arch.core_text_sec->sh_size))
		BUG();

	got[i].val = value;
	return i * sizeof(struct got_entry);
}

/* Get PC-relative PLT entry for this value.  Returns 0 on failure. */
static uint64_t
get_plt (struct module *mod, const struct insn *insn, uint64_t value)
{
	struct plt_entry *plt, *plt_end;
	uint64_t target_ip, target_gp;

	if (in_init(mod, (void *) insn)) {
		plt = (void *) (mod->arch.init_text_sec->sh_addr + mod->arch.init_plt_offset);
		plt_end = (void *) plt + mod->arch.init_text_sec->sh_size;
	} else {
		plt = (void *) (mod->arch.core_text_sec->sh_addr + mod->arch.core_plt_offset);
		plt_end = (void *) (mod->arch.core_text_sec->sh_addr + mod->arch.fdesc_offset);
	}

	/* "value" is a pointer to a function-descriptor; fetch the target ip/gp from it: */
	target_ip = ((uint64_t *) value)[0];
	target_gp = ((uint64_t *) value)[1];

	/* Look for existing PLT entry. */
	while (plt->bundle[0][0]) {
		if (plt_target(plt) == target_ip)
			goto found;
		if (++plt >= plt_end)
			BUG();
	}
	*plt = ia64_plt_template;
	patch_plt(plt, target_ip, target_gp);
#if ARCH_MODULE_DEBUG
	if (plt_target(plt) != target_ip)
		printk("%s: mistargeted PLT: wanted %lx, got %lx\n",
		       __FUNCTION__, target_ip, plt_target(plt));
#endif
  found:
	return (uint64_t) plt;
}

/* Get function descriptor for this function.  Returns 0 on failure. */
static uint64_t
get_fdesc (struct module *mod, uint64_t value)
{
	struct fdesc *fdesc = (void *) (mod->arch.core_text_sec->sh_addr + mod->arch.fdesc_offset);

	if (!value) {
		printk(KERN_ERR "%s: fdesc for zero requested!\n", mod->name);
		return 0;
	}

	/* Look for existing function descriptor. */
	while (fdesc->ip) {
		if (fdesc->ip == value)
			return (uint64_t)fdesc;
		if ((uint64_t) ++fdesc >= (mod->arch.core_text_sec->sh_addr
					   + mod->arch.core_text_sec->sh_size))
			BUG();
	}

	/* Create new one */
	fdesc->ip = value;
	fdesc->gp = mod->arch.core_text_sec->sh_addr + mod->arch.got_offset;
	return (uint64_t)fdesc;
}

static inline int
is_internal (const struct module *mod, uint64_t value)
{
	return in_init(mod, (void *)value) || in_core(mod, (void *)value);
}

int
apply_relocate_add (Elf64_Shdr *sechdrs, const char *strtab, unsigned int symindex,
		    unsigned int relsec, struct module *mod)
{
	unsigned int i, n = sechdrs[relsec].sh_size / sizeof(Elf64_Rela);
	Elf64_Rela *rela = (void *) sechdrs[relsec].sh_addr;
	Elf64_Sym *symtab, *sym;
	Elf64_Shdr *target_sec;
	void *base, *location;
	uint64_t value;

	DEBUGP("apply_relocate_add: applying section %u (%u relocs) to %u\n",
	       relsec, n, sechdrs[relsec].sh_info);

	target_sec = sechdrs + sechdrs[relsec].sh_info;

	if (target_sec->sh_entsize == ~0UL)
		/*
		 * If target section wasn't allocated, we don't need to relocate it.
		 * Happens, e.g., for debug sections.
		 */
		return 0;

	base = (void *) target_sec->sh_addr;
	symtab = (Elf64_Sym *) sechdrs[symindex].sh_addr;
	DEBUGP("base=%p, symtab=%p\n", base, symtab);

	for (i = 0; i < n; i++) {
		/* This is where to make the change */
		location = base + rela[i].r_offset;

		/* This is the symbol it is referring to */
		sym = symtab + ELF64_R_SYM(rela[i].r_info);
		if (!sym->st_value)
			return -ENOENT;

		/* `Everything is relative'. */
		value = sym->st_value + rela[i].r_addend;

		DEBUGP("loc=%p, val=0x%lx, sym=%p: ", location, value, sym);

		switch (ELF64_R_TYPE(rela[i].r_info)) {
		      case R_IA64_LTOFF22:
			value = get_got(mod, value);
			DEBUGP("LTOFF22 0x%lx\n", value);
			if (value == (uint64_t)-1 || !apply_imm22(mod, location, value))
				return -ENOEXEC;
			break;

		      case R_IA64_PCREL21B:
			if (!is_internal(mod, value))
				value = get_plt(mod, location, value);
			DEBUGP("PCREL21B 0x%lx\n", value);
			value -= (uint64_t) bundle(location);
			if (!apply_imm21b(mod, location, (long) value / 16))
				return -ENOEXEC;
			break;

		      case R_IA64_DIR32LSB:
			DEBUGP("DIR32LSB 0x%lx\n", value);
			*((uint32_t *)location) = value;
			break;

		      case R_IA64_SEGREL64LSB:
			/* My definition of "segment" is a little fuzzy here, but quoth
			   David Mosberger-Tang:

			   As long as it's used consistently, it doesn't matter much.  All
			   unwind offsets are 64-bit offsets anyhow, so you could just use
			   a segment base of zero (the reason SEGREL relocs are used is to
			   make it possible to have the unwind tables be read-only in
			   shared libraries, but since you need to do relocation anyhow,
			   that's a moot issue). */
			/* Fall thru */
		      case R_IA64_DIR64LSB:
			DEBUGP("DIR64LSB 0x%lx\n", value);
			*((uint64_t *)location) = value;
			break;

		      case R_IA64_FPTR64LSB:
			/* Create a function descriptor for internal functions only. */
			if (is_internal(mod, value))
				value = get_fdesc(mod, value);
			DEBUGP("FPTR64LSB 0x%lx\n", value);
			*((uint64_t *)location) = value;
			break;

		      case R_IA64_GPREL22:
			value -= mod->arch.core_text_sec->sh_addr + mod->arch.got_offset;
			DEBUGP("GPREL22 0x%lx\n", value);
			if (!apply_imm22(mod, location, value))
				return -ENOEXEC;
			break;

		      case R_IA64_LTOFF_FPTR22:
			DEBUGP("LTOFF_FPTR22: orig=%lx, ", value);
			if (is_internal(mod, value))
				value = get_fdesc(mod, value);
			DEBUGP("fdesc=%lx, ", value);
			value = get_got(mod, value);
			DEBUGP("got=%lx\n", value);
			if (value == (uint64_t) -1 || !apply_imm22(mod, location, value))
				return -ENOEXEC;
			break;

		      case R_IA64_SECREL32LSB:
			DEBUGP("SECREL32LSB: orig=0x%lx, val=0x%lx\n",
			       value, value - (uint64_t) base);
			*(uint32_t *) location = value - (uint64_t) base;
			break;

		      default:
			printk(KERN_ERR "%s: Unknown RELA relocation: %lu\n",
			       mod->name, ELF64_R_TYPE(rela[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}

int
apply_relocate (Elf64_Shdr *sechdrs, const char *strtab, unsigned int symindex,
		unsigned int relsec, struct module *mod)
{
	printk(KERN_ERR "module %s: REL relocs unsupported\n", mod->name);
	return -ENOEXEC;
}

int
module_finalize (const Elf_Ehdr *hdr, const Elf_Shdr *sechdrs, struct module *mod)
{
	DEBUGP("%s: init: entry=%p\n", __FUNCTION__, mod->init);
	return 0;
}
