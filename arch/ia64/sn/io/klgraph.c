/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

/*
 * klgraph.c-
 *      This file specifies the interface between the kernel and the PROM's
 *      configuration data structures.
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>

#include <asm/sn/cmn_err.h>
#include <asm/sn/agent.h>
#ifdef CONFIG_IA64_SGI_IO
#include <asm/sn/kldir.h>
#endif
#include <asm/sn/gda.h> 
#include <asm/sn/klconfig.h>
#include <asm/sn/router.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/hcl_util.h>

#define KLGRAPH_DEBUG 1
#ifdef KLGRAPH_DEBUG
#define GRPRINTF(x)	printk x
#define CE_GRPANIC	CE_PANIC
#else
#define GRPRINTF(x)
#define CE_GRPANIC	CE_PANIC
#endif

#include <asm/sn/sn_private.h>

extern char arg_maxnodes[];
extern int maxnodes;

#ifndef BRINGUP
/*
 * Gets reason for diagval using table lookup.
 */
static char*
get_diag_string(uint diagcode)
{
  int num_entries;
  int i;
  num_entries = sizeof(diagval_map) / sizeof(diagval_t);
  for (i = 0; i < num_entries; i++){
    if ((unchar)diagval_map[i].dv_code == (unchar)diagcode)
      return diagval_map[i].dv_msg;
  }
  return "Unknown";
}

#endif /* ndef BRINGUP */


/*
 * Support for verbose inventory via hardware graph. 
 * klhwg_invent_alloc allocates the necessary size of inventory information
 * and fills in the generic information.
 */
invent_generic_t *
klhwg_invent_alloc(cnodeid_t cnode, int class, int size)
{
	invent_generic_t *invent;

	invent = kern_malloc(size);
	if (!invent) return NULL;
	
	invent->ig_module = NODE_MODULEID(cnode);
	invent->ig_slot = SLOTNUM_GETSLOT(NODE_SLOTID(cnode));
	invent->ig_invclass = class;

	return invent;
}

/* 
 * Add information about the baseio prom version number
 * as a part of detailed inventory info in the hwgraph.
 */
void
klhwg_baseio_inventory_add(devfs_handle_t baseio_vhdl,cnodeid_t cnode)
{
	invent_miscinfo_t	*baseio_inventory;
	unsigned char		version = 0,revision = 0;

	/* Allocate memory for the "detailed inventory" info
	 * for the baseio
	 */
	baseio_inventory = (invent_miscinfo_t *) 
		klhwg_invent_alloc(cnode, INV_PROM, sizeof(invent_miscinfo_t));
	baseio_inventory->im_type = INV_IO6PROM;
	/* Read the io6prom revision from the nvram */
#ifndef CONFIG_IA64_SGI_IO
	nvram_prom_version_get(&version,&revision);
#endif
	/* Store the revision info  in the inventory */
	baseio_inventory->im_version = version;
	baseio_inventory->im_rev = revision;
	/* Put the inventory info in the hardware graph */
	hwgraph_info_add_LBL(baseio_vhdl, INFO_LBL_DETAIL_INVENT, 
			     (arbitrary_info_t) baseio_inventory);
	/* Make the information available to the user programs
	 * thru hwgfs.
	 */
        hwgraph_info_export_LBL(baseio_vhdl, INFO_LBL_DETAIL_INVENT,
				sizeof(invent_miscinfo_t));
}

char	*hub_rev[] = {
	"0.0",
	"1.0",
	"2.0",
	"2.1",
	"2.2",
	"2.3"
};

/*
 * Add detailed cpu inventory info to the hardware graph.
 */
void
klhwg_hub_invent_info(devfs_handle_t hubv,
		      cnodeid_t cnode, 
		      klhub_t *hub)
{
	invent_miscinfo_t *hub_invent;

	hub_invent = (invent_miscinfo_t *) 
	    klhwg_invent_alloc(cnode, INV_MISC, sizeof(invent_miscinfo_t));
	if (!hub_invent)
	    return;

	if (KLCONFIG_INFO_ENABLED((klinfo_t *)hub))
	    hub_invent->im_gen.ig_flag = INVENT_ENABLED;

	hub_invent->im_type = INV_HUB;
	hub_invent->im_rev = hub->hub_info.revision;
	hub_invent->im_speed = hub->hub_speed;
	hwgraph_info_add_LBL(hubv, INFO_LBL_DETAIL_INVENT, 
			     (arbitrary_info_t) hub_invent);
        hwgraph_info_export_LBL(hubv, INFO_LBL_DETAIL_INVENT,
				sizeof(invent_miscinfo_t));
}

