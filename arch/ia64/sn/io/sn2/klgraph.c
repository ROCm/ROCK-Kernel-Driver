/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

/*
 * klgraph.c-
 *      This file specifies the interface between the kernel and the PROM's
 *      configuration data structures.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/sn/sgi.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/iograph.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/sn_private.h>

/* #define KLGRAPH_DEBUG 1 */
#ifdef KLGRAPH_DEBUG
#define GRPRINTF(x)	printk x
#else
#define GRPRINTF(x)
#endif

void mark_cpuvertex_as_cpu(vertex_hdl_t vhdl, cpuid_t cpuid);


/* ARGSUSED */
static void __init
klhwg_add_hub(vertex_hdl_t node_vertex, klhub_t *hub, cnodeid_t cnode)
{
	vertex_hdl_t myhubv;
	vertex_hdl_t hub_mon;
	int rc;
	extern struct file_operations shub_mon_fops;

	hwgraph_path_add(node_vertex, EDGE_LBL_HUB, &myhubv);

	HWGRAPH_DEBUG(__FILE__, __FUNCTION__,__LINE__, myhubv, NULL, "Created path for hub vertex for Shub node.\n");

	rc = device_master_set(myhubv, node_vertex);
	if (rc) {
		printk("klhwg_add_hub: Unable to create hub vertex.\n");
		return;
	}
	hub_mon = hwgraph_register(myhubv, EDGE_LBL_PERFMON,
		0, 0, 0, 0,
		S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP, 0, 0,
		&shub_mon_fops, (void *)(long)cnode);
}

/* ARGSUSED */
static void __init
klhwg_add_disabled_cpu(vertex_hdl_t node_vertex, cnodeid_t cnode, klcpu_t *cpu, slotid_t slot)
{
        vertex_hdl_t my_cpu;
        char name[120];
        cpuid_t cpu_id;
	nasid_t nasid;

	nasid = cnodeid_to_nasid(cnode);
        cpu_id = nasid_slice_to_cpuid(nasid, cpu->cpu_info.physid);
        if(cpu_id != -1){
		snprintf(name, 120, "%s/%s/%c", EDGE_LBL_DISABLED, EDGE_LBL_CPU, 'a' + cpu->cpu_info.physid);
		(void) hwgraph_path_add(node_vertex, name, &my_cpu);

		HWGRAPH_DEBUG(__FILE__, __FUNCTION__,__LINE__, my_cpu, NULL, "Created path for disabled cpu slice.\n");

		mark_cpuvertex_as_cpu(my_cpu, cpu_id);
		device_master_set(my_cpu, node_vertex);
		return;
        }
}

/* ARGSUSED */
static void __init
klhwg_add_cpu(vertex_hdl_t node_vertex, cnodeid_t cnode, klcpu_t *cpu)
{
        vertex_hdl_t my_cpu, cpu_dir;
        char name[120];
        cpuid_t cpu_id;
	nasid_t nasid;

	nasid = cnodeid_to_nasid(cnode);
        cpu_id = nasid_slice_to_cpuid(nasid, cpu->cpu_info.physid);

        snprintf(name, 120, "%s/%d/%c",
                EDGE_LBL_CPUBUS,
                0,
                'a' + cpu->cpu_info.physid);

        (void) hwgraph_path_add(node_vertex, name, &my_cpu);

	HWGRAPH_DEBUG(__FILE__, __FUNCTION__,__LINE__, my_cpu, NULL, "Created path for active cpu slice.\n");

	mark_cpuvertex_as_cpu(my_cpu, cpu_id);
        device_master_set(my_cpu, node_vertex);

        /* Add an alias under the node's CPU directory */
        if (hwgraph_edge_get(node_vertex, EDGE_LBL_CPU, &cpu_dir) == GRAPH_SUCCESS) {
                snprintf(name, 120, "%c", 'a' + cpu->cpu_info.physid);
                (void) hwgraph_edge_add(cpu_dir, my_cpu, name);
		HWGRAPH_DEBUG(__FILE__, __FUNCTION__,__LINE__, cpu_dir, my_cpu, "Created % from vhdl1 to vhdl2.\n", name);
        }
}


