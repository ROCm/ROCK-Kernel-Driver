#ifndef BIOS_DCE110_H
#define BIOS_DCE110_H
void dce110_set_scratch_critical_state(struct dc_context *ctx,
				       bool state);
void dce110_set_scratch_acc_mode_change(struct dc_context *ctx);
#endif
