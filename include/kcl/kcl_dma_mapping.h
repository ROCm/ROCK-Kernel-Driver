/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_DMA_MAPPING_H
#define AMDKCL_DMA_MAPPING_H

#include <linux/dma-mapping.h>
#include <kcl/kcl_mem_encrypt.h>

/*
 * commit v4.8-11962-ga9a62c938441
 * dma-mapping: introduce the DMA_ATTR_NO_WARN attribute
 */
#ifndef DMA_ATTR_NO_WARN
#define DMA_ATTR_NO_WARN (0UL)
#endif

#ifdef HAVE_LINUX_DMA_ATTRS_H
static inline
void _kcl_convert_long_to_dma_attrs(struct dma_attrs *dma_attrs,
							unsigned long attrs)
{
	int i;

	init_dma_attrs(dma_attrs);

	for (i = 0; i < DMA_ATTR_MAX; i++) {
		if (attrs & (1 << i))
			dma_set_attr(i, dma_attrs);
	}
}
#endif

#ifndef HAVE_DMA_MAP_SGTABLE
#ifdef HAVE_LINUX_DMA_ATTRS_H
static inline
int _kcl_dma_map_sg_attrs(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir, unsigned long attrs)
{
	struct dma_attrs dma_attrs;

	_kcl_convert_long_to_dma_attrs(&dma_attrs, attrs);
	return dma_map_sg_attrs(dev, sg, nents, dir, &dma_attrs);
}

static inline
void _kcl_dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sg,
				      int nents, enum dma_data_direction dir,
				      unsigned long attrs)

{
	struct dma_attrs dma_attrs;

	_kcl_convert_long_to_dma_attrs(&dma_attrs, attrs);
	dma_unmap_sg_attrs(dev, sg, nents, dir, &dma_attrs);
}

#else
static inline
int _kcl_dma_map_sg_attrs(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir, unsigned long attrs)
{
	return dma_map_sg_attrs(dev, sg, nents, dir, attrs);
}
static inline
void _kcl_dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sg,
				      int nents, enum dma_data_direction dir,
				      unsigned long attrs)
{
	dma_unmap_sg_attrs(dev, sg, nents, dir, attrs);
}
#endif /* HAVE_LINUX_DMA_ATTRS_H */

static inline int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
		enum dma_data_direction dir, unsigned long attrs)
{
	int nents;

	nents = _kcl_dma_map_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
	if (nents <= 0)
		return -EINVAL;
	sgt->nents = nents;
	return 0;
}

static inline void dma_unmap_sgtable(struct device *dev, struct sg_table *sgt,
		enum dma_data_direction dir, unsigned long attrs)
{
	_kcl_dma_unmap_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
}
#endif

#ifndef HAVE_DMA_MAP_RESOURCE
static inline dma_addr_t dma_map_resource(struct device *dev,
                                          phys_addr_t phys_addr,
                                          size_t size,
                                          enum dma_data_direction dir,
                                          unsigned long attrs)
{
        pr_warn_once("%s is not supported\n", __func__);

        return phys_addr;
}

static inline void dma_unmap_resource(struct device *dev, dma_addr_t addr,
                                      size_t size, enum dma_data_direction dir,
                                      unsigned long attrs)
{
        pr_warn_once("%s is not supported\n", __func__);
}
#endif

#endif
