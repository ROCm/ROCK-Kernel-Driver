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
#include <linux/config.h>
#include <linux/ctype.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>

#include <asm/sn/agent.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/module.h>
#include <asm/sn/router.h>
#include <asm/sn/xtalk/xbow.h>

#define printf printk
int hasmetarouter;

#define LDEBUG 0
#define NIC_UNKNOWN ((nic_t) -1)

#undef DEBUG_KLGRAPH
#ifdef DEBUG_KLGRAPH
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG_KLGRAPH */

static void sort_nic_names(lboard_t *) ;

lboard_t *
find_lboard(lboard_t *start, unsigned char brd_type)
{
	/* Search all boards stored on this node. */
	while (start) {
		if (start->brd_type == brd_type)
			return start;
		start = KLCF_NEXT(start);
	}

	/* Didn't find it. */
	return (lboard_t *)NULL;
}

lboard_t *
find_lboard_class(lboard_t *start, unsigned char brd_type)
{
	/* Search all boards stored on this node. */
	while (start) {
		if (KLCLASS(start->brd_type) == KLCLASS(brd_type))
			return start;
		start = KLCF_NEXT(start);
	}

	/* Didn't find it. */
	return (lboard_t *)NULL;
}

klinfo_t *
find_component(lboard_t *brd, klinfo_t *kli, unsigned char struct_type)
{
	int index, j;

	if (kli == (klinfo_t *)NULL) {
		index = 0;
	} else {
		for (j = 0; j < KLCF_NUM_COMPS(brd); j++) {
			if (kli == KLCF_COMP(brd, j))
				break;
		}
		index = j;
		if (index == KLCF_NUM_COMPS(brd)) {
			printf("find_component: Bad pointer: 0x%p\n", kli);
			return (klinfo_t *)NULL;
		}
		index++;	/* next component */
	}
	
	for (; index < KLCF_NUM_COMPS(brd); index++) {		
		kli = KLCF_COMP(brd, index);
		DBG("find_component: brd %p kli %p  request type = 0x%x kli type 0x%x\n", brd, kli, kli->struct_type, KLCF_COMP_TYPE(kli));
		if (KLCF_COMP_TYPE(kli) == struct_type)
			return kli;
	}

	/* Didn't find it. */
	return (klinfo_t *)NULL;
}

klinfo_t *
find_first_component(lboard_t *brd, unsigned char struct_type)
{
	return find_component(brd, (klinfo_t *)NULL, struct_type);
}

lboard_t *
find_lboard_modslot(lboard_t *start, moduleid_t mod, slotid_t slot)
{
	/* Search all boards stored on this node. */
	while (start) {
		if (MODULE_MATCH(start->brd_module, mod) &&
		    (start->brd_slot == slot))
			return start;
		start = KLCF_NEXT(start);
	}

	/* Didn't find it. */
	return (lboard_t *)NULL;
}

lboard_t *
find_lboard_module(lboard_t *start, moduleid_t mod)
{
        /* Search all boards stored on this node. */
        while (start) {
                if (MODULE_MATCH(start->brd_module, mod))
                        return start;
                start = KLCF_NEXT(start);
        }

        /* Didn't find it. */
        return (lboard_t *)NULL;
}

lboard_t *
find_lboard_module_class(lboard_t *start, moduleid_t mod,
                                                unsigned char brd_type)
{
	while (start) {

		DBG("find_lboard_module_class: lboard 0x%p, start->brd_module 0x%x, mod 0x%x, start->brd_type 0x%x, brd_type 0x%x\n", start, start->brd_module, mod, start->brd_type, brd_type);

		if (MODULE_MATCH(start->brd_module, mod) &&
			(KLCLASS(start->brd_type) == KLCLASS(brd_type)))
			return start;
		start = KLCF_NEXT(start);
	}

	/* Didn't find it. */
	return (lboard_t *)NULL;
}

#ifndef CONFIG_IA64_SGI_IO
#define tolower(c)	(isupper(c) ? (c) - 'A' + 'a' : (c))
#define toupper(c)	(islower(c) ? (c) - 'a' + 'A' : (c))
#endif


/*
 * Convert a NIC name to a name for use in the hardware graph.
 */
void
nic_name_convert(char *old_name, char *new_name)
{
        int i;
        char c;
        char *compare_ptr;

	if ((old_name[0] == '\0') || (old_name[1] == '\0')) {
                strcpy(new_name, EDGE_LBL_XWIDGET);
        } else {
                for (i = 0; i < strlen(old_name); i++) {
                        c = old_name[i];

                        if (isalpha(c))
                                new_name[i] = tolower(c);
                        else if (isdigit(c))
                                new_name[i] = c;
                        else
                                new_name[i] = '_';
                }
                new_name[i] = '\0';
        }

        /* XXX -
         * Since a bunch of boards made it out with weird names like
         * IO6-fibbbed and IO6P2, we need to look for IO6 in a name and
         * replace it with "baseio" to avoid confusion in the field.
	 * We also have to make sure we don't report media_io instead of
	 * baseio.
         */

        /* Skip underscores at the beginning of the name */
        for (compare_ptr = new_name; (*compare_ptr) == '_'; compare_ptr++)
                ;

	/*
	 * Check for some names we need to replace.  Early boards
	 * had junk following the name so check only the first
	 * characters.
	 */
        if (!strncmp(new_name, "io6", 3) || 
            !strncmp(new_name, "mio", 3) || 
	    !strncmp(new_name, "media_io", 8))
		strcpy(new_name, "baseio");
#if !defined(CONFIG_SGI_IP35) && !defined(CONFIG_IA64_SGI_SN1) && !defined(CONFIG_IA64_GENERIC)
	else if (!strncmp(new_name, "ip29", 4))
		strcpy(new_name,SN00_MOTHERBOARD);
#endif
	else if (!strncmp(new_name, "divo", 4))
		strcpy(new_name, "divo") ;

}

