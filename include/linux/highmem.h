#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <linux/config.h>
#include <asm/pgalloc.h>

#ifdef CONFIG_HIGHMEM

extern struct page *highmem_start_page;

#include <asm/highmem.h>

/* declarations for linux/mm/highmem.c */
FASTCALL(unsigned int nr_free_highpages(void));

extern struct buffer_head * create_bounce(int rw, struct buffer_head * bh_orig);


static inline char *bh_kmap(struct buffer_head *bh)
{
	return kmap(bh->b_page) + bh_offset(bh);
}

static inline void bh_kunmap(struct buffer_head *bh)
{
	kunmap(bh->b_page);
}

#else /* CONFIG_HIGHMEM */

static inline unsigned int nr_free_highpages(void) { return 0; }

static inline void *kmap(struct page *page) { return page_address(page); }

#define kunmap(page) do { } while (0)

#define kmap_atomic(page,idx)		kmap(page)
#define kunmap_atomic(page,idx)		kunmap(page)

#define bh_kmap(bh)	((bh)->b_data)
#define bh_kunmap(bh)	do { } while (0);

#endif /* CONFIG_HIGHMEM */

/* when CONFIG_HIGHMEM is not set these will be plain clear/copy_page */
static inline void clear_user_highpage(struct page *page, unsigned long vaddr)
{
	clear_user_page(kmap(page), vaddr);
	kunmap(page);
}

static inline void clear_highpage(struct page *page)
{
	clear_page(kmap(page));
	kunmap(page);
}

static inline void memclear_highpage(struct page *page, unsigned int offset, unsigned int size)
{
	char *kaddr;

	if (offset + size > PAGE_SIZE)
		BUG();
	kaddr = kmap(page);
	memset(kaddr + offset, 0, size);
	kunmap(page);
}

/*
 * Same but also flushes aliased cache contents to RAM.
 */
static inline void memclear_highpage_flush(struct page *page, unsigned int offset, unsigned int size)
{
	char *kaddr;

	if (offset + size > PAGE_SIZE)
		BUG();
	kaddr = kmap(page);
	memset(kaddr + offset, 0, size);
	flush_page_to_ram(page);
	kunmap(page);
}

static inline void copy_user_highpage(struct page *to, struct page *from, unsigned long vaddr)
{
	char *vfrom, *vto;

	vfrom = kmap(from);
	vto = kmap(to);
	copy_user_page(vto, vfrom, vaddr);
	kunmap(from);
	kunmap(to);
}

static inline void copy_highpage(struct page *to, struct page *from)
{
	char *vfrom, *vto;

	vfrom = kmap(from);
	vto = kmap(to);
	copy_page(vto, vfrom);
	kunmap(from);
	kunmap(to);
}

#endif /* _LINUX_HIGHMEM_H */
