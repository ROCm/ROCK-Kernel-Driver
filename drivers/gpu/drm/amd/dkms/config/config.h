/* config/config.h.  Generated from config.h.in by configure.  */
/* config/config.h.in.  Generated from configure.ac by autoheader.  */

/* whether invalidate_range_start() wants 2 args */
#define HAVE_2ARGS_INVALIDATE_RANGE_START 1

/* whether invalidate_range_start() wants 5 args */
/* #undef HAVE_5ARGS_INVALIDATE_RANGE_START */

/* whether access_ok(x, x) is available */
#define HAVE_ACCESS_OK_WITH_TWO_ARGUMENTS 1

/* acpi_dev_get_first_match_dev() is available */
#define HAVE_ACPI_DEV_GET_FIRST_MATCH_DEV 1

/* acpi_put_table() is available */
#define HAVE_ACPI_PUT_TABLE 1

/* struct acpi_srat_generic_affinity is available */
#define HAVE_ACPI_SRAT_GENERIC_AFFINITY 1

/* acpi_video_backlight_use_native() is available */
#define HAVE_ACPI_VIDEO_BACKLIGHT_USE_NATIVE 1

/* acpi_video_register_backlight() is available */
#define HAVE_ACPI_VIDEO_REGISTER_BACKLIGHT 1

/* acpi_video_report_nolcd() is available */
#define HAVE_ACPI_VIDEO_REPORT_NOLCD 1

/* whether AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES is defined */
#define HAVE_AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES 1

/* *FLAGS_<basetarget>.o support to take the path relative to $(obj) */
#define HAVE_AMDKCL_FLAGS_TAKE_PATH 1

/* hmm support is enabled */
#define HAVE_AMDKCL_HMM_MIRROR_ENABLED 1

/* amd_iommu_invalidate_ctx take arg type of pasid as u32 */
#define HAVE_AMD_IOMMU_INVALIDATE_CTX_PASID_U32 1

/* amd_iommu_pc_get_max_banks() declared */
#define HAVE_AMD_IOMMU_PC_GET_MAX_BANKS_DECLARED 1

/* amd_iommu_pc_get_max_banks() arg is unsigned int */
/* #undef HAVE_AMD_IOMMU_PC_GET_MAX_BANKS_UINT */

/* amd_iommu_pc_supported() is available */
#define HAVE_AMD_IOMMU_PC_SUPPORTED 1

/* apple_gmux_detect() is available */
#define HAVE_APPLE_GMUX_DETECT 1

/* arch_io_{reserve/free}_memtype_wc() are available */
#define HAVE_ARCH_IO_RESERVE_FREE_MEMTYPE_WC 1

/* Define to 1 if you have the <asm/fpu/api.h> header file. */
#define HAVE_ASM_FPU_API_H 1

/* Define to 1 if you have the <asm/set_memory.h> header file. */
#define HAVE_ASM_SET_MEMORY_H 1

/* amdgpu_attr_group->is_bin_visible is available */
#define HAVE_ATTRIBUTE_GROUP_IS_BIN_VISIBLE 1

/* backlight_device_set_brightness() is available */
#define HAVE_BACKLIGHT_DEVICE_SET_BRIGHTNESS 1

/* bitmap_free(),bitmap_alloc(),bitmap_zalloc is available */
#define HAVE_BITMAP_FUNCS 1

/* bitmap_to_arr32() is available */
#define HAVE_BITMAP_TO_ARR32 1

/* cancel_work() is available */
#define HAVE_CANCEL_WORK 1

/* whether CHUNK_ID_SYNCOBJ_TIMELINE_WAIT_SIGNAL is defined */
#define HAVE_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT_SIGNAL 1

/* compat_ptr_ioctl() is available */
#define HAVE_COMPAT_PTR_IOCTL 1

/* debugfs_create_file_size() is available */
#define HAVE_DEBUGFS_CREATE_FILE_SIZE 1

/* kobj_type->default_groups is available */
#define HAVE_DEFAULT_GROUP_IN_KOBJ_TYPE 1

/* devcgroup_check_permission() is available */
#define HAVE_DEVCGROUP_CHECK_PERMISSION 1

/* MEMORY_DEVICE_COHERENT is availablea */
#define HAVE_DEVICE_COHERENT 1

/* dev_pagemap->owner is available */
#define HAVE_DEV_PAGEMAP_OWNER 1

/* there is 'range' field within dev_pagemap structure */
#define HAVE_DEV_PAGEMAP_RANGE 1

/* dev_pm_set_driver_flags() is available */
#define HAVE_DEV_PM_SET_DRIVER_FLAGS 1

/* dma_buf->dynamic_mapping is available */
/* #undef HAVE_DMA_BUF_OPS_DYNAMIC_MAPPING */

/* dma_buf->dynamic_mapping is not available */
/* #undef HAVE_DMA_BUF_OPS_LEGACY */

/* dma_fence_chain_alloc() is available */
#define HAVE_DMA_FENCE_CHAIN_ALLOC 1

/* dma_fence_chain_contained() is available */
#define HAVE_DMA_FENCE_CHAIN_CONTAINED 1

/* dma_fence_describe() is available */
#define HAVE_DMA_FENCE_DESCRIBE 1

/* dma_fence_is_container() is available */
#define HAVE_DMA_FENCE_IS_CONTAINER 1

/* struct dma_fence_ops has callback set_deadline */
#define HAVE_DMA_FENCE_OPS_SET_DEADLINE 1

