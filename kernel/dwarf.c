/*
 * Copyright (C) 2002-2006 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 * This code is released under version 2 of the GNU GPL.
 *
 * A simple API for unwinding kernel stacks.  This is used for
 * debugging and error reporting purposes.  The kernel doesn't need
 * full-blown stack unwinding with all the bells and whistles, so there
 * is not much point in implementing the full Dwarf2 unwind API.
 */

#include <linux/unwind.h>
#include <linux/module.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/stop_machine.h>
#include <linux/uaccess.h>
#include <asm/sections.h>
#include <asm/unaligned.h>

#define MAX_STACK_DEPTH 8

#define PTREGS_INFO(f) { \
		BUILD_BUG_ON_ZERO(offsetof(struct pt_regs, f) \
				  % FIELD_SIZEOF(struct pt_regs, f)) \
		+ offsetof(struct pt_regs, f) \
		  / FIELD_SIZEOF(struct pt_regs, f), \
		FIELD_SIZEOF(struct pt_regs, f) \
	}
#define FRAME_REG(f, r, t) (((t *)&(f)->u.regs_arr)[reg_info[r].offs])

static const struct {
	unsigned offs:BITS_PER_LONG / 2;
	unsigned width:BITS_PER_LONG / 2;
} reg_info[] = {
	UNW_REGISTER_INFO
};

#undef PTREGS_INFO

#ifndef REG_INVALID
#define REG_INVALID(r) (reg_info[r].width == 0)
#endif

#define DW_CFA_nop				0x00
#define DW_CFA_set_loc				0x01
#define DW_CFA_advance_loc1			0x02
#define DW_CFA_advance_loc2			0x03
#define DW_CFA_advance_loc4			0x04
#define DW_CFA_offset_extended			0x05
#define DW_CFA_restore_extended			0x06
#define DW_CFA_undefined			0x07
#define DW_CFA_same_value			0x08
#define DW_CFA_register				0x09
#define DW_CFA_remember_state			0x0a
#define DW_CFA_restore_state			0x0b
#define DW_CFA_def_cfa				0x0c
#define DW_CFA_def_cfa_register			0x0d
#define DW_CFA_def_cfa_offset			0x0e
#define DW_CFA_def_cfa_expression		0x0f
#define DW_CFA_expression			0x10
#define DW_CFA_offset_extended_sf		0x11
#define DW_CFA_def_cfa_sf			0x12
#define DW_CFA_def_cfa_offset_sf		0x13
#define DW_CFA_val_offset			0x14
#define DW_CFA_val_offset_sf			0x15
#define DW_CFA_val_expression			0x16
#define DW_CFA_lo_user				0x1c
#define DW_CFA_GNU_window_save			0x2d
#define DW_CFA_GNU_args_size			0x2e
#define DW_CFA_GNU_negative_offset_extended	0x2f
#define DW_CFA_hi_user				0x3f

#define DW_EH_PE_FORM		0x07
#define DW_EH_PE_native		0x00
#define DW_EH_PE_leb128		0x01
#define DW_EH_PE_data2		0x02
#define DW_EH_PE_data4		0x03
#define DW_EH_PE_data8		0x04
#define DW_EH_PE_signed		0x08
#define DW_EH_PE_ADJUST		0x70
#define DW_EH_PE_abs		0x00
#define DW_EH_PE_pcrel		0x10
#define DW_EH_PE_textrel	0x20
#define DW_EH_PE_datarel	0x30
#define DW_EH_PE_funcrel	0x40
#define DW_EH_PE_aligned	0x50
#define DW_EH_PE_indirect	0x80
#define DW_EH_PE_omit		0xff

#define DW_OP_addr		0x03
#define DW_OP_deref		0x06
#define DW_OP_const1u		0x08
#define DW_OP_const1s		0x09
#define DW_OP_const2u		0x0a
#define DW_OP_const2s		0x0b
#define DW_OP_const4u		0x0c
#define DW_OP_const4s		0x0d
#define DW_OP_const8u		0x0e
#define DW_OP_const8s		0x0f
#define DW_OP_constu		0x10
#define DW_OP_consts		0x11
#define DW_OP_dup		0x12
#define DW_OP_drop		0x13
#define DW_OP_over		0x14
#define DW_OP_pick		0x15
#define DW_OP_swap		0x16
#define DW_OP_rot		0x17
#define DW_OP_xderef		0x18
#define DW_OP_abs		0x19
#define DW_OP_and		0x1a
#define DW_OP_div		0x1b
#define DW_OP_minus		0x1c
#define DW_OP_mod		0x1d
#define DW_OP_mul		0x1e
#define DW_OP_neg		0x1f
#define DW_OP_not		0x20
#define DW_OP_or		0x21
#define DW_OP_plus		0x22
#define DW_OP_plus_uconst	0x23
#define DW_OP_shl		0x24
#define DW_OP_shr		0x25
#define DW_OP_shra		0x26
#define DW_OP_xor		0x27
#define DW_OP_bra		0x28
#define DW_OP_eq		0x29
#define DW_OP_ge		0x2a
#define DW_OP_gt		0x2b
#define DW_OP_le		0x2c
#define DW_OP_lt		0x2d
#define DW_OP_ne		0x2e
#define DW_OP_skip		0x2f
#define DW_OP_lit0		0x30
#define DW_OP_lit31		0x4f
#define DW_OP_reg0		0x50
#define DW_OP_reg31		0x6f
#define DW_OP_breg0		0x70
#define DW_OP_breg31		0x8f
#define DW_OP_regx		0x90
#define DW_OP_fbreg		0x91
#define DW_OP_bregx		0x92
#define DW_OP_piece		0x93
#define DW_OP_deref_size	0x94
#define DW_OP_xderef_size	0x95
#define DW_OP_nop		0x96

typedef unsigned long uleb128_t;
typedef signed long sleb128_t;
#define sleb128abs __builtin_labs

static struct unwind_table {
	struct {
		unsigned long pc;
		unsigned long range;
	} core, init;
	const void *address;
	unsigned long size;
	const unsigned char *header;
	unsigned long hdrsz;
	struct unwind_table *link;
	const char *name;
} root_table;

struct unwind_item {
	enum item_location {
		Nowhere,
		Memory,
		Register,
		Value
	} where;
	uleb128_t value;
};

struct dw_unwind_state {
	uleb128_t loc, org;
	const u8 *cieStart, *cieEnd;
	uleb128_t codeAlign;
	sleb128_t dataAlign;
	struct cfa {
		uleb128_t reg, offs, elen;
		const u8 *expr;
	} cfa;
	struct unwind_item regs[ARRAY_SIZE(reg_info)];
	u8 stackDepth;
	u8 version;
	const u8 *label;
	const u8 *stack[MAX_STACK_DEPTH];
};

