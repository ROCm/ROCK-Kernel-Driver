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

#include <sound/driver.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/info.h>
#ifdef CONFIG_SBUS
#include <asm/sbus.h>
#endif

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


/**
 * snd_malloc_pages - allocate pages with the given size
 * @size: the size to allocate in bytes
 * @dma_flags: the allocation conditions, GFP_XXX
 *
 * Allocates the physically contiguous pages with the given size.
 *
 * Returns the pointer of the buffer, or NULL if no enoguh memory.
 */
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

/**
 * snd_malloc_pages_fallback - allocate pages with the given size with fallback
 * @size: the requested size to allocate in bytes
 * @dma_flags: the allocation conditions, GFP_XXX
 * @res_size: the pointer to store the size of buffer actually allocated
 *
 * Allocates the physically contiguous pages with the given request
 * size.  When no space is left, this function reduces the size and
 * tries to allocate again.  The size actually allocated is stored in
 * res_size argument.
 *
 * Returns the pointer of the buffer, or NULL if no enoguh memory.
 */
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

/**
 * snd_free_pages - release the pages
 * @ptr: the buffer pointer to release
 * @size: the allocated buffer size
 *
 * Releases the buffer allocated via snd_malloc_pages().
 */
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

/**
 * snd_malloc_isa_pages - allocate pages for ISA bus with the given size
 * @size: the size to allocate in bytes
 * @dma_addr: the pointer to store the physical address of the buffer
 *
 * Allocates the physically contiguous pages with the given size for
 * ISA bus.
 *
 * Returns the pointer of the buffer, or NULL if no enoguh memory.
 */
void *snd_malloc_isa_pages(unsigned long size, dma_addr_t *dma_addr)
{
	void *dma_area;
	dma_area = snd_malloc_pages(size, GFP_ATOMIC|GFP_DMA);
	*dma_addr = dma_area ? isa_virt_to_bus(dma_area) : 0UL;
	return dma_area;
}

/**
 * snd_malloc_isa_pages_fallback - allocate pages with the given size with fallback for ISA bus
 * @size: the requested size to allocate in bytes
 * @dma_addr: the pointer to store the physical address of the buffer
 * @res_size: the pointer to store the size of buffer actually allocated
 *
 * Allocates the physically contiguous pages with the given request
 * size for PCI bus.  When no space is left, this function reduces the size and
 * tries to allocate again.  The size actually allocated is stored in
 * res_size argument.
 *
 * Returns the pointer of the buffer, or NULL if no enoguh memory.
 */
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

/**
 * snd_malloc_pci_pages - allocate pages for PCI bus with the given size
 * @pci: the pci device pointer
 * @size: the size to allocate in bytes
 * @dma_addr: the pointer to store the physical address of the buffer
 *
 * Allocates the physically contiguous pages with the given size for
 * PCI bus.
 *
 * Returns the pointer of the buffer, or NULL if no enoguh memory.
 */
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

/**
 * snd_malloc_pci_pages_fallback - allocate pages with the given size with fallback for PCI bus
 * @pci: pci device pointer
 * @size: the requested size to allocate in bytes
 * @dma_addr: the pointer to store the physical address of the buffer
 * @res_size: the pointer to store the size of buffer actually allocated
 *
 * Allocates the physically contiguous pages with the given request
 * size for PCI bus.  When no space is left, this function reduces the size and
 * tries to allocate again.  The size actually allocated is stored in
 * res_size argument.
 *
 * Returns the pointer of the buffer, or NULL if no enoguh memory.
 */
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

/**
 * snd_free_pci_pages - release the pages
 * @pci: pci device pointer
 * @size: the allocated buffer size
 * @ptr: the buffer pointer to release
 * @dma_addr: the physical address of the buffer
 *
 * Releases the buffer allocated via snd_malloc_pci_pages().
 */
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

#ifdef CONFIG_SBUS

/**
 * snd_malloc_sbus_pages - allocate pages for SBUS with the given size
 * @sdev: sbus device pointer
 * @size: the size to allocate in bytes
 * @dma_addr: the pointer to store the physical address of the buffer
 *
 * Allocates the physically contiguous pages with the given size for
 * SBUS.
 *
 * Returns the pointer of the buffer, or NULL if no enoguh memory.
 */
