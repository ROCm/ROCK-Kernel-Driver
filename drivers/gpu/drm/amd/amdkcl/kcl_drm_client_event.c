#include <kcl/kcl_drm_client_event.h>

#ifndef HAVE_DRM_CLIENT_DEV_RESUME
void drm_client_dev_resume(struct drm_device *dev, bool holds_console_lock)
{
	drm_fb_helper_set_suspend_unlocked(dev->fb_helper, holds_console_lock);
}
EXPORT_SYMBOL(drm_client_dev_resume);

void drm_client_dev_suspend(struct drm_device *dev, bool holds_console_lock)
{
	bool suspend = !holds_console_lock;
	drm_fb_helper_set_suspend_unlocked(dev->fb_helper, suspend);
}
EXPORT_SYMBOL(drm_client_dev_suspend);
#endif
