#define DRIVER_NAME "sx932x"
#define MAX_WRITE_ARRAY_SIZE 32

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/syscalls.h>
#include <linux/wakelock.h>
#include <linux/uaccess.h>
#include <linux/sort.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>


#include "sx932x.h"
/* main struct,  interrupt, init, pointers */



#define IDLE			0
#define ACTIVE			1

#define SX932x_NIRQ		34

/* CS1 */
#define MAIN_SENSOR		1

/* Failer Index */
#define SX932x_ID_ERROR	1
#define SX932x_NIRQ_ERROR	2
#define SX932x_CONN_ERROR	3
#define SX932x_I2C_ERROR	4

#define PROXOFFSET_LOW			1500

#define SX932x_ANALOG_GAIN		1
#define SX932x_DIGITAL_GAIN		1
#define SX932x_ANALOG_RANGE		2.65

/* 44523 */
#define	TOUCH_CHECK_REF_AMB      0
/* 50 */
#define	TOUCH_CHECK_SLOPE        0
/* 151282 */
#define	TOUCH_CHECK_MAIN_AMB     0
/*add proc node*/
#define PROC_SAR_DIR            "sar"
#define PROC_SAR_CARD_TYPE      "sar_card_type"
/*! \struct sx932x
 * Specialized struct containing input event data,  platform data,  and
 * last cap state read if needed.
 */
typedef struct sx932x {
	pbuttonInformation_t pbuttonInformation;
	psx932x_platform_data_t hw;		/* specific platform data settings */
} sx932x_t,  *psx932x_t;

struct proc_dir_entry *sar_proc_dir = NULL;
static int irq_gpio_num;
static int status_num;
bool testcard_or_usercard = false;
static struct wake_lock irq_wakelock;
psx93XX_t smeteck_sar_ptr;
static int mEnabled;
static char chip_info[20];
/*! \fn static int write_register(psx93XX_t this,  u8 address,  u8 value)
 * \brief Sends a write register to the device
 * \param this Pointer to main parent struct
 * \param address 8-bit register address
 * \param value   8-bit register value to write to address
 * \return Value from i2c_master_send
 */
static int write_register(psx93XX_t this,  u8 address,  u8 value)
{
	struct i2c_client *i2c = 0;
	char buffer[2];
	int returnValue = 0;

	buffer[0] = address;
	buffer[1] = value;
	returnValue = -ENOMEM;

	if (this && this->bus) {
		i2c = this->bus;
		returnValue = i2c_master_send(i2c, buffer, 2);
#ifdef DEBUG
		dev_info(&i2c->dev, "write_register Address: 0x%x Value: 0x%x Return: %d\n",
						address, value, returnValue);
#endif
	}
	return returnValue;
}

/*! \fn static int read_register(psx93XX_t this,  u8 address,  u8 *value)
* \brief Reads a register's value from the device
* \param this Pointer to main parent struct
* \param address 8-Bit address to read from
* \param value Pointer to 8-bit value to save register value to
* \return Value from i2c_smbus_read_byte_data if < 0. else 0
*/
static int read_register(psx93XX_t this,  u8 address,  u8 *value)
{
	struct i2c_client *i2c = 0;
	s32 returnValue = 0;

	if (this && value && this->bus) {
		i2c = this->bus;
		returnValue = i2c_smbus_read_byte_data(i2c, address);

		#ifdef DEBUG
		dev_info(&i2c->dev, "read_register Address: 0x%x Return: 0x%x\n",
						address, returnValue);
		#endif

		if (returnValue >= 0) {
			*value = returnValue;
			return 0;
		} else {
			return returnValue;
		}
	}
	return -ENOMEM;
}

/* static int sx932x_set_mode(psx93XX_t this,  unsigned char mode); */

/*! \fn static int read_regStat(psx93XX_t this)
 * \brief Shortcut to read what caused interrupt.
 * \details This is to keep the drivers a unified
 * function that will read whatever register(s)
 * provide information on why the interrupt was caused.
 * \param this Pointer to main parent struct
 * \return If successful,  Value of bit(s) that cause interrupt,  else 0
 */
static int read_regStat(psx93XX_t this)
{
	u8 data = 0;

	if (this) {
		if (read_register(this, SX932x_IRQSTAT_REG, &data) == 0)
		dev_info(this->pdev,  "read_regStat %d\n", (data & 0x00FF));

		return (data & 0x00FF);
	}
	return 0;
}

/*********************************************************************/
/*! \brief Perform a manual offset calibration
* \param this Pointer to main parent struct
* \return Value return value from the write register
 */
static int manual_offset_calibration(psx93XX_t this)
{
	s32 returnValue = 0;

	returnValue = write_register(this, SX932x_STAT2_REG, 0x0F);
	return returnValue;
}
/*! \brief sysfs show function for manual calibration which currently just
 * returns register value.
 */
static ssize_t manual_offset_calibration_show(struct device *dev,
								struct device_attribute *attr,  char *buf)
{
	u8 reg_value = 0;
	psx93XX_t this = dev_get_drvdata(dev);

	dev_info(this->pdev,  "Reading IRQSTAT_REG\n");
	read_register(this, SX932x_IRQSTAT_REG, &reg_value);
	return snprintf(buf, PAGE_SIZE, "%d\n", reg_value);
}

/*! \brief sysfs store function for manual calibration
 */
static ssize_t manual_offset_calibration_store(struct device *dev,
			struct device_attribute *attr, const char *buf,  size_t count)
{
	psx93XX_t this = dev_get_drvdata(dev);
	unsigned long val;

	if (kstrtoul(buf,  0,  &val))
	return -EINVAL;
	if (val) {
		dev_info(this->pdev,  "Performing manual_offset_calibration()\n");
		manual_offset_calibration(this);
	}
	return count;
}

