/*
 *  linux/mm/swapfile.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 */

#include <linux/malloc.h>
#include <linux/smp_lock.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/blkdev.h> /* for blk_size */
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/shm.h>

#include <asm/pgtable.h>

spinlock_t swaplock = SPIN_LOCK_UNLOCKED;
unsigned int nr_swapfiles;

struct swap_list_t swap_list = {-1, -1};

struct swap_info_struct swap_info[MAX_SWAPFILES];

#define SWAPFILE_CLUSTER 256

static inline int scan_swap_map(struct swap_info_struct *si, unsigned short count)
{
	unsigned long offset;
	/* 
	 * We try to cluster swap pages by allocating them
	 * sequentially in swap.  Once we've allocated
	 * SWAPFILE_CLUSTER pages this way, however, we resort to
	 * first-free allocation, starting a new cluster.  This
	 * prevents us from scattering swap pages all over the entire
	 * swap partition, so that we reduce overall disk seek times
	 * between swap pages.  -- sct */
	if (si->cluster_nr) {
		while (si->cluster_next <= si->highest_bit) {
			offset = si->cluster_next++;
			if (si->swap_map[offset])
				continue;
			si->cluster_nr--;
			goto got_page;
		}
	}
	si->cluster_nr = SWAPFILE_CLUSTER;

	/* try to find an empty (even not aligned) cluster. */
	offset = si->lowest_bit;
 check_next_cluster:
	if (offset+SWAPFILE_CLUSTER-1 <= si->highest_bit)
	{
		int nr;
		for (nr = offset; nr < offset+SWAPFILE_CLUSTER; nr++)
			if (si->swap_map[nr])
			{
				offset = nr+1;
				goto check_next_cluster;
			}
		/* We found a completly empty cluster, so start
		 * using it.
		 */
		goto got_page;
	}
	/* No luck, so now go finegrined as usual. -Andrea */
	for (offset = si->lowest_bit; offset <= si->highest_bit ; offset++) {
		if (si->swap_map[offset])
			continue;
	got_page:
		if (offset == si->lowest_bit)
			si->lowest_bit++;
		if (offset == si->highest_bit)
			si->highest_bit--;
		si->swap_map[offset] = count;
		nr_swap_pages--;
		si->cluster_next = offset+1;
		return offset;
	}
	return 0;
}

swp_entry_t __get_swap_page(unsigned short count)
{
	struct swap_info_struct * p;
	unsigned long offset;
	swp_entry_t entry;
	int type, wrapped = 0;

	entry.val = 0;	/* Out of memory */
	if (count >= SWAP_MAP_MAX)
		goto bad_count;
	swap_list_lock();
	type = swap_list.next;
	if (type < 0)
		goto out;
	if (nr_swap_pages == 0)
		goto out;

	while (1) {
		p = &swap_info[type];
		if ((p->flags & SWP_WRITEOK) == SWP_WRITEOK) {
			swap_device_lock(p);
			offset = scan_swap_map(p, count);
			swap_device_unlock(p);
			if (offset) {
				entry = SWP_ENTRY(type,offset);
				type = swap_info[type].next;
				if (type < 0 ||
					p->prio != swap_info[type].prio) {
						swap_list.next = swap_list.head;
				} else {
					swap_list.next = type;
				}
				goto out;
			}
		}
		type = p->next;
		if (!wrapped) {
			if (type < 0 || p->prio != swap_info[type].prio) {
				type = swap_list.head;
				wrapped = 1;
			}
		} else
			if (type < 0)
				goto out;	/* out of swap space */
	}
out:
	swap_list_unlock();
	return entry;

bad_count:
	printk(KERN_ERR "get_swap_page: bad count %hd from %p\n",
	       count, __builtin_return_address(0));
	goto out;
}


/*
 * Caller has made sure that the swapdevice corresponding to entry
 * is still around or has not been recycled.
 */
