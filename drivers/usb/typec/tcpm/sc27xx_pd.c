/* SPDX-License-Identifier: GPL-2.0
** Copyright (C) 2019 Spreadtrum Communications Inc.*/

#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/sched/clock.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/typec.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/pd.h>
#include <linux/wakelock.h>

/* PMIC global registers definition */
#define SC27XX_MODULE_EN		0x1808
#define SC27XX_TYPEC_PD_EN		BIT(13)
#define SC27XX_ARM_CLK_EN0		0x180c
#define SC27XX_RTC_CLK_EN0		0x1810
#define SC27XX_CLK_PD_EN		BIT(9)
#define SC27XX_XTL_WAIT_CTRL0		0x1b78
#define SC27XX_XTL_EN			BIT(8)

/* Typec controller registers definition */
#define SC27XX_TYPC_PD_CFG		0x8
#define SC27XX_TYPEC_STATUS		0x1c
#define SC27XX_TYPEC_SW_CFG		0x54
#define SC27XX_TYPEC_DBG1		0x60
#define SC27XX_TYPEC_IBIAS		0x70

/* PD controller registers definition */
#define SC27XX_PD_TX_BUF		0x0
#define SC27XX_PD_RX_BUF		0x4
#define SC27XX_PD_HEAD_CFG		0x8
#define SC27XX_PD_CTRL			0xc
#define SC27XX_PD_CFG0			0x10
#define SC27XX_PD_CFG1			0x14
#define SC27XX_PD_MESG_ID_CFG		0x18
#define SC27XX_PD_STS0			0x1c
#define SC27XX_PD_STS1			0x20
#define SC27XX_INT_STS			0x24
#define SC27XX_INT_FLG			0x28
#define SC27XX_INT_CLR			0x2c
#define SC27XX_INT_EN			0x30
#define SC27XX_PD_PHY_CFG0		0x50
#define SC27XX_PD_PHY_CFG1		0x54
#define SC27XX_PD_PHY_CFG2		0x58

/* Bits definitions for SC27XX_TYPC_PD_CFG register */
#define SC27XX_TYPEC_PD_SUPPORT		BIT(0)
#define SC27XX_TYPEC_PD_CONSTRACT	BIT(6)
#define SC27XX_TYPEC_PD_NO_CHEK_DETACH	BIT(9)
#define SC27XX_TYPEC_SW_FORCE_CC(x)	(((x) << 10) & GENMASK(11, 10))
#define SC27XX_TYPEC_VCCON_LDO_RDY	BIT(12)
#define SC27XX_TYPEC_VCCON_LDO_EN	BIT(13)

/* Bits definitions for SC27XX_TYPEC_STATUS register */
#define SC27XX_TYPEC_CURRENT_STATUS	GENMASK(4, 0)
#define SC27XX_TYPEC_FINAL_SWITCH	BIT(5)
#define SC27XX_TYPEC_VBUS_CL(x)		(((x) & GENMASK(7, 6)) >> 6)

/* Bits definitions for SC27XX_TYPEC_DBG1 register */
#define SC27XX_TYPEC_VBUS_OK		BIT(8)
#define SC27XX_TYPEC_CONN_CC		BIT(9)

/* Bits definitions for SC27XX_TYPEC_IBIAS register */
#define SC27XX_TYPEC_RX_REF_DECREASE	BIT(7)

/* Bits definitions for SC27XX_PD_HEAD_CFG register */
#define SC27XX_PD_EXTHEAD		BIT(15)
#define SC27XX_PD_NUM_DO(x)		(((x) << 12) & GENMASK(14, 12))
#define SC27XX_PD_MESS_ID(x)		(((x) << 9) & GENMASK(11, 9))
#define SC27XX_PD_POWER_ROLE		BIT(8)
#define SC27XX_PD_SPEC_REV(x)		(((x) << 6) & GENMASK(7, 6))
#define SC27XX_PD_DATA_ROLE		BIT(5)
#define SC27XX_PD_MESSAGE_TYPE(x)	((x) & GENMASK(4, 0))
#define SC27XX_PD_SPEC_MASK		GENMASK(7, 6)

/* Bits definitions for SC27XX_PD_CTRL register */
#define SC27XX_PD_TX_START		BIT(0)
#define SC27XX_PD_HARD_RESET		BIT(2)
#define SC27XX_PD_TX_FLASH		BIT(3)
#define SC27XX_PD_RX_FLASH		BIT(4)
#define SC27XX_PD_FAST_START		BIT(6)
#define SC27XX_PD_CABLE_RESET		BIT(7)
#define SC27XX_PD_RX_ID_CLR		BIT(8)
#define SC27XX_PD_RP_SINKTXNG_CLR	BIT(9)

/* Bits definitions for SC27XX_PD_CFG0 register */
#define SC27XX_PD_RP_CONTROL(x)		((x) & GENMASK(1, 0))
#define SC27XX_PD_SINK_RP		GENMASK(3, 2)
#define SC27XX_PD_EN_SOP1_TX		BIT(6)
#define SC27XX_PD_EN_SOP2_TX		BIT(7)
#define SC27XX_PD_EN_SOP		BIT(8)
#define SC27XX_PD_SRC_SINK_MODE		BIT(9)
#define SC27XX_PD_CTL_EN		BIT(10)
#define SC27XX_PD_BIST_MODE_EN		BIT(11)

/* Bits definitions for SC27XX_PD_CFG1 register */
#define SC27XX_PD_AUTO_RETRY		BIT(0)
#define SC27XX_PD_RETRY(x)		(((x) << 1) & GENMASK(2, 1))
#define SC27XX_PD_HEADER_REG_EN		BIT(3)
#define SC27XX_PD_EN_SOP1_RX		BIT(6)
#define SC27XX_PD_EN_SOP2_RX		BIT(7)
#define SC27XX_PD_EN_SOP_RX		BIT(8)
#define SC27XX_PD_RX_AUTO_GOOD_CRC	BIT(9)
#define SC27XX_PD_FRS_DETECT_EN		BIT(10)
#define SC27XX_PD_PHY_13M		BIT(11)
#define SC27XX_PD_TX_AUTO_GOOD_CRC	BIT(12)

/* Bits definitions for SC27XX_PD_MESG_ID_CFG register */
#define SC27XX_PD_MESS_ID_TX(x)		((x) & GENMASK(2, 0))
#define SC27XX_PD_MESS_ID_RX(x)		(((x) << 4) & GENMASK(6, 4))
#define SC27XX_PD_RETRY_MASK		GENMASK(2, 1)
#define SC27XX_PD_MESS_ID_MASK		GENMASK(2, 0)

/* Bits definitions for SC27XX_TYPEC_SW_CFG register */
#define SC27XX_TYPEC_SW_SWITCH(x)	(((x) << 10) & GENMASK(11, 10))

/* Bits definitions for SC27XX_INT_FLG register */
#define SC27XX_PD_HARD_RST_FLAG		BIT(0)
#define SC27XX_PD_CABLE_RST_FLAG	BIT(1)
#define SC27XX_PD_SOFT_RST_FLAG		BIT(2)
#define SC27XX_PD_PS_RDY_FLAG		BIT(3)
#define SC27XX_PD_PKG_RV_FLAG		BIT(4)
#define SC27XX_PD_TX_OK_FLAG		BIT(5)
#define SC27XX_PD_TX_ERROR_FLAG		BIT(6)
#define SC27XX_PD_TX_COLLSION_FLAG	BIT(7)
#define SC27XX_PD_PKG_RV_ERROR_FLAG	BIT(8)
#define SC27XX_PD_FRS_RV_FLAG		BIT(9)
#define SC27XX_PD_RX_FIFO_OVERFLOW_FLAG	BIT(10)

/* Bits definitions for SC27XX_PD_STS1 register */
#define SC27XX_PD_RX_DATA_NUM_MASK		GENMASK(8, 0)
#define SC27XX_PD_RX_EMPTY		BIT(13)

/* Bits definitions for SC27XX_INT_CLR register */
#define SC27XX_PD_HARD_RST_RV_CLR	BIT(0)
#define SC27XX_PD_CABLE_RST_RV_CLR	BIT(1)
#define SC27XX_PD_SOFT_RST_RV_CLR	BIT(2)
#define SC27XX_PD_PS_RDY_CLR		BIT(3)
#define SC27XX_PD_PKG_RV_CLR		BIT(4)
#define SC27XX_PD_TX_OK_CLR		BIT(5)
#define SC27XX_PD_TX_ERROR_CLR		BIT(6)
#define SC27XX_PD_TX_COLLSION_CLR	BIT(7)
#define SC27XX_PD_PKG_RV_ERROR_CLR	BIT(8)
#define SC27XX_PD_FRS_RV_CLR		BIT(9)
#define SC27XX_PD_RX_FIFO_OVERFLOW_CLR	BIT(10)