static ssize_t identify_diff_card(struct device *dev,
			struct device_attribute *attr, const char *buf,  size_t count)
{
	int val = 0;
	psx93XX_t this = dev_get_drvdata(dev);

	if (sscanf(buf,  "%d",   &val) != 1) {
		pr_err("[SX932x]: %s -   number of data are wrong\n", __func__);
		return -EINVAL;
	}
	if (val == 1) {
		dev_info(this->pdev,  "Test card inserted!\n");
		testcard_or_usercard = true;
	} else if (val == 0) {
		dev_info(this->pdev,  "User card inserted!\n");
		testcard_or_usercard = false;
	}
	return count;
}

static struct class sar_sensor_class = {
	.name = "sarsensor",
	.owner = THIS_MODULE,
};

static ssize_t delay_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	return snprintf(buf, 8, "%d\n", 200);
}

static ssize_t delay_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}

static CLASS_ATTR_RW(delay);

static ssize_t enable_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	return snprintf(buf, 8, "%d\n", mEnabled);
}

static ssize_t enable_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	if (smeteck_sar_ptr) {
		if ((!strncmp(buf, "1", 1)) && (mEnabled == 0) && (smeteck_sar_ptr != NULL)) {
			dev_info(smeteck_sar_ptr->pdev, "enable sar sensor\n");

			write_register(smeteck_sar_ptr, SX932x_CTRL1_REG, 0x22);
			mEnabled = 1;
		} else if ((!strncmp(buf, "0", 1)) && (mEnabled == 1) && (smeteck_sar_ptr != NULL)) {
			dev_info(smeteck_sar_ptr->pdev, "disable sar sensor\n");

			write_register(smeteck_sar_ptr, SX932x_CTRL1_REG, 0x02);
			mEnabled = 0;
		} else {
			dev_info(smeteck_sar_ptr->pdev, "unknown enable symbol, or repeat enable\n");
		}
	}

	return count;
}

static CLASS_ATTR_RW(enable);

static ssize_t chip_info_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	dev_info(smeteck_sar_ptr->pdev, "enable sar sensor, chip_info=%s\n", chip_info);
	return snprintf(buf, 25, "%s", chip_info);
}

static CLASS_ATTR_RO(chip_info);

static ssize_t status_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	u8 touchStatus = 0;

	if (smeteck_sar_ptr) {
		read_register(smeteck_sar_ptr, SX932x_IRQ_ENABLE_REG, &touchStatus);
		if (touchStatus == 0)
			return snprintf(buf, 64, "0\n");
	}
	if (status_num) {
		return snprintf(buf, 64, "1\n");
	} else {
		return snprintf(buf, 64, "0\n");
	}
}

static CLASS_ATTR_RO(status);

static ssize_t batch_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	dev_info(smeteck_sar_ptr->pdev, "batch_show sar sensor\n");
	return snprintf(buf, 64, "200\n");
}

static ssize_t batch_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	dev_info(smeteck_sar_ptr->pdev, "batch_store sar sensor\n");
	return count;
}
static CLASS_ATTR_RW(batch);

static ssize_t flush_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	dev_info(smeteck_sar_ptr->pdev, "flush_show sar sensor\n");
	return snprintf(buf, 64, "0\n");
}

static ssize_t flush_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	dev_info(smeteck_sar_ptr->pdev, "flush_store sar sensor\n");
	return count;
}
static CLASS_ATTR_RW(flush);

static ssize_t calibrate_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	u8 reg_value = 0;

	if (smeteck_sar_ptr) {
		dev_info(smeteck_sar_ptr->pdev,  "calibrate_show!\n");
		read_register(smeteck_sar_ptr, SX932x_IRQSTAT_REG, &reg_value);
		return snprintf(buf, PAGE_SIZE, "%d\n", reg_value);
	}

	dev_info(smeteck_sar_ptr->pdev, "calibrate_show\n");
	return snprintf(buf, 64, "0\n");
}

static ssize_t calibrate_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long val;

	if (smeteck_sar_ptr) {
		if (kstrtoul(buf, 0, &val))
			return -EINVAL;
		if (val) {
			dev_info(smeteck_sar_ptr->pdev,  "Performing calibrate_store()\n");
			manual_offset_calibration(smeteck_sar_ptr);
		}
		return count;
	}

	return count;
}

static CLASS_ATTR_RW(calibrate);

static int sx932x_Hardware_Check(psx93XX_t this)
{
	u8 loop = 0;

	this->failStatusCode = 0;

	/* Check th IRQ Status */
	while (this->get_nirq_low && this->get_nirq_low()) {
		read_regStat(this);
		msleep(100);
		if (++loop > 10) {
			this->failStatusCode = SX932x_NIRQ_ERROR;
			break;
		}
	}

	dev_info(this->pdev,  "sx932x failcode = 0x%x\n", this->failStatusCode);
	return (int)this->failStatusCode;
}


/*********************************************************************/
static int sx932x_global_variable_init(psx93XX_t this)
{
	this->irq_disabled = 0;
	this->failStatusCode = 0;
	this->reg_in_dts = true;
	return 0;
}

static ssize_t sx932x_register_write_store(struct device *dev,
			struct device_attribute *attr,  const char *buf,  size_t count)
{
	int reg_address = 0,  val = 0;
	psx93XX_t this = dev_get_drvdata(dev);

	if (sscanf(buf,  "%x, %x",  &reg_address,  &val) != 2) {
		pr_err("[SX932x]: %s - The number of data are wrong\n", __func__);
		return -EINVAL;
	}

	write_register(this,  (unsigned char)reg_address,  (unsigned char)val);
	pr_info("[SX932x]: %s - Register(0x%x) data(0x%x)\n", __func__,  reg_address,  val);

