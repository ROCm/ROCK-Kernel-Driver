/*
 * $Id: hid-debug.h,v 1.8 2001/09/25 09:37:57 vojtech Exp $
 *
 *  (c) 1999 Andreas Gal		<gal@cs.uni-magdeburg.de>
 *  (c) 2000-2001 Vojtech Pavlik	<vojtech@ucw.cz>
 *
 *  Some debug stuff for the HID parser.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

struct hid_usage_entry {
	unsigned  page;
	unsigned  usage;
	char     *description;
};

static const struct hid_usage_entry hid_usage_table[] = {
  {  0,      0, "Undefined" },
  {  1,      0, "GenericDesktop" },
    {0, 0x01, "Pointer"},
    {0, 0x02, "Mouse"},
    {0, 0x04, "Joystick"},
    {0, 0x05, "GamePad"},
    {0, 0x06, "Keyboard"},
    {0, 0x07, "Keypad"},
    {0, 0x08, "MultiAxis"},
      {0, 0x30, "X"},
      {0, 0x31, "Y"},
      {0, 0x32, "Z"},
      {0, 0x33, "Rx"},
      {0, 0x34, "Ry"},
      {0, 0x35, "Rz"},
      {0, 0x36, "Slider"},
      {0, 0x37, "Dial"},
      {0, 0x38, "Wheel"},
      {0, 0x39, "HatSwitch"},
    {0, 0x3a, "CountedBuffer"},
      {0, 0x3b, "ByteCount"},
      {0, 0x3c, "MotionWakeup"},
      {0, 0x3d, "Start"},
      {0, 0x3e, "Select"},
      {0, 0x40, "Vx"},
      {0, 0x41, "Vy"},
      {0, 0x42, "Vz"},
      {0, 0x43, "Vbrx"},
      {0, 0x44, "Vbry"},
      {0, 0x45, "Vbrz"},
      {0, 0x46, "Vno"},
    {0, 0x80, "SystemControl"}, 
      {0, 0x81, "SystemPowerDown"},
      {0, 0x82, "SystemSleep"},
      {0, 0x83, "SystemWakeUp"},
      {0, 0x84, "SystemContextMenu"},
      {0, 0x85, "SystemMainMenu"},
      {0, 0x86, "SystemAppMenu"},
      {0, 0x87, "SystemMenuHelp"},
      {0, 0x88, "SystemMenuExit"},
      {0, 0x89, "SystemMenuSelect"},
      {0, 0x8a, "SystemMenuRight"},
      {0, 0x8b, "SystemMenuLeft"},
      {0, 0x8c, "SystemMenuUp"},
      {0, 0x8d, "SystemMenuDown"},
    {0, 0x90, "D-padUp"},
    {0, 0x91, "D-padDown"},
    {0, 0x92, "D-padRight"},
    {0, 0x93, "D-padLeft"},
  {  7, 0, "Keyboard" },
  {  8, 0, "LED" },
  {  9, 0, "Button" },
  { 10, 0, "Ordinal" },
  { 12, 0, "Hotkey" },
  { 13, 0, "Digitizers" },
    {0, 0x01, "Digitizer"},
    {0, 0x02, "Pen"},
    {0, 0x03, "LightPen"},
    {0, 0x04, "TouchScreen"},
    {0, 0x05, "TouchPad"},
    {0, 0x20, "Stylus"},
    {0, 0x21, "Puck"},
    {0, 0x22, "Finger"},
    {0, 0x30, "TipPressure"},
    {0, 0x31, "BarrelPressure"},
    {0, 0x32, "InRange"},
    {0, 0x33, "Touch"},
    {0, 0x34, "UnTouch"},
    {0, 0x35, "Tap"},
    {0, 0x39, "TabletFunctionKey"},
    {0, 0x3a, "ProgramChangeKey"},
    {0, 0x3c, "Invert"},
    {0, 0x42, "TipSwitch"},
    {0, 0x43, "SecondaryTipSwitch"},
    {0, 0x44, "BarrelSwitch"},
    {0, 0x45, "Eraser"},
    {0, 0x46, "TabletPick"},
  { 15, 0, "PhysicalInterfaceDevice" },
    {0, 0x00, "Undefined"},
    {0, 0x01, "Physical_Interface_Device"},
      {0, 0x20, "Normal"},
    {0, 0x21, "Set_Effect_Report"},
      {0, 0x22, "Effect_Block_Index"},
      {0, 0x23, "Parameter_Block_Offset"},
      {0, 0x24, "ROM_Flag"},
      {0, 0x25, "Effect_Type"},
        {0, 0x26, "ET_Constant_Force"},
        {0, 0x27, "ET_Ramp"},
        {0, 0x28, "ET_Custom_Force_Data"},
        {0, 0x30, "ET_Square"},
        {0, 0x31, "ET_Sine"},
        {0, 0x32, "ET_Triangle"},
        {0, 0x33, "ET_Sawtooth_Up"},
        {0, 0x34, "ET_Sawtooth_Down"},
        {0, 0x40, "ET_Spring"},
        {0, 0x41, "ET_Damper"},
        {0, 0x42, "ET_Inertia"},
        {0, 0x43, "ET_Friction"},
      {0, 0x50, "Duration"},
      {0, 0x51, "Sample_Period"},
      {0, 0x52, "Gain"},
      {0, 0x53, "Trigger_Button"},
      {0, 0x54, "Trigger_Repeat_Interval"},
      {0, 0x55, "Axes_Enable"},
        {0, 0x56, "Direction_Enable"},
      {0, 0x57, "Direction"},
      {0, 0x58, "Type_Specific_Block_Offset"},
        {0, 0x59, "Block_Type"},
        {0, 0x5A, "Set_Envelope_Report"},
          {0, 0x5B, "Attack_Level"},
          {0, 0x5C, "Attack_Time"},
          {0, 0x5D, "Fade_Level"},
          {0, 0x5E, "Fade_Time"},
        {0, 0x5F, "Set_Condition_Report"},
        {0, 0x60, "CP_Offset"},
        {0, 0x61, "Positive_Coefficient"},
        {0, 0x62, "Negative_Coefficient"},
        {0, 0x63, "Positive_Saturation"},
        {0, 0x64, "Negative_Saturation"},
        {0, 0x65, "Dead_Band"},
      {0, 0x66, "Download_Force_Sample"},
      {0, 0x67, "Isoch_Custom_Force_Enable"},
      {0, 0x68, "Custom_Force_Data_Report"},
        {0, 0x69, "Custom_Force_Data"},
        {0, 0x6A, "Custom_Force_Vendor_Defined_Data"},
      {0, 0x6B, "Set_Custom_Force_Report"},
        {0, 0x6C, "Custom_Force_Data_Offset"},
        {0, 0x6D, "Sample_Count"},
      {0, 0x6E, "Set_Periodic_Report"},
        {0, 0x6F, "Offset"},
        {0, 0x70, "Magnitude"},
        {0, 0x71, "Phase"},
        {0, 0x72, "Period"},
      {0, 0x73, "Set_Constant_Force_Report"},
        {0, 0x74, "Set_Ramp_Force_Report"},
        {0, 0x75, "Ramp_Start"},
        {0, 0x76, "Ramp_End"},
      {0, 0x77, "Effect_Operation_Report"},
        {0, 0x78, "Effect_Operation"},
          {0, 0x79, "Op_Effect_Start"},
          {0, 0x7A, "Op_Effect_Start_Solo"},
          {0, 0x7B, "Op_Effect_Stop"},
          {0, 0x7C, "Loop_Count"},
      {0, 0x7D, "Device_Gain_Report"},
        {0, 0x7E, "Device_Gain"},
    {0, 0x7F, "PID_Pool_Report"},
      {0, 0x80, "RAM_Pool_Size"},
      {0, 0x81, "ROM_Pool_Size"},
      {0, 0x82, "ROM_Effect_Block_Count"},
      {0, 0x83, "Simultaneous_Effects_Max"},
      {0, 0x84, "Pool_Alignment"},
    {0, 0x85, "PID_Pool_Move_Report"},
      {0, 0x86, "Move_Source"},
      {0, 0x87, "Move_Destination"},
      {0, 0x88, "Move_Length"},
    {0, 0x89, "PID_Block_Load_Report"},
      {0, 0x8B, "Block_Load_Status"},
      {0, 0x8C, "Block_Load_Success"},
      {0, 0x8D, "Block_Load_Full"},
      {0, 0x8E, "Block_Load_Error"},
      {0, 0x8F, "Block_Handle"},
      {0, 0x90, "PID_Block_Free_Report"},
      {0, 0x91, "Type_Specific_Block_Handle"},
    {0, 0x92, "PID_State_Report"},
      {0, 0x94, "Effect_Playing"},
      {0, 0x95, "PID_Device_Control_Report"},
        {0, 0x96, "PID_Device_Control"},
        {0, 0x97, "DC_Enable_Actuators"},
        {0, 0x98, "DC_Disable_Actuators"},
        {0, 0x99, "DC_Stop_All_Effects"},
        {0, 0x9A, "DC_Device_Reset"},
        {0, 0x9B, "DC_Device_Pause"},
        {0, 0x9C, "DC_Device_Continue"},
      {0, 0x9F, "Device_Paused"},
      {0, 0xA0, "Actuators_Enabled"},
      {0, 0xA4, "Safety_Switch"},
      {0, 0xA5, "Actuator_Override_Switch"},
      {0, 0xA6, "Actuator_Power"},
    {0, 0xA7, "Start_Delay"},
    {0, 0xA8, "Parameter_Block_Size"},
    {0, 0xA9, "Device_Managed_Pool"},
    {0, 0xAA, "Shared_Parameter_Blocks"},
    {0, 0xAB, "Create_New_Effect_Report"},
    {0, 0xAC, "RAM_Pool_Available"},
  { 0x84, 0, "Power Device" },
    { 0x84, 0x02, "PresentStatus" },
    { 0x84, 0x03, "ChangeStatus" },
    { 0x84, 0x04, "UPS" },
    { 0x84, 0x05, "PowerSupply" },
    { 0x84, 0x10, "BatterySystem" },
    { 0x84, 0x11, "BatterySystemID" },
    { 0x84, 0x12, "Battery" },
    { 0x84, 0x13, "BatteryID" },
    { 0x84, 0x14, "Charger" },
    { 0x84, 0x15, "ChargerID" },
    { 0x84, 0x16, "PowerConverter" },
    { 0x84, 0x17, "PowerConverterID" },
    { 0x84, 0x18, "OutletSystem" },
    { 0x84, 0x19, "OutletSystemID" },
    { 0x84, 0x1a, "Input" },
    { 0x84, 0x1b, "InputID" },
    { 0x84, 0x1c, "Output" },
    { 0x84, 0x1d, "OutputID" },
    { 0x84, 0x1e, "Flow" },
    { 0x84, 0x1f, "FlowID" },
    { 0x84, 0x20, "Outlet" },
    { 0x84, 0x21, "OutletID" },
    { 0x84, 0x22, "Gang" },
    { 0x84, 0x24, "PowerSummary" },
    { 0x84, 0x25, "PowerSummaryID" },
    { 0x84, 0x30, "Voltage" },
    { 0x84, 0x31, "Current" },
    { 0x84, 0x32, "Frequency" },
    { 0x84, 0x33, "ApparentPower" },
    { 0x84, 0x35, "PercentLoad" },
    { 0x84, 0x40, "ConfigVoltage" },
    { 0x84, 0x41, "ConfigCurrent" },
    { 0x84, 0x43, "ConfigApparentPower" },
    { 0x84, 0x53, "LowVoltageTransfer" },
    { 0x84, 0x54, "HighVoltageTransfer" },
    { 0x84, 0x56, "DelayBeforeStartup" },
    { 0x84, 0x57, "DelayBeforeShutdown" },
    { 0x84, 0x58, "Test" },
    { 0x84, 0x5a, "AudibleAlarmControl" },
    { 0x84, 0x60, "Present" },
    { 0x84, 0x61, "Good" },
    { 0x84, 0x62, "InternalFailure" },
    { 0x84, 0x65, "Overload" },
    { 0x84, 0x66, "OverCharged" },
    { 0x84, 0x67, "OverTemperature" },
    { 0x84, 0x68, "ShutdownRequested" },
    { 0x84, 0x69, "ShutdownImminent" },
    { 0x84, 0x6b, "SwitchOn/Off" },
    { 0x84, 0x6c, "Switchable" },
    { 0x84, 0x6d, "Used" },
    { 0x84, 0x6e, "Boost" },
    { 0x84, 0x73, "CommunicationLost" },
    { 0x84, 0xfd, "iManufacturer" },
    { 0x84, 0xfe, "iProduct" },
    { 0x84, 0xff, "iSerialNumber" },
  { 0x85, 0, "Battery System" },
    { 0x85, 0x01, "SMBBatteryMode" },
    { 0x85, 0x02, "SMBBatteryStatus" },
    { 0x85, 0x03, "SMBAlarmWarning" },
    { 0x85, 0x04, "SMBChargerMode" },
    { 0x85, 0x05, "SMBChargerStatus" },
    { 0x85, 0x06, "SMBChargerSpecInfo" },
    { 0x85, 0x07, "SMBSelectorState" },
    { 0x85, 0x08, "SMBSelectorPresets" },
    { 0x85, 0x09, "SMBSelectorInfo" },
    { 0x85, 0x29, "RemainingCapacityLimit" },
    { 0x85, 0x2c, "CapacityMode" },
    { 0x85, 0x42, "BelowRemainingCapacityLimit" },
    { 0x85, 0x44, "Charging" },
    { 0x85, 0x45, "Discharging" },
    { 0x85, 0x4b, "NeedReplacement" },
    { 0x85, 0x66, "RemainingCapacity" },
    { 0x85, 0x68, "RunTimeToEmpty" },
    { 0x85, 0x6a, "AverageTimeToFull" },
    { 0x85, 0x83, "DesignCapacity" },
    { 0x85, 0x85, "ManufacturerDate" },
    { 0x85, 0x89, "iDeviceChemistry" },
    { 0x85, 0x8b, "Rechargable" },
    { 0x85, 0x8f, "iOEMInformation" },
    { 0x85, 0x8d, "CapacityGranularity1" },
    { 0x85, 0xd0, "ACPresent" },
  /* pages 0xff00 to 0xffff are vendor-specific */
  { 0xffff, 0, "Vendor-specific-FF" },
  { 0, 0, NULL }
};