/* Bits definitions for SC27XX_INT_EN register */
#define SC27XX_PD_HARD_RST_RV_EN	BIT(0)
#define SC27XX_PD_CABLE_RST_RV_EN	BIT(1)
#define SC27XX_PD_SOFT_RST_RV_EN	BIT(2)
#define SC27XX_PD_PS_RDY_EN		BIT(3)
#define SC27XX_PD_PKG_RV_EN		BIT(4)
#define SC27XX_PD_TX_OK_EN		BIT(5)
#define SC27XX_PD_TX_ERROR_EN		BIT(6)
#define SC27XX_PD_TX_COLLSION_EN	BIT(7)
#define SC27XX_PD_PKG_RV_ERROR_EN	BIT(8)
#define SC27XX_PD_FRS_RV_EN		BIT(9)
#define SC27XX_PD_RX_FIFO_OVERFLOW_EN	BIT(10)

/* SC27XX_PD_PHY_CFG1 */
#define SC27XX_PD_CC1_SW		BIT(3)
#define SC27XX_PD_CC2_SW		BIT(2)

/* SC27XX_PD_PHY_CFG2 */
#define SC27XX_PD_CFG2_PD_CLK_BIT		BIT(6)
#define SC27XX_PD_CFG2_RX_REF_CAL_BIT		BIT(7)
#define SC27XX_PD_CFG2_RX_REF_CAL_SHIFT		7

#define SC27XX_TX_RX_BUF_MASK		GENMASK(15, 0)
#define SC27XX_PD_INT_CLR		GENMASK(13, 0)
#define SC27XX_STATE_MASK		GENMASK(4, 0)
#define SC27XX_EVENT_MASK		GENMASK(15, 0)
#define SC27XX_TYPEC_INT_CLR_MASK	GENMASK(9, 0)
#define SC27XX_PD_HEAD_CONFIG_MASK	GENMASK(15, 0)
#define SC27XX_PD_CFG0_MASK		GENMASK(8, 0)
#define SC27XX_PD_PHY_CFG0_MASK		0x5102
#define	SC27XX_PD_PHY_CFG1_MASK		0x463c
#define SC27XX_PD_PHY_CFG2_MASK		0x40

#define SC27XX_PD_CFG0_RCCAL_MASK	GENMASK(8, 6)
#define SC27XX_PD_CFG0_VTL_MASK		GENMASK(3, 2)
#define SC27XX_PD_CFG0_VTH_MASK		GENMASK(1, 0)
#define SC27XX_PD_CFG0_RCCAL_SHIFT	6
#define SC27XX_PD_CFG0_VTL_SHIFT	2
#define SC27XX_PD_CFG1_VREF_SEL_MASK		GENMASK(7, 6)
#define SC27XX_PD_CFG1_REF_CAL_MASK		GENMASK(14, 12)
#define SC27XX_PD_CFG1_VREF_SEL_SHIFT		6
#define SC27XX_PD_CFG1_REF_CAL_SHIFT		12

#define SC27XX_PD_DATA_MASK		GENMASK(15, 0)
#define SC27XX_INT_CLR_MASK		0x3fff
#ifdef CONFIG_DEBUG_FS
#define SC27XX_INT_EN_MASK		0x85ff
#else
#define SC27XX_INT_EN_MASK		0x85f7
#endif
#define SC27xx_DETECT_TYPEC_DELAY	700
#define SC27xx_SECOND_DETECT_TYPEC_DELAY	10000


/* Timeout (us) for pd data ready according pd datasheet */
#define SC27XX_PD_RDY_TIMEOUT		2000
#define SC27XX_PD_POLL_RAW_STATUS	50

/* Timeout (us) for pd sink txok ready according pd datasheet */
#define SC27XX_PD_SINKOk_TIMEOUT	10000000
#define SC27XX_PD_SINKOK_RAW_STATUS	10000
#define LOG_BUFFER_ENTRIES	1024
#define LOG_BUFFER_ENTRY_SIZE	128

enum sc27xx_state {
	SC27XX_DETACHED_SNK,
	SC27XX_ATTACHWAIT_SNK,
	SC27XX_ATTACHED_SNK,
	SC27XX_DETACHED_SRC,
	SC27XX_ATTACHWAIT_SRC,
	SC27XX_ATTACHED_SRC,
	SC27XX_POWERED_CABLE,
};

struct sc27xx_pd {
	struct device *dev;
	struct extcon_dev *edev;
	struct extcon_dev *extcon;
	struct notifier_block extcon_nb;
	struct tcpm_port *tcpm_port;
	struct delayed_work typec_detect_work;
	struct delayed_work  read_msg_work;
	struct workqueue_struct *pd_wq;
	struct regmap *regmap;
	struct tcpc_dev tcpc;
	struct mutex lock;
	struct regulator *vbus;
	struct regulator *vconn;
	struct tcpc_config config;
	struct work_struct pd_work;
	enum typec_cc_polarity cc_polarity;
	enum typec_cc_status cc1;
	enum typec_cc_status cc2;
	enum typec_role role;
	enum typec_data_role data;
	enum sc27xx_state state;
	bool attached;
	bool constructed;
	bool vconn_on;
	bool vbus_on;
	bool vbus_present;
	u32 base;
	u32 typec_base;
	u32 rc_cal;
	u32 delta_cal;
	u32 ref_cal;
	int msg_flag;
	int need_retry;
	bool pd_attached;
	bool shutdown_flag;
	struct wake_lock pd_wakelock;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dentry;
	struct mutex logbuffer_lock;	/* log buffer access lock */
	struct delayed_work  log2printk;
	struct mutex logprintk_lock;	/* log buffer printk lock */
	int logbuffer_head;
	int logbuffer_tail;
	int logbuffer_last;
	int logbuffer_full;
	u8 *logbuffer[LOG_BUFFER_ENTRIES];
#endif
};

/*
 * Logging
 */

#ifdef CONFIG_DEBUG_FS

static bool tcpm_log_full(struct sc27xx_pd *port)
{
	return port->logbuffer_tail ==
		(port->logbuffer_head + 1) % LOG_BUFFER_ENTRIES;
}

__printf(2, 0)
static void _sprd_pd_log(struct sc27xx_pd *port, const char *fmt, va_list args)
{
	char tmpbuffer[LOG_BUFFER_ENTRY_SIZE];
	u64 ts_nsec = ktime_get_boot_fast_ns();
	unsigned long rem_nsec;

	mutex_lock(&port->logbuffer_lock);
	if (!port->logbuffer[port->logbuffer_head]) {
		port->logbuffer[port->logbuffer_head] =
				kzalloc(LOG_BUFFER_ENTRY_SIZE, GFP_KERNEL);
		if (!port->logbuffer[port->logbuffer_head]) {
			mutex_unlock(&port->logbuffer_lock);
			return;
		}
	}

	vsnprintf(tmpbuffer, sizeof(tmpbuffer), fmt, args);

	if (port->logbuffer_head < 0 ||
	    port->logbuffer_head >= LOG_BUFFER_ENTRIES) {
		dev_warn(port->dev,
			 "Bad log buffer index %d\n", port->logbuffer_head);
		goto abort;
	}

	if (!port->logbuffer[port->logbuffer_head]) {
		dev_warn(port->dev,
			 "Log buffer index %d is NULL\n", port->logbuffer_head);
		goto abort;
	}

	memset(port->logbuffer[port->logbuffer_head], 0, LOG_BUFFER_ENTRY_SIZE);

	rem_nsec = do_div(ts_nsec, 1000000000);
	scnprintf(port->logbuffer[port->logbuffer_head],
		  LOG_BUFFER_ENTRY_SIZE, "[%5lu.%06lu][sprd_pd][index = %d] %s",
		  (unsigned long)ts_nsec, rem_nsec / 1000,
		  port->logbuffer_head, tmpbuffer);

	if (tcpm_log_full(port)) {
		port->logbuffer_full = true;
		cancel_delayed_work(&port->log2printk);
		schedule_delayed_work(&port->log2printk, 0);
	}

	port->logbuffer_head = (port->logbuffer_head + 1) % LOG_BUFFER_ENTRIES;

abort:
	mutex_unlock(&port->logbuffer_lock);
}

__printf(2, 3)
static void sprd_pd_log(struct sc27xx_pd *port, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	_sprd_pd_log(port, fmt, args);
	va_end(args);
}

