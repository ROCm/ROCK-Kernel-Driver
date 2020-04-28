/* config/config.h.  Generated from config.h.in by configure.  */
/* config/config.h.in.  Generated from configure.ac by autoheader.  */

/* whether invalidate_range_start() wants 2 args */
#define HAVE_2ARGS_INVALIDATE_RANGE_START 1

/* crtc->funcs->set_crc_source() wants 2 args */
#define HAVE_2ARGS_SET_CRC_SOURCE 1

/* get_user_pages() wants 5 args */
#define HAVE_5ARGS_GET_USER_PAGES 1

/* whether invalidate_range_start() wants 5 args */
/* #undef HAVE_5ARGS_INVALIDATE_RANGE_START */

/* get_user_pages() wants 6 args */
/* #undef HAVE_6ARGS_GET_USER_PAGES */

/* get_user_pages_remote() wants 7 args */
/* #undef HAVE_7ARGS_GET_USER_PAGES_REMOTE */

/* get_user_pages() wants 8 args */
/* #undef HAVE_8ARGS_GET_USER_PAGES */

/* get_user_pages_remote() wants 8 args */
#define HAVE_8ARGS_GET_USER_PAGES_REMOTE 1

/* whether access_ok(x, x) is available */
#define HAVE_ACCESS_OK_WITH_TWO_ARGUMENTS 1

/* alloc_ordered_workqueue() is available */
#define HAVE_ALLOC_ORDERED_WORKQUEUE 1

/* whether AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES is defined */
#define HAVE_AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES 1

/* amd_iommu_pc_supported() is available */
#define HAVE_AMD_IOMMU_PC_SUPPORTED 1

/* arch_io_{reserve/free}_memtype_wc() are available */
#define HAVE_ARCH_IO_RESERVE_FREE_MEMTYPE_WC 1

/* asm/fpu/api.h is available */
#define HAVE_ASM_FPU_API_H 1

/* attribute_group->bin_attrs is available */
#define HAVE_ATTRIBUTE_GROUP_BIN_ATTRS 1

/* backlight_device_register() with 5 args is available */
#define HAVE_BACKLIGHT_DEVICE_REGISTER_WITH_5ARGS 1

/* backlight_properties->type is available */
#define HAVE_BACKLIGHT_PROPERTIES_TYPE 1

/* whether CHUNK_ID_SYNCOBJ_TIMELINE_WAIT_SIGNAL is defined */
#define HAVE_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT_SIGNAL 1

/* whether CHUNK_ID_SYNOBJ_IN_OUT is defined */
#define HAVE_CHUNK_ID_SYNOBJ_IN_OUT 1

/* compat_ptr_ioctl() is available */
#define HAVE_COMPAT_PTR_IOCTL 1

/* devcgroup_check_permission() is available */
#define HAVE_DEVCGROUP_CHECK_PERMISSION 1

/* devm_memremap_pages() wants struct dev_pagemap */
#define HAVE_DEVM_MEMREMAP_PAGES_DEV_PAGEMAP 1

/* devm_memremap_pages() wants p,p,p,p interface */
/* #undef HAVE_DEVM_MEMREMAP_PAGES_P_P_P_P */

/* dev_pm_set_driver_flags() is available */
#define HAVE_DEV_PM_SET_DRIVER_FLAGS 1

/* dma_buf dynamic_mapping is available */
/* #undef HAVE_DMA_BUF_OPS_DYNAMIC_MAPPING */

/* whether dma_fence_get_stub exits */
#define HAVE_DMA_FENCE_GET_STUB 1

/* dma_fence_set_error() is available */
#define HAVE_DMA_FENCE_SET_ERROR 1

/* linux/dma-resv.h is available */
#define HAVE_DMA_RESV_H 1

/* dma_resv->seq is available */
#define HAVE_DMA_RESV_SEQ 1

/* down_write_killable() is available */
#define HAVE_DOWN_WRITE_KILLABLE 1

/* drm_dp_mst_connector_early_unregister() is available */
#define HAVE_DP_MST_CONNECTOR_EARLY_UNREGISTER 1

/* drm_dp_mst_connector_late_register() is available */
#define HAVE_DP_MST_CONNECTOR_LATE_REGISTER 1

/* drm_accurate_vblank_count() is available */
/* #undef HAVE_DRM_ACCURATE_VBLANK_COUNT */

