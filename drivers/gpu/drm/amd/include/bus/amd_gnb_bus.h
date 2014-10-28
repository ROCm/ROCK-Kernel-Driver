/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */

#ifndef __AMD_GNB_BUS_H__
#define __AMD_GNB_BUS_H__

#include <linux/types.h>

enum amd_gnb_bus_ip {
	AMD_GNB_IP_ACP_DMA,
	AMD_GNB_IP_ACP_I2S,
	AMD_GNB_IP_ACP_PCM,
	AMD_GNB_IP_ISP,
	AMD_GNB_IP_NUM
};

struct amd_gnb_bus_dev {
	struct device dev; /* generic device interface */
	enum amd_gnb_bus_ip ip;
	/* private data can be acp_handle/isp_handle etc.*/
	void *private_data;
};

struct amd_gnb_bus_driver {
	const char *name;
	enum amd_gnb_bus_ip ip;
	int (*probe)(struct amd_gnb_bus_dev *dev); /* New device inserted */
	int (*remove)(struct amd_gnb_bus_dev *dev); /* Device removed */
	int (*suspend)(struct amd_gnb_bus_dev *dev, pm_message_t state);
	int (*resume)(struct amd_gnb_bus_dev *dev);
	void (*shutdown)(struct amd_gnb_bus_dev *dev);
	struct device_driver driver;  /* generic device driver interface */
};

#define amd_gnb_to_acp_device(x) container_of((x), \
					     struct amd_gnb_bus_dev_acp, \
					     base)
#define amd_gnb_to_isp_device(x) container_of((x), \
					     struct amd_gnb_bus_dev_isp, \
					     base)
#define amd_gnb_parent_to_pci_device(x) container_of((x)->dev.parent, \
						     struct pci_dev,  \
						     dev)

int amd_gnb_bus_register_device(struct amd_gnb_bus_dev *dev);
void amd_gnb_bus_unregister_device(struct amd_gnb_bus_dev *dev);
int amd_gnb_bus_device_init(struct amd_gnb_bus_dev *bus_dev,
			    enum amd_gnb_bus_ip ip,
			    char *dev_name,
			    void *handle,
			    struct device *parent);
int amd_gnb_bus_register_driver(struct amd_gnb_bus_driver *drv,
			       struct module *owner,
			       const char *mod_name);
void amd_gnb_bus_unregister_driver(struct amd_gnb_bus_driver *drv);

#endif