static void log_printk_work(struct work_struct *work)
{
	struct sc27xx_pd *port = container_of(work, struct sc27xx_pd,
					      log2printk.work);
	int tail, head;

	mutex_lock(&port->logprintk_lock);
	if (port->logbuffer_full) {
		tail = port->logbuffer_last;
		head = LOG_BUFFER_ENTRIES;
		while (tail < head) {
			if (port->logbuffer[tail])
				dev_info(port->dev, "%s\n", port->logbuffer[tail]);
			tail = tail + 1;
		}
		port->logbuffer_last = 0;
		port->logbuffer_full = false;
	} else {
		tail = port->logbuffer_last;
		head = port->logbuffer_head;
		while (tail < head) {
			if (port->logbuffer[tail])
				dev_info(port->dev, "%s\n", port->logbuffer[tail]);
			tail = tail + 1;
		}
		if (port->logbuffer_last != head)
			port->logbuffer_last = head;
	}

	schedule_delayed_work(&port->log2printk, msecs_to_jiffies(20000));
	mutex_unlock(&port->logprintk_lock);
}

#endif


static inline struct sc27xx_pd *tcpc_to_sc27xx_pd(struct tcpc_dev *tcpc)
{
	return container_of(tcpc, struct sc27xx_pd, tcpc);
}

static int sc27xx_pd_clk_cfg(struct sc27xx_pd *pd)
{
	int ret;

	ret = regmap_update_bits(pd->regmap, SC27XX_MODULE_EN,
				 SC27XX_TYPEC_PD_EN, SC27XX_TYPEC_PD_EN);
	if (ret)
		return ret;

	ret = regmap_update_bits(pd->regmap, SC27XX_ARM_CLK_EN0,
				 SC27XX_CLK_PD_EN, SC27XX_CLK_PD_EN);
	if (ret)
		return ret;

	return regmap_update_bits(pd->regmap, SC27XX_XTL_WAIT_CTRL0,
				  SC27XX_XTL_EN, SC27XX_XTL_EN);
}

static int sc27xx_pd_start_drp_toggling(struct tcpc_dev *tcpc,
					enum typec_cc_status cc)
{
	return 0;
}

static int sc27xx_pd_set_cc(struct tcpc_dev *tcpc, enum typec_cc_status cc)
{
	return 0;
}

static int sc27xx_pd_get_cc(struct tcpc_dev *tcpc,
			    enum typec_cc_status *cc1,
			    enum typec_cc_status *cc2)
{
	struct sc27xx_pd *pd = tcpc_to_sc27xx_pd(tcpc);

	mutex_lock(&pd->lock);
	*cc1 = pd->cc1;
	*cc2 = pd->cc2;
	mutex_unlock(&pd->lock);
	return 0;
}

static int sc27xx_pd_set_vbus(struct tcpc_dev *tcpc, bool on, bool charge)
{
	struct sc27xx_pd *pd = tcpc_to_sc27xx_pd(tcpc);

	mutex_lock(&pd->lock);
	if (pd->vbus_on == on) {
		dev_info(pd->dev, "vbus is already %s\n", on ? "On" : "Off");
	} else {
		pd->vbus_on = on;
		dev_info(pd->dev, "vbus := %s", on ? "On" : "Off");
	}
	mutex_unlock(&pd->lock);

	return 0;
}

static int sc27xx_pd_get_vbus(struct tcpc_dev *tcpc)
{
	struct sc27xx_pd *pd = tcpc_to_sc27xx_pd(tcpc);
	int ret;

	mutex_lock(&pd->lock);
	ret = pd->vbus_present ? 1 : 0;
	mutex_unlock(&pd->lock);
	return ret;
}

static int sc27xx_pd_set_current_limit(struct tcpc_dev *dev, u32 max_ma, u32 mv)
{
	return 0;
}

static int sc27xx_pd_get_current_limit(struct tcpc_dev *dev)
{
	return 0;
}

static int sc27xx_pd_set_polarity(struct tcpc_dev *tcpc,
				  enum typec_cc_polarity polarity)
{
	return 0;
}

static int sc27xx_pd_set_roles(struct tcpc_dev *tcpc, bool attached,
			       enum typec_role role, enum typec_data_role data)
{
	struct sc27xx_pd *pd = tcpc_to_sc27xx_pd(tcpc);
	int ret;
	u32 mask;

	mutex_lock(&pd->lock);
	pd->role = role;
	pd->data = data;
	pd->attached = attached;
	if (pd->role == TYPEC_SINK)
		mask = (u32)~SC27XX_PD_SRC_SINK_MODE;
	else
		mask = SC27XX_PD_SRC_SINK_MODE;

	ret = regmap_update_bits(pd->regmap,
				 pd->base + SC27XX_PD_CFG0,
				 SC27XX_PD_SRC_SINK_MODE, mask);
	mutex_unlock(&pd->lock);
	return ret;
}

static int sc27xx_pd_set_vconn(struct tcpc_dev *tcpc, bool enable)
{
	struct sc27xx_pd *pd = tcpc_to_sc27xx_pd(tcpc);
	int ret = 0;


	if (!pd->vconn) {
		dev_warn(pd->dev, "vconn NULL!!!\n");
		return -EINVAL;
	}

	mutex_lock(&pd->lock);

	if (pd->vconn_on == enable) {
		dev_info(pd->dev, "vconn already %s\n", enable ? "On" : "Off");
		goto unlock;
	}

	if (enable)
		ret = regulator_enable(pd->vconn);
	else
		ret = regulator_disable(pd->vconn);

	if (!ret)
		pd->vconn_on = enable;

unlock:
	mutex_unlock(&pd->lock);

	return ret;
}

static int sc27xx_pd_tx_flush(struct sc27xx_pd *pd)
{
	return regmap_update_bits(pd->regmap,
				  pd->base + SC27XX_PD_CTRL, SC27XX_PD_TX_FLASH,
				  SC27XX_PD_TX_FLASH);
}

static int sc27xx_pd_rx_flush(struct sc27xx_pd *pd)
{
	return regmap_update_bits(pd->regmap,
				  pd->base + SC27XX_PD_CTRL, SC27XX_PD_RX_FLASH,
				  SC27XX_PD_RX_FLASH);
}

static int sc27xx_pd_reset(struct sc27xx_pd *pd)
{
	int ret;

	ret = sc27xx_pd_rx_flush(pd);
	if (ret < 0)
		return ret;

	ret = sc27xx_pd_tx_flush(pd);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(pd->regmap,
				 pd->base + SC27XX_PD_CTRL, SC27XX_PD_RX_ID_CLR,
				 SC27XX_PD_RX_ID_CLR);
	if (ret < 0)
		return ret;

	return regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_MESG_ID_CFG,
				  SC27XX_PD_MESS_ID_MASK, 0x0);
}

static int sc27xx_pd_send_hardreset(struct sc27xx_pd *pd)
{
	int ret, state;

	ret = sc27xx_pd_reset(pd);
	if (ret < 0) {
		dev_err(pd->dev, "cannot PD reset, ret=%d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pd->regmap,
				 pd->base + SC27XX_PD_CTRL,
				 SC27XX_PD_HARD_RESET, SC27XX_PD_HARD_RESET);
	if (ret < 0)
		return ret;

	state = extcon_get_state(pd->edev, EXTCON_CHG_USB_PD);
	if (state == true)
		extcon_set_state_sync(pd->edev, EXTCON_CHG_USB_PD, false);
	else if (state == false)
		extcon_set_state_sync(pd->edev, EXTCON_CHG_USB_PD, true);

	dev_warn(pd->dev, "IRQ: PD send hardreset\n");

	return 0;
}

static int sc27xx_pd_set_rx(struct tcpc_dev *tcpc, bool on)
{
	struct sc27xx_pd *pd = tcpc_to_sc27xx_pd(tcpc);
	u32 mask = SC27XX_PD_CTL_EN, mask1 = SC27XX_PD_PKG_RV_EN;
	u32 mask2 = SC27XX_PD_RX_AUTO_GOOD_CRC;
	int ret;

	if (pd->shutdown_flag)
		return 0;

	mutex_lock(&pd->lock);
	ret = sc27xx_pd_reset(pd);
	if (ret < 0)
		goto done;

	if (on) {
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_EN,
					 mask1, mask1);
		if (ret < 0)
			goto done;

		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_CFG1,
					 mask2, mask2);
		if (ret < 0)
			goto done;

		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_CFG0,
					 mask, mask);
		if (ret < 0)
			goto done;
	} else {
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_CFG0,
					 mask, ~mask);
		if (ret < 0)
			goto done;

		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_EN,
					 mask1, ~mask1);
		if (ret < 0)
			goto done;

		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_CFG1,
					 mask2, ~mask2);
		if (ret < 0)
			goto done;
	}

	dev_info(pd->dev, "pd := %s", on ? "on" : "off");
