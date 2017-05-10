/*
 * Copyright (C) 2002-2017 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 *	Jiri Slaby <jirislaby@kernel.org>
 * This code is released under version 2 of the GNU GPL.
 *
 * Simple DWARF unwinder for kernel stacks. This is used for debugging and
 * error reporting purposes. The kernel does not need full-blown stack
 * unwinding with all the bells and whistles, so there is not much point in
 * implementing the full DWARF2 API.
 */

#include <linux/bootmem.h>
#include <linux/dwarf.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/stop_machine.h>
#include <linux/uaccess.h>

#include <asm/sections.h>
#include <asm/unaligned.h>

#if 0
#define DW_NOINLINE noinline
#else
#define DW_NOINLINE
#endif

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
	DW_REGISTER_INFO
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

#define DW_CFA_advance_loc			0x40
#define DW_CFA_offset				0x80
#define DW_CFA_restore				0xc0
#define DW_CFA_ENCODED_MASK			0xc0
#define DW_CFA_ENCODED_OP			(~DW_CFA_ENCODED_MASK)

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

static struct dwarf_table {
	struct {
		unsigned long pc;
		unsigned long range;
	} core, init;
	const void *address;
	unsigned long size;
	const unsigned char *header;
	unsigned long hdrsz;
	struct dwarf_table *link;
	const char *name;
} root_table;

union dw_uni_ptr {
	const u8 *pu8;
	const s8 *ps8;
	const u16 *pu16;
	const s16 *ps16;
	const u32 *pu32;
	const s32 *ps32;
	const u64 *pu64;
	const s64 *ps64;
	const unsigned long *pul;
};

struct dw_unwind_state {
	uleb128_t loc, orig_loc;

	struct {
		const u8 *ins_start, *ins_end;

		uleb128_t code_align, ret_addr_reg;
		sleb128_t data_align;
		bool is_signal_frame, has_aug_data;
		u8 ptr_type;
	} cie;

	struct {
		unsigned long pc_begin, pc_end;
		const u8 *ins_start, *ins_end;
	} fde;

	struct cfa {
		uleb128_t reg, offs, elen;
		const u8 *expr;
	} cfa;
	struct {
		enum dw_item_location {
			NOWHERE,
			MEMORY,
			REGISTER,
			VALUE,
		} where;
		uleb128_t value;
	} regs[ARRAY_SIZE(reg_info)];
	const u8 *label;
	const u8 *stack[MAX_STACK_DEPTH];
	u8 stackDepth;
};

static const struct cfa badCFA = {
	.reg = ARRAY_SIZE(reg_info),
	.offs = 1,
};

static unsigned int unwind_debug;
static int __init unwind_debug_setup(char *s)
{
	if (kstrtouint(s, 0, &unwind_debug))
		return 1;
	return 0;
}
early_param("unwind_debug", unwind_debug_setup);

#define dprintk(lvl, fmt, args...) do {				\
	if (unwind_debug >= lvl)					\
		printk(KERN_DEBUG "unwind: " fmt "\n", ##args);	\
} while (0)

static struct dwarf_table *dw_find_table(unsigned long pc)
{
	struct dwarf_table *table;

	for (table = &root_table; table; table = table->link)
		if ((pc >= table->core.pc &&
				 pc < table->core.pc + table->core.range) ||
				(pc >= table->init.pc &&
				 pc < table->init.pc + table->init.range))
			break;

	return table;
}

static unsigned long dw_read_pointer(const u8 **pLoc,
				  const void *end,
				  int ptrType,
				  unsigned long data_base);

static void __init_or_module
init_unwind_table(struct dwarf_table *table, const char *name,
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
	table->hdrsz = header_size;
	table->link = NULL;
	table->name = name;

	/* See if the linker provided table looks valid. */
	if (header_size <= 4 || header_start[0] != 1 ||
			/* ptr to eh_frame */
			(void *)dw_read_pointer(&ptr, end, header_start[1], 0) !=
				table_start ||
			/* fde count */
			!dw_read_pointer(&ptr, end, header_start[2], 0) ||
			/* table[0] -- initial location */
			!dw_read_pointer(&ptr, end, header_start[3],
				(unsigned long)header_start) ||
			/* table[0] -- address */
			!dw_read_pointer(&ptr, end, header_start[3],
				(unsigned long)header_start))
		header_start = NULL;

	smp_wmb(); /* counterpart in dwarf_unwind */
	table->header = header_start;
}

/*
 * dwarf_init -- initialize unwind support
 */
