#ifndef AMDKCL_APPLE_GMUX_H
#define AMDKCL_APPLE_GMUX_H

#include <linux/apple-gmux.h>
#include <linux/pnp.h>

#ifndef HAVE_APPLE_GMUX_DETECT
#if IS_ENABLED(CONFIG_APPLE_GMUX)
static inline bool apple_gmux_detect(struct pnp_dev *pnp_dev, bool *indexed_ret)
{
        pr_warn_once("legacy kernel without apple_gmux_detect()\n");
        return false;
}
#else
static inline bool apple_gmux_detect(struct pnp_dev *pnp_dev, bool *indexed_ret)
{
        return false;
}
#endif
#endif

#endif /* AMDKCL_APPLE_GMUX_H */