done:
	mutex_unlock(&pd->lock);
	return ret;
}

static int sc27xx_pd_tx_msg(struct sc27xx_pd *pd, const struct pd_message *msg)
{
	u16 header;
	u32 data_obj_num, data[PD_MAX_PAYLOAD * 2];
	int i, ret;

	ret = sc27xx_pd_tx_flush(pd);
	if (ret < 0)
		return ret;

	data_obj_num = msg ? pd_header_cnt_le(msg->header) : 0;
	if (data_obj_num > PD_MAX_PAYLOAD) {
		dev_err(pd->dev, "pd tmsg too long, num=%d\n", data_obj_num);
		return -EINVAL;
	}

	header = msg ? le16_to_cpu(msg->header) : 0;
	ret = regmap_write(pd->regmap, pd->base + SC27XX_PD_HEAD_CFG, header);
	if (ret < 0)
		return ret;

	if (msg) {
		for (i = 0; i < data_obj_num; i++) {
			data[2 * i] = le32_to_cpu(msg->payload[i]) &
			SC27XX_PD_DATA_MASK;
			data[2 * i + 1] = (le32_to_cpu(msg->payload[i]) >> 16) &
			SC27XX_PD_DATA_MASK;
		}
	}

	for (i = 0; i < data_obj_num * 2; i++) {
		ret = regmap_write(pd->regmap, pd->base + SC27XX_PD_TX_BUF,
				   data[i]);
		if (ret < 0)
			return ret;
	}

	return regmap_update_bits(pd->regmap,
				  pd->base + SC27XX_PD_CTRL, SC27XX_PD_TX_START,
				  SC27XX_PD_TX_START);
}

static int sc27xx_pd_transmit(struct tcpc_dev *tcpc,
			      enum tcpm_transmit_type type,
			      const struct pd_message *msg)
{
	struct sc27xx_pd *pd = tcpc_to_sc27xx_pd(tcpc);
	int ret;

	mutex_lock(&pd->lock);
	switch (type) {
	case TCPC_TX_SOP:
		ret = sc27xx_pd_tx_msg(pd, msg);
		if (ret < 0)
			dev_err(pd->dev, "cannot send PD message, ret=%d\n",
				ret);
		break;
	case TCPC_TX_HARD_RESET:
		ret = sc27xx_pd_send_hardreset(pd);
		if (ret < 0)
			dev_err(pd->dev, "cann't send hardreset ret=%d\n", ret);
		break;
	default:
		dev_err(pd->dev, "type %d not supported", type);
		ret = -EINVAL;
	}
	mutex_unlock(&pd->lock);

	return ret;
}

static int sc27xx_pd_read_message(struct sc27xx_pd *pd, struct pd_message *msg)
{
	int ret, i;
	u32 data[PD_MAX_PAYLOAD * 2] = {0};
	u32 data_obj_num, spec, reg_val = 0, header = 0, type = 0;
	bool vendor_define = false, source_capabilities = false;

	#ifdef CONFIG_DEBUG_FS
	ret = regmap_read(pd->regmap, pd->base + SC27XX_PD_STS1, &reg_val);
	if (ret < 0) {

		sprd_pd_log(pd, "failed to read SC27XX_PD_STS1, ret = 0x%x", ret);

	}
	reg_val &= SC27XX_PD_RX_DATA_NUM_MASK;

	sprd_pd_log(pd, "SC27XX_PD_STS1(0x20) = 0x%x", reg_val);
	#endif

	ret = regmap_read(pd->regmap, pd->base + SC27XX_PD_RX_BUF,
			  &header);
	if (ret < 0) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "failed to read header, ret = 0x%x", ret);
		#endif
		return ret;
	}

	#ifdef CONFIG_DEBUG_FS
	sprd_pd_log(pd, "SC27XX_PD_RX_BUF = 0x%x", header);
	if (reg_val == 2) {
		ret = regmap_read(pd->regmap, pd->base + SC27XX_PD_RX_BUF,
				  &reg_val);
		if (ret < 0) {
			#ifdef CONFIG_DEBUG_FS
			sprd_pd_log(pd, "failed to read reg_val, ret = 0x%x", ret);
			#endif
			return ret;
		}
		sprd_pd_log(pd, "SC27XX_PD_RX_BUF(2) = 0x%x", reg_val);
	}
	#endif

	ret = regmap_read(pd->regmap, pd->base + SC27XX_PD_STS1, &reg_val);
	if (ret < 0) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "failed to read SC27XX_PD_STS1, ret = 0x%x", ret);
		#endif
	}
	#ifdef CONFIG_DEBUG_FS
	sprd_pd_log(pd, "SC27XX_PD_STS1(0x20) = 0x%x", reg_val);
	#endif

	if (pd->need_retry) {
		pd->need_retry = false;
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "one msg receive before schedule read mdg work");
		#endif
		cancel_delayed_work_sync(&pd->read_msg_work);
	}
	reg_val &= SC27XX_PD_RX_DATA_NUM_MASK;

	header &= SC27XX_TX_RX_BUF_MASK;
	msg->header = cpu_to_le16(header);
	data_obj_num = pd_header_cnt_le(msg->header);
	spec = pd_header_rev_le(msg->header);
	type = pd_header_type_le(msg->header);

	if (msg->header & PD_HEADER_EXT_HDR)
		vendor_define = false;
	else if (data_obj_num && (type == PD_DATA_VENDOR_DEF))
		vendor_define = true;
	else if (data_obj_num && (type == PD_DATA_SOURCE_CAP))
		source_capabilities = true;

	#ifdef CONFIG_DEBUG_FS
	sprd_pd_log(pd, "header = 0x%x, data_obj_num = 0x%x, spec = 0x%x, rx_data_cnt = %d",
		header, data_obj_num, spec, reg_val);
	#endif

	if ((data_obj_num * 2 + 1) < reg_val && !vendor_define && !source_capabilities) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "one more package, need read again, rx_data_num = %d", reg_val);
		#endif
		pd->need_retry = true;
		queue_delayed_work(pd->pd_wq,
				   &pd->read_msg_work,
				   msecs_to_jiffies(5));
	}

	if (spec == 1) {
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_CFG1,
					 SC27XX_PD_RETRY_MASK,
					 SC27XX_PD_RETRY(3));
		if (ret < 0) {
			#ifdef CONFIG_DEBUG_FS
			sprd_pd_log(pd, "failed to SC27XX_PD_RETRY(3), ret = 0x%x", ret);
			#endif
			return ret;
		}
	} else if (spec == 2) {
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_CFG1,
					 SC27XX_PD_RETRY_MASK,
					 SC27XX_PD_RETRY(1));
		if (ret < 0) {
			#ifdef CONFIG_DEBUG_FS
			sprd_pd_log(pd, "failed to SC27XX_PD_RETRY(1), ret = 0x%x", ret);
			#endif
			return ret;
		}
	}

	if (data_obj_num &&
	   pd_header_type_le(msg->header) == PD_DATA_VENDOR_DEF)
		pd->msg_flag = 1;
	else
		pd->msg_flag = 0;

	if (data_obj_num > PD_MAX_PAYLOAD) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "pd rmesg too long, num=%d", data_obj_num);
		#endif
		dev_err(pd->dev, "pd rmesg too long, num=%d\n", data_obj_num);
		return -EINVAL;
	}

	for (i = 0; i < data_obj_num * 2; i++) {
		ret = regmap_read(pd->regmap, pd->base + SC27XX_PD_RX_BUF,
				  (u32 *)&data[i]);
		if (ret < 0) {
			#ifdef CONFIG_DEBUG_FS
			sprd_pd_log(pd, "failed to read data ret = 0x%x", ret);
			#endif
			return ret;
		}
	}

	/*
	 * According to the datasheet, sc27xx_pd_rx_buf is 16bit,
	 * but PD protocol source code msg->payload is 32bit,
	 * so need two 16bit assignment one 32bit.
	 */

	for (i = 0; i < data_obj_num; i++)
		msg->payload[i] = cpu_to_le32(data[2 * i + 1] << 16 |
				data[2 * i]);

	if (!data_obj_num &&
	    pd_header_type_le(msg->header) == PD_CTRL_GOOD_CRC) {
		if (!pd->constructed) {
			ret = regmap_update_bits(pd->regmap, pd->typec_base +
						 SC27XX_TYPC_PD_CFG,
						 SC27XX_TYPEC_PD_CONSTRACT,
						 SC27XX_TYPEC_PD_CONSTRACT);
			if (ret < 0)
				return ret;
			pd->constructed = true;
		}
		tcpm_pd_transmit_complete(pd->tcpm_port, TCPC_TX_SUCCESS);
	} else {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "report msg to tcpm");
		#endif
		tcpm_pd_receive(pd->tcpm_port, msg);
	}

	return sc27xx_pd_rx_flush(pd);
}