static void resolv_usage_page(unsigned page) {
	const struct hid_usage_entry *p;

	for (p = hid_usage_table; p->description; p++)
		if (p->page == page) {
			printk("%s", p->description);
			return;
		}
	printk("%04x", page);
}

static void resolv_usage(unsigned usage) {
	const struct hid_usage_entry *p;

	resolv_usage_page(usage >> 16);
	printk(".");
	for (p = hid_usage_table; p->description; p++)
		if (p->page == (usage >> 16)) {
			for(++p; p->description && p->page != 0; p++)
				if (p->usage == (usage & 0xffff)) {
					printk("%s", p->description);
					return;
				}
			break;
		}
	printk("%04x", usage & 0xffff);
}

__inline__ static void tab(int n) {
	while (n--) printk(" ");
}

static void hid_dump_field(struct hid_field *field, int n) {
	int j;
	
	if (field->physical) {
		tab(n);
		printk("Physical(");
		resolv_usage(field->physical); printk(")\n");
	}
	if (field->logical) {
		tab(n);
		printk("Logical(");
		resolv_usage(field->logical); printk(")\n");
	}
	tab(n); printk("Usage(%d)\n", field->maxusage);
	for (j = 0; j < field->maxusage; j++) {
		tab(n+2);resolv_usage(field->usage[j].hid); printk("\n");
	}
	if (field->logical_minimum != field->logical_maximum) {
		tab(n); printk("Logical Minimum(%d)\n", field->logical_minimum);
		tab(n); printk("Logical Maximum(%d)\n", field->logical_maximum);
	}
	if (field->physical_minimum != field->physical_maximum) {
		tab(n); printk("Physical Minimum(%d)\n", field->physical_minimum);
		tab(n); printk("Physical Maximum(%d)\n", field->physical_maximum);
	}
	if (field->unit_exponent) {
		tab(n); printk("Unit Exponent(%d)\n", field->unit_exponent);
	}
	if (field->unit) {
		char *systems[5] = { "None", "SI Linear", "SI Rotation", "English Linear", "English Rotation" };
		char *units[5][8] = {
			{ "None", "None", "None", "None", "None", "None", "None", "None" },
			{ "None", "Centimeter", "Gram", "Seconds", "Kelvin",     "Ampere", "Candela", "None" },
			{ "None", "Radians",    "Gram", "Seconds", "Kelvin",     "Ampere", "Candela", "None" },
			{ "None", "Inch",       "Slug", "Seconds", "Fahrenheit", "Ampere", "Candela", "None" },
			{ "None", "Degrees",    "Slug", "Seconds", "Fahrenheit", "Ampere", "Candela", "None" }
		};

		int i;
		int sys;
                __u32 data = field->unit;

		/* First nibble tells us which system we're in. */
		sys = data & 0xf;
		data >>= 4;

		if(sys > 4) {
			tab(n); printk("Unit(Invalid)\n");
		}
		else {
			int earlier_unit = 0;

			tab(n); printk("Unit(%s : ", systems[sys]);

			for (i=1 ; i<sizeof(__u32)*2 ; i++) {
				char nibble = data & 0xf;
				data >>= 4;
				if (nibble != 0) {
					if(earlier_unit++ > 0)
						printk("*");
					printk("%s", units[sys][i]);
					if(nibble != 1) {
						/* This is a _signed_ nibble(!) */
	
						int val = nibble & 0x7;
						if(nibble & 0x08)
							val = -((0x7 & ~val) +1);
						printk("^%d", val);
					}
				}
			}
			printk(")\n");
		}
	}
	tab(n); printk("Report Size(%u)\n", field->report_size);
	tab(n); printk("Report Count(%u)\n", field->report_count);
	tab(n); printk("Report Offset(%u)\n", field->report_offset);

	tab(n); printk("Flags( ");
	j = field->flags;
	printk("%s", HID_MAIN_ITEM_CONSTANT & j ? "Constant " : "");
	printk("%s", HID_MAIN_ITEM_VARIABLE & j ? "Variable " : "Array ");
	printk("%s", HID_MAIN_ITEM_RELATIVE & j ? "Relative " : "Absolute ");
	printk("%s", HID_MAIN_ITEM_WRAP & j ? "Wrap " : "");
	printk("%s", HID_MAIN_ITEM_NONLINEAR & j ? "NonLinear " : "");
	printk("%s", HID_MAIN_ITEM_NO_PREFERRED & j ? "NoPrefferedState " : "");
	printk("%s", HID_MAIN_ITEM_NULL_STATE & j ? "NullState " : "");
	printk("%s", HID_MAIN_ITEM_VOLATILE & j ? "Volatile " : "");
	printk("%s", HID_MAIN_ITEM_BUFFERED_BYTE & j ? "BufferedByte " : "");
	printk(")\n");
}

static void __attribute__((unused)) hid_dump_device(struct hid_device *device) {
	struct hid_report_enum *report_enum;
	struct hid_report *report;
	struct list_head *list;
	unsigned i,k;
	static char *table[] = {"INPUT", "OUTPUT", "FEATURE"};
	
	for (i = 0; i < HID_REPORT_TYPES; i++) {
		report_enum = device->report_enum + i;
		list = report_enum->report_list.next;
		while (list != &report_enum->report_list) {
			report = (struct hid_report *) list;
			tab(2);
			printk("%s", table[i]);
			if (report->id)
				printk("(%d)", report->id);
			printk("[%s]", table[report->type]);
			printk("\n");
			for (k = 0; k < report->maxfield; k++) {
				tab(4);
				printk("Field(%d)\n", k);
				hid_dump_field(report->field[k], 6);
			}
			list = list->next;
		}
	}
}

static void __attribute__((unused)) hid_dump_input(struct hid_usage *usage, __s32 value) {
	printk("hid-debug: input ");
	resolv_usage(usage->hid);
	printk(" = %d\n", value);
}
