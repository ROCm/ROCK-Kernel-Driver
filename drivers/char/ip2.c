// ip2.c
// This is a dummy module to make the firmware available when needed
// and allows it to be unloaded when not. Rumor is the __initdata 
// macro doesn't always works on all platforms so we use this kludge.
// If not compiled as a module it just makes fip_firm avaliable then
//  __initdata should work as advertized
//

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/wait.h>

#ifndef __init
#define __init
#endif
#ifndef __initfunc
#define __initfunc(a) a
#endif
#ifndef __initdata
#define __initdata
#endif

#include "./ip2/ip2types.h"		
#include "./ip2/fip_firm.h"		// the meat

int
ip2_loadmain(int *, int  *, unsigned char *, int ); // ref into ip2main.c

#ifdef MODULE
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#	define MODVERSIONS
#endif
#ifdef MODVERSIONS
#	include <linux/modversions.h>
#endif

static int io[IP2_MAX_BOARDS]= { 0,};
static int irq[IP2_MAX_BOARDS] = { 0,}; 

#	if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
		MODULE_AUTHOR("Doug McNash");
		MODULE_DESCRIPTION("Computone IntelliPort Plus Driver");
		MODULE_PARM(irq,"1-"__MODULE_STRING(IP2_MAX_BOARDS) "i");
		MODULE_PARM_DESC(irq,"Interrupts for IntelliPort Cards");
		MODULE_PARM(io,"1-"__MODULE_STRING(IP2_MAX_BOARDS) "i");
		MODULE_PARM_DESC(io,"I/O ports for IntelliPort Cards");
#	endif	/* LINUX_VERSION */


//======================================================================
int
init_module(void)
{
	int rc;

	MOD_INC_USE_COUNT;	// hold till done 
		
	rc = ip2_loadmain(io,irq,(unsigned char *)fip_firm,sizeof(fip_firm));
	// The call to lock and load main, create dep 

	MOD_DEC_USE_COUNT;	//done - kerneld now can unload us
	return rc;
}

//======================================================================
int
ip2_init(void)
{
	// call to this is int tty_io.c so we need this
	return 0;
}

//======================================================================
void
cleanup_module(void) 
{
}

#else	// !MODULE 

#ifndef NULL
# define NULL		((void *) 0)
#endif

int
ip2_init(void) {
	return ip2_loadmain(NULL,NULL,(unsigned char *)fip_firm,sizeof(fip_firm));
}

#endif /* !MODULE */