static int sc27xx_pd_rc_ref_cal(struct sc27xx_pd *pd)
{
	u32 vol = (pd->rc_cal >> 9)&0xfe;
	u32 pd_ref = (pd->ref_cal >> 12)&0x7;
	u32 val = 0;
	u32 cfg2_bit7, cfg0_vth = 0, cfg0_vtl = 0, typec_ibis = 0;
	u32 cfg0, cfg2, cfg0_mask, cfg2_mask;
	int ret;

	/*
	 * According to the datasheet, depending on the calibration
	 * voltage, different register values should be configured.
	 */

	if (vol >= 0 && vol <= 26)
		val = 0x0;
	if (vol >= 27 && vol <= 37)
		val = 0x1;
	if (vol >= 38 && vol <= 48)
		val = 0x2;
	if (vol >= 49 && vol <= 59)
		val = 0x3;
	if (vol >= 60 && vol <= 69)
		val = 0x4;
	if (vol >= 70 && vol <= 80)
		val = 0x5;
	if (vol >= 81 && vol <= 90)
		val = 0x6;
	if (vol >= 91 && vol <= 127)
		val = 0x7;

	switch (pd_ref) {
	case 0:
		cfg2_bit7 = 0;
		cfg0_vth = 0;
		cfg0_vtl = 2;
		typec_ibis = 0;
		break;
	case 1:
		cfg2_bit7 = 0;
		cfg0_vth = 1;
		cfg0_vtl = 1;
		typec_ibis = 1;
		break;
	case 2:
		cfg2_bit7 = 0;
		cfg0_vth = 1;
		cfg0_vtl = 1;
		typec_ibis = 0;
		break;
	case 3:
		cfg2_bit7 = 1;
		cfg0_vth = 1;
		cfg0_vtl = 1;
		typec_ibis = 0;
		break;
	case 4:
		cfg2_bit7 = 0;
		cfg0_vth = 2;
		cfg0_vtl = 0;
		typec_ibis = 0;
		break;
	}

	if (typec_ibis == 1)
		ret = regmap_update_bits(pd->regmap,
					 pd->typec_base + SC27XX_TYPEC_IBIAS,
					 SC27XX_TYPEC_RX_REF_DECREASE,
					 SC27XX_TYPEC_RX_REF_DECREASE);
	else
		ret = regmap_update_bits(pd->regmap,
					 pd->typec_base + SC27XX_TYPEC_IBIAS,
					 SC27XX_TYPEC_RX_REF_DECREASE, 0);

	cfg0 = (cfg0_vtl << SC27XX_PD_CFG0_VTL_SHIFT) & SC27XX_PD_CFG0_VTL_MASK;
	cfg0 |= (val << SC27XX_PD_CFG0_RCCAL_SHIFT) & SC27XX_PD_CFG0_RCCAL_MASK;
	cfg0 |= cfg0_vth & SC27XX_PD_CFG0_VTH_MASK;
	cfg0_mask = SC27XX_PD_CFG0_RCCAL_MASK |
		    SC27XX_PD_CFG0_VTL_MASK | SC27XX_PD_CFG0_VTH_MASK;

	ret = regmap_update_bits(pd->regmap,
				 pd->base + SC27XX_PD_PHY_CFG0,
				 cfg0_mask, cfg0);
	if (ret < 0)
		return ret;

	cfg2 = (cfg2_bit7 << SC27XX_PD_CFG2_RX_REF_CAL_SHIFT) & 0x80;
	cfg2 |= SC27XX_PD_CFG2_PD_CLK_BIT;
	cfg2_mask = SC27XX_PD_CFG2_PD_CLK_BIT | SC27XX_PD_CFG2_RX_REF_CAL_BIT;

	return regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_PHY_CFG2,
				  cfg2_mask, cfg2);
}

static int sc27xx_pd_delta_cal(struct sc27xx_pd *pd)
{
	u32 delta_cal = pd->delta_cal;
	u32 vol, vref_sel = 0, ref_cal = 0, delta;
	u32 cfg1, cfg1_mask;

	cfg1_mask = SC27XX_PD_CC1_SW | SC27XX_PD_CC2_SW |
		    SC27XX_PD_CFG1_VREF_SEL_MASK |
		    SC27XX_PD_CFG1_REF_CAL_MASK;

	delta = ((delta_cal & 0x3F80) >> 7);
	/*
	 * According to the datasheet, delta is efuse caliration
	 * vol = delta * 2 + 1000
	 */
	vol = delta * 2 + 1000;

	/*
	 * According to the datasheet, depending on the calibration
	 * voltage, different register values should be configured.
	 */

	if (vol >= 1185 && vol <= 1195) {
		vref_sel = 0x0;
		ref_cal = 0x0;
	} else if (vol >= 1175 && vol <= 1185) {
		vref_sel = 0x0;
		ref_cal = 0x1;
	} else if (vol >= 1165 && vol <= 1175) {
		vref_sel = 0x0;
		ref_cal = 0x2;
	} else if (vol >= 1155 && vol <= 1165) {
		vref_sel = 0x0;
		ref_cal = 0x3;
	} else if (vol >= 1145 && vol <= 1155) {
		vref_sel = 0x0;
		ref_cal = 0x4;
	} else if (vol >= 1135 && vol <= 1145) {
		vref_sel = 0x0;
		ref_cal = 0x5;
	} else if (vol >= 1125 && vol <= 1135) {
		vref_sel = 0x0;
		ref_cal = 0x6;
	} else if (vol >= 1115 && vol <= 1125) {
		vref_sel = 0x1;
		ref_cal = 0x2;
	} else if (vol >= 1105 && vol <= 1115) {
		vref_sel = 0x1;
		ref_cal = 0x3;
	} else if (vol >= 1095 && vol <= 1105) {
		vref_sel = 0x1;
		ref_cal = 0x4;
	} else if (vol >= 1085 && vol <= 1095) {
		vref_sel = 0x1;
		ref_cal = 0x5;
	} else if (vol >= 1075 && vol <= 1085) {
		vref_sel = 0x1;
		ref_cal = 0x6;
	} else if (vol >= 1065 && vol <= 1075) {
		vref_sel = 0x2;
		ref_cal = 0x2;
	} else if (vol >= 1055 && vol <= 1065) {
		vref_sel = 0x2;
		ref_cal = 0x3;
	} else if (vol >= 1045 && vol <= 1055) {
		vref_sel = 0x2;
		ref_cal = 0x4;
	} else if (vol >= 1035 && vol <= 1045) {
		vref_sel = 0x2;
		ref_cal = 05;
	} else if (vol >= 1025 && vol <= 1035) {
		vref_sel = 0x2;
		ref_cal = 0x6;
	} else if (vol >= 1015 && vol <= 1025) {
		vref_sel = 0x3;
		ref_cal = 0x2;
	} else if (vol >= 1005 && vol <= 1015) {
		vref_sel = 0x3;
		ref_cal = 0x3;
	} else if (vol >= 995 && vol <= 1005) {
		vref_sel = 0x3;
		ref_cal = 0x4;
	} else if (vol >= 985 && vol <= 995) {
		vref_sel = 0x2;
		ref_cal = 0x6;
	} else if (vol >= 975 && vol <= 985) {
		vref_sel = 0x2;
		ref_cal = 0x6;
	} else if (vol >= 965 && vol <= 975) {
		vref_sel = 0x2;
		ref_cal = 0x7;
	}

	cfg1 = SC27XX_PD_CC1_SW | SC27XX_PD_CC2_SW;
	cfg1 |= (vref_sel << SC27XX_PD_CFG1_VREF_SEL_SHIFT)
		& SC27XX_PD_CFG1_VREF_SEL_MASK;
	cfg1 |= (ref_cal << SC27XX_PD_CFG1_REF_CAL_SHIFT)
		& SC27XX_PD_CFG1_REF_CAL_MASK;

	return regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_PHY_CFG1,
				  cfg1_mask, cfg1);
}

