#ifndef _XEN_PVONHVM_H
#define _XEN_PVONHVM_H

#ifdef CONFIG_XEN
#define xen_pvonhvm_unplug 0
#else
extern int xen_pvonhvm_unplug;
#endif

#define XEN_PVONHVM_UNPLUG_IDE_DISKS (1 << 1)
#define XEN_PVONHVM_UNPLUG_NICS      (1 << 2)
#define XEN_PVONHVM_UNPLUG_NEVER     (1 << 3)
#define XEN_PVONHVM_UNPLUG_ALL       (XEN_PVONHVM_UNPLUG_IDE_DISKS | XEN_PVONHVM_UNPLUG_NICS)
#define xen_pvonhvm_unplugged_disks  (xen_pvonhvm_unplug & XEN_PVONHVM_UNPLUG_IDE_DISKS)
#define xen_pvonhvm_unplugged_nics   (xen_pvonhvm_unplug & XEN_PVONHVM_UNPLUG_NICS)

#endif /* _XEN_PVONHVM_H */
