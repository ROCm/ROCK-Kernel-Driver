/*
 * Dynamic DMA mapping support.
 *
 * On i386 there is no hardware dynamic DMA address translation,
 * so consistent alloc/free are merely page allocation/freeing.
 * The rest of the dynamic DMA mapping interface is implemented
 * in asm/pci.h.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <asm/io.h>

#ifdef CONFIG_MMU

void *dma_alloc_coherent(struct device *hwdev, size_t size, dma_addr_t *dma_handle, int gfp)
{
	void *ret;

	ret = consistent_alloc(gfp, size, dma_handle);
	if (ret)
		memset(ret, 0, size);

	return ret;
}

void dma_free_coherent(struct device *hwdev, size_t size, void *vaddr, dma_addr_t dma_handle)
{
	consistent_free(vaddr);
}


#else /* CONFIG_MMU */

#if 1
#define DMA_SRAM_START	dma_coherent_mem_start
#define DMA_SRAM_END	dma_coherent_mem_end
#else // Use video RAM on Matrox
#define DMA_SRAM_START	0xe8900000
#define DMA_SRAM_END	0xe8a00000
#endif

struct dma_alloc_record {
	struct list_head	list;
	unsigned long		ofs;
	unsigned long		len;
};

static spinlock_t dma_alloc_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(dma_alloc_list);

void *dma_alloc_coherent(struct device *hwdev, size_t size, dma_addr_t *dma_handle, int gfp)
{
	struct dma_alloc_record *new;
	struct list_head *this = &dma_alloc_list;
	unsigned long flags;
	unsigned long start = DMA_SRAM_START;
	unsigned long end;

	if (!DMA_SRAM_START) {
		printk("%s called without any DMA area reserved!\n", __func__);
		return NULL;
	}

	new = kmalloc(sizeof (*new), GFP_ATOMIC);
	if (!new)
		return NULL;

	/* Round up to a reasonable alignment */
	new->len = (size + 31) & ~31;

	spin_lock_irqsave(&dma_alloc_lock, flags);

	list_for_each (this, &dma_alloc_list) {
		struct dma_alloc_record *this_r = list_entry(this, struct dma_alloc_record, list);
		end = this_r->ofs;

		if (end - start >= size)
			goto gotone;

		start = this_r->ofs + this_r->len;
	}
	/* Reached end of list. */
	end = DMA_SRAM_END;
	this = &dma_alloc_list;

	if (end - start >= size) {
	gotone:
		new->ofs = start;
		list_add_tail(&new->list, this);
		spin_unlock_irqrestore(&dma_alloc_lock, flags);

		*dma_handle = start;
		return (void *)start;
	}

	kfree(new);
	spin_unlock_irqrestore(&dma_alloc_lock, flags);
	return NULL;
}

void dma_free_coherent(struct device *hwdev, size_t size, void *vaddr, dma_addr_t dma_handle)
{
	struct dma_alloc_record *rec;
	unsigned long flags;

	spin_lock_irqsave(&dma_alloc_lock, flags);

	list_for_each_entry(rec, &dma_alloc_list, list) {
		if (rec->ofs == dma_handle) {
			list_del(&rec->list);
			kfree(rec);
			spin_unlock_irqrestore(&dma_alloc_lock, flags);
			return;
		}
	}
	spin_unlock_irqrestore(&dma_alloc_lock, flags);
	BUG();
}

#endif /* CONFIG_MMU */