	return count;
}
/* read registers not include the advanced one */
static ssize_t sx932x_register_read_store(struct device *dev,
			struct device_attribute *attr,  const char *buf,  size_t count)
{
	u8 val = 0;
	int regist = 0;
	psx93XX_t this = dev_get_drvdata(dev);

	dev_info(this->pdev,  "Reading register\n");

	if (sscanf(buf,  "%x",  &regist) != 1) {
		pr_err("[SX932x]: %s - The number of data are wrong\n", __func__);
		return -EINVAL;
	}

	read_register(this,  regist,  &val);
	pr_info("[SX932x]: %s - Register(0x%2x) data(0x%2x)\n", __func__,  regist,  val);

	return count;
}

static void read_rawData(psx93XX_t this)
{
	u8 msb = 0,  lsb = 0;
	u8 csx;
	s32 useful;
	s32 average;
	s32 diff;
	u16 offset;

	if (this) {
		for (csx = 0; csx < 4; csx++) {
			/* here to check the CS1,  also can read other channel */
			write_register(this, SX932x_CPSRD, csx);
			read_register(this, SX932x_USEMSB, &msb);
			read_register(this, SX932x_USELSB, &lsb);
			useful = (s32)((msb << 8) | lsb);

			read_register(this, SX932x_AVGMSB, &msb);
			read_register(this, SX932x_AVGLSB, &lsb);
			average = (s32)((msb << 8) | lsb);

			read_register(this, SX932x_DIFFMSB, &msb);
			read_register(this, SX932x_DIFFLSB, &lsb);
			diff = (s32)((msb << 8) | lsb);

			read_register(this, SX932x_OFFSETMSB, &msb);
			read_register(this, SX932x_OFFSETLSB, &lsb);
			offset = (u16)((msb << 8) | lsb);
			if (useful > 32767)
				useful -= 65536;
			if (average > 32767)
				average -= 65536;
			if (diff > 32767)
				diff -= 65536;
			dev_info(this->pdev, "[CS: %d] Useful = %d Average = %d, DIFF = %d Offset = %d\n",
					csx, useful, average, diff, offset);
		}
	}
}

static ssize_t sx932x_raw_data_show(struct device *dev,
						struct device_attribute *attr,  char *buf)
{
	psx93XX_t this = dev_get_drvdata(dev);

	read_rawData(this);
	return 0;
}

static ssize_t diff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	psx93XX_t this = dev_get_drvdata(dev);
	u8 msb = 0,  lsb = 0;
	s32 diff = 0;
	s32 useful = 0;
	s32 average = 0;

	if (this) {
			/* here to check the CS1,if want check other CS ,can modify here*/
			write_register(this, SX932x_CPSRD, 1);
			read_register(this, SX932x_USEMSB, &msb);
			read_register(this, SX932x_USELSB, &lsb);
			useful = (s32)((msb << 8) | lsb);

			read_register(this, SX932x_AVGMSB, &msb);
			read_register(this, SX932x_AVGLSB, &lsb);
			average = (s32)((msb << 8) | lsb);

			read_register(this, SX932x_DIFFMSB, &msb);
			read_register(this, SX932x_DIFFLSB, &lsb);
			diff = (s32)((msb << 8) | lsb);

			if (useful > 32767)
				useful -= 65536;
			if (average > 32767)
				average -= 65536;
			if (diff > 32767)
				diff -= 65536;
			dev_info(this->pdev,
				"[CS1] Useful = %d Average = %d, DIFF = %d\n", useful, average, diff);
	}
	return snprintf(buf, 64, "%d,%d,%d\n", useful, average, diff);
}
/*
static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	psx93XX_t chip = dev_get_drvdata(dev);
	u8 touchStatus = 0;

	read_register(chip, SX932x_IRQ_ENABLE_REG, &touchStatus);
	if (touchStatus == 0)
		return snprintf(buf, 64, "0\n");

	if (status_num) {
		return snprintf(buf, 64, "1\n");
	} else {
		return snprintf(buf, 64, "0\n");
	}
}
*/
static ssize_t sx932x_batch_store(struct device *dev,
			struct device_attribute *attr,  const char *buf,  size_t count)
{
	return count;
}
static ssize_t sx932x_flush_store(struct device *dev,
			struct device_attribute *attr,  const char *buf,  size_t count)
{
	return count;
}
static ssize_t sx932x_flush_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "0\n");
}

static ssize_t sx932x_enable_store(struct device *dev,
			struct device_attribute *attr,  const char *buf,  size_t count)
{
	return count;
}
/*
static DEVICE_ATTR(status, 0444, status_show, NULL);
*/
static DEVICE_ATTR(calibrate,  0664,  manual_offset_calibration_show, manual_offset_calibration_store);
static DEVICE_ATTR(register_write,   0664,  NULL, sx932x_register_write_store);
static DEVICE_ATTR(register_read, 0664,  NULL, sx932x_register_read_store);
static DEVICE_ATTR(raw_data, 0664, sx932x_raw_data_show, NULL);
static DEVICE_ATTR(card_type, 0664, NULL, identify_diff_card);
static DEVICE_ATTR(diff, 0664, diff_show, NULL);
static DEVICE_ATTR(batch, 0664, NULL, sx932x_batch_store);
static DEVICE_ATTR(enable, 0664, NULL, sx932x_enable_store);
static DEVICE_ATTR(flush, 0664, sx932x_flush_show, sx932x_flush_store);

static struct attribute *sx932x_attributes[] = {
	&dev_attr_calibrate.attr,
	&dev_attr_register_write.attr,
	&dev_attr_register_read.attr,
	&dev_attr_raw_data.attr,
/*
	&dev_attr_status.attr,
*/
	&dev_attr_card_type.attr,
	&dev_attr_diff.attr,
	&dev_attr_enable.attr,
	&dev_attr_batch.attr,
	&dev_attr_flush.attr,
	NULL,
};
static struct attribute_group sx932x_attr_group = {
	.attrs = sx932x_attributes,
};

