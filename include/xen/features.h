/******************************************************************************
 * features.h
 *
 * Query the features reported by Xen.
 *
 * Copyright (c) 2006, Ian Campbell
 */

#ifndef __XEN_FEATURES_H__
#define __XEN_FEATURES_H__

#include <xen/interface/features.h>
#include <xen/interface/version.h>

void xen_setup_features(void);

extern u8 xen_features[XENFEAT_NR_SUBMAPS * 32];

static inline int xen_feature(int flag)
{
	return xen_features[flag];
}

/*
 * unmodified_drivers/linux-2.6/platform-pci/platform-pci.c from xen-kmp has to
 * include a new header to get the unplug defines from xen/xen_pvonhvm.h.  To
 * allow compiliation of xen-kmp with older kernel-source a guard has to be
 * provided to skip inclusion of the new header.  An easy place was
 * xen/features.h, but any header included by platform-pci.c would do.
 */
#ifndef CONFIG_XEN
#define HAVE_XEN_PVONHVM_UNPLUG 1
#endif

#endif /* __XEN_FEATURES_H__ */
