#include <linux/mm.h>

#ifndef HAVE_FOLLOW_PFN
int _kcl_follow_pfn(struct vm_area_struct *vma, unsigned long address,
        unsigned long *pfn)
{
        int ret = -EINVAL;
#ifdef HAVE_FOLLOW_PFNMAP_START
        struct follow_pfnmap_args args = {
                .vma = vma,
                .address = address,
        };
        ret = follow_pfnmap_start(&args);

        if (ret)
                return ret;
        *pfn = args.pfn;
        follow_pfnmap_end(&args);
#else
        spinlock_t *ptl;
        pte_t *ptep;
        ret = follow_pte(vma, address, &ptep, &ptl);

        if (ret)
                return ret;
        *pfn = pte_pfn(ptep_get(ptep));
        pte_unmap_unlock(ptep, ptl);
#endif
        return 0;
}

EXPORT_SYMBOL(_kcl_follow_pfn);
#endif
