/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * MyungJoo.Ham <myungjoo.ham@samsung.com>
 *
 * Charger Manager.
 * This framework enables to control and multiple chargers and to
 * monitor charging even in the context of suspend-to-RAM with
 * an interface combining the chargers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
**/

#ifndef _CHARGER_MANAGER_H
#define _CHARGER_MANAGER_H

#include <linux/power_supply.h>
#include <linux/extcon.h>
#include <linux/alarmtimer.h>
#include <linux/pmic-voter.h>


enum data_source {
	CM_BATTERY_PRESENT,
	CM_NO_BATTERY,
	CM_FUEL_GAUGE,
	CM_CHARGER_STAT,
};

enum polling_modes {
	CM_POLL_DISABLE = 0,
	CM_POLL_ALWAYS,
	CM_POLL_EXTERNAL_POWER_ONLY,
	CM_POLL_CHARGING_ONLY,
};

enum cm_event_types {
	CM_EVENT_UNKNOWN = 0,
	CM_EVENT_BATT_FULL,
	CM_EVENT_BATT_IN,
	CM_EVENT_BATT_OUT,
	CM_EVENT_BATT_OVERHEAT,
	CM_EVENT_BATT_COLD,
	CM_EVENT_EXT_PWR_IN_OUT,
	CM_EVENT_CHG_START_STOP,
	CM_EVENT_OTHERS,
	CM_EVENT_FAST_CHARGE,
	CM_EVENT_INT,
};

enum cm_jeita_types {
	CM_JEITA_DCP = 0,
	CM_JEITA_SDP,
	CM_JEITA_CDP,
	CM_JEITA_UNKNOWN,
	CM_JEITA_FCHG,
	CM_JEITA_FLASH,
	CM_JEITA_MAX,
};

enum cm_capacity_cmd {
	CM_CAPACITY = 0,
	CM_BOOT_CAPACITY,
};

enum cm_ir_comp_state {
	CM_IR_COMP_STATE_UNKNOWN,
	CM_IR_COMP_STATE_NORMAL,
	CM_IR_COMP_STATE_CP,
};

enum cm_cp_state {
	CM_CP_STATE_UNKNOWN,
	CM_CP_STATE_RECOVERY,
	CM_CP_STATE_ENTRY,
	CM_CP_STATE_CHECK_VBUS,
	CM_CP_STATE_TUNE,
	CM_CP_STATE_EXIT,
};

enum cm_charge_status {
	CM_CHARGE_TEMP_OVERHEAT = BIT(0),
	CM_CHARGE_TEMP_COLD = BIT(1),
	CM_CHARGE_VOLTAGE_ABNORMAL = BIT(2),
	CM_CHARGE_HEALTH_ABNORMAL = BIT(3),
	CM_CHARGE_DURATION_ABNORMAL = BIT(4),
};

enum thermal_limit_policy {
	THERMAL_LIMIT_IN_OUT,
	THERMAL_LIMIT_IN,
	THERMAL_LIMIT_OUT,
};

enum cm_fast_charge_command {
	CM_FAST_CHARGE_NORMAL_CMD = 1,
	CM_FAST_CHARGE_ENABLE_CMD,
	CM_FAST_CHARGE_DISABLE_CMD,
	CM_PPS_CHARGE_ENABLE_CMD,
	CM_PPS_CHARGE_DISABLE_CMD,
};

enum cm_charger_fault_status {
	CM_CHARGER_BAT_OVP_FAULT = BIT(0),
	CM_CHARGER_BAT_OCP_FAULT = BIT(1),
	CM_CHARGER_BUS_OVP_FAULT = BIT(2),
	CM_CHARGER_BUS_OCP_FAULT = BIT(3),
	CM_CHARGER_BAT_THERM_FAULT = BIT(4),
	CM_CHARGER_BUS_THERM_FAULT = BIT(5),
	CM_CHARGER_DIE_THERM_FAULT = BIT(6),
};

