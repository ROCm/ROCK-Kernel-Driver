/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Memory allocation routines.
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#define __NO_VERSION__
#include <sound/driver.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/info.h>

/*
 *  memory allocation helpers and debug routines
 */

#ifdef CONFIG_SND_DEBUG_MEMORY

struct snd_alloc_track {
	unsigned long magic;
	void *caller;
	size_t size;
	struct list_head list;
	long data[0];
};

#define snd_alloc_track_entry(obj) (struct snd_alloc_track *)((char*)obj - (unsigned long)((struct snd_alloc_track *)0)->data)

static long snd_alloc_pages;
static long snd_alloc_kmalloc;
static long snd_alloc_vmalloc;
static LIST_HEAD(snd_alloc_kmalloc_list);
static LIST_HEAD(snd_alloc_vmalloc_list);
static spinlock_t snd_alloc_kmalloc_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t snd_alloc_vmalloc_lock = SPIN_LOCK_UNLOCKED;
#define KMALLOC_MAGIC 0x87654321
#define VMALLOC_MAGIC 0x87654320
static snd_info_entry_t *snd_memory_info_entry;

void snd_memory_init(void)
{
	snd_alloc_pages = 0;
	snd_alloc_kmalloc = 0;
	snd_alloc_vmalloc = 0;
}

void snd_memory_done(void)
{
	struct list_head *head;
	struct snd_alloc_track *t;
	if (snd_alloc_pages > 0)
		snd_printk(KERN_ERR "Not freed snd_alloc_pages = %li\n", snd_alloc_pages);
	if (snd_alloc_kmalloc > 0)
		snd_printk(KERN_ERR "Not freed snd_alloc_kmalloc = %li\n", snd_alloc_kmalloc);
	if (snd_alloc_vmalloc > 0)
		snd_printk(KERN_ERR "Not freed snd_alloc_vmalloc = %li\n", snd_alloc_vmalloc);
	for (head = snd_alloc_kmalloc_list.prev;
	     head != &snd_alloc_kmalloc_list; head = head->prev) {
		t = list_entry(head, struct snd_alloc_track, list);
		if (t->magic != KMALLOC_MAGIC) {
			snd_printk(KERN_ERR "Corrupted kmalloc\n");
			break;
		}
		snd_printk(KERN_ERR "kmalloc(%ld) from %p not freed\n", (long) t->size, t->caller);
	}
	for (head = snd_alloc_vmalloc_list.prev;
	     head != &snd_alloc_vmalloc_list; head = head->prev) {
		t = list_entry(head, struct snd_alloc_track, list);
		if (t->magic != VMALLOC_MAGIC) {
			snd_printk(KERN_ERR "Corrupted vmalloc\n");
			break;
		}
		snd_printk(KERN_ERR "vmalloc(%ld) from %p not freed\n", (long) t->size, t->caller);
	}
}

void *__snd_kmalloc(size_t size, int flags, void *caller)
{
	unsigned long cpu_flags;
	struct snd_alloc_track *t;
	void *ptr;
	
	ptr = snd_wrapper_kmalloc(size + sizeof(struct snd_alloc_track), flags);
	if (ptr != NULL) {
		t = (struct snd_alloc_track *)ptr;
		t->magic = KMALLOC_MAGIC;
		t->caller = caller;
		spin_lock_irqsave(&snd_alloc_kmalloc_lock, cpu_flags);
		list_add_tail(&t->list, &snd_alloc_kmalloc_list);
		spin_unlock_irqrestore(&snd_alloc_kmalloc_lock, cpu_flags);
		t->size = size;
		snd_alloc_kmalloc += size;
		ptr = t->data;
	}
	return ptr;
}

#define _snd_kmalloc(size, flags) __snd_kmalloc((size), (flags), __builtin_return_address(0));
void *snd_hidden_kmalloc(size_t size, int flags)
{
	return _snd_kmalloc(size, flags);
}

