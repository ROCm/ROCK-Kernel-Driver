/*
 *	IrNET protocol module : Synchronous PPP over an IrDA socket.
 *
 *		Jean II - HPL `00 - <jt@hpl.hp.com>
 *
 * This file implement the IRDA interface of IrNET.
 * Basically, we sit on top of IrTTP. We set up IrTTP, IrIAS properly,
 * and exchange frames with IrTTP.
 */

#include <linux/config.h>
#include "irnet_irda.h"		/* Private header */

/************************* CONTROL CHANNEL *************************/
/*
 * When ppp is not active, /dev/irnet act as a control channel.
 * Writting allow to set up the IrDA destination of the IrNET channel,
 * and any application may be read events happening on IrNET...
 */

/*------------------------------------------------------------------*/
/*
 * Post an event to the control channel...
 * Put the event in the log, and then wait all process blocked on read
 * so they can read the log...
 */
static void
irnet_post_event(irnet_socket *	ap,
		 irnet_event	event,
		 __u32		addr,
		 char *		name)
{
  unsigned long		flags;		/* For spinlock */
  int			index;		/* In the log */

  DENTER(CTRL_TRACE, "(ap=0x%X, event=%d, addr=%08x, name=``%s'')\n",
	 (unsigned int) ap, event, addr, name);

  /* Protect this section via spinlock.
   * Note : as we are the only event producer, we only need to exclude
   * ourself when touching the log, which is nice and easy.
   */
  spin_lock_irqsave(&irnet_events.spinlock, flags);

  /* Copy the event in the log */
  index = irnet_events.index;
  irnet_events.log[index].event = event;
  irnet_events.log[index].addr = addr;
  /* Try to copy IrDA nickname */
  if(name)
    strcpy(irnet_events.log[index].name, name);
  else
    irnet_events.log[index].name[0] = '\0';
  /* Try to get ppp unit number */
  if((ap != (irnet_socket *) NULL) && (ap->ppp_open))
    irnet_events.log[index].unit = ppp_unit_number(&ap->chan);
  else
    irnet_events.log[index].unit = -1;

  /* Increment the index
   * Note that we increment the index only after the event is written,
   * to make sure that the readers don't get garbage... */
  irnet_events.index = (index + 1) % IRNET_MAX_EVENTS;

  DEBUG(CTRL_INFO, "New event index is %d\n", irnet_events.index);

  /* Spin lock end */
  spin_unlock_irqrestore(&irnet_events.spinlock, flags);

  /* Now : wake up everybody waiting for events... */
  wake_up_interruptible_all(&irnet_events.rwait);

  DEXIT(CTRL_TRACE, "\n");
}

/************************* IRDA SUBROUTINES *************************/
/*
 * These are a bunch of subroutines called from other functions
 * down there, mostly common code or to improve readability...
 *
 * Note : we duplicate quite heavily some routines of af_irda.c,
 * because our input structure (self) is quite different
 * (struct irnet instead of struct irda_sock), which make sharing
 * the same code impossible (at least, without templates).
 */

/*------------------------------------------------------------------*/
/*
 * Function irda_open_tsap (self)
 *
 *    Open local Transport Service Access Point (TSAP)
 *
 * Create a IrTTP instance for us and set all the IrTTP callbacks.
 */
