/*
 * Instruction-patching support.
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/init.h>
#include <linux/string.h>

#include <asm/patch.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/unistd.h>

/*
 * This was adapted from code written by Tony Luck:
 *
 * The 64-bit value in a "movl reg=value" is scattered between the two words of the bundle
 * like this:
 *
 * 6  6         5         4         3         2         1
 * 3210987654321098765432109876543210987654321098765432109876543210
 * ABBBBBBBBBBBBBBBBBBBBBBBCCCCCCCCCCCCCCCCCCDEEEEEFFFFFFFFFGGGGGGG
 *
 * CCCCCCCCCCCCCCCCCCxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
 * xxxxAFFFFFFFFFEEEEEDxGGGGGGGxxxxxxxxxxxxxBBBBBBBBBBBBBBBBBBBBBBB
 */
static u64
get_imm64 (u64 insn_addr)
{
	u64 *p = (u64 *) (insn_addr & -16);	/* mask out slot number */

	return ( (p[1] & 0x0800000000000000UL) << 4)  | /*A*/
		((p[1] & 0x00000000007fffffUL) << 40) | /*B*/
		((p[0] & 0xffffc00000000000UL) >> 24) | /*C*/
		((p[1] & 0x0000100000000000UL) >> 23) | /*D*/
		((p[1] & 0x0003e00000000000UL) >> 29) | /*E*/
		((p[1] & 0x07fc000000000000UL) >> 43) | /*F*/
		((p[1] & 0x000007f000000000UL) >> 36);  /*G*/
}

