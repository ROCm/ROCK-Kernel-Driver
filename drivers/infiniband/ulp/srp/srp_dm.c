/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: srp_dm.c 33 2004-04-09 03:58:41Z roland $
*/

#include "srp_host.h"

static ioc_entry_t ioc_table[MAX_IOCS];
static tTS_IB_PATH_RECORD srp_path_records;
static int last_used_path_record;
static int max_path_record_cache;

extern void srp_move_to_pending( srp_target_t *target );

int srp_find_ioc( tTS_IB_GUID guid, int *ioc_index )
{
    int i;

    for ( i = 0; i < MAX_IOCS; i++ ) {

        if ( ( memcmp( ioc_table[i].guid, guid, sizeof( tTS_IB_GUID ) ) == 0 ) &&
             ( ioc_table[i].valid == TRUE ) ) {
            /* we have a match, return IOC index */
            *ioc_index = i;
            return( TS_SUCCESS );
        }
    }
    return( TS_FAIL );
}


int srp_new_ioc( tTS_IB_GUID guid, int *ioc_index )
{
    int i;

    for ( i = 0; i < MAX_IOCS; i++ ) {

        if ( ioc_table[i].valid == FALSE ) {

            TS_REPORT_STAGE( MOD_SRPTP,"Creating IOC Entry %d for 0x%llx",
                             i, be64_to_cpu(*(uint64_t *)guid) );

            memcpy( ioc_table[i].guid, guid, sizeof( tTS_IB_GUID ) );
            ioc_table[i].valid = TRUE;
            *ioc_index = i;
            return( TS_SUCCESS );
        }
    }

    return( TS_FAIL );
}

static void srp_check_ioc_paths( ioc_entry_t *ioc )
{
    srp_target_t *target;
    int ioc_index;
    int hca_index;
    int port_index;
    int path_available;
    unsigned long cpu_flags;

    ioc_index = ioc - &ioc_table[0];

    spin_lock_irqsave( &driver_params.spin_lock, cpu_flags );

    path_available = FALSE;

    /* check if it has any valid paths */
    for ( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

        for ( port_index = 0; port_index < MAX_LOCAL_PORTS_PER_HCA; port_index++ ) {

            if ( ioc->path_valid[hca_index][port_index] == TRUE ) {

                /* we have at least one path, lets keep the IOC */
                path_available = TRUE;
            }
        }
    }

    if ( path_available == FALSE ) {

        TS_REPORT_WARN( MOD_SRPTP, "IOC GUID %llx, no available paths",
                        be64_to_cpu(*(uint64_t *)ioc->guid) );

        /* no paths available to this IOC, let's remove it from our list */
        ioc->valid = FALSE;

        /* loop through all targets, and indicate that this
         * ioc_index is not available as a path */
        for ( target = &srp_targets[0];
              target < &srp_targets[max_srp_targets];
              target++ ) {

            target->ioc_mask[ioc_index] = FALSE;
            target->ioc_needs_request[ioc_index] = FALSE;

        }
    }
    spin_unlock_irqrestore( &driver_params.spin_lock, cpu_flags );
}


void srp_dm_kill_ioc( srp_target_t *target, int flag )
{
    ioc_entry_t *ioc = target->ioc;
    int hca_index, port_index;

    port_index = target->port->local_port-1;
    hca_index = target->port->hca->hca_index;

    /* use the flag to determine if we want to kill the
     * IOC path or just the path for this target/IOC combination,
     * the difference is if we get a IOU connection failure versus
     * a redirected connection failure */
    if ( flag == TRUE ) {
        /* this will not cause the path to be lost,
         * just asks connection hunt code to skip this IOC
         * for this target */
        target->ioc_needs_request[ioc - &ioc_table[0]] = FALSE;
    } else {
        ioc->path_valid[hca_index][port_index] = FALSE;

        srp_check_ioc_paths( ioc );
    }
}


void srp_clean_ioc_table(void)
{
    int ioc_index;
    ioc_entry_t *ioc_entry;

    for( ioc_index = 0; ioc_index < MAX_IOCS; ioc_index++ ) {

        ioc_entry = &ioc_table[ioc_index];

        /* check if the entry is valid */
        if ( ioc_entry->valid == FALSE )
            continue;

        srp_check_ioc_paths( ioc_entry );


    } // end loop on ioc_table
}


