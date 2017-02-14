#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <kcl/kcl_drm_global.h>

struct drm_global_item {
	struct mutex mutex;
	void *object;
	int refcount;
};

static struct drm_global_item glob[DRM_GLOBAL_NUM];

void _kcl_drm_global_init(void)
{
	int i;

	for (i = 0; i < DRM_GLOBAL_NUM; ++i) {
		struct drm_global_item *item = &glob[i];
		mutex_init(&item->mutex);
		item->object = NULL;
		item->refcount = 0;
	}
}

void _kcl_drm_global_release(void)
{
	int i;
	for (i = 0; i < DRM_GLOBAL_NUM; ++i) {
		struct drm_global_item *item = &glob[i];
		BUG_ON(item->object != NULL);
		BUG_ON(item->refcount != 0);
	}
}

int _kcl_drm_global_item_ref(struct drm_global_reference *ref)
{
	int ret = 0;
	struct drm_global_item *item = &glob[ref->global_type];

	mutex_lock(&item->mutex);
	if (item->refcount == 0) {
		ref->object = kzalloc(ref->size, GFP_KERNEL);
		if (unlikely(ref->object == NULL)) {
			ret = -ENOMEM;
			goto error_unlock;
		}
		ret = ref->init(ref);
		if (unlikely(ret != 0))
			goto error_free;

		item->object = ref->object;
	} else {
		ref->object = item->object;
	}

	++item->refcount;
	mutex_unlock(&item->mutex);
	return 0;

error_free:
	kfree(ref->object);
	ref->object = NULL;
error_unlock:
	mutex_unlock(&item->mutex);
	return ret;
}
EXPORT_SYMBOL(_kcl_drm_global_item_ref);

void _kcl_drm_global_item_unref(struct drm_global_reference *ref)
{
	struct drm_global_item *item = &glob[ref->global_type];

	mutex_lock(&item->mutex);
	BUG_ON(item->refcount == 0);
	BUG_ON(ref->object != item->object);
	if (--item->refcount == 0) {
		ref->release(ref);
		item->object = NULL;
	}
	mutex_unlock(&item->mutex);
}
EXPORT_SYMBOL(_kcl_drm_global_item_unref);

