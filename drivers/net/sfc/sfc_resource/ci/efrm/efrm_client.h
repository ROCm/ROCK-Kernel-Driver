#ifndef __EFRM_CLIENT_H__
#define __EFRM_CLIENT_H__


struct efrm_client;


struct efrm_client_callbacks {
	/* Called before device is reset.  Callee may block. */
	void (*pre_reset)(struct efrm_client *, void *user_data);
	void (*stop)(struct efrm_client *, void *user_data);
	void (*restart)(struct efrm_client *, void *user_data);
};


#define EFRM_IFINDEX_DEFAULT  -1


/* NB. Callbacks may be invoked even before this returns. */
extern int  efrm_client_get(int ifindex, struct efrm_client_callbacks *,
			    void *user_data, struct efrm_client **client_out);
extern void efrm_client_put(struct efrm_client *);

extern struct efhw_nic *efrm_client_get_nic(struct efrm_client *);

#if 0
/* For each resource type... */
extern void efrm_x_resource_resume(struct x_resource *);
#endif


#endif  /* __EFRM_CLIENT_H__ */
