#ifndef __BEN_VLAN_PROC_INC__
#define __BEN_VLAN_PROC_INC__

#ifdef CONFIG_PROC_FS
int vlan_proc_init(void);

int vlan_proc_rem_dev(struct net_device *vlandev);
int vlan_proc_add_dev (struct net_device *vlandev);
void vlan_proc_cleanup (void);

#define	VLAN_PROC_BUFSZ	(4096)	/* buffer size for printing proc info */

#else /* No CONFIG_PROC_FS */

#define vlan_proc_init()	(0)
#define vlan_proc_cleanup()	do {} while(0)
#define vlan_proc_add_dev(dev)	((void)(dev), 0)
#define vlan_proc_rem_dev(dev)	((void)(dev), 0)

#endif

#endif /* !(__BEN_VLAN_PROC_INC__) */
