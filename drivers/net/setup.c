
/*
 *	New style setup code for the network devices
 */
 
#include <linux/config.h>
#include <linux/netdevice.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netlink.h>

extern int dmascc_init(void);

extern int scc_enet_init(void); 
extern int fec_enet_init(void); 
extern int sdla_setup(void); 
extern int sdla_c_setup(void); 
extern int lmc_setup(void);

extern int madgemc_probe(void);

/*
 *	Devices in this list must do new style probing. That is they must
 *	allocate their own device objects and do their own bus scans.
 */

struct net_probe
{
	int (*probe)(void);
	int status;	/* non-zero if autoprobe has failed */
};
 
static struct net_probe pci_probes[] __initdata = {
	/*
	 *	Early setup devices
	 */

#if defined(CONFIG_DMASCC)
	{dmascc_init, 0},
#endif	
#if defined(CONFIG_SDLA)
	{sdla_c_setup, 0},
#endif
#if defined(CONFIG_SCC_ENET)
        {scc_enet_init, 0},
#endif
#if defined(CONFIG_FEC_ENET)
        {fec_enet_init, 0},
#endif
#if defined(CONFIG_LANMEDIA)
	{lmc_setup, 0},
#endif
	 
/*
 *	Token Ring Drivers
 */  
#ifdef CONFIG_MADGEMC
	{madgemc_probe, 0},
#endif
 
	{NULL, 0},
};


/*
 *	Run the updated device probes. These do not need a device passed
 *	into them.
 */
 
void __init net_device_init(void)
{
	struct net_probe *p = pci_probes;

	while (p->probe != NULL)
	{
		p->status = p->probe();
		p++;
	}
}