/* Patch instruction with "val" where "mask" has 1 bits. */
void
ia64_patch (u64 insn_addr, u64 mask, u64 val)
{
	u64 m0, m1, v0, v1, b0, b1, *b = (u64 *) (insn_addr & -16);
#	define insn_mask ((1UL << 41) - 1)
	unsigned long shift;

	b0 = b[0]; b1 = b[1];
	shift = 5 + 41 * (insn_addr % 16); /* 5 bits of template, then 3 x 41-bit instructions */
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

void
ia64_patch_imm64 (u64 insn_addr, u64 val)
{
	ia64_patch(insn_addr,
		   0x01fffefe000, (  ((val & 0x8000000000000000) >> 27) /* bit 63 -> 36 */
				   | ((val & 0x0000000000200000) <<  0) /* bit 21 -> 21 */
				   | ((val & 0x00000000001f0000) <<  6) /* bit 16 -> 22 */
				   | ((val & 0x000000000000ff80) << 20) /* bit  7 -> 27 */
				   | ((val & 0x000000000000007f) << 13) /* bit  0 -> 13 */));
	ia64_patch(insn_addr - 1, 0x1ffffffffff, val >> 22);
}

void
ia64_patch_imm60 (u64 insn_addr, u64 val)
{
	ia64_patch(insn_addr,
		   0x011ffffe000, (  ((val & 0x1000000000000000) >> 24) /* bit 60 -> 36 */
				   | ((val & 0x00000000000fffff) << 13) /* bit  0 -> 13 */));
	ia64_patch(insn_addr - 1, 0x1fffffffffc, val >> 18);
}

/*
 * We need sometimes to load the physical address of a kernel
 * object.  Often we can convert the virtual address to physical
 * at execution time, but sometimes (either for performance reasons
 * or during error recovery) we cannot to this.  Patch the marked
 * bundles to load the physical address.
 * The 64-bit value in a "movl reg=value" is scattered between the
 * two words of the bundle like this:
 *
 * 6  6         5         4         3         2         1
 * 3210987654321098765432109876543210987654321098765432109876543210
 * ABBBBBBBBBBBBBBBBBBBBBBBCCCCCCCCCCCCCCCCCCDEEEEEFFFFFFFFFGGGGGGG
 *
 * CCCCCCCCCCCCCCCCCCxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
 * xxxxAFFFFFFFFFEEEEEDxGGGGGGGxxxxxxxxxxxxxBBBBBBBBBBBBBBBBBBBBBBB
 */
void __init
ia64_patch_vtop (unsigned long start, unsigned long end)
{
	s32 *offp = (s32 *) start;
	u64 ip;

	while (offp < (s32 *) end) {
		ip = (u64) offp + *offp;

		/* replace virtual address with corresponding physical address: */
		ia64_patch_imm64(ip, ia64_tpa(get_imm64(ip)));
		++offp;
	}
}

void
ia64_patch_mckinley_e9 (unsigned long start, unsigned long end)
{
	static int first_time = 1;
	int need_workaround;
	s32 *offp = (s32 *) start;
	u64 *wp;

	need_workaround = (local_cpu_data->family == 0x1f && local_cpu_data->model == 0);

	if (first_time) {
		first_time = 0;
		if (need_workaround)
			printk(KERN_INFO "Leaving McKinley Errata 9 workaround enabled\n");
		else
			printk(KERN_INFO "McKinley Errata 9 workaround not needed; "
			       "disabling it\n");
	}
	if (need_workaround)
		return;

	while (offp < (s32 *) end) {
		wp = (u64 *) ia64_imva((char *) offp + *offp);
		wp[0] = 0x0000000100000000;
		wp[1] = 0x0004000000000200;
		ia64_fc(wp);
		++offp;
	}
	ia64_insn_group_barrier();
	ia64_sync_i();
	ia64_insn_group_barrier();
	ia64_srlz_i();
	ia64_insn_group_barrier();
}

static void
patch_fsyscall_table (unsigned long start, unsigned long end)
{
	extern unsigned long fsyscall_table[NR_syscalls];
	s32 *offp = (s32 *) start;

	while (offp < (s32 *) end) {
		ia64_patch_imm64((u64) ia64_imva((char *) offp + *offp), (u64) fsyscall_table);
		++offp;
	}
}

static void
patch_brl_fsys_bubble_down (unsigned long start, unsigned long end)
{
	extern char fsys_bubble_down[];
	s32 *offp = (s32 *) start;
	u64 ip;

	while (offp < (s32 *) end) {
		ip = (u64) offp + *offp;
		ia64_patch_imm60((u64) ia64_imva((void *) ip),
				 (u64) (fsys_bubble_down - (ip & -16)) / 16);
		++offp;
	}
}

void
ia64_patch_gate (Elf64_Ehdr *ehdr)
{
	Elf64_Shdr *shdr, *strsec, *sec;
	Elf64_Half i;
	unsigned long start, end;
	char *strtab, *name;

	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 || ehdr->e_type != ET_DYN
	    || !elf_check_arch(ehdr))
		/*
		 * Without the gate shared library, we can't do signal return or fast
		 * system calls.  In other words, the kernel will crash quickly anyhow, so
		 * we might just as well do it now...
		 */
		panic("%s: gate shared library at %p not a valid ELF object!\n",
		      __FUNCTION__, ehdr);

	shdr = (void *) ((char *) ehdr + ehdr->e_shoff);
	strsec = (void *) ((char *) shdr + ehdr->e_shstrndx * ehdr->e_shentsize);
	strtab = (void *) ((char *) ehdr + strsec->sh_offset);
	for (i = 0; i < ehdr->e_shnum; ++i) {
		sec = (void *) ((char *) shdr + i * ehdr->e_shentsize);

		if (strncmp(strtab + sec->sh_name, ".data.patch.", 12) != 0)
			continue;

		if (sec->sh_size == 0)
			continue;

		name = strtab + sec->sh_name + 12;
		start = sec->sh_addr;
		end = start + sec->sh_size;
		if (strcmp(name, "fsyscall_table") == 0)
			patch_fsyscall_table(start, end);
		else if (strcmp(name, "brl_fsys_bubble_down") == 0)
			patch_brl_fsys_bubble_down(start, end);
		else if (strcmp(name, "top") == 0)
			ia64_patch_vtop(start, end);
		else if (strcmp(name, "mckinley_e9") == 0)
			ia64_patch_mckinley_e9(start, end);
		else
			panic("%s: found unknown patch-list `%s'\n", __FUNCTION__, name);
	}
}
