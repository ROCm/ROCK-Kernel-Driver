#ifndef _UNSUPPORTED_FEATURE_H_
#define _UNSUPPORTED_FEATURE_H_

#include <linux/types.h>

struct module;

#ifdef CONFIG_SUSE_KERNEL_SUPPORTED
struct unsupported_feature {
	const char *subsys_name;
	bool allowed;
};

static inline bool suse_allow_unsupported(struct unsupported_feature *uf)
{
	return uf->allowed;
}

extern struct kernel_param_ops suse_allow_unsupported_param_ops;
void suse_mark_unsupported(const struct unsupported_feature *uf,
			   struct module *module);

#define DECLARE_SUSE_UNSUPPORTED_FEATURE(name)				    \
extern struct unsupported_feature name ##__allow_unsupported;		    \
static inline bool name ## _allow_unsupported(void)			    \
{									    \
	return suse_allow_unsupported(&name ##__allow_unsupported);	    \
}									    \
									    \
static inline void name ## _mark_unsupported(void)			    \
{									    \
	suse_mark_unsupported(&name ##__allow_unsupported, THIS_MODULE);    \
}

#define DEFINE_SUSE_UNSUPPORTED_FEATURE(name) \
struct unsupported_feature name ##__allow_unsupported = {		    \
	.subsys_name = __stringify(name),				    \
	.allowed = false,						    \
};									    \
module_param_cb(allow_unsupported, &suse_allow_unsupported_param_ops,	    \
		&name ## __allow_unsupported, 0644);			    \
MODULE_PARM_DESC(allow_unsupported,					    \
		 "Allow using feature that are out of supported scope");

#else
#define DECLARE_SUSE_UNSUPPORTED_FEATURE(name)				    \
static inline bool name ## _allow_unsupported(void) { return true; }	    \
static inline void name ## _mark_unsupported(void) { }
#define DEFINE_SUSE_UNSUPPORTED_FEATURE(name)
#endif

#endif /* _UNSUPPORTED_FEATURE_H_ */