enum cm_charger_alarm_status {
	CM_CHARGER_BAT_OVP_ALARM = BIT(0),
	CM_CHARGER_BAT_OCP_ALARM = BIT(1),
	CM_CHARGER_BUS_OVP_ALARM = BIT(2),
	CM_CHARGER_BUS_OCP_ALARM = BIT(3),
	CM_CHARGER_BAT_THERM_ALARM = BIT(4),
	CM_CHARGER_BUS_THERM_ALARM = BIT(5),
	CM_CHARGER_DIE_THERM_ALARM = BIT(6),
	CM_CHARGER_BAT_UCP_ALARM = BIT(7),
};

#define CM_IBAT_BUFF_CNT 7

/*
struct wireless_data {
	struct power_supply_desc psd;
	struct power_supply *psy;
	int WIRELESS_ONLINE;
};

struct ac_data {
	struct power_supply_desc psd;
	struct power_supply *psy;
	int AC_ONLINE;
};

struct usb_data {
	struct power_supply_desc psd;
	struct power_supply *psy;
	int USB_ONLINE;
};*/

/**
 * struct charger_cable
 * @extcon_name: the name of extcon device.
 * @name: the name of charger cable(external connector).
 * @extcon_dev: the extcon device.
 * @wq: the workqueue to control charger according to the state of
 *	charger cable. If charger cable is attached, enable charger.
 *	But if charger cable is detached, disable charger.
 * @nb: the notifier block to receive changed state from EXTCON
 *	(External Connector) when charger cable is attached/detached.
 * @attached: the state of charger cable.
 *	true: the charger cable is attached
 *	false: the charger cable is detached
 * @charger: the instance of struct charger_regulator.
 * @cm: the Charger Manager representing the battery.
 */
struct charger_cable {
	const char *extcon_name;
	const char *name;

	/* The charger-manager use Extcon framework */
	struct extcon_dev *extcon_dev;
	struct notifier_block nb;

	/* The state of charger cable */
	bool attached;

	struct charger_regulator *charger;

	/*
	 * Set min/max current of regulator to protect over-current issue
	 * according to a kind of charger cable when cable is attached.
	 */
	int min_uA;
	int max_uA;

	struct charger_manager *cm;
};

/**
 * struct charger_regulator
 * @regulator_name: the name of regulator for using charger.
 * @consumer: the regulator consumer for the charger.
 * @externally_control:
 *	Set if the charger-manager cannot control charger,
 *	the charger will be maintained with disabled state.
 * @cables:
 *	the array of charger cables to enable/disable charger
 *	and set current limit according to constraint data of
 *	struct charger_cable if only charger cable included
 *	in the array of charger cables is attached/detached.
 * @num_cables: the number of charger cables.
 * @attr_g: Attribute group for the charger(regulator)
 * @attr_name: "name" sysfs entry
 * @attr_state: "state" sysfs entry
 * @attr_externally_control: "externally_control" sysfs entry
 * @attr_jeita_control: "jeita_control" sysfs entry
 * @attrs: Arrays pointing to attr_name/state/externally_control for attr_g
 */
struct charger_regulator {
	/* The name of regulator for charging */
	const char *regulator_name;
	struct regulator *consumer;

	/* charger never on when system is on */
	int externally_control;

	/*
	 * Store constraint information related to current limit,
	 * each cable have different condition for charging.
	 */
	struct charger_cable *cables;
	int num_cables;

	struct attribute_group attr_g;
	struct device_attribute attr_name;
	struct device_attribute attr_state;
	struct device_attribute attr_stop_charge;
	struct device_attribute attr_externally_control;
	struct device_attribute attr_jeita_control;
	struct attribute *attrs[6];

	struct charger_manager *cm;
};

struct charger_jeita_table {
	int temp;
	int recovery_temp;
	int current_ua;
	int term_volt;
};