void __init dwarf_init(void)
{
	extern const char __start_unwind[], __end_unwind[];
	extern const char __start_unwind_hdr[], __end_unwind_hdr[];
#ifdef CONFIG_DEBUG_RODATA
	unsigned long text_len = _etext - _text;
	const void *init_start = __init_begin;
	unsigned long init_len = __init_end - __init_begin;
#else
	unsigned long text_len = _end - _text;
	const void *init_start = NULL;
	unsigned long init_len = 0;
#endif
	init_unwind_table(&root_table, "kernel", _text, text_len,
			init_start, init_len,
			__start_unwind, __end_unwind - __start_unwind,
			__start_unwind_hdr,
			__end_unwind_hdr - __start_unwind_hdr);
}

static const u32 *cie_for_fde(const u32 *fde, const struct dwarf_table *table,
		unsigned long *start_loc, struct dw_unwind_state *state);

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
setup_unwind_table(struct dwarf_table *table,
		   void *(*alloc)(size_t, gfp_t))
{
	unsigned long tableSize = table->size, hdrSize, start_loc;
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
		const u32 *cie = cie_for_fde(fde, table, &start_loc, NULL);

		if (IS_ERR(cie)) {
			if (PTR_ERR(cie) == -ENODEV)
				continue;
			return;
		}

		if (!start_loc)
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
	header->eh_frame_ptr_enc = DW_EH_PE_abs | DW_EH_PE_native;
	header->fde_count_enc = DW_EH_PE_abs | DW_EH_PE_data4;
	header->table_enc = DW_EH_PE_abs | DW_EH_PE_native;
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
		const u32 *cie = cie_for_fde(fde, table, &start_loc, NULL);
		if (PTR_ERR(cie) == -ENODEV)
			continue;

		header->table[n].start = start_loc;
		header->table[n].fde = (unsigned long)fde;
		++n;
	}
	WARN_ON(n != header->fde_count);

	sort(header->table, n, sizeof(*header->table),
			cmp_eh_frame_hdr_table_entries,
			swap_eh_frame_hdr_table_entries);

	table->hdrsz = hdrSize;
	smp_wmb(); /* counterpart in dwarf_unwind */
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

static struct dwarf_table *last_table = &root_table;

/*
 * dwarf_add_table -- insert a dwarf table for a module
 *
 * Must be called with module_mutex held.
 */
struct dwarf_table *dwarf_add_table(struct module *module,
		const void *table_start, unsigned long table_size)
{
	struct dwarf_table *table;
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

	last_table->link = table;
	last_table = table;

	return table;
}

struct unlink_table_info {
	struct dwarf_table *table;
	bool init_only;
};

