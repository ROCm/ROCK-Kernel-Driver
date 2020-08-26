/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __SMU_INTERNAL_H__
#define __SMU_INTERNAL_H__

#include "amdgpu_smu.h"

#define smu_ppt_funcs(intf, ret, smu, args...) \
	((smu)->ppt_funcs ? ((smu)->ppt_funcs->intf ? (smu)->ppt_funcs->intf(smu, ##args) : ret) : -EINVAL)

#define smu_init_microcode(smu)						smu_ppt_funcs(init_microcode, 0, smu)
#define smu_fini_microcode(smu)						smu_ppt_funcs(fini_microcode, 0, smu)
#define smu_init_smc_tables(smu)					smu_ppt_funcs(init_smc_tables, 0, smu)
#define smu_fini_smc_tables(smu)					smu_ppt_funcs(fini_smc_tables, 0, smu)
#define smu_init_power(smu)						smu_ppt_funcs(init_power,	0, smu)
#define smu_fini_power(smu)						smu_ppt_funcs(fini_power, 0, smu)
#define smu_setup_pptable(smu)						smu_ppt_funcs(setup_pptable, 0, smu)
#define smu_powergate_sdma(smu, gate)					smu_ppt_funcs(powergate_sdma, 0, smu, gate)
#define smu_get_vbios_bootup_values(smu)				smu_ppt_funcs(get_vbios_bootup_values, 0, smu)
#define smu_check_fw_version(smu)					smu_ppt_funcs(check_fw_version, 0, smu)
#define smu_write_pptable(smu)						smu_ppt_funcs(write_pptable, 0, smu)
#define smu_set_min_dcef_deep_sleep(smu, clk)				smu_ppt_funcs(set_min_dcef_deep_sleep, 0, smu, clk)
#define smu_set_driver_table_location(smu)				smu_ppt_funcs(set_driver_table_location, 0, smu)
#define smu_set_tool_table_location(smu)				smu_ppt_funcs(set_tool_table_location, 0, smu)
#define smu_notify_memory_pool_location(smu)				smu_ppt_funcs(notify_memory_pool_location, 0, smu)
#define smu_gfx_off_control(smu, enable)				smu_ppt_funcs(gfx_off_control, 0, smu, enable)
#define smu_set_last_dcef_min_deep_sleep_clk(smu)			smu_ppt_funcs(set_last_dcef_min_deep_sleep_clk, 0, smu)
#define smu_system_features_control(smu, en)				smu_ppt_funcs(system_features_control, 0, smu, en)
#define smu_init_max_sustainable_clocks(smu)				smu_ppt_funcs(init_max_sustainable_clocks, 0, smu)
#define smu_set_default_od_settings(smu)				smu_ppt_funcs(set_default_od_settings, 0, smu)
#define smu_send_smc_msg_with_param(smu, msg, param, read_arg)		smu_ppt_funcs(send_smc_msg_with_param, 0, smu, msg, param, read_arg)
#define smu_send_smc_msg(smu, msg, read_arg)				smu_ppt_funcs(send_smc_msg_with_param, 0, smu, msg, 0, read_arg)
#define smu_alloc_dpm_context(smu)					smu_ppt_funcs(alloc_dpm_context, 0, smu)
#define smu_init_display_count(smu, count)				smu_ppt_funcs(init_display_count, 0, smu, count)
#define smu_feature_set_allowed_mask(smu)				smu_ppt_funcs(set_allowed_mask, 0, smu)
#define smu_feature_get_enabled_mask(smu, mask, num)			smu_ppt_funcs(get_enabled_mask, 0, smu, mask, num)
#define smu_is_dpm_running(smu)						smu_ppt_funcs(is_dpm_running, 0 , smu)
#define smu_notify_display_change(smu)					smu_ppt_funcs(notify_display_change, 0, smu)
#define smu_set_default_dpm_table(smu)					smu_ppt_funcs(set_default_dpm_table, 0, smu)
#define smu_populate_umd_state_clk(smu)					smu_ppt_funcs(populate_umd_state_clk, 0, smu)
#define smu_set_default_od8_settings(smu)				smu_ppt_funcs(set_default_od8_settings, 0, smu)
#define smu_tables_init(smu, tab)					smu_ppt_funcs(tables_init, 0, smu, tab)
#define smu_enable_thermal_alert(smu)					smu_ppt_funcs(enable_thermal_alert, 0, smu)
#define smu_disable_thermal_alert(smu)					smu_ppt_funcs(disable_thermal_alert, 0, smu)
#define smu_smc_read_sensor(smu, sensor, data, size)			smu_ppt_funcs(read_sensor, -EINVAL, smu, sensor, data, size)
#define smu_pre_display_config_changed(smu)				smu_ppt_funcs(pre_display_config_changed, 0, smu)
#define smu_display_config_changed(smu)					smu_ppt_funcs(display_config_changed, 0 , smu)
#define smu_apply_clocks_adjust_rules(smu)				smu_ppt_funcs(apply_clocks_adjust_rules, 0, smu)
#define smu_notify_smc_display_config(smu)				smu_ppt_funcs(notify_smc_display_config, 0, smu)
#define smu_set_cpu_power_state(smu)					smu_ppt_funcs(set_cpu_power_state, 0, smu)
#define smu_msg_get_index(smu, msg)					smu_ppt_funcs(get_smu_msg_index, -EINVAL, smu, msg)
#define smu_clk_get_index(smu, clk)					smu_ppt_funcs(get_smu_clk_index, -EINVAL, smu, clk)
#define smu_feature_get_index(smu, fea)					smu_ppt_funcs(get_smu_feature_index, -EINVAL, smu, fea)
#define smu_table_get_index(smu, tab)					smu_ppt_funcs(get_smu_table_index, -EINVAL, smu, tab)
#define smu_power_get_index(smu, src)					smu_ppt_funcs(get_smu_power_index, -EINVAL, smu, src)
#define smu_workload_get_type(smu, type)				smu_ppt_funcs(get_workload_type, -EINVAL, smu, type)
#define smu_run_btc(smu)						smu_ppt_funcs(run_btc, 0, smu)
#define smu_get_allowed_feature_mask(smu, feature_mask, num)		smu_ppt_funcs(get_allowed_feature_mask, 0, smu, feature_mask, num)
#define smu_store_cc6_data(smu, st, cc6_dis, pst_dis, pst_sw_dis)	smu_ppt_funcs(store_cc6_data, 0, smu, st, cc6_dis, pst_dis, pst_sw_dis)
#define smu_get_dal_power_level(smu, clocks)				smu_ppt_funcs(get_dal_power_level, 0, smu, clocks)
#define smu_get_perf_level(smu, designation, level)			smu_ppt_funcs(get_perf_level, 0, smu, designation, level)
#define smu_get_current_shallow_sleep_clocks(smu, clocks)		smu_ppt_funcs(get_current_shallow_sleep_clocks, 0, smu, clocks)
#define smu_dpm_set_vcn_enable(smu, enable)				smu_ppt_funcs(dpm_set_vcn_enable, 0, smu, enable)
#define smu_dpm_set_jpeg_enable(smu, enable)				smu_ppt_funcs(dpm_set_jpeg_enable, 0, smu, enable)
#define smu_set_watermarks_table(smu, tab, clock_ranges)		smu_ppt_funcs(set_watermarks_table, 0, smu, tab, clock_ranges)
#define smu_thermal_temperature_range_update(smu, range, rw)		smu_ppt_funcs(thermal_temperature_range_update, 0, smu, range, rw)
#define smu_register_irq_handler(smu)					smu_ppt_funcs(register_irq_handler, 0, smu)
#define smu_get_dpm_ultimate_freq(smu, param, min, max)			smu_ppt_funcs(get_dpm_ultimate_freq, 0, smu, param, min, max)
#define smu_asic_set_performance_level(smu, level)			smu_ppt_funcs(set_performance_level, -EINVAL, smu, level)
#define smu_dump_pptable(smu)						smu_ppt_funcs(dump_pptable, 0, smu)
#define smu_update_pcie_parameters(smu, pcie_gen_cap, pcie_width_cap)	smu_ppt_funcs(update_pcie_parameters, 0, smu, pcie_gen_cap, pcie_width_cap)
#define smu_disable_umc_cdr_12gbps_workaround(smu)			smu_ppt_funcs(disable_umc_cdr_12gbps_workaround, 0, smu)
#define smu_set_power_source(smu, power_src)				smu_ppt_funcs(set_power_source, 0, smu, power_src)
#define smu_i2c_eeprom_init(smu, control)				smu_ppt_funcs(i2c_eeprom_init, 0, smu, control)
#define smu_i2c_eeprom_fini(smu, control)				smu_ppt_funcs(i2c_eeprom_fini, 0, smu, control)
#define smu_get_unique_id(smu)						smu_ppt_funcs(get_unique_id, 0, smu)
#define smu_log_thermal_throttling(smu)					smu_ppt_funcs(log_thermal_throttling_event, 0, smu)
#define smu_get_asic_power_limits(smu)					smu_ppt_funcs(get_power_limit, 0, smu)

#endif
