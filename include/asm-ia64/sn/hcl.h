/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997,2000-2003 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_HCL_H
#define _ASM_IA64_SN_HCL_H

#include <linux/fs.h>
#include <asm/sn/sgi.h>

extern vertex_hdl_t hwgraph_root;
extern vertex_hdl_t linux_busnum;

void hwgraph_debug(char *, const char *, int, vertex_hdl_t, vertex_hdl_t, char *, ...);

#if 1
#define HWGRAPH_DEBUG(args...) hwgraph_debug(args)
#else   
#define HWGRAPH_DEBUG(args)
#endif  

typedef long            labelcl_info_place_t;
typedef long            arbitrary_info_t;
typedef long            arb_info_desc_t;


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

#include <asm/sn/labelcl.h>
#define hwgraph_fastinfo_set(a,b) labelcl_info_replace_IDX(a, HWGRAPH_FASTINFO, b, NULL)
#define hwgraph_connectpt_set labelcl_info_connectpt_set
#define hwgraph_generate_path hwgfs_generate_path
#define hwgraph_path_to_vertex(a) hwgfs_find_handle(NULL, a, 0, 0, 0, 1)
#define hwgraph_vertex_unref(a)

/*
 * External declarations of EXPORTED SYMBOLS in hcl.c
 */
extern vertex_hdl_t hwgraph_register(vertex_hdl_t, const char *,
	unsigned int, unsigned int, unsigned int, unsigned int,
	umode_t, uid_t, gid_t, struct file_operations *, void *);

extern int hwgraph_mk_symlink(vertex_hdl_t, const char *, unsigned int,
	unsigned int, const char *, unsigned int, vertex_hdl_t *, void *);

extern int hwgraph_vertex_destroy(vertex_hdl_t);

extern int hwgraph_edge_add(vertex_hdl_t, vertex_hdl_t, char *);
extern int hwgraph_edge_get(vertex_hdl_t, char *, vertex_hdl_t *);

extern arbitrary_info_t hwgraph_fastinfo_get(vertex_hdl_t);
extern vertex_hdl_t hwgraph_mk_dir(vertex_hdl_t, const char *, unsigned int, void *);

extern int hwgraph_connectpt_set(vertex_hdl_t, vertex_hdl_t);
extern vertex_hdl_t hwgraph_connectpt_get(vertex_hdl_t);
extern int hwgraph_edge_get_next(vertex_hdl_t, char *, vertex_hdl_t *, unsigned int *);

extern graph_error_t hwgraph_traverse(vertex_hdl_t, char *, vertex_hdl_t *);

extern int hwgraph_vertex_get_next(vertex_hdl_t *, vertex_hdl_t *);
extern int hwgraph_path_add(vertex_hdl_t, char *, vertex_hdl_t *);
extern vertex_hdl_t hwgraph_path_to_dev(char *);
extern vertex_hdl_t hwgraph_block_device_get(vertex_hdl_t);
extern vertex_hdl_t hwgraph_char_device_get(vertex_hdl_t);
extern graph_error_t hwgraph_char_device_add(vertex_hdl_t, char *, char *, vertex_hdl_t *);
extern int hwgraph_path_add(vertex_hdl_t, char *, vertex_hdl_t *);
extern int hwgraph_info_add_LBL(vertex_hdl_t, char *, arbitrary_info_t);
extern int hwgraph_info_get_LBL(vertex_hdl_t, char *, arbitrary_info_t *);
extern int hwgraph_info_replace_LBL(vertex_hdl_t, char *, arbitrary_info_t,
				    arbitrary_info_t *);
extern int hwgraph_info_get_exported_LBL(vertex_hdl_t, char *, int *, arbitrary_info_t *);
extern int hwgraph_info_get_next_LBL(vertex_hdl_t, char *, arbitrary_info_t *,
                                labelcl_info_place_t *);
extern int hwgraph_info_export_LBL(vertex_hdl_t, char *, int);
extern int hwgraph_info_unexport_LBL(vertex_hdl_t, char *);
extern int hwgraph_info_remove_LBL(vertex_hdl_t, char *, arbitrary_info_t *);
extern char *vertex_to_name(vertex_hdl_t, char *, unsigned int);

#endif /* _ASM_IA64_SN_HCL_H */