/* ARGSUSED */
void
klhwg_add_hub(devfs_handle_t node_vertex, klhub_t *hub, cnodeid_t cnode)
{
	devfs_handle_t myhubv;
	int rc;

	GRPRINTF(("klhwg_add_hub: adding %s\n", EDGE_LBL_HUB));

	(void) hwgraph_path_add(node_vertex, EDGE_LBL_HUB, &myhubv);
	rc = device_master_set(myhubv, node_vertex);

#ifndef CONFIG_IA64_SGI_IO
	/*
	 * Activate when we support hub stats.
	 */
	rc = hwgraph_info_add_LBL(myhubv, INFO_LBL_HUB_INFO,
                        (arbitrary_info_t)(&NODEPDA(cnode)->hubstats));
#endif

	if (rc != GRAPH_SUCCESS) {
		cmn_err(CE_WARN,
			"klhwg_add_hub: Can't add hub info label 0x%p, code %d",
			myhubv, rc);
	}

	klhwg_hub_invent_info(myhubv, cnode, hub);

#ifndef BRINGUP
	init_hub_stats(cnode, NODEPDA(cnode));
#endif /* ndef BRINGUP */

#ifndef CONFIG_IA64_SGI_IO
	sndrv_attach(myhubv);
#else
	/*
	 * Need to call our driver to do the attach?
	 */
	printk("klhwg_add_hub: Need to add code to do the attach.\n");
#endif
}

#ifndef BRINGUP

void
klhwg_add_rps(devfs_handle_t node_vertex, cnodeid_t cnode, int flag)
{
	devfs_handle_t myrpsv;
	invent_rpsinfo_t *rps_invent;
	int rc;

        if(cnode == CNODEID_NONE)
                return;                                                        
	
	GRPRINTF(("klhwg_add_rps: adding %s to vertex 0x%x\n", EDGE_LBL_RPS,
		node_vertex));

	rc = hwgraph_path_add(node_vertex, EDGE_LBL_RPS, &myrpsv);
	if (rc != GRAPH_SUCCESS)
		return;

	device_master_set(myrpsv, node_vertex);

        rps_invent = (invent_rpsinfo_t *)
            klhwg_invent_alloc(cnode, INV_RPS, sizeof(invent_rpsinfo_t));

        if (!rps_invent)
            return;

	rps_invent->ir_xbox = 0;	/* not an xbox RPS */

        if (flag)
            rps_invent->ir_gen.ig_flag = INVENT_ENABLED;
        else
            rps_invent->ir_gen.ig_flag = 0x0;                                  

        hwgraph_info_add_LBL(myrpsv, INFO_LBL_DETAIL_INVENT,
                             (arbitrary_info_t) rps_invent);
        hwgraph_info_export_LBL(myrpsv, INFO_LBL_DETAIL_INVENT,
                                sizeof(invent_rpsinfo_t));                     
	
}

/*
 * klhwg_update_rps gets invoked when the system controller sends an 
 * interrupt indicating the power supply has lost/regained the redundancy.
 * It's responsible for updating the Hardware graph information.
 *	rps_state = 0 -> if the rps lost the redundancy
 *		  = 1 -> If it is redundant. 
 */
void 
klhwg_update_rps(cnodeid_t cnode, int rps_state)
{
        devfs_handle_t node_vertex;
        devfs_handle_t rpsv;
        invent_rpsinfo_t *rps_invent;                                          
        int rc;
        if(cnode == CNODEID_NONE)
                return;                                                        

        node_vertex = cnodeid_to_vertex(cnode);                                
	rc = hwgraph_edge_get(node_vertex, EDGE_LBL_RPS, &rpsv);
        if (rc != GRAPH_SUCCESS)  {
		return;
	}

	rc = hwgraph_info_get_LBL(rpsv, INFO_LBL_DETAIL_INVENT, 
				  (arbitrary_info_t *)&rps_invent);
        if (rc != GRAPH_SUCCESS)  {
                return;
        }                                                                      

	if (rps_state == 0 ) 
		rps_invent->ir_gen.ig_flag = 0;
	else 
		rps_invent->ir_gen.ig_flag = INVENT_ENABLED;
}

