/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Author: Vamsi Krishna S. <vamsi_krishna@in.ibm.com>
 * (C) 2003 IBM Corporation.
 */

#include <linux/types.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>

#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/desc.h>
#include <asm/debugreg.h>
#if 0
#include <asm/pgtable.h>
#endif

MODULE_AUTHOR("Vamsi Krishna S./IBM");
MODULE_DESCRIPTION("x86 specific information (gdt/idt/ldt/page tables)");
MODULE_LICENSE("GPL");

typedef struct _kdb_desc {
	unsigned short limit;
	unsigned short base;
	unsigned char base_h1;
	unsigned char type:4;
	unsigned char seg:1;
	unsigned char dpl:2;
	unsigned char present:1;
	unsigned char limit_h:4;
	unsigned char avl:2;
	unsigned char db:1;
	unsigned char g:1; /* granularity */
	unsigned char base_h2;
} kdb_desc_t;

typedef struct _kdb_gate_desc {
	unsigned short offset;
	unsigned short sel;
	unsigned char res;
	unsigned char type:4;
	unsigned char seg:1;
	unsigned char dpl:2;
	unsigned char present:1;
	unsigned short offset_h;
} kdb_gate_desc_t;

#define KDB_SEL_MAX 			0x2000
#define KDB_IDT_MAX 			0x100
#define KDB_SYS_DESC_TYPE_TSS		0x01
#define KDB_SYS_DESC_TYPE_LDT		0x02
#define KDB_SYS_DESC_TYPE_TSSB		0x03
#define KDB_SYS_DESC_TYPE_CALLG		0x04
#define KDB_SYS_DESC_TYPE_TASKG		0x05
#define KDB_SYS_DESC_TYPE_INTG		0x06
#define KDB_SYS_DESC_TYPE_TRAPG		0x07

#define KDB_SYS_DESC_TYPE_TSS32 	0x09
#define KDB_SYS_DESC_TYPE_TSS32B	0x0b
#define KDB_SYS_DESC_TYPE_CALLG32	0x0c
#define KDB_SYS_DESC_TYPE_INTG32	0x0e
#define KDB_SYS_DESC_TYPE_TRAPG32	0x0f

#define KDB_SYS_DESC_OFFSET(d) ((unsigned long)(d->offset_h << 16 | d->offset))
#define KDB_SYS_DESC_CALLG_COUNT(d) ((unsigned int)(d->res & 0x0F))

#define KDB_SEG_DESC_TYPE_CODE		0x08
#define KDB_SEG_DESC_TYPE_CODE_R	0x02
#define KDB_SEG_DESC_TYPE_DATA_W	0x02
#define KDB_SEG_DESC_TYPE_CODE_C	0x02    /* conforming */
#define KDB_SEG_DESC_TYPE_DATA_D	0x02    /* expand-down */
#define KDB_SEG_DESC_TYPE_A		0x01	/* accessed */

#define KDB_SEG_DESC_BASE(d) ((unsigned long)(d->base_h2 << 24 | d->base_h1 << 16 | d->base))
#define _LIMIT(d) ((unsigned long)(d->limit_h << 16 | d->limit))
#define KDB_SEG_DESC_LIMIT(d) (d->g ? ((_LIMIT(d)+1) << 12) -1 : _LIMIT(d))

/* helper functions to display system registers in verbose mode */
static void display_gdtr(void)
{
	struct Xgt_desc_struct gdtr;

	__asm__ __volatile__ ("sgdt %0\n\t" : "=m"(gdtr));
	kdb_printf("gdtr.address = 0x%8.8lx, gdtr.size = 0x%x\n", gdtr.address, gdtr.size);

	return;
}

static void display_ldtr(void)
{
	struct Xgt_desc_struct gdtr;
	unsigned long ldtr;

	__asm__ __volatile__ ("sgdt %0\n\t" : "=m"(gdtr));
	__asm__ __volatile__ ("sldt %0\n\t" : "=m"(ldtr));

	kdb_printf("ldtr = 0x%8.8lx ", ldtr);

	if (ldtr < gdtr.size) {
		kdb_desc_t *ldt_desc = (kdb_desc_t *)(gdtr.address + (ldtr & ~7));
		kdb_printf("base=0x%8.8lx, limit=0x%8.8lx\n", KDB_SEG_DESC_BASE(ldt_desc),
				KDB_SEG_DESC_LIMIT(ldt_desc));
	} else {
		kdb_printf("invalid\n");
	}

	return;
}