/* DRM_AMDGPU_FENCE_TO_HANDLE is defined */
#define HAVE_DRM_AMDGPU_FENCE_TO_HANDLE 1

/* drm_atomic_get_old_crtc_state() and drm_atomic_get_new_crtc_state() are
   available */
#define HAVE_DRM_ATOMIC_GET_CRTC_STATE 1

/* drm_atomic_get_new_plane_state() is available */
#define HAVE_DRM_ATOMIC_GET_NEW_PLANE_STATE 1

/* __drm_atomic_helper_connector_reset() is available */
#define HAVE_DRM_ATOMIC_HELPER_CONNECTOR_RESET 1

/* drm_atomic_helper_disable_all() is available */
#define HAVE_DRM_ATOMIC_HELPER_DISABLE_ALL 1

/* drm_atomic_helper_duplicate_state() is available */
#define HAVE_DRM_ATOMIC_HELPER_DUPLICATE_STATE 1

/* drm_atomic_helper_shutdown() is available */
#define HAVE_DRM_ATOMIC_HELPER_SHUTDOWN 1

/* drm_atomic_helper_suspend() is available */
#define HAVE_DRM_ATOMIC_HELPER_SUSPEND_RESUME 1

/* drm_atomic_helper_update_legacy_modeset_state() is available */
#define HAVE_DRM_ATOMIC_HELPER_UPDATE_LEGACY_MODESET_STATE 1

/* drm_atomic_nonblocking_commit() is available */
#define HAVE_DRM_ATOMIC_NONBLOCKING_COMMIT 1

/* drm_atomic_private_obj_init() has p,p,p,p interface */
#define HAVE_DRM_ATOMIC_PRIVATE_OBJ_INIT_P_P_P_P 1

/* whether struct drm_atomic_state have async_update */
#define HAVE_DRM_ATOMIC_STATE_ASYNC_UPDATE 1

/* drm_atomic_state->plane_states is available */
/* #undef HAVE_DRM_ATOMIC_STATE_PLANE_STATES */

/* drm_atomic_state_put() is available */
#define HAVE_DRM_ATOMIC_STATE_PUT 1

/* drm/drm_atomic_uapi.h is available */
#define HAVE_DRM_ATOMIC_UAPI_HEADER 1

/* whether drm/drm_audio_component.h is defined */
#define HAVE_DRM_AUDIO_COMPONENT_HEADER 1

/* drm/drm_auth.h is available */
#define HAVE_DRM_AUTH_H 1

/* drm_calc_vbltimestamp_from_scanoutpos() drop mode arg */
/* #undef HAVE_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS_DROP_MOD_ARG */

/* drm_calc_vbltimestamp_from_scanoutpos() have the crtc & mode arg */
/* #undef HAVE_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS_HAVE_CRTC_MODE_ARG */

/* drm_calc_vbltimestamp_from_scanoutpos() remove crtc arg */
/* #undef HAVE_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS_HAVE_MODE_ARG */

/* drm_calc_vbltimestamp_from_scanoutpos() use ktime_t arg */
/* #undef HAVE_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS_USE_KTIMER_T_ARG */

/* drm_calloc_large() is available */
/* #undef HAVE_DRM_CALLOC_LARGE */

/* drm_color_lut structure is defined */
#define HAVE_DRM_COLOR_LUT 1

/* drm_color_lut_size() is available */
#define HAVE_DRM_COLOR_LUT_SIZE 1

/* drm_connector_attach_encoder() is available */
#define HAVE_DRM_CONNECTOR_ATTACH_ENCODER 1

/* drm_connector_for_each_possible_encoder() wants 2 arguments */
#define HAVE_DRM_CONNECTOR_FOR_EACH_POSSIBLE_ENCODER_2ARGS 1

/* struct drm_connector_funcs has register members */
#define HAVE_DRM_CONNECTOR_FUNCS_REGISTER 1

/* drm/drm_connector.h is available */
#define HAVE_DRM_CONNECTOR_H 1

/* drm_connector_helper_funcs->atomic_check() wants struct drm_atomic_state
   arg */
#define HAVE_DRM_CONNECTOR_HELPER_FUNCS_ATOMIC_CHECK_ARG_DRM_ATOMIC_STATE 1