/* Check if the given board corresponds to the global 
 * master io6
 */
int
is_master_baseio(nasid_t nasid,moduleid_t module,slotid_t slot)
{
	lboard_t	*board;

#if CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 || CONFIG_IA64_GENERIC
/* BRINGUP: If this works then look for callers of is_master_baseio()
 * (e.g. iograph.c) and let them pass in a slot if they want
 */
	board = find_lboard_module((lboard_t *)KL_CONFIG_INFO(nasid), module);
#else
	board = find_lboard_modslot((lboard_t *)KL_CONFIG_INFO(nasid),
				    module, slot);
#endif

#ifndef _STANDALONE
	{
		cnodeid_t cnode = NASID_TO_COMPACT_NODEID(nasid);

		if (!board && (NODEPDA(cnode)->xbow_peer != INVALID_NASID))
#if CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 || CONFIG_IA64_GENERIC
			board = find_lboard_module((lboard_t *)
				    KL_CONFIG_INFO(NODEPDA(cnode)->xbow_peer),
				    module);
#else
			board = find_lboard_modslot((lboard_t *)
				    KL_CONFIG_INFO(NODEPDA(cnode)->xbow_peer),
				    module, slot);
#endif
	}
#endif
	if (!board)
		return(0);
	return(board->brd_flags & GLOBAL_MASTER_IO6);
}
/*
 * Find the lboard structure and get the board name.
 * If we can't find the structure or it's too low a revision,
 * use default name.
 */
lboard_t *
get_board_name(nasid_t nasid, moduleid_t mod, slotid_t slot, char *name)
{
	lboard_t *brd;

	brd = find_lboard_modslot((lboard_t *)KL_CONFIG_INFO(nasid),
				  mod, slot);

#ifndef _STANDALONE
	{
		cnodeid_t cnode = NASID_TO_COMPACT_NODEID(nasid);

		if (!brd && (NODEPDA(cnode)->xbow_peer != INVALID_NASID))
			brd = find_lboard_modslot((lboard_t *)
				KL_CONFIG_INFO(NODEPDA(cnode)->xbow_peer),
				mod, slot);
	}
#endif

	if (!brd || (brd->brd_sversion < 2)) {
		strcpy(name, EDGE_LBL_XWIDGET);
	} else {
		nic_name_convert(brd->brd_name, name);
	}

	/*
 	 * PV # 540860
	 * If the name is not 'baseio' or SN00 MOTHERBOARD
	 * get the lowest of all the names in the nic string.
	 * This is needed for boards like divo, which can have
	 * a bunch of daughter cards, but would like to be called
	 * divo. We could do this for baseio and SN00 MOTHERBOARD
 	 * but it has some special case names that we would not
 	 * like to disturb at this point.
	 */

	/* gfx boards don't need any of this name scrambling */
	if (brd && (KLCLASS(brd->brd_type) == KLCLASS_GFX)) {
		return(brd);
	}

	if (!(!strcmp(name, "baseio") )) {
		if (brd) {
			sort_nic_names(brd) ;
			/* Convert to small case, '-' to '_' etc */
			nic_name_convert(brd->brd_name, name) ;
		}
	}

	return(brd);
}

int
get_cpu_slice(cpuid_t cpu)
{
	klcpu_t *acpu;
	if ((acpu = get_cpuinfo(cpu)) == NULL)
	    return -1;
	return acpu->cpu_info.physid;
}


/*
 * get_actual_nasid
 *
 *	Completely disabled brds have their klconfig on 
 *	some other nasid as they have no memory. But their
 *	actual nasid is hidden in the klconfig. Use this
 *	routine to get it. Works for normal boards too.
 */
nasid_t
get_actual_nasid(lboard_t *brd)
{
	klhub_t	*hub ;

	if (!brd)
		return INVALID_NASID ;

	/* find out if we are a completely disabled brd. */

        hub  = (klhub_t *)find_first_component(brd, KLSTRUCT_HUB);
	if (!hub)
                return INVALID_NASID ;
	if (!(hub->hub_info.flags & KLINFO_ENABLE))	/* disabled node brd */
		return hub->hub_info.physid ;
	else
		return brd->brd_nasid ;
}

int
xbow_port_io_enabled(nasid_t nasid, int link)
{
	lboard_t *brd;
	klxbow_t *xbow_p;

	/*
	 * look for boards that might contain an xbow or xbridge
	 */
#if SN0
	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_MIDPLANE8);