static const struct cfa badCFA = { ARRAY_SIZE(reg_info), 1 };

static unsigned int unwind_debug;
static int __init unwind_debug_setup(char *s)
{
	if (kstrtouint(s, 0, &unwind_debug))
		pr_info("%s: bad parameter '%s'\n", __func__, s);
	return 1;
}
__setup("unwind_debug=", unwind_debug_setup);

#define dprintk(lvl, fmt, args...) do {				\
	if (unwind_debug >= lvl)					\
		printk(KERN_DEBUG "unwind: " fmt "\n", ##args);	\
} while (0)

static struct unwind_table *find_table(unsigned long pc)
{
	struct unwind_table *table;

	for (table = &root_table; table; table = table->link)
		if ((pc >= table->core.pc &&
				 pc < table->core.pc + table->core.range) ||
				(pc >= table->init.pc &&
				 pc < table->init.pc + table->init.range))
			break;

	return table;
}

static unsigned long read_pointer(const u8 **pLoc,
				  const void *end,
				  int ptrType,
				  unsigned long data_base);

static void __init_or_module
init_unwind_table(struct unwind_table *table, const char *name,
		  const void *core_start, unsigned long core_size,
		  const void *init_start, unsigned long init_size,
		  const void *table_start, unsigned long table_size,
		  const u8 *header_start, unsigned long header_size)
{
	const u8 *ptr = header_start + 4;
	const u8 *end = header_start + header_size;

	table->core.pc = (unsigned long)core_start;
	table->core.range = core_size;
	table->init.pc = (unsigned long)init_start;
	table->init.range = init_size;
	table->address = table_start;
	table->size = table_size;
	/* See if the linker provided table looks valid. */
	if (header_size <= 4 || header_start[0] != 1 ||
			/* ptr to eh_frame */
			(void *)read_pointer(&ptr, end, header_start[1], 0) !=
				table_start ||
			/* fde count */
			!read_pointer(&ptr, end, header_start[2], 0) ||
			/* table[0] -- initial location */
			!read_pointer(&ptr, end, header_start[3],
				(unsigned long)header_start) ||
			/* table[0] -- address */
			!read_pointer(&ptr, end, header_start[3],
				(unsigned long)header_start))
		header_start = NULL;
	table->hdrsz = header_size;
	smp_wmb();
	table->header = header_start;
	table->link = NULL;
	table->name = name;
}

void __init dwarf_init(void)
{
	extern const char __start_unwind[], __end_unwind[];
	extern const char __start_unwind_hdr[], __end_unwind_hdr[];
#ifdef CONFIG_DEBUG_RODATA
	unsigned long text_len = _etext - _text;
	const void *init_start = __init_begin;
	unsigned long init_len = __init_end - __init_begin;
#else
	unsigned long text_len = _end - _text,
	const void *init_start = NULL;
	unsigned long init_len = 0;
#endif
	init_unwind_table(&root_table, "kernel", _text, text_len,
			init_start, init_len,
			__start_unwind, __end_unwind - __start_unwind,
			__start_unwind_hdr,
			__end_unwind_hdr - __start_unwind_hdr);
}

static const u32 bad_cie, not_fde;
static const u32 *cie_for_fde(const u32 *fde, const struct unwind_table *);
static int fde_pointer_type(const u32 *cie);

struct eh_frame_hdr_table_entry {
	unsigned long start, fde;
};

static int __init_or_module
cmp_eh_frame_hdr_table_entries(const void *p1, const void *p2)
{
	const struct eh_frame_hdr_table_entry *e1 = p1;
	const struct eh_frame_hdr_table_entry *e2 = p2;

	return (e1->start > e2->start) - (e1->start < e2->start);
}

static void __init_or_module
swap_eh_frame_hdr_table_entries(void *p1, void *p2, int size)
{
	struct eh_frame_hdr_table_entry *e1 = p1;
	struct eh_frame_hdr_table_entry *e2 = p2;
	unsigned long v;

	v = e1->start;
	e1->start = e2->start;
	e2->start = v;
	v = e1->fde;
	e1->fde = e2->fde;
	e2->fde = v;
}

static void __init_or_module
setup_unwind_table(struct unwind_table *table,
		   void *(*alloc)(size_t, gfp_t))
{
	const u8 *ptr;
	unsigned long tableSize = table->size, hdrSize;
	unsigned int n;
	const u32 *fde;
	struct {
		u8 version;
		u8 eh_frame_ptr_enc;
		u8 fde_count_enc;
		u8 table_enc;
		unsigned long eh_frame_ptr;
		unsigned int fde_count;
		struct eh_frame_hdr_table_entry table[];
	} __attribute__((__packed__)) *header;

	if (table->header)
		return;

	if (table->hdrsz)
		pr_warn(".eh_frame_hdr for '%s' present but unusable\n",
			table->name);

	if (tableSize & (sizeof(*fde) - 1))
		return;

	for (fde = table->address, n = 0;
			tableSize > sizeof(*fde) &&
			tableSize - sizeof(*fde) >= *fde;
			tableSize -= sizeof(*fde) + *fde,
			fde += 1 + *fde / sizeof(*fde)) {
		const u32 *cie = cie_for_fde(fde, table);
		int ptrType;

		if (cie == &not_fde)
			continue;
		if (cie == NULL || cie == &bad_cie)
			return;
		ptrType = fde_pointer_type(cie);
		if (ptrType < 0)
			return;
		ptr = (const u8 *)(fde + 2);
		if (!read_pointer(&ptr, (const u8 *)(fde + 1) + *fde,
					ptrType, 0))
			return;
		++n;
	}

	if (tableSize || n < 32)
		return;

	hdrSize = sizeof(*header) + n * sizeof(*header->table);
	dprintk(2, "Binary lookup table size for %s: %lu bytes", table->name,
			hdrSize);
	header = alloc(hdrSize, GFP_KERNEL);
	if (!header)
		return;
	header->version	= 1;
	header->eh_frame_ptr_enc = DW_EH_PE_abs|DW_EH_PE_native;
	header->fde_count_enc = DW_EH_PE_abs|DW_EH_PE_data4;
	header->table_enc = DW_EH_PE_abs|DW_EH_PE_native;
	put_unaligned((unsigned long)table->address, &header->eh_frame_ptr);
	BUILD_BUG_ON(offsetof(typeof(*header), fde_count) %
			__alignof(typeof(header->fde_count)));
	header->fde_count = n;

	BUILD_BUG_ON(offsetof(typeof(*header), table) %
			__alignof(typeof(*header->table)));
	for (fde = table->address, tableSize = table->size, n = 0;
			tableSize;
			tableSize -= sizeof(*fde) + *fde,
			fde += 1 + *fde / sizeof(*fde)) {
		const u32 *cie = fde + 1 - fde[1] / sizeof(*fde);

		if (!fde[1])
			continue; /* this is a CIE */
		ptr = (const u8 *)(fde + 2);
		header->table[n].start = read_pointer(&ptr,
				(const u8 *)(fde + 1) + *fde,
				fde_pointer_type(cie), 0);
		header->table[n].fde = (unsigned long)fde;
		++n;
	}
	WARN_ON(n != header->fde_count);

	sort(header->table, n, sizeof(*header->table),
			cmp_eh_frame_hdr_table_entries,
			swap_eh_frame_hdr_table_entries);

	table->hdrsz = hdrSize;
	smp_wmb();
	table->header = (const void *)header;
}

