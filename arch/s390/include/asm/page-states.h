#ifndef _ASM_S390_PAGE_STATES_H
#define _ASM_S390_PAGE_STATES_H

#define ESSA_GET_STATE			0
#define ESSA_SET_STABLE			1
#define ESSA_SET_UNUSED			2
#define ESSA_SET_VOLATILE		3
#define ESSA_SET_PVOLATILE		4
#define ESSA_SET_STABLE_MAKE_RESIDENT	5
#define ESSA_SET_STABLE_IF_NOT_DISCARDED	6
#define ESSA_STEAL_BLOCK		7

#define ESSA_USTATE_MASK		0x0c
#define ESSA_USTATE_STABLE		0x00
#define ESSA_USTATE_UNUSED		0x04
#define ESSA_USTATE_PVOLATILE		0x08
#define ESSA_USTATE_VOLATILE		0x0c

#define ESSA_CSTATE_MASK		0x03
#define ESSA_CSTATE_RESIDENT		0x00
#define ESSA_CSTATE_PRESERVED		0x02
#define ESSA_CSTATE_ZERO		0x03

extern int cmma_flag;
extern struct page *mem_map;

/*
 * ESSA <rc-reg>,<page-address-reg>,<command-immediate>
 */
#define page_essa(_page,_command) ({		       \
	int _rc; \
	asm volatile(".insn rrf,0xb9ab0000,%0,%1,%2,0" \
		     : "=&d" (_rc) : "a" (page_to_phys(_page)), \
		       "i" (_command)); \
	_rc; \
})

static inline int page_host_discards(void)
{
	return cmma_flag;
}

static inline int page_discarded(struct page *page)
{
	int state;

	if (!cmma_flag)
		return 0;
	state = page_essa(page, ESSA_GET_STATE);
	return (state & ESSA_USTATE_MASK) == ESSA_USTATE_VOLATILE &&
		(state & ESSA_CSTATE_MASK) == ESSA_CSTATE_ZERO;
}

static inline void page_set_unused(struct page *page, int order)
{
	int i;

	if (!cmma_flag)
		return;
	for (i = 0; i < (1 << order); i++)
		page_essa(page + i, ESSA_SET_UNUSED);
}

static inline void page_set_stable(struct page *page, int order)
{
	int i;

	if (!cmma_flag)
		return;
	for (i = 0; i < (1 << order); i++)
		page_essa(page + i, ESSA_SET_STABLE);
}

static inline void page_set_volatile(struct page *page, int writable)
{
	if (!cmma_flag)
		return;
	if (writable)
		page_essa(page, ESSA_SET_PVOLATILE);
	else
		page_essa(page, ESSA_SET_VOLATILE);
}

static inline int page_set_stable_if_present(struct page *page)
{
	int rc;

	if (!cmma_flag || PageReserved(page))
		return 1;

	rc = page_essa(page, ESSA_SET_STABLE_IF_NOT_DISCARDED);
	return (rc & ESSA_USTATE_MASK) != ESSA_USTATE_VOLATILE ||
		(rc & ESSA_CSTATE_MASK) != ESSA_CSTATE_ZERO;
}

/*
 * Page locking is done with the architecture page bit PG_arch_1.
 */
static inline int page_test_set_state_change(struct page *page)
{
	return test_and_set_bit(PG_arch_1, &page->flags);
}

static inline void page_clear_state_change(struct page *page)
{
	clear_bit(PG_arch_1, &page->flags);
}

static inline int page_state_change(struct page *page)
{
	return test_bit(PG_arch_1, &page->flags);
}

int page_free_discarded(struct page *page);
void page_shrink_discard_list(void);
void page_discard_init(void);

/* FIXME: Debug function, remove for final release. */
#define page_essa_cond(_page,_command,_cond) ({ \
	int _rc; \
	asm volatile(".insn rrf,0xb9ac0000,%0,%1,%2,0" \
		     : "=&d" (_rc) : "a" ((((_page)-mem_map)<<PAGE_SHIFT) \
					   | ((_cond)& ~PAGE_MASK)), \
		       "i" (_command)); \
	_rc; \
})

static inline int page_discard_by_guest(struct page *page, unsigned long cond)
{
	if (!cmma_flag)
		return 1;
	if (page_discarded(page))
		return 1;
	return page_essa_cond(page, ESSA_STEAL_BLOCK,cond);
}

#endif /* _ASM_S390_PAGE_STATES_H */