void snd_hidden_kfree(const void *obj)
{
	unsigned long flags;
	struct snd_alloc_track *t;
	if (obj == NULL) {
		snd_printk(KERN_WARNING "null kfree (called from %p)\n", __builtin_return_address(0));
		return;
	}
	t = snd_alloc_track_entry(obj);
	if (t->magic != KMALLOC_MAGIC) {
		snd_printk(KERN_WARNING "bad kfree (called from %p)\n", __builtin_return_address(0));
		return;
	}
	spin_lock_irqsave(&snd_alloc_kmalloc_lock, flags);
	list_del(&t->list);
	spin_unlock_irqrestore(&snd_alloc_kmalloc_lock, flags);
	t->magic = 0;
	snd_alloc_kmalloc -= t->size;
	obj = t;
	snd_wrapper_kfree(obj);
}

void *_snd_magic_kcalloc(unsigned long magic, size_t size, int flags)
{
	unsigned long *ptr;
	ptr = _snd_kmalloc(size + sizeof(unsigned long), flags);
	if (ptr) {
		*ptr++ = magic;
		memset(ptr, 0, size);
	}
	return ptr;
}

void *_snd_magic_kmalloc(unsigned long magic, size_t size, int flags)
{
	unsigned long *ptr;
	ptr = _snd_kmalloc(size + sizeof(unsigned long), flags);
	if (ptr)
		*ptr++ = magic;
	return ptr;
}

void snd_magic_kfree(void *_ptr)
{
	unsigned long *ptr = _ptr;
	if (ptr == NULL) {
		snd_printk(KERN_WARNING "null snd_magic_kfree (called from %p)\n", __builtin_return_address(0));
		return;
	}
	*--ptr = 0;
	{
		struct snd_alloc_track *t;
		t = snd_alloc_track_entry(ptr);
		if (t->magic != KMALLOC_MAGIC) {
			snd_printk(KERN_ERR "bad snd_magic_kfree (called from %p)\n", __builtin_return_address(0));
			return;
		}
	}
	snd_hidden_kfree(ptr);
	return;
}

void *snd_hidden_vmalloc(unsigned long size)
{
	void *ptr;
	ptr = snd_wrapper_vmalloc(size + sizeof(struct snd_alloc_track));
	if (ptr) {
		struct snd_alloc_track *t = (struct snd_alloc_track *)ptr;
		t->magic = VMALLOC_MAGIC;
		t->caller = __builtin_return_address(0);
		spin_lock(&snd_alloc_vmalloc_lock);
		list_add_tail(&t->list, &snd_alloc_vmalloc_list);
		spin_unlock(&snd_alloc_vmalloc_lock);
		t->size = size;
		snd_alloc_vmalloc += size;
		ptr = t->data;
	}
	return ptr;
}

void snd_hidden_vfree(void *obj)
{
	struct snd_alloc_track *t;
	if (obj == NULL) {
		snd_printk(KERN_WARNING "null vfree (called from %p)\n", __builtin_return_address(0));
		return;
	}
	t = snd_alloc_track_entry(obj);
	if (t->magic != VMALLOC_MAGIC) {
		snd_printk(KERN_ERR "bad vfree (called from %p)\n", __builtin_return_address(0));
		return;
	}
	spin_lock(&snd_alloc_vmalloc_lock);
	list_del(&t->list);
	spin_unlock(&snd_alloc_vmalloc_lock);
	t->magic = 0;
	snd_alloc_vmalloc -= t->size;
	obj = t;
	snd_wrapper_vfree(obj);
}

static void snd_memory_info_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	long pages = snd_alloc_pages >> (PAGE_SHIFT-12);
	snd_iprintf(buffer, "pages  : %li bytes (%li pages per %likB)\n", pages * PAGE_SIZE, pages, PAGE_SIZE / 1024);
	snd_iprintf(buffer, "kmalloc: %li bytes\n", snd_alloc_kmalloc);
	snd_iprintf(buffer, "vmalloc: %li bytes\n", snd_alloc_vmalloc);
}

