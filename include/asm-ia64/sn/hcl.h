/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_HCL_H
#define _ASM_SN_HCL_H

extern spinlock_t hcl_spinlock;
extern devfs_handle_t hcl_handle; /* HCL driver */
extern devfs_handle_t hwgraph_root;


typedef long            labelcl_info_place_t;
typedef long            arbitrary_info_t;
typedef long            arb_info_desc_t;

/* Support for INVENTORY */
struct inventory_s;
struct invplace_s;
extern struct invplace_s invplace_none;


/* 
 * Reserve room in every vertex for 2 pieces of fast access indexed information 
 * Note that we do not save a pointer to the bdevsw or cdevsw[] tables anymore.
 */
#define HWGRAPH_NUM_INDEX_INFO	2	/* MAX Entries */
#define HWGRAPH_CONNECTPT	0	/* connect point (aprent) */
#define HWGRAPH_FASTINFO	1	/* callee's private handle */

/*
 * Reserved edge_place_t values, used as the "place" parameter to edge_get_next.
 * Every vertex in the hwgraph has up to 2 *implicit* edges.  There is an implicit
 * edge called "." that points to the current vertex.  There is an implicit edge
 * called ".." that points to the vertex' connect point.
 */
#define EDGE_PLACE_WANT_CURRENT 0	/* "." */
#define EDGE_PLACE_WANT_CONNECTPT 1	/* ".." */
#define EDGE_PLACE_WANT_REAL_EDGES 2	/* Get the first real edge */
#define HWGRAPH_RESERVED_PLACES 2


/*
 * Special pre-defined edge labels.
 */
#define HWGRAPH_EDGELBL_HW 	"hw"
#define HWGRAPH_EDGELBL_DOT 	"."
#define HWGRAPH_EDGELBL_DOTDOT 	".."
#define graph_edge_place_t uint

/*
 * External declarations of EXPORTED SYMBOLS in hcl.c
 */
extern devfs_handle_t hwgraph_register(devfs_handle_t, const char *,
	unsigned int, unsigned int, unsigned int, unsigned int,
	umode_t, uid_t, gid_t, struct file_operations *, void *);

extern int hwgraph_mk_symlink(devfs_handle_t, const char *, unsigned int,
	unsigned int, const char *, unsigned int, devfs_handle_t *, void *);

extern int hwgraph_vertex_destroy(devfs_handle_t);

extern int hwgraph_edge_add(devfs_handle_t, devfs_handle_t, char *);
extern int hwgraph_edge_get(devfs_handle_t, char *, devfs_handle_t *);

extern arbitrary_info_t hwgraph_fastinfo_get(devfs_handle_t);
extern void hwgraph_fastinfo_set(devfs_handle_t, arbitrary_info_t );
extern devfs_handle_t hwgraph_mk_dir(devfs_handle_t, const char *, unsigned int, void *);

extern int hwgraph_connectpt_set(devfs_handle_t, devfs_handle_t);
extern devfs_handle_t hwgraph_connectpt_get(devfs_handle_t);
extern int hwgraph_edge_get_next(devfs_handle_t, char *, devfs_handle_t *, uint *);
extern graph_error_t hwgraph_edge_remove(devfs_handle_t, char *, devfs_handle_t *);

extern graph_error_t hwgraph_traverse(devfs_handle_t, char *, devfs_handle_t *);

extern int hwgraph_vertex_get_next(devfs_handle_t *, devfs_handle_t *);
extern int hwgraph_inventory_get_next(devfs_handle_t, invplace_t *, 
				      inventory_t **);
extern int hwgraph_inventory_add(devfs_handle_t, int, int, major_t, minor_t, int);
extern int hwgraph_inventory_remove(devfs_handle_t, int, int, major_t, minor_t, int);
extern int hwgraph_controller_num_get(devfs_handle_t);
extern void hwgraph_controller_num_set(devfs_handle_t, int);
extern int hwgraph_path_ad(devfs_handle_t, char *, devfs_handle_t *);
extern devfs_handle_t hwgraph_path_to_vertex(char *);
extern devfs_handle_t hwgraph_path_to_dev(char *);
extern devfs_handle_t hwgraph_block_device_get(devfs_handle_t);
extern devfs_handle_t hwgraph_char_device_get(devfs_handle_t);
extern graph_error_t hwgraph_char_device_add(devfs_handle_t, char *, char *, devfs_handle_t *);
extern int hwgraph_path_add(devfs_handle_t, char *, devfs_handle_t *);
extern struct file_operations * hwgraph_bdevsw_get(devfs_handle_t);
extern int hwgraph_info_add_LBL(devfs_handle_t, char *, arbitrary_info_t);
extern int hwgraph_info_get_LBL(devfs_handle_t, char *, arbitrary_info_t *);
extern int hwgraph_info_replace_LBL(devfs_handle_t, char *, arbitrary_info_t,
				    arbitrary_info_t *);
extern int hwgraph_info_get_exported_LBL(devfs_handle_t, char *, int *, arbitrary_info_t *);
extern int hwgraph_info_get_next_LBL(devfs_handle_t, char *, arbitrary_info_t *,
                                labelcl_info_place_t *);

extern int hwgraph_path_lookup(devfs_handle_t, char *, devfs_handle_t *, char **);
extern int hwgraph_info_export_LBL(devfs_handle_t, char *, int);
extern int hwgraph_info_unexport_LBL(devfs_handle_t, char *);
extern int hwgraph_info_remove_LBL(devfs_handle_t, char *, arbitrary_info_t *);
extern char * vertex_to_name(devfs_handle_t, char *, uint);
extern graph_error_t hwgraph_vertex_unref(devfs_handle_t);



#endif /* _ASM_SN_HCL_H */
