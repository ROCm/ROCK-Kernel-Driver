/*
 * include/linux/rmap-locking.h
 *
 * Locking primitives for exclusive access to a page's reverse-mapping
 * pte chain.
 */

static inline void pte_chain_lock(struct page *page)
{
	/*
	 * Assuming the lock is uncontended, this never enters
	 * the body of the outer loop. If it is contended, then
	 * within the inner loop a non-atomic test is used to
	 * busywait with less bus contention for a good time to
	 * attempt to acquire the lock bit.
	 */
	preempt_disable();
#ifdef CONFIG_SMP
	while (test_and_set_bit(PG_chainlock, &page->flags)) {
		while (test_bit(PG_chainlock, &page->flags))
			cpu_relax();
	}
#endif
}

static inline void pte_chain_unlock(struct page *page)
{
#ifdef CONFIG_SMP
	smp_mb__before_clear_bit();
	clear_bit(PG_chainlock, &page->flags);
#endif
	preempt_enable();
}