enum cm_track_state {
	CAP_TRACK_INIT,
	CAP_TRACK_IDLE,
	CAP_TRACK_UPDATING,
	CAP_TRACK_DONE,
	CAP_TRACK_ERR,
};

struct cm_track_capacity {
	enum cm_track_state state;
	int start_clbcnt;
	int start_cap;
	int end_vol;
	int end_cur;
	s64 start_time;
	bool cap_tracking;
	struct delayed_work track_capacity_work;
};

/*
 * struct cap_remap_table
 * @cnt: record the counts of battery capacity of this scope
 * @lcap: the lower boundary of the capacity scope before transfer
 * @hcap: the upper boundary of the capacity scope before transfer
 * @lb: the lower boundary of the capacity scope after transfer
 * @hb: the upper boundary of the capacity scope after transfer
*/
struct cap_remap_table {
	int cnt;
	int lcap;
	int hcap;
	int lb;
	int hb;
};

/*
 * struct cm_ir_compensation
 * @us: record the full charged battery voltage at normal condition.
 * @rc: compensation resistor value in mohm
 * @ibat_buf: record battery current
 * @us_upper_limit: limit the max battery voltage
 * @cp_upper_limit_offset: use for charge pump mode to adjust battery over
 *	voltage protection value.
 * @us_lower_limit: record the min battery voltage
 * @ir_compensation_en: enable/disable current and resistor compensation function.
 * ibat_index: record current battery current in the ibat_buf
 * @last_target_cccv: record last target cccv point;
 */
struct cm_ir_compensation {
	int us;
	int rc;
	int ibat_buf[CM_IBAT_BUFF_CNT];
	int us_upper_limit;
	int cp_upper_limit_offset;
	int us_lower_limit;
	bool ir_compensation_en;
	int ibat_index;
	int last_target_cccv;
};

/*
 * struct cm_fault_status
 * @bat_ovp_fault: record battery over voltage fault event
 * @bat_ocp_fault: record battery over current fault event
 * @bus_ovp_fault: record bus over voltage fault event
 * @bus_ocp_fault: record bus over current fault event
 * @bat_therm_fault: record battery over temperature fault event
 * @bus_therm_fault: record bus over temperature fault event
 * @die_therm_fault: record die over temperature fault event
 * @vbus_error_lo: record the bus voltage is low event
 * @vbus_error_hi: record the bus voltage is high event
 */
struct cm_fault_status {
	bool bat_ovp_fault;
	bool bat_ocp_fault;
	bool bus_ovp_fault;
	bool bus_ocp_fault;
	bool bat_therm_fault;
	bool bus_therm_fault;
	bool die_therm_fault;
	bool vbus_error_lo;
	bool vbus_error_hi;
};

/*
 * struct cm_alarm_status
 * @bat_ovp_alarm: record battery over voltage alarm event
 * @bat_ocp_alarm: record battery over current alarm event
 * @bus_ovp_alarm: record bus over voltage alarm event
 * @bus_ocp_alarm: record bus over current alarm event
 * @bat_ucp_alarm: record battery under current alarm event
 * @bat_therm_alarm: record battery over temperature alarm event
 * @bus_therm_alarm: record bus over temperature alarm event
 * @die_therm_alarm: record die over temperature alarm event
 */
struct cm_alarm_status {
	bool bat_ovp_alarm;
	bool bat_ocp_alarm;
	bool bus_ovp_alarm;
	bool bus_ocp_alarm;
	bool bat_ucp_alarm;
	bool bat_therm_alarm;
	bool bus_therm_alarm;
	bool die_therm_alarm;
};