void __swap_free(swp_entry_t entry, unsigned short count)
{
	struct swap_info_struct * p;
	unsigned long offset, type;

	if (!entry.val)
		goto out;

	type = SWP_TYPE(entry);
	if (type >= nr_swapfiles)
		goto bad_nofile;
	p = & swap_info[type];
	if (!(p->flags & SWP_USED))
		goto bad_device;
	offset = SWP_OFFSET(entry);
	if (offset >= p->max)
		goto bad_offset;
	if (!p->swap_map[offset])
		goto bad_free;
	swap_list_lock();
	if (p->prio > swap_info[swap_list.next].prio)
		swap_list.next = type;
	swap_device_lock(p);
	if (p->swap_map[offset] < SWAP_MAP_MAX) {
		if (p->swap_map[offset] < count)
			goto bad_count;
		if (!(p->swap_map[offset] -= count)) {
			if (offset < p->lowest_bit)
				p->lowest_bit = offset;
			if (offset > p->highest_bit)
				p->highest_bit = offset;
			nr_swap_pages++;
		}
	}
	swap_device_unlock(p);
	swap_list_unlock();
out:
	return;

bad_nofile:
	printk("swap_free: Trying to free nonexistent swap-page\n");
	goto out;
bad_device:
	printk("swap_free: Trying to free swap from unused swap-device\n");
	goto out;
bad_offset:
	printk("swap_free: offset exceeds max\n");
	goto out;
bad_free:
	printk("VM: Bad swap entry %08lx\n", entry.val);
	goto out;
bad_count:
	swap_device_unlock(p);
	swap_list_unlock();
	printk(KERN_ERR "VM: Bad count %hd current count %hd\n", count, p->swap_map[offset]);
	goto out;
}

/*
 * The swap entry has been read in advance, and we return 1 to indicate
 * that the page has been used or is no longer needed.
 *
 * Always set the resulting pte to be nowrite (the same as COW pages
 * after one process has exited).  We don't know just how many PTEs will
 * share this swap entry, so be cautious and let do_wp_page work out
 * what to do if a write is requested later.
 */
static inline void unuse_pte(struct vm_area_struct * vma, unsigned long address,
	pte_t *dir, swp_entry_t entry, struct page* page)
{
	pte_t pte = *dir;

	if (pte_none(pte))
		return;
	if (pte_present(pte)) {
		/* If this entry is swap-cached, then page must already
                   hold the right address for any copies in physical
                   memory */
		if (pte_page(pte) != page)
			return;
		/* We will be removing the swap cache in a moment, so... */
		ptep_mkdirty(dir);
		return;
	}
	if (pte_to_swp_entry(pte).val != entry.val)
		return;
	set_pte(dir, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	swap_free(entry);
	get_page(page);
	++vma->vm_mm->rss;
}

static inline void unuse_pmd(struct vm_area_struct * vma, pmd_t *dir,
	unsigned long address, unsigned long size, unsigned long offset,
	swp_entry_t entry, struct page* page)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*dir))
		return;
	if (pmd_bad(*dir)) {
		pmd_ERROR(*dir);
		pmd_clear(dir);
		return;
	}
	pte = pte_offset(dir, address);
	offset += address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		unuse_pte(vma, offset+address-vma->vm_start, pte, entry, page);
		address += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
}

static inline void unuse_pgd(struct vm_area_struct * vma, pgd_t *dir,
	unsigned long address, unsigned long size,
	swp_entry_t entry, struct page* page)
{
	pmd_t * pmd;
	unsigned long offset, end;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		pgd_ERROR(*dir);
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, address);
	offset = address & PGDIR_MASK;
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	if (address >= end)
		BUG();
	do {
		unuse_pmd(vma, pmd, address, end - address, offset, entry,
			  page);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
}

static void unuse_vma(struct vm_area_struct * vma, pgd_t *pgdir,
			swp_entry_t entry, struct page* page)
{
	unsigned long start = vma->vm_start, end = vma->vm_end;

	if (start >= end)
		BUG();
	do {
		unuse_pgd(vma, pgdir, start, end - start, entry, page);
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	} while (start && (start < end));
}