/* struct dma_fence_ops has use_64bit_seqno field */
#define HAVE_DMA_FENCE_OPS_USE_64BIT_SEQNO 1

/* dma_map_sgtable() is enabled */
#define HAVE_DMA_MAP_SGTABLE 1

/* dma_resv->fences is available */
#define HAVE_DMA_RESV_FENCES 1

/* dma_resv->seq is available */
/* #undef HAVE_DMA_RESV_SEQ */

/* dma_resv->seq is seqcount_ww_mutex_t */
/* #undef HAVE_DMA_RESV_SEQCOUNT_WW_MUTEX_T */

/* Reporting dma_resv->seq bug */
/* #undef HAVE_DMA_RESV_SEQ_BUG */

/* down_read_killable() is available */
#define HAVE_DOWN_READ_KILLABLE 1

/* down_write_killable() is available */
#define HAVE_DOWN_WRITE_KILLABLE 1

/* drm_add_override_edid_modes() is available */
/* #undef HAVE_DRM_ADD_OVERRIDE_EDID_MODES */

/* Define to 1 if you have the <drm/amdgpu_pciid.h> header file. */
/* #undef HAVE_DRM_AMDGPU_PCIID_H */

/* Define to 1 if you have the <drm/amd_asic_type.h> header file. */
#define HAVE_DRM_AMD_ASIC_TYPE_H 1

/* drm_aperture_remove_conflicting_pci_framebuffers() second arg is
   drm_driver* */
#define HAVE_DRM_APERTURE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_DRM_DRIVER_ARG 1

/* drm_atomic_helper_calc_timestamping_constants() is available */
#define HAVE_DRM_ATOMIC_HELPER_CALC_TIMESTAMPING_CONSTANTS 1

/* drm_atomic_private_obj_init() wants 4 args */
#define HAVE_DRM_ATOMIC_PRIVATE_OBJ_INIT_4ARGS 1

/* drm_connector_atomic_hdr_metadata_equal() is available */
#define HAVE_DRM_CONNECTOR_ATOMIC_HDR_METADATA_EQUAL 1

/* drm_connector_attach_hdr_output_metadata_property() is available */
#define HAVE_DRM_CONNECTOR_ATTACH_HDR_OUTPUT_METADATA_PROPERTY 1

/* drm_connector->edid_override is available */
#define HAVE_DRM_CONNECTOR_EDID_OVERRIDE 1

/* drm_connector_for_each_possible_encoder() wants 2 arguments */
#define HAVE_DRM_CONNECTOR_FOR_EACH_POSSIBLE_ENCODER_2ARGS 1

/* atomic_best_encoder take 2nd arg type of state as struct drm_atomic_state
   */
#define HAVE_DRM_CONNECTOR_HELPER_FUNCS_ATOMIC_BEST_ENCODER_ARG_DRM_ATOMIC_STATE 1

/* drm_connector_helper_funcs->atomic_check() wants struct drm_atomic_state
   arg */
#define HAVE_DRM_CONNECTOR_HELPER_FUNCS_ATOMIC_CHECK_ARG_DRM_ATOMIC_STATE 1

/* drm_connector_helper_funcs->prepare_writeback_job is available */
#define HAVE_DRM_CONNECTOR_HELPER_FUNCS_PREPARE_WRITEBACK_JOB 1

/* drm_connector_init_with_ddc() is available */
#define HAVE_DRM_CONNECTOR_INIT_WITH_DDC 1

/* drm_connector_set_panel_orientation_with_quirk() is available */
#define HAVE_DRM_CONNECTOR_SET_PANEL_ORIENTATION_WITH_QUIRK 1

/* drm_connector_state->colorspace is available */
#define HAVE_DRM_CONNECTOR_STATE_COLORSPACE 1

/* struct drm_connector_state has hdcp_content_type member */
#define HAVE_DRM_CONNECTOR_STATE_HDCP_CONTENT_TYPE 1

/* struct drm_connector_state has hdr_output_metadata member */
#define HAVE_DRM_CONNECTOR_STATE_HDR_OUTPUT_METADATA 1

/* drm_connector_attach_colorspace_property() is available */
#define HAVE_DRM_CONNECT_ATTACH_COLORSPACE_PROPERTY 1

/* drm_crtc_helper_funcs->atomic_check()/atomic_flush()/atomic_begin() wants
   struct drm_atomic_state arg */
#define HAVE_DRM_CRTC_HELPER_FUNCS_ATOMIC_CHECK_ARG_DRM_ATOMIC_STATE 1

/* drm_crtc_helper_funcs->atomic_enable()/atomic_disable() wants struct
   drm_atomic_state arg */
#define HAVE_DRM_CRTC_HELPER_FUNCS_ATOMIC_ENABLE_ARG_DRM_ATOMIC_STATE 1

/* drm_debug_enabled() is available */
#define HAVE_DRM_DEBUG_ENABLED 1

/* drm_device->open_count is int */
/* #undef HAVE_DRM_DEVICE_OPEN_COUNT_INT */

/* struct drm_device has pdev member */
/* #undef HAVE_DRM_DEVICE_PDEV */

/* Define to 1 if you have the <drm/display/drm_dp.h> header file. */
#define HAVE_DRM_DISPLAY_DRM_DP_H 1

/* Define to 1 if you have the <drm/display/drm_dp_helper.h> header file. */
#define HAVE_DRM_DISPLAY_DRM_DP_HELPER_H 1

/* Define to 1 if you have the <drm/display/drm_dp_mst_helper.h> header file.
   */