static void display_idtr(void)
{
	struct Xgt_desc_struct idtr;
	__asm__ __volatile__ ("sidt %0\n\t" : "=m"(idtr));
	kdb_printf("idtr.address = 0x%8.8lx, idtr.size = 0x%x\n", idtr.address, idtr.size);
	return;
}

static char *cr0_flags[] = {
	"pe", "mp", "em", "ts", "et", "ne", NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	"wp", NULL, "am", NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, "nw", "cd", "pg"};

static void display_cr0(void)
{
	kdb_machreg_t cr0;
	int i;
	__asm__ ("movl %%cr0,%0\n\t":"=r"(cr0));
	kdb_printf("cr0=0x%08lx ", cr0);
	for (i = 0; i < 32; i++) {
		if (test_bit(i, &cr0) && cr0_flags[i])
			kdb_printf("%s ", cr0_flags[i]);
	}
	kdb_printf("\n");
	return;
}

static void display_cr3(void)
{
	kdb_machreg_t cr3;
	__asm__ ("movl %%cr3,%0\n\t":"=r"(cr3));
	kdb_printf("cr3 = 0x%08lx ", cr3);
	if (cr3 & 0x08)
		kdb_printf("pwt ");
	if (cr3 & 0x10)
		kdb_printf("pcd ");
	kdb_printf("pgdir=%8.8lx\n", cr3 & PAGE_MASK);
	return;
}

static char *cr4_flags[] = {
	"vme", "pvi", "tsd", "de", "pse", "pae", "mce", "pge", "pce"};

static void display_cr4(void)
{
	kdb_machreg_t cr4;
	int i;
	__asm__ ("movl %%cr4,%0\n\t":"=r"(cr4));
	kdb_printf("cr4 = 0x%08lx ", cr4);
	for (i = 0; i < 9; i++) {
		if (test_bit(i, &cr4))
			kdb_printf("%s ", cr4_flags[i]);
	}
	kdb_printf("\n");
	return;
}

static char *dr_type_name[] = { "exec", "write", "io", "rw" };

static void display_dr_status(int nr, int enabled, int local, int len, int type)
{
	if (!enabled) {
		kdb_printf("\tdebug register %d: not enabled\n", nr);
		return;
	}

	kdb_printf("\tdebug register %d: %s, len = %d, type = %s\n",
			nr,
			local? " local":"global",
			len,
			dr_type_name[type]);
}

static void display_dr(void)
{
	kdb_machreg_t dr0, dr1, dr2, dr3, dr6, dr7;
	int dbnr, set;

	__asm__ ("movl %%db0,%0\n\t":"=r"(dr0));
	__asm__ ("movl %%db1,%0\n\t":"=r"(dr1));
	__asm__ ("movl %%db2,%0\n\t":"=r"(dr2));
	__asm__ ("movl %%db3,%0\n\t":"=r"(dr3));
	__asm__ ("movl %%db6,%0\n\t":"=r"(dr6));
	__asm__ ("movl %%db7,%0\n\t":"=r"(dr7));

	kdb_printf("dr0 = 0x%08lx dr1 = 0x%08lx dr2 = 0x%08lx dr3 = 0x%08lx\n",
		   dr0, dr1, dr2, dr3);
	kdb_printf("dr6 = 0x%08lx ", dr6);
	dbnr = dr6 & DR6_DR_MASK;
	if (dbnr) {
		int nr;
		switch(dbnr) {
		case 1:
			nr = 0; break;
		case 2:
			nr = 1; break;
		case 4:
			nr = 2; break;
		default:
			nr = 3; break;
		}
		kdb_printf("debug register hit = %d", nr);
	} else if (dr6 & DR_STEP) {
		kdb_printf("single step");
	} else if (dr6 & DR_SWITCH) {
		kdb_printf("task switch");
	}
	kdb_printf("\n");

	kdb_printf("dr7 = 0x%08lx\n", dr7);
	set = DR7_L0(dr7) || DR7_G0(dr7);
	display_dr_status(0, set, DR7_L0(dr7), DR7_LEN0(dr7), DR7_RW0(dr7));
	set = DR7_L1(dr7) || DR7_G1(dr7);
	display_dr_status(1, set, DR7_L1(dr7), DR7_LEN1(dr7), DR7_RW1(dr7));
	set = DR7_L2(dr7) || DR7_G2(dr7);
	display_dr_status(2, set, DR7_L2(dr7), DR7_LEN2(dr7), DR7_RW2(dr7));
	set = DR7_L3(dr7) || DR7_G3(dr7);
	display_dr_status(3, set, DR7_L3(dr7), DR7_LEN3(dr7), DR7_RW3(dr7));
}