static void *__init balloc(size_t sz, gfp_t unused)
{
	return __alloc_bootmem_nopanic(sz, sizeof(unsigned long),
			__pa(MAX_DMA_ADDRESS));
}

void __init dwarf_setup(void)
{
	setup_unwind_table(&root_table, balloc);
}

#ifdef CONFIG_MODULES

static struct unwind_table *last_table;

/* Must be called with module_mutex held. */
void *dwarf_add_table(struct module *module, const void *table_start,
		unsigned long table_size)
{
	struct unwind_table *table;
#ifdef CONFIG_DEBUG_SET_MODULE_RONX
	unsigned long core_size = module->core_layout.text_size;
	unsigned long init_size = module->init_layout.text_size;
#else
	unsigned long core_size = module->core_layout.size;
	unsigned long init_size = module->init_layout.size;
#endif

	if (table_size <= 0)
		return NULL;

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return NULL;

	init_unwind_table(table, module->name,
			module->core_layout.base, core_size,
			module->init_layout.base, init_size,
			table_start, table_size, NULL, 0);
	setup_unwind_table(table, kmalloc);

	if (last_table)
		last_table->link = table;
	else
		root_table.link = table;
	last_table = table;

	return table;
}

struct unlink_table_info {
	struct unwind_table *table;
	bool init_only;
};

static int unlink_table(void *arg)
{
	struct unlink_table_info *info = arg;
	struct unwind_table *table = info->table, *prev;

	for (prev = &root_table; prev->link && prev->link != table;
			prev = prev->link)
		;

	if (prev->link) {
		if (info->init_only) {
			table->init.pc = 0;
			table->init.range = 0;
			info->table = NULL;
		} else {
			prev->link = table->link;
			if (!prev->link)
				last_table = prev;
		}
	} else
		info->table = NULL;

	return 0;
}

/* Must be called with module_mutex held. */
void dwarf_remove_table(void *handle, bool init_only)
{
	struct unwind_table *table = handle;
	struct unlink_table_info info;

	if (!table || table == &root_table)
		return;

	if (init_only && table == last_table) {
		table->init.pc = 0;
		table->init.range = 0;
		return;
	}

	info.table = table;
	info.init_only = init_only;
	stop_machine(unlink_table, &info, NULL);

	if (info.table) {
		kfree(table->header);
		kfree(table);
	}
}

#endif /* CONFIG_MODULES */

static uleb128_t get_uleb128(const u8 **pcur, const u8 *end)
{
	const u8 *cur = *pcur;
	uleb128_t value;
	unsigned int shift;

	for (shift = 0, value = 0; cur < end; shift += 7) {
		if (shift + 7 > 8 * sizeof(value)
		    && (*cur & 0x7fU) >= (1U << (8 * sizeof(value) - shift))) {
			cur = end + 1;
			break;
		}
		value |= (uleb128_t)(*cur & 0x7f) << shift;
		if (!(*cur++ & 0x80))
			break;
	}
	*pcur = cur;

	return value;
}

static sleb128_t get_sleb128(const u8 **pcur, const u8 *end)
{
	const u8 *cur = *pcur;
	sleb128_t value;
	unsigned int shift;

	for (shift = 0, value = 0; cur < end; shift += 7) {
		if (shift + 7 > 8 * sizeof(value)
		    && (*cur & 0x7fU) >= (1U << (8 * sizeof(value) - shift))) {
			cur = end + 1;
			break;
		}
		value |= (sleb128_t)(*cur & 0x7f) << shift;
		if (!(*cur & 0x80)) {
			value |= -(*cur++ & 0x40) << shift;
			break;
		}
	}
	*pcur = cur;

	return value;
}

static const u32 *cie_for_fde(const u32 *fde, const struct unwind_table *table)
{
	const u32 *cie;

	if (!*fde || (*fde & (sizeof(*fde) - 1)))
		return &bad_cie;
	if (!fde[1])
		return &not_fde; /* this is a CIE */
	if ((fde[1] & (sizeof(*fde) - 1)) ||
			fde[1] > (unsigned long)(fde + 1) -
			(unsigned long)table->address)
		return NULL; /* this is not a valid FDE */
	cie = fde + 1 - fde[1] / sizeof(*fde);
	if (*cie <= sizeof(*cie) + 4 || *cie >= fde[1] - sizeof(*fde) ||
			(*cie & (sizeof(*cie) - 1)) || cie[1])
		return NULL; /* this is not a (valid) CIE */
	return cie;
}