static inline int
irnet_open_tsap(irnet_socket *	self)
{
  notify_t	notify;		/* Callback structure */

  DENTER(IRDA_SR_TRACE, "(self=0x%X)\n", (unsigned int) self);

  DABORT(self->tsap != NULL, -EBUSY, IRDA_SR_ERROR, "Already busy !\n");

  /* Initialize IrTTP callbacks to be used by the IrDA stack */
  irda_notify_init(&notify);
  notify.connect_confirm	= irnet_connect_confirm;
  notify.connect_indication	= irnet_connect_indication;
  notify.disconnect_indication	= irnet_disconnect_indication;
  notify.data_indication	= irnet_data_indication;
  /*notify.udata_indication	= NULL;*/
  notify.flow_indication	= irnet_flow_indication;
  notify.status_indication	= irnet_status_indication;
  notify.instance		= self;
  strncpy(notify.name, IRNET_NOTIFY_NAME, NOTIFY_MAX_NAME);

  /* Open an IrTTP instance */
  self->tsap = irttp_open_tsap(LSAP_ANY, DEFAULT_INITIAL_CREDIT,
			       &notify);	
  DABORT(self->tsap == NULL, -ENOMEM,
	 IRDA_SR_ERROR, "Unable to allocate TSAP !\n");

  /* Remember which TSAP selector we actually got */
  self->stsap_sel = self->tsap->stsap_sel;

  DEXIT(IRDA_SR_TRACE, " - tsap=0x%X, sel=0x%X\n",
	(unsigned int) self->tsap, self->stsap_sel);
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Function irnet_find_lsap_sel (self)
 *
 *    Try to lookup LSAP selector in remote LM-IAS
 *
 * Basically, we start a IAP query, and then go to sleep. When the query
 * return, irnet_getvalue_confirm will wake us up, and we can examine the
 * result of the query...
 * Note that in some case, the query fail even before we go to sleep,
 * creating some races...
 */
static int
irnet_find_lsap_sel(irnet_socket *	self)
{
  DENTER(IRDA_SR_TRACE, "(self=0x%X)\n", (unsigned int) self);

  /* This should not happen */
  DABORT(self->iriap, -EBUSY, IRDA_SR_ERROR, "busy with a previous query.\n");

  /* Create an IAP instance, will be closed in irnet_getvalue_confirm() */
  self->iriap = iriap_open(LSAP_ANY, IAS_CLIENT, self,
			   irnet_getvalue_confirm);

  /* Treat unexpected signals as disconnect */
  self->errno = -EHOSTUNREACH;

  /* Query remote LM-IAS */
  iriap_getvaluebyclass_request(self->iriap, self->saddr, self->daddr,
				IRNET_SERVICE_NAME, IRNET_IAS_VALUE);
  /* Wait for answer (if not already failed) */
  if(self->iriap != NULL)
    interruptible_sleep_on(&self->query_wait);

  /* Check what happened */
  if(self->errno)
    {
      DEBUG(IRDA_SR_INFO, "IAS query failed! (%d)\n", self->errno);
      /* Requested object/attribute doesn't exist */
      if((self->errno == IAS_CLASS_UNKNOWN) ||
	 (self->errno == IAS_ATTRIB_UNKNOWN))
	return (-EADDRNOTAVAIL);
      else
	return (-EHOSTUNREACH);
    }

  /* Get the remote TSAP selector */
  switch(self->ias_result->type)
    {
    case IAS_INTEGER:
      DEBUG(IRDA_SR_INFO, "result=%d\n", self->ias_result->t.integer);
      if(self->ias_result->t.integer != -1)
	self->dtsap_sel = self->ias_result->t.integer;
      else 
	self->dtsap_sel = 0;
      break;
    default:
      self->dtsap_sel = 0;
      DERROR(IRDA_SR_ERROR, "bad type ! (0x%X)\n", self->ias_result->type);
      break;
    }
  /* Cleanup */
  if(self->ias_result)
    irias_delete_value(self->ias_result);

  DEXIT(IRDA_SR_TRACE, "\n");
  if(self->dtsap_sel)
    return 0;

  return -EADDRNOTAVAIL;
}

/*------------------------------------------------------------------*/
/*
 * Function irnet_discover_daddr_and_lsap_sel (self)
 *
 *    This try to find a device with the requested service.
 *
 * It basically look into the discovery log. For each address in the list,
 * it queries the LM-IAS of the device to find if this device offer
 * the requested service.
 * If there is more than one node supporting the service, we complain
 * to the user (it should move devices around).
 * The, we set both the destination address and the lsap selector to point
 * on the service on the unique device we have found.
 *
 * Note : this function fails if there is more than one device in range,
 * because IrLMP doesn't disconnect the LAP when the last LSAP is closed.
 * Moreover, we would need to wait the LAP disconnection...
 */
static inline int
irnet_discover_daddr_and_lsap_sel(irnet_socket *	self)
{
  struct irda_device_info *discoveries;	/* Copy of the discovery log */
  int	number;			/* Number of nodes in the log */
  int	i;
  int	err = -ENETUNREACH;
  __u32	daddr = DEV_ADDR_ANY;	/* Address we found the service on */
  __u8	dtsap_sel = 0x0;	/* TSAP associated with it */

  DENTER(IRDA_SR_TRACE, "(self=0x%X)\n", (unsigned int) self);

  /* Ask lmp for the current discovery log
   * Note : we have to use irlmp_get_discoveries(), as opposed
   * to play with the cachelog directly, because while we are
   * making our ias query, le log might change... */
  discoveries = irlmp_get_discoveries(&number, self->mask);
  /* Check if the we got some results */
  if (discoveries == NULL)
    DRETURN(-ENETUNREACH, IRDA_SR_INFO, "Cachelog empty...\n");

  /* 
   * Now, check all discovered devices (if any), and connect
   * client only about the services that the client is
   * interested in...
   */
  for(i = 0; i < number; i++)
    {
      /* Try the address in the log */
      self->daddr = discoveries[i].daddr;
      self->saddr = 0x0;
      DEBUG(IRDA_SR_INFO, "trying daddr = %08x\n", self->daddr);

      /* Query remote LM-IAS for this service */
      err = irnet_find_lsap_sel(self);
      switch(err)
	{
	case 0:
	  /* We found the requested service */
	  if(daddr != DEV_ADDR_ANY)
	    {
	      DEBUG(IRDA_SR_INFO, "More than one device in range supports IrNET...\n");
	    }
	  else
	    {
	      /* First time we found that one, save it ! */
	      daddr = self->daddr;
	      dtsap_sel = self->dtsap_sel;
	    }
	  break;
	case -EADDRNOTAVAIL:
	  /* Requested service simply doesn't exist on this node */
	  break;
	default:
	  /* Something bad did happen :-( */
	  DERROR(IRDA_SR_ERROR, "unexpected IAS query failure\n");
	  self->daddr = DEV_ADDR_ANY;
	  kfree(discoveries);
	  return(-EHOSTUNREACH);
	  break;
	}
    }
  /* Cleanup our copy of the discovery log */
  kfree(discoveries);

  /* Check out what we found */
  if(daddr == DEV_ADDR_ANY)
    {
      self->daddr = DEV_ADDR_ANY;
      DEXIT(IRDA_SR_INFO, "cannot discover IrNET in any device !!!\n");
      return(-EADDRNOTAVAIL);
    }

  /* Revert back to discovered device & service */
  self->daddr = daddr;
  self->saddr = 0x0;
  self->dtsap_sel = dtsap_sel;

  DEBUG(IRDA_SR_INFO, "discovered IrNET at address %08x\n", self->daddr);
  DEXIT(IRDA_SR_TRACE, "\n");

  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Function irnet_dname_to_daddr (self)
 *
 *    Convert an IrDA nickname to a valid IrDA address
 *
 * It basically look into the discovery log until there is a match.
 */
static inline int
irnet_dname_to_daddr(irnet_socket *	self)
{
  struct irda_device_info *discoveries;	/* Copy of the discovery log */
  int	number;			/* Number of nodes in the log */
  int	i;

  DENTER(IRDA_SR_TRACE, "(self=0x%X)\n", (unsigned int) self);

  /* Ask lmp for the current discovery log */
  discoveries = irlmp_get_discoveries(&number, 0xffff);
  /* Check if the we got some results */
  if(discoveries == NULL)
    DRETURN(-ENETUNREACH, IRDA_SR_INFO, "Cachelog empty...\n");

  /* 
   * Now, check all discovered devices (if any), and connect
   * client only about the services that the client is
   * interested in...
   */
  for(i = 0; i < number; i++)
    {
      /* Does the name match ? */
      if(!strncmp(discoveries[i].info, self->rname, NICKNAME_MAX_LEN))
	{
	  /* Yes !!! Get it.. */
	  self->daddr = discoveries[i].daddr;
	  DEBUG(IRDA_SR_INFO, "discovered device ``%s'' at address 0x%08x.\n",
		self->rname, self->daddr);
	  kfree(discoveries);
	  DEXIT(IRDA_SR_TRACE, "\n");
	  return 0;
	}
    }
  /* No luck ! */
  DEBUG(IRDA_SR_INFO, "cannot discover device ``%s'' !!!\n", self->rname);
  kfree(discoveries);
  return(-EADDRNOTAVAIL);
}


/************************* SOCKET ROUTINES *************************/
/*
 * This are the main operations on IrNET sockets, basically to create
 * and destroy IrNET sockets. These are called from the PPP part...
 */

/*------------------------------------------------------------------*/
/*
 * Create a IrNET instance : just initialise some parameters...
 */
int
irda_irnet_create(irnet_socket *	self)
{
  DENTER(IRDA_SOCK_TRACE, "(self=0x%X)\n", (unsigned int) self);

  self->magic = IRNET_MAGIC;	/* Paranoia */

  init_waitqueue_head(&self->query_wait);

  self->ttp_open = 0;		/* Prevent higher layer from accessing IrTTP */
  self->rname[0] = '\0';	/* May be set via control channel */
  self->raddr = DEV_ADDR_ANY;	/* May be set via control channel */
  self->daddr = DEV_ADDR_ANY;	/* Until we get connected */
  self->saddr = 0x0;		/* so IrLMP assign us any link */
  self->max_sdu_size_rx = TTP_SAR_UNBOUND;

  /* Register as a client with IrLMP */
  self->ckey = irlmp_register_client(0, NULL, NULL, NULL);
#ifdef DISCOVERY_NOMASK
  self->mask = 0xffff;		/* For W2k compatibility */
#else DISCOVERY_NOMASK
  self->mask = irlmp_service_to_hint(S_LAN);
#endif DISCOVERY_NOMASK
  self->tx_flow = FLOW_START;	/* Flow control from IrTTP */

  DEXIT(IRDA_SOCK_TRACE, "\n");
  return(0);
}

/*------------------------------------------------------------------*/
/*
 * Connect to the other side :
 *	o convert device name to an address
 *	o find the socket number (dlsap)
 *	o Establish the connection
 */
int
irda_irnet_connect(irnet_socket *	self)
{
  int		err;

  DENTER(IRDA_SOCK_TRACE, "(self=0x%X)\n", (unsigned int) self);

  /* Check if we have opened a local TSAP :
   * If we have already opened a TSAP, it means that either we are already
   * connected or in the process of doing so... */
  if(self->tsap != NULL)
    DRETURN(-EBUSY, IRDA_SOCK_INFO, "Already connecting...\n");

  /* Insert ourselves in the hashbin so that the IrNET server can find us.
   * Notes : 4th arg is string of 32 char max and must be null terminated
   *	     When 4th arg is used (string), 3rd arg isn't (int)
   *	     Can't re-insert (MUST remove first) so check for that... */
  if((irnet_server.running) && (self->q.q_next == NULL))
    {
      unsigned long		flags;
      spin_lock_irqsave(&irnet_server.spinlock, flags);
      hashbin_insert(irnet_server.list, (irda_queue_t *) self, 0, self->rname);
      spin_unlock_irqrestore(&irnet_server.spinlock, flags);
      DEBUG(IRDA_SOCK_INFO, "Inserted ``%s'' in hashbin...\n", self->rname);
    }

  /* If we don't have anything (no address, no name) */
  if((self->raddr == DEV_ADDR_ANY) && (self->rname[0] == '\0'))
    {
      /* Try to find a suitable address */
      if((err = irnet_discover_daddr_and_lsap_sel(self)) != 0) 
	DRETURN(err, IRDA_SOCK_INFO, "auto-connect failed!\n");
    }
  else
    {
      /* If we have only the name (no address), try to get an address */
      if(self->raddr == DEV_ADDR_ANY)
	{
	  if((err = irnet_dname_to_daddr(self)) != 0)
	    DRETURN(err, IRDA_SOCK_INFO, "name-connect failed!\n");
	}
      else
	/* Use the requested destination address */
	self->daddr = self->raddr;

      /* Query remote LM-IAS to find LSAP selector */
      if((err = irnet_find_lsap_sel(self)) != 0)
	DRETURN(err, IRDA_SOCK_INFO, "connect failed!\n");
    }
  DEBUG(IRDA_SOCK_INFO, "daddr = %08x, lsap = %d, starting IrTTP connection\n",
	self->daddr, self->dtsap_sel);

  /* Open a local TSAP (an IrTTP instance) */
  err = irnet_open_tsap(self);
  DABORT(err != 0, err, IRDA_SOCK_ERROR, "connect aborted!\n");

  /* Connect to remote device */
  err = irttp_connect_request(self->tsap, self->dtsap_sel, 
			      self->saddr, self->daddr, NULL, 
			      self->max_sdu_size_rx, NULL);
  DABORT(err != 0, err, IRDA_SOCK_ERROR, "connect aborted!\n");

  DEXIT(IRDA_SOCK_TRACE, "\n");
  return(0);
}

/*------------------------------------------------------------------*/
/*
 * Function irda_irnet_destroy(self)
 *
 *    Destroy irnet instance
 *
 */
void
irda_irnet_destroy(irnet_socket *	self)
{
  DENTER(IRDA_SOCK_TRACE, "(self=0x%X)\n", (unsigned int) self);
  if(self == NULL)
    return;

  /* Remove ourselves from hashbin (if we are queued in hashbin)
   * Note : `irnet_server.running' protect us from calls in hashbin_delete() */
  if((irnet_server.running) && (self->q.q_next != NULL))
    {
      struct irnet_socket *	entry;
      unsigned long		flags;
      DEBUG(IRDA_SOCK_INFO, "Removing from hash..\n");
      spin_lock_irqsave(&irnet_server.spinlock, flags);
      entry = hashbin_remove_this(irnet_server.list, (irda_queue_t *) self);
      self->q.q_next = NULL;
      spin_unlock_irqrestore(&irnet_server.spinlock, flags);
      DASSERT(entry == self, , IRDA_SOCK_ERROR, "Can't remove from hash.\n");
    }

  /* Unregister with IrLMP */
  irlmp_unregister_client(self->ckey);

  /* Unregister with LM-IAS */
  if(self->iriap) 
    iriap_close(self->iriap);

  /* Prevent higher layer from accessing IrTTP */
  self->ttp_open = 0;

  /* Close our IrTTP connection */
  if(self->tsap)
    {
      DEBUG(IRDA_SOCK_INFO, "Closing our TTP connection.\n");
      irttp_disconnect_request(self->tsap, NULL, P_NORMAL);
      irttp_close_tsap(self->tsap);
      self->tsap = NULL;
      /* Note : as the disconnect comes from ppp_generic, the unit number
       * doesn't exist anymore when we post the event, so we need to pass
       * NULL as the first arg... */
      irnet_post_event(NULL, IRNET_DISCONNECT_TO, self->daddr, self->rname);
    }
  self->stsap_sel = 0;

  DEXIT(IRDA_SOCK_TRACE, "\n");
  return;
}


/************************** SERVER SOCKET **************************/
/*
 * The IrNET service is composed of one server socket and a variable
 * number of regular IrNET sockets. The server socket is supposed to
 * handle incomming connections and redirect them to one IrNET sockets.
 * It's a superset of the regular IrNET socket, but has a very distinct
 * behaviour...
 */

/*------------------------------------------------------------------*/
/*
 * Function irnet_daddr_to_dname (self)
 *
 *    Convert an IrDA address to a IrDA nickname
 *
 * It basically look into the discovery log until there is a match.
 */
static inline int
irnet_daddr_to_dname(irnet_socket *	self)
{
  struct irda_device_info *discoveries;	/* Copy of the discovery log */
  int	number;			/* Number of nodes in the log */
  int	i;

  DENTER(IRDA_SERV_TRACE, "(self=0x%X)\n", (unsigned int) self);

  /* Ask lmp for the current discovery log */
  discoveries = irlmp_get_discoveries(&number, 0xffff);
  /* Check if the we got some results */
  if (discoveries == NULL)
    DRETURN(-ENETUNREACH, IRDA_SERV_INFO, "Cachelog empty...\n");

  /* Now, check all discovered devices (if any) */
  for(i = 0; i < number; i++)
    {
      /* Does the name match ? */
      if(discoveries[i].daddr == self->daddr)
	{
	  /* Yes !!! Get it.. */
	  strncpy(self->rname, discoveries[i].info, NICKNAME_MAX_LEN);
	  self->rname[NICKNAME_MAX_LEN + 1] = '\0';
	  DEBUG(IRDA_SERV_INFO, "Device 0x%08x is in fact ``%s''.\n",
		self->daddr, self->rname);
	  kfree(discoveries);
	  DEXIT(IRDA_SERV_TRACE, "\n");
	  return 0;
	}
    }
  /* No luck ! */
  DEXIT(IRDA_SERV_INFO, ": cannot discover device 0x%08x !!!\n", self->daddr);
  kfree(discoveries);
  return(-EADDRNOTAVAIL);
}

/*------------------------------------------------------------------*/
/*
 * Function irda_find_socket (self)
 *
 *    Find the correct IrNET socket
 *
 * Look into the list of IrNET sockets and finds one with the right
 * properties...
 */
static inline irnet_socket *
irnet_find_socket(irnet_socket *	self)
{
  irnet_socket *	new = (irnet_socket *) NULL;
  unsigned long		flags;
  int			err;

  DENTER(IRDA_SERV_TRACE, "(self=0x%X)\n", (unsigned int) self);

  /* Get the address of the requester */
  self->daddr = irttp_get_daddr(self->tsap);

  /* Try to get the IrDA nickname of the requester */
  err = irnet_daddr_to_dname(self);

  /* Protect access to the instance list */
  spin_lock_irqsave(&irnet_server.spinlock, flags);

  /* So now, try to get an socket having specifically
   * requested that nickname */
  if(err == 0)
    {
      new = (irnet_socket *) hashbin_find(irnet_server.list,
					  0, self->rname);
      if(new)
	DEBUG(IRDA_SERV_INFO, "Socket 0x%X matches rname ``%s''.\n",
	      (unsigned int) new, new->rname);
    }

  /* If no name matches, try to find an socket by the destination address */
  /* It can be either the requested destination address (set via the
   * control channel), or the current destination address if the
   * socket is in the middle of a connection request */
  if(new == (irnet_socket *) NULL)
    {
      new = (irnet_socket *) hashbin_get_first(irnet_server.list);
      while(new !=(irnet_socket *) NULL)
	{
	  /* Does it have the same address ? */
	  if((new->raddr == self->daddr) || (new->daddr == self->daddr))
	    {
	      /* Yes !!! Get it.. */
	      DEBUG(IRDA_SERV_INFO, "Socket 0x%X matches daddr %#08x.\n",
		    (unsigned int) new, self->daddr);
	      break;
	    }
	  new = (irnet_socket *) hashbin_get_next(irnet_server.list);
	}
    }

  /* If we don't have any socket, get the first unconnected socket */
  if(new == (irnet_socket *) NULL)
    {
      new = (irnet_socket *) hashbin_get_first(irnet_server.list);
      while(new !=(irnet_socket *) NULL)
	{
	  /* Is it available ? */
	  if(!(new->ttp_open) && (new->raddr == DEV_ADDR_ANY) &&
	     (new->rname[0] == '\0') && (new->ppp_open))
	    {
	      /* Yes !!! Get it.. */
	      DEBUG(IRDA_SERV_INFO, "Socket 0x%X is free.\n",
		    (unsigned int) new);
	      break;
	    }
	  new = (irnet_socket *) hashbin_get_next(irnet_server.list);
	}
    }

  /* Spin lock end */
  spin_unlock_irqrestore(&irnet_server.spinlock, flags);

  DEXIT(IRDA_SERV_TRACE, " - new = 0x%X\n", (unsigned int) new);
  return new;
}

/*------------------------------------------------------------------*/
/*
 * Function irda_connect_socket (self)
 *
 *    Connect an incomming connection to the socket
 *
 */
static inline int
irnet_connect_socket(irnet_socket *	self,
		     irnet_socket *	new,
		     struct qos_info *	qos,
		     __u32		max_sdu_size,
		     __u8		max_header_size)
{
  DENTER(IRDA_SERV_TRACE, "(self=0x%X, new=0x%X)\n",
	 (unsigned int) self, (unsigned int) new);

  /* Now attach up the new socket */
  new->tsap = irttp_dup(self->tsap, new);
  DABORT(new->tsap == NULL, -1, IRDA_SERV_ERROR, "dup failed!\n");

  /* Set up all the relevant parameters on the new socket */
  new->stsap_sel = new->tsap->stsap_sel;
  new->dtsap_sel = new->tsap->dtsap_sel;
  new->saddr = irttp_get_saddr(new->tsap);
  new->daddr = irttp_get_daddr(new->tsap);

  new->max_header_size = max_header_size;
  new->max_sdu_size_tx = max_sdu_size;
  new->max_data_size   = max_sdu_size;
#ifdef STREAM_COMPAT
  /* If we want to receive "stream sockets" */
  if(max_sdu_size == 0)
    new->max_data_size = irttp_get_max_seg_size(new->tsap);
#endif STREAM_COMPAT

  /* Clean up the original one to keep it in listen state */
  self->tsap->dtsap_sel = self->tsap->lsap->dlsap_sel = LSAP_ANY;
  self->tsap->lsap->lsap_state = LSAP_DISCONNECTED;

  /* Send a connection response on the new socket */
  irttp_connect_response(new->tsap, new->max_sdu_size_rx, NULL);

  /* Allow PPP to send its junk over the new socket... */
  new->ttp_open = 1;
#ifdef CONNECT_INDIC_KICK
  /* As currently we don't packets in ppp_irnet_send(), this is not needed...
   * Also, not doing it give IrDA a chance to finish the setup properly
   * before beeing swamped with packets... */
  ppp_output_wakeup(&new->chan);
#endif CONNECT_INDIC_KICK

  /* Notify the control channel */
  irnet_post_event(new, IRNET_CONNECT_FROM, new->daddr, self->rname);

  DEXIT(IRDA_SERV_TRACE, "\n");
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Function irda_disconnect_server (self)
 *
 *    Cleanup the server socket when the incomming connection abort
 *
 */
static inline void
irnet_disconnect_server(irnet_socket *	self,
			struct sk_buff *skb)
{
  DENTER(IRDA_SERV_TRACE, "(self=0x%X)\n", (unsigned int) self);

  /* Put the received packet in the black hole */
  kfree_skb(skb);

#ifdef FAIL_SEND_DISCONNECT
  /* Tell the other party we don't want to be connected */
  /* Hum... Is it the right thing to do ? And do we need to send
   * a connect response before ? It looks ok without this... */
  irttp_disconnect_request(self->tsap, NULL, P_NORMAL);
#endif FAIL_SEND_DISCONNECT

  /* Clean up the server to keep it in listen state */
  self->tsap->dtsap_sel = self->tsap->lsap->dlsap_sel = LSAP_ANY;
  self->tsap->lsap->lsap_state = LSAP_DISCONNECTED;

  /* Notify the control channel */
  irnet_post_event(NULL, IRNET_REQUEST_FROM, self->daddr, self->rname);

  DEXIT(IRDA_SERV_TRACE, "\n");
  return;
}

/*------------------------------------------------------------------*/
/*
 * Function irda_setup_server (self)
 *
 *    Create a IrTTP server and set it up...
 *
 * Register the IrLAN hint bit, create a IrTTP instance for us,
 * set all the IrTTP callbacks and create an IrIAS entry...
 */
static inline int
irnet_setup_server(void)
{
  __u16		hints;

  DENTER(IRDA_SERV_TRACE, "()\n");

  /* Initialise the regular socket part of the server */
  irda_irnet_create(&irnet_server.s);

  /* Open a local TSAP (an IrTTP instance) for the server */
  irnet_open_tsap(&irnet_server.s);

  /* PPP part setup */
  irnet_server.s.ppp_open = 0;
  irnet_server.s.chan.private = NULL;
  irnet_server.s.file = NULL;

  /* Get the hint bit corresponding to IrLAN */
  /* Note : we overload the IrLAN hint bit. As it is only a "hint", and as
   * we provide roughly the same functionality as IrLAN, this is ok.
   * In fact, the situation is similar as JetSend overloading the Obex hint
   */
  hints = irlmp_service_to_hint(S_LAN);

#ifdef ADVERTISE_HINT
  /* Register with IrLMP as a service (advertise our hint bit) */
  irnet_server.skey = irlmp_register_service(hints);
#endif ADVERTISE_HINT

  /* Register with LM-IAS (so that people can connect to us) */
  irnet_server.ias_obj = irias_new_object(IRNET_SERVICE_NAME, jiffies);
  irias_add_integer_attrib(irnet_server.ias_obj, IRNET_IAS_VALUE, 
			   irnet_server.s.stsap_sel, IAS_KERNEL_ATTR);
  irias_insert_object(irnet_server.ias_obj);

#ifdef DISCOVERY_EVENTS
  /* Tell IrLMP we want to be notified of newly discovered nodes */
  irlmp_update_client(irnet_server.s.ckey, hints,
		      irnet_discovery_indication, irnet_expiry_indication,
		      (void *) &irnet_server.s);
#endif

  DEXIT(IRDA_SERV_TRACE, " - self=0x%X\n", (unsigned int) &irnet_server.s);
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Function irda_destroy_server (self)
 *
 *    Destroy the IrTTP server...
 *
 * Reverse of the previous function...
 */
static inline void
irnet_destroy_server(void)
{
  DENTER(IRDA_SERV_TRACE, "()\n");

#ifdef ADVERTISE_HINT
  /* Unregister with IrLMP */
  irlmp_unregister_service(irnet_server.skey);
#endif ADVERTISE_HINT

  /* Unregister with LM-IAS */
  if(irnet_server.ias_obj)
    irias_delete_object(irnet_server.ias_obj);

  /* Cleanup the socket part */
  irda_irnet_destroy(&irnet_server.s);

  DEXIT(IRDA_SERV_TRACE, "\n");
  return;
}


/************************ IRDA-TTP CALLBACKS ************************/
/*
 * When we create a IrTTP instance, we pass to it a set of callbacks
 * that IrTTP will call in case of various events.
 * We take care of those events here.
 */

/*------------------------------------------------------------------*/
/*
 * Function irnet_data_indication (instance, sap, skb)
 *
 *    Received some data from TinyTP. Just queue it on the receive queue
 *
 */
static int
irnet_data_indication(void *	instance,
		      void *	sap,
		      struct sk_buff *skb)
{
  irnet_socket *	ap = (irnet_socket *) instance;
  unsigned char *	p;
  int			code = 0;

  DENTER(IRDA_TCB_TRACE, "(self/ap=0x%X, skb=0x%X)\n",
	 (unsigned int) ap,(unsigned int) skb);
  DASSERT(skb != NULL, 0, IRDA_CB_ERROR, "skb is NULL !!!\n");

  /* Check is ppp is ready to receive our packet */
  if(!ap->ppp_open)
    {
      DERROR(IRDA_CB_ERROR, "PPP not ready, dropping packet...\n");
      /* When we return error, TTP will need to requeue the skb and
       * will stop the sender. IrTTP will stall until we send it a
       * flow control request... */
      return -ENOMEM;
    }

  /* strip address/control field if present */
  p = skb->data;
  if((p[0] == PPP_ALLSTATIONS) && (p[1] == PPP_UI))
    {
      /* chop off address/control */
      if(skb->len < 3)
	goto err_exit;
      p = skb_pull(skb, 2);
    }

  /* decompress protocol field if compressed */
  if(p[0] & 1)
    {
      /* protocol is compressed */
      skb_push(skb, 1)[0] = 0;
    }
  else
    if(skb->len < 2)
      goto err_exit;

  /* pass to generic ppp layer */
  /* Note : how do I know if ppp can accept or not the packet ? This is
   * essential if I want to manage flow control smoothly... */
  ppp_input(&ap->chan, skb);

  DEXIT(IRDA_TCB_TRACE, "\n");
  return 0;

 err_exit:
  DERROR(IRDA_CB_ERROR, "Packet too small, dropping...\n");
  kfree_skb(skb);
  ppp_input_error(&ap->chan, code);
  return 0;	/* Don't return an error code, only for flow control... */
}

/*------------------------------------------------------------------*/
/*
 * Function irnet_disconnect_indication (instance, sap, reason, skb)
 *
 *    Connection has been closed. Chech reason to find out why
 *
 * Note : there are many cases where we come here :
 *	o attempted to connect, timeout
 *	o connected, link is broken, LAP has timeout
 *	o connected, other side close the link
 *	o connection request on the server no handled
 */
static void
irnet_disconnect_indication(void *	instance,
			    void *	sap, 
			    LM_REASON	reason,
			    struct sk_buff *skb)
{
  irnet_socket *	self = (irnet_socket *) instance;

  DENTER(IRDA_TCB_TRACE, "(self=0x%X)\n", (unsigned int) self);
  DASSERT(self != NULL, , IRDA_CB_ERROR, "Self is NULL !!!\n");

  /* If we were active, notify the control channel */
  if(self->ttp_open)
    irnet_post_event(self, IRNET_DISCONNECT_FROM, self->daddr, self->rname);
  else
    /* If we were trying to connect, notify the control channel */
    if((self->tsap) && (self != &irnet_server.s))
      irnet_post_event(self, IRNET_NOANSWER_FROM, self->daddr, self->rname);

  /* Prevent higher layer from accessing IrTTP */
  self->ttp_open = 0;

  /* Close our IrTTP connection */
  if((self->tsap) && (self != &irnet_server.s))
    {
      DEBUG(IRDA_CB_INFO, "Closing our TTP connection.\n");
      irttp_disconnect_request(self->tsap, NULL, P_NORMAL);
      irttp_close_tsap(self->tsap);
      self->tsap = NULL;

      /* Flush (drain) ppp_generic Tx queue (most often we have blocked it) */
      if(self->ppp_open)
	ppp_output_wakeup(&self->chan);
    }
  /* Cleanup the socket in case we want to reconnect */
  self->stsap_sel = 0;
  self->daddr = DEV_ADDR_ANY;
  self->tx_flow = FLOW_START;

  /* Note : what should we say to ppp ?
   * It seem the ppp_generic and pppd are happy that way and will eventually
   * timeout gracefully, so don't bother them... */

  DEXIT(IRDA_TCB_TRACE, "\n");
}

/*------------------------------------------------------------------*/
/*
 * Function irnet_connect_confirm (instance, sap, qos, max_sdu_size, skb)
 *
 *    Connections has been confirmed by the remote device
 *
 */
static void
irnet_connect_confirm(void *	instance,
		      void *	sap, 
		      struct qos_info *qos,
		      __u32	max_sdu_size,
		      __u8	max_header_size, 
		      struct sk_buff *skb)
{
  irnet_socket *	self = (irnet_socket *) instance;

  DENTER(IRDA_TCB_TRACE, "(self=0x%X)\n", (unsigned int) self);

  /* How much header space do we need to reserve */
  self->max_header_size = max_header_size;

  /* IrTTP max SDU size in transmit direction */
  self->max_sdu_size_tx = max_sdu_size;
  self->max_data_size = max_sdu_size;
#ifdef STREAM_COMPAT
  if(max_sdu_size == 0)
    self->max_data_size = irttp_get_max_seg_size(self->tsap);
#endif STREAM_COMPAT

  /* At this point, IrLMP has assigned our source address */
  self->saddr = irttp_get_saddr(self->tsap);

  /* Allow higher layer to access IrTTP */
  self->ttp_open = 1;
  /* Give a kick in the ass of ppp_generic so that he sends us some data */
  ppp_output_wakeup(&self->chan);

  /* Check size of received packet */
  if(skb->len > 0)
    {
#ifdef PASS_CONNECT_PACKETS
      DEBUG(IRDA_CB_INFO, "Passing connect packet to PPP.\n");
      /* Try to pass it to PPP */
      irnet_data_indication(instance, sap, skb);
#else PASS_CONNECT_PACKETS
      DERROR(IRDA_CB_ERROR, "Dropping non empty packet.\n");
      kfree_skb(skb);	/* Note : will be optimised with other kfree... */
#endif PASS_CONNECT_PACKETS
    }
  else
    kfree_skb(skb);

  /* Notify the control channel */
  irnet_post_event(self, IRNET_CONNECT_TO, self->daddr, self->rname);

  DEXIT(IRDA_TCB_TRACE, "\n");
}

/*------------------------------------------------------------------*/
/*
 * Function irnet_flow_indication (instance, sap, flow)
 *
 *    Used by TinyTP to tell us if it can accept more data or not
 *
 */
static void
irnet_flow_indication(void *	instance,
		      void *	sap,
		      LOCAL_FLOW flow) 
{
  irnet_socket *	self = (irnet_socket *) instance;

  DENTER(IRDA_TCB_TRACE, "(self=0x%X, flow=%d)\n", (unsigned int) self, flow);

  /* Update our state */
  self->tx_flow = flow;

  /* Check what IrTTP want us to do... */
  switch(flow)
    {
    case FLOW_START:
      DEBUG(IRDA_CB_INFO, "IrTTP wants us to start again\n");
      ppp_output_wakeup(&self->chan);
      break;
    case FLOW_STOP:
      DEBUG(IRDA_CB_INFO, "IrTTP wants us to slow down\n");
      break;
    default:
      DEBUG(IRDA_CB_INFO, "Unknown flow command!\n");
      break;
    }

  DEXIT(IRDA_TCB_TRACE, "\n");
}

/*------------------------------------------------------------------*/
/*
 * Function irnet_status_indication (instance, sap, reason, skb)
 *
 *    Link (IrLAP) status report.
 *
 */
static void
irnet_status_indication(void *	instance,
			LINK_STATUS link,
			LOCK_STATUS lock)
{
  irnet_socket *	self = (irnet_socket *) instance;

  DENTER(IRDA_TCB_TRACE, "(self=0x%X)\n", (unsigned int) self);
  DASSERT(self != NULL, , IRDA_CB_ERROR, "Self is NULL !!!\n");

  /* We can only get this event if we are connected */
  switch(link)
    {
    case STATUS_NO_ACTIVITY:
      irnet_post_event(self, IRNET_BLOCKED_LINK, self->daddr, self->rname);
      break;
    default:
      DEBUG(IRDA_CB_INFO, "Unknown status...\n");
    }

  DEXIT(IRDA_TCB_TRACE, "\n");
}

/*------------------------------------------------------------------*/
/*
 * Function irnet_connect_indication(instance, sap, qos, max_sdu_size, userdata)
 *
 *    Incomming connection
 *
 * In theory, this function is called only on the server socket.
 * Some other node is attempting to connect to the IrNET service, and has
 * sent a connection request on our server socket.
 * We just redirect the connection to the relevant IrNET socket.
 * 
 * Note : we also make sure that between 2 irnet nodes, there can
 * exist only one irnet connection.
 */
static void
irnet_connect_indication(void *		instance,
			 void *		sap, 
			 struct qos_info *qos,
			 __u32		max_sdu_size,
			 __u8		max_header_size,
			 struct sk_buff *skb)
{
  irnet_socket *	self = &irnet_server.s;
  irnet_socket *	new = (irnet_socket *) NULL;

  DENTER(IRDA_TCB_TRACE, "(self=0x%X)\n", (unsigned int) self);
  DASSERT(instance == &irnet_server, , IRDA_CB_ERROR,
	  "Invalid instance (0x%X) !!!\n", (unsigned int) instance);
  DASSERT(sap == irnet_server.s.tsap, , IRDA_CB_ERROR, "Invalid sap !!!\n");

  /* Try to find the most appropriate IrNET socket */
  new = irnet_find_socket(self);

  /* After all this hard work, do we have an socket ? */
  if(new == (irnet_socket *) NULL)
    {
      DEXIT(IRDA_CB_INFO, ": No socket waiting for this connection.\n");
      irnet_disconnect_server(self, skb);
      return;
    }

  /* Is the socket already busy ? */
  if(new->ttp_open)
    {
      DEXIT(IRDA_CB_INFO, ": Socket already connected.\n");
      irnet_disconnect_server(self, skb);
      return;
    }

  /* Socket connecting */
  if(new->tsap != NULL)
    {
      /* The socket has sent a IrTTP connection request and is waiting for
       * a connection response (that may never come).
       * Now, the pain is that the socket has open a tsap and is waiting on it,
       * while the other end is trying to connect to it on another tsap.
       * Argh ! We will deal with that later...
       */
      DERROR(IRDA_CB_ERROR, "Socket already connecting. Ouch !\n");
#ifdef ALLOW_SIMULT_CONNECT
      /* Close the connection the new socket was attempting.
       * WARNING : This need more testing ! */
      irttp_close_tsap(new->tsap);
      /* Note : no return, fall through... */
#else ALLOW_SIMULT_CONNECT
      irnet_disconnect_server(self, skb);
      return;
#endif ALLOW_SIMULT_CONNECT
    }

  /* So : at this point, we have a socket, and it is idle. Good ! */
  irnet_connect_socket(self, new, qos, max_sdu_size, max_header_size);

  /* Check size of received packet */
  if(skb->len > 0)
    {
#ifdef PASS_CONNECT_PACKETS
      DEBUG(IRDA_CB_INFO, "Passing connect packet to PPP.\n");
      /* Try to pass it to PPP */
      irnet_data_indication(new, new->tsap, skb);
#else PASS_CONNECT_PACKETS
      DERROR(IRDA_CB_ERROR, "Dropping non empty packet.\n");
      kfree_skb(skb);	/* Note : will be optimised with other kfree... */
#endif PASS_CONNECT_PACKETS
    }
  else
    kfree_skb(skb);

  DEXIT(IRDA_TCB_TRACE, "\n");
}


/********************** IRDA-IAS/LMP CALLBACKS **********************/
/*
 * These are the callbacks called by other layers of the IrDA stack,
 * mainly LMP for discovery and IAS for name queries.
 */

/*------------------------------------------------------------------*/
/*
 * Function irnet_getvalue_confirm (obj_id, value, priv)
 *
 *    Got answer from remote LM-IAS, just pass object to requester...
 *
 */
static void
irnet_getvalue_confirm(int	result,
		       __u16	obj_id, 
		       struct ias_value *value,
		       void *	priv)
{
  irnet_socket *	self = (irnet_socket *) priv;

  DENTER(IRDA_OCB_TRACE, "(self=0x%X)\n", (unsigned int) self);
  DASSERT(self != NULL, , IRDA_CB_ERROR, "Self is NULL !!!\n");

  /* We probably don't need to make any more queries */
  iriap_close(self->iriap);
  self->iriap = NULL;

  /* Check if request succeeded */
  if(result != IAS_SUCCESS)
    {
      DEBUG(IRDA_CB_INFO, "IAS query failed! (%d)\n", result);
      self->errno = result;	/* We really need it later */
    }
  else
    {
      /* Pass the object to the caller (so the caller must delete it) */
      self->ias_result = value;
      self->errno = 0;
    }

  /* Wake up any processes waiting for result */
  wake_up_interruptible(&self->query_wait);

  DEXIT(IRDA_OCB_TRACE, "\n");
}

#ifdef DISCOVERY_EVENTS
/*------------------------------------------------------------------*/
/*
 * Function irnet_discovery_indication (discovery)
 *
 *    Got a discovery indication from IrLMP, post an event
 *
 * Note : IrLMP take care of matching the hint mask for us, we only
 * check if it is a "new" node...
 *
 * As IrLMP filter on the IrLAN hint bit, we get both IrLAN and IrNET
 * nodes, so it's only at connection time that we will know if the
 * node support IrNET, IrLAN or both. The other solution is to check
 * in IAS the PNP ids and service name.
 * Note : even if a node support IrNET (or IrLAN), it's no guarantee
 * that we will be able to connect to it, the node might already be
 * busy...
 *
 * One last thing : in some case, this function will trigger duplicate
 * discovery events. On the other hand, we should catch all
 * discoveries properly (i.e. not miss one). Filtering duplicate here
 * is to messy, so we leave that to user space...
 */
static void
irnet_discovery_indication(discovery_t *discovery,
			   void *	priv)
{
  irnet_socket *	self = &irnet_server.s;
	
  DENTER(IRDA_OCB_TRACE, "(self=0x%X)\n", (unsigned int) self);
  DASSERT(priv == &irnet_server, , IRDA_CB_ERROR,
	  "Invalid instance (0x%X) !!!\n", (unsigned int) priv);

  /* Check if node is discovered is a new one or an old one.
   * We check when how long ago this node was discovered, with a
   * coarse timeout (we may miss some discovery events or be delayed).
   */
  if((jiffies - discovery->first_timestamp) >= (sysctl_discovery_timeout * HZ))
    {
      return;		/* Too old, not interesting -> goodbye */
    }

  DEBUG(IRDA_CB_INFO, "Discovered new IrNET/IrLAN node %s...\n",
	discovery->nickname);

  /* Notify the control channel */
  irnet_post_event(NULL, IRNET_DISCOVER, discovery->daddr,
		   discovery->nickname);

  DEXIT(IRDA_OCB_TRACE, "\n");
}

/*------------------------------------------------------------------*/
/*
 * Function irnet_expiry_indication (expiry)
 *
 *    Got a expiry indication from IrLMP, post an event
 *
 * Note : IrLMP take care of matching the hint mask for us, we only
 * check if it is a "new" node...
 */
static void
irnet_expiry_indication(discovery_t *	expiry,
			void *		priv)
{
  irnet_socket *	self = &irnet_server.s;
	
  DENTER(IRDA_OCB_TRACE, "(self=0x%X)\n", (unsigned int) self);
  DASSERT(priv == &irnet_server, , IRDA_CB_ERROR,
	  "Invalid instance (0x%X) !!!\n", (unsigned int) priv);

  DEBUG(IRDA_CB_INFO, "IrNET/IrLAN node %s expired...\n",
	expiry->nickname);

  /* Notify the control channel */
  irnet_post_event(NULL, IRNET_EXPIRE, expiry->daddr,
		   expiry->nickname);

  DEXIT(IRDA_OCB_TRACE, "\n");
}
#endif DISCOVERY_EVENTS


/*********************** PROC ENTRY CALLBACKS ***********************/
/*
 * We create a instance in the /proc filesystem, and here we take care
 * of that...
 */

#ifdef CONFIG_PROC_FS
/*------------------------------------------------------------------*/
/*
 * Function irnet_proc_read (buf, start, offset, len, unused)
 *
 *    Give some info to the /proc file system
 */
static int
irnet_proc_read(char *	buf,
		char **	start,
		off_t	offset,
		int	len)
{
  irnet_socket *	self;
  char *		state;
  unsigned long		flags;
  int			i = 0;

  len = 0;
	
  /* Get the IrNET server information... */
  len += sprintf(buf+len, "IrNET server - ");
  len += sprintf(buf+len, "IrDA state: %s, ",
		 (irnet_server.running ? "running" : "dead"));
  len += sprintf(buf+len, "stsap_sel: %02x, ", irnet_server.s.stsap_sel);
  len += sprintf(buf+len, "dtsap_sel: %02x\n", irnet_server.s.dtsap_sel);

  /* Do we need to continue ? */
  if(!irnet_server.running)
    return len;

  /* Protect access to the instance list */
  spin_lock_irqsave(&irnet_server.spinlock, flags);

  /* Get the sockets one by one... */
  self = (irnet_socket *) hashbin_get_first(irnet_server.list);
  while(self != NULL)
    {
      /* Start printing info about the socket. */
      len += sprintf(buf+len, "\nIrNET socket %d - ", i++);

      /* First, get the requested configuration */
      len += sprintf(buf+len, "Requested IrDA name: \"%s\", ", self->rname);
      len += sprintf(buf+len, "addr: %08x\n", self->raddr);

      /* Second, get all the PPP info */
      len += sprintf(buf+len, "	PPP state: %s",
		 (self->ppp_open ? "registered" : "unregistered"));
      if(self->ppp_open)
	{
	  len += sprintf(buf+len, ", unit: ppp%d",
			 ppp_unit_number(&self->chan));
	  len += sprintf(buf+len, ", channel: %d",
			 ppp_channel_index(&self->chan));
	  len += sprintf(buf+len, ", mru: %d",
			 self->mru);
	  /* Maybe add self->flags ? Later... */
	}

      /* Then, get all the IrDA specific info... */
      if(self->ttp_open)
	state = "connected";
      else
	if(self->tsap != NULL)
	  state = "connecting";
	else
	  state = "idle";
      len += sprintf(buf+len, "\n	IrDA state: %s, ", state);
      len += sprintf(buf+len, "daddr: %08x, ", self->daddr);
      len += sprintf(buf+len, "stsap_sel: %02x, ", self->stsap_sel);
      len += sprintf(buf+len, "dtsap_sel: %02x\n", self->dtsap_sel);

      /* Next socket, please... */
      self = (irnet_socket *) hashbin_get_next(irnet_server.list);
    }

  /* Spin lock end */
  spin_unlock_irqrestore(&irnet_server.spinlock, flags);

  return len;
}
#endif /* PROC_FS */


/********************** CONFIGURATION/CLEANUP **********************/
/*
 * Initialisation and teardown of the IrDA part, called at module
 * insertion and removal...
 */

/*------------------------------------------------------------------*/
/*
 * Prepare the IrNET layer for operation...
 */
int
irda_irnet_init(void)
{
  int		err = 0;

  DENTER(MODULE_TRACE, "()\n");

  /* Pure paranoia - should be redundant */
  memset(&irnet_server, 0, sizeof(struct irnet_root));

  /* Setup start of irnet instance list */
  irnet_server.list = hashbin_new(HB_LOCAL); 
  DABORT(irnet_server.list == NULL, -ENOMEM,
	 MODULE_ERROR, "Can't allocate hashbin!\n");
  /* Init spinlock for instance list */
  spin_lock_init(&irnet_server.spinlock);

  /* Initialise control channel */
  init_waitqueue_head(&irnet_events.rwait);
  irnet_events.index = 0;
  /* Init spinlock for event logging */
  spin_lock_init(&irnet_events.spinlock);

#ifdef CONFIG_PROC_FS
  /* Add a /proc file for irnet infos */
  create_proc_info_entry("irnet", 0, proc_irda, irnet_proc_read);
#endif /* CONFIG_PROC_FS */

  /* Setup the IrNET server */
  err = irnet_setup_server();

  if(!err)
    /* We are no longer functional... */
    irnet_server.running = 1;

  DEXIT(MODULE_TRACE, "\n");
  return err;
}

/*------------------------------------------------------------------*/
/*
 * Cleanup at exit...
 */
void
irda_irnet_cleanup(void)
{
  DENTER(MODULE_TRACE, "()\n");

  /* We are no longer there... */
  irnet_server.running = 0;

#ifdef CONFIG_PROC_FS
  /* Remove our /proc file */
  remove_proc_entry("irnet", proc_irda);
#endif CONFIG_PROC_FS

  /* Remove our IrNET server from existence */
  irnet_destroy_server();

  /* Remove all instances of IrNET socket still present */
  hashbin_delete(irnet_server.list, (FREE_FUNC) irda_irnet_destroy);

  DEXIT(MODULE_TRACE, "\n");
}