/****************************************************/
/*! \brief  Initialize I2C config from platform data
 * \param this Pointer to main parent struct
 */
static void sx932x_reg_init(psx93XX_t this)
{
	psx932x_t pDevice = 0;
	psx932x_platform_data_t pdata = 0;
	int i = 0;

	/* configure device */
	dev_info(this->pdev,  "Going to Setup I2C Registers\n");
	if (this) {
		pDevice = this->pDevice;
		pdata = pDevice->hw;
		/*******************************************************************************/
		/* try to initialize from device tree! */
		/*******************************************************************************/
		if (this->reg_in_dts == true) {
			while (i < pdata->i2c_reg_num) {
				/* Write all registers/values contained in i2c_reg */
#ifdef DEBUG
				dev_info(this->pdev,  "Going to Write Reg from dts: 0x%x Value: 0x%x\n",
				pdata->pi2c_reg[i].reg, pdata->pi2c_reg[i].val);
#endif
				write_register(this,  pdata->pi2c_reg[i].reg, pdata->pi2c_reg[i].val);
				i++;
			}
		} else { /* use static ones!! */
			while (i < ARRAY_SIZE(sx932x_i2c_reg_setup)) {
				/* Write all registers/values contained in i2c_reg */
#ifdef DEBUG
				dev_info(this->pdev,  "Going to Write Reg: 0x%x Value: 0x%x\n",
				sx932x_i2c_reg_setup[i].reg, sx932x_i2c_reg_setup[i].val);
#endif
				write_register(this,  sx932x_i2c_reg_setup[i].reg, sx932x_i2c_reg_setup[i].val);
				i++;
			}
		}
	/*******************************************************************************/
	} else {
		dev_err(this->pdev,  "ERROR! platform data 0x%p\n", pDevice->hw);
	}

}


/*! \fn static int initialize(psx93XX_t this)
 * \brief Performs all initialization needed to configure the device
 * \param this Pointer to main parent struct
 * \return Last used command's return value (negative if error)
 */
static int initialize(psx93XX_t this)
{
	int ret;

	if (this) {
		pr_info("SX932x income initialize\n");
		/* prepare reset by disabling any irq handling */
		this->irq_disabled = 1;
		disable_irq(this->irq);
		/* perform a reset */
		write_register(this, SX932x_SOFTRESET_REG, SX932x_SOFTRESET);
		/* wait until the reset has finished by monitoring NIRQ */
		dev_info(this->pdev,  "Sent Software Reset. Waiting until device is back from reset to continue.\n");
		/* just sleep for awhile instead of using a loop with reading irq status */
		msleep(100);

		ret = sx932x_global_variable_init(this);

		sx932x_reg_init(this);
		msleep(100); /* make sure everything is running */
		manual_offset_calibration(this);

		/* re-enable interrupt handling */
		enable_irq(this->irq);

		/* make sure no interrupts are pending since enabling irq will only
		* work on next falling edge */
		read_regStat(this);
		return 0;
	}
	return -ENOMEM;
}

/*!
 * \brief Handle what to do when a touch occurs
 * \param this Pointer to main parent struct
 */
static void touchProcess(psx93XX_t this)
{
	int counter = 0;
	u8 i = 0;
	int numberOfButtons = 0;
	psx932x_t pDevice = NULL;
	struct _buttonInfo *buttons = NULL;
	struct input_dev *input = NULL;

	struct _buttonInfo *pCurrentButton  = NULL;

	if (this) {
		pDevice = this->pDevice;
		dev_info(this->pdev,  "Inside touchProcess()\n");
		read_register(this,  SX932x_STAT0_REG,  &i);

		buttons = pDevice->pbuttonInformation->buttons;
		input = pDevice->pbuttonInformation->input;
		numberOfButtons = pDevice->pbuttonInformation->buttonSize;

		if (unlikely((buttons == NULL) || (input == NULL))) {
			dev_err(this->pdev,  "ERROR!! buttons or input NULL!!!\n");
			return;
		}

		for (counter = 0; counter < numberOfButtons; counter++) {
			pCurrentButton = &buttons[counter];
			if (pCurrentButton == NULL) {
				dev_err(this->pdev, "ERROR!! current button at index: %d NULL!!!\n",  counter);
				return; /* ERRORR!!!! */
			}
			switch (pCurrentButton->state) {
			case IDLE: /* Button is not being touched! */
				if (((i & pCurrentButton->mask) == pCurrentButton->mask)) {
					/* User pressed button */
					dev_info(this->pdev,  "cap button %d touched\n",  counter);
					/*input_report_key(input,  pCurrentButton->keycode,  1);*/
					/*input_report_key(input,  pCurrentButton->keycode,  0);*/

					/*use sar type scheme*/
					this->sar_status_ph.status = 1;
					this->sar_status_ph.ph_num = counter;/*counter is correspond to the ph num*/
					input_report_abs(input, ABS_RX, this->sar_status_ph.status);
					input_report_abs(input, ABS_RY, this->sar_status_ph.ph_num);
					input_sync(input);

					pCurrentButton->state = ACTIVE;
					status_num = 1;
				} else {
					dev_info(this->pdev,  "Button %d already released.\n", counter);
				}
				break;
			case ACTIVE: /* Button is being touched! */
				if (((i & pCurrentButton->mask) != pCurrentButton->mask)) {
					/* User released button */
					dev_info(this->pdev,  "cap button %d released\n", counter);
					/*input_report_key(input,  pCurrentButton->keycode,  1);*/
					/*input_report_key(input,  pCurrentButton->keycode,  0);*/

					/*use sar type scheme*/
					this->sar_status_ph.status = 0;
					this->sar_status_ph.ph_num = counter;/*counter is correspond to the ph num*/
					input_report_abs(input, ABS_RX, this->sar_status_ph.status);
					input_report_abs(input, ABS_RY, this->sar_status_ph.ph_num);
					input_sync(input);

					pCurrentButton->state = IDLE;
					status_num = 0;
				} else {
					dev_info(this->pdev,  "Button %d still touched.\n", counter);
				}
				break;
			default: /* Shouldn't be here,  device only allowed ACTIVE or IDLE */
				break;
			};
		}
		input_sync(input);

   /* dev_info(this->pdev,  "Leaving touchProcess()\n"); */
	}
}

