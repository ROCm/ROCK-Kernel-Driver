/*********************************************************************
 *                
 * Filename:      irsyms.c
 * Version:       0.9
 * Description:   IrDA module symbols
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Dec 15 13:55:39 1997
 * Modified at:   Wed Jan  5 15:12:41 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997, 1999-2000 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 2000-2001 Jean Tourrilhes <jt@hpl.hp.com>
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <linux/config.h>
#include <linux/module.h>

#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/if_arp.h>		/* ARPHRD_IRDA */

#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irias_object.h>
#include <net/irda/irttp.h>
#include <net/irda/irda_device.h>
#include <net/irda/wrapper.h>
#include <net/irda/timer.h>
#include <net/irda/parameters.h>
#include <net/irda/crc.h>

extern struct proc_dir_entry *proc_irda;

extern void irda_proc_register(void);
extern void irda_proc_unregister(void);
extern int  irda_sysctl_register(void);
extern void irda_sysctl_unregister(void);

extern int irda_proto_init(void);
extern void irda_proto_cleanup(void);

extern int irda_device_init(void);
extern int irlan_init(void);
extern int irlan_client_init(void);
extern int irlan_server_init(void);
extern int ircomm_init(void);
extern int ircomm_tty_init(void);
extern int irlpt_client_init(void);
extern int irlpt_server_init(void);

extern int  irsock_init(void);
extern void irsock_cleanup(void);
extern int  irlap_driver_rcv(struct sk_buff *, struct net_device *, 
			     struct packet_type *);

/* IrTTP */
EXPORT_SYMBOL(irttp_open_tsap);
EXPORT_SYMBOL(irttp_close_tsap);
EXPORT_SYMBOL(irttp_connect_response);
EXPORT_SYMBOL(irttp_data_request);
EXPORT_SYMBOL(irttp_disconnect_request);
EXPORT_SYMBOL(irttp_flow_request);
EXPORT_SYMBOL(irttp_connect_request);
EXPORT_SYMBOL(irttp_udata_request);
EXPORT_SYMBOL(irttp_dup);

/* Main IrDA module */
#ifdef CONFIG_IRDA_DEBUG
EXPORT_SYMBOL(irda_debug);
#endif
EXPORT_SYMBOL(irda_notify_init);
#ifdef CONFIG_PROC_FS
EXPORT_SYMBOL(proc_irda);
#endif
EXPORT_SYMBOL(irda_param_insert);
EXPORT_SYMBOL(irda_param_extract);
EXPORT_SYMBOL(irda_param_extract_all);
EXPORT_SYMBOL(irda_param_pack);
EXPORT_SYMBOL(irda_param_unpack);

/* IrIAP/IrIAS */
EXPORT_SYMBOL(iriap_open);
EXPORT_SYMBOL(iriap_close);
EXPORT_SYMBOL(iriap_getvaluebyclass_request);
EXPORT_SYMBOL(irias_object_change_attribute);
EXPORT_SYMBOL(irias_add_integer_attrib);
EXPORT_SYMBOL(irias_add_octseq_attrib);
EXPORT_SYMBOL(irias_add_string_attrib);
EXPORT_SYMBOL(irias_insert_object);
EXPORT_SYMBOL(irias_new_object);
EXPORT_SYMBOL(irias_delete_object);
EXPORT_SYMBOL(irias_delete_value);
EXPORT_SYMBOL(irias_find_object);
EXPORT_SYMBOL(irias_find_attrib);
EXPORT_SYMBOL(irias_new_integer_value);
EXPORT_SYMBOL(irias_new_string_value);
EXPORT_SYMBOL(irias_new_octseq_value);