static int sc27xx_pd_module_init(struct sc27xx_pd *pd)
{
	int ret;
	u32 mask2 = SC27XX_PD_RX_AUTO_GOOD_CRC;

	ret = sc27xx_pd_delta_cal(pd);
	if (ret < 0)
		return ret;

	ret = sc27xx_pd_rc_ref_cal(pd);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_CFG1,
				 SC27XX_PD_PHY_13M | SC27XX_PD_RETRY(3),
				 SC27XX_PD_PHY_13M | SC27XX_PD_RETRY(3));
	if (ret < 0)
		return ret;
	ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_MESG_ID_CFG,
				 SC27XX_PD_MESS_ID_MASK, 0x0);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(pd->regmap,
				 pd->base + SC27XX_PD_CFG0,
				 SC27XX_PD_BIST_MODE_EN,
				 SC27XX_PD_BIST_MODE_EN);
	if (ret < 0)
		return ret;

	ret = regmap_write(pd->regmap, pd->base + SC27XX_INT_CLR,
			   SC27XX_INT_CLR_MASK);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_CFG1,
				 mask2, ~mask2);
	if (ret < 0)
		return ret;

	return regmap_write(pd->regmap, pd->base + SC27XX_INT_EN,
			    SC27XX_INT_EN_MASK);
}

static int sc27xx_pd_init(struct tcpc_dev *tcpc)
{
	struct sc27xx_pd *pd = tcpc_to_sc27xx_pd(tcpc);
	int ret;

	ret = sc27xx_pd_clk_cfg(pd);
	if (ret)
		return ret;

	return sc27xx_pd_module_init(pd);
}

static irqreturn_t sc27xx_pd_irq(int irq, void *dev_id)
{
	struct sc27xx_pd *pd = dev_id;
	struct pd_message pd_msg;
	u32 status, pd_sts1;
	int ret, state;

	#ifdef CONFIG_DEBUG_FS
	sprd_pd_log(pd, "interrupt occur");
	#endif

	if (pd->shutdown_flag) {
		/* Clear all the INT FLAG */
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_CLR,
					 SC27XX_INT_CLR_MASK,
					 SC27XX_INT_CLR_MASK);
		dev_info(pd->dev, "SC27XX_INT_FLG(0x28)=0x%x, ret=%d ->: return!!!", status, ret);
		goto done;
	}

	mutex_lock(&pd->lock);
	ret = regmap_read(pd->regmap, pd->base + SC27XX_INT_FLG, &status);
	if (ret < 0) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "failed to read status, ret = 0x%x", ret);
		#endif
		goto done;
	}

	#ifdef CONFIG_DEBUG_FS
	sprd_pd_log(pd, "interrupt status = 0x%x", status);
	#endif

	if (status & SC27XX_PD_PS_RDY_FLAG) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "ps ready########");
		#endif
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_CLR,
						 SC27XX_PD_PS_RDY_CLR,
						 SC27XX_PD_PS_RDY_CLR);
			if (ret < 0) {
				#ifdef CONFIG_DEBUG_FS
				sprd_pd_log(pd, "failed to clear ps ready int status, ret = 0x%x", ret);
				#endif
				goto done;
			}
	}

	if (status & SC27XX_PD_HARD_RST_FLAG) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "pd hard reset");
		#endif
		dev_warn(pd->dev, "IRQ: PD received hardreset");
		ret = sc27xx_pd_reset(pd);
		if (ret < 0) {
			dev_err(pd->dev, "cannot PD reset, ret=%d\n", ret);
			goto done;
		}

		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_CLR,
					 SC27XX_PD_HARD_RST_RV_CLR,
					 SC27XX_PD_HARD_RST_RV_CLR);
		if (ret < 0)
			goto done;

		state = extcon_get_state(pd->edev, EXTCON_CHG_USB_PD);
		if (state == true)
			extcon_set_state_sync(pd->edev, EXTCON_CHG_USB_PD, false);
		else if (state == false)
			extcon_set_state_sync(pd->edev, EXTCON_CHG_USB_PD, true);

		tcpm_pd_hard_reset(pd->tcpm_port);
	}

	if (status & SC27XX_PD_CABLE_RST_FLAG) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "IRQ: PD cable rst flag");
		#endif
		dev_warn(pd->dev, "IRQ: PD cable rst flag\n");
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_CLR,
					 SC27XX_PD_CABLE_RST_RV_CLR,
					 SC27XX_PD_CABLE_RST_RV_CLR);
		if (ret < 0)
			goto done;
	}

	if (status & SC27XX_PD_SOFT_RST_FLAG) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "SC27XX_PD_SOFT_RST_FLAG");
		#endif
		ret = sc27xx_pd_reset(pd);
		if (ret < 0) {
			dev_err(pd->dev, "cannot PD reset, ret=%d\n", ret);
			goto done;
		}

		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_CLR,
					 SC27XX_PD_SOFT_RST_RV_CLR,
					 SC27XX_PD_SOFT_RST_RV_CLR);
		if (ret < 0)
			goto done;
	}

	if ((status & SC27XX_PD_PKG_RV_FLAG)) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "receive data interrupt");
		#endif
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_CLR,
					 SC27XX_PD_PKG_RV_CLR,
					 SC27XX_PD_PKG_RV_CLR);
		if (ret < 0) {
			#ifdef CONFIG_DEBUG_FS
			sprd_pd_log(pd, "failed to clear rx int status, ret = 0x%x", ret);
			#endif
			goto done;
		}

	       /*
		* According to the requirements of ASIC spec, after receiving
		* the interrupt,the data should be read after 500us.
		*/
		udelay(500);
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "start to read rx status");
		#endif
		ret = regmap_read_poll_timeout(pd->regmap,
					      pd->base + SC27XX_PD_STS1,
					      pd_sts1,
					      (pd_sts1 & (~SC27XX_PD_RX_EMPTY)),
					      SC27XX_PD_POLL_RAW_STATUS,
					      SC27XX_PD_RDY_TIMEOUT);
		if (ret < 0) {
			#ifdef CONFIG_DEBUG_FS
			sprd_pd_log(pd, "failed to read rx status, ret = 0x%x", ret);
			#endif
			goto done;
		}

		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "start to read data==============");
		#endif
		ret = sc27xx_pd_read_message(pd, &pd_msg);
		if (ret < 0) {
			#ifdef CONFIG_DEBUG_FS
			sprd_pd_log(pd, "failed to read rx data, ret = 0x%x", ret);
			#endif
			dev_err(pd->dev, "not read PD msg, ret=%d\n", ret);
			goto done;
		}
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "end to read data============== status = 0x%x", status);
		#endif
	}

	if (status & SC27XX_PD_TX_OK_FLAG) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "SC27XX_PD_TX_OK_FLAG");
		#endif
		tcpm_pd_transmit_complete(pd->tcpm_port, TCPC_TX_SUCCESS);
		ret = regmap_update_bits(pd->regmap,
					 pd->base + SC27XX_INT_CLR,
					 SC27XX_PD_TX_OK_CLR,
					 SC27XX_PD_TX_OK_CLR);
		if (ret < 0)
			goto done;
	}

	if (status & SC27XX_PD_TX_ERROR_FLAG) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "IRQ: tx error failed");
		#endif
		dev_err(pd->dev, "IRQ: tx error failed\n");
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_CLR,
					 SC27XX_PD_TX_ERROR_CLR,
					 SC27XX_PD_TX_ERROR_CLR);
		if (ret < 0)
			goto done;
		tcpm_pd_transmit_complete(pd->tcpm_port, TCPC_TX_FAILED);
	}

	if (status & SC27XX_PD_TX_COLLSION_FLAG) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "IRQ: PD collision");
		#endif
		dev_err(pd->dev, "IRQ: PD collision\n");
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_CLR,
					 SC27XX_PD_TX_COLLSION_CLR,
					 SC27XX_PD_TX_COLLSION_CLR);
		if (ret < 0)
			goto done;

		tcpm_pd_transmit_complete(pd->tcpm_port, TCPC_TX_FAILED);
	}

	if (status & SC27XX_PD_PKG_RV_ERROR_FLAG) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "IRQ: PD rx error flag");
		#endif
		dev_err(pd->dev, "IRQ: PD rx error flag");
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_CLR,
					 SC27XX_PD_PKG_RV_ERROR_CLR,
					 SC27XX_PD_PKG_RV_ERROR_CLR);
		if (ret < 0)
			goto done;
	}

	if (status & SC27XX_PD_FRS_RV_FLAG) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "SC27XX_PD_FRS_RV_FLAG");
		#endif
		ret = regmap_update_bits(pd->regmap,
					 pd->base + SC27XX_INT_CLR,
					 SC27XX_PD_FRS_RV_CLR,
					 SC27XX_PD_FRS_RV_CLR);
		if (ret < 0)
			goto done;
	}

	if (status & SC27XX_PD_RX_FIFO_OVERFLOW_FLAG) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "IRQ: PD rx fifo overflow flag");
		#endif
		dev_err(pd->dev, "IRQ: PD rx fifo overflow flag\n");
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_INT_CLR,
					 SC27XX_PD_RX_FIFO_OVERFLOW_CLR,
					 SC27XX_PD_RX_FIFO_OVERFLOW_CLR);
		if (ret < 0)
			goto done;
	}