void pick_connection_path( srp_target_t *target )
{
    srp_host_port_params_t *port, *lowest_port = NULL;
    ioc_entry_t *lowest_ioc_entry = NULL;
    int connection_count = 0xFFFF;
    int ioc_index;
    unsigned long cpu_flags;

    spin_lock_irqsave( &driver_params.spin_lock, cpu_flags );

    /*
     * Search for IOC guid with lowest connections first
     */
    for ( ioc_index = 0; ioc_index < MAX_IOCS; ioc_index++ ) {

        if (( target->ioc_mask[ioc_index] == TRUE ) &&
            ( target->ioc_needs_request[ioc_index] == TRUE )) {

            if ( ioc_table[ioc_index].num_connections < connection_count ) {
                lowest_ioc_entry = &ioc_table[ioc_index];
                connection_count = lowest_ioc_entry->num_connections;
            }
        }
    }

    connection_count = 0xFFFF;
    if ( lowest_ioc_entry ) {
        int hca_index, port_index;

        /*
         * Search for the port with the lowest number of connections
         */
        for ( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {
            for ( port_index = 0; port_index < MAX_LOCAL_PORTS_PER_HCA; port_index++ ) {

                port = &hca_params[hca_index].port[port_index];

                /*
                 * check if the port is valid, the port is up and if the IOC can be
                 * seen through this port
                 */
                if (( port->valid == TRUE ) &&
                    ( lowest_ioc_entry->path_valid[hca_index][port_index] == TRUE )) {

                    if ( port->num_connections < connection_count ) {
                        lowest_port = port;
                        connection_count = port->num_connections;
                    }
                }
            }
        }

        spin_unlock_irqrestore( &driver_params.spin_lock, cpu_flags );

        /*
         * We have a port and an IOC we can log into
         */
        if ( lowest_port ) {

            target->ioc = lowest_ioc_entry;
            target->port = lowest_port;

            initialize_connection( target );
        } else {
            TS_REPORT_WARN( MOD_SRPTP, "Found ioc %p, but no local port",
                            lowest_ioc_entry );
        }

    } else {
        spin_unlock_irqrestore( &driver_params.spin_lock, cpu_flags );

        TS_REPORT_STAGE( MOD_SRPTP, "Target %d, no paths available", target->target_index );

        spin_lock_irqsave( &target->spin_lock, cpu_flags );
        target->timeout = jiffies + TARGET_NO_PATHS_TIMEOUT;
        target->state = TARGET_NO_PATHS;
        spin_unlock_irqrestore( &target->spin_lock, cpu_flags );
    }
}


void srp_port_query_cancel( srp_host_port_params_t *port )
{
	struct list_head *temp_entry, *query_entry;
    srp_query_entry_t *query;
    tTS_IB_DM_CLIENT_HOST_TID dm_xid = TS_IB_DM_CLIENT_HOST_TID_INVALID;

    down( &driver_params.sema );

    list_for_each_safe( query_entry, temp_entry, &driver_params.query_list ) {

        query = list_entry( query_entry, srp_query_entry_t, list );

        if ( query->port == port ) {
            int status = 0;

            TS_REPORT_STAGE( MOD_SRPTP, "Cancelling port query on hca %d, port %d",
                             port->hca->hca_index +1, port->local_port );

            list_del( &query->list );

            up( &driver_params.sema );
            switch( query->state ) {
                case QUERY_PATH_RECORD_LOOKUP:
                    status = tsIbClientQueryCancel( query->xid );
                    if ( status ) {
                        TS_REPORT_WARN( MOD_SRPTP, "Query %d, cancel failed %d",
                                        query->id, status );
                    }
                    break;

                case QUERY_PORT_INFO:
                    tsIbHostIoQueryCancel( query->dm_xid );
                    break;

                default:
                    TS_REPORT_FATAL( MOD_SRPTP, "Unknown query type" );
                    break;
            }
            down( &driver_params.sema );

            kfree( query );
        }
    }
    
    if ( port->dm_query_in_progress ) {

        port->dm_query_in_progress = FALSE;
        port->dm_need_query = FALSE;
                
        TS_REPORT_STAGE( MOD_SRP, "Canceling DM Query on hca %d port %d", 
                         port->hca->hca_index+1, port->local_port );

        dm_xid = port->dm_xid;
    }
            
    up( &driver_params.sema ); 
    
    if ( dm_xid != TS_IB_DM_CLIENT_HOST_TID_INVALID ) { 
        tsIbHostIoQueryCancel( dm_xid );
	}         
}
    

int srp_find_query_id( int id, srp_query_entry_t **query_entry )
{
	struct list_head *temp_entry;

    list_for_each( temp_entry, &driver_params.query_list ) {

        *query_entry = list_entry( temp_entry, srp_query_entry_t, list );

        /*
         * If we already have a query in-progress, set the need_retry flag
         */
        if ( (*query_entry)->id == id ) {
            return( TS_SUCCESS );
        }
    }

    *query_entry = NULL;
    return( TS_FAIL );
}


int srp_find_query( srp_host_port_params_t *port,
                    uint8_t *gid )
{
    unsigned long cpu_flags;
	struct list_head *temp_entry;
    srp_query_entry_t *query;

    spin_lock_irqsave( &driver_params.spin_lock, cpu_flags );

    list_for_each( temp_entry, &driver_params.query_list ) {

        query = list_entry( temp_entry, srp_query_entry_t, list );

        /*
         * If we already have a query in-progress, set the need_retry flag
         */
        if ( ( query->port == port ) &&
             ( memcmp(query->remote_gid, gid, sizeof( tTS_IB_GID )) == 0 ) ) {

            query->need_retry = TRUE;
            spin_unlock_irqrestore( &driver_params.spin_lock, cpu_flags );
            return( TS_SUCCESS );
        }
    }

    spin_unlock_irqrestore( &driver_params.spin_lock, cpu_flags );

    return( TS_FAIL );
}


void srp_update_ioc( ioc_entry_t *ioc_entry,
                     srp_host_port_params_t *port,
    	             tTS_IB_DM_CLIENT_HOST_IO_SVC io_svc )
{
    tTS_IB_PATH_RECORD svc_path_record;

    svc_path_record = &ioc_entry->iou_path_record[port->hca->hca_index][port->local_port-1];

    memset( svc_path_record, 0, sizeof(tTS_IB_PATH_RECORD));

    /*
     * Build our service record
     */
	svc_path_record->mtu = TS_IB_MTU_1024;
	svc_path_record->slid = port->slid;
	svc_path_record->dlid = io_svc->port_lid;
    svc_path_record->pkey = 0xffff;

    memcpy( svc_path_record->sgid,
            port->local_gid,
		    sizeof(tTS_IB_GID) );

    ioc_entry->path_valid[port->hca->hca_index][port->local_port-1] = TRUE;

    TS_REPORT_STAGE( MOD_SRPTP, "Updating IOC on hca %d port %d",
                     port->hca->hca_index+1, port->local_port );

    TS_REPORT_STAGE( MOD_SRPTP,
                     " Controller GUID= %02x%02x%02x%02x%02x%02x%02x%02x",
                     io_svc->controller_guid[0], io_svc->controller_guid[1],
                     io_svc->controller_guid[2], io_svc->controller_guid[3],
                     io_svc->controller_guid[4], io_svc->controller_guid[5],
                     io_svc->controller_guid[6], io_svc->controller_guid[7]);

	TS_REPORT_STAGE( MOD_SRPTP," DLID 0x%x", io_svc->port_lid);
}


void srp_process_services_list( tTS_IB_DM_CLIENT_HOST_IO_LIST io_list,
                                srp_host_port_params_t *port )
{
	struct	list_head	   *cur;
	char				   *service_name_str;
	uint64_t			   service_id;
	uint64_t 			   service_name_cpu_endian, service_name;
	int 				   ioc_index = 0;
    int                    svc_index = 0;
    ioc_entry_t            *ioc_entry;
    int                    status;


    TS_REPORT_WARN( MOD_SRPTP,"New Service List hca %d, port %d",
                    port->hca->hca_index+1,port->local_port );

	list_for_each(cur,&io_list->io_svc_list) {

    	tTS_IB_DM_CLIENT_HOST_IO_SVC io_svc =
			list_entry(cur, tTS_IB_DM_CLIENT_HOST_IO_SVC_STRUCT, list);

		srp_target_t	*empty_target;

		svc_index++;

		service_name_str = strchr(io_svc->svc_entry.service_name, ':');
		if (service_name_str == NULL) {
			TS_REPORT_WARN( MOD_SRPTP,
                            "unexpected service name %s index %d \n",
				            io_svc->svc_entry.service_name, svc_index);
			break;
		}

		service_name_str++;
		StringToHex64(service_name_str,&service_name_cpu_endian);

		service_name = cpu_to_be64(service_name_cpu_endian);

		service_id = be64_to_cpu(*(uint64_t *)io_svc->svc_entry.service_id);
		if (service_id != ((uint64_t) (SRP_SERVICE_ID)) ) {
			TS_REPORT_WARN( MOD_SRPTP,
			                "Invalid service id 0x%llx (expected 0x%llx) from DM Client\n",
			                service_id,SRP_SERVICE_ID);
            break;
		}

        /*
         * before we initialize the target,
         * update the pathing information for the IOC
         */
        status = srp_find_ioc( io_svc->controller_guid, &ioc_index );
        if ( status == TS_FAIL ) {
            TS_REPORT_STAGE( MOD_SRPTP,
                             "IOC not found %llx, creating new IOC entry",
                             be64_to_cpu(*(uint64_t *)io_svc->controller_guid) );

            status = srp_new_ioc( io_svc->controller_guid, &ioc_index );

            if ( status == TS_FAIL ) {
                TS_REPORT_STAGE( MOD_SRPTP, "IOC entry creation failed, too many IOCs" );
                break;
            }
        }

        ioc_entry = &ioc_table[ioc_index];
        srp_update_ioc( ioc_entry,
                        port,
                        io_svc );

        /*
		 * Check if we know about this service already.
		 */
		for (empty_target = &srp_targets[0];
			 empty_target < &srp_targets[max_srp_targets];
			 empty_target++) {

			/*
			 * We use up the target structs consecutively i.e
			 * there are no holes.
			 */
            if ( empty_target->service_name == service_name )
                break;
		}

        //
        // did not find a previous match for this entry
        // in the dm_query, look for a free spot
        //
        if ( empty_target == &srp_targets[max_srp_targets] ) {
            for (empty_target = &srp_targets[0];
			    empty_target < &srp_targets[max_srp_targets];
			    empty_target++) {

			    /*
			     * We use up the target structs consecutively i.e
			     * there are no holes.
			     */
				if (empty_target->service_name == 0) {
                    empty_target->service_name = service_name;

                    TS_REPORT_STAGE( MOD_SRPTP,"svc name= %s 0x%llx",
		                             io_svc->svc_entry.service_name,
                                     be64_to_cpu(empty_target->service_name) );
				    break;
		        }
            }
        }

		if (empty_target == &srp_targets[max_srp_targets]) {
			TS_REPORT_WARN(MOD_SRPTP,"Too many svc entries -- maxed out at %d\n",
			max_srp_targets);
			break;
		}

        /*
         * If target wasn't previously discovered
         * Allocate packets and mark as in-use.
         */
        if ( empty_target->valid == FALSE ) {
            int status;

            status = srp_host_alloc_pkts( empty_target );
            if ( status ) {
                TS_REPORT_FATAL( MOD_SRPTP, "Could not allocat target %d",
                                 empty_target->target_index );
            } else {
                empty_target->valid = TRUE;
            }
        }

        /*
         * Indicate which IOCs the target/service is visible on
         */
        empty_target->ioc_mask[ioc_index] = TRUE;
        empty_target->ioc_needs_request[ioc_index] = TRUE;
    }

}


void
srp_host_dm_completion(
	int								status,
	tTS_IB_DM_CLIENT_HOST_IO_LIST	io_list,
	void							*context
	) {

	srp_host_port_params_t *port = (srp_host_port_params_t *)context;

    /*
     * If we are shuting down, throw away the query
     */
    if ( driver_params.dm_shutdown ) {
        if (( status == TS_SUCCESS ) && ( io_list )) {
            tsIbHostIoListFree(io_list);
        }
        return;
    }

    down( &driver_params.sema );
	
    if ( (status == TS_SUCCESS) && (io_list == NULL) ) {

        TS_REPORT_STAGE( MOD_SRPTP, "DM Client Query complete hca %d port %d",
                        port->hca->hca_index+1, port->local_port );

		port->dm_query_in_progress = FALSE;
        port->dm_need_query = FALSE;

        if ( port->dm_need_retry == TRUE ) {

            if ( port->dm_retry_count++ < MAX_DM_RETRIES ) {
                TS_REPORT_WARN( MOD_SRPTP, "Restarting DM Query on hca %d port %d timeout, retry count %d",
                                port->hca->hca_index+1, port->local_port, port->dm_retry_count );

                srp_dm_query( port );
            } else {
                TS_REPORT_WARN( MOD_SRPTP, "DM Client timeout on hca %d port %d, retry count %d exceeded",
                                port->hca->hca_index+1, port->local_port, port->dm_retry_count );
            }
        }

    } else if ( status == -ETIMEDOUT ) {

        TS_REPORT_WARN( MOD_SRPTP, "DM Client timeout on hca %d port %d",
                        port->hca->hca_index+1, port->local_port );
        
        port->dm_need_retry = TRUE;           

    } else if ( status ) {
        //
        // All other error cases
        //
        TS_REPORT_WARN( MOD_SRPTP,"DM Client error status %d from hca %d port %d",
			            status, port->hca->hca_index+1, port->local_port );
    } else {

        srp_process_services_list( io_list, port );
    
        tsIbHostIoListFree(io_list);
    } 

    up( &driver_params.sema );
}


void srp_host_port_query_completion(
                int status,
                tTS_IB_DM_CLIENT_HOST_IO_LIST io_list,
                void                          *context )
{
	srp_query_entry_t *query_entry;
    srp_host_port_params_t *port;
    int error;

    /*
     * If we are shuting down, throw away the query
     */
    if ( driver_params.dm_shutdown ) {
        if (( status == TS_SUCCESS ) && ( io_list )) {
            tsIbHostIoListFree(io_list);
        }
        return;
    }

    down( &driver_params.sema );

    error = srp_find_query_id( (uintptr_t)context, &query_entry );
    if ( error ) {
        TS_REPORT_WARN( MOD_SRPTP, "Cannot find query id %d status %d, list %p",
                                   context,
                                   status,
                                   io_list );
        up( &driver_params.sema );
        return;
    }

    port = query_entry->port;

    if ( (status == TS_SUCCESS) && (io_list == NULL) ) {

        TS_REPORT_STAGE( MOD_SRPTP, "Port Query %d complete hca %d port %d",
                         query_entry->id,
                         port->hca->hca_index+1,
                         port->local_port );

        if ( query_entry->need_retry == TRUE ) {

            query_entry->need_retry = FALSE;

            driver_params.port_query = TRUE;

            tsIbHostIoPortQuery( query_entry->port->hca->ca_hndl,
                                 query_entry->port->local_port,
                                 query_entry->remote_lid,
                                 PORT_QUERY_TIMEOUT,
                                 srp_host_port_query_completion,
                                 (void *)(uintptr_t)query_entry->id,
                                 &query_entry->dm_xid );

        } else {

            list_del( &query_entry->list );
            kfree( query_entry );
        }

    } else if ( status == -ETIMEDOUT ) {

        TS_REPORT_WARN( MOD_SRPTP, "Port Query %d timeout on hca %d port %d",
                        query_entry->id,
                        port->hca->hca_index+1,
                        port->local_port );

        if ( ++query_entry->retry < MAX_QUERY_RETRIES ) {
            query_entry->need_retry = TRUE;
        } else {
            TS_REPORT_WARN( MOD_SRPTP, "Retries exceeded on hca %d port %d",
                            port->hca->hca_index+1, port->local_port );
            query_entry->need_retry = FALSE;
        }
    } else if ( status ) {
        //
        // All other error cases
        //
        TS_REPORT_WARN( MOD_SRPTP,"Port Query %d error status %d from hca %d port %d",
			            query_entry->id, status, port->hca->hca_index+1, port->local_port );

    } else {

        TS_REPORT_STAGE( MOD_SRPTP,"Processing Client Query %d on hca %d, port %d",
                         query_entry->id, port->hca->hca_index+1, port->local_port );

        srp_process_services_list( io_list, port );

        tsIbHostIoListFree(io_list);
    }

    up( &driver_params.sema );
    return;
}


static tTS_IB_PATH_RECORD srp_empty_path_record_cache(void) {

	uint32_t	index;

	tTS_IB_PATH_RECORD_STRUCT	unused_path_record;

	memset(&unused_path_record, 0, sizeof(tTS_IB_PATH_RECORD_STRUCT));

	for (index = 0; index < max_path_record_cache; index++) {

		if (memcmp(&srp_path_records[index],
				   &unused_path_record,
				   sizeof(tTS_IB_PATH_RECORD_STRUCT)) == 0) {

			if ( index > last_used_path_record )
				last_used_path_record = index;

			return &srp_path_records[index];

		}

	}

	TS_REPORT_FATAL(MOD_SRPTP,"Couldnt find free path record entry");

	return NULL;
}


static void srp_flush_path_record_cache( srp_host_port_params_t *port, uint8_t *gid )
{
	uint32_t	  index;
    unsigned long cpu_flags;

    TS_REPORT_STAGE( MOD_SRPTP, "Flushing path record cache" );

    spin_lock_irqsave( &driver_params.spin_lock, cpu_flags );

    for (index = 0; index < max_path_record_cache; index++) {

		if ( memcmp( srp_path_records[index].sgid,
				     port->local_gid,
				     sizeof(tTS_IB_GID)) == 0 ) {

            if ( gid ) {
		        if ( memcmp( srp_path_records[index].dgid,
				             gid,
				             sizeof(tTS_IB_GID)) == 0 ) {
                    memset( &srp_path_records[index], 0, sizeof(tTS_IB_PATH_RECORD_STRUCT));
                    TS_REPORT_STAGE( MOD_SRPTP," Flushing path record index %d", index );
                }
		    } else {
                memset( &srp_path_records[index], 0, sizeof(tTS_IB_PATH_RECORD_STRUCT));
                TS_REPORT_STAGE( MOD_SRPTP," Flushing path record index %d", index );
            }
        }
    }

    spin_unlock_irqrestore( &driver_params.spin_lock, cpu_flags );
}


static int srp_find_path_record( tTS_IB_GID find_gid,
                                 srp_host_port_params_t *port,
                                 tTS_IB_PATH_RECORD *path_record )
{
    int index;

    for( index = 0; index <= last_used_path_record; index++ ) {

        if ( (memcmp(srp_path_records[index].dgid,
				     find_gid,
				     sizeof(tTS_IB_GID) ) == 0 ) &&
			 (srp_path_records[index].slid == port->slid ) ) {

            *path_record = &srp_path_records[index];
            return( TS_SUCCESS );
        }
    }

    return( TS_FAIL );
}


void srp_update_cache( tTS_IB_PATH_RECORD path_record, srp_host_port_params_t *port )
{
    tTS_IB_PATH_RECORD cache_path_record;
    int status;

    status = srp_find_path_record( path_record->dgid, port, &cache_path_record );

    if ( status ) {

        TS_REPORT_STAGE( MOD_SRPTP, "Updating path record cache" );

        cache_path_record = srp_empty_path_record_cache();

        if ( cache_path_record ) {
            memcpy( cache_path_record, path_record, sizeof(tTS_IB_PATH_RECORD_STRUCT) );
        }
    }
}


int srp_get_path_record( tTS_IB_GID find_gid,
                         srp_host_port_params_t *port,
                         int retry_count,
                         tTS_IB_CLIENT_QUERY_TID *tid,
                         tTS_IB_PATH_RECORD_COMPLETION_FUNC completion_function,
                         void *completion_arg )
{
    int status;
    tTS_IB_PATH_RECORD path_record;

    status = srp_find_path_record( find_gid, port, &path_record );

    if ( status == TS_SUCCESS ) {

        TS_REPORT_STAGE( MOD_SRPTP, "Found Path Record in cache" );

	    completion_function( TS_IB_CLIENT_QUERY_TID_INVALID,
			                 0, /* status */
			                 path_record,
			                 0, /* remaining */
		                     completion_arg	);

	} else {

   	    status = tsIbPathRecordRequest( port->hca->ca_hndl,
                                        port->local_port,
                                        port->local_gid,
        					            find_gid,
                                        TS_IB_SA_INVALID_PKEY,
                                        TS_IB_PATH_RECORD_ALLOW_DUPLICATE, /* flags */
                                        (retry_count+1)*2*HZ, /* timeout */
                                        (retry_count+1)*2*HZ, /* XXX cache jiffies */
                                        completion_function,
                                        completion_arg,
                                        tid );

        if ( status ) {
            TS_REPORT_WARN( MOD_SRPTP,
                            "tsIbPathRecordRequest failed %d hca %d port %d",
					        status, port->hca->hca_index+1, port->local_port );
        }
    }

    return( status );
}


int
srp_trap_path_record_completion( tTS_IB_CLIENT_QUERY_TID tid,
                                 int 		             status,
                                 tTS_IB_PATH_RECORD      path_record,
                                 int 			         remaining,
                                 void 			         *context )
{
    srp_query_entry_t *query_entry;
    srp_host_port_params_t *port;
    int error;

    down( &driver_params.sema );

    error = srp_find_query_id( (uintptr_t)context, &query_entry );
    if ( error ) {
        TS_REPORT_WARN( MOD_SRPTP, "Cannot find query id %d", context );
        up( &driver_params.sema );
        return( 0 );
    }

    port = query_entry->port;

    if ( status ) {

        if ( status == -ETIMEDOUT ) {
            if ( ++query_entry->retry < MAX_PATH_RECORD_RETRIES ) {

                TS_REPORT_WARN( MOD_SRPTP, "Query %d, Path Record timeout on hca %d port %d",
                                query_entry->id, port->hca->hca_index+1, port->local_port );

	            status = tsIbPathRecordRequest( port->hca->ca_hndl,
                                                port->local_port,
                                                port->local_gid,
							                    query_entry->remote_gid,
                                                TS_IB_SA_INVALID_PKEY,
                                                TS_IB_PATH_RECORD_ALLOW_DUPLICATE, /* flags */
                                                PATH_RECORD_TIMEOUT, /* timeout */
                                                PATH_RECORD_TIMEOUT, /* XXX cache jiffies */
                                                srp_trap_path_record_completion,
                                                (void *)(uintptr_t)query_entry->id,
                                                &query_entry->xid );

                if ( status ) {
                    TS_REPORT_WARN( MOD_SRPTP,
                                 " tsIbPathRecordRequest failed status %d for hca %d port %d",
					            status, port->hca->hca_index+1,port->local_port );

                    goto kill_query;
                }
            } else {
                TS_REPORT_WARN( MOD_SRPTP,
                                "Path Record exceeded retries status %d, killing query on hca %d port %d",
                                status, port->hca->hca_index+1, port->local_port );

                goto kill_query;
            }
        } else {
            TS_REPORT_WARN( MOD_SRPTP,
                            "Query %d, Path Record status %d, killing query on hca %d port %d",
                            query_entry->id, status, port->hca->hca_index+1, port->local_port );

            goto kill_query;
        }
    } else {

        query_entry->remote_lid = path_record->dlid;
        query_entry->retry = 0;

        query_entry->state = QUERY_PORT_INFO;

        driver_params.port_query = TRUE;

        tsIbHostIoPortQuery( port->hca->ca_hndl,
                             port->local_port,
                             path_record->dlid,
                             PORT_QUERY_TIMEOUT,
                             srp_host_port_query_completion,
                             (void *)(uintptr_t)query_entry->id,
                             &query_entry->dm_xid );
    }

    up( &driver_params.sema );
    return( 0 );

kill_query:
    list_del( &query_entry->list );
    kfree( query_entry );

    up( &driver_params.sema );
    return( 0 );
}


void srp_dm_notice_handler( tTS_IB_COMMON_ATTRIB_NOTICE notice,
                            tTS_IB_DEVICE_HANDLE dev_hndl,
                            tTS_IB_PORT local_port,
                            tTS_IB_LID io_port_lid,
                            void *arg)
{
    uint8_t *notified_port_gid = notice->detail.sm_trap.gid;
    int hca_index;
    srp_host_hca_params_t *hca;
    srp_host_port_params_t *port;
    srp_query_entry_t *query_entry;
    int id;
    tTS_IB_PATH_RECORD_STRUCT path_record;

    for( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {
        if ( hca_params[hca_index].ca_hndl == dev_hndl )
            break;
    }

    if ( hca_index == MAX_HCAS ) {
        TS_REPORT_FATAL( MOD_SRPTP, "Async event for unknown HCA 0x%x", dev_hndl );
        return;
    }

    hca = &hca_params[hca_index];
    port = &hca->port[local_port-1];

    TS_REPORT_WARN(MOD_SRPTP, "DM async notification HCA %d port %d",
                   hca_index+1, local_port );

    down( &driver_params.sema );

    /*
     * Query already outstanding, do nothing
     */
    if ( srp_find_query( port, notified_port_gid ) == TS_SUCCESS ) {
        up( &driver_params.sema );
        return;
    }

    TS_REPORT_WARN(MOD_SRPTP,
		 "New GID %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x LID %x",
         notice->detail.sm_trap.gid[0], notice->detail.sm_trap.gid[1],
         notice->detail.sm_trap.gid[2], notice->detail.sm_trap.gid[3],
         notice->detail.sm_trap.gid[4], notice->detail.sm_trap.gid[5],
         notice->detail.sm_trap.gid[6], notice->detail.sm_trap.gid[7],
         notice->detail.sm_trap.gid[8], notice->detail.sm_trap.gid[9],
         notice->detail.sm_trap.gid[10], notice->detail.sm_trap.gid[11],
         notice->detail.sm_trap.gid[12], notice->detail.sm_trap.gid[13],
         notice->detail.sm_trap.gid[14], notice->detail.sm_trap.gid[15],
         io_port_lid );

    query_entry = kmalloc (sizeof (srp_query_entry_t), GFP_KERNEL );

    memset( query_entry, 0, sizeof( srp_query_entry_t ));

    memcpy( query_entry->remote_gid, notified_port_gid, sizeof( tTS_IB_GID ) );

    query_entry->port = port;

    query_entry->state = QUERY_PATH_RECORD_LOOKUP;

    memset( &path_record, 0, sizeof( tTS_IB_PATH_RECORD_STRUCT ) );
    path_record.dlid = io_port_lid;

    list_add_tail( &query_entry->list, &driver_params.query_list );
    id = query_entry->id = driver_params.query_count++;

    up( &driver_params.sema );

    srp_trap_path_record_completion( 0,
                                     0,
                                     &path_record,
                                     0,
                                     (void *)(uintptr_t)id );

}


/*
 * In Service Notice Handler.
 */
void
srp_in_service_handler( tTS_IB_COMMON_ATTRIB_NOTICE	notice,
	                    tTS_IB_PORT	port,
	                    void	    *arg ) {

    srp_host_port_params_t *srp_port = ( srp_host_port_params_t *)arg;
	uint8_t 	           *notified_port_gid = notice->detail.sm_trap.gid;
    srp_query_entry_t      *query_entry;
    int status;

	TS_REPORT_WARN(MOD_SRPTP,
		 "New GID %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x on hca %d, port %d",
         notice->detail.sm_trap.gid[0], notice->detail.sm_trap.gid[1],
         notice->detail.sm_trap.gid[2], notice->detail.sm_trap.gid[3],
         notice->detail.sm_trap.gid[4], notice->detail.sm_trap.gid[5],
         notice->detail.sm_trap.gid[6], notice->detail.sm_trap.gid[7],
         notice->detail.sm_trap.gid[8], notice->detail.sm_trap.gid[9],
         notice->detail.sm_trap.gid[10], notice->detail.sm_trap.gid[11],
         notice->detail.sm_trap.gid[12], notice->detail.sm_trap.gid[13],
         notice->detail.sm_trap.gid[14], notice->detail.sm_trap.gid[15],
         srp_port->hca->hca_index + 1, srp_port->local_port );

    down( &driver_params.sema );

    if( srp_find_query( srp_port, notified_port_gid ) == TS_SUCCESS ) {
        up( &driver_params.sema );
        return;
    }

	query_entry = kmalloc (sizeof (srp_query_entry_t), GFP_KERNEL );

    memset( query_entry, 0, sizeof( srp_query_entry_t ));

    memcpy( query_entry->remote_gid, notified_port_gid, sizeof( tTS_IB_GID ) );

    query_entry->port = srp_port;

    query_entry->state = QUERY_PATH_RECORD_LOOKUP;

    query_entry->id = driver_params.query_count++;

    list_add_tail( &query_entry->list, &driver_params.query_list );

    status = tsIbPathRecordRequest( srp_port->hca->ca_hndl,
                                    srp_port->local_port,
                                    srp_port->local_gid,
							        query_entry->remote_gid,
                                    TS_IB_SA_INVALID_PKEY,
                                    TS_IB_PATH_RECORD_ALLOW_DUPLICATE, /* flags */
                                    PATH_RECORD_TIMEOUT, /* timeout */
                                    PATH_RECORD_TIMEOUT, /* XXX cache jiffies */
                                    srp_trap_path_record_completion,
                                    (void *)(uintptr_t)query_entry->id,
                                    &query_entry->xid );

    if ( status ) {
        TS_REPORT_WARN( MOD_SRPTP,
                        " tsIbPathRecordRequest failed status %d for hca %d port %d",
					    status, srp_port->hca->hca_index+1,srp_port->local_port );

        list_del( &query_entry->list );

        kfree( query_entry );
    }

    up( &driver_params.sema );

}


/*
 * Out Of Service Notice Handler.
 */
void
srp_out_of_service_handler( tTS_IB_COMMON_ATTRIB_NOTICE	notice,
	                        tTS_IB_PORT	local_port,
	                        void	    *arg ) {

	uint8_t 	           *notified_port_gid = notice->detail.sm_trap.gid;
    srp_host_port_params_t *port = (srp_host_port_params_t *)arg;
    srp_target_t           *target;
    uint8_t *r;

	TS_REPORT_WARN(MOD_SRPTP,
		 "Lost GID %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x on HCA %d Port %d",
         notice->detail.sm_trap.gid[0], notice->detail.sm_trap.gid[1],
         notice->detail.sm_trap.gid[2], notice->detail.sm_trap.gid[3],
         notice->detail.sm_trap.gid[4], notice->detail.sm_trap.gid[5],
         notice->detail.sm_trap.gid[6], notice->detail.sm_trap.gid[7],
         notice->detail.sm_trap.gid[8], notice->detail.sm_trap.gid[9],
         notice->detail.sm_trap.gid[10], notice->detail.sm_trap.gid[11],
         notice->detail.sm_trap.gid[12], notice->detail.sm_trap.gid[13],
         notice->detail.sm_trap.gid[14], notice->detail.sm_trap.gid[15],
         port->hca->hca_index + 1, local_port );

    srp_flush_path_record_cache( port, notified_port_gid );

	/*
	 * Right now, we get notified about any IB port going down on the IB fabric.
	 * Scan through our list of all paths for all targets and see if
	 * their redirected path matches the GID that went down.
	 */
    for ( target = &srp_targets[0];
          target < &srp_targets[max_srp_targets];
          target++ ) {

        down( &target->sema );

        if ( target->state != TARGET_ACTIVE_CONNECTION ) {
            up( &target->sema );
            continue;
        }

	    r = target->active_conn->redirected_port_gid; 

        TS_REPORT_DATA(MOD_SRPTP,
		 "match port gid= %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
         r[0],r[1],r[2],r[3],
         r[4],r[5],r[6],r[7],
         r[8],r[9],r[10],r[11],
         r[12],r[13],r[14],r[15] );

        if ( ( memcmp( target->active_conn->redirected_port_gid,
                       notified_port_gid,
					   sizeof(tTS_IB_GID)) == 0) &&
             ( target->active_conn->port == port ) )  {

            TS_REPORT_WARN( MOD_SRPTP,
                            "Target %d, disconnected...reconnecting",
                            target->target_index );

            remove_connection( target->active_conn, TARGET_POTENTIAL_CONNECTION );

            srp_move_to_pending( target );

            initialize_connection( target );
		}

        up( &target->sema );
	}
}


void srp_out_of_service_completion( tTS_IB_CLIENT_QUERY_TID transaction_id,
                                    int status,
                                    tTS_IB_COMMON_ATTRIB_INFORM inform,
                                    void *arg )
{
    srp_host_port_params_t *port = (srp_host_port_params_t *)arg;

    if ( transaction_id == port->out_of_service_xid ) {

        if ( status ) {
            /* trap failed to register */
            TS_REPORT_WARN( MOD_SRPTP, "Out of service error on hca %d port %d status %d",
                            port->local_port, port->hca->hca_index+1,status );

            if( status == -ETIMEDOUT )
                srp_register_out_of_service( port, TRUE );
            else
                TS_REPORT_WARN( MOD_SRPTP, "Unhandled error" );
        } else {
            TS_REPORT_STAGE( MOD_SRPTP, "Out of service trap for hca %d port %d complete",
                             port->hca->hca_index+1,port->local_port );
        }
    }
}


void srp_in_service_completion( tTS_IB_CLIENT_QUERY_TID transaction_id,
                                int status,
                                tTS_IB_COMMON_ATTRIB_INFORM inform,
                                void *arg )
{
    srp_host_port_params_t *port = (srp_host_port_params_t *)arg;

    if ( transaction_id == port->in_service_xid ) {

        if ( status ) {
            /* trap failed to register */
            TS_REPORT_WARN( MOD_SRPTP, "In service failed on hca %d port %d status %d",
                            port->local_port, port->hca->hca_index+1,status );

            if( status == -ETIMEDOUT )
                srp_register_in_service( port, TRUE );
            else
                TS_REPORT_WARN( MOD_SRPTP, "Unhandled error for in-service trap handler" );
        } else {
            TS_REPORT_STAGE( MOD_SRPTP, "In service trap for hca %d port %d complete",
                             port->hca->hca_index+1,port->local_port );
        }
    }
}


void srp_register_in_service( srp_host_port_params_t *port, int flag )
{
    int err_code;
    tTS_IB_GID zero_port_gid;
    tTS_IB_SA_NOTICE_HANDLER_FUNC handler;
    tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_handler;

    if ( port->valid == FALSE )
        return;

    if ( flag ) {
        handler = srp_in_service_handler;
        completion_handler = srp_in_service_completion;

		if (port->port_state != TS_IB_PORT_STATE_ACTIVE)
            return;

        TS_REPORT_STAGE( MOD_SRPTP,"Registering hca %d local port %d for IB in service traps",
            	         port->hca->hca_index+1,
                         port->local_port );

    } else {
        handler = NULL;
        completion_handler = NULL;

        TS_REPORT_STAGE( MOD_SRPTP,"Deregistering hca %d local port %d for IB in service traps",
            	        port->hca->hca_index+1,
                        port->local_port );
    }

    memset( zero_port_gid, 0, sizeof( tTS_IB_GID ));
    err_code = tsIbSetInServiceNoticeHandler( port->hca->ca_hndl,        /* hca */
			                                  port->local_port,          /* local port */
                                              zero_port_gid,             /* zero port GID */
			                                  0xffff,                    /* LID begin, get traps for all ports */
                                              0,                         /* LID end, ignored */
			                                  handler,                   /* out of service handler */
                                              port,                      /* argument */
			                                  5*HZ,                      /* timeout */
			                                  completion_handler,        /* completion function */
                                              port,                      /* completion arg */
			                                  &port->in_service_xid );

    if ( err_code )
        TS_REPORT_FATAL(MOD_SRPTP,
                        "InServiceNoticeHandler failed (%d) ", err_code);
}


void srp_register_out_of_service( srp_host_port_params_t *port, int flag )
{

    int err_code;
    tTS_IB_GID zero_port_gid;
    tTS_IB_SA_NOTICE_HANDLER_FUNC handler;
    tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_handler;

    if ( port->valid == FALSE )
        return;

    if ( flag ) {
        handler = srp_out_of_service_handler;
        completion_handler = srp_out_of_service_completion;

		if (port->port_state != TS_IB_PORT_STATE_ACTIVE)
            return;

        TS_REPORT_STAGE( MOD_SRPTP,"Registering hca %d local port %d for IB out of service traps",
            	         port->hca->hca_index+1,
                         port->local_port );

    } else {
        handler = NULL;
        completion_handler = NULL;

        TS_REPORT_STAGE( MOD_SRPTP,"Deregistering hca %d local port %d for IB out of service traps",
            	         port->hca->hca_index+1,
                         port->local_port );
    }

    memset( zero_port_gid, 0, sizeof( tTS_IB_GID ));

    err_code = tsIbSetOutofServiceNoticeHandler( port->hca->ca_hndl,        /* hca */
			                                     port->local_port,          /* local port */
                                                 zero_port_gid,             /* zero port GID */
			                                     0xffff,                    /* LID begin, get traps for all ports */
                                                 0,                         /* LID end, ignored */
			                                     handler,                   /* out of service handler */
                                                 port,                      /* argument */
			                                     5*HZ,                      /* timeout */
			                                     completion_handler,        /* completion function */
                                                 port,                      /* completion arg */
			                                     &port->out_of_service_xid );

    if ( err_code )
        TS_REPORT_FATAL(MOD_SRPTP,
                        "OutOfServiceNoticeHandler failed (%d) ", err_code);
}


int srp_dm_query( srp_host_port_params_t *port )
{
    int status = TS_FAIL;

    if ( port->port_state == TS_IB_PORT_STATE_ACTIVE ) {

        port->dm_query_in_progress = TRUE;

        port->dm_need_retry = FALSE;

        TS_REPORT_STAGE( MOD_SRPTP, "DM Query Initiated on hca %d local port %d",
                        port->hca->hca_index+1, port->local_port );

        if ( (status = tsIbHostIoQuery(
                            port->hca->ca_hndl,
	                  		port->local_port,
							20*HZ,
							srp_host_dm_completion,
							(void *)port,
							&port->dm_xid )) ){

			TS_REPORT_WARN( MOD_SRPTP,
				"tsIbHostIoQuery failed status 0x%x",
				status);

			port->dm_query_in_progress = FALSE;
		}
	}

    return( status );
}


void
srp_hca_async_event_handler( tTS_IB_ASYNC_EVENT_RECORD event,
	                         void *arg )
{
    int hca_index;
    srp_host_port_params_t *port;
    srp_host_hca_params_t *hca;

    for( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {
        if ( hca_params[hca_index].ca_hndl == event->device )
            break;
    }

    if ( hca_index == MAX_HCAS ) {
        TS_REPORT_FATAL( MOD_SRPTP, "Async event for unknown HCA %d", event->device );
        return;
    }

    hca = &hca_params[hca_index];
    port = &hca->port[event->modifier.port-1];

    switch (event->event) {

		case TS_IB_PORT_ACTIVE:
			/*
			 * Wake up that DM thread, so that it will start
			 * a fresh scan and initiate connections to discovered
			 * services
			 */
			TS_REPORT_WARN(MOD_SRPTP,
				"Port active event for hca %d port %d",
				hca_index+1, event->modifier.port );

            if ( port->valid == FALSE )
                break;

            down( &driver_params.sema );
 
            port->dm_need_query = TRUE;

            if (port->port_state != TS_IB_PORT_ACTIVE) {
                driver_params.need_refresh = TRUE;
            }

            up( &driver_params.sema );
		    break;

		case	TS_IB_LOCAL_CATASTROPHIC_ERROR :
			{
			int port_index;

			TS_REPORT_WARN(MOD_SRPTP,
				"Got \"local catastrophic error\" from the HCA %d", hca_index );
		    TS_REPORT_WARN(MOD_SRPTP, "Moving all targets to offline state" );
			/*
			 * As far as we are concerned,
			 * a local catastrophic error is equivalent to
			 * all the local ports going into error state
			 */
			for (port_index = 0; port_index < MAX_LOCAL_PORTS_PER_HCA; port_index++) {
                if ( hca->port[port_index].valid == FALSE )
                    break;

                event->event = TS_IB_PORT_ERROR;

                event->modifier.port = hca->port[port_index].local_port;

				srp_hca_async_event_handler( event, NULL );
			}
			}
		    break;

		case	TS_IB_PORT_ERROR :
			{
			tUINT32		i;
            int ioc_index;
            srp_target_t *target;
            unsigned long cpu_flags;

			TS_REPORT_WARN(MOD_SRPTP,
				"Port error event for hca %d port %d",
				hca_index+1, event->modifier.port );

			if ( port->valid == FALSE )
				break;

			/*
			 * Cannot call refresh hca info as the HCA may be hung,
			 * simply mark the port as being down
			 */
			port->port_state = TS_IB_PORT_STATE_DOWN;

            /*
             * Two step process to update the paths for the IOCs.
             * First mark down the path in the IOC table for the specific
             * hca and port combination that went down.  Then sweep through
             * the IOC table and remove IOC entries that have no paths from the
             * IOC table and the ioc_mask within the target structure.
             */
            spin_lock_irqsave( &driver_params.spin_lock, cpu_flags );
            for ( ioc_index = 0; ioc_index < MAX_IOCS; ioc_index++ ) {
                ioc_table[ioc_index].path_valid[hca->hca_index][port->local_port-1] = FALSE;
            }
            spin_unlock_irqrestore( &driver_params.spin_lock, cpu_flags );

            srp_clean_ioc_table();

            srp_flush_path_record_cache( port, NULL );

            /*
			 * We now have a updated path table...Disconnect all connections
             * for this given hca and port and try reconnecting.
			 */
			for ( i = 0; i < max_srp_targets ; i++ ) {
			
                target = &srp_targets[i];
                
                down( &target->sema );	
					
				if ( target->state == TARGET_ACTIVE_CONNECTION ) { 
                   
                    srp_host_conn_t	*conn = target->active_conn;
 
                    TS_REPORT_STAGE(MOD_SRPTP, 
						" Target %d active conn thru port %d active_count %d",
						i, conn->port->local_port,
						target->active_count );

                    memset( &conn->path_record, 0, sizeof( tTS_IB_PATH_RECORD_STRUCT));

                    remove_connection( conn, TARGET_POTENTIAL_CONNECTION );

                    srp_move_to_pending( conn->target );

                    /*
				     * Force the pending IOs to switch connections if possible
				     */
				    pick_connection_path( conn->target );
			    }

                up( &target->sema );
			}

            srp_register_out_of_service( port, FALSE );
            port->out_of_service_xid = 0;
            srp_register_in_service( port, FALSE );
            port->in_service_xid = 0;

            srp_port_query_cancel( port );

            }
		    break;

        case TS_IB_LID_CHANGE:
            break;

        case TS_IB_PKEY_CHANGE:
            break;

		default :
			TS_REPORT_FATAL( MOD_SRPTP, "Unsupported event type %d", event->event );
            break;
    }
}


int srp_dm_init()
{
    int i, async_event_index, hca_index, status;
    srp_host_hca_params_t *hca;
    tTS_IB_ASYNC_EVENT_RECORD_STRUCT async_record;

    max_path_record_cache = max_srp_targets * MAX_LOCAL_PORTS;
    /*
     * Initialize path record cache
     * Allow for one path record entry for each per target
     */
	srp_path_records = (tTS_IB_PATH_RECORD) kmalloc(
		sizeof(tTS_IB_PATH_RECORD_STRUCT)*max_path_record_cache,
		 GFP_KERNEL );

    if( srp_path_records == NULL ) {
        TS_REPORT_FATAL( MOD_SRPTP, "kmalloc failed srp_targets" );
        return -ENOMEM;
    }


    /*
     * Setup path record cache
     */
    last_used_path_record = 0;

    memset( &srp_path_records[0], 0,
		sizeof(tTS_IB_PATH_RECORD_STRUCT)*MAX_LOCAL_PORTS*max_srp_targets);

	/*
	 * Register for async events on the HCA
	 */
    for( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

        hca = &hca_params[hca_index];

        if ( hca->valid == FALSE )
            break;

	    TS_REPORT_STAGE( MOD_SRPTP,
                         "Registering async events handler for HCA %d", hca->hca_index );

        async_record.device = hca->ca_hndl;

        async_event_index = TS_IB_LOCAL_CATASTROPHIC_ERROR;
        for ( i = 0; i < MAX_ASYNC_EVENT_HANDLES; i++ ) {

            async_record.event = async_event_index;
	        status = tsIbAsyncEventHandlerRegister( &async_record,
                                                    srp_hca_async_event_handler,
                                                    hca,
			                                        &hca->async_handles[i] );

            if ( status ) {
			    TS_REPORT_FATAL(MOD_SRPTP,
				    "Registration of async event %d on hca %d failed",
                    i, hca->hca_index, status );
                return( -EINVAL );
            }

            async_event_index++;
        }
    }

    sema_init( &driver_params.sema, 1 );

    /* Register with DM to register for async notification */
    tsIbDmAsyncNotifyRegister( TS_IB_DM_NOTICE_READY_TO_TEST_TRAP_NUM,
                               srp_dm_notice_handler,
                               NULL);

    return( 0 );
}


void srp_dm_unload()
{
    srp_host_hca_params_t *hca;
    int i, hca_index;

    /*
	 * Unegister for async events on the HCA
	 */
    for ( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

        hca = &hca_params[hca_index];

        if( hca->valid == FALSE )
            break;

        TS_REPORT_STAGE( MOD_SRPTP,
                         "DeRegistering async event handlers for HCA %d", hca_index );

        /*
         * Loop through the async handles for the HCA and
         * deregister them.
         */
        for( i = 0;
             i < MAX_ASYNC_EVENT_HANDLES;
             i++ ) {

            tsIbAsyncEventHandlerDeregister( hca->async_handles[i] );

        }

    }

    /* Register with DM to register for async notification */
    tsIbDmAsyncNotifyRegister( TS_IB_DM_NOTICE_READY_TO_TEST_TRAP_NUM,
                               NULL,
                               NULL);

    kfree( srp_path_records );
}