static unsigned long read_pointer(const u8 **pLoc,
				  const void *end,
				  int ptrType,
				  unsigned long data_base)
{
	unsigned long value = 0;
	union {
		const u8 *p8;
		const u16 *p16u;
		const s16 *p16s;
		const u32 *p32u;
		const s32 *p32s;
		const unsigned long *pul;
	} ptr;

	if (ptrType < 0 || ptrType == DW_EH_PE_omit) {
		dprintk(1, "Invalid pointer encoding %02X (%p,%p).", ptrType,
				*pLoc, end);
		return 0;
	}
	ptr.p8 = *pLoc;
	switch (ptrType & DW_EH_PE_FORM) {
	case DW_EH_PE_data2:
		if (end < (const void *)(ptr.p16u + 1)) {
			dprintk(1, "Data16 overrun (%p,%p).", ptr.p8, end);
			return 0;
		}
		if (ptrType & DW_EH_PE_signed)
			value = get_unaligned(ptr.p16s++);
		else
			value = get_unaligned(ptr.p16u++);
		break;
	case DW_EH_PE_data4:
#ifdef CONFIG_64BIT
		if (end < (const void *)(ptr.p32u + 1)) {
			dprintk(1, "Data32 overrun (%p,%p).", ptr.p8, end);
			return 0;
		}
		if (ptrType & DW_EH_PE_signed)
			value = get_unaligned(ptr.p32s++);
		else
			value = get_unaligned(ptr.p32u++);
		break;
	case DW_EH_PE_data8:
		BUILD_BUG_ON(sizeof(u64) != sizeof(value));
#else
		BUILD_BUG_ON(sizeof(u32) != sizeof(value));
#endif
		/* fallthrough */
	case DW_EH_PE_native:
		if (end < (const void *)(ptr.pul + 1)) {
			dprintk(1, "DataUL overrun (%p,%p).", ptr.p8, end);
			return 0;
		}
		value = get_unaligned(ptr.pul++);
		break;
	case DW_EH_PE_leb128:
		BUILD_BUG_ON(sizeof(uleb128_t) > sizeof(value));
		if (ptrType & DW_EH_PE_signed)
			value = get_sleb128(&ptr.p8, end);
		else
			value = get_uleb128(&ptr.p8, end);
		if ((const void *)ptr.p8 > end) {
			dprintk(1, "DataLEB overrun (%p,%p).", ptr.p8, end);
			return 0;
		}
		break;
	default:
		dprintk(2, "Cannot decode pointer type %02X (%p,%p).",
				ptrType, ptr.p8, end);
		return 0;
	}
	switch (ptrType & DW_EH_PE_ADJUST) {
	case DW_EH_PE_abs:
		break;
	case DW_EH_PE_pcrel:
		value += (unsigned long)*pLoc;
		break;
	case DW_EH_PE_textrel:
		dprintk(2, "Text-relative encoding %02X (%p,%p), but zero text base.",
				ptrType, *pLoc, end);
		return 0;
	case DW_EH_PE_datarel:
		if (likely(data_base)) {
			value += data_base;
			break;
		}
		dprintk(2, "Data-relative encoding %02X (%p,%p), but zero data base.",
				ptrType, *pLoc, end);
		return 0;
	default:
		dprintk(2, "Cannot adjust pointer type %02X (%p,%p).",
				ptrType, *pLoc, end);
		return 0;
	}
	if ((ptrType & DW_EH_PE_indirect)
	    && probe_kernel_address((unsigned long *)value, value)) {
		dprintk(1, "Cannot read indirect value %lx (%p,%p).",
				value, *pLoc, end);
		return 0;
	}
	*pLoc = ptr.p8;

	return value;
}

static int fde_pointer_type(const u32 *cie)
{
	const u8 *ptr = (const u8 *)(cie + 2);
	unsigned int version = *ptr;

	if (version != 1)
		return -1; /* unsupported */
	if (*++ptr) {
		const char *aug;
		const u8 *end = (const u8 *)(cie + 1) + *cie;
		uleb128_t len;

		/* check if augmentation size is first (and thus present) */
		if (*ptr != 'z')
			return -1;
		/* check if augmentation string is nul-terminated */
		aug = (const void *)ptr;
		ptr = memchr(aug, 0, end - ptr);
		if (ptr == NULL)
			return -1;
		++ptr; /* skip terminator */
		get_uleb128(&ptr, end); /* skip code alignment */
		get_sleb128(&ptr, end); /* skip data alignment */
		/* skip return address column */
		if (version <= 1)
			++ptr;
		else
			get_uleb128(&ptr, end);
		len = get_uleb128(&ptr, end); /* augmentation length */
		if (ptr + len < ptr || ptr + len > end)
			return -1;
		end = ptr + len;
		while (*++aug) {
			if (ptr >= end)
				return -1;
			switch (*aug) {
			case 'L':
				++ptr;
				break;
			case 'P': {
				int ptrType = *ptr++;

				if (!read_pointer(&ptr, end, ptrType, 0) ||
						ptr > end)
					return -1;
				break;
			}
			case 'R':
				return *ptr;
			default:
				return -1;
			}
		}
	}
	return DW_EH_PE_native | DW_EH_PE_abs;
}

static int advance_loc(unsigned long delta, struct dw_unwind_state *state)
{
	state->loc += delta * state->codeAlign;

	return delta > 0;
}

static void set_rule(uleb128_t reg,
		     enum item_location where,
		     uleb128_t value,
		     struct dw_unwind_state *state)
{
	if (reg < ARRAY_SIZE(state->regs)) {
		state->regs[reg].where = where;
		state->regs[reg].value = value;
	}
}

static int processCFI(const u8 *start,
		      const u8 *end,
		      unsigned long targetLoc,
		      int ptrType,
		      struct dw_unwind_state *state)
{
	union {
		const u8 *p8;
		const u16 *p16;
		const u32 *p32;
	} ptr;
	int result = 1;

