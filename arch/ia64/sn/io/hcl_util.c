/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/devfs_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/sn/sgi.h>
#include <asm/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/nodepda.h>

static devfs_handle_t hwgraph_all_cnodes = GRAPH_VERTEX_NONE;
extern devfs_handle_t hwgraph_root;


/*
** Return the "master" for a given vertex.  A master vertex is a
** controller or adapter or other piece of hardware that the given
** vertex passes through on the way to the rest of the system.
*/
devfs_handle_t
device_master_get(devfs_handle_t vhdl)
{
	graph_error_t rc;
	devfs_handle_t master;

	rc = hwgraph_edge_get(vhdl, EDGE_LBL_MASTER, &master);
	if (rc == GRAPH_SUCCESS)
		return(master);
	else
		return(GRAPH_VERTEX_NONE);
}

/*
** Set the master for a given vertex.
** Returns 0 on success, non-0 indicates failure
*/
int
device_master_set(devfs_handle_t vhdl, devfs_handle_t master)
{
	graph_error_t rc;

	rc = hwgraph_edge_add(vhdl, master, EDGE_LBL_MASTER);
	return(rc != GRAPH_SUCCESS);
}


/*
** Return the compact node id of the node that ultimately "owns" the specified
** vertex.  In order to do this, we walk back through masters and connect points
** until we reach a vertex that represents a node.
*/
cnodeid_t
master_node_get(devfs_handle_t vhdl)
{
	cnodeid_t cnodeid;
	devfs_handle_t master;

	for (;;) {
		cnodeid = nodevertex_to_cnodeid(vhdl);
		if (cnodeid != CNODEID_NONE)
			return(cnodeid);

		master = device_master_get(vhdl);

		/* Check for exceptional cases */
		if (master == vhdl) {
			/* Since we got a reference to the "master" thru
			 * device_master_get() we should decrement
			 * its reference count by 1
			 */
			return(CNODEID_NONE);
		}

		if (master == GRAPH_VERTEX_NONE) {
			master = hwgraph_connectpt_get(vhdl);
			if ((master == GRAPH_VERTEX_NONE) ||
			    (master == vhdl)) {
				return(CNODEID_NONE);
			}
		}

		vhdl = master;
	}
}

/*
** If the specified device represents a node, return its
** compact node ID; otherwise, return CNODEID_NONE.
*/
cnodeid_t
nodevertex_to_cnodeid(devfs_handle_t vhdl)
{
	int rv = 0;
	arbitrary_info_t cnodeid = CNODEID_NONE;

	rv = labelcl_info_get_LBL(vhdl, INFO_LBL_CNODEID, NULL, &cnodeid);

	return((cnodeid_t)cnodeid);
}

void
mark_nodevertex_as_node(devfs_handle_t vhdl, cnodeid_t cnodeid)
{
	if (cnodeid == CNODEID_NONE)
		return;

	cnodeid_to_vertex(cnodeid) = vhdl;
	labelcl_info_add_LBL(vhdl, INFO_LBL_CNODEID, INFO_DESC_EXPORT, 
		(arbitrary_info_t)cnodeid);

	{
		char cnodeid_buffer[10];

		if (hwgraph_all_cnodes == GRAPH_VERTEX_NONE) {
			(void)hwgraph_path_add( hwgraph_root,
						EDGE_LBL_NODENUM,
						&hwgraph_all_cnodes);
		}

		sprintf(cnodeid_buffer, "%d", cnodeid);
		(void)hwgraph_edge_add( hwgraph_all_cnodes,
					vhdl,
					cnodeid_buffer);
	}
}


/*
** dev_to_name converts a devfs_handle_t into a canonical name.  If the devfs_handle_t
** represents a vertex in the hardware graph, it is converted in the
** normal way for vertices.  If the devfs_handle_t is an old devfs_handle_t (one which
** does not represent a hwgraph vertex), we synthesize a name based
** on major/minor number.
**
** Usually returns a pointer to the original buffer, filled in as
** appropriate.  If the buffer is too small to hold the entire name,
** or if anything goes wrong while determining the name, dev_to_name
** returns "UnknownDevice".
*/
char *
dev_to_name(devfs_handle_t dev, char *buf, uint buflen)
{
        return(vertex_to_name(dev, buf, buflen));
}