#else
	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_PBRICK_XBOW);
#endif
	if (brd == NULL) return 0;
		
	if ((xbow_p = (klxbow_t *)find_component(brd, NULL, KLSTRUCT_XBOW))
	    == NULL)
	    return 0;

	if (!XBOW_PORT_TYPE_IO(xbow_p, link) || !XBOW_PORT_IS_ENABLED(xbow_p, link))
	    return 0;

	printf("xbow_port_io_enabled:  brd 0x%p xbow_p 0x%p \n", brd, xbow_p);

	return 1;
}

void
board_to_path(lboard_t *brd, char *path)
{
	moduleid_t modnum;
	char *board_name;
#if !defined(CONFIG_SGI_IP35) && !defined(CONFIG_IA64_SGI_SN1) && !defined(CONFIG_IA64_GENERIC)
	slotid_t slot;
	char slot_name[SLOTNUM_MAXLENGTH];
#endif

	ASSERT(brd);

	switch (KLCLASS(brd->brd_type)) {

		case KLCLASS_NODE:
			board_name = EDGE_LBL_NODE;
			break;
		case KLCLASS_ROUTER:
			if (brd->brd_type == KLTYPE_META_ROUTER) {
				board_name = EDGE_LBL_META_ROUTER;
				hasmetarouter++;
			} else
				board_name = EDGE_LBL_ROUTER;
			break;
		case KLCLASS_MIDPLANE:
			board_name = EDGE_LBL_MIDPLANE;
			break;
		case KLCLASS_IO:
			board_name = EDGE_LBL_IO;
			break;
		case KLCLASS_IOBRICK:
			if (brd->brd_type == KLTYPE_PBRICK)
				board_name = EDGE_LBL_PBRICK;
			else if (brd->brd_type == KLTYPE_IBRICK)
				board_name = EDGE_LBL_IBRICK;
			else if (brd->brd_type == KLTYPE_XBRICK)
				board_name = EDGE_LBL_XBRICK;
			else
				board_name = EDGE_LBL_IOBRICK;
			break;
		default:
			board_name = EDGE_LBL_UNKNOWN;
	}
			
	modnum = brd->brd_module;

#if defined(SN0)
	slot = brd->brd_slot;
	get_slotname(slot, slot_name);

	ASSERT(modnum >= 0);

	sprintf(path, "%H/" EDGE_LBL_SLOT "/%s/%s", 
		modnum, slot_name, board_name);
#else
	ASSERT(modnum != MODULE_UNKNOWN && modnum != INVALID_MODULE);
#ifdef BRINGUP /* fix IP35 hwgraph */
	sprintf(path, EDGE_LBL_MODULE "/%x/%s", modnum, board_name);
#else
	sprintf(path, "%H/%s", modnum, board_name);
#endif
#endif
}

/*
 * Get the module number for a NASID.
 */
moduleid_t
get_module_id(nasid_t nasid)
{
	lboard_t *brd;

	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_IP27);

	if (!brd)
		return INVALID_MODULE;
	else
		return brd->brd_module;
}


#ifndef CONFIG_IA64_SGI_IO
#if 1
/*
 *  find_gfxpipe(#)
 *
 *  XXXmacko
 *  This is only used by graphics drivers, and should be moved
 *  over to gfx/kern/graphics/SN0 as soon as it's convenient.
 */
static klgfx_t *graphics_pipe_list = NULL;
static devfs_handle_t hwgraph_all_gfxids = GRAPH_VERTEX_NONE;

void
setup_gfxpipe_link(devfs_handle_t vhdl,int pipenum)
{
	char idbuf[8];
	extern graph_hdl_t hwgraph;

	graph_info_add_LBL(hwgraph, vhdl, INFO_LBL_GFXID, INFO_DESC_EXPORT, 
		(arbitrary_info_t)pipenum);
	if (hwgraph_all_gfxids == GRAPH_VERTEX_NONE)
		hwgraph_path_add(hwgraph_root, EDGE_LBL_GFX, &hwgraph_all_gfxids);
	sprintf(idbuf, "%d", pipenum);
	hwgraph_edge_add(hwgraph_all_gfxids, vhdl, idbuf);

}
#endif

/* 
 * find the pipenum'th logical graphics pipe (KLCLASS_GFX)
 */
