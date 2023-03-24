// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel IFC VF NIC driver for virtio dataplane offloading
 *
 * Copyright (C) 2020 Intel Corporation.
 *
 * Author: Zhu Lingshan <lingshan.zhu@intel.com>
 *
 */

#include "ifcvf_base.h"

u16 ifcvf_set_vq_vector(struct ifcvf_hw *hw, u16 qid, int vector)
{
	struct virtio_pci_common_cfg __iomem *cfg = hw->common_cfg;

	vp_iowrite16(qid, &cfg->queue_select);
	vp_iowrite16(vector, &cfg->queue_msix_vector);

	return vp_ioread16(&cfg->queue_msix_vector);
}

u16 ifcvf_set_config_vector(struct ifcvf_hw *hw, int vector)
{
	struct virtio_pci_common_cfg __iomem *cfg = hw->common_cfg;

	vp_iowrite16(vector,  &cfg->msix_config);

	return vp_ioread16(&cfg->msix_config);
}

static void __iomem *get_cap_addr(struct ifcvf_hw *hw,
				  struct virtio_pci_cap *cap)
{
	u32 length, offset;
	u8 bar;

	length = le32_to_cpu(cap->length);
	offset = le32_to_cpu(cap->offset);
	bar = cap->bar;

	if (bar >= IFCVF_PCI_MAX_RESOURCE) {
		IFCVF_DBG(hw->pdev,
			  "Invalid bar number %u to get capabilities\n", bar);
		return NULL;
	}

	if (offset + length > pci_resource_len(hw->pdev, bar)) {
		IFCVF_DBG(hw->pdev,
			  "offset(%u) + len(%u) overflows bar%u's capability\n",
			  offset, length, bar);
		return NULL;
	}

	return hw->base[bar] + offset;
}