static void unuse_process(struct mm_struct * mm,
			swp_entry_t entry, struct page* page)
{
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	if (!mm)
		return;
	spin_lock(&mm->page_table_lock);
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		pgd_t * pgd = pgd_offset(mm, vma->vm_start);
		unuse_vma(vma, pgd, entry, page);
	}
	spin_unlock(&mm->page_table_lock);
	return;
}

/*
 * We completely avoid races by reading each swap page in advance,
 * and then search for the process using it.  All the necessary
 * page table adjustments can then be made atomically.
 */
static int try_to_unuse(unsigned int type)
{
	struct swap_info_struct * si = &swap_info[type];
	struct task_struct *p;
	struct page *page;
	swp_entry_t entry;
	int i;

	while (1) {
		/*
		 * Find a swap page in use and read it in.
		 */
		swap_device_lock(si);
		for (i = 1; i < si->max ; i++) {
			if (si->swap_map[i] > 0 && si->swap_map[i] != SWAP_MAP_BAD) {
				/*
				 * Prevent swaphandle from being completely
				 * unused by swap_free while we are trying
				 * to read in the page - this prevents warning
				 * messages from rw_swap_page_base.
				 */
				if (si->swap_map[i] != SWAP_MAP_MAX)
					si->swap_map[i]++;
				swap_device_unlock(si);
				goto found_entry;
			}
		}
		swap_device_unlock(si);
		break;

	found_entry:
		entry = SWP_ENTRY(type, i);

		/* Get a page for the entry, using the existing swap
                   cache page if there is one.  Otherwise, get a clean
                   page and read the swap into it. */
		page = read_swap_cache(entry);
		if (!page) {
			swap_free(entry);
  			return -ENOMEM;
		}
		if (PageSwapCache(page))
			delete_from_swap_cache(page);
		read_lock(&tasklist_lock);
		for_each_task(p)
			unuse_process(p->mm, entry, page);
		read_unlock(&tasklist_lock);
		shmem_unuse(entry, page);
		/* Now get rid of the extra reference to the temporary
                   page we've been using. */
		page_cache_release(page);
		/*
		 * Check for and clear any overflowed swap map counts.
		 */
		swap_free(entry);
		swap_list_lock();
		swap_device_lock(si);
		if (si->swap_map[i] > 0) {
			if (si->swap_map[i] != SWAP_MAP_MAX)
				printk("VM: Undead swap entry %08lx\n", 
								entry.val);
			nr_swap_pages++;
			si->swap_map[i] = 0;
		}
		swap_device_unlock(si);
		swap_list_unlock();
	}
	return 0;
}

asmlinkage long sys_swapoff(const char * specialfile)
{
	struct swap_info_struct * p = NULL;
	struct nameidata nd;
	int i, type, prev;
	int err;
	
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	err = user_path_walk(specialfile, &nd);
	if (err)
		goto out;

	lock_kernel();
	prev = -1;
	swap_list_lock();
	for (type = swap_list.head; type >= 0; type = swap_info[type].next) {
		p = swap_info + type;
		if ((p->flags & SWP_WRITEOK) == SWP_WRITEOK) {
			if (p->swap_file) {
				if (p->swap_file == nd.dentry)
				  break;
			} else {
				if (S_ISBLK(nd.dentry->d_inode->i_mode)
				    && (p->swap_device == nd.dentry->d_inode->i_rdev))
				  break;
			}
		}
		prev = type;
	}
	err = -EINVAL;
	if (type < 0) {
		swap_list_unlock();
		goto out_dput;
	}

	if (prev < 0) {
		swap_list.head = p->next;
	} else {
		swap_info[prev].next = p->next;
	}
	if (type == swap_list.next) {
		/* just pick something that's safe... */
		swap_list.next = swap_list.head;
	}
	nr_swap_pages -= p->pages;
	swap_list_unlock();
	p->flags = SWP_USED;
	err = try_to_unuse(type);
	if (err) {
		/* re-insert swap space back into swap_list */
		swap_list_lock();
		for (prev = -1, i = swap_list.head; i >= 0; prev = i, i = swap_info[i].next)
			if (p->prio >= swap_info[i].prio)
				break;
		p->next = i;
		if (prev < 0)
			swap_list.head = swap_list.next = p - swap_info;
		else
			swap_info[prev].next = p - swap_info;
		nr_swap_pages += p->pages;
		swap_list_unlock();
		p->flags = SWP_WRITEOK;
		goto out_dput;
	}
	if (p->swap_device)
		blkdev_put(nd.dentry->d_inode->i_bdev, BDEV_SWAP);
	path_release(&nd);

	nd.dentry = p->swap_file;
	p->swap_file = NULL;
	nd.mnt = p->swap_vfsmnt;
	p->swap_vfsmnt = NULL;
	p->swap_device = 0;
	vfree(p->swap_map);
	p->swap_map = NULL;
	p->flags = 0;
	err = 0;

out_dput:
	unlock_kernel();
	path_release(&nd);
out:
	return err;
}

