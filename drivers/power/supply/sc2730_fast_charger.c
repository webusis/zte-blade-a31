// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Spreadtrum Communications Inc.

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/usb/phy.h>
#include <linux/regmap.h>
#include <linux/notifier.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/power/charger-manager.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/pd.h>

#ifdef CONFIG_VENDOR_SQC_CHARGER_V2
#include <vendor/common/sqc_common.h>
#include <vendor/common/zte_misc.h>

int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val);
#endif


#define FCHG1_TIME1				0x0
#define FCHG1_TIME2				0x4
#define FCHG1_DELAY				0x8
#define FCHG2_DET_HIGH				0xc
#define FCHG2_DET_LOW				0x10
#define FCHG2_DET_LOW_CV			0x14
#define FCHG2_DET_HIGH_CV			0x18
#define FCHG2_DET_LOW_CC			0x1c
#define FCHG2_ADJ_TIME1				0x20
#define FCHG2_ADJ_TIME2				0x24
#define FCHG2_ADJ_TIME3				0x28
#define FCHG2_ADJ_TIME4				0x2c
#define FCHG_CTRL				0x30
#define FCHG_ADJ_CTRL				0x34
#define FCHG_INT_EN				0x38
#define FCHG_INT_CLR				0x3c
#define FCHG_INT_STS				0x40
#define FCHG_INT_STS0				0x44
#define FCHG_ERR_STS				0x48

#define SC2721_MODULE_EN0			0xC08
#define SC2721_CLK_EN0				0xC10
#define SC2721_IB_CTRL				0xEA4
#define SC2730_MODULE_EN0			0x1808
#define SC2730_CLK_EN0				0x1810
#define SC2730_IB_CTRL				0x1b84
#define UMP9620_MODULE_EN0			0x2008
#define UMP9620_CLK_EN0				0x2010
#define UMP9620_IB_CTRL				0x2384

#define ANA_REG_IB_TRIM_MASK			GENMASK(6, 0)
#define ANA_REG_IB_TRIM_SHIFT			2
#define ANA_REG_IB_TRIM_EM_SEL_BIT		BIT(1)
#define ANA_REG_IB_TRUM_OFFSET			0x1e

#define FAST_CHARGE_MODULE_EN0_BIT		BIT(11)
#define FAST_CHARGE_RTC_CLK_EN0_BIT		BIT(4)

#define FCHG_ENABLE_BIT				BIT(0)
#define FCHG_INT_EN_BIT				BIT(1)
#define FCHG_INT_CLR_MASK			BIT(1)
#define FCHG_TIME1_MASK				GENMASK(10, 0)
#define FCHG_TIME2_MASK				GENMASK(11, 0)
#define FCHG_DET_VOL_MASK			GENMASK(1, 0)
#define FCHG_DET_VOL_SHIFT			3
#define FCHG_DET_VOL_EXIT_SFCP			3
#define FCHG_CALI_MASK				GENMASK(15, 9)
#define FCHG_CALI_SHIFT				9

#define FCHG_ERR0_BIT				BIT(1)
#define FCHG_ERR1_BIT				BIT(2)
#define FCHG_ERR2_BIT				BIT(3)
#define FCHG_OUT_OK_BIT				BIT(0)

#define FCHG_INT_STS_DETDONE			BIT(5)

/* FCHG1_TIME1_VALUE is used for detect the time of V > VT1 */
#define FCHG1_TIME1_VALUE			0x514
/* FCHG1_TIME2_VALUE is used for detect the time of V > VT2 */
#define FCHG1_TIME2_VALUE			0x9c4

#define FCHG_VOLTAGE_5V				5000000
#define FCHG_VOLTAGE_9V				9000000
#define FCHG_VOLTAGE_12V			12000000
#define FCHG_VOLTAGE_20V			20000000

#define SC2730_FCHG_TIMEOUT			msecs_to_jiffies(5000)
#define SC2730_FAST_CHARGER_DETECT_MS		msecs_to_jiffies(1000)

#define SC2730_PD_START_POWER_MW		18000
#define SC2730_PD_STOP_POWER_MW			10000

#define SC2730_ENABLE_PPS			2
#define SC2730_DISABLE_PPS			1

#define TCPM_NAME 				"tcpm-source-psy-sc27xx-pd"

struct sc27xx_fast_chg_data {
	u32 module_en;
	u32 clk_en;
	u32 ib_ctrl;
};

static const struct sc27xx_fast_chg_data sc2721_info = {
	.module_en = SC2721_MODULE_EN0,
	.clk_en = SC2721_CLK_EN0,
	.ib_ctrl = SC2721_IB_CTRL,
};

static const struct sc27xx_fast_chg_data sc2730_info = {
	.module_en = SC2730_MODULE_EN0,
	.clk_en = SC2730_CLK_EN0,
	.ib_ctrl = SC2730_IB_CTRL,
};

static const struct sc27xx_fast_chg_data ump9620_info = {
	.module_en = UMP9620_MODULE_EN0,
	.clk_en = UMP9620_CLK_EN0,
	.ib_ctrl = UMP9620_IB_CTRL,
};

struct sc2730_fchg_info {
	struct device *dev;
	struct regmap *regmap;
	struct usb_phy *usb_phy;
	struct notifier_block usb_notify;
	struct notifier_block pd_notify;
	struct power_supply *psy_usb;
	struct power_supply *psy_tcpm;
	struct delayed_work work;
	struct delayed_work delay_notify_sqc;
	struct workqueue_struct *delay_notify_queue;
	struct work_struct pd_change_work;
	struct mutex lock;
	struct completion completion;
	struct adapter_power_cap pd_source_cap;
	u32 state;
	u32 base;
	int input_vol;
	u32 limit;
	bool detected;
	bool pd_enable;
	bool sfcp_enable;
	bool pps_enable;
	bool pps_active;
	bool tcpm_flag;
	bool support_sfcp;
	const struct sc27xx_fast_chg_data *pdata;
};