	if (start != state->cieStart) {
		state->loc = state->org;
		result = processCFI(state->cieStart, state->cieEnd, 0, ptrType,
				state);
		if (targetLoc == 0 && state->label == NULL)
			return result;
	}
	for (ptr.p8 = start; result && ptr.p8 < end; ) {
		switch (*ptr.p8 >> 6) {
			uleb128_t value;

		case 0:
			switch (*ptr.p8++) {
			case DW_CFA_nop:
				break;
			case DW_CFA_set_loc:
				state->loc = read_pointer(&ptr.p8, end,
						ptrType, 0);
				if (state->loc == 0)
					result = 0;
				break;
			case DW_CFA_advance_loc1:
				result = ptr.p8 < end &&
					advance_loc(*ptr.p8++, state);
				break;
			case DW_CFA_advance_loc2:
				result = ptr.p8 <= end + 2 &&
					advance_loc(*ptr.p16++, state);
				break;
			case DW_CFA_advance_loc4:
				result = ptr.p8 <= end + 4 &&
					advance_loc(*ptr.p32++, state);
				break;
			case DW_CFA_offset_extended:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Memory,
						get_uleb128(&ptr.p8, end),
						state);
				break;
			case DW_CFA_val_offset:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Value,
						get_uleb128(&ptr.p8, end),
						state);
				break;
			case DW_CFA_offset_extended_sf:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Memory,
						get_sleb128(&ptr.p8, end),
						state);
				break;
			case DW_CFA_val_offset_sf:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Value,
						get_sleb128(&ptr.p8, end),
						state);
				break;
			/*todo case DW_CFA_expression: */
			/*todo case DW_CFA_val_expression: */
			case DW_CFA_restore_extended:
			case DW_CFA_undefined:
			case DW_CFA_same_value:
				set_rule(get_uleb128(&ptr.p8, end), Nowhere, 0,
						state);
				break;
			case DW_CFA_register:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Register,
						get_uleb128(&ptr.p8, end),
						state);
				break;
			case DW_CFA_remember_state:
				if (ptr.p8 == state->label) {
					state->label = NULL;
					return 1;
				}
				if (state->stackDepth >= MAX_STACK_DEPTH) {
					dprintk(1, "State stack overflow (%p,%p).",
							ptr.p8, end);
					return 0;
				}
				state->stack[state->stackDepth++] = ptr.p8;
				break;
			case DW_CFA_restore_state:
				if (state->stackDepth) {
					const uleb128_t loc = state->loc;
					const u8 *label = state->label;

					state->label = state->stack[state->stackDepth - 1];
					memcpy(&state->cfa, &badCFA,
							sizeof(state->cfa));
					memset(state->regs, 0,
							sizeof(state->regs));
					state->stackDepth = 0;
					result = processCFI(start, end, 0,
							ptrType, state);
					state->loc = loc;
					state->label = label;
				} else {
					dprintk(1, "State stack underflow (%p,%p).",
							ptr.p8, end);
					return 0;
				}
				break;
			case DW_CFA_def_cfa:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				state->cfa.elen = 0;
				/* fallthrough */
			case DW_CFA_def_cfa_offset:
				state->cfa.offs = get_uleb128(&ptr.p8, end);
				break;
			case DW_CFA_def_cfa_sf:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				state->cfa.elen = 0;
				/* fallthrough */
			case DW_CFA_def_cfa_offset_sf:
				state->cfa.offs = get_sleb128(&ptr.p8, end) *
					state->dataAlign;
				break;
			case DW_CFA_def_cfa_register:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				state->cfa.elen = 0;
				break;
			case DW_CFA_def_cfa_expression:
				state->cfa.elen = get_uleb128(&ptr.p8, end);
				if (!state->cfa.elen) {
					dprintk(1, "Zero-length CFA expression.");
					return 0;
				}
				state->cfa.expr = ptr.p8;
				ptr.p8 += state->cfa.elen;
				break;
			case DW_CFA_GNU_args_size:
				get_uleb128(&ptr.p8, end);
				break;
			case DW_CFA_GNU_negative_offset_extended:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Memory, (uleb128_t)0 -
						get_uleb128(&ptr.p8, end),
						state);
				break;
			case DW_CFA_GNU_window_save:
			default:
				dprintk(1, "Unrecognized CFI op %02X (%p,%p).",
						ptr.p8[-1], ptr.p8 - 1, end);
				result = 0;
				break;
			}
			break;
		case 1:
			result = advance_loc(*ptr.p8++ & 0x3f, state);
			break;
		case 2:
			value = *ptr.p8++ & 0x3f;
			set_rule(value, Memory, get_uleb128(&ptr.p8, end),
					state);
			break;
		case 3:
			set_rule(*ptr.p8++ & 0x3f, Nowhere, 0, state);
			break;
		}
		if (ptr.p8 > end) {
			dprintk(1, "Data overrun (%p,%p).", ptr.p8, end);
			result = 0;
		}
		if (result && targetLoc != 0 && targetLoc < state->loc)
			return 1;
	}

	if (result && ptr.p8 < end)
		dprintk(1, "Data underrun (%p,%p).", ptr.p8, end);

	return result && ptr.p8 == end && (targetLoc == 0 ||
		(/*
		  * TODO While in theory this should apply, gcc in practice
		  * omits everything past the function prolog, and hence the
		  * location never reaches the end of the function.
		  *    targetLoc < state->loc &&
		  */
		 state->label == NULL));
}

