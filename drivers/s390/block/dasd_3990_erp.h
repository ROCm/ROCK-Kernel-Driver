ccw_req_t *dasd_3990_erp_com_rej (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_int_req (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_bus_out (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_equip_check (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_data_check (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_overrun (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_inv_format (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_EOC (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_env_data (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_no_rec (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_file_prot (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_perm (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_first_log (ccw_req_t *, devstat_t *);
ccw_req_t *dasd_3990_erp_add_erp (ccw_req_t *, devstat_t *);	/* tbd - delete */

ccw_req_t *dasd_3990_erp_action (ccw_req_t *);
ccw_req_t *dasd_2105_erp_action (ccw_req_t *);