int __init snd_memory_info_init(void)
{
	snd_info_entry_t *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "meminfo", NULL);
	if (entry) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_memory_info_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_memory_info_entry = entry;
	return 0;
}

int __exit snd_memory_info_done(void)
{
	if (snd_memory_info_entry)
		snd_info_unregister(snd_memory_info_entry);
	return 0;
}

#else

#define _snd_kmalloc kmalloc

#endif /* CONFIG_SND_DEBUG_MEMORY */



void *snd_malloc_pages(unsigned long size, unsigned int dma_flags)
{
	int pg;
	void *res;

	snd_assert(size > 0, return NULL);
	snd_assert(dma_flags != 0, return NULL);
	for (pg = 0; PAGE_SIZE * (1 << pg) < size; pg++);
	if ((res = (void *) __get_free_pages(dma_flags, pg)) != NULL) {
		struct page *page = virt_to_page(res);
		struct page *last_page = page + (1 << pg);
		while (page < last_page)
			SetPageReserved(page++);
#ifdef CONFIG_SND_DEBUG_MEMORY
		snd_alloc_pages += 1 << pg;
#endif
	}
	return res;
}

void *snd_malloc_pages_fallback(unsigned long size, unsigned int dma_flags, unsigned long *res_size)
{
	void *res;

	snd_assert(size > 0, return NULL);
	snd_assert(res_size != NULL, return NULL);
	do {
		if ((res = snd_malloc_pages(size, dma_flags)) != NULL) {
			*res_size = size;
			return res;
		}
		size >>= 1;
	} while (size >= PAGE_SIZE);
	return NULL;
}

void snd_free_pages(void *ptr, unsigned long size)
{
	int pg;
	struct page *page, *last_page;

	if (ptr == NULL)
		return;
	for (pg = 0; PAGE_SIZE * (1 << pg) < size; pg++);
	page = virt_to_page(ptr);
	last_page = page + (1 << pg);
	while (page < last_page)
		ClearPageReserved(page++);
	free_pages((unsigned long) ptr, pg);
#ifdef CONFIG_SND_DEBUG_MEMORY
	snd_alloc_pages -= 1 << pg;
#endif
}

#if defined(CONFIG_ISA) && ! defined(CONFIG_PCI)

void *snd_malloc_isa_pages(unsigned long size, dma_addr_t *dma_addr)
{
	void *dma_area;
	dma_area = snd_malloc_pages(size, GFP_ATOMIC|GFP_DMA);
	*dma_addr = dma_area ? isa_virt_to_bus(dma_area) : 0UL;
	return dma_area;
}

void *snd_malloc_isa_pages_fallback(unsigned long size,
				    dma_addr_t *dma_addr,
				    unsigned long *res_size)
{
	void *dma_area;
	dma_area = snd_malloc_pages_fallback(size, GFP_ATOMIC|GFP_DMA, res_size);
	*dma_addr = dma_area ? isa_virt_to_bus(dma_area) : 0UL;
	return dma_area;
}

#endif /* CONFIG_ISA && !CONFIG_PCI */

#ifdef CONFIG_PCI

void *snd_malloc_pci_pages(struct pci_dev *pci,
			   unsigned long size,
			   dma_addr_t *dma_addr)
{
	int pg;
	void *res;

	snd_assert(size > 0, return NULL);
	snd_assert(dma_addr != NULL, return NULL);
	for (pg = 0; PAGE_SIZE * (1 << pg) < size; pg++);
	res = pci_alloc_consistent(pci, PAGE_SIZE * (1 << pg), dma_addr);
	if (res != NULL) {
		struct page *page = virt_to_page(res);
		struct page *last_page = page + (1 << pg);
		while (page < last_page)
			SetPageReserved(page++);
#ifdef CONFIG_SND_DEBUG_MEMORY
		snd_alloc_pages += 1 << pg;
#endif
	}
	return res;
}

