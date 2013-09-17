#ifndef _BTRFS_SYSFS_H_
#define _BTRFS_SYSFS_H_

enum btrfs_feature_set {
	FEAT_COMPAT,
	FEAT_COMPAT_RO,
	FEAT_INCOMPAT,
	FEAT_MAX
};

struct btrfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct btrfs_attr *, struct btrfs_fs_info *, char *);
	ssize_t (*store)(struct btrfs_attr *, struct btrfs_fs_info *,
			 const char *, size_t);
};

#define __INIT_BTRFS_ATTR(_name, _mode, _show, _store)			\
{									\
	.attr	= { .name = __stringify(_name), .mode = _mode },	\
	.show	= _show,						\
	.store	= _store,						\
}

#define BTRFS_ATTR(_name, _mode, _show, _store)				\
static struct btrfs_attr btrfs_attr_##_name =				\
			__INIT_BTRFS_ATTR(_name, _mode, _show, _store)
#define BTRFS_ATTR_LIST(_name)    (&btrfs_attr_##_name.attr),

struct btrfs_feature_attr {
	struct attribute attr;		/* per-fs/global show, no store */
	enum btrfs_feature_set feature_set;
	u64 feature_bit;
};

#define BTRFS_FEAT_ATTR(_name, _feature_set, _prefix, _feature_bit)	     \
static struct btrfs_feature_attr btrfs_attr_##_name = {			     \
	.attr		= { .name = __stringify(_name), .mode = S_IRUGO, },  \
	.feature_set	= _feature_set,					     \
	.feature_bit	= _prefix ##_## _feature_bit,			     \
}
#define BTRFS_FEAT_ATTR_LIST(_name)    (&btrfs_attr_##_name.attr),

#define BTRFS_FEAT_ATTR_COMPAT(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_COMPAT, BTRFS_FEATURE_COMPAT, feature)
#define BTRFS_FEAT_ATTR_COMPAT_RO(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_COMPAT_RO, BTRFS_FEATURE_COMPAT, feature)
#define BTRFS_FEAT_ATTR_INCOMPAT(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_INCOMPAT, BTRFS_FEATURE_INCOMPAT, feature)

/* convert from attribute */
#define to_btrfs_attr(a) container_of(a, struct btrfs_attr, attr)
#define to_btrfs_feature_attr(a) \
			container_of(a, struct btrfs_feature_attr, attr)
char *btrfs_printable_features(enum btrfs_feature_set set, u64 flags);
extern const char * const btrfs_feature_set_names[FEAT_MAX];
#endif /* _BTRFS_SYSFS_H_ */
