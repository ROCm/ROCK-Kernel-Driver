/* flow.c: Generic flow cache.
 *
 * Copyright (C) 2003 Alexey N. Kuznetsov (kuznet@ms2.inr.ac.ru)
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/init.h>
#include <net/flow.h>
#include <asm/atomic.h>

struct flow_cache_entry {
	struct flow_cache_entry	*next;
	u16			family;
	u8			dir;
	struct flowi		key;
	u32			genid;
	void			*object;
	atomic_t		*object_ref;
};

atomic_t flow_cache_genid = ATOMIC_INIT(0);

static u32 flow_hash_shift;
#define flow_hash_size	(1 << flow_hash_shift)
static struct flow_cache_entry **flow_table;
static kmem_cache_t *flow_cachep;

static int flow_lwm, flow_hwm;

struct flow_percpu_info {
	u32 hash_rnd;
	int number;
} ____cacheline_aligned;
static struct flow_percpu_info flow_hash_info[NR_CPUS];

#define flow_count(cpu)			(flow_hash_info[cpu].number)
#define flow_hash_rnd(cpu)		(flow_hash_info[cpu].hash_rnd)

static struct timer_list flow_hash_rnd_timer;

#define FLOW_HASH_RND_PERIOD	(10 * 60 * HZ)

static void flow_cache_new_hashrnd(unsigned long arg)
{
	int i;

	for (i = 0; i < NR_CPUS; i++)
		get_random_bytes(&flow_hash_rnd(i), sizeof(u32));

	flow_hash_rnd_timer.expires = jiffies + FLOW_HASH_RND_PERIOD;
	add_timer(&flow_hash_rnd_timer);
}

static void flow_cache_shrink(int cpu)
{
	struct flow_cache_entry *fle, **flp;
	int shrink_to = flow_lwm / flow_hash_size;
	int i;

	for (i = 0; i < flow_hash_size; i++) {
		int k = 0;

		flp = &flow_table[cpu*flow_hash_size+i];
		while ((fle = *flp) != NULL && k < shrink_to) {
			k++;
			flp = &fle->next;
		}
		while ((fle = *flp) != NULL) {
			*flp = fle->next;
			if (fle->object)
				atomic_dec(fle->object_ref);
			kmem_cache_free(flow_cachep, fle);
			flow_count(cpu)--;
		}
	}
}

static u32 flow_hash_code(struct flowi *key, int cpu)
{
	u32 *k = (u32 *) key;

	return (jhash2(k, (sizeof(*key) / sizeof(u32)), flow_hash_rnd(cpu)) &
		(flow_hash_size - 1));
}

#if (BITS_PER_LONG == 64)
typedef u64 flow_compare_t;
#else
typedef u32 flow_compare_t;
#endif

extern void flowi_is_missized(void);

/* I hear what you're saying, use memcmp.  But memcmp cannot make
 * important assumptions that we can here, such as alignment and
 * constant size.
 */
static int flow_key_compare(struct flowi *key1, struct flowi *key2)
{
	flow_compare_t *k1, *k1_lim, *k2;
	const int n_elem = sizeof(struct flowi) / sizeof(flow_compare_t);

	if (sizeof(struct flowi) % sizeof(flow_compare_t))
		flowi_is_missized();

	k1 = (flow_compare_t *) key1;
	k1_lim = k1 + n_elem;

	k2 = (flow_compare_t *) key2;

	do {
		if (*k1++ != *k2++)
			return 1;
	} while (k1 < k1_lim);

	return 0;
}

void *flow_cache_lookup(struct flowi *key, u16 family, u8 dir,
			flow_resolve_t resolver)
{
	struct flow_cache_entry *fle, **head;
	unsigned int hash;
	int cpu;

	local_bh_disable();
	cpu = smp_processor_id();
	hash = flow_hash_code(key, cpu);

	head = &flow_table[(cpu << flow_hash_shift) + hash];
	for (fle = *head; fle; fle = fle->next) {
		if (fle->family == family &&
		    fle->dir == dir &&
		    flow_key_compare(key, &fle->key) == 0) {
			if (fle->genid == atomic_read(&flow_cache_genid)) {
				void *ret = fle->object;

				if (ret)
					atomic_inc(fle->object_ref);
				local_bh_enable();

				return ret;
			}
			break;
		}
	}

	{
		void *obj;
		atomic_t *obj_ref;

		resolver(key, family, dir, &obj, &obj_ref);

		if (fle) {
			fle->genid = atomic_read(&flow_cache_genid);

			if (fle->object)
				atomic_dec(fle->object_ref);

			fle->object = obj;
			fle->object_ref = obj_ref;
			if (obj)
				atomic_inc(fle->object_ref);
		} else {
			if (flow_count(cpu) > flow_hwm)
				flow_cache_shrink(cpu);

			fle = kmem_cache_alloc(flow_cachep, SLAB_ATOMIC);
			if (fle) {
				fle->next = *head;
				*head = fle;
				fle->family = family;
				fle->dir = dir;
				memcpy(&fle->key, key, sizeof(*key));
				fle->genid = atomic_read(&flow_cache_genid);
				fle->object = obj;
				fle->object_ref = obj_ref;

				flow_count(cpu)++;
			}
		}
		local_bh_enable();

		return obj;
	}
}

static int __init flow_cache_init(void)
{
	unsigned long order;
	int i;

	flow_cachep = kmem_cache_create("flow_cache",
					sizeof(struct flow_cache_entry),
					0, SLAB_HWCACHE_ALIGN,
					NULL, NULL);

	if (!flow_cachep)
		panic("NET: failed to allocate flow cache slab\n");

	flow_hash_shift = 10;
	flow_lwm = 2 * flow_hash_size;
	flow_hwm = 4 * flow_hash_size;

	for (i = 0; i < NR_CPUS; i++) {
		flow_hash_rnd(i) =
			(u32) ((num_physpages ^ (num_physpages>>8)) ^
			       (jiffies ^ (jiffies >> 7)));
		flow_hash_rnd(i) ^= i;
	}

	init_timer(&flow_hash_rnd_timer);
	flow_hash_rnd_timer.function = flow_cache_new_hashrnd;
	flow_hash_rnd_timer.expires = jiffies + FLOW_HASH_RND_PERIOD;
	add_timer(&flow_hash_rnd_timer);

	for (order = 0;
	     (PAGE_SIZE << order) <
		     (NR_CPUS*sizeof(struct flow_entry *)*flow_hash_size);
	     order++)
		/* NOTHING */;

	flow_table = (struct flow_cache_entry **)
		__get_free_pages(GFP_ATOMIC, order);

	if (!flow_table)
		panic("Failed to allocate flow cache hash table\n");

	memset(flow_table, 0, PAGE_SIZE << order);

	return 0;
}

module_init(flow_cache_init);