static int sc2730_fchg_internal_cur_calibration(struct sc2730_fchg_info *info)
{
	struct nvmem_cell *cell;
	int calib_data, calib_current, ret;
	void *buf;
	size_t len;
	const struct sc27xx_fast_chg_data *pdata = info->pdata;

	cell = nvmem_cell_get(info->dev, "fchg_cur_calib");
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	memcpy(&calib_data, buf, min(len, sizeof(u32)));
	kfree(buf);

	/*
	 * In the handshake protocol behavior of sfcp, the current source
	 * of the fast charge internal module is small, we improve it
	 * by set the register ANA_REG_IB_CTRL. Now we add 30 level compensation.
	 */
	calib_current = (calib_data & FCHG_CALI_MASK) >> FCHG_CALI_SHIFT;
	calib_current += ANA_REG_IB_TRUM_OFFSET;

	ret = regmap_update_bits(info->regmap,
				 pdata->ib_ctrl,
				 ANA_REG_IB_TRIM_MASK << ANA_REG_IB_TRIM_SHIFT,
				 (calib_current & ANA_REG_IB_TRIM_MASK) << ANA_REG_IB_TRIM_SHIFT);
	if (ret) {
		dev_err(info->dev, "failed to calibrate fast charger current.\n");
		return ret;
	}

	/*
	 * Fast charge dm current source calibration mode, enable soft calibration mode.
	 */
	ret = regmap_update_bits(info->regmap, pdata->ib_ctrl,
				 ANA_REG_IB_TRIM_EM_SEL_BIT,
				 0);
	if (ret) {
		dev_err(info->dev, "failed to select ib trim mode.\n");
		return ret;
	}

	return 0;
}

static irqreturn_t sc2730_fchg_interrupt(int irq, void *dev_id)
{
	struct sc2730_fchg_info *info = dev_id;
	u32 int_sts, int_sts0;
	int ret;

	ret = regmap_read(info->regmap, info->base + FCHG_INT_STS, &int_sts);
	if (ret)
		return IRQ_RETVAL(ret);

	ret = regmap_read(info->regmap, info->base + FCHG_INT_STS0, &int_sts0);
	if (ret)
		return IRQ_RETVAL(ret);

	ret = regmap_update_bits(info->regmap, info->base + FCHG_INT_EN,
				 FCHG_INT_EN_BIT, 0);
	if (ret) {
		dev_err(info->dev, "failed to disable fast charger irq.\n");
		return IRQ_RETVAL(ret);
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG_INT_CLR,
				 FCHG_INT_CLR_MASK, FCHG_INT_CLR_MASK);
	if (ret) {
		dev_err(info->dev, "failed to clear fast charger interrupts\n");
		return IRQ_RETVAL(ret);
	}

	if ((int_sts & FCHG_INT_STS_DETDONE) && !(int_sts0 & FCHG_OUT_OK_BIT))
		dev_warn(info->dev,
			 "met some errors, now status = 0x%x, status0 = 0x%x\n",
			 int_sts, int_sts0);

	if (info->state == POWER_SUPPLY_USB_TYPE_PD)
		dev_info(info->dev, "Already PD, don't update SFCP\n");
	else if ((int_sts & FCHG_INT_STS_DETDONE) && (int_sts0 & FCHG_OUT_OK_BIT))
		info->state = POWER_SUPPLY_CHARGE_TYPE_FAST;
	else
		info->state = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;

	complete(&info->completion);

	return IRQ_HANDLED;
}

static void sc2730_fchg_detect_status(struct sc2730_fchg_info *info)
{
	unsigned int min, max;

	/*
	 * If the USB charger status has been USB_CHARGER_PRESENT before
	 * registering the notifier, we should start to charge with getting
	 * the charge current.
	 */
	if (info->usb_phy->chg_state != USB_CHARGER_PRESENT)
		return;

	usb_phy_get_charger_current(info->usb_phy, &min, &max);

	info->limit = min;
	/*
	 * There is a confilt between charger detection and fast charger
	 * detection, and BC1.2 detection time consumption is <300ms,
	 * so we delay fast charger detection to avoid this issue.
	 */
	schedule_delayed_work(&info->work, SC2730_FAST_CHARGER_DETECT_MS);
}

static int sc2730_fchg_usb_change(struct notifier_block *nb,
				     unsigned long limit, void *data)
{
	struct sc2730_fchg_info *info =
		container_of(nb, struct sc2730_fchg_info, usb_notify);

	dev_info(info->dev, "%s limit:%d, support_sfcp:%d\n", __func__, limit, info->support_sfcp);
	if (!info->support_sfcp) {
		return NOTIFY_OK;
	}


	info->limit = limit;
	if (!info->limit) {
		cancel_delayed_work(&info->work);
		schedule_delayed_work(&info->work, 0);
	} else {
		/*
		 * There is a confilt between charger detection and fast charger
		 * detection, and BC1.2 detection time consumption is <300ms,
		 * so we delay fast charger detection to avoid this issue.
		 */
		schedule_delayed_work(&info->work, SC2730_FAST_CHARGER_DETECT_MS);
	}
	return NOTIFY_OK;
}