lboard_t *
find_gfxpipe(int pipenum)
{
        gda_t           *gdap;
        cnodeid_t       cnode;
        nasid_t         nasid;
        lboard_t        *lb;
	klgfx_t		*kg,**pkg;
	int		i;

        gdap = (gda_t *)GDA_ADDR(get_nasid());
        if (gdap->g_magic != GDA_MAGIC)
        	return NULL;

	if (!graphics_pipe_list) {
		/* for all nodes */
        	for (cnode = 0; cnode < MAX_COMPACT_NODES; cnode ++) {
                	nasid = gdap->g_nasidtable[cnode];
                	if (nasid == INVALID_NASID)
                        	continue;
			lb = KL_CONFIG_INFO(nasid) ;
			while (lb = find_lboard_class(lb, KLCLASS_GFX)) {
				moduleid_t kgm, pkgm;
				int	kgs, pkgs;

#if defined(DEBUG) && (defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1 || defined(CONFIG_IA64_GENERIC))) && defined(BRINGUP)
				printf("find_gfxpipe(): PIPE: %s mod %M slot %d\n",lb?lb->brd_name:"!LBRD",
					lb->brd_module,lb->brd_slot);
#endif
				/* insert lb into list */
				if (!(kg = (klgfx_t*)find_first_component(lb,KLSTRUCT_GFX))) {
					lb = KLCF_NEXT(lb);
					continue;
				}
				/* set moduleslot now that we have brd_module set */
				kg->moduleslot = (lb->brd_module << 8) | SLOTNUM_GETSLOT(lb->brd_slot);
				/* make sure board has device flag set */
				kg->gfx_info.flags |= KLINFO_DEVICE;
				if (kg->cookie < KLGFX_COOKIE) {
				    kg->gfx_next_pipe = NULL;
				    kg->cookie = KLGFX_COOKIE;
				}

				kgm = kg->moduleslot>>8;
				kgs = kg->moduleslot&0xff;
				pkg = &graphics_pipe_list;
				while (*pkg) {
					pkgm = (*pkg)->moduleslot>>8;
					pkgs = (*pkg)->moduleslot&0xff;

					if (!(MODULE_CMP(kgm, pkgm) > 0 ||
					      (MODULE_CMP(kgm, pkgm) == 0 &&
					       kgs > pkgs)))
					    break;

					pkg = &(*pkg)->gfx_next_pipe;
				}
				kg->gfx_next_pipe = *pkg;
				*pkg = kg;
				lb = KLCF_NEXT(lb);
			}
		}
#ifdef FIND_GFXPIPE_DEBUG
		i = 0;
		kg = graphics_pipe_list;
		while (kg) {
			lboard_t *lb;
#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
			lb = find_lboard_class(KL_CONFIG_INFO(kg->gfx_info.nasid), KLCLASS_GFX);
#else
#error Need to figure out how to find graphics boards ...
#endif
#if defined(SUPPORT_PRINTING_M_FORMAT)
			printf("find_gfxpipe(): %s pipe %d mod %M slot %d\n",lb?lb->brd_name:"!LBRD",i,
				(kg->moduleslot>>8),(kg->moduleslot&0xff));
#else
			printf("find_gfxpipe(): %s pipe %d mod 0x%x slot %d\n",lb?lb->brd_name:"!LBRD",i,
				(kg->moduleslot>>8),(kg->moduleslot&0xff));
#endif
			kg = kg->gfx_next_pipe;
			i++;
		}
#endif
        }

	i = 0;
	kg = graphics_pipe_list;
	while (kg && (i < pipenum)) {
		kg = kg->gfx_next_pipe;
		i++;
		}

	if (!kg) return NULL;

#if defined(SN0)
	return find_lboard_modslot(KL_CONFIG_INFO(kg->gfx_info.nasid),
				(kg->moduleslot>>8),
				SLOTNUM_XTALK_CLASS|(kg->moduleslot&0xff));
#elif defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
	return find_lboard_class(KL_CONFIG_INFO(kg->gfx_info.nasid), KLCLASS_GFX);
#else
#error Need to figure out how to find graphics boards ...
#endif
}
#endif


#define MHZ	1000000

#ifndef CONFIG_IA64_SGI_IO
uint
cpu_cycles_adjust(uint orig_cycles)
{
	klcpu_t *acpu;
	uint speed;

	acpu  = nasid_slice_to_cpuinfo(get_nasid(), get_slice());

	if (acpu == NULL) return orig_cycles;

	/*
	 * cpu cycles seem to be half of the real value, hack and mult by 2
	 * for now.
	 */
	speed = (orig_cycles * 2) / MHZ;

	/*
	 * if the cpu thinks its running at some random speed nowhere close 
	 * the programmed speed, do nothing.
	 */
	if ((speed < (acpu->cpu_speed - 2)) || (speed > (acpu->cpu_speed + 2)))
	    return orig_cycles;
	return (acpu->cpu_speed * MHZ/2);
}
#endif /* CONFIG_IA64_SGI_IO */

/* Get the canonical hardware graph name for the given pci component
 * on the given io board.
 */