int get_swaparea_info(char *buf)
{
	char * page = (char *) __get_free_page(GFP_KERNEL);
	struct swap_info_struct *ptr = swap_info;
	int i, j, len = 0, usedswap;

	if (!page)
		return -ENOMEM;

	len += sprintf(buf, "Filename\t\t\tType\t\tSize\tUsed\tPriority\n");
	for (i = 0 ; i < nr_swapfiles ; i++, ptr++) {
		if (ptr->flags & SWP_USED) {
			char * path = d_path(ptr->swap_file, ptr->swap_vfsmnt,
						page, PAGE_SIZE);

			len += sprintf(buf + len, "%-31s ", path);

			if (!ptr->swap_device)
				len += sprintf(buf + len, "file\t\t");
			else
				len += sprintf(buf + len, "partition\t");

			usedswap = 0;
			for (j = 0; j < ptr->max; ++j)
				switch (ptr->swap_map[j]) {
					case SWAP_MAP_BAD:
					case 0:
						continue;
					default:
						usedswap++;
				}
			len += sprintf(buf + len, "%d\t%d\t%d\n", ptr->pages << (PAGE_SHIFT - 10), 
				usedswap << (PAGE_SHIFT - 10), ptr->prio);
		}
	}
	free_page((unsigned long) page);
	return len;
}

int is_swap_partition(kdev_t dev) {
	struct swap_info_struct *ptr = swap_info;
	int i;

	for (i = 0 ; i < nr_swapfiles ; i++, ptr++) {
		if (ptr->flags & SWP_USED)
			if (ptr->swap_device == dev)
				return 1;
	}
	return 0;
}

/*
 * Written 01/25/92 by Simmule Turner, heavily changed by Linus.
 *
 * The swapon system call
 */