static int sx932x_parse_dt(struct sx932x_platform_data *pdata,  struct device *dev)
{
	struct device_node *dNode = dev->of_node;
	enum of_gpio_flags flags;
	/* int ret; */
	if (dNode == NULL)
		return -ENODEV;

	pdata->irq_gpio = of_get_named_gpio_flags(dNode,
					"Semtech,nirq-gpio",  0,  &flags);
	irq_gpio_num = pdata->irq_gpio;
	if (pdata->irq_gpio < 0) {
		pr_err("[SENSOR]: %s - get irq_gpio error\n",  __func__);
		return -ENODEV;
	}

	/***********************************************************************/
	/* load in registers from device tree */
	of_property_read_u32(dNode, "Semtech,reg-num", &pdata->i2c_reg_num);
	/* layout is register,  value,  register,  value.... */
	/* if an extra item is after just ignore it. reading the array in will cause it to fail anyway */
	pr_info("[SX932x]:%s -  size of elements %d\n", __func__, pdata->i2c_reg_num);
	if (pdata->i2c_reg_num > 0) {
		 /* initialize platform reg data array */
		 pdata->pi2c_reg = devm_kzalloc(dev, sizeof(struct smtc_reg_data)*pdata->i2c_reg_num,  GFP_KERNEL);
		 if (unlikely(pdata->pi2c_reg == NULL)) {
			return -ENOMEM;
		}

	 /* initialize the array */
		if (of_property_read_u8_array(dNode, "Semtech,reg-init",
			(u8 *)&(pdata->pi2c_reg[0]), sizeof(struct smtc_reg_data)*pdata->i2c_reg_num))
		return -ENOMEM;
	}
	/***********************************************************************/
	pr_info("[SX932x]: %s -[%d] parse_dt complete\n",  __func__, pdata->irq_gpio);
	return 0;
}

/* get the NIRQ state (1->NIRQ-low,  0->NIRQ-high) */
static int sx932x_init_platform_hw(struct i2c_client *client)
{
	psx93XX_t this = i2c_get_clientdata(client);
	struct sx932x *pDevice = NULL;
	struct sx932x_platform_data *pdata = NULL;

	int rc = 0;

	pr_info("[SX932x] : %s init_platform_hw start!", __func__);

	if (this) {
		pDevice = this->pDevice;
		pdata = pDevice->hw;
		if (gpio_is_valid(pdata->irq_gpio)) {
			rc = gpio_request(pdata->irq_gpio,  "sx932x_irq_gpio");
			if (rc < 0) {
				dev_err(this->pdev,  "SX932x Request gpio. Fail![%d]\n",  rc);
				return rc;
			}
			rc = gpio_direction_input(pdata->irq_gpio);
			if (rc < 0) {
				dev_err(this->pdev,  "SX932x Set gpio direction. Fail![%d]\n",  rc);
				return rc;
			}
			this->irq = client->irq = gpio_to_irq(pdata->irq_gpio);
		} else {
			dev_err(this->pdev,  "SX932x Invalid irq gpio num.(init)\n");
		}
	} else {
		pr_err("[SX932x] : %s - Do not init platform HW",  __func__);
	}

	pr_err("[SX932x]: %s - sx932x_irq_debug\n", __func__);
	return rc;
}

static void sx932x_exit_platform_hw(struct i2c_client *client)
{
	psx93XX_t this = i2c_get_clientdata(client);
	struct sx932x *pDevice = NULL;
	struct sx932x_platform_data *pdata = NULL;

	if (this) {
		pDevice = this->pDevice;
		pdata = pDevice->hw;
		if (gpio_is_valid(pdata->irq_gpio)) {
			gpio_free(pdata->irq_gpio);
		} else {
			dev_err(this->pdev,  "Invalid irq gpio num.(exit)\n");
		}
	}
/* return; */
}

static int sx932x_get_nirq_state(void)
{
	return  !gpio_get_value(irq_gpio_num);
}

static ssize_t proc_sx932x_card_type(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int val = 0;
	int ret = 0;
	char data_buf[10] = {0};

	ret = copy_from_user(data_buf, buffer, len);
	pr_info("[SX932x]:%s copy_from_user ret %d!\n", __func__, ret);
	if (ret) {
		pr_info("[SX932x]:%s copy form user fail!\n", __func__);
		return -EINVAL;
		}

	ret = kstrtouint(data_buf, 0, &val);
			pr_info("[SX932x]:%s strtouint ret %d!\n", __func__, ret);
	if (ret) {
				pr_info("[SX932x]:%sstrtouint fail!\n", __func__);
		return -EINVAL;
		}
	if (val == 1) {
		pr_info("[SX932x]:%s Test card inserted!\n", __func__);
		testcard_or_usercard = true;
	} else if (val == 0) {
		pr_info("[SX932x]:%s User card inserted!\n", __func__);
		testcard_or_usercard = false;
	}
	return len;
}


static const struct file_operations proc_ops_card_Type = {
	.owner = THIS_MODULE,
	.write = proc_sx932x_card_type,
};