/* IrLMP */
EXPORT_SYMBOL(irlmp_discovery_request);
EXPORT_SYMBOL(irlmp_get_discoveries);
EXPORT_SYMBOL(sysctl_discovery_timeout);
EXPORT_SYMBOL(irlmp_register_client);
EXPORT_SYMBOL(irlmp_unregister_client);
EXPORT_SYMBOL(irlmp_update_client);
EXPORT_SYMBOL(irlmp_register_service);
EXPORT_SYMBOL(irlmp_unregister_service);
EXPORT_SYMBOL(irlmp_service_to_hint);
EXPORT_SYMBOL(irlmp_data_request);
EXPORT_SYMBOL(irlmp_open_lsap);
EXPORT_SYMBOL(irlmp_close_lsap);
EXPORT_SYMBOL(irlmp_connect_request);
EXPORT_SYMBOL(irlmp_connect_response);
EXPORT_SYMBOL(irlmp_disconnect_request);
EXPORT_SYMBOL(irlmp_get_daddr);
EXPORT_SYMBOL(irlmp_get_saddr);
EXPORT_SYMBOL(irlmp_dup);
EXPORT_SYMBOL(lmp_reasons);

/* Queue */
EXPORT_SYMBOL(hashbin_new);
EXPORT_SYMBOL(hashbin_insert);
EXPORT_SYMBOL(hashbin_delete);
EXPORT_SYMBOL(hashbin_remove);
EXPORT_SYMBOL(hashbin_remove_this);
EXPORT_SYMBOL(hashbin_find);
EXPORT_SYMBOL(hashbin_lock_find);
EXPORT_SYMBOL(hashbin_find_next);
EXPORT_SYMBOL(hashbin_get_next);
EXPORT_SYMBOL(hashbin_get_first);

/* IrLAP */
EXPORT_SYMBOL(irlap_open);
EXPORT_SYMBOL(irlap_close);
EXPORT_SYMBOL(irda_init_max_qos_capabilies);
EXPORT_SYMBOL(irda_qos_bits_to_value);
EXPORT_SYMBOL(irda_device_setup);
EXPORT_SYMBOL(alloc_irdadev);
EXPORT_SYMBOL(irda_device_set_media_busy);
EXPORT_SYMBOL(irda_device_txqueue_empty);

EXPORT_SYMBOL(irda_device_dongle_init);
EXPORT_SYMBOL(irda_device_dongle_cleanup);
EXPORT_SYMBOL(irda_device_register_dongle);
EXPORT_SYMBOL(irda_device_unregister_dongle);
EXPORT_SYMBOL(irda_task_execute);
EXPORT_SYMBOL(irda_task_kick);
EXPORT_SYMBOL(irda_task_next_state);
EXPORT_SYMBOL(irda_task_delete);

EXPORT_SYMBOL(async_wrap_skb);
EXPORT_SYMBOL(async_unwrap_char);
EXPORT_SYMBOL(irda_calc_crc16);
EXPORT_SYMBOL(irda_crc16_table);
EXPORT_SYMBOL(irda_start_timer);
#ifdef CONFIG_ISA
EXPORT_SYMBOL(setup_dma);
#endif
EXPORT_SYMBOL(infrared_mode);

#ifdef CONFIG_IRTTY
EXPORT_SYMBOL(irtty_set_dtr_rts);
EXPORT_SYMBOL(irtty_register_dongle);
EXPORT_SYMBOL(irtty_unregister_dongle);
EXPORT_SYMBOL(irtty_set_packet_mode);
#endif

#ifdef CONFIG_IRDA_DEBUG
__u32 irda_debug = IRDA_DEBUG_LEVEL;
#endif

/* Packet type handler.
 * Tell the kernel how IrDA packets should be handled.
 */
static struct packet_type irda_packet_type = {
	.type	= __constant_htons(ETH_P_IRDA),
	.func	= irlap_driver_rcv,	/* Packet type handler irlap_frame.c */
};

/*
 * Function irda_device_event (this, event, ptr)
 *
 *    Called when a device is taken up or down
 *
 */