/*
 * struct cm_charge_pump_status
 * @cp_running: record charge pumps running status
 * @check_cp_threshold: record the flag whether need to check charge pump
 *	start condition.
 * @cp_ocv_threshold: the ocv threshold of entry pps fast charge directly.
 * @recovery: record the flag whether need recover charge pump machine
 * @cp_state: record current charge pumps state
 * @cp_target_ibat: record target battery current
 * @cp_target_vbat: record target battery voltage
 * @cp_target_ibus: record target bus current
 * @cp_target_vbus: record target bus voltage
 * @cp_last_target_vbus: record the last request target bus voltage
 * @cp_max_ibat: record the upper limit of  battery current
 * @cp_max_ibus: record the upper limit of  bus current
 * @adapter_max_ibus: record the max current of bus
 * @adapter_max_vbus: record the max voltage of bus
 * @vbatt_uV: record the current battery voltage
 * @ibatt_uA: record the current battery current
 * @ibus_uA: record the current bus current
 * @ibus_uV: record the current bus voltage
 * @tune_vbus_retry: record the retry time from vbus low to vbus high
 * @cp_taper_trigger_cnt: record the count of battery current reach taper current
 * @cp_taper_current: record the battery current threshold of exit charge pump
 * @cp_fault_event: record the fault event
 * @flt: record the all fault status
 * @alm: record the all alarm status
 */
struct cm_charge_pump_status {
	bool cp_running;
	bool check_cp_threshold;
	int cp_ocv_threshold;
	bool recovery;
	int cp_state;
	int cp_target_ibat;
	int cp_target_vbat;
	int cp_target_ibus;
	int cp_target_vbus;
	int cp_last_target_vbus;
	int cp_max_ibat;
	int cp_max_ibus;
	int adapter_max_ibus;
	int adapter_max_vbus;
	int vbatt_uV;
	int ibatt_uA;
	int ibus_uA;
	int vbus_uV;
	int tune_vbus_retry;
	int cp_taper_trigger_cnt;
	int cp_adjust_cnt;
	int cp_taper_current;
	bool cp_fault_event;

	struct cm_fault_status  flt;
	struct cm_alarm_status  alm;
};

