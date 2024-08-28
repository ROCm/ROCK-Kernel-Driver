#include <linux/mm.h>

#ifndef HAVE_FOLLOW_PFN
int _kcl_follow_pfn(struct vm_area_struct *vma, unsigned long address,
        unsigned long *pfn)
{
        int ret = -EINVAL;
        spinlock_t *ptl;
        pte_t *ptep;

        ret = follow_pte(vma, address, &ptep, &ptl);
        if (ret)
                return ret;
        *pfn = pte_pfn(ptep_get(ptep));
        pte_unmap_unlock(ptep, ptl);
        return 0;
}

EXPORT_SYMBOL(_kcl_follow_pfn);
#endif