void
klhwg_add_xbox_rps(devfs_handle_t node_vertex, cnodeid_t cnode, int flag)
{
	devfs_handle_t myrpsv;
	invent_rpsinfo_t *rps_invent;
	int rc;

        if(cnode == CNODEID_NONE)
                return;                                                        
	
	GRPRINTF(("klhwg_add_rps: adding %s to vertex 0x%x\n", 
		  EDGE_LBL_XBOX_RPS, node_vertex));

	rc = hwgraph_path_add(node_vertex, EDGE_LBL_XBOX_RPS, &myrpsv);
	if (rc != GRAPH_SUCCESS)
		return;

	device_master_set(myrpsv, node_vertex);

        rps_invent = (invent_rpsinfo_t *)
            klhwg_invent_alloc(cnode, INV_RPS, sizeof(invent_rpsinfo_t));

        if (!rps_invent)
            return;

	rps_invent->ir_xbox = 1;	/* xbox RPS */

        if (flag)
            rps_invent->ir_gen.ig_flag = INVENT_ENABLED;
        else
            rps_invent->ir_gen.ig_flag = 0x0;                                  

        hwgraph_info_add_LBL(myrpsv, INFO_LBL_DETAIL_INVENT,
                             (arbitrary_info_t) rps_invent);
        hwgraph_info_export_LBL(myrpsv, INFO_LBL_DETAIL_INVENT,
                                sizeof(invent_rpsinfo_t));                     
	
}

/*
 * klhwg_update_xbox_rps gets invoked when the xbox system controller
 * polls the status register and discovers that the power supply has 
 * lost/regained the redundancy.
 * It's responsible for updating the Hardware graph information.
 *	rps_state = 0 -> if the rps lost the redundancy
 *		  = 1 -> If it is redundant. 
 */
void 
klhwg_update_xbox_rps(cnodeid_t cnode, int rps_state)
{
        devfs_handle_t node_vertex;
        devfs_handle_t rpsv;
        invent_rpsinfo_t *rps_invent;                                          
        int rc;
        if(cnode == CNODEID_NONE)
                return;                                                        

        node_vertex = cnodeid_to_vertex(cnode);                                
	rc = hwgraph_edge_get(node_vertex, EDGE_LBL_XBOX_RPS, &rpsv);
        if (rc != GRAPH_SUCCESS)  {
		return;
	}

	rc = hwgraph_info_get_LBL(rpsv, INFO_LBL_DETAIL_INVENT, 
				  (arbitrary_info_t *)&rps_invent);
        if (rc != GRAPH_SUCCESS)  {
                return;
        }                                                                      

	if (rps_state == 0 ) 
		rps_invent->ir_gen.ig_flag = 0;
	else 
		rps_invent->ir_gen.ig_flag = INVENT_ENABLED;
}

#endif /* ndef BRINGUP */

void
klhwg_add_xbow(cnodeid_t cnode, nasid_t nasid)
{
	lboard_t *brd;
	klxbow_t *xbow_p;
	nasid_t hub_nasid;
	cnodeid_t hub_cnode;
	int widgetnum;
	devfs_handle_t xbow_v, hubv;
	/*REFERENCED*/
	graph_error_t err;

#if CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 || defined(CONFIG_IA64_GENERIC)
	if ((brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid),
				KLTYPE_PBRICK_XBOW)) == NULL)
			return;
#endif

	if (KL_CONFIG_DUPLICATE_BOARD(brd))
	    return;

	GRPRINTF(("klhwg_add_xbow: adding cnode %d nasid %d xbow edges\n",
			cnode, nasid));

	if ((xbow_p = (klxbow_t *)find_component(brd, NULL, KLSTRUCT_XBOW))
	    == NULL)
	    return;