static void __init
klhwg_add_xbow(cnodeid_t cnode, nasid_t nasid)
{
	lboard_t *brd;
	klxbow_t *xbow_p;
	nasid_t hub_nasid;
	cnodeid_t hub_cnode;
	int widgetnum;
	vertex_hdl_t xbow_v, hubv;
	/*REFERENCED*/
	graph_error_t err;

	if (!(brd = find_lboard_nasid((lboard_t *)KL_CONFIG_INFO(nasid), 
			nasid, KLTYPE_IOBRICK_XBOW)))
		return;

	if (KL_CONFIG_DUPLICATE_BOARD(brd))
	    return;

	if ((xbow_p = (klxbow_t *)find_component(brd, NULL, KLSTRUCT_XBOW))
	    == NULL)
	    return;

	for (widgetnum = HUB_WIDGET_ID_MIN; widgetnum <= HUB_WIDGET_ID_MAX; widgetnum++) {
		if (!XBOW_PORT_TYPE_HUB(xbow_p, widgetnum)) 
		    continue;

		hub_nasid = XBOW_PORT_NASID(xbow_p, widgetnum);
		if (hub_nasid == INVALID_NASID) {
			printk(KERN_WARNING  "hub widget %d, skipping xbow graph\n", widgetnum);
			continue;
		}

		hub_cnode = nasid_to_cnodeid(hub_nasid);

		if (hub_cnode == INVALID_CNODEID) {
			continue;
		}
			
		hubv = cnodeid_to_vertex(hub_cnode);

		err = hwgraph_path_add(hubv, EDGE_LBL_XTALK, &xbow_v);
                if (err != GRAPH_SUCCESS) {
                        if (err == GRAPH_DUP)
                                printk(KERN_WARNING  "klhwg_add_xbow: Check for "
                                        "working routers and router links!");

                        printk("klhwg_add_xbow: Failed to add "
                                "edge: vertex 0x%p to vertex 0x%p,"
                                "error %d\n",
                                (void *)hubv, (void *)xbow_v, err);
			return;
                }

		HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, xbow_v, NULL, "Created path for xtalk.\n");

		xswitch_vertex_init(xbow_v); 

		NODEPDA(hub_cnode)->xbow_vhdl = xbow_v;

		/*
		 * XXX - This won't work is we ever hook up two hubs
		 * by crosstown through a crossbow.
		 */
		if (hub_nasid != nasid) {
			NODEPDA(hub_cnode)->xbow_peer = nasid;
			NODEPDA(nasid_to_cnodeid(nasid))->xbow_peer =
				hub_nasid;
		}
	}
}


/* ARGSUSED */
static void __init
klhwg_add_node(vertex_hdl_t hwgraph_root, cnodeid_t cnode)
{
	nasid_t nasid;
	lboard_t *brd;
	klhub_t *hub;
	vertex_hdl_t node_vertex = NULL;
	char path_buffer[100];
	int rv;
	char *s;
	int board_disabled = 0;
	klcpu_t *cpu;
	vertex_hdl_t cpu_dir;

	nasid = cnodeid_to_nasid(cnode);
	brd = find_lboard_any((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_SNIA);
	ASSERT(brd);

	/* Generate a hardware graph path for this board. */
	board_to_path(brd, path_buffer);
	rv = hwgraph_path_add(hwgraph_root, path_buffer, &node_vertex);
	if (rv != GRAPH_SUCCESS) {
		printk("Node vertex creation failed.  Path == %s", path_buffer);
		return;
	}

	HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, node_vertex, NULL, "Created path for SHUB node.\n");
	hub = (klhub_t *)find_first_component(brd, KLSTRUCT_HUB);
	ASSERT(hub);
	if(hub->hub_info.flags & KLINFO_ENABLE)
		board_disabled = 0;
	else
		board_disabled = 1;
		
	if(!board_disabled) {
		mark_nodevertex_as_node(node_vertex, cnode);
		s = dev_to_name(node_vertex, path_buffer, sizeof(path_buffer));
		NODEPDA(cnode)->hwg_node_name =
					kmalloc(strlen(s) + 1, GFP_KERNEL);
		if (NODEPDA(cnode)->hwg_node_name <= 0) {
			printk("%s: no memory\n", __FUNCTION__);
			return;
		}
		strcpy(NODEPDA(cnode)->hwg_node_name, s);
		hubinfo_set(node_vertex, NODEPDA(cnode)->pdinfo);
		NODEPDA(cnode)->slotdesc = brd->brd_slot;
		NODEPDA(cnode)->geoid = brd->brd_geoid;
		NODEPDA(cnode)->module = module_lookup(geo_module(brd->brd_geoid));
		klhwg_add_hub(node_vertex, hub, cnode);
	}

	/*
	 * If there's at least 1 CPU, add a "cpu" directory to represent
	 * the collection of all CPUs attached to this node.
	 */
	cpu = (klcpu_t *)find_first_component(brd, KLSTRUCT_CPU);
	if (cpu) {
		graph_error_t rv;

		rv = hwgraph_path_add(node_vertex, EDGE_LBL_CPU, &cpu_dir);
		if (rv != GRAPH_SUCCESS) {
			printk("klhwg_add_node: Cannot create CPU directory\n");
			return;
		}
		HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, cpu_dir, NULL, "Created cpu directiry on SHUB node.\n");

	}

	while (cpu) {
		cpuid_t cpu_id;
		cpu_id = nasid_slice_to_cpuid(nasid,cpu->cpu_info.physid);
		if (cpu_online(cpu_id))
			klhwg_add_cpu(node_vertex, cnode, cpu);
		else
			klhwg_add_disabled_cpu(node_vertex, cnode, cpu, brd->brd_slot);

		cpu = (klcpu_t *)
			find_component(brd, (klinfo_t *)cpu, KLSTRUCT_CPU);
	}
}