/* drm_connector_init_with_ddc() is available */
#define HAVE_DRM_CONNECTOR_INIT_WITH_DDC 1

/* drm_connector_list_iter_begin() is available */
#define HAVE_DRM_CONNECTOR_LIST_ITER_BEGIN 1

/* drm_connector_put() is available */
#define HAVE_DRM_CONNECTOR_PUT 1

/* drm_connector_set_path_property() is available */
#define HAVE_DRM_CONNECTOR_SET_PATH_PROPERTY 1

/* drm_connector_update_edid_property() is available */
#define HAVE_DRM_CONNECTOR_UPDATE_EDID_PROPERTY 1

/* ddrm_atomic_stat has __drm_crtcs_state */
/* #undef HAVE_DRM_CRTCS_STATE_MEMBER */

/* drm_crtc_accurate_vblank_count() is available */
#define HAVE_DRM_CRTC_ACCURATE_VBLANK_COUNT 1

/* drm_crtc_force_disable_all() is available */
/* #undef HAVE_DRM_CRTC_FORCE_DISABLE_ALL */

/* drm_crtc_from_index() is available */
#define HAVE_DRM_CRTC_FROM_INDEX 1

/* drm_crtc_init_with_planes() wants name */
#define HAVE_DRM_CRTC_INIT_WITH_PLANES_VALID_WITH_NAME 1

/* drm/drm_debugfs.h is available */
#define HAVE_DRM_DEBUGFS_H 1

/* drm_debug_enabled() is available */
#define HAVE_DRM_DEBUG_ENABLED 1

/* drm_debug_printer() function is available */
#define HAVE_DRM_DEBUG_PRINTER 1

/* dev_device->driver_features is available */
#define HAVE_DRM_DEVICE_DRIVER_FEATURES 1

/* drm_device->filelist_mutex is available */
#define HAVE_DRM_DEVICE_FILELIST_MUTEX 1

/* drm/drm_device.h is available */
#define HAVE_DRM_DEVICE_H 1

/* drm_device->open_count is int */
/* #undef HAVE_DRM_DEVICE_OPEN_COUNT_INT */

/* drm_dev_put() is available */
#define HAVE_DRM_DEV_PUT 1

/* drm_dev_unplug() is available */
/* #undef HAVE_DRM_DEV_UNPLUG */

/* display_info->hdmi.scdc.scrambling are available */
#define HAVE_DRM_DISPLAY_INFO_HDMI_SCDC_SCRAMBLING 1

/* drm_dp_atomic_find_vcpi_slots() is available */
#define HAVE_DRM_DP_ATOMIC_FIND_VCPI_SLOTS 1

/* drm_dp_atomic_find_vcpi_slots() wants 5args */
#define HAVE_DRM_DP_ATOMIC_FIND_VCPI_SLOTS_5ARGS 1

/* drm_dp_calc_pbn_mode() wants 3args */
#define HAVE_DRM_DP_CALC_PBN_MODE_3ARGS 1

/* drm_dp_cec* correlation functions are available */
#define HAVE_DRM_DP_CEC_CORRELATION_FUNCTIONS 1

/* drm_dp_cec_register_connector() wants p,p interface */
#define HAVE_DRM_DP_CEC_REGISTER_CONNECTOR_PP 1

/* drm_dp_mst_add_affected_dsc_crtcs() is available */
#define HAVE_DRM_DP_MST_ADD_AFFECTED_DSC_CRTCS 1

/* drm_dp_mst_allocate_vcpi() has p,p,i,i interface */
#define HAVE_DRM_DP_MST_ALLOCATE_VCPI_P_P_I_I 1

/* drm_dp_mst_atomic_check() is available */
#define HAVE_DRM_DP_MST_ATOMIC_CHECK 1

/* drm_dp_mst_atomic_enable_dsc() is available */
#define HAVE_DRM_DP_MST_ATOMIC_ENABLE_DSC 1

/* drm_dp_mst_detect_port() wants p,p,p,p args */
#define HAVE_DRM_DP_MST_DETECT_PORT_PPPP 1

/* drm_dp_mst_dsc_aux_for_port() is available */
#define HAVE_DRM_DP_MST_DSC_AUX_FOR_PORT 1

/* drm_dp_mst_{get,put}_port_malloc() is available */
#define HAVE_DRM_DP_MST_GET_PUT_PORT_MALLOC 1