static int unlink_table(void *arg)
{
	struct unlink_table_info *info = arg;
	struct dwarf_table *table = info->table, *prev;

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

/*
 * dwarf_remove_table -- remove a dwarf table for a module
 *
 * Must be called with module_mutex held.
 */
void dwarf_remove_table(struct dwarf_table *table, bool init_only)
{
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

static int dw_parse_cie(const u32 *cie, struct dw_unwind_state *state)
{
	const u8 *ptr = (const u8 *)(cie + 2); /* skip len+ID */
	const u8 *end = (const u8 *)(cie + 1) + *cie, *aug_data_end;
	const char *aug = NULL;
	unsigned int version = *ptr++;
	int ptr_type = DW_EH_PE_native | DW_EH_PE_abs;
	uleb128_t code_align, ret_addr_reg, len;
	sleb128_t data_align;
	bool signal_frame = false;

	if (version != 1)
		return -1;

	if (*ptr) {
		aug = (const char *)ptr;

		/* check if augmentation string is nul-terminated */
		ptr = memchr(aug, 0, end - ptr);
		if (ptr == NULL)
			return -1;

		/* check if augmentation size is first (and thus present) */
		if (*aug != 'z')
			return -1;
	} else if (!state)
		goto finish;

	ptr++; /* skip terminator */
	code_align = get_uleb128(&ptr, end);
	data_align = get_sleb128(&ptr, end);
	ret_addr_reg = version <= 1 ? *ptr++ : get_uleb128(&ptr, end);

	if (aug) {
		len = get_uleb128(&ptr, end); /* augmentation length */
		if (ptr + len < ptr || ptr + len > end)
			return -1;

		aug_data_end = ptr + len;
		while (*++aug) {
			if (ptr >= aug_data_end)
				return -1;
			switch (*aug) {
			case 'L':
				++ptr;
				break;
			case 'P': {
				int ptr_type = *ptr++;

				if (!dw_read_pointer(&ptr, aug_data_end,
							ptr_type, 0) ||
						ptr > aug_data_end)
					return -1;
				break;
			}
			case 'R':
				ptr_type = *ptr;
				break;
			case 'S':
				signal_frame = true;
				break;
			default:
				pr_info("%s: unhandled AUG %c\n", __func__,
						*aug);
				return -1;
			}
		}
	} else
		aug_data_end = ptr;

	if (state) {
		state->cie.ins_start = aug_data_end;
		state->cie.ins_end = end;

		state->cie.code_align = code_align;
		state->cie.data_align = data_align;
		state->cie.ret_addr_reg = ret_addr_reg;
		state->cie.is_signal_frame = signal_frame;
		state->cie.has_aug_data = aug;
	}
finish:
	return ptr_type;
}

static const u32 *cie_for_fde(const u32 *fde, const struct dwarf_table *table,
		unsigned long *start_loc, struct dw_unwind_state *state)
{
	const u32 *cie;
	int ptr_type;

	/* no len or odd len? */
	if (!*fde || (*fde & (sizeof(*fde) - 1)))
		return ERR_PTR(-EBADF);

	/* a CIE? */
	if (!fde[1])
		return ERR_PTR(-ENODEV);

	/* invalid pointer? */
	if (fde[1] & (sizeof(*fde) - 1))
		return ERR_PTR(-EINVAL);

	/* out of range? */
	if (fde[1] > (unsigned long)(fde + 1) - (unsigned long)table->address)
		return ERR_PTR(-EINVAL);

	cie = fde + 1 - fde[1] / sizeof(*fde);

	/* invalid CIE? */
	if (*cie <= sizeof(*cie) + 4 || *cie >= fde[1] - sizeof(*fde) ||
			(*cie & (sizeof(*cie) - 1)) || cie[1])
		return ERR_PTR(-EINVAL);

	ptr_type = dw_parse_cie(cie, state);
	if (ptr_type < 0)
		return ERR_PTR(-EINVAL);

	if (start_loc) {
		const u8 *ptr = (const u8 *)(fde + 2);
		const u8 *end = (const u8 *)(fde + 1) + *fde;
		/* PC begin */
		*start_loc = dw_read_pointer(&ptr, end, ptr_type, 0);

		/* force absolute type for PC range */
		if (!(ptr_type & DW_EH_PE_indirect))
			ptr_type &= DW_EH_PE_FORM | DW_EH_PE_signed;

		if (state) {
			/* PC range */
			state->fde.pc_end = *start_loc +
				dw_read_pointer(&ptr, end, ptr_type, 0);

			/* skip augmentation */
			if (state->cie.has_aug_data) {
				uleb128_t aug_len = get_uleb128(&ptr, end);

				ptr += aug_len;
				if (ptr > end)
					return ERR_PTR(-EINVAL);
			}

			state->fde.ins_start = ptr;
			state->fde.ins_end = end;
			state->cie.ptr_type = ptr_type;
		}
	}

	return cie;
}

static unsigned long dw_read_pointer(const u8 **pLoc, const void *end,
		int ptrType, unsigned long data_base)
{
	unsigned long value = 0;
	union dw_uni_ptr ptr;

	if (ptrType < 0 || ptrType == DW_EH_PE_omit) {
		dprintk(1, "Invalid pointer encoding %02X (%p,%p).", ptrType,
				*pLoc, end);
		return 0;
	}
	ptr.pu8 = *pLoc;
	switch (ptrType & DW_EH_PE_FORM) {
	case DW_EH_PE_data2:
		if (end < (const void *)(ptr.pu16 + 1)) {
			dprintk(1, "Data16 overrun (%p,%p).", ptr.pu8, end);
			return 0;
		}
		if (ptrType & DW_EH_PE_signed)
			value = get_unaligned(ptr.ps16++);
		else
			value = get_unaligned(ptr.pu16++);
		break;
	case DW_EH_PE_data4:
#ifdef CONFIG_64BIT
		if (end < (const void *)(ptr.pu32 + 1)) {
			dprintk(1, "Data32 overrun (%p,%p).", ptr.pu8, end);
			return 0;
		}
		if (ptrType & DW_EH_PE_signed)
			value = get_unaligned(ptr.ps32++);
		else
			value = get_unaligned(ptr.pu32++);
		break;
	case DW_EH_PE_data8:
		BUILD_BUG_ON(sizeof(u64) != sizeof(value));
#else
		BUILD_BUG_ON(sizeof(u32) != sizeof(value));
#endif
		/* fallthrough */
	case DW_EH_PE_native:
		if (end < (const void *)(ptr.pul + 1)) {
			dprintk(1, "DataUL overrun (%p,%p).", ptr.pu8, end);
			return 0;
		}
		value = get_unaligned(ptr.pul++);
		break;
	case DW_EH_PE_leb128:
		BUILD_BUG_ON(sizeof(uleb128_t) > sizeof(value));
		if (ptrType & DW_EH_PE_signed)
			value = get_sleb128(&ptr.pu8, end);
		else
			value = get_uleb128(&ptr.pu8, end);
		if ((const void *)ptr.pu8 > end) {
			dprintk(1, "DataLEB overrun (%p,%p).", ptr.pu8, end);
			return 0;
		}
		break;
	default:
		dprintk(2, "Cannot decode pointer type %02X (%p,%p).",
				ptrType, ptr.pu8, end);
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
	*pLoc = ptr.pu8;

	return value;
}

static bool dw_advance_loc(unsigned long delta, struct dw_unwind_state *state)
{
	state->loc += delta * state->cie.code_align;

	return delta > 0;
}

static void dw_set_rule(uleb128_t reg, enum dw_item_location where,
		uleb128_t value, struct dw_unwind_state *state)
{
	if (reg < ARRAY_SIZE(state->regs)) {
		state->regs[reg].where = where;
		state->regs[reg].value = value;
	}
}

static bool dw_process_CFI_encoded(struct dw_unwind_state *state,
		union dw_uni_ptr *ptr, const u8 *end, u8 inst)
{
	u8 op = inst & DW_CFA_ENCODED_OP;

	switch (inst & DW_CFA_ENCODED_MASK) {
	case DW_CFA_advance_loc:
		return dw_advance_loc(op, state);
	case DW_CFA_offset:
		dw_set_rule(op, MEMORY, get_uleb128(&ptr->pu8, end), state);
		break;
	case DW_CFA_restore:
		dw_set_rule(op, NOWHERE, 0, state);
		break;
	}

	return true;
}

static bool dw_process_CFIs(struct dw_unwind_state *state, unsigned long pc);

static int dw_process_CFI_normal(struct dw_unwind_state *state,
		union dw_uni_ptr *ptr, const u8 *end, u8 inst)
{
	uleb128_t value;
	bool ok = true;

	switch (inst) {
	case DW_CFA_nop:
		break;
	case DW_CFA_set_loc:
		state->loc = dw_read_pointer(&ptr->pu8, end,
				state->cie.ptr_type, 0);
		if (state->loc == 0)
			return false;
		break;
	case DW_CFA_advance_loc1:
		return ptr->pu8 < end && dw_advance_loc(*ptr->pu8++, state);
	case DW_CFA_advance_loc2:
		return ptr->pu8 <= end + 2 &&
			dw_advance_loc(*ptr->pu16++, state);
	case DW_CFA_advance_loc4:
		return ptr->pu8 <= end + 4 &&
			dw_advance_loc(*ptr->pu32++, state);
	case DW_CFA_offset_extended:
		value = get_uleb128(&ptr->pu8, end);
		dw_set_rule(value, MEMORY, get_uleb128(&ptr->pu8, end), state);
		break;
	case DW_CFA_val_offset:
		value = get_uleb128(&ptr->pu8, end);
		dw_set_rule(value, VALUE, get_uleb128(&ptr->pu8, end), state);
		break;
	case DW_CFA_offset_extended_sf:
		value = get_uleb128(&ptr->pu8, end);
		dw_set_rule(value, MEMORY, get_sleb128(&ptr->pu8, end), state);
		break;
	case DW_CFA_val_offset_sf:
		value = get_uleb128(&ptr->pu8, end);
		dw_set_rule(value, VALUE, get_sleb128(&ptr->pu8, end), state);
		break;
	/*todo case DW_CFA_expression: */
	/*todo case DW_CFA_val_expression: */
	case DW_CFA_restore_extended:
	case DW_CFA_undefined:
	case DW_CFA_same_value:
		dw_set_rule(get_uleb128(&ptr->pu8, end), NOWHERE, 0, state);
		break;
	case DW_CFA_register:
		value = get_uleb128(&ptr->pu8, end);
		dw_set_rule(value, REGISTER, get_uleb128(&ptr->pu8, end),
				state);
		break;
	case DW_CFA_remember_state:
		if (ptr->pu8 == state->label) {
			state->label = NULL;
			/* required state */
			return 2;
		}
		if (state->stackDepth >= MAX_STACK_DEPTH) {
			dprintk(1, "State stack overflow (%p, %p).", ptr->pu8,
					end);
			return false;
		}
		state->stack[state->stackDepth++] = ptr->pu8;
		break;
	case DW_CFA_restore_state: {
		const uleb128_t loc = state->loc;
		const u8 *label = state->label;

		if (!state->stackDepth) {
			dprintk(1, "State stack underflow (%p, %p).", ptr->pu8,
					end);
			return false;
		}

		state->label = state->stack[state->stackDepth - 1];
		memcpy(&state->cfa, &badCFA, sizeof(badCFA));
		memset(state->regs, 0, sizeof(state->regs));
		state->stackDepth = 0;
		ok = dw_process_CFIs(state, 0);
		state->loc = loc;
		state->label = label;

		break;
	}
	case DW_CFA_def_cfa:
		state->cfa.reg = get_uleb128(&ptr->pu8, end);
		state->cfa.elen = 0;
		/* fallthrough */
	case DW_CFA_def_cfa_offset:
		state->cfa.offs = get_uleb128(&ptr->pu8, end);
		break;
	case DW_CFA_def_cfa_sf:
		state->cfa.reg = get_uleb128(&ptr->pu8, end);
		state->cfa.elen = 0;
		/* fallthrough */
	case DW_CFA_def_cfa_offset_sf:
		state->cfa.offs = get_sleb128(&ptr->pu8, end) *
			state->cie.data_align;
		break;
	case DW_CFA_def_cfa_register:
		state->cfa.reg = get_uleb128(&ptr->pu8, end);
		state->cfa.elen = 0;
		break;
	case DW_CFA_def_cfa_expression:
		state->cfa.elen = get_uleb128(&ptr->pu8, end);
		if (!state->cfa.elen) {
			dprintk(1, "Zero-length CFA expression.");
			return false;
		}
		state->cfa.expr = ptr->pu8;
		ptr->pu8 += state->cfa.elen;
		break;
	case DW_CFA_GNU_args_size:
		get_uleb128(&ptr->pu8, end);
		break;
	case DW_CFA_GNU_negative_offset_extended:
		value = get_uleb128(&ptr->pu8, end);
		dw_set_rule(value, MEMORY,
				(uleb128_t)0 - get_uleb128(&ptr->pu8, end),
				state);
		break;
	case DW_CFA_GNU_window_save:
	default:
		dprintk(1, "Unrecognized CFI op %02X (%p, %p).", ptr->pu8[-1],
				ptr->pu8 - 1, end);
		return false;
	}

	return ok;
}

static bool dw_process_CFI(struct dw_unwind_state *state, const u8 *start,
		const u8 *end, unsigned long pc)
{
	union dw_uni_ptr ptr;
	int res;

	dprintk(4, "processing CFI: %p-%p", start, end);

	for (ptr.pu8 = start; ptr.pu8 < end; ) {
		u8 inst = *ptr.pu8++;

		if (inst & DW_CFA_ENCODED_MASK)
			res = dw_process_CFI_encoded(state, &ptr, end, inst);
		else {
			res = dw_process_CFI_normal(state, &ptr, end, inst);
			if (res == 2)
				return true;
		}

		if (!res)
			return false;

		if (ptr.pu8 > end) {
			dprintk(1, "Data overrun (%p, %p).", ptr.pu8, end);
			return false;
		}
		if (pc != 0 && pc < state->loc)
			return true;
	}

	if (ptr.pu8 < end) {
		dprintk(1, "Data underrun (%p, %p).", ptr.pu8, end);
		return false;
	}

	if (ptr.pu8 != end) {
		pr_err("%s: Data mismatch (%p, %p).\n", __func__, ptr.pu8, end);
		return false;
	}

	return (pc == 0 ||
		(/*
		  * TODO While in theory this should apply, gcc in practice
		  * omits everything past the function prolog, and hence the
		  * location never reaches the end of the function.
		  *    pc < state->loc &&
		  */
		 state->label == NULL));
}

static bool dw_process_CFIs(struct dw_unwind_state *state, unsigned long pc)
{
	bool ok = true;

	state->loc = state->orig_loc;

	ok = dw_process_CFI(state, state->cie.ins_start, state->cie.ins_end, 0);
	if (pc == 0 && state->label == NULL)
		return ok;
	if (!ok)
		return false;

	return dw_process_CFI(state, state->fde.ins_start, state->fde.ins_end,
			pc);
}

static unsigned long dw_evaluate_cfa(const u8 *expr, const u8 *end,
			      const struct unwind_state *frame)
{
	union dw_uni_ptr ptr = { expr };
	unsigned long stack[8], val1, val2;
	unsigned int stidx = 0;
	int ret;

#define PUSH(v) do {							\
	unsigned long v__ = (v);					\
									\
	if (stidx >= ARRAY_SIZE(stack))					\
		return 0;						\
	stack[stidx++] = v__;						\
} while (0)

#define POP() ({							\
	if (!stidx)							\
		return 0;						\
	stack[--stidx];							\
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

static unsigned long dw_get_cfa(const struct unwind_state *frame,
		const struct dw_unwind_state *state)
{
	if (state->cfa.elen) {
		unsigned long cfa = dw_evaluate_cfa(state->cfa.expr,
				state->cfa.expr + state->cfa.elen, frame);
		if (!cfa)
			dprintk(1, "Bad CFA expr (%p:%lu).", state->cfa.expr,
					state->cfa.elen);

		return cfa;
	}

	if (state->cfa.reg >= ARRAY_SIZE(reg_info) ||
			reg_info[state->cfa.reg].width !=
					sizeof(unsigned long) ||
			FRAME_REG(frame, state->cfa.reg, unsigned long) %
					sizeof(unsigned long) ||
			state->cfa.offs % sizeof(unsigned long)) {
		dprintk(1, "Bad CFA (%lu, %lx).", state->cfa.reg,
				state->cfa.offs);
		return 0;
	}

	return FRAME_REG(frame, state->cfa.reg, unsigned long) +
		state->cfa.offs;
}

static int dw_update_frame(struct unwind_state *frame,
		struct dw_unwind_state *state)
{
	unsigned long start_loc, end_loc, sp, pc, cfa;
	unsigned int i;
	int ret;

	cfa = dw_get_cfa(frame, state);
	if (!cfa)
		return -EIO;

#ifndef CONFIG_AS_CFI_SIGNAL_FRAME
	if (frame->call_frame &&
			!UNW_DEFAULT_RA(state->regs[state->cie.ret_addr_reg],
					state->cie.data_align))
		frame->call_frame = 0;
#endif
	start_loc = min_t(unsigned long, DW_SP(frame), cfa);
	end_loc = max_t(unsigned long, DW_SP(frame), cfa);
	if (STACK_LIMIT(start_loc) != STACK_LIMIT(end_loc)) {
		start_loc = min(STACK_LIMIT(cfa), cfa);
		end_loc = max(STACK_LIMIT(cfa), cfa);
	}

#ifdef CONFIG_64BIT
# define CASES CASE(8); CASE(16); CASE(32); CASE(64)
#else
# define CASES CASE(8); CASE(16); CASE(32)
#endif
	pc = DW_PC(frame);
	sp = DW_SP(frame);

	/* 1st step: store the computed register values */
	for (i = 0; i < ARRAY_SIZE(state->regs); ++i) {
		if (REG_INVALID(i)) {
			if (state->regs[i].where == NOWHERE)
				continue;
			dprintk(1, "Cannot restore register %u (%d).", i,
					state->regs[i].where);
			return -EIO;
		}

		if (state->regs[i].where != REGISTER)
			continue;

		if (state->regs[i].value >= ARRAY_SIZE(reg_info) ||
				REG_INVALID(state->regs[i].value) ||
				reg_info[i].width > reg_info[state->regs[i].value].width) {
			dprintk(1, "Cannot restore register %u from register %lu.",
					i, state->regs[i].value);
			return -EIO;
		}

		switch (reg_info[state->regs[i].value].width) {
#define CASE(n)	case sizeof(u##n):					\
			state->regs[i].value = FRAME_REG(frame, 	\
					state->regs[i].value, const u##n); \
			break
		CASES;
#undef CASE
		default:
			dprintk(1, "Unsupported register size %u (%lu).",
					reg_info[state->regs[i].value].width,
					state->regs[i].value);
			return -EIO;
		}
	}

	/* 2nd step: update the state, incl. registers */
	for (i = 0; i < ARRAY_SIZE(state->regs); ++i) {
		if (REG_INVALID(i))
			continue;
		switch (state->regs[i].where) {
		case NOWHERE:
			if (reg_info[i].width != sizeof(DW_SP(frame))
			    || &FRAME_REG(frame, i, __typeof__(DW_SP(frame)))
			       != &DW_SP(frame))
				continue;
			DW_SP(frame) = cfa;
			break;
		case REGISTER:
			switch (reg_info[i].width) {
#define CASE(n)		case sizeof(u##n):				\
				FRAME_REG(frame, i, u##n) =		\
					state->regs[i].value;		\
				break
			CASES;
#undef CASE
			default:
				dprintk(1, "Unsupported register size %u (%u).",
						reg_info[i].width, i);
				return -EIO;
			}
			break;
		case VALUE:
			if (reg_info[i].width != sizeof(unsigned long)) {
				dprintk(1, "Unsupported value size %u (%u).",
						reg_info[i].width, i);
				return -EIO;
			}
			FRAME_REG(frame, i, unsigned long) = cfa +
				state->regs[i].value * state->cie.data_align;
			break;
		case MEMORY: {
			unsigned long off = state->regs[i].value *
				state->cie.data_align;
			unsigned long addr = cfa + off;

			if (off % sizeof(unsigned long) ||
					addr < start_loc ||
					addr + sizeof(unsigned long) < addr ||
					addr + sizeof(unsigned long) > end_loc) {
				dprintk(1, "Bad memory location %lx (%lx).",
					addr, state->regs[i].value);
				return -EIO;
			}

			switch (reg_info[i].width) {
#define CASE(n)		case sizeof(u##n):				\
				ret = probe_kernel_address((void *)addr, \
						FRAME_REG(frame, i, u##n)); \
				if (ret)				\
					return -EFAULT;			\
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

	if (DW_PC(frame) % state->cie.code_align ||
			DW_SP(frame) % sleb128abs(state->cie.data_align)) {
		dprintk(1, "Output pointer(s) misaligned (%lx,%lx).",
				DW_PC(frame), DW_SP(frame));
		return -EIO;
	}

	if (pc == DW_PC(frame) && sp == DW_SP(frame)) {
		dprintk(1, "No progress (%lx,%lx).", pc, sp);
		return -EIO;
	}

	return 0;
#undef CASES
}

static DW_NOINLINE int
dw_lookup_fde_binary(struct unwind_state *frame,
		const struct dwarf_table *table, unsigned long pc,
		struct dw_unwind_state *state)
{
	unsigned long tableSize, pc_begin = 0;
	unsigned int fde_cnt;
	const u32 *fde = NULL, *cie;
	const u8 *hdr = table->header;
	const u8 *ptr, *end;

	/* version */
	if (hdr[0] != 1)
		goto failed;

	/* table_enc */
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
		goto failed;
	}

	ptr = hdr + 4;
	end = hdr + table->hdrsz;

	/* eh_frame_ptr */
	if (dw_read_pointer(&ptr, end, hdr[1], 0) !=
			(unsigned long)table->address)
		goto failed;

	/* fde_cnt */
	fde_cnt = dw_read_pointer(&ptr, end, hdr[2], 0);
	if (fde_cnt == 0 || fde_cnt != (end - ptr) / (2 * tableSize))
		goto failed;

	if ((end - ptr) % (2 * tableSize))
		goto failed;

	/* binary search in fde_cnt entries */
	do {
		const u8 *cur = ptr + (fde_cnt / 2) * (2 * tableSize);

		/* location */
		pc_begin = dw_read_pointer(&cur, cur + tableSize, hdr[3],
				(unsigned long)hdr);
		if (pc < pc_begin)
			fde_cnt /= 2;
		else {
			ptr = cur - tableSize;
			fde_cnt = (fde_cnt + 1) / 2;
		}
	} while (pc_begin && fde_cnt > 1);

	if (pc_begin == 0 || fde_cnt != 1)
		goto failed;

	/* read the bisected one -- location... */
	pc_begin = dw_read_pointer(&ptr, ptr + tableSize, hdr[3],
			(unsigned long)hdr);
	if (pc_begin == 0 || pc < pc_begin)
		goto failed;

	/* ...and its corresponding fde */
	fde = (void *)dw_read_pointer(&ptr, ptr + tableSize, hdr[3],
			(unsigned long)hdr);
	if (!fde)
		goto failed;

	dprintk(4, "have fde: %zx at %p", (void *)fde - table->address, fde);

	/* now find a cie for the fde */

	cie = cie_for_fde(fde, table, &state->fde.pc_begin, state);
	if (IS_ERR(cie) || state->fde.pc_begin != pc_begin ||
			pc >= state->fde.pc_end)
		goto discard;

	dprintk(4, "still have fde at %p", fde);

	return 0;
discard:
	dprintk(1, "Binary lookup result for %lx discarded.", pc);
	return -EIO;
failed:
	if (hdr)
		dprintk(3, "Binary lookup for %lx failed.", pc);
	return -EIO;
}

static DW_NOINLINE int
dw_lookup_fde_linear(struct unwind_state *frame,
		const struct dwarf_table *table, unsigned long pc,
		struct dw_unwind_state *state)
{
	unsigned long tableSize = table->size;
	const u32 *fde;

	for (fde = table->address; tableSize > sizeof(*fde) &&
			tableSize - sizeof(*fde) >= *fde;
			tableSize -= sizeof(*fde) + *fde,
			fde += 1 + *fde / sizeof(*fde)) {
		const u32 *cie = cie_for_fde(fde, table, &state->fde.pc_begin,
				state);
		if (IS_ERR(cie)) {
			if (PTR_ERR(cie) == -EBADF)
				break;
			continue;
		}

		if (!state->fde.pc_begin)
			continue;

		if (pc >= state->fde.pc_begin && pc < state->fde.pc_end)
			return 0;
	}

	dprintk(3, "Linear lookup for %lx failed.", pc);

	return -ENOENT;
}


#ifdef CONFIG_FRAME_POINTER
static DW_NOINLINE int dw_fp_fallback(struct unwind_state *frame)
{
	unsigned long top = TSK_STACK_TOP(frame->task);
	unsigned long bottom = STACK_BOTTOM(frame->task);
	unsigned long fp = DW_FP(frame);
	unsigned long sp = DW_SP(frame);
	unsigned long link;
	int ret;

	if ((sp | fp) & (sizeof(unsigned long) - 1))
		return -EPERM;

#if FRAME_RETADDR_OFFSET < 0
	if (!(sp < top && fp <= sp && bottom < fp))
#else
	if (!(sp > top && fp >= sp && bottom > fp))
#endif
		return -ENXIO;

	ret = probe_kernel_address((unsigned long *)(fp + FRAME_LINK_OFFSET),
			link);
	if (ret)
		return -ENXIO;

#if FRAME_RETADDR_OFFSET < 0
	if (!(link > bottom && link < fp))
#else
	if (!(link < bottom && link > fp))
#endif
		return -ENXIO;

	if (link & (sizeof(link) - 1))
		return -ENXIO;

	fp += FRAME_RETADDR_OFFSET;
	ret = probe_kernel_address((unsigned long *)fp, DW_PC(frame));
	if (ret)
		return -ENXIO;
	pr_info("%s: read fp=%lx into PC=%lx\n", __func__,
			fp, DW_PC(frame));

	/* Ok, we can use it */
#if FRAME_RETADDR_OFFSET < 0
	DW_SP(frame) = fp - sizeof(DW_PC(frame));
#else
	DW_SP(frame) = fp + sizeof(DW_PC(frame));
#endif
	DW_FP(frame) = link;
	return 0;
}
#else
static inline int dw_fp_fallback(struct unwind_state *frame)
{
	return -ENXIO;
}
#endif

/*
 * dwarf_unwind -- unwind to previous to frame
 *
 * Returns 0 if successful, negative number in case of an error.
 */
int dwarf_unwind(struct unwind_state *frame)
{
	struct dw_unwind_state state = {
		.cie.ptr_type = -1,
		.cfa = badCFA,
	};
	const struct dwarf_table *table;
	unsigned long pc = DW_PC(frame) - frame->call_frame;
	int ret;

	if (DW_PC(frame) == 0)
		return -EINVAL;

	table = dw_find_table(pc);
	if (table == NULL || (table->size & (sizeof(u32) - 1)))
		goto fallback;

	/* counterpart in setup_unwind_table, to protect table->header */
	smp_rmb();

	dprintk(4, "have table: %lx-%lx (%s) hdr=%p addr=%p for %pS",
			table->core.pc,
			table->core.pc + table->core.range - 1,
			table->name,
			table->header, table->address, (void *)pc);

	if (table->header)
		ret = dw_lookup_fde_binary(frame, table, pc, &state);
	else
		ret = dw_lookup_fde_linear(frame, table, pc, &state);

	if (ret)
		goto fallback;

	if (state.cie.code_align == 0 || state.cie.data_align == 0)
		goto fallback;

	if (DW_PC(frame) % state.cie.code_align ||
			DW_SP(frame) % sleb128abs(state.cie.data_align)) {
		dprintk(1, "Input pointer(s) misaligned (%lx, %lx).",
				DW_PC(frame), DW_SP(frame));
		return -EPERM;
	}

	if (state.cie.ret_addr_reg >= ARRAY_SIZE(reg_info) ||
			REG_INVALID(state.cie.ret_addr_reg) ||
			reg_info[state.cie.ret_addr_reg].width !=
				sizeof(unsigned long)) {
		dprintk(1, "CIE validation failed (ret addr reg).");
		goto fallback;
	}

	frame->call_frame = !state.cie.is_signal_frame;
	state.orig_loc = state.fde.pc_begin;

	/* process instructions */
	ret = dw_process_CFIs(&state, pc);
	if (!ret || state.loc > state.fde.pc_end ||
			state.regs[state.cie.ret_addr_reg].where == NOWHERE) {
		dprintk(1, "Unusable unwind info (%p, %p).",
				state.fde.ins_start, state.fde.ins_end);
		return -EIO;
	}

	ret = dw_update_frame(frame, &state);
	if (ret)
		return ret;

	return 0;
fallback:
	return dw_fp_fallback(frame);
}
EXPORT_SYMBOL_GPL(dwarf_unwind);
