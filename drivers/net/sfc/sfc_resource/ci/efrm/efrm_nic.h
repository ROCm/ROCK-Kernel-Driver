#ifndef __EFRM_NIC_H__
#define __EFRM_NIC_H__

#include <ci/efhw/efhw_types.h>


struct efrm_nic_per_vi {
	unsigned long state;
	struct vi_resource *vi;
};


struct efrm_nic {
	struct efhw_nic efhw_nic;
	struct list_head link;
	struct list_head clients;
	struct efrm_nic_per_vi *vis;
};


#define efrm_nic(_efhw_nic)				\
  container_of(_efhw_nic, struct efrm_nic, efhw_nic)



#endif  /* __EFRM_NIC_H__ */