/* struct drm_dp_mst_topology_cbs has hotplug member */
/* #undef HAVE_DRM_DP_MST_TOPOLOGY_CBS_HOTPLUG */

/* struct drm_dp_mst_topology_cbs->register_connector is available */
/* #undef HAVE_DRM_DP_MST_TOPOLOGY_CBS_REGISTER_CONNECTOR */

/* drm_dp_mst_topology_mgr_resume() wants 2 args */
#define HAVE_DRM_DP_MST_TOPOLOGY_MGR_RESUME_2ARGS 1

/* drm_driver->gem_prime_res_obj() is available */
/* #undef HAVE_DRM_DRIVER_GEM_PRIME_RES_OBJ */

/* Define to 1 if you have the <drm/drmP.h> header file. */
/* #undef HAVE_DRM_DRMP_H */

/* Define to 1 if you have the <drm/drm_drv.h> header file. */
#define HAVE_DRM_DRM_DRV_H 1

/* drm_driver_feature DRIVER_ATOMIC is available */
#define HAVE_DRM_DRV_DRIVER_ATOMIC 1

/* drm_driver_feature DRIVER_IRQ_SHARED is available */
/* #undef HAVE_DRM_DRV_DRIVER_IRQ_SHARED */

/* drm_driver_feature DRIVER_PRIME is available */
/* #undef HAVE_DRM_DRV_DRIVER_PRIME */

/* drm_driver_feature DRIVER_SYNCOBJ_TIMELINE is available */
#define HAVE_DRM_DRV_DRIVER_SYNCOBJ_TIMELINE 1

/* drm_driver->gem_prime_export with p,i arg is available */
#define HAVE_DRM_DRV_GEM_PRIME_EXPORT_PI 1

/* drm_edid_to_eld() are available */
/* #undef HAVE_DRM_EDID_TO_ELD */

/* drm_encoder_find() wants file_priv */
#define HAVE_DRM_ENCODER_FIND_VALID_WITH_FILE 1

/* drm/drm_encoder.h is available */
#define HAVE_DRM_ENCODER_H 1

/* drm_encoder_init() wants name */
#define HAVE_DRM_ENCODER_INIT_VALID_WITH_NAME 1

/* drm_fb_helper_cfb_{fillrect/copyarea/imageblit}() is available */
#define HAVE_DRM_FB_HELPER_CFB_XX 1

/* drm_fb_helper_fill_info() is available */
#define HAVE_DRM_FB_HELPER_FILL_INFO 1

/* drm_fb_helper_init() has 2 args */
#define HAVE_DRM_FB_HELPER_INIT_2ARGS 1

/* drm_fb_helper_init() has 3 args */
/* #undef HAVE_DRM_FB_HELPER_INIT_3ARGS */

/* whether drm_fb_helper_lastclose() is available */
#define HAVE_DRM_FB_HELPER_LASTCLOSE 1

/* drm_fb_helper_remove_conflicting_pci_framebuffers() is available */
#define HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS 1

/* drm_fb_helper_remove_conflicting_pci_framebuffers() wants p,i,p args */
/* #undef HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PIP */

/* drm_fb_helper_remove_conflicting_pci_framebuffers() wants p,p args */
#define HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PP 1

/* drm_fb_helper_set_suspend_unlocked() is available */
#define HAVE_DRM_FB_HELPER_SET_SUSPEND_UNLOCKED 1

/* drm_fb_helper_{alloc/unregister}_fbi is available */
#define HAVE_DRM_FB_HELPER_XX_FBI 1

/* drm/drm_file.h is available */
#define HAVE_DRM_FILE_H 1

/* whether struct drm_framebuffer have format */
#define HAVE_DRM_FRAMEBUFFER_FORMAT 1

/* drm_free_large() is available */
/* #undef HAVE_DRM_FREE_LARGE */

/* drm_gem_map_attach() wants 2 arguments */
/* #undef HAVE_DRM_GEM_MAP_ATTACH_2ARGS */

/* drm_gem_object_lookup() wants 2 args */
#define HAVE_DRM_GEM_OBJECT_LOOKUP_2ARGS 1

/* drm_gem_object_put_unlocked() is available */
#define HAVE_DRM_GEM_OBJECT_PUT_UNLOCKED 1

