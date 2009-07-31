#ifndef __EFRM_INTERNAL_H__
#define __EFRM_INTERNAL_H__


struct filter_resource {
	struct efrm_resource rs;
	struct vi_resource *pt;
	int filter_idx;
};

#define filter_resource(rs1)  container_of((rs1), struct filter_resource, rs)


struct efrm_client {
	void *user_data;
	struct list_head link;
	struct efrm_client_callbacks *callbacks;
	struct efhw_nic *nic;
	int ref_count;
	struct list_head resources;
};


extern void efrm_client_add_resource(struct efrm_client *,
				     struct efrm_resource *);

extern int efrm_buffer_table_size(void);


static inline void efrm_resource_init(struct efrm_resource *rs,
				      int type, int instance)
{
	EFRM_ASSERT(instance >= 0);
	EFRM_ASSERT(type >= 0 && type < EFRM_RESOURCE_NUM);
	rs->rs_ref_count = 1;
	rs->rs_handle.handle = (type << 28u) |
		(((unsigned)jiffies & 0xfff) << 16) | instance;
}


#endif  /* __EFRM_INTERNAL_H__ */
