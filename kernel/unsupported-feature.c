#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/unsupported-feature.h>

static inline struct unsupported_feature *
to_unsupported_feature(const struct kernel_param *kp)
{
	return kp->arg;
}

static int suse_set_allow_unsupported(const char *buffer,
				      const struct kernel_param *kp)
{
	int ret;
	struct unsupported_feature *uf = to_unsupported_feature(kp);
	struct kernel_param dummy_kp = *kp;
	bool newval;

	dummy_kp.arg = &newval;

	ret = param_set_bool(buffer, &dummy_kp);
	if (ret)
		return ret;
	if (uf->allowed && !newval) {
		pr_info("%s: disallowing unsupported features, kernel remains tainted.\n",
			uf->subsys_name);
		uf->allowed = false;
	} else if (!uf->allowed && newval) {
		pr_info("%s: allowing unsupported features, kernel tainted.\n",
			uf->subsys_name);
		add_taint(TAINT_NO_SUPPORT, LOCKDEP_STILL_OK);
		uf->allowed = true;
	}
	return 0;
}

static int suse_get_allow_unsupported(char *buffer,
				      const struct kernel_param *kp)
{
	struct unsupported_feature *uf = to_unsupported_feature(kp);
	struct kernel_param dummy_kp = *kp;

	dummy_kp.arg = &uf->allowed;
	return param_get_bool(buffer, &dummy_kp);
}

struct kernel_param_ops suse_allow_unsupported_param_ops = {
	.set = suse_set_allow_unsupported,
	.get = suse_get_allow_unsupported,
};
EXPORT_SYMBOL_GPL(suse_allow_unsupported_param_ops);

/* including above breaks kABI due to struct module becoming defined */
#include <linux/module.h>

void suse_mark_unsupported(const struct unsupported_feature *uf,
			   struct module *module)
{
	if (module && !test_and_set_bit(TAINT_NO_SUPPORT, &module->taints))
		pr_warn("%s: marking %s unsupported\n",
		        uf->subsys_name, module_name(module));
}
EXPORT_SYMBOL_GPL(suse_mark_unsupported);