void
device_component_canonical_name_get(lboard_t 	*brd,
				    klinfo_t 	*component,
				    char 	*name)
{
	moduleid_t 	modnum;
	slotid_t 	slot;
	char 		board_name[20];
#ifdef SN0
	char 		slot_name[SLOTNUM_MAXLENGTH];
#endif

	ASSERT(brd);

	/* Get the module number of this board */
	modnum = brd->brd_module;

	/* Convert the [ CLASS | TYPE ] kind of slotid
	 * into a string 
	 */
	slot = brd->brd_slot;
#ifdef SN0
	get_slotname(slot, slot_name);

	ASSERT(modnum >= 0);
#else
	ASSERT(modnum != MODULE_UNKNOWN && modnum != INVALID_MODULE);
#endif

	/* Get the io board name  */
	if (!brd || (brd->brd_sversion < 2)) {
		strcpy(name, EDGE_LBL_XWIDGET);
	} else {
		nic_name_convert(brd->brd_name, board_name);
	}

	/* Give out the canonical  name of the pci device*/
#ifdef SN0
	sprintf(name, 
		"/hw/"EDGE_LBL_MODULE "/%M/"EDGE_LBL_SLOT"/%s/%s/"
		EDGE_LBL_PCI"/%d", 
		modnum, slot_name, board_name,KLCF_BRIDGE_W_ID(component));
#elif defined (CONFIG_SGI_IP35)  || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
	sprintf(name, 
		"/dev/hw/"EDGE_LBL_MODULE "/%x/"EDGE_LBL_SLOT"/%s/"
		EDGE_LBL_PCI"/%d", 
		modnum, board_name,KLCF_BRIDGE_W_ID(component));
#endif
	
}

/*
 * Get the serial number of the main  component of a board
 * Returns 0 if a valid serial number is found
 * 1 otherwise.
 * Assumptions: Nic manufacturing string  has the following format
 *			*Serial:<serial_number>;*
 */
static int
component_serial_number_get(lboard_t 		*board,
			    klconf_off_t 	mfg_nic_offset,
			    char		*serial_number,
			    char		*key_pattern)
{

	char	*mfg_nic_string;
	char	*serial_string,*str;
	int	i;
	char	*serial_pattern = "Serial:";

	/* We have an error on a null mfg nic offset */
	if (!mfg_nic_offset)
		return(1);
	/* Get the hub's manufacturing nic information
	 * which is in the form of a pre-formatted string
	 */
	mfg_nic_string = 
		(char *)NODE_OFFSET_TO_K0(NASID_GET(board),
					  mfg_nic_offset);
	/* There is no manufacturing nic info */
	if (!mfg_nic_string)
		return(1);

	str = mfg_nic_string;
	/* Look for the key pattern first (if it is  specified)
	 * and then print the serial number corresponding to that.
	 */
	if (strcmp(key_pattern,"") && 
	    !(str = strstr(mfg_nic_string,key_pattern)))
		return(1);

	/* There is no serial number info in the manufacturing
	 * nic info
	 */
	if (!(serial_string = strstr(str,serial_pattern)))
		return(1);

	serial_string = serial_string + strlen(serial_pattern);
	/*  Copy the serial number information from the klconfig */
	i = 0;
	while (serial_string[i] != ';') {
		serial_number[i] = serial_string[i];
		i++;
	}
	serial_number[i] = 0;
	
	return(0);
}
/*
 * Get the serial number of a board
 * Returns 0 if a valid serial number is found
 * 1 otherwise.
 */

int
board_serial_number_get(lboard_t *board,char *serial_number)
{
	ASSERT(board && serial_number);
	if (!board || !serial_number)
		return(1);

	strcpy(serial_number,"");
	switch(KLCLASS(board->brd_type)) {
	case KLCLASS_CPU: {	/* Node board */
		klhub_t	*hub;
		
		/* Get the hub component information */
		hub = (klhub_t *)find_first_component(board,
						      KLSTRUCT_HUB);
		/* If we don't have a hub component on an IP27
		 * then we have a weird klconfig.
		 */
		if (!hub)
			return(1);
		/* Get the serial number information from
		 * the hub's manufacturing nic info
		 */
		if (component_serial_number_get(board,
						hub->hub_mfg_nic,
						serial_number,
#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
						"IP35"))
#else
						"IP27"))
			/* Try with IP31 key if IP27 key fails */
			if (component_serial_number_get(board,
							hub->hub_mfg_nic,
							serial_number,
							"IP31"))
#endif /* CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 */
				return(1);
		break;
	}
	case KLCLASS_IO: {	/* IO board */
		if (KLTYPE(board->brd_type) == KLTYPE_TPU) {
		/* Special case for TPU boards */
			kltpu_t *tpu;	
		
			/* Get the tpu component information */
			tpu = (kltpu_t *)find_first_component(board,
						      KLSTRUCT_TPU);
			/* If we don't have a tpu component on a tpu board
			 * then we have a weird klconfig.
			 */
			if (!tpu)
				return(1);
			/* Get the serial number information from
			 * the tpu's manufacturing nic info
			 */
			if (component_serial_number_get(board,
						tpu->tpu_mfg_nic,
						serial_number,
						""))
				return(1);
			break;
		} else  if ((KLTYPE(board->brd_type) == KLTYPE_GSN_A) ||
		            (KLTYPE(board->brd_type) == KLTYPE_GSN_B)) {
		/* Special case for GSN boards */
			klgsn_t *gsn;	
		
			/* Get the gsn component information */
			gsn = (klgsn_t *)find_first_component(board,
			      ((KLTYPE(board->brd_type) == KLTYPE_GSN_A) ?
					KLSTRUCT_GSN_A : KLSTRUCT_GSN_B));
			/* If we don't have a gsn component on a gsn board
			 * then we have a weird klconfig.
			 */
			if (!gsn)
				return(1);
			/* Get the serial number information from
			 * the gsn's manufacturing nic info
			 */
			if (component_serial_number_get(board,
						gsn->gsn_mfg_nic,
						serial_number,
						""))
				return(1);
			break;
		} else {
		     	klbri_t	*bridge;
		
			/* Get the bridge component information */
			bridge = (klbri_t *)find_first_component(board,
							 KLSTRUCT_BRI);
			/* If we don't have a bridge component on an IO board
			 * then we have a weird klconfig.
			 */
			if (!bridge)
				return(1);
			/* Get the serial number information from
		 	 * the bridge's manufacturing nic info
			 */
			if (component_serial_number_get(board,
						bridge->bri_mfg_nic,
						serial_number,
						""))
				return(1);
			break;
		}
	}
	case KLCLASS_ROUTER: {	/* Router board */
		klrou_t *router;	
		
		/* Get the router component information */
		router = (klrou_t *)find_first_component(board,
							 KLSTRUCT_ROU);
		/* If we don't have a router component on a router board
		 * then we have a weird klconfig.
		 */
		if (!router)
			return(1);
		/* Get the serial number information from
		 * the router's manufacturing nic info
		 */
		if (component_serial_number_get(board,
						router->rou_mfg_nic,
						serial_number,
						""))
			return(1);
		break;
	}
	case KLCLASS_GFX: {	/* Gfx board */
		klgfx_t *graphics;
		
		/* Get the graphics component information */
		graphics = (klgfx_t *)find_first_component(board, KLSTRUCT_GFX);
		/* If we don't have a gfx component on a gfx board
		 * then we have a weird klconfig.
		 */
		if (!graphics)
			return(1);
		/* Get the serial number information from
		 * the graphics's manufacturing nic info
		 */
		if (component_serial_number_get(board,
						graphics->gfx_mfg_nic,
						serial_number,
						""))
			return(1);
		break;
	}
	default:
		strcpy(serial_number,"");
		break;
	}
	return(0);
}