#ifndef CONFIG_IA64_SGI_IO
	/*
	 * We cannot support this function in devfs .. see below where 
	 * we use hwgraph_path_add() to create this vertex with a known 
	 * name.
	 */
	err = hwgraph_vertex_create(&xbow_v);
	ASSERT(err == GRAPH_SUCCESS);

	xswitch_vertex_init(xbow_v);
#endif /* !CONFIG_IA64_SGI_IO */

	for (widgetnum = HUB_WIDGET_ID_MIN; widgetnum <= HUB_WIDGET_ID_MAX; widgetnum++) {
		if (!XBOW_PORT_TYPE_HUB(xbow_p, widgetnum)) 
		    continue;

		hub_nasid = XBOW_PORT_NASID(xbow_p, widgetnum);
		printk("klhwg_add_xbow: Found xbow port type hub hub_nasid %d widgetnum %d\n", hub_nasid, widgetnum);
		if (hub_nasid == INVALID_NASID) {
			cmn_err(CE_WARN, "hub widget %d, skipping xbow graph\n", widgetnum);
			continue;
		}

		hub_cnode = NASID_TO_COMPACT_NODEID(hub_nasid);
		printk("klhwg_add_xbow: cnode %d cnode %d\n", nasid_to_compact_node[0], nasid_to_compact_node[1]);

		if (is_specified(arg_maxnodes) && hub_cnode == INVALID_CNODEID) {
			continue;
		}
			
		hubv = cnodeid_to_vertex(hub_cnode);

#ifdef CONFIG_IA64_SGI_IO
		printk("klhwg_add_xbow: Hub Vertex found = %p hub_cnode %d\n", hubv, hub_cnode);
		err = hwgraph_path_add(hubv, EDGE_LBL_XTALK, &xbow_v);
                if (err != GRAPH_SUCCESS) {
                        if (err == GRAPH_DUP)
                                cmn_err(CE_WARN, "klhwg_add_xbow: Check for "
                                        "working routers and router links!");

                        cmn_err(CE_GRPANIC, "klhwg_add_xbow: Failed to add "
                                "edge: vertex 0x%p (0x%p) to vertex 0x%p (0x%p),"
                                "error %d\n",
                                hubv, hubv, xbow_v, xbow_v, err);
                }
		xswitch_vertex_init(xbow_v); 
#endif

		NODEPDA(hub_cnode)->xbow_vhdl = xbow_v;

		/*
		 * XXX - This won't work is we ever hook up two hubs
		 * by crosstown through a crossbow.
		 */
		if (hub_nasid != nasid) {
			NODEPDA(hub_cnode)->xbow_peer = nasid;
			NODEPDA(NASID_TO_COMPACT_NODEID(nasid))->xbow_peer =
				hub_nasid;
		}

		GRPRINTF(("klhwg_add_xbow: adding port nasid %d %s to vertex 0x%p\n",
			hub_nasid, EDGE_LBL_XTALK, hubv));

#ifndef CONFIG_IA64_SGI_IO
		err = hwgraph_edge_add(hubv, xbow_v, EDGE_LBL_XTALK);
		if (err != GRAPH_SUCCESS) {
			if (err == GRAPH_DUP)
				cmn_err(CE_WARN, "klhwg_add_xbow: Check for "
					"working routers and router links!");

			cmn_err(CE_GRPANIC, "klhwg_add_xbow: Failed to add "
				"edge: vertex 0x%p (0x%p) to vertex 0x%p (0x%p), "
				"error %d\n",
				hubv, hubv, xbow_v, xbow_v, err);
		}
#endif
	}
}