static char *set_eflags[] = {
	"carry", NULL, "parity",  NULL, "adjust",  NULL, "zero", "sign",
	"trace", "intr-on", "dir", "overflow",  NULL, NULL, "nestedtask", NULL,
	"resume", "vm", "align", "vif", "vip", "id", NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

static void display_eflags(unsigned long ef)
{
	int i, iopl;
	kdb_printf("eflags = 0x%08lx ", ef);
	for (i = 0; i < 22; i++) {
		if (test_bit(i, &ef) && set_eflags[i])
			kdb_printf("%s ", set_eflags[i]);
	}

	iopl = ((unsigned long)(ef & 0x00003000)) >> 12;
	kdb_printf("iopl=%c\n", '0'+iopl);
	return;
}

static void display_tss(struct tss_struct *t)
{
	kdb_printf("    cs = %04x,  eip = 0x%8.8lx\n", t->es, t->eip);
	kdb_printf("    ss = %04x,  esp = 0x%8.8lx\n", t->ss, t->esp);
	kdb_printf("   ss0 = %04x, esp0 = 0x%8.8lx\n", t->ss0, t->esp0);
	kdb_printf("   ss1 = %04x, esp1 = 0x%8.8lx\n", t->ss1, t->esp1);
	kdb_printf("   ss2 = %04x, esp2 = 0x%8.8lx\n", t->ss2, t->esp2);
	kdb_printf("   ldt = %04x, cr3 = 0x%8.8lx\n", t->ldt, t->__cr3);
	kdb_printf("    ds = %04x, es = %04x fs = %04x gs = %04x\n",
			t->ds, t->es, t->fs, t->gs);
	kdb_printf("   eax = 0x%8.8lx, ebx = 0x%8.8lx ecx = 0x%8.8lx edx = 0x%8.8lx\n",
			t->eax, t->ebx, t->ecx, t->edx);
	kdb_printf("   esi = 0x%8.8lx, edi = 0x%8.8lx ebp = 0x%8.8lx\n",
			t->esi, t->edi, t->ebp);
}

static char *gate_desc_types[] = {
	"invalid", "tss-avlb", "ldt", "tss-busy",
	"callgate", "taskgate", "intgate", "trapgate",
	"invalid", "tss32-avlb", "invalid", "tss32-busy",
	"callgate32", "invalid", "intgate32", "trapgate32",
	NULL };

static int
display_gate_desc(kdb_gate_desc_t * d)
{
	kdb_printf("%-11s ", gate_desc_types[d->type]);

	switch(d->type) {
	case KDB_SYS_DESC_TYPE_LDT:
		kdb_printf("base=0x%8.8lx limit=0x%8.8lx dpl=%d\n",
			KDB_SEG_DESC_BASE(((kdb_desc_t *)d)),
			KDB_SEG_DESC_LIMIT(((kdb_desc_t *)d)), d->dpl);
		break;
	case KDB_SYS_DESC_TYPE_TSS32:
	case KDB_SYS_DESC_TYPE_TSS32B:
	{
		struct tss_struct *tss = (struct tss_struct *)KDB_SEG_DESC_BASE(((kdb_desc_t *)d));
		kdb_printf("base=0x%8.8lx limit=0x%8.8lx dpl=%d\n",
			(unsigned long)tss,
			KDB_SEG_DESC_LIMIT(((kdb_desc_t *)d)), d->dpl);
		display_tss(tss);
		break;
	}
	case KDB_SYS_DESC_TYPE_CALLG:
		kdb_printf("sel=0x%4.4x off=0x%8.8lx dpl=%d wc=%d\n",
			d->sel, KDB_SYS_DESC_OFFSET(d), d->dpl,
			KDB_SYS_DESC_CALLG_COUNT(d));
		break;
	case KDB_SYS_DESC_TYPE_CALLG32:
		kdb_printf("sel=0x%4.4x off=0x%8.8lx dpl=%d dwc=%d\n",
			d->sel, KDB_SYS_DESC_OFFSET(d), d->dpl,
			KDB_SYS_DESC_CALLG_COUNT(d));
		break;
	default:
		kdb_printf("sel=0x%4.4x off=0x%8.8lx dpl=%d\n",
			d->sel, KDB_SYS_DESC_OFFSET(d), d->dpl);
		break;
	}

	return 0;
}

static int
display_seg_desc(kdb_desc_t * d)
{
	unsigned char type = d->type;

	if (type & KDB_SEG_DESC_TYPE_CODE) {
		kdb_printf("%-7s base=0x%8.8lx limit=0x%8.8lx dpl=%d %c%c%c %s %s %s \n",
			"code",
			KDB_SEG_DESC_BASE(d), KDB_SEG_DESC_LIMIT(d),
			d->dpl,
			(type & KDB_SEG_DESC_TYPE_CODE_R)?'r':'-',
			'-', 'x',
			d->db ? "32b" : "16b",
			(type & KDB_SEG_DESC_TYPE_A)?"ac":"",
			(type & KDB_SEG_DESC_TYPE_CODE_C)?"conf":"");
	}
	else {
		kdb_printf("%-7s base=0x%8.8lx limit=0x%8.8lx dpl=%d %c%c%c %s %s %s \n",
			"data",
			KDB_SEG_DESC_BASE(d), KDB_SEG_DESC_LIMIT(d),
			d->dpl,
			'r',
			(type & KDB_SEG_DESC_TYPE_DATA_W)?'w':'-',
			'-',
			d->db ? "32b" : "16b",
			(type & KDB_SEG_DESC_TYPE_A)?"ac":"",
			(type & KDB_SEG_DESC_TYPE_DATA_D)?"down":"");
	}

	return 0;
}

static int
kdb_parse_two_numbers(int argc, const char **argv, int *sel, int *count, int *last_sel, int *last_count)
{
	int diag;

	if (argc > 2)
		return KDB_ARGCOUNT;

	kdbgetintenv("MDCOUNT", count);

	if (argc == 0) {
		*sel = *last_sel;
		if (*last_count)
			*count = *last_count;
	} else {
		unsigned long val;

		if (argc >= 1) {
			diag = kdbgetularg(argv[1], &val);
			if (diag)
				return diag;
			*sel = val;
		}
		if (argc >= 2) {
			diag = kdbgetularg(argv[2], &val);
			if (diag)
				return diag;
			*count = (int) val;
			*last_count = (int) val;
		} else if (*last_count) {
			*count = *last_count;
		}
	}
	return 0;
}

/*
 * kdb_gdt
 *
 *	This function implements the 'gdt' command.
 *
 *	gdt [<selector> [<line count>]]
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */
static int
kdb_gdt(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int sel = 0;
	struct Xgt_desc_struct gdtr;
	int diag, count = 8;
	kdb_desc_t * gdt;
	unsigned int max_sel;
	static int last_sel = 0, last_count = 0;

	diag = kdb_parse_two_numbers(argc, argv, &sel, &count, &last_sel, &last_count);
	if (diag)
		return diag;

	__asm__ __volatile__ ("sgdt %0\n\t" : "=m"(gdtr));
	gdt = (kdb_desc_t *) gdtr.address;

	max_sel = (gdtr.size + 1) / sizeof(kdb_desc_t);
	if (sel >= max_sel) {
		sel = 0;
	}

	if (sel + count > max_sel)
		count = max_sel - sel;

	while (count--) {
		kdb_desc_t * d = &gdt[sel];
		kdb_printf("0x%4.4x ", sel++);

		if (!d->present) {
			kdb_printf("not present\n");
			continue;
		}
		if (d->seg)
			display_seg_desc(d);
		else
			display_gate_desc((kdb_gate_desc_t *)d);
	}

	last_sel = sel;
	return 0;
}

/*
 * kdb_ldt
 *
 *	This function implements the 'ldt' command.
 *
 *	ldt [<selector> [<line count>]]
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */
static int
kdb_ldt(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int sel = 0;
	struct Xgt_desc_struct gdtr;
	unsigned long ldtr = 0;
	int diag, count = 8;
	kdb_desc_t * ldt, *ldt_desc;
	unsigned int max_sel;
	static int last_sel = 0, last_count = 0;

	diag = kdb_parse_two_numbers(argc, argv, &sel, &count, &last_sel, &last_count);
	if (diag)
		return diag;

	if (strcmp(argv[0], "ldtp") == 0) {
		kdb_printf("pid=%d, process=%s\n", kdb_current_task->pid, kdb_current_task->comm);
		if (!kdb_current_task->mm || !kdb_current_task->mm->context.ldt) {
			kdb_printf("no special LDT for this process\n");
			return 0;
		}
		ldt = kdb_current_task->mm->context.ldt;
		max_sel = kdb_current_task->mm->context.size;
	} else {

		/* sldt gives the GDT selector for the segment containing LDT */
		__asm__ __volatile__ ("sgdt %0\n\t" : "=m"(gdtr));
		__asm__ __volatile__ ("sldt %0\n\t" : "=m"(ldtr));

		if (ldtr > gdtr.size+1) {
			kdb_printf("invalid ldtr\n");
			return 0;
		}

		ldt_desc = (kdb_desc_t *)(gdtr.address + (ldtr & ~7));
		ldt = (kdb_desc_t *) KDB_SEG_DESC_BASE(ldt_desc);
		max_sel = (KDB_SEG_DESC_LIMIT(ldt_desc)+1) / sizeof(kdb_desc_t);
	}

	if (sel >= max_sel) {
		sel = 0;
	}

	if (sel + count > max_sel)
		count = max_sel - sel;

	while (count--) {
		kdb_desc_t * d = &ldt[sel];
		kdb_printf("0x%4.4x ", sel++);

		if (d->seg)
			display_seg_desc(d);
		else
			display_gate_desc((kdb_gate_desc_t *)d);
	}

	last_sel = sel;
	return 0;
}

/*
 * kdb_idt
 *
 *	This function implements the 'idt' command.
 *
 *	idt [<vector> [<line count>]]
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */
static int
kdb_idt(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int vec = 0;
	struct Xgt_desc_struct idtr;
	int diag, count = 8;
	kdb_gate_desc_t * idt;
	unsigned int max_entries;
	static int last_vec = 0, last_count = 0;

	diag = kdb_parse_two_numbers(argc, argv, &vec, &count, &last_vec, &last_count);
	if (diag)
		return diag;

	__asm__ __volatile__ ("sidt %0\n\t" : "=m"(idtr));
	idt = (kdb_gate_desc_t *) idtr.address;

	max_entries = (idtr.size+1) / sizeof(kdb_gate_desc_t);
	if (vec >= max_entries) {
		vec = 0;
	}

	if (vec + count > max_entries)
		count = max_entries - vec;

	while (count--) {
		kdb_gate_desc_t * d = &idt[vec];
		kdb_printf("0x%4.4x ", vec++);
		if (!d->present) {
			kdb_printf("not present\n");
			continue;
		}
		if (d->seg) {
			kdb_printf("invalid\n");
			continue;
		}
		display_gate_desc(d);
	}

	last_vec = vec;

	return 0;
}

#define _PAGE_PSE 0x080

#if 0
static int
get_pagetables(unsigned long addr, pgd_t **pgdir, pmd_t **pgmiddle, pte_t **pte)
{
	pgd_t * d;
	pmd_t * m;
	pte_t * t;

	if (addr > PAGE_OFFSET) {
		d = pgd_offset_k(addr);
	} else {
		kdb_printf("pid=%d, process=%s\n", kdb_current_task->pid, kdb_current_task->comm);
		d = pgd_offset(kdb_current_task->mm, addr);
	}

	if (pgd_none(*d) || pgd_bad(*d)) {
		*pgdir = NULL;
		*pgmiddle = NULL;
		*pte = NULL;
		return 0;
	} else {
		*pgdir = d;
	}

	/* if _PAGE_PSE is set, pgdir points directly to the page. */
	if (pgd_val(*d) & _PAGE_PSE) {
		*pgmiddle = NULL;
		*pte = NULL;
		return 0;
	}

	m = pmd_offset(d, addr);
	if (pmd_none(*m) || pmd_bad(*m)) {
		*pgmiddle = NULL;
		*pte = NULL;
		return 0;
	} else {
		*pgmiddle = m;
	}

	t = pte_offset(m, addr);
	if (pte_none(*t)) {
		*pte = NULL;
		return 0;
	} else {
		*pte = t;
	}
	kdb_printf("\naddr=%08lx, pgd=%08lx, pmd=%08lx, pte=%08lx\n",
			addr,
			(unsigned long) pgd_val(*d),
			(unsigned long) pmd_val(*m),
			(unsigned long) pte_val(*t));
	return 0;
}
#endif

#define FORMAT_PGDIR(entry) \
	kdb_printf("frame=%05lx %c %s %c %c %c %s %c %s %s \n",\
			(entry >> PAGE_SHIFT), 				\
			(entry & _PAGE_PRESENT)?'p':'n', 		\
			(entry & _PAGE_RW)?"rw":"ro", 			\
			(entry & _PAGE_USER)?'u':'s', 			\
			(entry & _PAGE_ACCESSED)?'a':' ', 		\
			' ', 						\
			(entry & _PAGE_PSE)?"4M":"4K", 			\
			(entry & _PAGE_GLOBAL)?'g':' ', 		\
			(entry & _PAGE_PWT)?"wt":"wb", 			\
			(entry & _PAGE_PCD)?"cd":"  ");

#define FORMAT_PTE(p, entry) \
	kdb_printf("frame=%05lx %c%c%c %c %c %c %s %c %s %s\n",	\
			(entry >> PAGE_SHIFT), 			\
			(pte_read(p))? 'r':'-', 		\
			(pte_write(p))? 'w':'-', 		\
			(pte_exec(p))? 'x':'-', 		\
			(pte_dirty(p))? 'd':' ', 		\
			(pte_young(p))? 'a':' ', 		\
			(entry & _PAGE_USER)? 'u':'s', 		\
			"  ", 					\
			(entry & _PAGE_GLOBAL)? 'g':' ',	\
			(entry & _PAGE_PWT)? "wt":"wb", 	\
			(entry & _PAGE_PCD)? "cd":"  ");
#if 0
static int
display_pgdir(unsigned long addr, pgd_t *pgdir, int count)
{
	unsigned long entry;
	int i;
	int index = pgdir - ((pgd_t *)(((unsigned long)pgdir) & PAGE_MASK));

	count = min(count, PTRS_PER_PGD - index);
	addr &= ~(PGDIR_SIZE-1);

	for (i = 0; i < count; i++, pgdir++) {
		entry = pgd_val(*pgdir);
		kdb_printf("pgd: addr=%08lx ", addr);
		if (pgd_none(*pgdir)) {
			kdb_printf("pgdir not present\n");
		} else {
			FORMAT_PGDIR(entry);
		}
		addr += PGDIR_SIZE;
	}
	return i;
}
#endif

#if 0	/* for now, let's not print pgmiddle. */
static int
display_pgmiddle(unsigned long addr, pmd_t *pgmiddle, int count)
{
	unsigned long entry;
	int i;
	int index = pgmiddle - ((pmd_t *)(((unsigned long)pgmiddle) & PAGE_MASK));

	count = min(count, PTRS_PER_PMD - index);
	addr &= ~(PMD_SIZE-1);

	for (i = 0; i < count; i++, pgmiddle++) {
		entry = pmd_val(*pgmiddle);
		kdb_printf("pmd: addr=%08lx ", addr);
		if (pmd_none(*pgmiddle)) {
			kdb_printf("pgmiddle not present\n");
		} else {
			FORMAT_PGDIR(entry);
		}
		addr += PMD_SIZE;
	}
	return i;
}
#endif

#if 0
static int
display_pte(unsigned long addr, pte_t *pte, int count)
{
	unsigned long entry;
	int i;
	int index = pte - ((pte_t *)(((unsigned long)pte) & PAGE_MASK));

	count = min(count, PTRS_PER_PTE - index);
	addr &= PAGE_MASK;

	for (i = 0; i < count; i++, pte++) {
		entry = pte_val(*pte);
		kdb_printf("pte: addr=%08lx ", addr);
		if (pte_none(*pte)) {
			kdb_printf("pte not present\n");
		} else if (!pte_present(*pte)) {
			kdb_printf("page swapped out. swp_offset=%08lx ", SWP_OFFSET(pte_to_swp_entry(*pte)));
			kdb_printf("swp_type=%8lx", SWP_TYPE(pte_to_swp_entry(*pte)));
		} else {
			FORMAT_PTE(*pte, entry);
		}
		addr += PAGE_SIZE;
	}
	return i;
}


/*
 * kdb_pte
 *
 *	This function implements the 'pte' command.
 *
 *	pte  <addr arg> [<line count>]
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */
static int
kdb_pte(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	static unsigned long last_addr = 0, last_count = 0;
	int count = 8;
	unsigned long addr;
	long offset = 0;
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pte;

#ifdef CONFIG_X86_PAE
	kdb_printf("This kernel is compiled with PAE support.");
	return KDB_NOTIMP;
#endif
	kdbgetintenv("MDCOUNT", &count);

	if (argc == 0) {
		if (last_addr == 0)
			return KDB_ARGCOUNT;
		addr = last_addr;
		if (last_count)
			count = last_count;
	} else {
		kdb_machreg_t val;
		int diag, nextarg = 1;
		diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
		if (diag)
			return diag;
		if (argc > nextarg+1)
			return KDB_ARGCOUNT;

		if (argc >= nextarg) {
			diag = kdbgetularg(argv[nextarg], &val);
			if (!diag) {
				count = (int) val;
				last_count = count;
			} else if (last_count) {
				count = last_count;
			}
		}
	}

	/*
	 * round off the addr to a page boundary.
	 */
	addr &= PAGE_MASK;

	get_pagetables(addr, &pgdir, &pgmiddle, &pte);

	if (pgdir)
		display_pgdir(addr, pgdir, 1);
#if 0	/* for now, let's not print pgmiddle. */
	   if (pgmiddle)
		display_pgmiddle(addr, pgmiddle, 1);
#endif
	if (pte) {
		int displayed;
		displayed = display_pte(addr, pte, count);
		addr += (displayed << PAGE_SHIFT);
	}
	last_addr = addr;
	return 0;
}
#else
/*
 * Todo - In 2.5 the pte_offset macro in asm/pgtable.h seems to be
 * renamed to pte_offset_kernel.
 */
static int
kdb_pte(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	kdb_printf("not supported.");
	return KDB_NOTIMP;
}
#endif

/*
 * kdb_rdv
 *
 *	This function implements the 'rdv' command.
 *	It displays all registers of the current processor
 *	included control registers in verbose mode.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 * 	This should have been an option to rd command say "rd v",
 * 	but it is here as it is a non-essential x86-only command,
 * 	that need not clutter arch/i386/kdb/kdbasupport.c.
 */
static int
kdb_rdv(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	kdba_dumpregs(regs, NULL, NULL);
	kdb_printf("\n");
	display_eflags(regs->eflags);
	kdb_printf("\n");
	display_gdtr();
	display_idtr();
	display_ldtr();
	kdb_printf("\n");
	display_cr0();
	display_cr3();
	display_cr4();
	kdb_printf("\n");
	display_dr();
	return 0;
}

static int __init kdbm_x86_init(void)
{
	kdb_register("rdv", kdb_rdv, NULL, "Display registers in verbose mode", 0);
	kdb_register_repeat("gdt", kdb_gdt, "<sel> [<count>]", "Display GDT", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("idt", kdb_idt, "<int> [<count>]", "Display IDT", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("ldt", kdb_ldt, "<sel> [<count>]", "Display LDT", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("ptex", kdb_pte, "<addr> [<count>]", "Display pagetables", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("ldtp", kdb_ldt, "<sel> [<count>]", "Display Process LDT", 0, KDB_REPEAT_NO_ARGS);
	return 0;
}

static void __exit kdbm_x86_exit(void)
{
	kdb_unregister("rdv");
	kdb_unregister("gdt");
	kdb_unregister("ldt");
	kdb_unregister("idt");
	kdb_unregister("ptex");
	kdb_unregister("ldtp");
}

module_init(kdbm_x86_init)
module_exit(kdbm_x86_exit)