#include "asm/sn/sn_private.h"
#ifndef CONFIG_IA64_SGI_IO
/*
 * Given a physical address get the name of memory dimm bank
 * in a hwgraph name format.
 */
void
membank_pathname_get(paddr_t paddr,char *name)
{
	cnodeid_t	cnode;
	char		slotname[SLOTNUM_MAXLENGTH];

	cnode = paddr_cnode(paddr);
	/* Make sure that we have a valid name buffer */
	if (!name)
		return;

	name[0] = 0;
	/* Make sure that the cnode is valid */
	if ((cnode == CNODEID_NONE) || (cnode > numnodes))
		return;
	/* Given a slotid(class:type) get the slotname */
#if defined (SN0)
	get_slotname(NODE_SLOTID(cnode),slotname);
	sprintf(name,
		"/hw/"EDGE_LBL_MODULE"/%M/"EDGE_LBL_SLOT"/%s/"EDGE_LBL_NODE
		"/"EDGE_LBL_MEMORY"/dimm_bank/%d",
		NODE_MODULEID(cnode),slotname,paddr_dimm(paddr));
#elif defined (CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
	sprintf(name,
		"/dev/hw/"EDGE_LBL_MODULE"/%M/"EDGE_LBL_NODE
		"/"EDGE_LBL_MEMORY"/dimm_bank/%d",
		NODE_MODULEID(cnode),paddr_dimm(paddr));
#endif
}



int
membank_check_mixed_hidensity(nasid_t nasid)
{
	lboard_t *brd;
	klmembnk_t *mem;
	int min_size = 1024, max_size = 0;
	int bank, mem_size;

	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_IP27);
	ASSERT(brd);

	mem = (klmembnk_t *)find_first_component(brd, KLSTRUCT_MEMBNK);
	ASSERT(mem);


	for (mem_size = 0, bank = 0; bank < MD_MEM_BANKS; bank++) {
		mem_size = KLCONFIG_MEMBNK_SIZE(mem, bank);
		if (mem_size < min_size)
		    min_size = mem_size;
		if (mem_size > max_size)
		    max_size = mem_size;
	}
	
	if ((max_size == 512) && (max_size != min_size))
	    return 1;

	return 0;
}


int
mem_mixed_hidensity_banks(void)
{
	cnodeid_t cnode;
	nasid_t nasid;

	for (cnode = 0; cnode < maxnodes; cnode++) {
		nasid = COMPACT_TO_NASID_NODEID(cnode);
		if (nasid == INVALID_NASID)
		    continue;
		if (membank_check_mixed_hidensity(nasid))
		    return 1;
	}
	return 0;

}
#endif /* CONFIG_IA64_SGI_IO */

xwidgetnum_t
nodevertex_widgetnum_get(devfs_handle_t node_vtx)
{
	hubinfo_t hubinfo_p;

	hwgraph_info_get_LBL(node_vtx, INFO_LBL_NODE_INFO, 
			     (arbitrary_info_t *) &hubinfo_p);
	return(hubinfo_p->h_widgetid);
}