done:
	mutex_unlock(&pd->lock);

	return IRQ_HANDLED;
}

static int sc27xx_get_vbus_status(struct sc27xx_pd *pd)
{
	u32 status;
	bool vbus_present;
	int ret;

	ret = regmap_read(pd->regmap, pd->typec_base +
			  SC27XX_TYPEC_DBG1, &status);
	if (ret < 0)
		return ret;

	vbus_present = !!(status & SC27XX_TYPEC_VBUS_OK);
	if (vbus_present != pd->vbus_present) {
		pd->vbus_present = vbus_present;
		tcpm_vbus_change(pd->tcpm_port);
	}

	return 0;
}

static void sc27xx_cc_polarity_status(struct sc27xx_pd *pd, u32 status)
{
	if (status & SC27XX_TYPEC_FINAL_SWITCH)
		pd->cc_polarity = TYPEC_POLARITY_CC1;
	else
		pd->cc_polarity = TYPEC_POLARITY_CC2;
}

static void sc27xx_cc_status(struct sc27xx_pd *pd, u32 status)
{
	u32 cc_rp, rp_sts = SC27XX_TYPEC_VBUS_CL(status);

	switch (rp_sts) {
	case 0:
		cc_rp = TYPEC_CC_RP_DEF;
		break;
	case 1:
		cc_rp = TYPEC_CC_RP_1_5;
		break;
	case 2:
		cc_rp = TYPEC_CC_RP_3_0;
		break;
	default:
		cc_rp = TYPEC_CC_OPEN;
		break;
	}

	switch (pd->state) {
	case SC27XX_ATTACHED_SNK:
		if (pd->cc_polarity == TYPEC_POLARITY_CC1) {
			pd->cc1 = cc_rp;
			pd->cc2 = TYPEC_CC_OPEN;
		} else {
			pd->cc1 = TYPEC_CC_OPEN;
			pd->cc2 = cc_rp;
		}
		break;

	case SC27XX_ATTACHED_SRC:
		if (pd->cc_polarity == TYPEC_POLARITY_CC1) {
			pd->cc1 = TYPEC_CC_RD;
			pd->cc2 = TYPEC_CC_OPEN;
		} else {
			pd->cc1 = TYPEC_CC_OPEN;
			pd->cc2 = TYPEC_CC_RD;
		}
		break;

	case SC27XX_POWERED_CABLE:
		if (pd->cc_polarity == TYPEC_POLARITY_CC1) {
			pd->cc1 = TYPEC_CC_RD;
			pd->cc2 = TYPEC_CC_RA;
		} else {
			pd->cc1 = TYPEC_CC_RA;
			pd->cc2 = TYPEC_CC_RD;
		}
		break;
	default:
		pd->cc1 = TYPEC_CC_OPEN;
		pd->cc2 = TYPEC_CC_OPEN;
		break;
	}

	tcpm_cc_change(pd->tcpm_port);
}

static int sc27xx_pd_check_vbus_cc_status(struct sc27xx_pd *pd)
{
	u32 val;
	int ret;

	ret = regmap_read(pd->regmap, pd->typec_base + SC27XX_TYPEC_STATUS,
			  &val);
	if (ret) {
		dev_err(pd->dev, "%s regmap_read fail ret:%d\n", __func__, ret);
		return ret;
	}

	pd->state = val & SC27XX_STATE_MASK;
	sc27xx_cc_polarity_status(pd, val);
	sc27xx_cc_status(pd, val);
	sc27xx_get_vbus_status(pd);

	return 0;
}

static int sc27xx_pd_extcon_event(struct notifier_block *nb,
				  unsigned long event, void *param)
{
	struct sc27xx_pd *pd = container_of(nb, struct sc27xx_pd, extcon_nb);

	dev_info(pd->dev, "typec in or out, pd attached = %d\n", pd->pd_attached);
	if (pd->pd_attached)
		schedule_work(&pd->pd_work);
	return NOTIFY_OK;
}

static void sc27xx_pd_work(struct work_struct *work)
{
	struct sc27xx_pd *pd = container_of(work, struct sc27xx_pd, pd_work);
	int ret;

	dev_info(pd->dev, "check vbus and cc\n");
	ret = sc27xx_pd_check_vbus_cc_status(pd);
	if (ret)
		dev_err(pd->dev, "failed to check vbus and cc status\n");
}

static void sc27xx_init_tcpc_dev(struct sc27xx_pd *pd)
{
	pd->tcpc.config = &pd->config;
	pd->tcpc.init = sc27xx_pd_init;
	pd->tcpc.get_vbus = sc27xx_pd_get_vbus;
	pd->tcpc.get_current_limit = sc27xx_pd_get_current_limit;
	pd->tcpc.set_cc = sc27xx_pd_set_cc;
	pd->tcpc.get_cc = sc27xx_pd_get_cc;
	pd->tcpc.set_polarity = sc27xx_pd_set_polarity;
	pd->tcpc.set_vconn = sc27xx_pd_set_vconn;
	pd->tcpc.set_vbus = sc27xx_pd_set_vbus;
	pd->tcpc.set_current_limit = sc27xx_pd_set_current_limit;
	pd->tcpc.set_pd_rx = sc27xx_pd_set_rx;
	pd->tcpc.set_roles = sc27xx_pd_set_roles;
	pd->tcpc.start_drp_toggling = sc27xx_pd_start_drp_toggling;
	pd->tcpc.pd_transmit = sc27xx_pd_transmit;
}

static int sc27xx_pd_efuse_read(struct sc27xx_pd *pd,
				const char *cell_id, u32 *val)
{
	struct nvmem_cell *cell;
	void *buf;
	size_t len = 0;

	cell = nvmem_cell_get(pd->dev, cell_id);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	memcpy(val, buf, min(len, sizeof(u16)));

	kfree(buf);
	return 0;
}

static int sc27xx_pd_cal(struct sc27xx_pd *pd)
{
	int ret;

	ret = sc27xx_pd_efuse_read(pd, "pdrc_calib", &pd->rc_cal);
	if (ret)
		return ret;

	ret = sc27xx_pd_efuse_read(pd, "pddelta_calib", &pd->delta_cal);
	if (ret)
		return ret;

	return sc27xx_pd_efuse_read(pd, "pdref_calib", &pd->ref_cal);
}

static void sc27xx_pd_read_msg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sc27xx_pd *pd = container_of(dwork, struct sc27xx_pd,
					   read_msg_work);

	struct pd_message pd_msg;
	int ret;

	#ifdef CONFIG_DEBUG_FS
	sprd_pd_log(pd, "sc27xx_pd_read_msg_work enter");
	#endif

	mutex_lock(&pd->lock);

	if (!pd->need_retry) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "receive msg again during this period");
		#endif
		goto out;
	}

	pd->need_retry = false;
	#ifdef CONFIG_DEBUG_FS
	sprd_pd_log(pd, "sc27xx_pd_read_msg_work start to read data==============");
	#endif

	ret = sc27xx_pd_read_message(pd, &pd_msg);
	if (ret < 0) {
		#ifdef CONFIG_DEBUG_FS
		sprd_pd_log(pd, "failed to read rx data, ret = 0x%x", ret);
		#endif
	}
	#ifdef CONFIG_DEBUG_FS
	sprd_pd_log(pd, "sc27xx_pd_read_msg_work end to read data==============");
	#endif
out:
	mutex_unlock(&pd->lock);

}

static void sc27xx_pd_detect_typec_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sc27xx_pd *pd = container_of(dwork, struct sc27xx_pd,
					   typec_detect_work);
	int extcon_state = 0;

	extcon_state = extcon_get_state(pd->extcon, EXTCON_USB);

	dev_info(pd->dev, "pd try to detect typec extcon, pd_attached:%d,"
		"shutdown_flag:%d, pps_type:%d, extcon_state:%d\n",
		pd->pd_attached, pd->shutdown_flag, sc2730_fchg_get_pps_type(), extcon_state);

	if (pd->pd_attached == false) {
		if (extcon_state) {
			sc27xx_pd_check_vbus_cc_status(pd);
			dev_info(pd->dev, "sc27xx_pd_check_vbus_cc_status first\n");
		}

		schedule_delayed_work(&pd->typec_detect_work,
				  msecs_to_jiffies(SC27xx_SECOND_DETECT_TYPEC_DELAY));

		pd->pd_attached = true;
	} else {
		if ((sc2730_fchg_get_pps_type() == false)
				&& (pd->shutdown_flag == false)
				&& (extcon_state)) {
			sc27xx_pd_check_vbus_cc_status(pd);
			dev_info(pd->dev, "sc27xx_pd_check_vbus_cc_status twice\n");
		}
	}
}