asmlinkage long sys_swapon(const char * specialfile, int swap_flags)
{
	struct swap_info_struct * p;
	struct nameidata nd;
	struct inode * swap_inode;
	unsigned int type;
	int i, j, prev;
	int error;
	static int least_priority = 0;
	union swap_header *swap_header = 0;
	int swap_header_version;
	int nr_good_pages = 0;
	unsigned long maxpages;
	int swapfilesize;
	struct block_device *bdev = NULL;
	
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	lock_kernel();
	p = swap_info;
	for (type = 0 ; type < nr_swapfiles ; type++,p++)
		if (!(p->flags & SWP_USED))
			break;
	error = -EPERM;
	if (type >= MAX_SWAPFILES)
		goto out;
	if (type >= nr_swapfiles)
		nr_swapfiles = type+1;
	p->flags = SWP_USED;
	p->swap_file = NULL;
	p->swap_vfsmnt = NULL;
	p->swap_device = 0;
	p->swap_map = NULL;
	p->lowest_bit = 0;
	p->highest_bit = 0;
	p->cluster_nr = 0;
	p->sdev_lock = SPIN_LOCK_UNLOCKED;
	p->max = 1;
	p->next = -1;
	if (swap_flags & SWAP_FLAG_PREFER) {
		p->prio =
		  (swap_flags & SWAP_FLAG_PRIO_MASK)>>SWAP_FLAG_PRIO_SHIFT;
	} else {
		p->prio = --least_priority;
	}
	error = user_path_walk(specialfile, &nd);
	if (error)
		goto bad_swap_2;

	p->swap_file = nd.dentry;
	p->swap_vfsmnt = nd.mnt;
	swap_inode = nd.dentry->d_inode;
	error = -EINVAL;

	if (S_ISBLK(swap_inode->i_mode)) {
		kdev_t dev = swap_inode->i_rdev;
		struct block_device_operations *bdops;

		p->swap_device = dev;
		set_blocksize(dev, PAGE_SIZE);
		
		bdev = swap_inode->i_bdev;
		bdops = devfs_get_ops(devfs_get_handle_from_inode(swap_inode));
		if (bdops) bdev->bd_op = bdops;

		error = blkdev_get(bdev, FMODE_READ|FMODE_WRITE, 0, BDEV_SWAP);
		if (error)
			goto bad_swap_2;
		set_blocksize(dev, PAGE_SIZE);
		error = -ENODEV;
		if (!dev || (blk_size[MAJOR(dev)] &&
		     !blk_size[MAJOR(dev)][MINOR(dev)]))
			goto bad_swap;
		error = -EBUSY;
		for (i = 0 ; i < nr_swapfiles ; i++) {
			if (i == type)
				continue;
			if (dev == swap_info[i].swap_device)
				goto bad_swap;
		}
		swapfilesize = 0;
		if (blk_size[MAJOR(dev)])
			swapfilesize = blk_size[MAJOR(dev)][MINOR(dev)]
				>> (PAGE_SHIFT - 10);
	} else if (S_ISREG(swap_inode->i_mode)) {
		error = -EBUSY;
		for (i = 0 ; i < nr_swapfiles ; i++) {
			if (i == type || !swap_info[i].swap_file)
				continue;
			if (swap_inode == swap_info[i].swap_file->d_inode)
				goto bad_swap;
		}
		swapfilesize = swap_inode->i_size >> PAGE_SHIFT;
	} else
		goto bad_swap;

	swap_header = (void *) __get_free_page(GFP_USER);
	if (!swap_header) {
		printk("Unable to start swapping: out of memory :-)\n");
		error = -ENOMEM;
		goto bad_swap;
	}

	lock_page(virt_to_page(swap_header));
	rw_swap_page_nolock(READ, SWP_ENTRY(type,0), (char *) swap_header, 1);

	if (!memcmp("SWAP-SPACE",swap_header->magic.magic,10))
		swap_header_version = 1;
	else if (!memcmp("SWAPSPACE2",swap_header->magic.magic,10))
		swap_header_version = 2;
	else {
		printk("Unable to find swap-space signature\n");
		error = -EINVAL;
		goto bad_swap;
	}
	
	switch (swap_header_version) {
	case 1:
		memset(((char *) swap_header)+PAGE_SIZE-10,0,10);
		j = 0;
		p->lowest_bit = 0;
		p->highest_bit = 0;
		for (i = 1 ; i < 8*PAGE_SIZE ; i++) {
			if (test_bit(i,(char *) swap_header)) {
				if (!p->lowest_bit)
					p->lowest_bit = i;
				p->highest_bit = i;
				p->max = i+1;
				j++;
			}
		}
		nr_good_pages = j;
		p->swap_map = vmalloc(p->max * sizeof(short));
		if (!p->swap_map) {
			error = -ENOMEM;		
			goto bad_swap;
		}
		for (i = 1 ; i < p->max ; i++) {
			if (test_bit(i,(char *) swap_header))
				p->swap_map[i] = 0;
			else
				p->swap_map[i] = SWAP_MAP_BAD;
		}
		break;

	case 2:
		/* Check the swap header's sub-version and the size of
                   the swap file and bad block lists */
		if (swap_header->info.version != 1) {
			printk(KERN_WARNING
			       "Unable to handle swap header version %d\n",
			       swap_header->info.version);
			error = -EINVAL;
			goto bad_swap;
		}

		p->lowest_bit  = 1;
		p->highest_bit = swap_header->info.last_page - 1;
		p->max	       = swap_header->info.last_page;

		maxpages = SWP_OFFSET(SWP_ENTRY(0,~0UL));
		if (p->max >= maxpages)
			p->max = maxpages-1;

		error = -EINVAL;
		if (swap_header->info.nr_badpages > MAX_SWAP_BADPAGES)
			goto bad_swap;
		
		/* OK, set up the swap map and apply the bad block list */
		if (!(p->swap_map = vmalloc (p->max * sizeof(short)))) {
			error = -ENOMEM;
			goto bad_swap;
		}

		error = 0;
		memset(p->swap_map, 0, p->max * sizeof(short));
		for (i=0; i<swap_header->info.nr_badpages; i++) {
			int page = swap_header->info.badpages[i];
			if (page <= 0 || page >= swap_header->info.last_page)
				error = -EINVAL;
			else
				p->swap_map[page] = SWAP_MAP_BAD;
		}
		nr_good_pages = swap_header->info.last_page -
				swap_header->info.nr_badpages -
				1 /* header page */;
		if (error) 
			goto bad_swap;
	}
	
	if (swapfilesize && p->max > swapfilesize) {
		printk(KERN_WARNING
		       "Swap area shorter than signature indicates\n");
		error = -EINVAL;
		goto bad_swap;
	}
	if (!nr_good_pages) {
		printk(KERN_WARNING "Empty swap-file\n");
		error = -EINVAL;
		goto bad_swap;
	}
	p->swap_map[0] = SWAP_MAP_BAD;
	p->flags = SWP_WRITEOK;
	p->pages = nr_good_pages;
	swap_list_lock();
	nr_swap_pages += nr_good_pages;
	printk(KERN_INFO "Adding Swap: %dk swap-space (priority %d)\n",
	       nr_good_pages<<(PAGE_SHIFT-10), p->prio);

	/* insert swap space into swap_list: */
	prev = -1;
	for (i = swap_list.head; i >= 0; i = swap_info[i].next) {
		if (p->prio >= swap_info[i].prio) {
			break;
		}
		prev = i;
	}
	p->next = i;
	if (prev < 0) {
		swap_list.head = swap_list.next = p - swap_info;
	} else {
		swap_info[prev].next = p - swap_info;
	}
	swap_list_unlock();
	error = 0;
	goto out;
bad_swap:
	if (bdev)
		blkdev_put(bdev, BDEV_SWAP);
bad_swap_2:
	if (p->swap_map)
		vfree(p->swap_map);
	nd.mnt = p->swap_vfsmnt;
	nd.dentry = p->swap_file;
	p->swap_device = 0;
	p->swap_file = NULL;
	p->swap_vfsmnt = NULL;
	p->swap_map = NULL;
	p->flags = 0;
	if (!(swap_flags & SWAP_FLAG_PREFER))
		++least_priority;
	path_release(&nd);
out:
	if (swap_header)
		free_page((long) swap_header);
	unlock_kernel();
	return error;
}