/**
 * struct charger_desc
 * @psy_name: the name of power-supply-class for charger manager
 * @polling_mode:
 *	Determine which polling mode will be used
 * @fullbatt_vchkdrop_ms:
 * @fullbatt_vchkdrop_uV:
 *	Check voltage drop after the battery is fully charged.
 *	If it has dropped more than fullbatt_vchkdrop_uV after
 *	fullbatt_vchkdrop_ms, CM will restart charging.
 * @fullbatt_uV: voltage in microvolt
 *	If VBATT >= fullbatt_uV, it is assumed to be full.
 * @fullbatt_uA: battery current in microamp
 * @first_fullbatt_uA: battery current in microamp of first_full charged
 * @fullbatt_soc: state of Charge in %
 *	If state of Charge >= fullbatt_soc, it is assumed to be full.
 * @fullbatt_full_capacity: full capacity measure
 *	If full capacity of battery >= fullbatt_full_capacity,
 *	it is assumed to be full.
 * @polling_interval_ms: interval in millisecond at which
 *	charger manager will monitor battery health
 * @battery_present:
 *	Specify where information for existence of battery can be obtained
 * @psy_charger_stat: the names of power-supply for chargers
 * @psy_cp_stat: the names of charge pumps
 * @num_charger_regulator: the number of entries in charger_regulators
 * @charger_regulators: array of charger regulators
 * @psy_fuel_gauge: the name of power-supply for fuel gauge
 * @thermal_zone : the name of thermal zone for battery
 * @temp_min : Minimum battery temperature for charging.
 * @temp_max : Maximum battery temperature for charging.
 * @temp_diff : Temperature difference to restart charging.
 * @cap : Battery capacity report to user space.
 * @measure_battery_temp:
 *	true: measure battery temperature
 *	false: measure ambient temperature
 * @charging_max_duration_ms: Maximum possible duration for charging
 *	If whole charging duration exceed 'charging_max_duration_ms',
 *	cm stop charging.
 * @discharging_max_duration_ms:
 *	Maximum possible duration for discharging with charger cable
 *	after full-batt. If discharging duration exceed 'discharging
 *	max_duration_ms', cm start charging.
 * @normal_charge_voltage_max:
 *	maximum normal charge voltage in microVolts
 * @normal_charge_voltage_drop:
 *	drop voltage in microVolts to allow restart normal charging
 * @fast_charge_voltage_max:
 *	maximum fast charge voltage in microVolts
 * @fast_charge_voltage_drop:
 *	drop voltage in microVolts to allow restart fast charging
 * @flash_charge_voltage_max:
 *	maximum flash charge voltage in microVolts
 * @flash_charge_voltage_drop:
 *	drop voltage in microVolts to allow restart flash charging
 * @charger_status: Recording state of charge
 * @charger_type: Recording type of charge
 * @first_trigger_cnt: The number of times the battery is first_fully charged
 * @trigger_cnt: The number of times the battery is fully charged
 * @uvlo_trigger_cnt: The number of times the battery voltage is
 *	less than under voltage lock out
 * @low_temp_trigger_cnt: The number of times the battery temperature
 *	is less than 10 degree.
 * @cap_one_time: The percentage of electricity is not
 *	allowed to change by 1% in cm->desc->cap_one_time
 * @trickle_time_out: If 99% lasts longer than it , will force set full statu
 * @trickle_time: Record the charging time when battery
 *	capacity is larger than 99%.
 * @trickle_start_time: Record current time when battery capacity is 99%
 * @update_capacity_time: Record the battery capacity update time
 * @last_query_time: Record last time enter cm_batt_works
 * @force_set_full: The flag is indicate whether
 *	there is a mandatory setting of full status
 * @shutdown_voltage: If it has dropped more than shutdown_voltage,
 *	the phone will automatically shut down
 * @wdt_interval: Watch dog time pre-load value
 * @jeita_tab: Specify the jeita temperature table, which is used to
 *	adjust the charging current according to the battery temperature.
 * @jeita_tab_size: Specify the size of jeita temperature table.
 * @jeita_tab_array: Specify the jeita temperature table array, which is used to
 *	save the point of adjust the charging current according to the battery temperature.
 * @jeita_disabled: disable jeita function when needs
 * @temperature: the battery temperature
 * @internal_resist: the battery internal resistance in mOhm
 * @cap_table_len: the length of ocv-capacity table
 * @cap_table: capacity table with corresponding ocv
 * @cap_remap_table: the table record the different scope of capacity
 *	information.
 * @cap_remap_table_len: the length of cap_remap_table
 * @cap_remap_total_cnt: the total count the whole battery capacity is divided
	into.
 * @is_fast_charge: if it is support fast charge or not
 * @enable_fast_charge: if is it start fast charge or not
 * @fast_charge_enable_count: to count the number that satisfy start
 *	fast charge condition.
 * @fast_charge_disable_count: to count the number that satisfy stop
 *	fast charge condition.
 * @double_IC_total_limit_current: if it use two charge IC to support
 *	fast charge, we use total limit current to campare with thermal_val,
 *	to limit the thermal_val under total limit current.
 * @cm_check_int: record the intterupt event
 * @cm_check_fault: record the flag whether need to check fault status
 * @fast_charger_type: record the charge type
 * @cp: record the charge pump status
 * @ir_comp: record the current and resistor compensation status
 */
struct charger_desc {
	const char *psy_name;

	enum polling_modes polling_mode;
	unsigned int polling_interval_ms;

	unsigned int fullbatt_vchkdrop_ms;
	unsigned int fullbatt_vchkdrop_uV;
	unsigned int fullbatt_uV;
	unsigned int fullbatt_warm_uV;
	unsigned int fullbatt_uA;
	unsigned int first_fullbatt_uA;
	unsigned int fullbatt_soc;
	unsigned int fullbatt_full_capacity;
	unsigned int fullbatt_comp_uV;
	int fullbatt_advance_level;