#define HAVE_DRM_DISPLAY_DRM_DP_MST_HELPER_H 1

/* Define to 1 if you have the <drm/display/drm_dsc.h> header file. */
#define HAVE_DRM_DISPLAY_DRM_DSC_H 1

/* Define to 1 if you have the <drm/display/drm_dsc_helper.h> header file. */
#define HAVE_DRM_DISPLAY_DRM_DSC_HELPER_H 1

/* Define to 1 if you have the <drm/display/drm_hdcp.h> header file. */
#define HAVE_DRM_DISPLAY_DRM_HDCP_H 1

/* Define to 1 if you have the <drm/display/drm_hdcp_helper.h> header file. */
#define HAVE_DRM_DISPLAY_DRM_HDCP_HELPER_H 1

/* Define to 1 if you have the <drm/display/drm_hdmi_helper.h> header file. */
#define HAVE_DRM_DISPLAY_DRM_HDMI_HELPER_H 1

/* display_info->edid_hdmi_rgb444_dc_modes is available */
#define HAVE_DRM_DISPLAY_INFO_EDID_HDMI_RGB444_DC_MODES 1

/* display_info->is_hdmi is available */
#define HAVE_DRM_DISPLAY_INFO_IS_HDMI 1

/* display_info->luminance_range is available */
#define HAVE_DRM_DISPLAY_INFO_LUMINANCE_RANGE 1

/* display_info->max_dsc_bpp is available */
#define HAVE_DRM_DISPLAY_INFO_MAX_DSC_BPP 1

/* struct drm_display_info has monitor_range member */
#define HAVE_DRM_DISPLAY_INFO_MONITOR_RANGE 1

/* drm_dp_atomic_find_time_slots() is available */
#define HAVE_DRM_DP_ATOMIC_FIND_TIME_SLOTS 1

/* drm_dp_atomic_find_vcpi_slots() wants 5args */
/* #undef HAVE_DRM_DP_ATOMIC_FIND_VCPI_SLOTS_5ARGS */

/* drm_dp_atomic_release_time_slots() is available */
#define HAVE_DRM_DP_ATOMIC_RELEASE_TIME_SLOTS 1

/* drm_dp_atomic_release_vcpi_slots() with drm_dp_mst_port argument is
   available */
/* #undef HAVE_DRM_DP_ATOMIC_RELEASE_VCPI_SLOTS_MST_PORT */

/* drm_dp_mst_atomic_setup_commit() is available */
#define HAVE_DRM_DP_ATOMIC_SETUP_COMMIT 1

/* drm_dp_mst_atomic_wait_for_dependencies() is available */
#define HAVE_DRM_DP_ATOMIC_WAIT_FOR_DEPENDENCIES 1

/* struct drm_dp_aux has member named 'drm_dev' */
#define HAVE_DRM_DP_AUX_DRM_DEV 1

/* drm_dp_calc_pbn_mode() wants 3args */
#define HAVE_DRM_DP_CALC_PBN_MODE_3ARGS 1

/* drm_dp_cec_register_connector() wants p,p interface */
#define HAVE_DRM_DP_CEC_REGISTER_CONNECTOR_PP 1

/* Define to 1 if you have the <drm/dp/drm_dp_helper.h> header file. */
/* #undef HAVE_DRM_DP_DRM_DP_HELPER_H */

/* Define to 1 if you have the <drm/dp/drm_dp_mst_helper.h> header file. */
/* #undef HAVE_DRM_DP_DRM_DP_MST_HELPER_H */

/* drm_dp_link_train_channel_eq_delay() has 2 args */
#define HAVE_DRM_DP_LINK_TRAIN_CHANNEL_EQ_DELAY_2ARGS 1

/* drm_dp_link_train_clock_recovery_delay() has 2 args */
#define HAVE_DRM_DP_LINK_TRAIN_CLOCK_RECOVERY_DELAY_2ARGS 1

/* drm_dp_mst_add_affected_dsc_crtcs() is available */
#define HAVE_DRM_DP_MST_ADD_AFFECTED_DSC_CRTCS 1

/* drm_dp_mst_atomic_check() is available */
#define HAVE_DRM_DP_MST_ATOMIC_CHECK 1

/* drm_dp_mst_atomic_enable_dsc() is available */
#define HAVE_DRM_DP_MST_ATOMIC_ENABLE_DSC 1

/* drm_dp_mst_atomic_enable_dsc() wants 5args */
/* #undef HAVE_DRM_DP_MST_ATOMIC_ENABLE_DSC_WITH_5_ARGS */

/* drm_dp_mst_connector_early_unregister() is available */
#define HAVE_DRM_DP_MST_CONNECTOR_EARLY_UNREGISTER 1

/* drm_dp_mst_connector_late_register() is available */
#define HAVE_DRM_DP_MST_CONNECTOR_LATE_REGISTER 1

/* drm_dp_mst_detect_port() wants p,p,p,p args */
#define HAVE_DRM_DP_MST_DETECT_PORT_PPPP 1

/* drm_dp_mst_dsc_aux_for_port() is available */
#define HAVE_DRM_DP_MST_DSC_AUX_FOR_PORT 1

/* drm_dp_mst_{get,put}_port_malloc() is available */
#define HAVE_DRM_DP_MST_GET_PUT_PORT_MALLOC 1

/* drm_dp_mst_hpd_irq_handle_event() is available */
#define HAVE_DRM_DP_MST_HPD_IRQ_HANDLE_EVENT 1