static int ifcvf_read_config_range(struct pci_dev *dev,
				   uint32_t *val, int size, int where)
{
	int ret, i;

	for (i = 0; i < size; i += 4) {
		ret = pci_read_config_dword(dev, where + i, val + i / 4);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int ifcvf_init_hw(struct ifcvf_hw *hw, struct pci_dev *pdev)
{
	struct virtio_pci_cap cap;
	u16 notify_off;
	int ret;
	u8 pos;
	u32 i;

	ret = pci_read_config_byte(pdev, PCI_CAPABILITY_LIST, &pos);
	if (ret < 0) {
		IFCVF_ERR(pdev, "Failed to read PCI capability list\n");
		return -EIO;
	}
	hw->pdev = pdev;

	while (pos) {
		ret = ifcvf_read_config_range(pdev, (u32 *)&cap,
					      sizeof(cap), pos);
		if (ret < 0) {
			IFCVF_ERR(pdev,
				  "Failed to get PCI capability at %x\n", pos);
			break;
		}

		if (cap.cap_vndr != PCI_CAP_ID_VNDR)
			goto next;

		switch (cap.cfg_type) {
		case VIRTIO_PCI_CAP_COMMON_CFG:
			hw->common_cfg = get_cap_addr(hw, &cap);
			IFCVF_DBG(pdev, "hw->common_cfg = %p\n",
				  hw->common_cfg);
			break;
		case VIRTIO_PCI_CAP_NOTIFY_CFG:
			pci_read_config_dword(pdev, pos + sizeof(cap),
					      &hw->notify_off_multiplier);
			hw->notify_bar = cap.bar;
			hw->notify_base = get_cap_addr(hw, &cap);
			hw->notify_base_pa = pci_resource_start(pdev, cap.bar) +
					le32_to_cpu(cap.offset);
			IFCVF_DBG(pdev, "hw->notify_base = %p\n",
				  hw->notify_base);
			break;
		case VIRTIO_PCI_CAP_ISR_CFG:
			hw->isr = get_cap_addr(hw, &cap);
			IFCVF_DBG(pdev, "hw->isr = %p\n", hw->isr);
			break;
		case VIRTIO_PCI_CAP_DEVICE_CFG:
			hw->dev_cfg = get_cap_addr(hw, &cap);
			hw->cap_dev_config_size = le32_to_cpu(cap.length);
			IFCVF_DBG(pdev, "hw->dev_cfg = %p\n", hw->dev_cfg);
			break;
		}

next:
		pos = cap.cap_next;
	}

	if (hw->common_cfg == NULL || hw->notify_base == NULL ||
	    hw->isr == NULL || hw->dev_cfg == NULL) {
		IFCVF_ERR(pdev, "Incomplete PCI capabilities\n");
		return -EIO;
	}

	hw->nr_vring = vp_ioread16(&hw->common_cfg->num_queues);

	for (i = 0; i < hw->nr_vring; i++) {
		vp_iowrite16(i, &hw->common_cfg->queue_select);
		notify_off = vp_ioread16(&hw->common_cfg->queue_notify_off);
		hw->vring[i].notify_addr = hw->notify_base +
			notify_off * hw->notify_off_multiplier;
		hw->vring[i].notify_pa = hw->notify_base_pa +
			notify_off * hw->notify_off_multiplier;
		hw->vring[i].irq = -EINVAL;
	}

	hw->lm_cfg = hw->base[IFCVF_LM_BAR];

	IFCVF_DBG(pdev,
		  "PCI capability mapping: common cfg: %p, notify base: %p\n, isr cfg: %p, device cfg: %p, multiplier: %u\n",
		  hw->common_cfg, hw->notify_base, hw->isr,
		  hw->dev_cfg, hw->notify_off_multiplier);

	hw->vqs_reused_irq = -EINVAL;
	hw->config_irq = -EINVAL;

	return 0;
}

u8 ifcvf_get_status(struct ifcvf_hw *hw)
{
	return vp_ioread8(&hw->common_cfg->device_status);
}

void ifcvf_set_status(struct ifcvf_hw *hw, u8 status)
{
	vp_iowrite8(status, &hw->common_cfg->device_status);
}

void ifcvf_reset(struct ifcvf_hw *hw)
{
	hw->config_cb.callback = NULL;
	hw->config_cb.private = NULL;

	ifcvf_set_status(hw, 0);
	/* flush set_status, make sure VF is stopped, reset */
	ifcvf_get_status(hw);
}

static void ifcvf_add_status(struct ifcvf_hw *hw, u8 status)
{
	if (status != 0)
		status |= ifcvf_get_status(hw);

	ifcvf_set_status(hw, status);
	ifcvf_get_status(hw);
}

u64 ifcvf_get_hw_features(struct ifcvf_hw *hw)
{
	struct virtio_pci_common_cfg __iomem *cfg = hw->common_cfg;
	u32 features_lo, features_hi;
	u64 features;

	vp_iowrite32(0, &cfg->device_feature_select);
	features_lo = vp_ioread32(&cfg->device_feature);

	vp_iowrite32(1, &cfg->device_feature_select);
	features_hi = vp_ioread32(&cfg->device_feature);

	features = ((u64)features_hi << 32) | features_lo;

	return features;
}

u64 ifcvf_get_features(struct ifcvf_hw *hw)
{
	return hw->hw_features;
}

int ifcvf_verify_min_features(struct ifcvf_hw *hw, u64 features)
{
	if (!(features & BIT_ULL(VIRTIO_F_ACCESS_PLATFORM)) && features) {
		IFCVF_ERR(hw->pdev, "VIRTIO_F_ACCESS_PLATFORM is not negotiated\n");
		return -EINVAL;
	}

	return 0;
}

u32 ifcvf_get_config_size(struct ifcvf_hw *hw)
{
	u32 net_config_size = sizeof(struct virtio_net_config);
	u32 blk_config_size = sizeof(struct virtio_blk_config);
	u32 cap_size = hw->cap_dev_config_size;
	u32 config_size;

	/* If the onboard device config space size is greater than
	 * the size of struct virtio_net/blk_config, only the spec
	 * implementing contents size is returned, this is very
	 * unlikely, defensive programming.
	 */
	switch (hw->dev_type) {
	case VIRTIO_ID_NET:
		config_size = min(cap_size, net_config_size);
		break;
	case VIRTIO_ID_BLOCK:
		config_size = min(cap_size, blk_config_size);
		break;
	default:
		config_size = 0;
		IFCVF_ERR(hw->pdev, "VIRTIO ID %u not supported\n", hw->dev_type);
	}

	return config_size;
}

void ifcvf_read_dev_config(struct ifcvf_hw *hw, u64 offset,
			   void *dst, int length)
{
	u8 old_gen, new_gen, *p;
	int i;

	WARN_ON(offset + length > hw->config_size);
	do {
		old_gen = vp_ioread8(&hw->common_cfg->config_generation);
		p = dst;
		for (i = 0; i < length; i++)
			*p++ = vp_ioread8(hw->dev_cfg + offset + i);

		new_gen = vp_ioread8(&hw->common_cfg->config_generation);
	} while (old_gen != new_gen);
}

void ifcvf_write_dev_config(struct ifcvf_hw *hw, u64 offset,
			    const void *src, int length)
{
	const u8 *p;
	int i;

	p = src;
	WARN_ON(offset + length > hw->config_size);
	for (i = 0; i < length; i++)
		vp_iowrite8(*p++, hw->dev_cfg + offset + i);
}

static void ifcvf_set_features(struct ifcvf_hw *hw, u64 features)
{
	struct virtio_pci_common_cfg __iomem *cfg = hw->common_cfg;

	vp_iowrite32(0, &cfg->guest_feature_select);
	vp_iowrite32((u32)features, &cfg->guest_feature);

	vp_iowrite32(1, &cfg->guest_feature_select);
	vp_iowrite32(features >> 32, &cfg->guest_feature);
}

static int ifcvf_config_features(struct ifcvf_hw *hw)
{
	ifcvf_set_features(hw, hw->req_features);
	ifcvf_add_status(hw, VIRTIO_CONFIG_S_FEATURES_OK);

	if (!(ifcvf_get_status(hw) & VIRTIO_CONFIG_S_FEATURES_OK)) {
		IFCVF_ERR(hw->pdev, "Failed to set FEATURES_OK status\n");
		return -EIO;
	}

	return 0;
}

u16 ifcvf_get_vq_state(struct ifcvf_hw *hw, u16 qid)
{
	struct ifcvf_lm_cfg __iomem *ifcvf_lm;
	void __iomem *avail_idx_addr;
	u16 last_avail_idx;
	u32 q_pair_id;

	ifcvf_lm = (struct ifcvf_lm_cfg __iomem *)hw->lm_cfg;
	q_pair_id = qid / 2;
	avail_idx_addr = &ifcvf_lm->vring_lm_cfg[q_pair_id].idx_addr[qid % 2];
	last_avail_idx = vp_ioread16(avail_idx_addr);

	return last_avail_idx;
}

int ifcvf_set_vq_state(struct ifcvf_hw *hw, u16 qid, u16 num)
{
	struct ifcvf_lm_cfg __iomem *ifcvf_lm;
	void __iomem *avail_idx_addr;
	u32 q_pair_id;

	ifcvf_lm = (struct ifcvf_lm_cfg __iomem *)hw->lm_cfg;
	q_pair_id = qid / 2;
	avail_idx_addr = &ifcvf_lm->vring_lm_cfg[q_pair_id].idx_addr[qid % 2];
	hw->vring[qid].last_avail_idx = num;
	vp_iowrite16(num, avail_idx_addr);

	return 0;
}

static int ifcvf_hw_enable(struct ifcvf_hw *hw)
{
	struct virtio_pci_common_cfg __iomem *cfg;
	u32 i;

	cfg = hw->common_cfg;
	for (i = 0; i < hw->nr_vring; i++) {
		if (!hw->vring[i].ready)
			break;

		vp_iowrite16(i, &cfg->queue_select);
		vp_iowrite64_twopart(hw->vring[i].desc, &cfg->queue_desc_lo,
				     &cfg->queue_desc_hi);
		vp_iowrite64_twopart(hw->vring[i].avail, &cfg->queue_avail_lo,
				      &cfg->queue_avail_hi);
		vp_iowrite64_twopart(hw->vring[i].used, &cfg->queue_used_lo,
				     &cfg->queue_used_hi);
		vp_iowrite16(hw->vring[i].size, &cfg->queue_size);
		ifcvf_set_vq_state(hw, i, hw->vring[i].last_avail_idx);
		vp_iowrite16(1, &cfg->queue_enable);
	}

	return 0;
}

static void ifcvf_hw_disable(struct ifcvf_hw *hw)
{
	u32 i;

	ifcvf_set_config_vector(hw, VIRTIO_MSI_NO_VECTOR);
	for (i = 0; i < hw->nr_vring; i++) {
		ifcvf_set_vq_vector(hw, i, VIRTIO_MSI_NO_VECTOR);
	}
}

int ifcvf_start_hw(struct ifcvf_hw *hw)
{
	ifcvf_reset(hw);
	ifcvf_add_status(hw, VIRTIO_CONFIG_S_ACKNOWLEDGE);
	ifcvf_add_status(hw, VIRTIO_CONFIG_S_DRIVER);

	if (ifcvf_config_features(hw) < 0)
		return -EINVAL;

	if (ifcvf_hw_enable(hw) < 0)
		return -EINVAL;

	ifcvf_add_status(hw, VIRTIO_CONFIG_S_DRIVER_OK);

	return 0;
}

void ifcvf_stop_hw(struct ifcvf_hw *hw)
{
	ifcvf_hw_disable(hw);
	ifcvf_reset(hw);
}

void ifcvf_notify_queue(struct ifcvf_hw *hw, u16 qid)
{
	vp_iowrite16(qid, hw->vring[qid].notify_addr);
}