	enum data_source battery_present;

	const char **psy_charger_stat;
	const char **psy_fast_charger_stat;
	const char **psy_cp_stat;

	int num_charger_regulators;
	struct charger_regulator *charger_regulators;

	const char *psy_fuel_gauge;

	const char *thermal_zone;

	const char *psy_hardware;
	const char *psy_hardware2;

	int temp_min;
	int temp_max;
	int temp_diff;

	int cap;
	int cap_debug;
	int cap_buff_debug;
	bool measure_battery_temp;
	bool thermal_control_en;

	u32 charging_max_duration_ms;
	u32 discharging_max_duration_ms;

	u32 charge_voltage_max;
	u32 charge_voltage_drop;
	u32 normal_charge_voltage_max;
	u32 normal_charge_voltage_drop;
	u32 fast_charge_voltage_max;
	u32 fast_charge_voltage_drop;
	u32 flash_charge_voltage_max;
	u32 flash_charge_voltage_drop;

	int charger_status;
	u32 charger_type;
	int trigger_cnt;
	int first_trigger_cnt;
	int uvlo_trigger_cnt;
	int low_temp_trigger_cnt;

	u32 cap_one_time;

	u32 trickle_time_out;
	u64 trickle_time;
	u64 trickle_start_time;

	u64 update_capacity_time;
	u64 last_query_time;

	bool force_set_full;
	u32 shutdown_voltage;

	u32 wdt_interval;

	int thm_adjust_cur;

	struct charger_jeita_table *jeita_tab;
	u32 jeita_tab_size;
	struct charger_jeita_table *jeita_tab_array[CM_JEITA_MAX];

	bool jeita_disabled;
	bool disable_charger_type_jeita;

	int temperature;

	int internal_resist;
	int cap_table_len;
	struct power_supply_battery_ocv_table *cap_table;
	struct cap_remap_table *cap_remap_table;
	u32 cap_remap_table_len;
	int cap_remap_total_cnt;
	bool is_fast_charge;
	bool enable_fast_charge;
	u32 fast_charge_enable_count;
	u32 fast_charge_disable_count;
	u32 double_ic_total_limit_current;
	enum thermal_limit_policy thermal_limit;

	bool cm_check_int;
	bool cm_check_fault;
	u32 fast_charger_type;

	struct cm_charge_pump_status cp;
	struct cm_ir_compensation ir_comp;
};

#define PSY_NAME_MAX	30

#define vote_debug(fmt, args...)	pr_info("VOTE: %s(): "fmt, __func__, ## args)
#define vote_error(fmt, args...)	pr_err("VOTE: %s(): "fmt, __func__, ## args)

#define DEFAULT_SETTING_VOTER "DEFAULT_SETTING_VOTER"
#define BATTERY_SETTING_VOTER "BATTERY_SETTING_VOTER"
#define THERMAL_SETTING_VOTER "THERMAL_SETTING_VOTER"
#define CAS_SETTING_VOTER		"CAS_SETTING_VOTER"
#define POLICY_SETTING_VOTER	"POLICY_SETTING_VOTER"
#define CHARGER_TYPE_VOTER		"CHARGER_TYPE_VOTER"
#define JEITA_SETTING_VOTER		"JEITA_SETTING_VOTER"
#define OTHER_SETTING_VOTER		"OTHER_SETTING_VOTER"
#define UVP_SETTING_VOTER		"UVP_SETTING_VOTER"
#define ADB_SETTING_VOTER		"ADB_SETTING_VOTER"
#define ZTE_THERMAL_SETTING_VOTER "ZTE_THERMAL_SETTING_VOTER"
#define HOOK_PSY_NAME			"hook"

#define HARDWARE_PSY_NAME_MAX	64

#define DEFAULT_SETTING_ICL 3400000