static void create_sar_proc_entry(void)
{
	struct proc_dir_entry *sar_proc_entry = NULL;

	sar_proc_dir = proc_mkdir(PROC_SAR_DIR, NULL);
	if (sar_proc_dir == NULL) {
		pr_err("%s: mkdir sar proc failed!\n",  __func__);
		return;
	}
	sar_proc_entry = proc_create(PROC_SAR_CARD_TYPE, 0664, sar_proc_dir, &proc_ops_card_Type);
	if (sar_proc_entry == NULL)
		pr_err("proc_create sar_card_type failed!\n");
}



/*! \fn static int sx932x_probe(struct i2c_client *client,  const struct i2c_device_id *id)
 * \brief Probe function
 * \param client pointer to i2c_client
 * \param id pointer to i2c_device_id
 * \return Whether probe was successful
 */
static int sx932x_probe(struct i2c_client *client,  const struct i2c_device_id *id)
{
	int i = 0;
	int err = 0;
	u8 failcode = 0;
	psx93XX_t this = 0;
	psx932x_t pDevice = 0;
	psx932x_platform_data_t pplatData = 0;
	struct totalButtonInformation *pButtonInformationData = NULL;
	struct input_dev *input = NULL;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);

	dev_info(&client->dev,  "sx932x_probe()\n");

	if (!i2c_check_functionality(adapter,  I2C_FUNC_SMBUS_READ_WORD_DATA)) {
		dev_err(&client->dev,  "Check i2c functionality.Fail!\n");
		err = -EIO;
		return err;
	}

	snprintf(chip_info, sizeof(chip_info), "%s", "smeteck");

	this = devm_kzalloc(&client->dev, sizeof(sx93XX_t),  GFP_KERNEL); /* create memory for main struct */
	dev_info(&client->dev,  "\t Initialized Main Memory: 0x%p\n", this);

	pButtonInformationData = devm_kzalloc(&client->dev,  sizeof(struct totalButtonInformation),  GFP_KERNEL);
	if (!pButtonInformationData) {
		dev_err(&client->dev,  "Failed to allocate memory(totalButtonInformation)\n");
		err = -ENOMEM;
		return err;
	}

	pButtonInformationData->buttonSize = ARRAY_SIZE(psmtcButtons);
	pButtonInformationData->buttons =  psmtcButtons;
	pplatData = devm_kzalloc(&client->dev, sizeof(struct sx932x_platform_data),  GFP_KERNEL);
	if (!pplatData) {
		dev_err(&client->dev,  "platform data is required!\n");
		return -EINVAL;
	}
	pplatData->get_is_nirq_low = sx932x_get_nirq_state;
	pplatData->pbuttonInformation = pButtonInformationData;

	client->dev.platform_data = pplatData;
	err = sx932x_parse_dt(pplatData,  &client->dev);
	if (err) {
		dev_err(&client->dev,  "could not setup pin\n");
		return -ENODEV;
	}
	/*wake up lock for irq process when suspend*/
	wake_lock_init(&irq_wakelock, WAKE_LOCK_SUSPEND, "IRQ_wakelock");

	pplatData->init_platform_hw = sx932x_init_platform_hw;
	dev_err(&client->dev,  "SX932x init_platform_hw done!\n");

	if (this) {
		dev_info(&client->dev,  "SX932x initialize start!!");
		smeteck_sar_ptr = this;
		/* In case we need to reinitialize data
		* (e.q. if suspend reset device) */
		this->init = initialize;
		/* shortcut to read status of interrupt */
		this->refreshStatus = read_regStat;
		/* pointer to function from platform data to get pendown
		* (1->NIRQ=0,  0->NIRQ=1) */
		this->get_nirq_low = pplatData->get_is_nirq_low;
		/* save irq in case we need to reference it */
		this->irq = client->irq;
		/* do we need to create an irq timer after interrupt ? */
		this->useIrqTimer = 0;

		/* Setup function to call on corresponding reg irq source bit */
		if (MAX_NUM_STATUS_BITS >= 8) {
			this->statusFunc[0] = 0; /* TXEN_STAT */
			this->statusFunc[1] = 0; /* UNUSED */
			this->statusFunc[2] = 0; /* UNUSED */
			this->statusFunc[3] = read_rawData; /* CONV_STAT */
			this->statusFunc[4] = 0; /* COMP_STAT */
			this->statusFunc[5] = touchProcess; /* RELEASE_STAT */
			this->statusFunc[6] = touchProcess; /* TOUCH_STAT  */
			this->statusFunc[7] = 0; /* RESET_STAT */
		}

		/* setup i2c communication */
		this->bus = client;
		i2c_set_clientdata(client,  this);

		/* record device struct */
		this->pdev = &client->dev;

		/* create memory for device specific struct */
		this->pDevice = pDevice = devm_kzalloc(&client->dev, sizeof(sx932x_t),  GFP_KERNEL);
		dev_info(&client->dev,  "\t Initialized Device Specific Memory: 0x%p\n", pDevice);

		/* Check I2C Connection and chip ID */
		err = read_register(this,  SX932x_WHOAMI_REG,  &failcode);
		if (err < 0) {
			dev_info(this->pdev,  "sx932x failcode = 0x%x\n", SX932x_I2C_ERROR);
			return -EIO;
		}
		if (failcode != SX932x_WHOAMI_VALUE) {
			dev_info(this->pdev,  "sx932x failcode = 0x%x\n", SX932x_ID_ERROR);
			goto check_id_fail;
		}

		if (pDevice) {
			/* for accessing items in user data (e.g. calibrate) */
			err = sysfs_create_group(&client->dev.kobj,  &sx932x_attr_group);
			/* sysfs_create_group(client,  &sx932x_attr_group); */

			/* Add Pointer to main platform data struct */
			pDevice->hw = pplatData;

			/* Check if we hava a platform initialization function to call*/
			if (pplatData->init_platform_hw)
			pplatData->init_platform_hw(client);

			/* Initialize the button information initialized with keycodes */
			pDevice->pbuttonInformation = pplatData->pbuttonInformation;
			/* Create the input device */
			input = input_allocate_device();
			if (!input) {
				return -ENOMEM;
			}
			/* Set all the keycodes */
			__set_bit(EV_KEY,  input->evbit);
			input_set_capability(input, EV_ABS, ABS_RX);
			input_set_capability(input, EV_ABS, ABS_RY);
			input_set_capability(input, EV_ABS, ABS_RZ);
			/* #if 1 */
			for (i = 0; i < pButtonInformationData->buttonSize; i++) {
				__set_bit(pButtonInformationData->buttons[i].keycode, input->keybit);
				pButtonInformationData->buttons[i].state = IDLE;
			}
			/* #endif */
			/* save the input pointer and finish initialization */
			pButtonInformationData->input = input;
			input->name = "sar_sensor";
			input->id.bustype = BUS_I2C;
			input_set_drvdata(input, &this);
			if (input_register_device(input)) {
				return -ENOMEM;
			}
			/*add class sysfs*/
			err = class_register(&sar_sensor_class);
			if (err < 0) {
				dev_info(this->pdev, "Create fsys class failed (%d)\n", err);
				return err;
			}

			err = class_create_file(&sar_sensor_class, &class_attr_delay);
			if (err < 0) {
				dev_info(this->pdev, "Create delay file failed (%d)\n", err);
				goto err_class_creat;
			}

			err = class_create_file(&sar_sensor_class, &class_attr_enable);
			if (err < 0) {
				dev_info(this->pdev, "Create enable file failed (%d)\n", err);
				goto err_class_creat;
			}

			err = class_create_file(&sar_sensor_class, &class_attr_chip_info);
			if (err < 0) {
				dev_info(this->pdev, "Create chip_info file failed (%d)\n", err);
				goto err_class_creat;
			}

			err = class_create_file(&sar_sensor_class, &class_attr_status);
			if (err < 0) {
				dev_info(this->pdev, "Create status file failed (%d)\n", err);
				goto err_class_creat;
			}

			err = class_create_file(&sar_sensor_class, &class_attr_batch);
			if (err < 0) {
				dev_info(this->pdev, "Create batch file failed (%d)\n", err);
				goto err_class_creat;
			}

			err = class_create_file(&sar_sensor_class, &class_attr_flush);
			if (err < 0) {
				dev_info(this->pdev, "Create flush file failed (%d)\n", err);
				goto err_class_creat;
			}

			err = class_create_file(&sar_sensor_class, &class_attr_calibrate);
			if (err < 0) {
				dev_info(this->pdev, "Create calibrate file failed (%d)\n", err);
				goto err_class_creat;
			}
		}


		sx93XX_IRQ_init(this);
		/* call init function pointer (this should initialize all registers */
		if (this->init) {
			this->init(this);
		} else {
			dev_err(this->pdev, "No init function!!!!\n");
			return -ENOMEM;
		}
	} else {
		return -EPERM;
	}
	/*check NIRQ level*/
	sx932x_Hardware_Check(this);
	pplatData->exit_platform_hw = sx932x_exit_platform_hw;
	create_sar_proc_entry();

	this->sar_status_ph.status = 0;
	this->sar_status_ph.ph_num = 0;
	mEnabled = 1;
	dev_info(&client->dev, "sx932x_probe() Done\n");

	return 0;