/* drm_dp_mst_port struct has full_pbn member */
#define HAVE_DRM_DP_MST_PORT_FULL_PBN 1

/* struct drm_dp_mst_port has passthrough_aux member */
#define HAVE_DRM_DP_MST_PORT_PASSTHROUGH_AUX 1

/* drm_dp_mst_root_conn_atomic_check() is available */
#define HAVE_DRM_DP_MST_ROOT_CONN_ATOMIC_CHECK 1

/* struct drm_dp_mst_topology_cbs->destroy_connector is available */
/* #undef HAVE_DRM_DP_MST_TOPOLOGY_CBS_DESTROY_CONNECTOR */

/* struct drm_dp_mst_topology_cbs has hotplug member */
/* #undef HAVE_DRM_DP_MST_TOPOLOGY_CBS_HOTPLUG */

/* struct drm_dp_mst_topology_cbs->poll_hpd_irq is available */
#define HAVE_DRM_DP_MST_TOPOLOGY_CBS_POLL_HPD_IRQ 1

/* struct drm_dp_mst_topology_cbs->register_connector is available */
/* #undef HAVE_DRM_DP_MST_TOPOLOGY_CBS_REGISTER_CONNECTOR */

/* struct drm_dp_mst_topology_mgr.base is available */
#define HAVE_DRM_DP_MST_TOPOLOGY_MGR_BASE 1

/* drm_dp_mst_topology_mgr_init() has max_lane_count and max_link_rate */
/* #undef HAVE_DRM_DP_MST_TOPOLOGY_MGR_INIT_MAX_LANE_COUNT */

/* drm_dp_mst_topology_mgr_resume() wants 2 args */
#define HAVE_DRM_DP_MST_TOPOLOGY_MGR_RESUME_2ARGS 1

/* struct drm_dp_mst_topology_state has member payloads */
#define HAVE_DRM_DP_MST_TOPOLOGY_STATE_PAYLOADS 1

/* struct drm_dp_mst_topology_state has member pbn_div */
#define HAVE_DRM_DP_MST_TOPOLOGY_STATE_PBN_DIV 1

/* struct drm_dp_mst_topology_state has member total_avail_slots */
#define HAVE_DRM_DP_MST_TOPOLOGY_STATE_TOTAL_AVAIL_SLOTS 1

/* drm_dp_read_dpcd_caps() is available */
#define HAVE_DRM_DP_READ_DPCD_CAPS 1

/* drm_dp_remove_payload_part{1,2}() is available */
#define HAVE_DRM_DP_REMOVE_RAYLOAD_PART 1

/* drm_dp_send_real_edid_checksum() is available */
#define HAVE_DRM_DP_SEND_REAL_EDID_CHECKSUM 1

/* drm_dp_update_payload_part1() function has start_slot argument */
/* #undef HAVE_DRM_DP_UPDATE_PAYLOAD_PART1_START_SLOT_ARG */

/* drm_driver->gem_prime_res_obj() is available */
/* #undef HAVE_DRM_DRIVER_GEM_PRIME_RES_OBJ */

/* Define to 1 if you have the <drm/drmP.h> header file. */
/* #undef HAVE_DRM_DRMP_H */

/* Define to 1 if you have the <drm/drm_aperture.h> header file. */
#define HAVE_DRM_DRM_APERTURE_H 1

/* Define to 1 if you have the <drm/drm_backport.h> header file. */
/* #undef HAVE_DRM_DRM_BACKPORT_H */

/* Define to 1 if you have the <drm/drm_fbdev_generic.h> header file. */
#define HAVE_DRM_DRM_FBDEV_GENERIC_H 1

/* Define to 1 if you have the <drm/drm_gem_atomic_helper.h> header file. */
#define HAVE_DRM_DRM_GEM_ATOMIC_HELPER_H 1

/* Define to 1 if you have the <drm/drm_managed.h> header file. */
#define HAVE_DRM_DRM_MANAGED_H 1

/* Define to 1 if you have the <drm/drm_probe_helper.h> header file. */
#define HAVE_DRM_DRM_PROBE_HELPER_H 1

/* Define to 1 if you have the <drm/drm_suballoc.h> header file. */
#define HAVE_DRM_DRM_SUBALLOC_H 1

/* drm_driver_feature DRIVER_IRQ_SHARED is available */
/* #undef HAVE_DRM_DRV_DRIVER_IRQ_SHARED */

/* drm_driver_feature DRIVER_PRIME is available */
/* #undef HAVE_DRM_DRV_DRIVER_PRIME */

/* drm_driver_feature DRIVER_SYNCOBJ_TIMELINE is available */
#define HAVE_DRM_DRV_DRIVER_SYNCOBJ_TIMELINE 1

/* drm_gem_prime_export() with p,i arg is available */
#define HAVE_DRM_DRV_GEM_PRIME_EXPORT_PI 1

/* drm_dsc_compute_rc_parameters() is available */
#define HAVE_DRM_DSC_COMPUTE_RC_PARAMETERS 1

/* struct drm_dsc_config has member simple_422 */
#define HAVE_DRM_DSC_CONFIG_SIMPLE_422 1

/* drm_dsc_pps_payload_pack() is available */
#define HAVE_DRM_DSC_PPS_PAYLOAD_PACK 1

/* drm_edid_override_connector_update() is available */
#define HAVE_DRM_EDID_OVERRIDE_CONNECTOR_UPDATE 1

/* drm_fb_helper_fill_info() is available */
#define HAVE_DRM_FB_HELPER_FILL_INFO 1