#define DEFAULT_SETTING_TOPOFF 100
#define DEFAULT_SETTING_RECHARGE_SOC 98
#define DEFAULT_SETTING_RECHARGE_VOLTAGE 100000

struct charger_policy {
	struct notifier_block usb_notify;
	struct delayed_work	usb_changed_work;
	struct usb_phy *usb_phy;
	char hardware_pyh[HARDWARE_PSY_NAME_MAX];
	int charge_term_current_ua;	    /* microAmps */
	int constant_charge_current_max_ua; /* microAmps */
	int constant_charge_voltage_max_uv; /* microVolts */
	int limit;
	struct power_supply_charge_current battery_current;
	struct power_supply	*usb_power_phy;
	struct power_supply	*ac_power_phy;
	struct power_supply	*hook_psy;
	struct power_supply	*interface_psy;
	struct votable		*fcc_votable;
	struct votable		*fcv_votable;
	struct votable		*topoff_votable;
	struct votable		*recharge_soc_votable;
	struct votable		*recharge_voltage_votable;
	struct votable		*usb_icl_votable;
	struct votable		*battery_charging_disabled_votable;
};

/**
 * struct charger_manager
 * @entry: entry for list
 * @dev: device pointer
 * @desc: instance of charger_desc
 * @fuel_gauge: power_supply for fuel gauge
 * @charger_stat: array of power_supply for chargers
 * @tzd_batt : thermal zone device for battery
 * @charger_enabled: the state of charger
 * @fullbatt_vchk_jiffies_at:
 *	jiffies at the time full battery check will occur.
 * @fullbatt_vchk_work: work queue for full battery check
 * @uvlo_work: work queue to check uvlo state
 * @ir_compensation_work: work queue to check current and resistor
 *	compensation state
 * @emergency_stop:
 *	When setting true, stop charging
 * @psy_name_buf: the name of power-supply-class for charger manager
 * @charger_psy: power_supply for charger manager
 * @status_save_ext_pwr_inserted:
 *	saved status of external power before entering suspend-to-RAM
 * @status_save_batt:
 *	saved status of battery before entering suspend-to-RAM
 * @charging_start_time: saved start time of enabling charging
 * @charging_end_time: saved end time of disabling charging
 * @charging_status: saved charging status, 0 means charging normal
 * @charge_ws: wakeup source to prevent ap enter sleep mode in charge
 *	pump mode
 */
struct charger_manager {
	struct list_head entry;
	struct device *dev;
	struct charger_desc *desc;

#ifdef CONFIG_THERMAL
	struct thermal_zone_device *tzd_batt;
#endif
	bool charger_enabled;
	bool shutdown_flag;
	bool parallel_charge_enabled;
	bool should_en_parallel_chg;
	bool is_battery_charging_enabled;

	unsigned long fullbatt_vchk_jiffies_at;
	struct delayed_work fullbatt_vchk_work;
	struct delayed_work cap_update_work;
	struct delayed_work uvlo_work;
	struct delayed_work ir_compensation_work;
	struct delayed_work cp_work;
	struct delayed_work charge_debug_work;
	int emergency_stop;

	char psy_name_buf[PSY_NAME_MAX + 1];
	struct power_supply_desc charger_psy_desc;
	struct power_supply *charger_psy;

	u64 charging_start_time;
	u64 charging_end_time;
	u32 charging_status;
	int health;
	struct charger_policy policy;
	struct cm_track_capacity track;
	struct wakeup_source charge_ws;
	int batt_id_index;
};

#if defined(CONFIG_CHARGER_MANAGER) || defined(CONFIG_VENDOR_SQC_CHARGER_V2)
extern void cm_notify_event(struct power_supply *psy,
				enum cm_event_types type, char *msg);
#else
static inline void cm_notify_event(struct power_supply *psy,
				enum cm_event_types type, char *msg) { }
#endif
#endif /* _CHARGER_MANAGER_H */