void *snd_malloc_sbus_pages(struct sbus_dev *sdev,
			    unsigned long size,
			    dma_addr_t *dma_addr)
{
	int pg;
	void *res;

	snd_assert(size > 0, return NULL);
	snd_assert(dma_addr != NULL, return NULL);
	for (pg = 0; PAGE_SIZE * (1 << pg) < size; pg++);
	res = sbus_alloc_consistent(sdev, PAGE_SIZE * (1 << pg), dma_addr);
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

/**
 * snd_malloc_pci_pages_fallback - allocate pages with the given size with fallback for SBUS
 * @sdev: sbus device pointer
 * @size: the requested size to allocate in bytes
 * @dma_addr: the pointer to store the physical address of the buffer
 * @res_size: the pointer to store the size of buffer actually allocated
 *
 * Allocates the physically contiguous pages with the given request
 * size for SBUS.  When no space is left, this function reduces the size and
 * tries to allocate again.  The size actually allocated is stored in
 * res_size argument.
 *
 * Returns the pointer of the buffer, or NULL if no enoguh memory.
 */
void *snd_malloc_sbus_pages_fallback(struct sbus_dev *sdev,
				     unsigned long size,
				     dma_addr_t *dma_addr,
				     unsigned long *res_size)
{
	void *res;

	snd_assert(res_size != NULL, return NULL);
	do {
		if ((res = snd_malloc_sbus_pages(sdev, size, dma_addr)) != NULL) {
			*res_size = size;
			return res;
		}
		size >>= 1;
	} while (size >= PAGE_SIZE);
	return NULL;
}

/**
 * snd_free_sbus_pages - release the pages
 * @sdev: sbus device pointer
 * @size: the allocated buffer size
 * @ptr: the buffer pointer to release
 * @dma_addr: the physical address of the buffer
 *
 * Releases the buffer allocated via snd_malloc_pci_pages().
 */
void snd_free_sbus_pages(struct sbus_dev *sdev,
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
	sbus_free_consistent(sdev, PAGE_SIZE * (1 << pg), ptr, dma_addr);
#ifdef CONFIG_SND_DEBUG_MEMORY
	snd_alloc_pages -= 1 << pg;
#endif
}

#endif /* CONFIG_SBUS */

/**
 * snd_kcalloc - memory allocation and zero-clear
 * @size: the size to allocate in bytes
 * @flags: allocation conditions, GFP_XXX
 *
 * Allocates a memory chunk via kmalloc() and initializes it to zero.
 *
 * Returns the pointer, or NULL if no enoguh memory.
 */
void *snd_kcalloc(size_t size, int flags)
{
	void *ptr;
	
	ptr = _snd_kmalloc(size, flags);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

/**
 * snd_kmalloc_strdup - copy the string
 * @string: the original string
 * @flags: allocation conditions, GFP_XXX
 *
 * Allocates a memory chunk via kmalloc() and copies the string to it.
 *
 * Returns the pointer, or NULL if no enoguh memory.
 */
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

/**
 * copy_to_user_fromio - copy data from mmio-space to user-space
 * @dst: the destination pointer on user-space
 * @src: the source pointer on mmio
 * @count: the data size to copy in bytes
 *
 * Copies the data from mmio-space to user-space.
 *
 * Returns zero if successful, or non-zero on failure.
 */
int copy_to_user_fromio(void *dst, unsigned long src, size_t count)
{
#if defined(__i386__) || defined(CONFIG_SPARC32)
	return copy_to_user(dst, (const void*)src, count) ? -EFAULT : 0;
#else
	char buf[256];
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

/**
 * copy_from_user_toio - copy data from user-space to mmio-space
 * @dst: the destination pointer on mmio-space
 * @src: the source pointer on user-space
 * @count: the data size to copy in bytes
 *
 * Copies the data from user-space to mmio-space.
 *
 * Returns zero if successful, or non-zero on failure.
 */
int copy_from_user_toio(unsigned long dst, const void *src, size_t count)
{
#if defined(__i386__) || defined(CONFIG_SPARC32)
	return copy_from_user((void*)dst, src, count) ? -EFAULT : 0;
#else
	char buf[256];
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


#ifdef CONFIG_PCI

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 0) && defined(__i386__)
/*
 * on ix86, we allocate a page with GFP_KERNEL to assure the
 * allocation.  the code is almost same with kernel/i386/pci-dma.c but
 * it allocates only a single page and checks the validity of the
 * page address with the given pci dma mask.
 */

/**
 * snd_malloc_pci_page - allocate a page in the valid pci dma mask
 * @pci: pci device pointer
 * @addrp: the pointer to store the physical address of the buffer
 *
 * Allocates a single page for the given PCI device and returns
 * the virtual address and stores the physical address on addrp.
 * 
 * This function cannot be called from interrupt handlers or
 * within spinlocks.
 */
void *snd_malloc_pci_page(struct pci_dev *pci, dma_addr_t *addrp)
{
	void *ptr;
	dma_addr_t addr;
	unsigned long rmask;

	rmask = ~(unsigned long)(pci ? pci->dma_mask : 0x00ffffff);
	ptr = (void *)__get_free_page(GFP_KERNEL);
	if (ptr) {
		addr = virt_to_phys(ptr);
		if (((unsigned long)addr + PAGE_SIZE - 1) & rmask) {
			/* try to reallocate with the GFP_DMA */
			free_page((unsigned long)ptr);
			ptr = (void *)__get_free_page(GFP_KERNEL | GFP_DMA);
			if (ptr) /* ok, the address must be within lower 16MB... */
				addr = virt_to_phys(ptr);
			else
				addr = 0;
		}
	} else
		addr = 0;
	if (ptr) {
		struct page *page = virt_to_page(ptr);
		memset(ptr, 0, PAGE_SIZE);
		SetPageReserved(page);
#ifdef CONFIG_SND_DEBUG_MEMORY
		snd_alloc_pages++;
#endif
	}
	*addrp = addr;
	return ptr;
}
#else
/* on other architectures, call snd_malloc_pci_pages() helper function
 * which uses pci_alloc_consistent().
 */
void *snd_malloc_pci_page(struct pci_dev *pci, dma_addr_t *addrp)
{
	return snd_malloc_pci_pages(pci, PAGE_SIZE, addrp);
}
#endif /* 2.4 && i386 */

#if 0 /* for kernel-doc */
/**
 * snd_free_pci_page - release a page
 * @pci: pci device pointer
 * @ptr: the buffer pointer to release
 * @dma_addr: the physical address of the buffer
 *
 * Releases the buffer allocated via snd_malloc_pci_page().
 */
void snd_free_pci_page(struct pci_dev *pci, void *ptr, dma_addr_t dma_addr);
#endif /* for kernel-doc */

#endif /* CONFIG_PCI */