/* drm_fb_helper_init() has 2 args */
#define HAVE_DRM_FB_HELPER_INIT_2ARGS 1

/* drm_fb_helper_init() has 3 args */
/* #undef HAVE_DRM_FB_HELPER_INIT_3ARGS */

/* drm_firmware_drivers_only() is available */
#define HAVE_DRM_FIRMWARE_DRIVERS_ONLY 1

/* drm_format_info.block_w and rm_format_info.block_h is available */
#define HAVE_DRM_FORMAT_INFO_MODIFIER_SUPPORTED 1

/* drm_gem_object_funcs->vmap() has 2 args */
#define HAVE_DRM_GEM_OBJECT_FUNCS_VMAP_2ARGS 1

/* drm_gem_object_funcs.vmap hsa iosys_map arg */
#define HAVE_DRM_GEM_OBJECT_FUNCS_VMAP_HAS_IOSYS_MAP_ARG 1

/* drm_gem_object_put() is available */
#define HAVE_DRM_GEM_OBJECT_PUT 1

/* drm_gem_object_put() is exported */
/* #undef HAVE_DRM_GEM_OBJECT_PUT_SYMBOL */

/* drm_gem_object->resv/_resv is available */
#define HAVE_DRM_GEM_OBJECT_RESV 1

/* drm_gem_plane_helper_prepare_fb() is available */
#define HAVE_DRM_GEM_PLANE_HELPER_PREPARE_FB 1

/* drm_hdcp_update_content_protection is available */
#define HAVE_DRM_HDCP_UPDATE_CONTENT_PROTECTION 1

/* drm_hdmi_avi_infoframe_from_display_mode() has p,p,b interface */
/* #undef HAVE_DRM_HDMI_AVI_INFOFRAME_FROM_DISPLAY_MODE_P_P_B */

/* drm_hdmi_avi_infoframe_from_display_mode() has p,p,p interface */
#define HAVE_DRM_HDMI_AVI_INFOFRAME_FROM_DISPLAY_MODE_P_P_P 1

/* drm_helper_force_disable_all() is available */
#define HAVE_DRM_HELPER_FORCE_DISABLE_ALL 1

/* drm_kms_helper_connector_hotplug_event() function is available */
#define HAVE_DRM_KMS_HELPER_CONNECTOR_HOTPLUG_EVENT 1

/* drm_legacy_irq_uninstall() is available */
#define HAVE_DRM_LEGACY_IRQ_UNINSTALL 1

/* drm_memcpy_from_wc() is availablea */
/* #undef HAVE_DRM_MEMCPY_FROM_WC */

/* drm_memcpy_from_wc() is availablea and has struct iosys_map* arg */
#define HAVE_DRM_MEMCPY_FROM_WC_IOSYS_MAP_ARG 1

/* drm_mode_config->dp_subconnector_property is available */
#define HAVE_DRM_MODE_CONFIG_DP_SUBCONNECTOR_PROPERTY 1

/* drm_mode_config->fb_base is available */
/* #undef HAVE_DRM_MODE_CONFIG_FB_BASE */

/* drm_mode_config->fb_modifiers_not_supported is available */
#define HAVE_DRM_MODE_CONFIG_FB_MODIFIERS_NOT_SUPPORTED 1

/* drm_mode_create_dp_colorspace_property() has 2 args */
#define HAVE_DRM_MODE_CREATE_DP_COLORSPACE_PROPERTY_2ARGS 1

/* drm_mode_create_hdmi_colorspace_property() has 2 args */
#define HAVE_DRM_MODE_CREATE_HDMI_COLORSPACE_PROPERTY_2ARGS 1

/* drm_mode_init() is available */
#define HAVE_DRM_MODE_INIT 1

/* drm_need_swiotlb() is availablea */
#define HAVE_DRM_NEED_SWIOTLB 1

/* drm_plane_helper_destroy() is available */
#define HAVE_DRM_PLANE_HELPER_DESTROY 1

/* drm_prime_pages_to_sg() wants 3 arguments */
#define HAVE_DRM_PRIME_PAGES_TO_SG_3ARGS 1

/* drm_prime_sg_to_dma_addr_array() is available */
#define HAVE_DRM_PRIME_SG_TO_DMA_ADDR_ARRAY 1

/* drm_print_bits() is available */
#define HAVE_DRM_PRINT_BITS 1

/* drm_print_bits() has 4 args */
#define HAVE_DRM_PRINT_BITS_4ARGS 1

/* drm_simple_encoder is available */
#define HAVE_DRM_SIMPLE_ENCODER_INIT 1

/* Has function drm_suballoc_manager_init() */
#define HAVE_DRM_SUBALLOC_MANAGER_INIT 1

/* Define to 1 if you have the <drm/task_barrier.h> header file. */
#define HAVE_DRM_TASK_BARRIER_H 1

/* struct drm_vma_offset_node has readonly field */
/* #undef HAVE_DRM_VMA_OFFSET_NODE_READONLY_FIELD */

/* fault_flag_allow_retry_first() is available */
#define HAVE_FAULT_FLAG_ALLOW_RETRY_FIRST 1

/* fsleep() is available */
#define HAVE_FSLEEP 1

/* fs_reclaim_acquire() is available */
#define HAVE_FS_RECLAIM_ACQUIRE 1

/* generic_handle_domain_irq() is available */
#define HAVE_GENERIC_HANDLE_DOMAIN_IRQ 1

/* get_user_pages() wants 6 args */
/* #undef HAVE_GET_USER_PAGES_6ARGS */