static unsigned long evaluate(const u8 *expr, const u8 *end,
			      const struct unwind_state *frame)
{
	union {
		const u8 *pu8;
		const s8 *ps8;
		const u16 *pu16;
		const s16 *ps16;
		const u32 *pu32;
		const s32 *ps32;
		const u64 *pu64;
		const s64 *ps64;
	} ptr = { expr };
	unsigned long stack[8], val1, val2;
	unsigned int stidx = 0;
	int ret;
#define PUSH(v) do {			\
	unsigned long v__ = (v);	\
					\
	if (stidx >= ARRAY_SIZE(stack))	\
		return 0;		\
	stack[stidx++] = v__;		\
} while (0)
#define POP() ({			\
	if (!stidx)			\
		return 0;		\
	stack[--stidx];			\
})

	while (ptr.pu8 < end) {
		switch (*ptr.pu8++) {
		/*todo case DW_OP_addr: */
		case DW_OP_deref:
			val1 = POP();
			ret = probe_kernel_address((unsigned long *)val1, val2);
			if (ret) {
				dprintk(1, "Cannot de-reference %lx (%p,%p).",
						val1, ptr.pu8 - 1, end);
				return 0;
			}
			PUSH(val2);
			break;
		/*todo? case DW_OP_xderef: */
		/*todo case DW_OP_deref_size: */
		/*todo? case DW_OP_xderef_size: */
		case DW_OP_const1u:
			if (ptr.pu8 < end)
				PUSH(*ptr.pu8);
			++ptr.pu8;
			break;
		case DW_OP_const1s:
			if (ptr.pu8 < end)
				PUSH(*ptr.ps8);
			++ptr.ps8;
			break;
		case DW_OP_const2u:
			if (ptr.pu8 + 1 < end)
				PUSH(*ptr.pu16);
			++ptr.pu16;
			break;
		case DW_OP_const2s:
			if (ptr.pu8 + 1 < end)
				PUSH(*ptr.ps16);
			++ptr.ps16;
			break;
		case DW_OP_const4u:
			if (ptr.pu8 + 3 < end)
				PUSH(*ptr.pu32);
			++ptr.pu32;
			break;
		case DW_OP_const4s:
			if (ptr.pu8 + 3 < end)
				PUSH(*ptr.ps32);
			++ptr.ps32;
			break;
		case DW_OP_const8u:
			if (ptr.pu8 + 7 < end)
				PUSH(*ptr.pu64);
			++ptr.pu64;
			break;
		case DW_OP_const8s:
			if (ptr.pu8 + 7 < end)
				PUSH(*ptr.ps64);
			++ptr.ps64;
			break;
		case DW_OP_constu:
			PUSH(get_uleb128(&ptr.pu8, end));
			break;
		case DW_OP_consts:
			PUSH(get_sleb128(&ptr.pu8, end));
			break;
		case DW_OP_dup:
			if (!stidx)
				return 0;
			PUSH(stack[stidx - 1]);
			break;
		case DW_OP_drop:
			(void)POP();
			break;
		case DW_OP_over:
			if (stidx <= 1)
				return 0;
			PUSH(stack[stidx - 2]);
			break;
		case DW_OP_pick:
			if (ptr.pu8 < end) {
				if (stidx <= *ptr.pu8)
					return 0;
				PUSH(stack[stidx - *ptr.pu8 - 1]);
			}
			++ptr.pu8;
			break;
		case DW_OP_swap:
			if (stidx <= 1)
				return 0;
			val1 = stack[stidx - 1];
			stack[stidx - 1] = stack[stidx - 2];
			stack[stidx - 2] = val1;
			break;
		case DW_OP_rot:
			if (stidx <= 2)
				return 0;
			val1 = stack[stidx - 1];
			stack[stidx - 1] = stack[stidx - 2];
			stack[stidx - 2] = stack[stidx - 3];
			stack[stidx - 3] = val1;
			break;
		case DW_OP_abs:
			PUSH(__builtin_labs(POP()));
			break;
		case DW_OP_and:
			val1 = POP();
			val2 = POP();
			PUSH(val2 & val1);
			break;
		case DW_OP_div:
			val1 = POP();
			if (!val1)
				return 0;
			val2 = POP();
			PUSH(val2 / val1);
			break;
		case DW_OP_minus:
			val1 = POP();
			val2 = POP();
			PUSH(val2 - val1);
			break;
		case DW_OP_mod:
			val1 = POP();
			if (!val1)
				return 0;
			val2 = POP();
			PUSH(val2 % val1);
			break;
		case DW_OP_mul:
			val1 = POP();
			val2 = POP();
			PUSH(val2 * val1);
			break;
		case DW_OP_neg:
			PUSH(-(long)POP());
			break;
		case DW_OP_not:
			PUSH(~POP());
			break;
		case DW_OP_or:
			val1 = POP();
			val2 = POP();
			PUSH(val2 | val1);
			break;
		case DW_OP_plus:
			val1 = POP();
			val2 = POP();
			PUSH(val2 + val1);
			break;
		case DW_OP_plus_uconst:
			PUSH(POP() + get_uleb128(&ptr.pu8, end));
			break;
		case DW_OP_shl:
			val1 = POP();
			val2 = POP();
			PUSH(val1 < BITS_PER_LONG ? val2 << val1 : 0);
			break;
		case DW_OP_shr:
			val1 = POP();
			val2 = POP();
			PUSH(val1 < BITS_PER_LONG ? val2 >> val1 : 0);
			break;
		case DW_OP_shra:
			val1 = POP();
			val2 = POP();
			if (val1 < BITS_PER_LONG)
				PUSH((long)val2 >> val1);
			else
				PUSH((long)val2 < 0 ? -1 : 0);
			break;
		case DW_OP_xor:
			val1 = POP();
			val2 = POP();
			PUSH(val2 ^ val1);
			break;
		case DW_OP_bra:
			if (!POP()) {
				++ptr.ps16;
				break;
			}
			/* fallthrough */
		case DW_OP_skip:
			if (ptr.pu8 + 1 < end) {
				ptr.pu8 += *ptr.ps16;
				if (ptr.pu8 < expr)
					return 0;
			} else
				++ptr.ps16;
			break;
		case DW_OP_eq:
			val1 = POP();
			val2 = POP();
			PUSH(val2 == val1);
			break;
		case DW_OP_ne:
			val1 = POP();
			val2 = POP();
			PUSH(val2 != val1);
			break;
		case DW_OP_lt:
			val1 = POP();
			val2 = POP();
			PUSH(val2 < val1);
			break;
		case DW_OP_le:
			val1 = POP();
			val2 = POP();
			PUSH(val2 <= val1);
			break;
		case DW_OP_ge:
			val1 = POP();
			val2 = POP();
			PUSH(val2 >= val1);
			break;
		case DW_OP_gt:
			val1 = POP();
			val2 = POP();
			PUSH(val2 > val1);
			break;
		case DW_OP_lit0 ... DW_OP_lit31:
			PUSH(ptr.pu8[-1] - DW_OP_lit0);
			break;
		case DW_OP_breg0 ... DW_OP_breg31:
			val1 = ptr.pu8[-1] - DW_OP_breg0;
			if (0)
				/* fallthrough */
		case DW_OP_bregx:
			val1 = get_uleb128(&ptr.pu8, end);
			if (val1 >= ARRAY_SIZE(reg_info) ||
					reg_info[val1].width != sizeof(unsigned long))
				return 0;

			PUSH(FRAME_REG(frame, val1, unsigned long)
					+ get_sleb128(&ptr.pu8, end));
			break;
		/*todo? case DW_OP_fbreg: */
		/*todo? case DW_OP_piece: */
		case DW_OP_nop:
			break;
		default:
			dprintk(1, "Unsupported expression op %02x (%p,%p).",
					ptr.pu8[-1], ptr.pu8 - 1, end);
			return 0;
		}
	}
	if (ptr.pu8 > end)
		return 0;
	val1 = POP();
#undef POP
#undef PUSH
	return val1;
}

/*
 * Unwind to previous to frame.  Returns 0 if successful, negative number in
 * case of an error.
 */