static const u32 sc27xx_pd_hardreset[] = {
	EXTCON_CHG_USB_PD,
	EXTCON_NONE,
};

static int sc27xx_pd_probe(struct platform_device *pdev)
{
	struct sc27xx_pd *pd;
	int pd_irq, ret;

	pd = devm_kzalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		dev_err(&pdev->dev, "failed to alloc memory\n");
		return -ENOMEM;
	}

	pd->dev = &pdev->dev;
	pd->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pd->regmap) {
		dev_err(&pdev->dev, "failed to get pd regmap\n");
		return -ENODEV;
	}

	pd->edev = devm_extcon_dev_allocate(&pdev->dev, sc27xx_pd_hardreset);
	if (IS_ERR(pd->edev)) {
		dev_err(&pdev->dev, "failed to allocate extcon device\n");
		return PTR_ERR(pd->edev);
	}

	ret = devm_extcon_dev_register(&pdev->dev, pd->edev);
	if (ret < 0) {
		dev_err(&pdev->dev, "can't register extcon device: %d\n", ret);
		return ret;
	}

	if (of_property_read_bool(pdev->dev.of_node, "extcon")) {
		pd->extcon = extcon_get_edev_by_phandle(&pdev->dev, 0);
		if (IS_ERR(pd->extcon)) {
			dev_err(&pdev->dev, "failed to find extcon device.\n");
			return PTR_ERR(pd->extcon);
		}

		pd->extcon_nb.notifier_call = sc27xx_pd_extcon_event;
		ret = devm_extcon_register_notifier_all(&pdev->dev,
							pd->extcon,
							&pd->extcon_nb);
		if (ret) {
			dev_err(&pdev->dev, "Can't register extcon\n");
			goto register_notifier_err;
		}
	}

	ret = of_property_read_u32_index(pdev->dev.of_node, "reg", 0,
					&pd->base);
	if (ret) {
		dev_err(&pdev->dev, "failed to get pd reg address\n");
		goto prop_read_err;
	}

	ret = of_property_read_u32_index(pdev->dev.of_node, "reg", 1,
					&pd->typec_base);
	if (ret) {
		dev_err(&pdev->dev, "failed to get typec reg address\n");
		goto prop_read_err;
	}

	pd_irq = platform_get_irq(pdev, 0);
	if (pd_irq < 0) {
		ret = pd_irq;
		dev_err(&pdev->dev, "failed to get pd irq number\n");
		goto prop_read_err;
	}

	mutex_init(&pd->lock);
	#ifdef CONFIG_DEBUG_FS
	mutex_init(&pd->logbuffer_lock);
	mutex_init(&pd->logprintk_lock);
	#endif
	pd->vbus_present = false;
	pd->constructed = false;
	pd->tcpc.config = &pd->config;
	pd->tcpc.fwnode = device_get_named_child_node(&pdev->dev, "connector");

	ret = sc27xx_pd_cal(pd);
	if (ret) {
		dev_err(&pdev->dev, "failed to calibrate with efuse data\n");
		goto prop_read_err;
	}
	sc27xx_init_tcpc_dev(pd);

	pd->vbus = devm_regulator_get(pd->dev, "vbus");
	if (IS_ERR(pd->vbus)) {
		dev_err(&pdev->dev, "pd failed to get vbus\n");
		return PTR_ERR(pd->vbus);
	}

	pd->vconn = devm_regulator_get_optional(pd->dev, "vconn");
	if (IS_ERR(pd->vconn)) {
		ret = PTR_ERR(pd->vconn);
		if (ret == -ENODEV) {
			dev_warn(pd->dev, "unable to get vconn supply\n");
			pd->vconn = NULL;
		} else {
			dev_err(pd->dev, "failed to get vconn supply\n");
			goto prop_read_err;
		}
	}

	ret = devm_request_threaded_irq(pd->dev, pd_irq, NULL,
					sc27xx_pd_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_LOW,
					"sc27xx_pd", pd);
	if (ret < 0) {
		dev_err(pd->dev, "failed to request irq\n");
		goto prop_read_err;
	}

	pd->pd_wq = create_singlethread_workqueue("sprd_pd_driver");
	if (!pd->pd_wq) {
		dev_err(pd->dev, "failed to create_singlethread_workqueue\n");
		ret = PTR_ERR(pd->pd_wq);
		goto prop_read_err;
	}

	pd->tcpm_port = tcpm_register_port(pd->dev, &pd->tcpc);
	if (IS_ERR(pd->tcpm_port)) {
		dev_err(pd->dev, "failed to register tcpm port\n");
		destroy_workqueue(pd->pd_wq);
		ret = PTR_ERR(pd->tcpm_port);
		goto prop_read_err;
	}

	INIT_DELAYED_WORK(&pd->typec_detect_work, sc27xx_pd_detect_typec_work);
	INIT_DELAYED_WORK(&pd->read_msg_work, sc27xx_pd_read_msg_work);
	INIT_WORK(&pd->pd_work, sc27xx_pd_work);
	#ifdef CONFIG_DEBUG_FS
	INIT_DELAYED_WORK(&pd->log2printk, log_printk_work);
	#endif

	pd->pd_attached = false;
	pd->shutdown_flag = false;

	platform_set_drvdata(pdev, pd);

	wake_lock_init(&pd->pd_wakelock, WAKE_LOCK_SUSPEND, "sprd_pd");
	wake_lock_timeout(&pd->pd_wakelock, (msecs_to_jiffies(SC27xx_DETECT_TYPEC_DELAY)+2*HZ));
	schedule_delayed_work(&pd->typec_detect_work,
			      msecs_to_jiffies(SC27xx_DETECT_TYPEC_DELAY));
	#ifdef CONFIG_DEBUG_FS
	schedule_delayed_work(&pd->log2printk, msecs_to_jiffies(50000));
	#endif

	return 0;

prop_read_err:
	devm_extcon_unregister_notifier_all(&pdev->dev,
					    pd->extcon,
					    &pd->extcon_nb);

register_notifier_err:
	devm_extcon_dev_free(&pdev->dev, pd->edev);

	return ret;
}

static int sc27xx_pd_remove(struct platform_device *pdev)
{
	struct sc27xx_pd *pd = platform_get_drvdata(pdev);

	tcpm_unregister_port(pd->tcpm_port);
	return 0;
}

static void sc27xx_pd_shutdown(struct platform_device *pdev)
{
	int ret;

	struct sc27xx_pd *pd = platform_get_drvdata(pdev);

	pr_info("%s shutdown \n", __func__);
	if (!pd->tcpm_port) {
		dev_warn(pd->dev, "tcpm_port Null!!!\n");
		return;
	}

	tcpm_shutdown(pd->tcpm_port);

	ret = sc27xx_pd_set_rx(&pd->tcpc, false);
	if (ret) {
		dev_err(pd->dev, "failed to disable set_rx at shutdown, ret = %d\n", ret);
		return;
	}

	pd->shutdown_flag = true;

	cancel_delayed_work_sync(&pd->typec_detect_work);
	cancel_delayed_work_sync(&pd->read_msg_work);
	cancel_work_sync(&pd->pd_work);
	#ifdef CONFIG_DEBUG_FS
	cancel_delayed_work_sync(&pd->log2printk);
	#endif

	pr_info("%s shutdown end \n", __func__);

	return;
}

static const struct of_device_id sc27xx_pd_of_match[] = {
	{.compatible = "sprd,sc2730-pd"},
	{}
};

static struct platform_driver sc27xx_pd_driver = {
	.probe = sc27xx_pd_probe,
	.remove = sc27xx_pd_remove,
	.driver = {
		.name = "sc27xx-typec-pd",
		.of_match_table = sc27xx_pd_of_match,
	},
	.shutdown = sc27xx_pd_shutdown,
};

module_platform_driver(sc27xx_pd_driver);

MODULE_AUTHOR("Freeman Liu <freeman.liu@spreadtrum.com>");
MODULE_DESCRIPTION("Spreadtrum SC27xx typec driver");
MODULE_LICENSE("GPL v2");