/* ARGSUSED */
void
klhwg_add_node(devfs_handle_t hwgraph_root, cnodeid_t cnode, gda_t *gdap)
{
	nasid_t nasid;
	lboard_t *brd;
	klhub_t *hub;
	devfs_handle_t node_vertex = NULL;
	char path_buffer[100];
	int rv;
	char *s;
	int board_disabled = 0;

	nasid = COMPACT_TO_NASID_NODEID(cnode);
	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_IP27);
	GRPRINTF(("klhwg_add_node: Adding cnode %d, nasid %d, brd 0x%p\n",
                cnode, nasid, brd));
	ASSERT(brd);

	do {

		/* Generate a hardware graph path for this board. */
		board_to_path(brd, path_buffer);

		GRPRINTF(("klhwg_add_node: adding %s to vertex 0x%p\n",
			path_buffer, hwgraph_root));
		rv = hwgraph_path_add(hwgraph_root, path_buffer, &node_vertex);

		printk("klhwg_add_node: rv = %d graph success %d node_vertex 0x%p\n", rv, GRAPH_SUCCESS, node_vertex);
		if (rv != GRAPH_SUCCESS)
			cmn_err(CE_PANIC, "Node vertex creation failed.  "
					  "Path == %s",
				path_buffer);

		hub = (klhub_t *)find_first_component(brd, KLSTRUCT_HUB);
		ASSERT(hub);
		if(hub->hub_info.flags & KLINFO_ENABLE)
			board_disabled = 0;
		else
			board_disabled = 1;
		
		if(!board_disabled) {
			mark_nodevertex_as_node(node_vertex,
					    cnode + board_disabled * numnodes);
			printk("klhwg_add_node: node_vertex %p, cnode %d numnodes %d\n", node_vertex, cnode, numnodes);

			s = dev_to_name(node_vertex, path_buffer, sizeof(path_buffer));
			printk("klhwg_add_node: s %s\n", s);

			NODEPDA(cnode)->hwg_node_name =
						kmalloc(strlen(s) + 1,
						GFP_KERNEL);
			ASSERT_ALWAYS(NODEPDA(cnode)->hwg_node_name != NULL);
			strcpy(NODEPDA(cnode)->hwg_node_name, s);

			hubinfo_set(node_vertex, NODEPDA(cnode)->pdinfo);

			/* Set up node board's slot */
			NODEPDA(cnode)->slotdesc = brd->brd_slot;

			/* Set up the module we're in */
			NODEPDA(cnode)->module_id = brd->brd_module;
			NODEPDA(cnode)->module = module_lookup(brd->brd_module);
		}

		if(!board_disabled)
		klhwg_add_hub(node_vertex, hub, cnode);
		
		brd = KLCF_NEXT(brd);
		if (brd)
			brd = find_lboard(brd, KLTYPE_IP27);
		else
			break;
	} while(brd);
}


/* ARGSUSED */
void
klhwg_add_all_routers(devfs_handle_t hwgraph_root)
{
	nasid_t nasid;
	cnodeid_t cnode;
	lboard_t *brd;
	devfs_handle_t node_vertex;
	char path_buffer[100];
	int rv;

	for (cnode = 0; cnode < maxnodes; cnode++) {
		nasid = COMPACT_TO_NASID_NODEID(cnode);

		GRPRINTF(("klhwg_add_all_routers: adding router on cnode %d\n",
			cnode));

		brd = find_lboard_class((lboard_t *)KL_CONFIG_INFO(nasid),
				KLTYPE_ROUTER);

		if (!brd)
			/* No routers stored in this node's memory */
			continue;

		do {
			ASSERT(brd);
			GRPRINTF(("Router board struct is %p\n", brd));

			/* Don't add duplicate boards. */
			if (brd->brd_flags & DUPLICATE_BOARD)
				continue;

			GRPRINTF(("Router 0x%p module number is %d\n", brd, brd->brd_module));
			/* Generate a hardware graph path for this board. */
			board_to_path(brd, path_buffer);

			GRPRINTF(("Router path is %s\n", path_buffer));

			/* Add the router */
			GRPRINTF(("klhwg_add_all_routers: adding %s to vertex 0x%p\n",
				path_buffer, hwgraph_root));
			rv = hwgraph_path_add(hwgraph_root, path_buffer, &node_vertex);

			if (rv != GRAPH_SUCCESS)
				cmn_err(CE_PANIC, "Router vertex creation "
						  "failed.  Path == %s",
					path_buffer);

			GRPRINTF(("klhwg_add_all_routers: get next board from 0x%p\n",
					brd));
		/* Find the rest of the routers stored on this node. */
		} while ( (brd = find_lboard_class(KLCF_NEXT(brd),
			 KLTYPE_ROUTER)) );

		GRPRINTF(("klhwg_add_all_routers: Done.\n"));
	}

}