/* get_user_pages() wants gup_flags parameter */
#define HAVE_GET_USER_PAGES_GUP_FLAGS 1

/* get_user_pages_remote() wants gup_flags parameter */
/* #undef HAVE_GET_USER_PAGES_REMOTE_GUP_FLAGS */

/* get_user_pages_remote() is introduced with initial prototype */
/* #undef HAVE_GET_USER_PAGES_REMOTE_INTRODUCED */

/* get_user_pages_remote() wants locked parameter */
/* #undef HAVE_GET_USER_PAGES_REMOTE_LOCKED */

/* get_user_pages_remote() remove task_struct pointer */
#define HAVE_GET_USER_PAGES_REMOTE_REMOVE_TASK_STRUCT 1

/* drm_connector_hdr_sink_metadata() is available */
#define HAVE_HDR_SINK_METADATA 1

/* hmm remove the customizable pfn format */
#define HAVE_HMM_DROP_CUSTOMIZABLE_PFN_FORMAT 1

/* hmm_range_fault() wants 1 arg */
#define HAVE_HMM_RANGE_FAULT_1ARG 1

/* hypervisor_is_type() is available */
#define HAVE_HYPERVISOR_IS_TYPE 1

/* struct i2c_lock_operations is defined */
#define HAVE_I2C_LOCK_OPERATIONS_STRUCT 1

/* i2c_new_client_device() is enabled */
#define HAVE_I2C_NEW_CLIENT_DEVICE 1

/* idr_init_base() is available */
#define HAVE_IDR_INIT_BASE 1

/* idr_remove return void pointer */
#define HAVE_IDR_REMOVE_RETURN_VOID_POINTER 1

/* in_compat_syscall is defined */
#define HAVE_IN_COMPAT_SYSCALL 1

/* io_mapping_map_local_wc() is available */
#define HAVE_IO_MAPPING_MAP_LOCAL_WC 1

/* io_mapping_unmap_local() is available */
#define HAVE_IO_MAPPING_UNMAP_LOCAL 1

/* is_cow_mapping() is available */
#define HAVE_IS_COW_MAPPING 1

/* jiffies64_to_msecs() is available */
#define HAVE_JIFFIES64_TO_MSECS 1

/* kallsyms_lookup_name is available */
/* #undef HAVE_KALLSYMS_LOOKUP_NAME */

/* close_fd() is available */
#define HAVE_KERNEL_CLOSE_FD 1

/* kernel_write() take arg type of position as pointer */
#define HAVE_KERNEL_WRITE_PPOS 1

/* kmalloc_size_roundup is available */
#define HAVE_KMALLOC_SIZE_ROUNDUP 1

/* kmap_local_* is available */
#define HAVE_KMAP_LOCAL 1

/* krealloc_array() is available */
#define HAVE_KREALLOC_ARRAY 1

/* ksys_fd() is available */
/* #undef HAVE_KSYS_CLOSE_FD */

/* ksys_sync_helper() is available */
#define HAVE_KSYS_SYNC_HELPER 1

/* kthread_{use,unuse}_mm() is available */
#define HAVE_KTHREAD_USE_MM 1

/* ktime_get_boottime_ns() is available */
#define HAVE_KTIME_GET_BOOTTIME_NS 1

/* ktime_get_ns is available */
#define HAVE_KTIME_GET_NS 1

/* ktime_get_raw_ns is available */
#define HAVE_KTIME_GET_RAW_NS 1

/* ktime_t is union */
/* #undef HAVE_KTIME_IS_UNION */

/* Define to 1 if you have the <linux/apple-gmux.h> header file. */
#define HAVE_LINUX_APPLE_GMUX_H 1

/* Define to 1 if you have the <linux/bits.h> header file. */
#define HAVE_LINUX_BITS_H 1

/* Define to 1 if you have the <linux/build_bug.h> header file. */
#define HAVE_LINUX_BUILD_BUG_H 1

/* Define to 1 if you have the <linux/cc_platform.h> header file. */
#define HAVE_LINUX_CC_PLATFORM_H 1

/* Define to 1 if you have the <linux/compiler_attributes.h> header file. */
#define HAVE_LINUX_COMPILER_ATTRIBUTES_H 1

/* Define to 1 if you have the <linux/container_of.h> header file. */
#define HAVE_LINUX_CONTAINER_OF_H 1

/* Define to 1 if you have the <linux/dma-attrs.h> header file. */
/* #undef HAVE_LINUX_DMA_ATTRS_H */

/* Define to 1 if you have the <linux/dma-buf-map.h> header file. */
/* #undef HAVE_LINUX_DMA_BUF_MAP_H */

/* Define to 1 if you have the <linux/dma-fence-chain.h> header file. */
#define HAVE_LINUX_DMA_FENCE_CHAIN_H 1

/* Define to 1 if you have the <linux/dma-map-ops.h> header file. */
#define HAVE_LINUX_DMA_MAP_OPS_H 1

/* Define to 1 if you have the <linux/dma-resv.h> header file. */
#define HAVE_LINUX_DMA_RESV_H 1

/* Define to 1 if you have the <linux/fence-array.h> header file. */
/* #undef HAVE_LINUX_FENCE_ARRAY_H */

/* Define to 1 if you have the <linux/iosys-map.h> header file. */
#define HAVE_LINUX_IOSYS_MAP_H 1

/* Define to 1 if you have the <linux/io-64-nonatomic-lo-hi.h> header file. */
#define HAVE_LINUX_IO_64_NONATOMIC_LO_HI_H 1