static u32 sc2730_fchg_get_detect_status(struct sc2730_fchg_info *info)
{
	unsigned long timeout;
	int value, ret;
	const struct sc27xx_fast_chg_data *pdata = info->pdata;

	/*
	 * In cold boot phase, system will detect fast charger status,
	 * if charger is not plugged in, it will cost another 2s
	 * to detect fast charger status, so we detect fast charger
	 * status only when DCP charger is plugged in
	 */
	if (info->usb_phy->chg_type != DCP_TYPE)
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;

	reinit_completion(&info->completion);

	if (info->input_vol < FCHG_VOLTAGE_9V)
		value = 0;
	else if (info->input_vol < FCHG_VOLTAGE_12V)
		value = 1;
	else if (info->input_vol < FCHG_VOLTAGE_20V)
		value = 2;
	else
		value = 3;

	/*
	 * Due to the the current source of the fast charge internal module is small
	 * we need to dynamically calibrate it through the software during the process
	 * of identifying fast charge. After fast charge recognition is completed, we
	 * disable soft calibration compensate function, in order to prevent the dm current
	 * source from deviating in accuracy when used in other modules.
	 */
	ret = sc2730_fchg_internal_cur_calibration(info);
	if (ret) {
		dev_err(info->dev, "failed to set fast charger calibration.\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, pdata->module_en,
				 FAST_CHARGE_MODULE_EN0_BIT,
				 FAST_CHARGE_MODULE_EN0_BIT);
	if (ret) {
		dev_err(info->dev, "failed to enable fast charger.\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, pdata->clk_en,
				 FAST_CHARGE_RTC_CLK_EN0_BIT,
				 FAST_CHARGE_RTC_CLK_EN0_BIT);
	if (ret) {
		dev_err(info->dev,
			"failed to enable fast charger clock.\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG1_TIME1,
				 FCHG_TIME1_MASK, FCHG1_TIME1_VALUE);
	if (ret) {
		dev_err(info->dev, "failed to set fast charge time1\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG1_TIME2,
				 FCHG_TIME2_MASK, FCHG1_TIME2_VALUE);
	if (ret) {
		dev_err(info->dev, "failed to set fast charge time2\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
			FCHG_DET_VOL_MASK << FCHG_DET_VOL_SHIFT,
			(value & FCHG_DET_VOL_MASK) << FCHG_DET_VOL_SHIFT);
	if (ret) {
		dev_err(info->dev,
			"failed to set fast charger detect voltage.\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
				 FCHG_ENABLE_BIT, FCHG_ENABLE_BIT);
	if (ret) {
		dev_err(info->dev, "failed to enable fast charger.\n");
		return ret;
	}

	ret = regmap_update_bits(info->regmap, info->base + FCHG_INT_EN,
				 FCHG_INT_EN_BIT, FCHG_INT_EN_BIT);
	if (ret) {
		dev_err(info->dev, "failed to enable fast charger irq.\n");
		return ret;
	}

	timeout = wait_for_completion_timeout(&info->completion,
					      SC2730_FCHG_TIMEOUT);
	if (!timeout) {
		dev_err(info->dev, "timeout to get fast charger status\n");
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}

	/*
	 * Fast charge dm current source calibration mode, select efuse calibration
	 * as default.
	 */
	ret = regmap_update_bits(info->regmap, pdata->ib_ctrl,
				 ANA_REG_IB_TRIM_EM_SEL_BIT,
				 ANA_REG_IB_TRIM_EM_SEL_BIT);
	if (ret) {
		dev_err(info->dev, "failed to select ib trim mode.\n");
		return ret;
	}

	return info->state;
}

static void sc2730_fchg_disable(struct sc2730_fchg_info *info)
{
	const struct sc27xx_fast_chg_data *pdata = info->pdata;
	int ret;

	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
				 FCHG_ENABLE_BIT, 0);
	if (ret)
		dev_err(info->dev, "failed to disable fast charger.\n");

	/*
	 * Adding delay is to make sure writing the the control register
	 * successfully firstly, then disable the module and clock.
	 */
	msleep(100);

	ret = regmap_update_bits(info->regmap, pdata->module_en,
				 FAST_CHARGE_MODULE_EN0_BIT, 0);
	if (ret)
		dev_err(info->dev, "failed to disable fast charger module.\n");

	ret = regmap_update_bits(info->regmap, pdata->clk_en,
				 FAST_CHARGE_RTC_CLK_EN0_BIT, 0);
	if (ret)
		dev_err(info->dev, "failed to disable charger clock.\n");
}

static int sc2730_fchg_sfcp_adjust_voltage(struct sc2730_fchg_info *info,
					   u32 input_vol)
{
	int ret, value;

	if (input_vol < FCHG_VOLTAGE_9V)
		value = 0;
	else if (input_vol < FCHG_VOLTAGE_12V)
		value = 1;
	else if (input_vol < FCHG_VOLTAGE_20V)
		value = 2;
	else
		value = 3;

	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
				 FCHG_DET_VOL_MASK << FCHG_DET_VOL_SHIFT,
				 (value & FCHG_DET_VOL_MASK) << FCHG_DET_VOL_SHIFT);
	if (ret) {
		dev_err(info->dev,
			"failed to set fast charger detect voltage.\n");
		return ret;
	}

	return 0;
}

static const u32 sc2730_snk_pdo[] = {
	PDO_FIXED(5000, 2000, 0),
};

static const u32 sc2730_snk9v_pdo[] = {
	PDO_FIXED(5000, 2000, 0),
	PDO_FIXED(9000, 2000, 0),
};

#ifdef CONFIG_TYPEC_TCPM
static int sc2730_get_pps_voltage_max(struct sc2730_fchg_info *info, u32 *max_vol)
{
	union power_supply_propval val;
	int ret;

	if (!info->psy_tcpm) {
		dev_err(info->dev, "psy_tcpm is NULL !!!\n");
		return -EINVAL;
	}

	ret = power_supply_get_property(info->psy_tcpm,
					POWER_SUPPLY_PROP_VOLTAGE_MAX,
					&val);
	if (ret) {
		dev_err(info->dev, "failed to set online property\n");
		return ret;
	}

	*max_vol = val.intval;

	return ret;
}

static int sc2730_get_pps_current_max(struct sc2730_fchg_info *info, u32
				      *max_cur)
{
	union power_supply_propval val;
	int ret;

	if (!info->psy_tcpm) {
		dev_err(info->dev, "psy_tcpm is NULL !!!\n");
		return -EINVAL;
	}

	ret = power_supply_get_property(info->psy_tcpm,
					POWER_SUPPLY_PROP_CURRENT_MAX,
					&val);
	if (ret) {
		dev_err(info->dev, "failed to set online property\n");
		return ret;
	}

	*max_cur = val.intval;

	return ret;
}
static int sc2730_fchg_pd_adjust_voltage(struct sc2730_fchg_info *info,
					 u32 input_vol)
{
	struct tcpm_port *port;
	int ret;

	if (!info->psy_tcpm) {
		dev_err(info->dev, "psy_tcpm is NULL !!!\n");
		return -EINVAL;
	}

	port = power_supply_get_drvdata(info->psy_tcpm);
	if (!port) {
		dev_err(info->dev, "failed to get tcpm port\n");
		return -EINVAL;
	}

	if (input_vol < FCHG_VOLTAGE_9V) {
		ret = tcpm_update_sink_capabilities(port, sc2730_snk_pdo,
						    ARRAY_SIZE(sc2730_snk_pdo),
						    SC2730_PD_STOP_POWER_MW);
		if (ret) {
			dev_err(info->dev,
				"failed to set pd 5V ret = %d\n", ret);
			return ret;
		}
	} else if (input_vol < FCHG_VOLTAGE_12V) {
		ret = tcpm_update_sink_capabilities(port, sc2730_snk9v_pdo,
						    ARRAY_SIZE(sc2730_snk9v_pdo),
						    SC2730_PD_START_POWER_MW);
		if (ret) {
			dev_err(info->dev,
				"failed to set pd 9V ret = %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int sc2730_fchg_pps_adjust_voltage(struct sc2730_fchg_info *info,
					 u32 input_vol)
{
	union power_supply_propval val, vol;
	int ret;

	if (!info->psy_tcpm) {
		dev_err(info->dev, "psy_tcpm is NULL !!!\n");
		return -EINVAL;
	}

	if (!info->pps_active) {
		val.intval = SC2730_ENABLE_PPS;
		ret = power_supply_set_property(info->psy_tcpm, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "failed to set online property ret = %d\n", ret);
			return ret;
		}
		info->pps_active = true;
		msleep(150);
	}

	vol.intval = input_vol;
	ret = power_supply_set_property(info->psy_tcpm, POWER_SUPPLY_PROP_VOLTAGE_NOW, &vol);
	if (ret) {
		dev_err(info->dev, "failed to set vol property, ret:%d\n", ret);
		return ret;
	}

	return 0;
}

static int sc2730_fchg_pps_adjust_current(struct sc2730_fchg_info *info,
					 u32 input_current)
{
	union power_supply_propval val;
	int ret;

	if (!info->psy_tcpm) {
		dev_err(info->dev, "psy_tcpm is NULL !!!\n");
		return -EINVAL;
	}

	if (!info->pps_active) {
		val.intval = SC2730_ENABLE_PPS;
		ret = power_supply_set_property(info->psy_tcpm, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "failed to set online property, ret:%d\n", ret);
			return ret;
		}
		info->pps_active = true;
		msleep(150);
	}

	val.intval = input_current;
	ret = power_supply_set_property(info->psy_tcpm, POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (ret) {
		dev_err(info->dev, "failed to set current property, ret:%d\n", ret);
		return ret;
	}

	return 0;
}

static int sc2730_fchg_enable_pps(struct sc2730_fchg_info *info, bool enable)
{
	union power_supply_propval val;
	int ret;

	if (!info->psy_tcpm) {
		dev_err(info->dev, "psy_tcpm is NULL !!!\n");
		return -EINVAL;
	}

	if (info->pps_active && !enable) {
		val.intval = SC2730_DISABLE_PPS;
		ret = power_supply_set_property(info->psy_tcpm, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "failed to disbale pps, ret = %d\n", ret);
			return ret;
		}
		info->pps_active = false;
	} else if (!info->pps_active && enable) {
		val.intval = SC2730_ENABLE_PPS;
		ret = power_supply_set_property(info->psy_tcpm, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_err(info->dev, "failed to enable pps, ret = %d\n", ret);
			return ret;
		}
		info->pps_active = true;
	}

	return 0;
}

static bool sc2730_pps_type = false;
int sc2730_fchg_get_pps_type(void)
{
	return sc2730_pps_type;
}
EXPORT_SYMBOL_GPL(sc2730_fchg_get_pps_type);

static void sc2730_log_source_caps(struct sc2730_fchg_info *info)
{
	int i;
	struct adapter_power_cap pd_source_cap = info->pd_source_cap;

	dev_info(info->dev, "%s[%d]\n", __func__, __LINE__);
	for (i = 0; i < pd_source_cap.nr_source_caps; i++) {
		dev_info(info->dev, " PDO %d: type %d, SRC[%u-%u mV], %u mA, %u mW\n",
			 i, pd_source_cap.type[i], pd_source_cap.min_mv[i], pd_source_cap.max_mv[i],
			 pd_source_cap.ma[i], pd_source_cap.pwr_mw_limit[i]);
	}
}


static void sc2730_fchg_set_pps_type(struct sc2730_fchg_info *info)
{
	if (info->state == POWER_SUPPLY_USB_TYPE_PD_PPS) {
		sc2730_pps_type = true;
	} else {
		sc2730_pps_type = false;
	}
}


static int sc2730_fchg_pd_change(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	struct sc2730_fchg_info *info =
		container_of(nb, struct sc2730_fchg_info, pd_notify);
	struct power_supply *psy = data;

	dev_info(info->dev, "%s() [line=%d]: event=%d, psy->desc->name=%s\n"
		, __func__, __LINE__, event, psy->desc->name);

	if (strcmp(psy->desc->name, TCPM_NAME) != 0)
		goto out;

	if (event != PSY_EVENT_PROP_CHANGED)
		goto out;

	info->tcpm_flag = true;

	info->psy_tcpm = data;

	schedule_work(&info->pd_change_work);

out:
	return NOTIFY_OK;
}

static void sc2730_fchg_pd_change_work(struct work_struct *data)
{
	struct sc2730_fchg_info *info =
		container_of(data, struct sc2730_fchg_info, pd_change_work);
	union power_supply_propval val;
	struct tcpm_port *port;
	int pd_type, ret;
#ifdef CONFIG_VENDOR_SQC_CHARGER_V2
	static bool pd_enable = 0, pps_enable = 0;
#endif


	dev_info(info->dev, "%s() [line=%d]\n", __func__, __LINE__);

	mutex_lock(&info->lock);

	if (!info->psy_tcpm) {
		dev_err(info->dev, "psy_tcpm NULL !!!\n");
		goto out;
	}

	port = power_supply_get_drvdata(info->psy_tcpm);
	if (!port) {
		dev_err(info->dev, "failed to get tcpm port!\n");
		goto out;
	}

	ret = power_supply_get_property(info->psy_tcpm,
					POWER_SUPPLY_PROP_USB_TYPE,
					&val);
	if (ret) {
		dev_err(info->dev, "failed to get pd type\n");
		goto out;
	}

	tcpm_get_source_capabilities(port, &info->pd_source_cap);
	sc2730_log_source_caps(info);

	pd_type = val.intval;
	dev_info(info->dev, "%s() [line=%d] ->: pd_type=%d\n", __func__, __LINE__, pd_type);
	if (pd_type == POWER_SUPPLY_USB_TYPE_PD) {
		info->pd_enable = true;
		info->pps_enable = false;
		info->pps_active = false;
		info->state = POWER_SUPPLY_USB_TYPE_PD;
		mutex_unlock(&info->lock);
		cm_notify_event(info->psy_usb, CM_EVENT_FAST_CHARGE, NULL);
		goto out1;
	} else if (pd_type == POWER_SUPPLY_USB_TYPE_PD_PPS) {
		info->pps_enable = true;
		info->pd_enable = false;
		info->state = POWER_SUPPLY_USB_TYPE_PD_PPS;
		mutex_unlock(&info->lock);
		cm_notify_event(info->psy_usb, CM_EVENT_FAST_CHARGE, NULL);
		goto out1;
	} else if (pd_type == POWER_SUPPLY_USB_TYPE_C) {
		if (info->pd_enable)
			sc2730_fchg_pd_adjust_voltage(info, FCHG_VOLTAGE_5V);

		info->pd_enable = false;
		info->pps_enable = false;
		info->pps_active = false;
		if (info->state != POWER_SUPPLY_CHARGE_TYPE_FAST)
			info->state = POWER_SUPPLY_USB_TYPE_C;
	}

out:
	mutex_unlock(&info->lock);

out1:
#ifdef CONFIG_VENDOR_SQC_CHARGER_V2
	dev_info(info->dev, "[begin]pd type = %d, pd_enable=%d, pps_enable=%d, pps_active=%d, state=%d\n",
			pd_type, info->pd_enable, info->pps_enable, info->pps_active, info->state);

	if (info->pps_enable != pps_enable) {
		if (info->pps_enable) {
			queue_delayed_work(info->delay_notify_queue,
											&info->delay_notify_sqc,
											msecs_to_jiffies(1500));
		}
	} else if (info->pd_enable != pd_enable) {
		if (info->pd_enable) {
			queue_delayed_work(info->delay_notify_queue,
											&info->delay_notify_sqc,
											msecs_to_jiffies(1500));
		}
	}

	if (!info->pd_enable && !info->pps_enable) {
		flush_delayed_work(&info->delay_notify_sqc);
		cancel_delayed_work_sync(&info->delay_notify_sqc);
		queue_delayed_work(info->delay_notify_queue,
										&info->delay_notify_sqc,
										msecs_to_jiffies(0));
	}

	pd_enable = info->pd_enable;
	pps_enable = info->pps_enable;

#endif

	sc2730_fchg_set_pps_type(info);

	dev_info(info->dev, "[end]pd type = %d, pd_enable=%d, pps_enable=%d, pps_active=%d, state=%d\n",
		pd_type, info->pd_enable, info->pps_enable, info->pps_active, info->state);
}

static void sc2730_fchg_pd_tcpm_change(struct sc2730_fchg_info *info)
{
	struct power_supply *psy = power_supply_get_by_name(TCPM_NAME);

	dev_info(info->dev, "%s() [line=%d]\n", __func__, __LINE__);
	if (psy == NULL)
		return;

	info->psy_tcpm = psy;

	schedule_work(&info->pd_change_work);
}

#else
static int sc2730_get_pps_voltage_max(struct sc2730_fchg_info *info, u32
				      *max_vol)
{
	return 0;
}

static int sc2730_get_pps_current_max(struct sc2730_fchg_info *info, u32
				      *max_cur)
{
	return 0;
}

static int sc2730_fchg_pd_adjust_voltage(struct sc2730_fchg_info *info,
					 u32 input_vol)
{
	return 0;
}

static int sc2730_fchg_pps_adjust_voltage(struct sc2730_fchg_info *info,
					 u32 input_vol)
{
	return 0;
}

static int sc2730_fchg_pps_adjust_current(struct sc2730_fchg_info *info,
					 u32 input_current)
{
	return 0;
}

static int sc2730_fchg_enable_pps(struct sc2730_fchg_info *info, bool enable)
{
	return 0;
}


static int sc2730_fchg_pd_change(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	return NOTIFY_OK;
}

static void sc2730_fchg_pd_change_work(struct work_struct *data)
{

}

static void sc2730_fchg_pd_tcpm_change(struct sc2730_fchg_info *info)
{

}

#endif

#ifdef CONFIG_VENDOR_SQC_CHARGER_V2
static int sqc_shutdown = 0;
extern struct sqc_pd_proto_ops sqc_pd_ops_node;

static int pd_status_init(void)
{
	struct sc2730_fchg_info *info =
		(struct sc2730_fchg_info *)sqc_pd_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: info is null\n", __func__);
		return SQC_ADAPTER_ERROR;
	}


	return SQC_ADAPTER_OK;
}

static int pd_status_end(void)
{
	struct sc2730_fchg_info *info =
		(struct sc2730_fchg_info *)sqc_pd_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: info is null\n", __func__);
		return SQC_ADAPTER_ERROR;
	}

	pr_err("[SQC-HW]: %s\n", __func__);

	if (sc2730_fchg_enable_pps(info, false)) {
		dev_err(info->dev, "failed to disable pps\n");
		return SQC_ADAPTER_ERROR;
	}

	return SQC_ADAPTER_OK;
}

static int pd_get_charger_type(int *type)
{
	struct sc2730_fchg_info *info =
		(struct sc2730_fchg_info *)sqc_pd_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: [%s] info is null\n", __func__);
		*type = SQC_NONE_TYPE;
		return SQC_NONE_TYPE;
	}

	if (info->pps_enable) {
		*type = SQC_PD3D0_APDO_TYPE;
	} else if (info->pd_enable) {
		*type = SQC_PD3D0_BASE_TYPE;
	} else {
		*type = SQC_NONE_TYPE;
	}

	pr_info("[SQC-HW]: [%s] pd_get_charger_type %d\n", __func__, *type);

	return *type;
}

static int pd_get_cable_type(int *type)
{
	struct sc2730_fchg_info *info =
		(struct sc2730_fchg_info *)sqc_pd_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: [%s] info is null\n", __func__);
		return SQC_NONE_TYPE;
	}

	*type = SQC_CABLE_TYPE_NONE;

	return 0;
}

static int pd_set_sqc_cap(int mV, int mA)
{
	struct sc2730_fchg_info *info =
		(struct sc2730_fchg_info *)sqc_pd_ops_node.arg;
	int ret = 0;

	if (!info || sqc_shutdown) {
		pr_err("[SQC-HW]: info is null|sqc_shutdown\n", __func__);
		return SQC_ADAPTER_ERROR;
	}

	pr_info("[SQC-HW]: [%s] mV %d, mA %d\n", __func__, mV, mA);

	if (info->pps_enable) {
		ret = sc2730_fchg_pps_adjust_current(info, mA * 1000);
		if (ret)
			dev_err(info->dev, "[SQC-HW]%s sc2730_fchg_pps_adjust_current failed, ret:%d\n",
				__func__, ret);

		ret = sc2730_fchg_pps_adjust_voltage(info, mV * 1000);
		if (ret)
			dev_err(info->dev, "[SQC-HW]%s sc2730_fchg_pps_adjust_voltage failed, ret:%d\n",
				__func__, ret);
	} else if (info->pd_enable) {
		ret = sc2730_fchg_enable_pps(info, false);
		if (ret)
			dev_err(info->dev, "[SQC-HW]%s sc2730_fchg_enable_pps failed disable, ret:%d\n",
				__func__, ret);

		ret = sc2730_fchg_pd_adjust_voltage(info, mV * 1000);
		if (ret)
			dev_err(info->dev, "[SQC-HW]%s sc2730_fchg_pd_adjust_voltage failed, ret:%d\n",
				__func__, ret);
	}

	/* According to sprd's suggestions, ignore the error requests.
	Let the PD protocol to control the state machine. */
	if (ret == -ETIMEDOUT)
		return SQC_ADAPTER_TIMEOUT;
	else if (ret < 0)
		return SQC_ADAPTER_ERROR;

	return SQC_ADAPTER_OK;
}

static int pd_get_apdo_cap(int *mV, int *mA)
{
	struct sc2730_fchg_info *info =
		(struct sc2730_fchg_info *)sqc_pd_ops_node.arg;

	if (info == NULL) {
		pr_err("[SQC-HW]: info is null\n", __func__);
		return SQC_ADAPTER_ERROR;
	}

	pr_err("[SQC-HW]: %s\n", __func__);

	if (info->pps_enable)
		sc2730_get_pps_voltage_max(info, mV);

	if (info->pps_enable)
		sc2730_get_pps_current_max(info, mA);

	*mV = *mV / 1000;
	*mA = *mA / 1000;

	pr_err("[SQC-HW]: %s, max_vol: %d, max_cur: %d\n", __func__, *mV, *mA);

	return SQC_ADAPTER_OK;
}

static int pd_get_base_cap(int *mV, int *mA)
{
	struct sc2730_fchg_info *info =
		(struct sc2730_fchg_info *)sqc_pd_ops_node.arg;

	if (info == NULL) {
		pr_err("[SQC-HW]: info is null\n", __func__);
		return SQC_ADAPTER_ERROR;
	}

	*mV |= 1 << SQC_PD_BASE_SUPPORT_5V;
	*mV |= 1 << SQC_PD_BASE_SUPPORT_9V;
	*mV |= 1 << SQC_PD_BASE_SUPPORT_12V;

	return SQC_ADAPTER_OK;
}

static int pd_get_sqc_cap(int *mV, int *mA)
{
	struct sc2730_fchg_info *info =
		(struct sc2730_fchg_info *)sqc_pd_ops_node.arg;

	if (info->pps_enable) {
		pd_get_apdo_cap(mV, mA);
	} else if (info->pd_enable) {
		pd_get_base_cap(mV, mA);
	}

	return 0;
}

struct sqc_pd_proto_ops sqc_pd_ops_node = {
	.status_init = pd_status_init,
	.status_remove = pd_status_end,
	.get_charger_type = pd_get_charger_type,
	.get_cable_type = pd_get_cable_type,
	.set_apdo_cap = pd_set_sqc_cap,
	.get_apdo_cap = pd_get_sqc_cap,
};

static void sqc_delay_notify_daemon(struct work_struct *data)
{
	struct delayed_work *dwork = to_delayed_work(data);
	struct sc2730_fchg_info *info =
		container_of(dwork, struct sc2730_fchg_info, delay_notify_sqc);

	if (info->pd_enable || info->pps_enable) {
		pr_err("[SQC-HW]: [%s] notify 1\n", __func__);
		sqc_notify_daemon_changed(SQC_NOTIFY_USBPD,
					SQC_NOTIFY_USB_STATUS_CHANGED, 1);
	} else {
		pr_err("[SQC-HW]: [%s] notify 0\n", __func__);
		sqc_notify_daemon_changed(SQC_NOTIFY_USBPD,
					SQC_NOTIFY_USB_STATUS_CHANGED, 0);
	}

}

#endif

static int sc2730_fchg_usb_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct sc2730_fchg_info *info = power_supply_get_drvdata(psy);
	int ret = 0;

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = info->state;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = 0;
		if (info->pps_enable)
			sc2730_get_pps_voltage_max(info, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (info->pps_enable)
			sc2730_get_pps_current_max(info, &val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sc2730_fchg_usb_set_property(struct power_supply *psy,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct sc2730_fchg_info *info = power_supply_get_drvdata(psy);
	int ret = 0;

#ifdef CONFIG_VENDOR_SQC_CHARGER_V2
	return 0;
#endif

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (val->intval == CM_PPS_CHARGE_DISABLE_CMD) {
			if (sc2730_fchg_enable_pps(info, false)) {
				ret = -EINVAL;
				dev_err(info->dev, "failed to disable pps\n");
			}
			break;
		} else if (val->intval == CM_PPS_CHARGE_ENABLE_CMD) {
			if (sc2730_fchg_enable_pps(info, true)) {
				ret = -EINVAL;
				dev_err(info->dev, "failed to enable pps\n");
			}
			break;
		}

		if (info->pd_enable) {
			if (sc2730_fchg_enable_pps(info, false))
				dev_err(info->dev, "failed to disable pps\n");

			ret = sc2730_fchg_pd_adjust_voltage(info, val->intval);
			if (ret)
				dev_err(info->dev, "failed to adjust pd vol\n");
		} else if (info->pps_enable) {
			ret = sc2730_fchg_pps_adjust_voltage(info, val->intval);
			if (ret)
				dev_err(info->dev, "failed to adjust pd vol\n");
		} else if (info->sfcp_enable) {
			ret = sc2730_fchg_sfcp_adjust_voltage(info, val->intval);
			if (ret)
				dev_err(info->dev, "failed to adjust sfcp vol\n");
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		sc2730_fchg_pps_adjust_current(info, val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sc2730_fchg_property_is_writeable(struct power_supply *psy,
					     enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case  POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case  POWER_SUPPLY_PROP_CURRENT_MAX:
		ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static enum power_supply_property sc2730_fchg_usb_props[] = {
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static const struct power_supply_desc sc2730_fchg_desc = {
	.name			= "sc2730_fast_charger",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= sc2730_fchg_usb_props,
	.num_properties		= ARRAY_SIZE(sc2730_fchg_usb_props),
	.get_property		= sc2730_fchg_usb_get_property,
	.set_property		= sc2730_fchg_usb_set_property,
	.property_is_writeable	= sc2730_fchg_property_is_writeable,
};

static void sc2730_fchg_work(struct work_struct *data)
{
	struct delayed_work *dwork = to_delayed_work(data);
	struct sc2730_fchg_info *info =
		container_of(dwork, struct sc2730_fchg_info, work);

	dev_info(info->dev, "limit:%d, detected:%d, state:%d, pd_enable = %d, "
			"pps_enable = %d, sfcp_enable = %d\n",
			info->limit, info->detected, info->state, info->pd_enable,
			info->pps_enable, info->sfcp_enable);

	mutex_lock(&info->lock);
	if (!info->limit) {
		if (!info->pps_enable || info->state != POWER_SUPPLY_USB_TYPE_PD_PPS)
			info->state = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;

		info->detected = false;
		info->sfcp_enable = false;
		sc2730_fchg_disable(info);
	} else if (!info->detected) {
		info->detected = true;
		if (info->pd_enable || info->pps_enable) {
			sc2730_fchg_disable(info);
		} else if (sc2730_fchg_get_detect_status(info) ==
		    POWER_SUPPLY_CHARGE_TYPE_FAST) {
			/*
			 * Must release info->lock before send fast charge event
			 * to charger manager, otherwise it will cause deadlock.
			 */
			info->sfcp_enable = true;
			mutex_unlock(&info->lock);
			cm_notify_event(info->psy_usb, CM_EVENT_FAST_CHARGE, NULL);
			dev_info(info->dev, "pd_enable = %d, sfcp_enable = %d\n",
				 info->pd_enable, info->sfcp_enable);
			return;
		} else {
			sc2730_fchg_disable(info);
		}
	}

	mutex_unlock(&info->lock);

	dev_info(info->dev, "state=%d, pd_enable = %d, pps_enable = %d, sfcp_enable = %d\n",
		 info->state, info->pd_enable, info->pps_enable, info->sfcp_enable);
}

static int sc2730_fchg_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sc2730_fchg_info *info;
	struct power_supply_config charger_cfg = { };
	int irq, ret;

	pr_info("%s enter\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	info->state = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	info->pdata = of_device_get_match_data(info->dev);
	if (!info->pdata) {
		dev_err(info->dev, "no matching driver data found\n");
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&info->work, sc2730_fchg_work);
	INIT_WORK(&info->pd_change_work, sc2730_fchg_pd_change_work);

#ifdef CONFIG_VENDOR_SQC_CHARGER_V2
	info->delay_notify_queue = create_singlethread_workqueue("sqc_notify_queue");
	INIT_DELAYED_WORK(&info->delay_notify_sqc, sqc_delay_notify_daemon);
#endif

	init_completion(&info->completion);

	info->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!info->regmap) {
		dev_err(&pdev->dev, "failed to get charger regmap\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "reg", &info->base);
	if (ret) {
		dev_err(&pdev->dev, "failed to get register address\n");
		return -ENODEV;
	}

	ret = device_property_read_u32(&pdev->dev,
				       "sprd,input-voltage-microvolt",
				       &info->input_vol);
	if (ret) {
		dev_err(&pdev->dev, "failed to get fast charger voltage.\n");
		return ret;
	}

	info->support_sfcp = device_property_read_bool(&pdev->dev,
					   "sprd,support_sfcp");
	if (info->support_sfcp) {
		dev_info(&pdev->dev, "%s support_sfcp\n", __func__);
	}


	info->pps_active = false;
	platform_set_drvdata(pdev, info);

	info->usb_phy = devm_usb_get_phy_by_phandle(&pdev->dev, "phys", 0);
	if (IS_ERR(info->usb_phy)) {
		dev_err(&pdev->dev, "failed to find USB phy\n");
		return PTR_ERR(info->usb_phy);
	}

	info->usb_notify.notifier_call = sc2730_fchg_usb_change;
	ret = usb_register_notifier(info->usb_phy, &info->usb_notify);
	if (ret) {
		dev_err(&pdev->dev, "failed to register notifier:%d\n", ret);
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource specified\n");
		usb_unregister_notifier(info->usb_phy, &info->usb_notify);
		return irq;
	}
	ret = devm_request_threaded_irq(info->dev, irq, NULL,
					sc2730_fchg_interrupt,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					pdev->name, info);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq.\n");
		usb_unregister_notifier(info->usb_phy, &info->usb_notify);
		return ret;
	}

	dev_info(info->dev, "%s() [line=%d]\n", __func__, __LINE__);
	info->pd_notify.notifier_call = sc2730_fchg_pd_change;
	ret = power_supply_reg_notifier(&info->pd_notify);
	if (ret) {
		dev_err(info->dev, "failed to register pd notifier:%d\n", ret);
		usb_unregister_notifier(info->usb_phy, &info->usb_notify);
		return ret;
	}
	dev_info(info->dev, "%s() [line=%d]\n", __func__, __LINE__);

	charger_cfg.drv_data = info;
	charger_cfg.of_node = np;

	info->psy_usb = devm_power_supply_register(&pdev->dev,
						   &sc2730_fchg_desc,
						   &charger_cfg);
	if (IS_ERR(info->psy_usb)) {
		dev_err(&pdev->dev, "failed to register power supply\n");
		usb_unregister_notifier(info->usb_phy, &info->usb_notify);
		power_supply_unreg_notifier(&info->pd_notify);
		return PTR_ERR(info->psy_usb);
	}

	sc2730_fchg_detect_status(info);

#ifdef CONFIG_VENDOR_SQC_CHARGER_V2
	sqc_pd_ops_node.arg = (void *)info;

	sqc_hal_pd_register(&sqc_pd_ops_node);
#endif

	if ((!info->tcpm_flag) && (info->usb_phy->chg_state == USB_CHARGER_PRESENT))
		sc2730_fchg_pd_tcpm_change(info);

	pr_info("%s end input_vol:%d support_sfcp:%d\n",
		__func__, info->input_vol, info->support_sfcp);

	return 0;
}

static int sc2730_fchg_remove(struct platform_device *pdev)
{
	struct sc2730_fchg_info *info = platform_get_drvdata(pdev);
	int ret = 0;

#ifdef CONFIG_VENDOR_SQC_CHARGER_V2
	sqc_shutdown = 1;
#endif

	pr_info("%s remove disactive pd_enable:%d, pps_enable:%d\n",
		__func__, info->pd_enable, info->pps_enable);

	/* Disable PPS when fchg remove */
	ret = sc2730_fchg_enable_pps(info, false);
	if (ret)
		dev_err(info->dev, "failed to disable pps at shutdown, ret:%d\n", ret);

	usb_unregister_notifier(info->usb_phy, &info->usb_notify);

	return 0;
}

static void sc2730_fchg_shutdown(struct platform_device *pdev)
{
	struct sc2730_fchg_info *info = platform_get_drvdata(pdev);
	int ret;
	u32 value = FCHG_DET_VOL_EXIT_SFCP;

#ifdef CONFIG_VENDOR_SQC_CHARGER_V2
	sqc_shutdown = 1;
#endif

	pr_info("%s shutdown disactive pd_enable:%d, pps_enable:%d\n",
		__func__, info->pd_enable, info->pps_enable);

	cancel_delayed_work_sync(&info->work);
	cancel_work_sync(&info->pd_change_work);
#ifdef CONFIG_VENDOR_SQC_CHARGER_V2
	cancel_delayed_work_sync(&info->delay_notify_sqc);
#endif

	/* Disable PPS when system shutdown */
	if (info->pps_enable) {
		ret = sc2730_fchg_enable_pps(info, false);
		if (ret)
			dev_err(info->dev, "failed to disable pps at shutdown, ret:%d\n", ret);
	}

	/*
	 * SFCP will handsharke failed from charging in shut down
	 * to charging in power up, because SFCP is not exit before
	 * shut down. Set bit3:4 to 2b'11 to exit SFCP.
	 */
	ret = regmap_update_bits(info->regmap, info->base + FCHG_CTRL,
			FCHG_DET_VOL_MASK << FCHG_DET_VOL_SHIFT,
			(value & FCHG_DET_VOL_MASK) << FCHG_DET_VOL_SHIFT);
	if (ret)
		dev_err(info->dev,
			"failed to set fast charger detect voltage.\n");
}

static const struct of_device_id sc2730_fchg_of_match[] = {
	{ .compatible = "sprd,sc2730-fast-charger", .data = &sc2730_info },
	{ .compatible = "sprd,ump9620-fast-chg", .data = &ump9620_info },
	{ .compatible = "sprd,sc2721-fast-charger", .data = &sc2721_info },
	{ }
};

static struct platform_driver sc2730_fchg_driver = {
	.driver = {
		.name = "sc2730-fast-charger",
		.of_match_table = sc2730_fchg_of_match,
	},
	.probe = sc2730_fchg_probe,
	.remove = sc2730_fchg_remove,
	.shutdown = sc2730_fchg_shutdown,
};

module_platform_driver(sc2730_fchg_driver);

MODULE_DESCRIPTION("Spreadtrum SC2730 Fast Charger Driver");
MODULE_LICENSE("GPL v2");