void si_swapinfo(struct sysinfo *val)
{
	unsigned int i;
	unsigned long freeswap = 0;
	unsigned long totalswap = 0;

	for (i = 0; i < nr_swapfiles; i++) {
		unsigned int j;
		if ((swap_info[i].flags & SWP_WRITEOK) != SWP_WRITEOK)
			continue;
		for (j = 0; j < swap_info[i].max; ++j) {
			switch (swap_info[i].swap_map[j]) {
				case SWAP_MAP_BAD:
					continue;
				case 0:
					freeswap++;
				default:
					totalswap++;
			}
		}
	}
	val->freeswap = freeswap;
	val->totalswap = totalswap;
	return;
}

/*
 * Verify that a swap entry is valid and increment its swap map count.
 * Kernel_lock is held, which guarantees existance of swap device.
 *
 * Note: if swap_map[] reaches SWAP_MAP_MAX the entries are treated as
 * "permanent", but will be reclaimed by the next swapoff.
 */
int swap_duplicate(swp_entry_t entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;
	int result = 0;

	/* Swap entry 0 is illegal */
	if (!entry.val)
		goto out;
	type = SWP_TYPE(entry);
	if (type >= nr_swapfiles)
		goto bad_file;
	p = type + swap_info;
	offset = SWP_OFFSET(entry);
	if (offset >= p->max)
		goto bad_offset;
	if (!p->swap_map[offset])
		goto bad_unused;
	/*
	 * Entry is valid, so increment the map count.
	 */
	swap_device_lock(p);
	if (p->swap_map[offset] < SWAP_MAP_MAX)
		p->swap_map[offset]++;
	else {
		static int overflow = 0;
		if (overflow++ < 5)
			printk("VM: swap entry overflow\n");
		p->swap_map[offset] = SWAP_MAP_MAX;
	}
	swap_device_unlock(p);
	result = 1;
out:
	return result;

bad_file:
	printk("Bad swap file entry %08lx\n", entry.val);
	goto out;
bad_offset:
	printk("Bad swap offset entry %08lx\n", entry.val);
	goto out;
bad_unused:
	printk("Unused swap offset entry in swap_dup %08lx\n", entry.val);
	goto out;
}