/* Define to 1 if you have the <linux/mmap_lock.h> header file. */
#define HAVE_LINUX_MMAP_LOCK_H 1

/* Define to 1 if you have the <linux/pci-p2pdma.h> header file. */
#define HAVE_LINUX_PCI_P2PDMA_H 1

/* Define to 1 if you have the <linux/pgtable.h> header file. */
#define HAVE_LINUX_PGTABLE_H 1

/* Define to 1 if you have the <linux/processor.h> header file. */
#define HAVE_LINUX_PROCESSOR_H 1

/* Define to 1 if you have the <linux/stdarg.h> header file. */
#define HAVE_LINUX_STDARG_H 1

/* Define to 1 if you have the <linux/xarray.h> header file. */
#define HAVE_LINUX_XARRAY_H 1

/* list_is_first() is available */
#define HAVE_LIST_IS_FIRST 1

/* list_rotate_to_front() is available */
#define HAVE_LIST_ROTATE_TO_FRONT 1

/* enum MCE_PRIO_UC is available */
#define HAVE_MCE_PRIO_UC 1

/* memalloc_nofs_{save,restore}() are available */
#define HAVE_MEMALLOC_NOFS_SAVE 1

/* memalloc_noreclaim_save() is available */
#define HAVE_MEMALLOC_NORECLAIM_SAVE 1

/* migrate_disable() is available */
#define HAVE_MIGRATE_DISABLE 1

/* struct migrate_vma has fault_page */
#define HAVE_MIGRATE_VMA_FAULT_PAGE 1

/* migrate_vma->pgmap_owner is available */
#define HAVE_MIGRATE_VMA_PGMAP_OWNER 1

/* mmap_assert_write_locked() is available */
#define HAVE_MMAP_ASSERT_WRITE_LOCKED 1

/* mmput_async() is available */
#define HAVE_MMPUT_ASYNC 1

/* mmu_notifier_call_srcu() is available */
/* #undef HAVE_MMU_NOTIFIER_CALL_SRCU */

/* mmu_notifier_put() is available */
#define HAVE_MMU_NOTIFIER_PUT 1

/* mmu_notifier_range_blockable() is available */
#define HAVE_MMU_NOTIFIER_RANGE_BLOCKABLE 1

/* mmu_notifier_synchronize() is available */
#define HAVE_MMU_NOTIFIER_SYNCHRONIZE 1

/* release_pages() wants 2 args */
#define HAVE_MM_RELEASE_PAGES_2ARGS 1

/* pcie_aspm_enabled() is available */
#define HAVE_PCIE_ASPM_ENABLED 1

/* PCI driver handles extended tags */
#define HAVE_PCI_CONFIGURE_EXTENDED_TAGS 1

/* pci_dev_id() is available */
#define HAVE_PCI_DEV_ID 1

/* strurct pci_dev->ltr_path is available */
#define HAVE_PCI_DEV_LTR_PATH 1

/* struct pci_driver has field dev_groups */
#define HAVE_PCI_DRIVER_DEV_GROUPS 1

/* pci_is_thunderbolt_attached() is available */
#define HAVE_PCI_IS_THUNDERBOLD_ATTACHED 1

/* pci_pr3_present() is available */
#define HAVE_PCI_PR3_PRESENT 1

/* pci_rebar_bytes_to_size() is available */
#define HAVE_PCI_REBAR_BYTES_TO_SIZE 1

/* pci_upstream_bridge() is available */
#define HAVE_PCI_UPSTREAM_BRIDGE 1

/* pfn_t is defined */
#define HAVE_PFN_T 1

/* vm_insert_mixed() wants pfn_t arg */
/* #undef HAVE_PFN_T_VM_INSERT_MIXED */

/* PIDTYPE is availablea */
#define HAVE_PIDTYPE_TGID 1

/* pm_suspend_target_state is available */
#define HAVE_PM_SUSPEND_TARGET_STATE 1

/* pm_suspend_via_firmware() is available */
#define HAVE_PM_SUSPEND_VIA_FIRMWARE 1

/* pxm_to_node() is available */
#define HAVE_PXM_TO_NODE 1

/* rb_add_cached is available */
#define HAVE_RB_ADD_CACHED 1

/* whether register_shrinker(x, x) is available */
#define HAVE_REGISTER_SHRINKER_WITH_TWO_ARGUMENTS 1

/* remove_conflicting_pci_framebuffers() wants p,p args */
/* #undef HAVE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PP */

/* sched_set_fifo_low() is available */
#define HAVE_SCHED_SET_FIFO_LOW 1

/* seq_hex_dump() is available */
#define HAVE_SEQ_HEX_DUMP 1

/* smca_get_bank_type(x) is available */
/* #undef HAVE_SMCA_GET_BANK_TYPE_WITH_ONE_ARGUMENT */

/* whether smca_get_bank_type(x, x) is available */
#define HAVE_SMCA_GET_BANK_TYPE_WITH_TWO_ARGUMENTS 1

/* is_smca_umc_v2() is available */
/* #undef HAVE_SMCA_UMC_V2 */

/* struct dma_buf_attach_ops->allow_peer2peer is available */
#define HAVE_STRUCT_DMA_BUF_ATTACH_OPS_ALLOW_PEER2PEER 1

/* struct dma_buf_ops->pin() is available */
#define HAVE_STRUCT_DMA_BUF_OPS_PIN 1

/* struct dma_fence_chain is available */
#define HAVE_STRUCT_DMA_FENCE_CHAIN 1