/* ttm_buffer_object->base is available */
#define HAVE_DRM_GEM_OBJECT_RESV 1

/* drm_get_format_info() is available */
#define HAVE_DRM_GET_FORMAT_INFO 1

/* drm_get_format_name() has i,p interface */
#define HAVE_DRM_GET_FORMAT_NAME_I_P 1

/* ddrm_get_max_iome() is available */
/* #undef HAVE_DRM_GET_MAX_IOMEM */

/* drm_hdmi_avi_infoframe_from_display_mode() has p,p,b interface */
/* #undef HAVE_DRM_HDMI_AVI_INFOFRAME_FROM_DISPLAY_MODE_P_P_B */

/* drm_hdmi_avi_infoframe_from_display_mode() has p,p,p interface */
#define HAVE_DRM_HDMI_AVI_INFOFRAME_FROM_DISPLAY_MODE_P_P_P 1

/* drm_hdmi_vendor_infoframe_from_display_mode() has p,p,p interface */
#define HAVE_DRM_HDMI_VENDOR_INFOFRAME_FROM_DISPLAY_MODE_P_P_P 1

/* drm_helper_force_disable_all() is available */
#define HAVE_DRM_HELPER_FORCE_DISABLE_ALL 1

/* drm/drm_ioctl.h is available */
#define HAVE_DRM_IOCTL_H 1

/* drm/drm_irq.h is available */
#define HAVE_DRM_IRQ_H 1

/* drm_is_current_master() is available */
#define HAVE_DRM_IS_CURRENT_MASTER 1

/* drm_malloc_ab() is available */
/* #undef HAVE_DRM_MALLOC_AB */

/* whether drm_mm_insert_mode is available */
#define HAVE_DRM_MM_INSERT_MODE 1

/* drm_mm_print() is available */
#define HAVE_DRM_MM_PRINT 1

/* drm_modeset_lock_all_ctx() is available */
#define HAVE_DRM_MODESET_LOCK_ALL_CTX 1

/* drm_mode_is_420_xxx() is available */
#define HAVE_DRM_MODE_IS_420_XXX 1

/* drm_need_swiotlb() is availablea */
#define HAVE_DRM_NEED_SWIOTLB 1

/* drm/drm_plane.h is available */
#define HAVE_DRM_PLANE_H 1

/* drm/drm_print.h is available */
#define HAVE_DRM_PRINT_H 1

/* drm/drm_probe_helper.h is available */
#define HAVE_DRM_PROBE_HELPER_H 1

/* drm_send_event_locked() function is available */
#define HAVE_DRM_SEND_EVENT_LOCKED 1

/* drm_syncobj_fence_get() is available */
/* #undef HAVE_DRM_SYNCOBJ_FENCE_GET */

/* drm_syncobj_find_fence() is available */
#define HAVE_DRM_SYNCOBJ_FIND_FENCE 1

/* whether drm_syncobj_find_fence() wants 3 args */
/* #undef HAVE_DRM_SYNCOBJ_FIND_FENCE_3ARGS */

/* whether drm_syncobj_find_fence() wants 4 args */
/* #undef HAVE_DRM_SYNCOBJ_FIND_FENCE_4ARGS */

/* whether drm_syncobj_find_fence() wants 5 args */
#define HAVE_DRM_SYNCOBJ_FIND_FENCE_5ARGS 1

/* drm_universal_plane_init() wants 7 args */
/* #undef HAVE_DRM_UNIVERSAL_PLANE_INIT_7ARGS */

/* drm_universal_plane_init() wants 8 args */
/* #undef HAVE_DRM_UNIVERSAL_PLANE_INIT_8ARGS */

/* drm_universal_plane_init() wants 9 args */
#define HAVE_DRM_UNIVERSAL_PLANE_INIT_9ARGS 1

/* drm/drm_util.h is available */
#define HAVE_DRM_UTIL_H 1

/* drm/drm_vblank.h is available */
#define HAVE_DRM_VBLANK_H 1

/* drm_vma_node_verify_access() 2nd argument is drm_file */
#define HAVE_DRM_VMA_NODE_VERIFY_ACCESS_HAS_DRM_FILE 1

/* fb_info_apertures() is available */
#define HAVE_FB_INFO_APERTURES 1