check_id_fail:
	dev_info(&client->dev,  "sx932x_probe() Check ID fail!\n");
	wake_lock_destroy(&irq_wakelock);
	return -ENXIO;

err_class_creat:
	dev_info(&client->dev, "unregister sar_sensor_class.\n");
	class_unregister(&sar_sensor_class);
	return err;
}

/*! \fn static int sx932x_remove(struct i2c_client *client)
 * \brief Called when device is to be removed
 * \param client Pointer to i2c_client struct
 * \return Value from sx93XX_remove()
 */
/* static int __devexit sx932x_remove(struct i2c_client *client) */
static int sx932x_remove(struct i2c_client *client)
{
	psx932x_platform_data_t pplatData = 0;
	psx932x_t pDevice = 0;
	psx93XX_t this = i2c_get_clientdata(client);

	if (this) {
		pDevice = this->pDevice;
		input_unregister_device(pDevice->pbuttonInformation->input);

		sysfs_remove_group(&client->dev.kobj,  &sx932x_attr_group);
		pplatData = client->dev.platform_data;
		if (pplatData && pplatData->exit_platform_hw)
			pplatData->exit_platform_hw(client);
		kfree(this->pDevice);
	}
	return sx93XX_remove(this);
}
/* def CONFIG_PM */
#if SUSPEND_SUPPORT
/*====================================================*/
/***** Kernel Suspend *****/
static int sx932x_suspend(struct device *dev)
{
	psx93XX_t this = dev_get_drvdata(dev);

	sx93XX_suspend(this);
	return 0;
}
/***** Kernel Resume *****/
static int sx932x_resume(struct device *dev)
{
	psx93XX_t this = dev_get_drvdata(dev);

	sx93XX_resume(this);
	return 0;
}
/*====================================================*/
#else
#define sx932x_suspend		NULL
#define sx932x_resume		NULL
#endif /* CONFIG_PM */