static int irda_device_event(struct notifier_block *this, unsigned long event,
			     void *ptr)
{
	struct net_device *dev = (struct net_device *) ptr;
	
        /* Reject non IrDA devices */
	if (dev->type != ARPHRD_IRDA) 
		return NOTIFY_DONE;
	
        switch (event) {
	case NETDEV_UP:
		IRDA_DEBUG(3, "%s(), NETDEV_UP\n", __FUNCTION__);
		/* irda_dev_device_up(dev); */
		break;
	case NETDEV_DOWN:
		IRDA_DEBUG(3, "%s(), NETDEV_DOWN\n", __FUNCTION__);
		/* irda_kill_by_device(dev); */
		/* irda_rt_device_down(dev); */
		/* irda_dev_device_down(dev); */
		break;
	default:
		break;
        }

        return NOTIFY_DONE;
}

static struct notifier_block irda_dev_notifier = {
	irda_device_event,
	NULL,
	0
};

/*
 * Function irda_notify_init (notify)
 *
 *    Used for initializing the notify structure
 *
 */
void irda_notify_init(notify_t *notify)
{
	notify->data_indication = NULL;
	notify->udata_indication = NULL;
	notify->connect_confirm = NULL;
	notify->connect_indication = NULL;
	notify->disconnect_indication = NULL;
	notify->flow_indication = NULL;
	notify->status_indication = NULL;
	notify->instance = NULL;
	strlcpy(notify->name, "Unknown", sizeof(notify->name));
}

/*
 * Function irda_init (void)
 *
 *  Protocol stack initialisation entry point.
 *  Initialise the various components of the IrDA stack
 */
int __init irda_init(void)
{
	IRDA_DEBUG(0, "%s()\n", __FUNCTION__);

	/* Lower layer of the stack */
 	irlmp_init();
	irlap_init();
	
	/* Higher layers of the stack */
	iriap_init();
 	irttp_init();
	irsock_init();
	
	/* Add IrDA packet type (Start receiving packets) */
        dev_add_pack(&irda_packet_type);

	/* Notifier for Interface changes */
	register_netdevice_notifier(&irda_dev_notifier);

	/* External APIs */
#ifdef CONFIG_PROC_FS
	irda_proc_register();
#endif
#ifdef CONFIG_SYSCTL
	irda_sysctl_register();
#endif

	/* Driver/dongle support */
 	irda_device_init();

	return 0;
}

/*
 * Function irda_cleanup (void)
 *
 *  Protocol stack cleanup/removal entry point.
 *  Cleanup the various components of the IrDA stack
 */
void __exit irda_cleanup(void)
{
	/* Remove External APIs */
#ifdef CONFIG_SYSCTL
	irda_sysctl_unregister();
#endif	
#ifdef CONFIG_PROC_FS
	irda_proc_unregister();
#endif

	/* Remove IrDA packet type (stop receiving packets) */
        dev_remove_pack(&irda_packet_type);
	
	/* Stop receiving interfaces notifications */
        unregister_netdevice_notifier(&irda_dev_notifier);
	
	/* Remove higher layers */
	irsock_cleanup();
	irttp_cleanup();
	iriap_cleanup();

	/* Remove lower layers */
	irda_device_cleanup();
	irlap_cleanup(); /* Must be done before irlmp_cleanup()! DB */

	/* Remove middle layer */
	irlmp_cleanup();
}

/*
 * The IrDA stack must be initialised *before* drivers get initialised,
 * and *before* higher protocols (IrLAN/IrCOMM/IrNET) get initialised,
 * otherwise bad things will happen (hashbins will be NULL for example).
 * Those modules are at module_init()/device_initcall() level.
 *
 * On the other hand, it needs to be initialised *after* the basic
 * networking, the /proc/net filesystem and sysctl module. Those are
 * currently initialised in .../init/main.c (before initcalls).
 * Also, IrDA drivers needs to be initialised *after* the random number
 * generator (main stack and higher layer init don't need it anymore).
 *
 * Jean II
 */
subsys_initcall(irda_init);
module_exit(irda_cleanup);
 
MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no> & Jean Tourrilhes <jt@hpl.hp.com>");
MODULE_DESCRIPTION("The Linux IrDA Protocol Stack"); 
MODULE_LICENSE("GPL");
#ifdef CONFIG_IRDA_DEBUG
MODULE_PARM(irda_debug, "1l");
#endif
MODULE_ALIAS_NETPROTO(PF_IRDA);