/* fb_ops->fb_debug_xx is available */
#define HAVE_FB_OPS_FB_DEBUG_XX 1

/* fence_set_error() is available */
/* #undef HAVE_FENCE_SET_ERROR */

/* drm_mode_object->free_cb is available */
/* #undef HAVE_FREE_CB_IN_STRUCT_DRM_MODE_OBJECT */

/* drm_driver->gem_free_object_unlocked() is available */
#define HAVE_GEM_FREE_OBJECT_UNLOCKED_IN_DRM_DRIVER 1

/* get_scanout_position has struct drm_display_mode arg */
/* #undef HAVE_GET_SCANOUT_POSITION_HAS_DRM_DISPLAY_MODE_ARG */

/* get_scanout_position has timestamp arg */
/* #undef HAVE_GET_SCANOUT_POSITION_HAS_TIMESTAMP_ARG */

/* get_scanout_position return bool */
/* #undef HAVE_GET_SCANOUT_POSITION_RETURN_BOOL */

/* get_vblank_timestamp has bool in_vblank_irq arg */
/* #undef HAVE_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER_HAS_BOOL_IN_VBLANK_IRQ */

/* get_vblank_timestamp has ktime_t arg */
/* #undef HAVE_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER_HAS_KTIME_T */

/* get_vblank_timestamp return bool */
/* #undef HAVE_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER_RETURN_BOOL */

/* hash_for_each_xxx() drop the node parameter */
#define HAVE_HASH_FOR_EACH_XXX_DROP_NODE 1

/* drm_connector_hdr_sink_metadata() is available */
#define HAVE_HDR_SINK_METADATA 1

/* hwmon_device_register_with_groups() is available */
#define HAVE_HWMON_DEVICE_REGISTER_WITH_GROUPS 1

/* struct i2c_lock_operations is defined */
#define HAVE_I2C_LOCK_OPERATIONS_STRUCT 1

/* idr_remove return void pointer */
#define HAVE_IDR_REMOVE_RETURN_VOID_POINTER 1

/* whether INTERVAL_TREE_DEFINE() is defined */
#define HAVE_INTERVAL_TREE_DEFINE 1

/* in_compat_syscall is defined */
#define HAVE_IN_COMPAT_SYSCALL 1

/* iommu_get_domain_for_dev() is available */
#define HAVE_IOMMU_GET_DOMAIN_FOR_DEV 1

/* IRQ translation domains exist */
#define HAVE_IRQ_DOMAIN 1

/* kallsyms_lookup_name is available */
#define HAVE_KALLSYMS_LOOKUP_NAME 1

/* kfifo_new.h is available */
/* #undef HAVE_KFIFO_NEW_H */

/* kmap_atomic() have one argument */
#define HAVE_KMAP_ATOMIC_ONE_ARG 1

/* kobj_to_dev() is available */
#define HAVE_KOBJ_TO_DEV 1

/* kref_read() function is available */
#define HAVE_KREF_READ 1

/* ksys_sync_helper() is available */
#define HAVE_KSYS_SYNC_HELPER 1

/* kthread_{park/unpark/parkme/should_park}() is available */
#define HAVE_KTHREAD_PARK_XX 1

/* ktime_get_boottime_ns() is available */
#define HAVE_KTIME_GET_BOOTTIME_NS 1

/* ktime_get_ns is available */
#define HAVE_KTIME_GET_NS 1

/* ktime_get_raw_ns is available */
#define HAVE_KTIME_GET_RAW_NS 1

/* ktime_get_real_seconds() is available */
#define HAVE_KTIME_GET_REAL_SECONDS 1

/* kvcalloc() is available */
#define HAVE_KVCALLOC 1

/* kvfree() is available */
#define HAVE_KVFREE 1

/* kvmalloc_array() is available */
#define HAVE_KVMALLOC_ARRAY 1

/* kv[mz]alloc() are available */
#define HAVE_KVZALLOC_KVMALLOC 1

/* whether linux/bits.h is available */
#define HAVE_LINUX_BITS_H 1

/* Define to 1 if you have the <linux/dma-fence.h> header file. */
#define HAVE_LINUX_DMA_FENCE_H 1

/* Define to 1 if you have the <linux/fence-array.h> header file. */
/* #undef HAVE_LINUX_FENCE_ARRAY_H */