devfs_handle_t
nodevertex_xbow_peer_get(devfs_handle_t node_vtx)
{
	hubinfo_t hubinfo_p;
	nasid_t xbow_peer_nasid;
	cnodeid_t xbow_peer;

	hwgraph_info_get_LBL(node_vtx, INFO_LBL_NODE_INFO,
				     (arbitrary_info_t *) &hubinfo_p);
	xbow_peer_nasid = hubinfo_p->h_nodepda->xbow_peer;
	if(xbow_peer_nasid == INVALID_NASID) 
			return ( (devfs_handle_t)-1);
	xbow_peer = NASID_TO_COMPACT_NODEID(xbow_peer_nasid);
	return(NODEPDA(xbow_peer)->node_vertex);
}

/* NIC Sorting Support */

#define MAX_NICS_PER_STRING 	32
#define MAX_NIC_NAME_LEN	32

static char *
get_nic_string(lboard_t *lb)
{
        int         	i;
        klinfo_t    	*k = NULL ;
    	klconf_off_t    mfg_off = 0 ;
    	char            *mfg_nic = NULL ;

        for (i = 0; i < KLCF_NUM_COMPS(lb); i++) {
                k = KLCF_COMP(lb, i) ;
                switch(k->struct_type) {
                        case KLSTRUCT_BRI:
            			mfg_off = ((klbri_t *)k)->bri_mfg_nic ;
				break ;

                        case KLSTRUCT_HUB:
            			mfg_off = ((klhub_t *)k)->hub_mfg_nic ;
				break ;

                        case KLSTRUCT_ROU:
            			mfg_off = ((klrou_t *)k)->rou_mfg_nic ;
				break ;

                        case KLSTRUCT_GFX:
            			mfg_off = ((klgfx_t *)k)->gfx_mfg_nic ;
				break ;

                        case KLSTRUCT_TPU:
            			mfg_off = ((kltpu_t *)k)->tpu_mfg_nic ;
				break ;

                        case KLSTRUCT_GSN_A:
                        case KLSTRUCT_GSN_B:
            			mfg_off = ((klgsn_t *)k)->gsn_mfg_nic ;
				break ;

                        case KLSTRUCT_XTHD:
                                mfg_off = ((klxthd_t *)k)->xthd_mfg_nic ;
                                break;

			default:
				mfg_off = 0 ;
                                break ;
                }
		if (mfg_off)
			break ;
        }

	if ((mfg_off) && (k))
		mfg_nic = (char *)NODE_OFFSET_TO_K0(k->nasid, mfg_off) ;

        return mfg_nic ;
}

char *
get_first_string(char **ptrs, int n)
{
        int     i ;
        char    *tmpptr ;

        if ((ptrs == NULL) || (n == 0))
                return NULL ;

        tmpptr = ptrs[0] ;

        if (n == 1)
                return tmpptr ;

        for (i = 0 ; i < n ; i++) {
                if (strcmp(tmpptr, ptrs[i]) > 0)
                        tmpptr = ptrs[i] ;
        }

        return tmpptr ;
}

int
get_ptrs(char *idata, char **ptrs, int n, char *label)
{
        int     i = 0 ;
        char    *tmp = idata ;

        if ((ptrs == NULL) || (idata == NULL) || (label == NULL) || (n == 0))
                return 0 ;

        while  ( (tmp = strstr(tmp, label)) ){
                tmp += strlen(label) ;
                /* check for empty name field, and last NULL ptr */
                if ((i < (n-1)) && (*tmp != ';')) {
                        ptrs[i++] = tmp ;
                }
        }

        ptrs[i] = NULL ;

        return i ;
}

/*
 * sort_nic_names
 *
 * 	Does not really do sorting. Find the alphabetically lowest
 *	name among all the nic names found in a nic string.
 *
 * Return:
 *	Nothing
 *
 * Side Effects:
 *
 *	lb->brd_name gets the new name found
 */

static void
sort_nic_names(lboard_t *lb)
{
	char 	*nic_str ;
        char    *ptrs[MAX_NICS_PER_STRING] ;
        char    name[MAX_NIC_NAME_LEN] ;
        char    *tmp, *tmp1 ;

	*name = 0 ;

	/* Get the nic pointer from the lb */

	if ((nic_str = get_nic_string(lb)) == NULL)
		return ;

        tmp = get_first_string(ptrs,
                        get_ptrs(nic_str, ptrs, MAX_NICS_PER_STRING, "Name:")) ;

        if (tmp == NULL)
		return ;

        if  ( (tmp1 = strchr(tmp, ';')) ){
                strncpy(name, tmp, tmp1-tmp) ;
                name[tmp1-tmp] = 0 ;
        } else {
                strncpy(name, tmp, (sizeof(name) -1)) ;
                name[sizeof(name)-1] = 0 ;
        }

	strcpy(lb->brd_name, name) ;
}


#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)

char brick_types[MAX_BRICK_TYPES + 1] = "crikxdp789012345";

/*
 * Format a module id for printing.
 */
