#ifndef _LINUX_HUGETLB_H
#define _LINUX_HUGETLB_H

#ifdef CONFIG_HUGETLB_PAGE
static inline int is_vm_hugetlb_page(struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_HUGETLB;
}

int copy_hugetlb_page_range(struct mm_struct *, struct mm_struct *, struct vm_area_struct *);
int follow_hugetlb_page(struct mm_struct *, struct vm_area_struct *,
		struct page **, struct vm_area_struct **, unsigned long *, int *, int);
int free_hugepages(struct vm_area_struct *);
int hugetlb_prefault(struct address_space *, struct vm_area_struct *);
#else /* !CONFIG_HUGETLB_PAGE */
static inline int is_vm_hugetlb_page(struct vm_area_struct *vma)
{
	return 0;
}

static inline int
copy_hugetlb_page_range(struct mm_struct *src, struct mm_struct *dst,
			struct vm_area_struct *vma)
{
	return -ENOSYS;
}

static inline int
follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
		struct page **pages, struct vm_area_struct **vmas,
		unsigned long *start, int *len, int i)
{
	return -ENOSYS;
}

static inline int free_hugepages(struct vm_area_struct *vma)
{
	return -EINVAL;
}

static inline int
hugetlb_prefault(struct address_space *mapping, struct vm_area_struct *vma)
{
	return -ENOSYS;
}
#endif /* !CONFIG_HUGETLB_PAGE */

#ifdef CONFIG_HUGETLBFS
extern struct file_operations hugetlbfs_file_operations;
extern struct vm_operations_struct hugetlb_vm_ops;
struct file *hugetlb_zero_setup(size_t);

static inline int is_file_hugetlb_page(struct file *file)
{
	return file->f_op == &hugetlbfs_file_operations;
}

static inline void set_file_hugetlb_page(struct file *file)
{
	file->f_op = &hugetlbfs_file_operations;
}
#else /* !CONFIG_HUGETLBFS */
static inline int is_file_hugetlb_page(struct file *file)
{
	return 0;
}

static inline void set_file_hugetlb_page(struct file *file)
{
}

static inline struct file *hugetlb_zero_setup(size_t size)
{
	return ERR_PTR(-ENOSYS);
}
#endif /* !CONFIG_HUGETLBFS */

#endif /* _LINUX_HUGETLB_H */