/* struct drm_connector_state->duplicated is available */
#define HAVE_STRUCT_DRM_ATOMIC_STATE_DUPLICATED 1

/* struct drm_connector_state->colorspace is available */
#define HAVE_STRUCT_DRM_CONNECTOR_STATE_COLORSPACE 1

/* struct drm_connector_state->self_refresh_aware is available */
#define HAVE_STRUCT_DRM_CONNECTOR_STATE_SELF_REFRESH_AWARE 1

/* HAVE_STRUCT_DRM_CRTC_FUNCS_GAMMA_SET_OPTIONAL is available */
#define HAVE_STRUCT_DRM_CRTC_FUNCS_GAMMA_SET_OPTIONAL 1

/* struct drm_crtc_funcs->get_vblank_timestamp() is available */
#define HAVE_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP 1

/* struct drm_crtc_state->async_flip is available */
#define HAVE_STRUCT_DRM_CRTC_STATE_ASYNC_FLIP 1

/* drm_gem_open_object is defined in struct drm_drv */
/* #undef HAVE_STRUCT_DRM_DRV_GEM_OPEN_OBJECT_CALLBACK */

/* drm_plane_helper_funcs->atomic_check() second param wants drm_atomic_state
   arg */
#define HAVE_STRUCT_DRM_PLANE_HELPER_FUNCS_ATOMIC_CHECK_DRM_ATOMIC_STATE_PARAMS 1

/* drm_plane_helper_funcs->prepare_fb() wants const p arg */
/* #undef HAVE_STRUCT_DRM_PLANE_HELPER_FUNCS_PREPARE_FB_CONST */

/* drm_plane_helper_funcs->prepare_fb() wants p,p arg */
#define HAVE_STRUCT_DRM_PLANE_HELPER_FUNCS_PREPARE_FB_PP 1

/* ide->idr_base is available */
#define HAVE_STRUCT_IDE_IDR_BASE 1

/* struct smca_bank is available */
/* #undef HAVE_STRUCT_SMCA_BANK */

/* struct xarray is available */
#define HAVE_STRUCT_XARRAY 1

/* zone->managed_pages is available */
/* #undef HAVE_STRUCT_ZONE_MANAGED_PAGES */

/* str_yes_no() is defined */
#define HAVE_STR_YES_NO 1

/* synchronize_shrinkers() is available */
#define HAVE_SYNCHRONIZE_SHRINKERS 1

/* sysfs_emit() and sysfs_emit_at() are available */
#define HAVE_SYSFS_EMIT 1

/* totalram_pages() is available */
#define HAVE_TOTALRAM_PAGES 1

/* interval_tree_insert have struct rb_root_cached */
#define HAVE_TREE_INSERT_HAVE_RB_ROOT_CACHED 1

/* __poll_t is available */
#define HAVE_TYPE__POLL_T 1

/* vga_client_register() don't pass a cookie */
#define HAVE_VGA_CLIENT_REGISTER_NOT_PASS_COOKIE 1

/* vga_remove_vgacon() is available */
#define HAVE_VGA_REMOVE_VGACON 1

/* vga_switcheroo_set_dynamic_switch() exist */
/* #undef HAVE_VGA_SWITCHEROO_SET_DYNAMIC_SWITCH */

/* vma_lookup() is available */
#define HAVE_VMA_LOOKUP 1

/* vmf_insert_*() are available */
#define HAVE_VMF_INSERT 1

/* vmf_insert_mixed_prot() is available */
#define HAVE_VMF_INSERT_MIXED_PROT 1

/* vmf_insert_pfn_prot() is available */
#define HAVE_VMF_INSERT_PFN_PROT 1

/* vm_fault->{address/vma} is available */
#define HAVE_VM_FAULT_ADDRESS_VMA 1

/* vm_flags_{set, clear} is available */
#define HAVE_VM_FLAGS_SET 1

/* vm_insert_pfn_prot() is available */
/* #undef HAVE_VM_INSERT_PFN_PROT */

/* vm_operations_struct->fault() wants 1 arg */
#define HAVE_VM_OPERATIONS_STRUCT_FAULT_1ARG 1

/* wait_queue_entry_t exists */
#define HAVE_WAIT_QUEUE_ENTRY 1

/* want_init_on_free() is available */
#define HAVE_WANT_INIT_ON_FREE 1

/* ww_mutex_trylock() has context arg */
#define HAVE_WW_MUTEX_TRYLOCK_CONTEXT_ARG 1

/* enum x86_hypervisor_type is available */
#define HAVE_X86_HYPERVISOR_TYPE 1

/* zone_device_page_init() is available */
#define HAVE_ZONE_DEVICE_PAGE_INIT 1

/* zone_managed_pages() is available */
#define HAVE_ZONE_MANAGED_PAGES 1

/* __dma_fence_is_later() is available and has 2 args */
/* #undef HAVE__DMA_FENCE_IS_LATER_2ARGS */

/* __dma_fence_is_later() is available and has ops arg */
#define HAVE__DMA_FENCE_IS_LATER_WITH_OPS_ARG 1

/* __drm_atomic_helper_crtc_reset() is available */
#define HAVE___DRM_ATOMIC_HELPER_CRTC_RESET 1

/* __kthread_should_park() is available */
#define HAVE___KTHREAD_SHOULD_PARK 1

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME "amdgpu-dkms"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "amdgpu-dkms 6.3.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "amdgpu-dkms"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "6.3.0"

#include "config-amd-chips.h"

#define AMDGPU_VERSION PACKAGE_VERSION