/* ARGSUSED */
static void __init
klhwg_add_all_routers(vertex_hdl_t hwgraph_root)
{
	nasid_t nasid;
	cnodeid_t cnode;
	lboard_t *brd;
	vertex_hdl_t node_vertex;
	char path_buffer[100];
	int rv;

	for (cnode = 0; cnode < numnodes; cnode++) {
		nasid = cnodeid_to_nasid(cnode);
		brd = find_lboard_class_any((lboard_t *)KL_CONFIG_INFO(nasid),
				KLTYPE_ROUTER);

		if (!brd)
			/* No routers stored in this node's memory */
			continue;

		do {
			ASSERT(brd);

			/* Don't add duplicate boards. */
			if (brd->brd_flags & DUPLICATE_BOARD)
				continue;

			/* Generate a hardware graph path for this board. */
			board_to_path(brd, path_buffer);

			/* Add the router */
			rv = hwgraph_path_add(hwgraph_root, path_buffer, &node_vertex);
			if (rv != GRAPH_SUCCESS) {
				printk("Router vertex creation "
						  "failed.  Path == %s", path_buffer);
				return;
			}
			HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, node_vertex, NULL, "Created router path.\n");

		/* Find the rest of the routers stored on this node. */
		} while ( (brd = find_lboard_class_any(KLCF_NEXT_ANY(brd),
			 KLTYPE_ROUTER)) );
	}

}

/* ARGSUSED */
static void __init
klhwg_connect_one_router(vertex_hdl_t hwgraph_root, lboard_t *brd,
			 cnodeid_t cnode, nasid_t nasid)
{
	klrou_t *router;
	char path_buffer[50];
	char dest_path[50];
	vertex_hdl_t router_hndl;
	vertex_hdl_t dest_hndl;
	int rc;
	int port;
	lboard_t *dest_brd;

	/* Don't add duplicate boards. */
	if (brd->brd_flags & DUPLICATE_BOARD) {
		return;
	}

	/* Generate a hardware graph path for this board. */
	board_to_path(brd, path_buffer);

	rc = hwgraph_traverse(hwgraph_root, path_buffer, &router_hndl);

	if (rc != GRAPH_SUCCESS)
			return;

	if (rc != GRAPH_SUCCESS)
		printk(KERN_WARNING  "Can't find router: %s", path_buffer);

	/* We don't know what to do with multiple router components */
	if (brd->brd_numcompts != 1) {
		printk("klhwg_connect_one_router: %d cmpts on router\n",
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
		if (nasid_to_cnodeid(router->rou_port[port].port_nasid) 
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
			if (KL_CONFIG_DUPLICATE_BOARD(dest_brd))
				continue;
			printk("Can't find router: %s", dest_path);
			return;
		}

		sprintf(dest_path, "%d", port);

		rc = hwgraph_edge_add(router_hndl, dest_hndl, dest_path);

		if (rc == GRAPH_DUP) {
			GRPRINTF(("Skipping port %d. nasid %d %s/%s\n",
				  port, router->rou_port[port].port_nasid,
				  path_buffer, dest_path));
			continue;
		}

		if (rc != GRAPH_SUCCESS) {
			printk("Can't create edge: %s/%s to vertex 0x%p error 0x%x\n",
				path_buffer, dest_path, (void *)dest_hndl, rc);
			return;
		}
		HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, router_hndl, dest_hndl, "Created edge %s from vhdl1 to vhdl2.\n", dest_path);
		
	}
}


static void __init
klhwg_connect_routers(vertex_hdl_t hwgraph_root)
{
	nasid_t nasid;
	cnodeid_t cnode;
	lboard_t *brd;

	for (cnode = 0; cnode < numnodes; cnode++) {
		nasid = cnodeid_to_nasid(cnode);
		brd = find_lboard_class_any((lboard_t *)KL_CONFIG_INFO(nasid),
				KLTYPE_ROUTER);

		if (!brd)
			continue;

		do {

			nasid = cnodeid_to_nasid(cnode);

			klhwg_connect_one_router(hwgraph_root, brd,
						 cnode, nasid);

		/* Find the rest of the routers stored on this node. */
		} while ( (brd = find_lboard_class_any(KLCF_NEXT_ANY(brd), KLTYPE_ROUTER)) );
	}
}