void *snd_malloc_pci_pages_fallback(struct pci_dev *pci,
				    unsigned long size,
				    dma_addr_t *dma_addr,
				    unsigned long *res_size)
{
	void *res;

	snd_assert(res_size != NULL, return NULL);
	do {
		if ((res = snd_malloc_pci_pages(pci, size, dma_addr)) != NULL) {
			*res_size = size;
			return res;
		}
		size >>= 1;
	} while (size >= PAGE_SIZE);
	return NULL;
}

void snd_free_pci_pages(struct pci_dev *pci,
			unsigned long size,
			void *ptr,
			dma_addr_t dma_addr)
{
	int pg;
	struct page *page, *last_page;

	if (ptr == NULL)
		return;
	for (pg = 0; PAGE_SIZE * (1 << pg) < size; pg++);
	page = virt_to_page(ptr);
	last_page = page + (1 << pg);
	while (page < last_page)
		ClearPageReserved(page++);
	pci_free_consistent(pci, PAGE_SIZE * (1 << pg), ptr, dma_addr);
#ifdef CONFIG_SND_DEBUG_MEMORY
	snd_alloc_pages -= 1 << pg;
#endif
}

#endif /* CONFIG_PCI */

void *snd_kcalloc(size_t size, int flags)
{
	void *ptr;
	
	ptr = _snd_kmalloc(size, flags);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

char *snd_kmalloc_strdup(const char *string, int flags)
{
	size_t len;
	char *ptr;

	if (!string)
		return NULL;
	len = strlen(string) + 1;
	ptr = _snd_kmalloc(len, flags);
	if (ptr)
		memcpy(ptr, string, len);
	return ptr;
}

int copy_to_user_fromio(void *dst, unsigned long src, size_t count)
{
#if defined(__i386_) || defined(CONFIG_SPARC32)
	return copy_to_user(dst, (const void*)src, count) ? -EFAULT : 0;
#else
	char buf[1024];
	while (count) {
		size_t c = count;
		if (c > sizeof(buf))
			c = sizeof(buf);
		memcpy_fromio(buf, (void*)src, c);
		if (copy_to_user(dst, buf, c))
			return -EFAULT;
		count -= c;
		dst += c;
		src += c;
	}
	return 0;
#endif
}

int copy_from_user_toio(unsigned long dst, const void *src, size_t count)
{
#if defined(__i386_) || defined(CONFIG_SPARC32)
	return copy_from_user((void*)dst, src, count) ? -EFAULT : 0;
#else
	char buf[1024];
	while (count) {
		size_t c = count;
		if (c > sizeof(buf))
			c = sizeof(buf);
		if (copy_from_user(buf, src, c))
			return -EFAULT;
		memcpy_toio((void*)dst, buf, c);
		count -= c;
		dst += c;
		src += c;
	}
	return 0;
#endif
}

#ifdef HACK_PCI_ALLOC_CONSISTENT
/*
 * A dirty hack... when the kernel code is fixed this should be removed.
 *
 * since pci_alloc_consistent always tries GFP_DMA when the requested
 * pci memory region is below 32bit, it happens quite often that even
 * 2 order or pages cannot be allocated.
 *
 * so in the following, GFP_DMA is used only when the first allocation
 * doesn't match the requested region.
 */
#ifdef __i386__
#define get_phys_addr(x) virt_to_phys(x)
#else /* ppc and x86-64 */
#define get_phys_addr(x) virt_to_bus(x)
#endif
void *snd_pci_hack_alloc_consistent(struct pci_dev *hwdev, size_t size,
				    dma_addr_t *dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC;

	if (hwdev == NULL)
		gfp |= GFP_DMA;
	ret = (void *)__get_free_pages(gfp, get_order(size));
	if (ret) {
		if (hwdev && ((get_phys_addr(ret) + size - 1) & ~hwdev->dma_mask)) {
			free_pages((unsigned long)ret, get_order(size));
			ret = (void *)__get_free_pages(gfp | GFP_DMA, get_order(size));
		}
	}
	if (ret) {
		memset(ret, 0, size);
		*dma_handle = get_phys_addr(ret);
	}
	return ret;
}
#endif /* hack */