int dwarf_unwind(struct unwind_state *frame)
{
	const u32 *fde = NULL, *cie = NULL;
	const u8 *ptr = NULL, *end = NULL;
	unsigned long pc = UNW_PC(frame) - frame->call_frame, sp;
	unsigned long startLoc = 0, endLoc = 0, cfa;
	unsigned int i;
	int ptrType = -1;
	uleb128_t retAddrReg = 0;
	const struct unwind_table *table;
	struct dw_unwind_state state;
	int ret;

	if (UNW_PC(frame) == 0)
		return -EINVAL;
	table = find_table(pc);
	if (table != NULL && !(table->size & (sizeof(*fde) - 1))) {
		const u8 *hdr = table->header;
		unsigned long tableSize;

		smp_rmb();
		if (hdr && hdr[0] == 1) {
			switch (hdr[3] & DW_EH_PE_FORM) {
			case DW_EH_PE_native:
				tableSize = sizeof(unsigned long);
				break;
			case DW_EH_PE_data2:
				tableSize = 2;
				break;
			case DW_EH_PE_data4:
				tableSize = 4;
				break;
			case DW_EH_PE_data8:
				tableSize = 8;
				break;
			default:
				tableSize = 0;
				break;
			}
			ptr = hdr + 4;
			end = hdr + table->hdrsz;
			if (tableSize && read_pointer(&ptr, end, hdr[1], 0) ==
					(unsigned long)table->address &&
					(i = read_pointer(&ptr, end, hdr[2], 0)) > 0 &&
					i == (end - ptr) / (2 * tableSize) &&
					!((end - ptr) % (2 * tableSize))) {
				do {
					const u8 *cur = ptr +
						(i / 2) * (2 * tableSize);

					startLoc = read_pointer(&cur,
							cur + tableSize, hdr[3],
							(unsigned long)hdr);
					if (pc < startLoc)
						i /= 2;
					else {
						ptr = cur - tableSize;
						i = (i + 1) / 2;
					}
				} while (startLoc && i > 1);
				if (i == 1) {
					startLoc = read_pointer(&ptr,
							ptr + tableSize, hdr[3],
							(unsigned long)hdr);
					if (startLoc != 0 && pc >= startLoc)
						fde = (void *)read_pointer(&ptr,
							ptr + tableSize, hdr[3],
							(unsigned long)hdr);
				}
			}
		}
		if (hdr && !fde)
			dprintk(3, "Binary lookup for %lx failed.", pc);

		if (fde != NULL) {
			cie = cie_for_fde(fde, table);
			ptr = (const u8 *)(fde + 2);
			if (cie != NULL && cie != &bad_cie && cie != &not_fde &&
					(ptrType = fde_pointer_type(cie)) >= 0 &&
					read_pointer(&ptr,
						(const u8 *)(fde + 1) + *fde,
						ptrType, 0) == startLoc) {
				if (!(ptrType & DW_EH_PE_indirect))
					ptrType &= DW_EH_PE_FORM |
						DW_EH_PE_signed;
				endLoc = startLoc + read_pointer(&ptr,
						(const u8 *)(fde + 1) + *fde,
						ptrType, 0);
				if (pc >= endLoc)
					fde = NULL;
			} else
				fde = NULL;
			if (!fde)
				dprintk(1, "Binary lookup result for %lx discarded.",
						pc);
		}
		if (fde == NULL) {
			for (fde = table->address, tableSize = table->size;
					cie = NULL, tableSize > sizeof(*fde) &&
					tableSize - sizeof(*fde) >= *fde;
					tableSize -= sizeof(*fde) + *fde,
					fde += 1 + *fde / sizeof(*fde)) {
				cie = cie_for_fde(fde, table);
				if (cie == &bad_cie) {
					cie = NULL;
					break;
				}
				if (cie == NULL || cie == &not_fde)
					continue;
				ptrType = fde_pointer_type(cie);
				if (ptrType < 0)
					continue;
				ptr = (const u8 *)(fde + 2);
				startLoc = read_pointer(&ptr,
						(const u8 *)(fde + 1) + *fde,
						ptrType, 0);
				if (!startLoc)
					continue;
				if (!(ptrType & DW_EH_PE_indirect))
					ptrType &= DW_EH_PE_FORM |
						DW_EH_PE_signed;
				endLoc = startLoc + read_pointer(&ptr,
						(const u8 *)(fde + 1) + *fde,
						ptrType, 0);
				if (pc >= startLoc && pc < endLoc)
					break;
			}
			if (!fde)
				dprintk(3, "Linear lookup for %lx failed.", pc);
		}
	}
	if (cie != NULL) {
		memset(&state, 0, sizeof(state));
		state.cieEnd = ptr; /* keep here temporarily */
		ptr = (const u8 *)(cie + 2);
		end = (const u8 *)(cie + 1) + *cie;
		frame->call_frame = 1;
		state.version = *ptr;
		if (state.version != 1)
			cie = NULL; /* unsupported version */
		else if (*++ptr) {
			/*
			 * check if augmentation size is first (and thus
			 * present)
			 */
			if (*ptr == 'z') {
				while (++ptr < end && *ptr) {
					switch (*ptr) {
					/*
					 * check for ignorable (or already
					 * handled) nul-terminated augmentation
					 * string
					 */
					case 'L':
					case 'P':
					case 'R':
						continue;
					case 'S':
						frame->call_frame = 0;
						continue;
					default:
						break;
					}
					break;
				}
			}
			if (ptr >= end || *ptr)
				cie = NULL;
		}
		if (!cie)
			dprintk(1, "CIE unusable (%p,%p).", ptr, end);
		++ptr;
	}
	if (cie != NULL) {
		/* get code alignment factor */
		state.codeAlign = get_uleb128(&ptr, end);
		/* get data alignment factor */
		state.dataAlign = get_sleb128(&ptr, end);
		if (state.codeAlign == 0 || state.dataAlign == 0 || ptr >= end)
			cie = NULL;
		else if (UNW_PC(frame) % state.codeAlign ||
				UNW_SP(frame) % sleb128abs(state.dataAlign)) {
			dprintk(1, "Input pointer(s) misaligned (%lx,%lx).",
					UNW_PC(frame), UNW_SP(frame));
			return -EPERM;
		} else {
			retAddrReg = state.version <= 1 ? *ptr++ :
				get_uleb128(&ptr, end);
			/* skip augmentation */
			if (((const char *)(cie + 2))[1] == 'z') {
				uleb128_t augSize = get_uleb128(&ptr, end);

				ptr += augSize;
			}
			if (ptr > end || retAddrReg >= ARRAY_SIZE(reg_info) ||
					REG_INVALID(retAddrReg) ||
					reg_info[retAddrReg].width != sizeof(unsigned long))
				cie = NULL;
		}
		if (!cie)
			dprintk(1, "CIE validation failed (%p,%p).", ptr, end);
	}
	if (cie != NULL) {
		state.cieStart = ptr;
		ptr = state.cieEnd;
		state.cieEnd = end;
		end = (const u8 *)(fde + 1) + *fde;
		/* skip augmentation */
		if (((const char *)(cie + 2))[1] == 'z') {
			uleb128_t augSize = get_uleb128(&ptr, end);

			ptr += augSize;
			if (ptr > end)
				fde = NULL;
		}
		if (!fde)
			dprintk(1, "FDE validation failed (%p,%p).", ptr, end);
	}
	if (cie == NULL || fde == NULL) {
#ifdef CONFIG_FRAME_POINTER
		unsigned long top = TSK_STACK_TOP(frame->task);
		unsigned long bottom = STACK_BOTTOM(frame->task);
		unsigned long fp = UNW_FP(frame);
		unsigned long sp = UNW_SP(frame);
		unsigned long link;

		if ((sp | fp) & (sizeof(unsigned long) - 1))
			return -EPERM;

# if FRAME_RETADDR_OFFSET < 0
		if (!(sp < top && fp <= sp && bottom < fp))
# else
		if (!(sp > top && fp >= sp && bottom > fp))
# endif
			return -ENXIO;

		ret = probe_kernel_address((unsigned long *)(fp + FRAME_LINK_OFFSET),
				link);
		if (ret)
			return -ENXIO;

# if FRAME_RETADDR_OFFSET < 0
		if (!(link > bottom && link < fp))
# else
		if (!(link < bottom && link > fp))
# endif
			return -ENXIO;

		if (link & (sizeof(link) - 1))
			return -ENXIO;

		fp += FRAME_RETADDR_OFFSET;
		ret = probe_kernel_address((unsigned long *)fp, UNW_PC(frame));
		if (ret)
			return -ENXIO;

		/* Ok, we can use it */
# if FRAME_RETADDR_OFFSET < 0
		UNW_SP(frame) = fp - sizeof(UNW_PC(frame));
# else
		UNW_SP(frame) = fp + sizeof(UNW_PC(frame));
# endif
		UNW_FP(frame) = link;
		return 0;
#else
		return -ENXIO;
#endif
	}
	state.org = startLoc;
	memcpy(&state.cfa, &badCFA, sizeof(state.cfa));
	/* process instructions */
	if (!processCFI(ptr, end, pc, ptrType, &state)
	    || state.loc > endLoc
	    || state.regs[retAddrReg].where == Nowhere) {
		dprintk(1, "Unusable unwind info (%p,%p).", ptr, end);
		return -EIO;
	}
	if (state.cfa.elen) {
		cfa = evaluate(state.cfa.expr, state.cfa.expr + state.cfa.elen,
				frame);
		if (!cfa) {
			dprintk(1, "Bad CFA expr (%p:%lu).", state.cfa.expr,
					state.cfa.elen);
			return -EIO;
		}
	} else if (state.cfa.reg >= ARRAY_SIZE(reg_info)
		   || reg_info[state.cfa.reg].width != sizeof(unsigned long)
		   || FRAME_REG(frame, state.cfa.reg, unsigned long) % sizeof(unsigned long)
		   || state.cfa.offs % sizeof(unsigned long)) {
		dprintk(1, "Bad CFA (%lu,%lx).", state.cfa.reg, state.cfa.offs);
		return -EIO;
	} else
		cfa = FRAME_REG(frame, state.cfa.reg, unsigned long) +
			state.cfa.offs;
	/* update frame */
#ifndef CONFIG_AS_CFI_SIGNAL_FRAME
	if (frame->call_frame
	    && !UNW_DEFAULT_RA(state.regs[retAddrReg], state.dataAlign))
		frame->call_frame = 0;
#endif
	startLoc = min_t(unsigned long, UNW_SP(frame), cfa);
	endLoc = max_t(unsigned long, UNW_SP(frame), cfa);
	if (STACK_LIMIT(startLoc) != STACK_LIMIT(endLoc)) {
		startLoc = min(STACK_LIMIT(cfa), cfa);
		endLoc = max(STACK_LIMIT(cfa), cfa);
	}
#ifdef CONFIG_64BIT
# define CASES CASE(8); CASE(16); CASE(32); CASE(64)
#else
# define CASES CASE(8); CASE(16); CASE(32)
#endif
	pc = UNW_PC(frame);
	sp = UNW_SP(frame);
	for (i = 0; i < ARRAY_SIZE(state.regs); ++i) {
		if (REG_INVALID(i)) {
			if (state.regs[i].where == Nowhere)
				continue;
			dprintk(1, "Cannot restore register %u (%d).", i,
					state.regs[i].where);
			return -EIO;
		}
		switch (state.regs[i].where) {
		default:
			break;
		case Register:
			if (state.regs[i].value >= ARRAY_SIZE(reg_info)
			    || REG_INVALID(state.regs[i].value)
			    || reg_info[i].width > reg_info[state.regs[i].value].width) {
				dprintk(1, "Cannot restore register %u from register %lu.",
						i, state.regs[i].value);
				return -EIO;
			}
			switch (reg_info[state.regs[i].value].width) {
#define CASE(n) \
			case sizeof(u##n): \
				state.regs[i].value = FRAME_REG(frame, \
						state.regs[i].value, \
						const u##n); \
				break
			CASES;
#undef CASE
			default:
				dprintk(1, "Unsupported register size %u (%lu).",
						reg_info[state.regs[i].value].width,
						state.regs[i].value);
				return -EIO;
			}
			break;
		}
	}
	for (i = 0; i < ARRAY_SIZE(state.regs); ++i) {
		if (REG_INVALID(i))
			continue;
		switch (state.regs[i].where) {
		case Nowhere:
			if (reg_info[i].width != sizeof(UNW_SP(frame))
			    || &FRAME_REG(frame, i, __typeof__(UNW_SP(frame)))
			       != &UNW_SP(frame))
				continue;
			UNW_SP(frame) = cfa;
			break;
		case Register:
			switch (reg_info[i].width) {
#define CASE(n)		case sizeof(u##n): \
				FRAME_REG(frame, i, u##n) = \
					state.regs[i].value; \
				break
			CASES;
#undef CASE
			default:
				dprintk(1, "Unsupported register size %u (%u).",
						reg_info[i].width, i);
				return -EIO;
			}
			break;
		case Value:
			if (reg_info[i].width != sizeof(unsigned long)) {
				dprintk(1, "Unsupported value size %u (%u).",
						reg_info[i].width, i);
				return -EIO;
			}
			FRAME_REG(frame, i, unsigned long) = cfa +
				state.regs[i].value * state.dataAlign;
			break;
		case Memory: {
			unsigned long addr = cfa + state.regs[i].value
						   * state.dataAlign;

			if ((state.regs[i].value * state.dataAlign)
			    % sizeof(unsigned long)
			    || addr < startLoc
			    || addr + sizeof(unsigned long) < addr
			    || addr + sizeof(unsigned long) > endLoc) {
				dprintk(1, "Bad memory location %lx (%lx).",
					addr, state.regs[i].value);
				return -EIO;
			}
			switch (reg_info[i].width) {
#define CASE(n)		case sizeof(u##n): \
				ret = probe_kernel_address((unsigned long *)addr, \
						FRAME_REG(frame, i, u##n)); \
				if (ret) \
					return -EFAULT; \
				break
			CASES;
#undef CASE
			default:
				dprintk(1, "Unsupported memory size %u (%u).",
					reg_info[i].width, i);
				return -EIO;
			}
			break;
		}
		}
	}

	if (UNW_PC(frame) % state.codeAlign ||
			UNW_SP(frame) % sleb128abs(state.dataAlign)) {
		dprintk(1, "Output pointer(s) misaligned (%lx,%lx).",
				UNW_PC(frame), UNW_SP(frame));
		return -EIO;
	}
	if (pc == UNW_PC(frame) && sp == UNW_SP(frame)) {
		dprintk(1, "No progress (%lx,%lx).", pc, sp);
		return -EIO;
	}

	return 0;
#undef CASES
}
EXPORT_SYMBOL_GPL(dwarf_unwind);

#if 0
/*
 * Unwind until the return pointer is in user-land (or until an error
 * occurs).  Returns 0 if successful, negative number in case of
 * error.
 */
int unwind_to_user(struct unwind_state *info)
{
	while (!arch_unw_user_mode(info)) {
		int err = unwind(info);

		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(unwind_to_user);
#endif