static void __init
klhwg_connect_hubs(vertex_hdl_t hwgraph_root)
{
	nasid_t nasid;
	cnodeid_t cnode;
	lboard_t *brd;
	klhub_t *hub;
	lboard_t *dest_brd;
	vertex_hdl_t hub_hndl;
	vertex_hdl_t dest_hndl;
	char path_buffer[50];
	char dest_path[50];
	graph_error_t rc;
	int port;

	for (cnode = 0; cnode < numionodes; cnode++) {
		nasid = cnodeid_to_nasid(cnode);

		brd = find_lboard_any((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_SNIA);

		hub = (klhub_t *)find_first_component(brd, KLSTRUCT_HUB);
		ASSERT(hub);

		for (port = 1; port <= MAX_NI_PORTS; port++) {
			if (hub->hub_port[port].port_nasid == INVALID_NASID) {
				continue; /* Port not active */
			}

			if (nasid_to_cnodeid(hub->hub_port[port].port_nasid) == INVALID_CNODEID)
				continue;

			/* Generate a hardware graph path for this board. */
			board_to_path(brd, path_buffer);
			rc = hwgraph_traverse(hwgraph_root, path_buffer, &hub_hndl);

			if (rc != GRAPH_SUCCESS)
				printk(KERN_WARNING  "Can't find hub: %s", path_buffer);

			dest_brd = (lboard_t *)NODE_OFFSET_TO_K0(
					hub->hub_port[port].port_nasid,
					hub->hub_port[port].port_offset);

			/* Generate a hardware graph path for this board. */
			board_to_path(dest_brd, dest_path);

			rc = hwgraph_traverse(hwgraph_root, dest_path, &dest_hndl);

			if (rc != GRAPH_SUCCESS) {
				if (KL_CONFIG_DUPLICATE_BOARD(dest_brd))
					continue;
				printk("Can't find board: %s", dest_path);
				return;
			} else {
				char buf[1024];

				rc = hwgraph_path_add(hub_hndl, EDGE_LBL_INTERCONNECT, &hub_hndl);

				HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, hub_hndl, NULL, "Created link path.\n");

				sprintf(buf,"%s/%s",path_buffer,EDGE_LBL_INTERCONNECT);
				rc = hwgraph_traverse(hwgraph_root, buf, &hub_hndl);
				sprintf(buf,"%d",port);
				rc = hwgraph_edge_add(hub_hndl, dest_hndl, buf);

				HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, hub_hndl, dest_hndl, "Created edge %s from vhdl1 to vhdl2.\n", buf);

				if (rc != GRAPH_SUCCESS) {
					printk("Can't create edge: %s/%s to vertex 0x%p, error 0x%x\n",
							path_buffer, dest_path, (void *)dest_hndl, rc);
					return;
				}
			}
		}
	}
}

void __init
klhwg_add_all_modules(vertex_hdl_t hwgraph_root)
{
	cmoduleid_t	cm;
	char		name[128];
	vertex_hdl_t	vhdl;
	vertex_hdl_t  module_vhdl;
	int		rc;
	char		buffer[16];

	/* Add devices under each module */

	for (cm = 0; cm < nummodules; cm++) {
		/* Use module as module vertex fastinfo */

		memset(buffer, 0, 16);
		format_module_id(buffer, sn_modules[cm]->id, MODULE_FORMAT_BRIEF);
		sprintf(name, EDGE_LBL_MODULE "/%s", buffer);

		rc = hwgraph_path_add(hwgraph_root, name, &module_vhdl);
		ASSERT(rc == GRAPH_SUCCESS);
		rc = rc;
		HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, module_vhdl, NULL, "Created module path.\n");

		hwgraph_fastinfo_set(module_vhdl, (arbitrary_info_t) sn_modules[cm]);

		/* Add system controller */
		sprintf(name,
			EDGE_LBL_MODULE "/%s/" EDGE_LBL_L1,
			buffer);

		rc = hwgraph_path_add(hwgraph_root, name, &vhdl);
		ASSERT_ALWAYS(rc == GRAPH_SUCCESS); 
		rc = rc;
		HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, vhdl, NULL, "Created L1 path.\n");

		hwgraph_info_add_LBL(vhdl, INFO_LBL_ELSC,
				     (arbitrary_info_t)1);

	}
}

void __init
klhwg_add_all_nodes(vertex_hdl_t hwgraph_root)
{
	cnodeid_t	cnode;

	for (cnode = 0; cnode < numionodes; cnode++) {
		klhwg_add_node(hwgraph_root, cnode);
	}

	for (cnode = 0; cnode < numionodes; cnode++) {
		klhwg_add_xbow(cnode, cnodeid_to_nasid(cnode));
	}

	/*
	 * As for router hardware inventory information, we set this
	 * up in router.c. 
	 */
	
	klhwg_add_all_routers(hwgraph_root);
	klhwg_connect_routers(hwgraph_root);
	klhwg_connect_hubs(hwgraph_root);
}