/* ARGSUSED */
void
klhwg_connect_one_router(devfs_handle_t hwgraph_root, lboard_t *brd,
			 cnodeid_t cnode, nasid_t nasid)
{
	klrou_t *router;
	char path_buffer[50];
	char dest_path[50];
	devfs_handle_t router_hndl;
	devfs_handle_t dest_hndl;
	int rc;
	int port;
	lboard_t *dest_brd;

	GRPRINTF(("klhwg_connect_one_router: Connecting router on cnode %d\n",
			cnode));

	/* Don't add duplicate boards. */
	if (brd->brd_flags & DUPLICATE_BOARD) {
		GRPRINTF(("klhwg_connect_one_router: Duplicate router 0x%p on cnode %d\n",
			brd, cnode));
		return;
	}

	/* Generate a hardware graph path for this board. */
	board_to_path(brd, path_buffer);

	rc = hwgraph_traverse(hwgraph_root, path_buffer, &router_hndl);

	if (rc != GRAPH_SUCCESS && is_specified(arg_maxnodes))
			return;

	if (rc != GRAPH_SUCCESS)
		cmn_err(CE_WARN, "Can't find router: %s", path_buffer);

	/* We don't know what to do with multiple router components */
	if (brd->brd_numcompts != 1) {
		cmn_err(CE_PANIC,
			"klhwg_connect_one_router: %d cmpts on router\n",
			brd->brd_numcompts);
		return;
	}


	/* Convert component 0 to klrou_t ptr */
	router = (klrou_t *)NODE_OFFSET_TO_K0(NASID_GET(brd),
					      brd->brd_compts[0]);

	for (port = 1; port <= MAX_ROUTER_PORTS; port++) {
		/* See if the port's active */
		if (router->rou_port[port].port_nasid == INVALID_NASID) {
			GRPRINTF(("klhwg_connect_one_router: port %d inactive.\n",
				 port));
			continue;
		}
		if (is_specified(arg_maxnodes) && NASID_TO_COMPACT_NODEID(router->rou_port[port].port_nasid) 
		    == INVALID_CNODEID) {
			continue;
		}

		dest_brd = (lboard_t *)NODE_OFFSET_TO_K0(
				router->rou_port[port].port_nasid,
				router->rou_port[port].port_offset);

		/* Generate a hardware graph path for this board. */
		board_to_path(dest_brd, dest_path);

		rc = hwgraph_traverse(hwgraph_root, dest_path, &dest_hndl);

		if (rc != GRAPH_SUCCESS) {
			if (is_specified(arg_maxnodes) && KL_CONFIG_DUPLICATE_BOARD(dest_brd))
				continue;
			cmn_err(CE_PANIC, "Can't find router: %s", dest_path);
		}
		GRPRINTF(("klhwg_connect_one_router: Link from %s/%d to %s\n",
			  path_buffer, port, dest_path));

		sprintf(dest_path, "%d", port);

		rc = hwgraph_edge_add(router_hndl, dest_hndl, dest_path);

		if (rc == GRAPH_DUP) {
			GRPRINTF(("Skipping port %d. nasid %d %s/%s\n",
				  port, router->rou_port[port].port_nasid,
				  path_buffer, dest_path));
			continue;
		}

		if (rc != GRAPH_SUCCESS && !is_specified(arg_maxnodes))
			cmn_err(CE_GRPANIC, "Can't create edge: %s/%s to vertex 0x%p error 0x%x\n",
				path_buffer, dest_path, dest_hndl, rc);
		
	}
}


void
klhwg_connect_routers(devfs_handle_t hwgraph_root)
{
	nasid_t nasid;
	cnodeid_t cnode;
	lboard_t *brd;

	for (cnode = 0; cnode < maxnodes; cnode++) {
		nasid = COMPACT_TO_NASID_NODEID(cnode);

		GRPRINTF(("klhwg_connect_routers: Connecting routers on cnode %d\n",
			cnode));

		brd = find_lboard_class((lboard_t *)KL_CONFIG_INFO(nasid),
				KLTYPE_ROUTER);

		if (!brd)
			continue;

		do {

			nasid = COMPACT_TO_NASID_NODEID(cnode);

			klhwg_connect_one_router(hwgraph_root, brd,
						 cnode, nasid);

		/* Find the rest of the routers stored on this node. */
		} while ( (brd = find_lboard_class(KLCF_NEXT(brd), KLTYPE_ROUTER)) );
	}
}