/*
 * Page lock needs to be held in all cases to prevent races with
 * swap file deletion.
 */
int swap_count(struct page *page)
{
	struct swap_info_struct * p;
	unsigned long offset, type;
	swp_entry_t entry;
	int retval = 0;

	entry.val = page->index;
	if (!entry.val)
		goto bad_entry;
	type = SWP_TYPE(entry);
	if (type >= nr_swapfiles)
		goto bad_file;
	p = type + swap_info;
	offset = SWP_OFFSET(entry);
	if (offset >= p->max)
		goto bad_offset;
	if (!p->swap_map[offset])
		goto bad_unused;
	retval = p->swap_map[offset];
out:
	return retval;

bad_entry:
	printk(KERN_ERR "swap_count: null entry!\n");
	goto out;
bad_file:
	printk("Bad swap file entry %08lx\n", entry.val);
	goto out;
bad_offset:
	printk("Bad swap offset entry %08lx\n", entry.val);
	goto out;
bad_unused:
	printk("Unused swap offset entry in swap_count %08lx\n", entry.val);
	goto out;
}

/*
 * Kernel_lock protects against swap device deletion.
 */
void get_swaphandle_info(swp_entry_t entry, unsigned long *offset, 
			kdev_t *dev, struct inode **swapf)
{
	unsigned long type;
	struct swap_info_struct *p;

	type = SWP_TYPE(entry);
	if (type >= nr_swapfiles) {
		printk("Internal error: bad swap-device\n");
		return;
	}

	p = &swap_info[type];
	*offset = SWP_OFFSET(entry);
	if (*offset >= p->max) {
		printk("rw_swap_page: weirdness\n");
		return;
	}
	if (p->swap_map && !p->swap_map[*offset]) {
		printk("VM: Bad swap entry %08lx\n", entry.val);
		return;
	}
	if (!(p->flags & SWP_USED)) {
		printk(KERN_ERR "rw_swap_page: "
			"Trying to swap to unused swap-device\n");
		return;
	}

	if (p->swap_device) {
		*dev = p->swap_device;
	} else if (p->swap_file) {
		*swapf = p->swap_file->d_inode;
	} else {
		printk(KERN_ERR "rw_swap_page: no swap file or device\n");
	}
	return;
}

/*
 * Kernel_lock protects against swap device deletion. Grab an extra
 * reference on the swaphandle so that it dos not become unused.
 */
int valid_swaphandles(swp_entry_t entry, unsigned long *offset)
{
	int ret = 0, i = 1 << page_cluster;
	unsigned long toff;
	struct swap_info_struct *swapdev = SWP_TYPE(entry) + swap_info;

	*offset = SWP_OFFSET(entry);
	toff = *offset = (*offset >> page_cluster) << page_cluster;

	swap_device_lock(swapdev);
	do {
		/* Don't read-ahead past the end of the swap area */
		if (toff >= swapdev->max)
			break;
		/* Don't read in bad or busy pages */
		if (!swapdev->swap_map[toff])
			break;
		if (swapdev->swap_map[toff] == SWAP_MAP_BAD)
			break;
		swapdev->swap_map[toff]++;
		toff++;
		ret++;
	} while (--i);
	swap_device_unlock(swapdev);
	return ret;
}
