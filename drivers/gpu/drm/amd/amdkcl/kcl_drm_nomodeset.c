// SPDX-License-Identifier: GPL-2.0

#ifndef HAVE_DRM_FIRMWARE_DRIVERS_ONLY

static bool drm_nomodeset;

bool drm_firmware_drivers_only(void)
{
        return drm_nomodeset;
}
EXPORT_SYMBOL(drm_firmware_drivers_only);

static int __init disable_modeset(char *str)
{
        drm_nomodeset = true;

        pr_warn("Booted with the nomodeset parameter. Only the system framebuffer will be available\n");

        return 1;
}

/* Disable kernel modesetting */
__setup("nomodeset", disable_modeset);

#endif