void
klhwg_connect_hubs(devfs_handle_t hwgraph_root)
{
	nasid_t nasid;
	cnodeid_t cnode;
	lboard_t *brd;
	klhub_t *hub;
	lboard_t *dest_brd;
	devfs_handle_t hub_hndl;
	devfs_handle_t dest_hndl;
	char path_buffer[50];
	char dest_path[50];
	graph_error_t rc;

	for (cnode = 0; cnode < maxnodes; cnode++) {
		nasid = COMPACT_TO_NASID_NODEID(cnode);

		GRPRINTF(("klhwg_connect_hubs: Connecting hubs on cnode %d\n",
			cnode));

		brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid),
				KLTYPE_IP27);
		ASSERT(brd);

		hub = (klhub_t *)find_first_component(brd, KLSTRUCT_HUB);
		ASSERT(hub);

		/* See if the port's active */
		if (hub->hub_port.port_nasid == INVALID_NASID) {
			GRPRINTF(("klhwg_connect_hubs: port inactive.\n"));
			continue;
		}

		if (is_specified(arg_maxnodes) && NASID_TO_COMPACT_NODEID(hub->hub_port.port_nasid) == INVALID_CNODEID)
			continue;

		/* Generate a hardware graph path for this board. */
		board_to_path(brd, path_buffer);

		GRPRINTF(("klhwg_connect_hubs: Hub path is %s.\n", path_buffer));
		rc = hwgraph_traverse(hwgraph_root, path_buffer, &hub_hndl);

		if (rc != GRAPH_SUCCESS)
			cmn_err(CE_WARN, "Can't find hub: %s", path_buffer);

		dest_brd = (lboard_t *)NODE_OFFSET_TO_K0(
				hub->hub_port.port_nasid,
				hub->hub_port.port_offset);

		/* Generate a hardware graph path for this board. */
		board_to_path(dest_brd, dest_path);

		rc = hwgraph_traverse(hwgraph_root, dest_path, &dest_hndl);

		if (rc != GRAPH_SUCCESS) {
			if (is_specified(arg_maxnodes) && KL_CONFIG_DUPLICATE_BOARD(dest_brd))
				continue;
			cmn_err(CE_PANIC, "Can't find board: %s", dest_path);
		} else {
		

			GRPRINTF(("klhwg_connect_hubs: Link from %s to %s.\n",
			  path_buffer, dest_path));

			rc = hwgraph_edge_add(hub_hndl, dest_hndl, EDGE_LBL_INTERCONNECT);

			if (rc != GRAPH_SUCCESS)
				cmn_err(CE_GRPANIC, "Can't create edge: %s/%s to vertex 0x%p, error 0x%x\n",
				path_buffer, dest_path, dest_hndl, rc);

		}
	}
}

/* Store the pci/vme disabled board information as extended administrative
 * hints which can later be used by the drivers using the device/driver
 * admin interface. 
 */
void
klhwg_device_disable_hints_add(void)
{
	cnodeid_t	cnode; 		/* node we are looking at */
	nasid_t		nasid;		/* nasid of the node */
	lboard_t	*board;		/* board we are looking at */
	int		comp_index;	/* component index */
	klinfo_t	*component;	/* component in the board we are
					 * looking at 
					 */
	char		device_name[MAXDEVNAME];
	
#ifndef CONFIG_IA64_SGI_IO
	device_admin_table_init();
#endif
	for(cnode = 0; cnode < numnodes; cnode++) {
		nasid = COMPACT_TO_NASID_NODEID(cnode);
		board = (lboard_t *)KL_CONFIG_INFO(nasid);
		/* Check out all the board info stored  on a node */
		while(board) {
			/* No need to look at duplicate boards or non-io 
			 * boards
			 */
			if (KL_CONFIG_DUPLICATE_BOARD(board) ||
			    KLCLASS(board->brd_type) != KLCLASS_IO) {
				board = KLCF_NEXT(board);
				continue;
			}
			/* Check out all the components of a board */
			for (comp_index = 0; 
			     comp_index < KLCF_NUM_COMPS(board);
			     comp_index++) {
				component = KLCF_COMP(board,comp_index);
				/* If the component is enabled move on to
				 * the next component
				 */
				if (KLCONFIG_INFO_ENABLED(component))
					continue;
				/* NOTE : Since the prom only supports
				 * the disabling of pci devices the following
				 * piece of code makes sense. 
				 * Make sure that this assumption is valid
				 */
				/* This component is disabled. Store this
				 * hint in the extended device admin table
				 */
				/* Get the canonical name of the pci device */
				device_component_canonical_name_get(board,
							    component,
							    device_name);
#ifndef CONFIG_IA64_SGI_IO
				device_admin_table_update(device_name,
							  ADMIN_LBL_DISABLED,
							  "yes");
#endif
#ifdef DEBUG
				printf("%s DISABLED\n",device_name);
#endif				
			}
			/* go to the next board info stored on this 
			 * node 
			 */
			board = KLCF_NEXT(board);
		}
	}
}