/* linux/io-64-nonatomic-lo-hi.h is available */
#define HAVE_LINUX_IO_64_NONATOMIC_LO_HI_H 1

/* linux/nospec.h is available */
#define HAVE_LINUX_NOSPEC_H 1

/* list_bulk_move_tail() is available */
#define HAVE_LIST_BULK_MOVE_TAIL 1

/* list_is_first() is available */
#define HAVE_LIST_IS_FIRST 1

/* list_rotate_to_front() is available */
#define HAVE_LIST_ROTATE_TO_FRONT 1

/* memalloc_nofs_{save,restore}() are available */
#define HAVE_MEMALLOC_NOFS_SAVE 1

/* mmgrab() is available in linux/sched.h */
#define HAVE_MMGRAB 1

/* mmu_notifier_call_srcu() is available */
/* #undef HAVE_MMU_NOTIFIER_CALL_SRCU */

/* mmu_notifier_put() is available */
#define HAVE_MMU_NOTIFIER_PUT 1

/* mmu_notifier_range_blockable() is available */
#define HAVE_MMU_NOTIFIER_RANGE_BLOCKABLE 1

/* mmu_notifier_synchronize() is available */
#define HAVE_MMU_NOTIFIER_SYNCHRONIZE 1

/* mm_access() is available */
#define HAVE_MM_ACCESS 1

/* linux/sched/mm.h is available */
#define HAVE_MM_H 1

/* release_pages() wants 2 args */
#define HAVE_MM_RELEASE_PAGES_2ARGS 1

/* num_u32_u32 is available */
#define HAVE_MUL_U32_U32 1

/* linux/overflow.h is available */
#define HAVE_OVERFLOW_H 1

/* pcie_bandwidth_available() is available */
#define HAVE_PCIE_BANDWIDTH_AVAILABLE 1

/* pci_enable_atomic_ops_to_root() exist */
#define HAVE_PCIE_ENABLE_ATOMIC_OPS_TO_ROOT 1

/* pcie_get_speed_cap() and pcie_get_width_cap() exist */
#define HAVE_PCIE_GET_SPEED_AND_WIDTH_CAP 1

/* PCI driver handles extended tags */
#define HAVE_PCI_CONFIGURE_EXTENDED_TAGS 1

/* pci_dev_id() is available */
#define HAVE_PCI_DEV_ID 1

/* pci_is_thunderbolt_attached() is available */
#define HAVE_PCI_IS_THUNDERBOLD_ATTACHED 1

/* pci_pcie_type() exist */
#define HAVE_PCI_PCIE_TYPE 1

/* pci_upstream_bridge() is available */
#define HAVE_PCI_UPSTREAM_BRIDGE 1

/* perf_event_update_userpage() is exported */
#define HAVE_PERF_EVENT_UPDATE_USERPAGE 1

/* pfn_t is defined */
#define HAVE_PFN_T 1

/* vm_insert_mixed() wants pfn_t arg */
/* #undef HAVE_PFN_T_VM_INSERT_MIXED */

/* pm_genpd_remove_device() wants 2 arguments */
/* #undef HAVE_PM_GENPD_REMOVE_DEVICE_2ARGS */

/* ptrace_parent() is available */
#define HAVE_PTRACE_PARENT 1

/* register_shrinker() returns integer */
#define HAVE_REGISTER_SHRINKER_RETURN_INT 1

/* remove_conflicting_framebuffers() returns int */
/* #undef HAVE_REMOVE_CONFLICTING_FRAMEBUFFERS_RETURNS_INT */

/* request_firmware_direct() is available */
#define HAVE_REQUEST_FIRMWARE_DIRECT 1

/* reservation_object->seq is dropped */
/* #undef HAVE_RESERVATION_OBJECT_DROP_SEQ */

/* reservation_object->staged is dropped */
/* #undef HAVE_RESERVATION_OBJECT_DROP_STAGED */

/* sched/types.h is available */
#define HAVE_SCHED_TYPES_H 1

/* seq_hex_dump() is available */
#define HAVE_SEQ_HEX_DUMP 1

/* drm_driver have set_busid */
/* #undef HAVE_SET_BUSID_IN_STRUCT_DRM_DRIVER */

/* asm/set_memory.h is available */
#define HAVE_SET_MEMORY_H 1