static struct i2c_device_id sx932x_idtable[] = {
	{ DRIVER_NAME,  0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c,  sx932x_idtable);
#ifdef CONFIG_OF
static struct of_device_id sx932x_match_table[] = {
	{ .compatible = "Semtech,sx932x", },
	{ },
};
#else
#define sx932x_match_table NULL
#endif
static const struct dev_pm_ops sx932x_pm_ops = {
	.suspend = sx932x_suspend,
	.resume = sx932x_resume,
};
static struct i2c_driver sx932x_driver = {
	.driver = {
		.owner			= THIS_MODULE,
		.name			= DRIVER_NAME,
		.of_match_table	= sx932x_match_table,
		.pm				= &sx932x_pm_ops,
	},
	.id_table		= sx932x_idtable,
	.probe			= sx932x_probe,
	.remove			= sx932x_remove,
};
static int __init sx932x_I2C_init(void)
{
	return i2c_add_driver(&sx932x_driver);
}
static void __exit sx932x_I2C_exit(void)
{
	i2c_del_driver(&sx932x_driver);
}

module_init(sx932x_I2C_init);
module_exit(sx932x_I2C_exit);

MODULE_AUTHOR("Semtech Corp. (http://www.semtech.com/)");
MODULE_DESCRIPTION("SX932x Capacitive Touch Controller Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

static void sx93XX_schedule_work(psx93XX_t this,  unsigned long delay)
{
	unsigned long flags;

	if (this) {
		dev_info(this->pdev,  "sx93XX_schedule_work()\n");
		spin_lock_irqsave(&this->lock, flags);
		/* Stop any pending penup queues */
		cancel_delayed_work(&this->dworker);
		/*
		* after waiting for a delay,  this put the job in the kernel-global workqueue.
		* so no need to create new thread in work queue.
		*/
		schedule_delayed_work(&this->dworker, delay);
		spin_unlock_irqrestore(&this->lock, flags);
	} else {
		dev_err(this->pdev, "sx93XX_schedule_work,  NULL psx93XX_t\n");
	}
}

static irqreturn_t sx93XX_irq(int irq,  void *pvoid)
{
	psx93XX_t this = 0;

	if (pvoid) {
		this = (psx93XX_t)pvoid;
		if ((!this->get_nirq_low) || this->get_nirq_low()) {
		sx93XX_schedule_work(this, 0);
		} else{
			dev_err(this->pdev,  "sx93XX_irq - nirq read high\n");
		}
	} else{
		dev_err(this->pdev, "sx93XX_irq,  NULL pvoid\n");
	}
	return IRQ_HANDLED;
}

static void sx93XX_worker_func(struct work_struct *work)
{
	psx93XX_t this = 0;
	int status = 0;
	int counter = 0;
	u8 nirqLow = 0;

	wake_lock(&irq_wakelock);
	if (work) {
		this = container_of(work, sx93XX_t, dworker.work);

		if (!this) {
			dev_err(this->pdev, "sx93XX_worker_func,  NULL sx93XX_t\n");
			return;
		}
		if (unlikely(this->useIrqTimer)) {
			if ((!this->get_nirq_low) || this->get_nirq_low()) {
				nirqLow = 1;
			}
		}
		/* since we are not in an interrupt don't need to disable irq. */
		status = this->refreshStatus(this);
		counter = -1;
		dev_dbg(this->pdev,  "Worker - Refresh Status %d\n", status);

		while ((++counter) < MAX_NUM_STATUS_BITS) { /* counter start from MSB */
			if (((status>>counter) & 0x01)  && (this->statusFunc[counter])) {
				dev_info(this->pdev,  "SX932x Function Pointer Found. Calling\n");
				this->statusFunc[counter](this);
			}
		}
		/* Early models and if RATE=0 for newer models require a penup timer */
		if (unlikely(this->useIrqTimer && nirqLow)) {
			/* Queue up the function again for checking on penup */
			sx93XX_schedule_work(this, msecs_to_jiffies(this->irqTimeout));
		}
	} else {
		dev_err(this->pdev, "sx93XX_worker_func,  NULL work_struct\n");
	}
	wake_unlock(&irq_wakelock);
}

int sx93XX_remove(psx93XX_t this)
{
	if (this) {
		cancel_delayed_work_sync(&this->dworker); /* Cancel the Worker Func */
		/*destroy_workqueue(this->workq); */
		free_irq(this->irq,  this);
		kfree(this);
		return 0;
	}
	return -ENOMEM;
}
void sx93XX_suspend(psx93XX_t this)
{
	if (this) {
		if (testcard_or_usercard == 1) {
			enable_irq_wake(this->irq);
			dev_err(this->pdev, "sx93XX_suspend with test card!\n");
			/* write_register(this, SX932x_CTRL1_REG, 0x20);//make sx932x in Sleep mode */
		} else {
			disable_irq(this->irq);
			dev_err(this->pdev, "sx93XX_suspend with user card or no card!\n");
			}
		}

}
void sx93XX_resume(psx93XX_t this)
{
	if (this) {
		if (testcard_or_usercard == 1) {
			sx93XX_schedule_work(this, 0);
			dev_err(this->pdev, "sx93XX_resume with test card!\n");
			/*if (this->init)
			this->init(this);*/
			disable_irq_wake(this->irq);
			} else {
			sx93XX_schedule_work(this, 0);
			dev_err(this->pdev, "sx93XX_resume with user card or no card!\n");
			enable_irq(this->irq);
		}
	}
}

int sx93XX_IRQ_init(psx93XX_t this)
{
	int err = 0;

	if (this && this->pDevice) {
		/* initialize spin lock */
		spin_lock_init(&this->lock);
		/* initialize worker function */
		INIT_DELAYED_WORK(&this->dworker,  sx93XX_worker_func);
		/* initailize interrupt reporting */
		this->irq_disabled = 0;
		err = request_irq(this->irq,  sx93XX_irq,  IRQF_TRIGGER_FALLING,
							this->pdev->driver->name,  this);
		if (err) {
			dev_err(this->pdev,  "irq %d busy?\n",  this->irq);
			return err;
		}
		dev_info(this->pdev,  "registered with irq (%d)\n",  this->irq);
	}
	return -ENOMEM;
}