void
klhwg_add_all_modules(devfs_handle_t hwgraph_root)
{
	cmoduleid_t	cm;
	char		name[128];
	devfs_handle_t	vhdl;
	int		rc;

	/* Add devices under each module */

	for (cm = 0; cm < nummodules; cm++) {
		/* Use module as module vertex fastinfo */

		sprintf(name, EDGE_LBL_MODULE "/%x", modules[cm]->id);

		rc = hwgraph_path_add(hwgraph_root, name, &vhdl);
		ASSERT(rc == GRAPH_SUCCESS);
		rc = rc;

		hwgraph_fastinfo_set(vhdl, (arbitrary_info_t) modules[cm]);

		/* Add system controller */

		sprintf(name,
			EDGE_LBL_MODULE "/%x/" EDGE_LBL_L1,
			modules[cm]->id);

		rc = hwgraph_path_add(hwgraph_root, name, &vhdl);
		ASSERT_ALWAYS(rc == GRAPH_SUCCESS); 
		rc = rc;

		hwgraph_info_add_LBL(vhdl,
				     INFO_LBL_ELSC,
				     (arbitrary_info_t) (__psint_t) 1);

#ifndef CONFIG_IA64_SGI_IO
		sndrv_attach(vhdl);
#else
		/*
		 * We need to call the drivers attach routine ..
		 */
		FIXME("klhwg_add_all_modules: Need code to call driver attach.\n");
#endif
	}
}

void
klhwg_add_all_nodes(devfs_handle_t hwgraph_root)
{
	//gda_t		*gdap = GDA;
	gda_t		*gdap;
	cnodeid_t	cnode;

#ifdef SIMULATED_KLGRAPH
	//gdap = 0xa800000000011000;
	gdap = (gda_t *)0xe000000000011000;
	printk("klhwg_add_all_nodes: SIMULATED_KLGRAPH FIXME: gdap= 0x%p\n", gdap);
#else
	gdap = GDA;
#endif /* SIMULATED_KLGRAPH */
	for (cnode = 0; cnode < numnodes; cnode++) {
		ASSERT(gdap->g_nasidtable[cnode] != INVALID_NASID);
		klhwg_add_node(hwgraph_root, cnode, gdap);
	}

	for (cnode = 0; cnode < numnodes; cnode++) {
		ASSERT(gdap->g_nasidtable[cnode] != INVALID_NASID);

#ifndef CONFIG_IA64_SGI_IO
		klhwg_add_xbow(cnode, gdap->g_nasidtable[cnode]);
#else
		printk("klhwg_add_all_nodes: Fix me by getting real nasid\n");
		klhwg_add_xbow(cnode, 0);
#endif
	}

	/*
	 * As for router hardware inventory information, we set this
	 * up in router.c. 
	 */
	
	klhwg_add_all_routers(hwgraph_root);
	klhwg_connect_routers(hwgraph_root);
	klhwg_connect_hubs(hwgraph_root);

	/* Assign guardian nodes to each of the
	 * routers in the system.
	 */

#ifndef CONFIG_IA64_SGI_IO
	router_guardians_set(hwgraph_root);
#endif

	/* Go through the entire system's klconfig
	 * to figure out which pci components have been disabled
	 */
	klhwg_device_disable_hints_add();

}