/* linux/sched/signal.h is available */
#define HAVE_SIGNAL_H 1

/* whether si_mem_available() is available */
#define HAVE_SI_MEM_AVAILABLE 1

/* strscpy() is available */
#define HAVE_STRSCPY 1

/* struct dma_buf_ops->pin() is available */
#define HAVE_STRUCT_DMA_BUF_OPS_PIN 1

/* struct drm_crtc_funcs->get_vblank_timestamp() is available */
#define HAVE_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP 1

/* struct drm_crtc_state->async_flip is available */
#define HAVE_STRUCT_DRM_CRTC_STATE_ASYNC_FLIP 1

/* struct mmu_notifier_mm is exported */
/* #undef HAVE_STRUCT_MMU_NOTIFIER_MM_EXPORTED */

/* struct mmu_notifier_subscriptions is available */
#define HAVE_STRUCT_MMU_NOTIFIER_SUBSCRIPTIONS 1

/* zone->managed_pages is available */
/* #undef HAVE_STRUCT_ZONE_MANAGED_PAGES */

/* system_highpri_wq is declared */
#define HAVE_SYSTEM_HIGHPRI_WQ_DECLARED 1

/* system_highpri_wq is exported */
#define HAVE_SYSTEM_HIGHPRI_WQ_EXPORTED 1

/* include/drm/task_barrier.h is available */
#define HAVE_TASK_BARRIER_H 1

/* linux/sched/task.h is available */
#define HAVE_TASK_H 1

/* timer_setup() is available */
#define HAVE_TIMER_SETUP 1

/* interval_tree_insert have struct rb_root_cached */
#define HAVE_TREE_INSERT_HAVE_RB_ROOT_CACHED 1

/* __poll_t is available */
#define HAVE_TYPE__POLL_T 1

/* vga_switcheroo_handler->get_client_id() return int */
/* #undef HAVE_VGA_SWITCHEROO_GET_CLIENT_ID_RETURN_INT */

/* enum vga_switcheroo_handler_flags_t is available */
#define HAVE_VGA_SWITCHEROO_HANDLER_FLAGS_T_ENUM 1

/* struct vga_switcheroo_client_ops is available */
#define HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_OPS 1

/* vga_switcheroo_register_client() has p,p interface */
/* #undef HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_P_P */

/* vga_switcheroo_register_client() has p,p,b interface */
#define HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_P_P_B 1

/* vga_switcheroo_register_client() has p,p,p,p interface */
/* #undef HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_P_P_P_P */

/* vga_switeroo_register_handler() p_c interface */
/* #undef HAVE_VGA_SWITCHEROO_REGISTER_HANDLER_PC */

/* vga_switeroo_register_handler() has p,e interface */
#define HAVE_VGA_SWITCHEROO_REGISTER_HANDLER_PC_E 1

/* vga_switcheroo_set_dynamic_switch() exist */
/* #undef HAVE_VGA_SWITCHEROO_SET_DYNAMIC_SWITCH */

/* get_scanout_position use unsigned int pipe */
/* #undef HAVE_VGA_USE_UNSIGNED_INT_PIPE */

/* vmf_insert_*() are available */
#define HAVE_VMF_INSERT 1

/* vmf_insert_mixed_prot() is available */
#define HAVE_VMF_INSERT_MIXED_PROT 1

/* vmf_insert_pfn_prot() is available */
#define HAVE_VMF_INSERT_PFN_PROT 1

/* vm_fault->{address/vam} is available */
#define HAVE_VM_FAULT_ADDRESS_VMA 1

/* vm_insert_pfn_prot() is available */
/* #undef HAVE_VM_INSERT_PFN_PROT */

/* vm_operations_struct->fault() wants 2 args */
/* #undef HAVE_VM_OPERATIONS_STRUCT_FAULT_2ARG */

/* wait_queue_entry_t exists */
#define HAVE_WAIT_QUEUE_ENTRY 1

/* WQ_HIGHPRI is available */
#define HAVE_WQ_HIGHPRI 1

/* zone_managed_pages() is available */
#define HAVE_ZONE_MANAGED_PAGES 1

/* __kthread_should_park() is available */
#define HAVE___KTHREAD_SHOULD_PARK 1

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME "amdgpu-dkms"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "amdgpu-dkms 19.40"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "amdgpu-dkms"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "19.40"