void
format_module_id(char *buffer, moduleid_t m, int fmt)
{
	int rack, position;
	char brickchar;

	rack = MODULE_GET_RACK(m);
	ASSERT(MODULE_GET_BTYPE(m) < MAX_BRICK_TYPES);
	brickchar = MODULE_GET_BTCHAR(m);
	position = MODULE_GET_BPOS(m);

	if (fmt == MODULE_FORMAT_BRIEF) {
	    /* Brief module number format, eg. 002c15 */

	    /* Decompress the rack number */
	    *buffer++ = '0' + RACK_GET_CLASS(rack);
	    *buffer++ = '0' + RACK_GET_GROUP(rack);
	    *buffer++ = '0' + RACK_GET_NUM(rack);

	    /* Add the brick type */
	    *buffer++ = brickchar;
	}
	else if (fmt == MODULE_FORMAT_LONG) {
	    /* Fuller hwgraph format, eg. rack/002/bay/15 */

	    strcpy(buffer, EDGE_LBL_RACK "/");  buffer += strlen(buffer);

	    *buffer++ = '0' + RACK_GET_CLASS(rack);
	    *buffer++ = '0' + RACK_GET_GROUP(rack);
	    *buffer++ = '0' + RACK_GET_NUM(rack);

	    strcpy(buffer, "/" EDGE_LBL_RPOS "/");  buffer += strlen(buffer);
	}

	/* Add the bay position, using at least two digits */
	if (position < 10)
	    *buffer++ = '0';
	sprintf(buffer, "%d", position);

}

/*
 * Parse a module id, in either brief or long form.
 * Returns < 0 on error.
 * The long form does not include a brick type, so it defaults to 0 (CBrick)
 */
int
parse_module_id(char *buffer)
{
	unsigned int	v, rack, bay, type, form;
	moduleid_t	m;
	char 		c;

	if (strstr(buffer, EDGE_LBL_RACK "/") == buffer) {
		form = MODULE_FORMAT_LONG;
		buffer += strlen(EDGE_LBL_RACK "/");

		/* A long module ID must be exactly 5 non-template chars. */
		if (strlen(buffer) != strlen("/" EDGE_LBL_RPOS "/") + 5)
			return -1;
	}
	else {
		form = MODULE_FORMAT_BRIEF;

		/* A brief module id must be exactly 6 characters */
		if (strlen(buffer) != 6)
			return -2;
	}

	/* The rack number must be exactly 3 digits */
	if (!(isdigit(buffer[0]) && isdigit(buffer[1]) && isdigit(buffer[2])))
		return -3;

	rack = 0;
	v = *buffer++ - '0';
	if (v > RACK_CLASS_MASK(rack) >> RACK_CLASS_SHFT(rack))
		return -4;
	RACK_ADD_CLASS(rack, v);

	v = *buffer++ - '0';
	if (v > RACK_GROUP_MASK(rack) >> RACK_GROUP_SHFT(rack))
		return -5;
	RACK_ADD_GROUP(rack, v);

	v = *buffer++ - '0';
	/* rack numbers are 1-based */
	if (v-1 > RACK_NUM_MASK(rack) >> RACK_NUM_SHFT(rack))
		return -6;
	RACK_ADD_NUM(rack, v);

	if (form == MODULE_FORMAT_BRIEF) {
		/* Next should be a module type character.  Accept ucase or lcase. */
		c = *buffer++;
		if (!isalpha(c))
			return -7;

		/* strchr() returns a pointer into brick_types[], or NULL */
		type = (unsigned int)(strchr(brick_types, tolower(c)) - brick_types);
		if (type > MODULE_BTYPE_MASK >> MODULE_BTYPE_SHFT)
			return -8;
	}
	else {
		/* Hardcode the module type, and skip over the boilerplate */
		type = MODULE_CBRICK;

		if (strstr(buffer, "/" EDGE_LBL_RPOS "/") != buffer)
			return -9;

		buffer += strlen("/" EDGE_LBL_RPOS "/");
	}
		
	/* The bay number is last.  Make sure it's exactly two digits */

	if (!(isdigit(buffer[0]) && isdigit(buffer[1]) && !buffer[2]))
		return -10;

	bay = 10 * (buffer[0] - '0') + (buffer[1] - '0');

	if (bay > MODULE_BPOS_MASK >> MODULE_BPOS_SHFT)
		return -11;

	m = RBT_TO_MODULE(rack, bay, type);

	/* avoid sign extending the moduleid_t */
	return (int)(unsigned short)m;
}

#else /* CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 */

/*
 * Format a module id for printing.
 */
void
format_module_id(char *buffer, moduleid_t m, int fmt)
{
    if (fmt == MODULE_FORMAT_BRIEF) {
		sprintf(buffer, "%d", m);
    }
    else if (fmt == MODULE_FORMAT_LONG) {
		sprintf(buffer, EDGE_LBL_MODULE "/%d", m);
    }
}

/*
 * Parse a module id, in either brief or long form.
 * Returns < 0 on error.
 */
int
parse_module_id(char *buffer)
{
    moduleid_t m;
    char c;

    if (strstr(buffer, EDGE_LBL_MODULE "/") == buffer)
	buffer += strlen(EDGE_LBL_MODULE "/");

    m = 0;
    while(c = *buffer++) {
	if (!isdigit(c))
	    return -1;
	m = 10 * m + (c - '0');
    }

    /* avoid sign extending the moduleid_t */
    return (int)(unsigned short)m;
}

#endif /* CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 */


