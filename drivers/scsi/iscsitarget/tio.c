/*
 * Target I/O.
 * (C) 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 */

#include "iscsi.h"
#include "iscsi_dbg.h"
#include "iotype.h"

static int tio_add_pages(struct tio *tio, int count)
{
	int i;
	struct page *page;

	dprintk(D_GENERIC, "%p %d (%d)\n", tio, count, tio->pg_cnt);

	tio->pg_cnt = count;

	count *= sizeof(struct page *);

	do {
		if (!(tio->pvec = kmalloc(count, GFP_KERNEL)))
			yield();
	} while (!tio->pvec);

	memset(tio->pvec, 0, count);

	for (i = 0; i < tio->pg_cnt; i++) {
		do {
			if (!(page = alloc_page(GFP_KERNEL)))
				yield();
		} while (!page);
		tio->pvec[i] = page;
	}
	return 0;
}

static kmem_cache_t *tio_cache;

struct tio *tio_alloc(int count)
{
	struct tio *tio;

	tio = kmem_cache_alloc(tio_cache, GFP_KERNEL | __GFP_NOFAIL);

	tio->pg_cnt = 0;
	tio->idx = 0;
	tio->offset = 0;
	tio->size = 0;
	tio->pvec = NULL;

	atomic_set(&tio->count, 1);

	if (count)
		tio_add_pages(tio, count);

	return tio;
}

static void tio_free(struct tio *tio)
{
	int i;
	for (i = 0; i < tio->pg_cnt; i++) {
		assert(tio->pvec[i]);
		__free_page(tio->pvec[i]);
	}
	kfree(tio->pvec);
	kmem_cache_free(tio_cache, tio);
}

void tio_put(struct tio *tio)
{
	assert(atomic_read(&tio->count));
	if (atomic_dec_and_test(&tio->count))
		tio_free(tio);
}

void tio_get(struct tio *tio)
{
	atomic_inc(&tio->count);
}

void tio_set(struct tio *tio, u32 size, loff_t offset)
{
	tio->idx = offset >> PAGE_CACHE_SHIFT;
	tio->offset = offset & ~PAGE_CACHE_MASK;
	tio->size = size;
}

int tio_read(struct iet_volume *lu, struct tio *tio)
{
	struct iotype *iot = lu->iotype;
	assert(iot);
	return iot->make_request ? iot->make_request(lu, tio, READ) : 0;
}

int tio_write(struct iet_volume *lu, struct tio *tio)
{
	struct iotype *iot = lu->iotype;
	assert(iot);
	return iot->make_request ? iot->make_request(lu, tio, WRITE) : 0;
}

int tio_sync(struct iet_volume *lu, struct tio *tio)
{
	struct iotype *iot = lu->iotype;
	assert(iot);
	return iot->sync ? iot->sync(lu, tio) : 0;
}

int tio_init(void)
{
	tio_cache = kmem_cache_create("tio",
					      sizeof(struct tio), 0, 0, NULL, NULL);
	return  tio_cache ? 0 : -ENOMEM;
}

void tio_exit(void)
{
	if (tio_cache)
		kmem_cache_destroy(tio_cache);
}
