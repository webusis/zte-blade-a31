/*
 * The original Work has been changed by NXP Semiconductors.
 * Copyright 2013-2019 NXP
 *
 * Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*
 * Copyright (C) 2010 Trusted Logic S.A.
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include <linux/clk.h>

#include "nfc.h"
#include "sn1xx.h"
#include "pn8xt.h"

#define MAX_BUFFER_SIZE		 (512)
#define WAKEUP_SRC_TIMEOUT	  (2000)
#define MAX_RETRY_COUNT		  3
#define MAX_SECURE_SESSIONS	  1

/*Compile time function calls based on the platform selection*/
#define platform_func(prefix, postfix) prefix##postfix
#define func(prefix, postfix) platform_func(prefix, postfix)

void nfc_disable_irq(struct nfc_dev *nfc_dev)
{
	unsigned long flags;

	if (gpio_get_value(nfc_dev->clkreq_gpio)) {
		__pm_wakeup_event(nfc_wake_lock, 5 * MSEC_PER_SEC);
	}
	spin_lock_irqsave(&nfc_dev->irq_enabled_lock, flags);
	if (nfc_dev->irq_enabled) {
		disable_irq_nosync(nfc_dev->client->irq);
		nfc_dev->irq_enabled = false;
	}
	spin_unlock_irqrestore(&nfc_dev->irq_enabled_lock, flags);
}

void nfc_enable_irq(struct nfc_dev *nfc_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&nfc_dev->irq_enabled_lock, flags);
	if (!nfc_dev->irq_enabled) {
		nfc_dev->irq_enabled = true;
		enable_irq(nfc_dev->client->irq);
	}
	spin_unlock_irqrestore(&nfc_dev->irq_enabled_lock, flags);
}

static irqreturn_t nfc_dev_irq_handler(int irq, void *dev_id)
{
	struct nfc_dev *nfc_dev = dev_id;
	unsigned long flags;

	pr_debug("%s: irq enter !\n", __func__);
	nfc_disable_irq(nfc_dev);
	spin_lock_irqsave(&nfc_dev->irq_enabled_lock, flags);
	nfc_dev->count_irq++;
	spin_unlock_irqrestore(&nfc_dev->irq_enabled_lock, flags);
	wake_up(&nfc_dev->read_wq);
	return IRQ_HANDLED;
}

static irqreturn_t nfc_dev_clkirq_handler(int irq, void *dev_id)
{
	pr_info("%s: clkreq_irq enter !\n", __func__);
	return IRQ_HANDLED;
}

static ssize_t nfc_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offset)
{
	struct nfc_dev *nfc_dev = filp->private_data;
	char tmp[MAX_BUFFER_SIZE];
	int ret;
	int irq_gpio_val = 0;

	if (!nfc_dev) {
		return -ENODEV;
	}
	if (count > MAX_BUFFER_SIZE)
		count = MAX_BUFFER_SIZE;
	pr_debug("%s: start reading of %zu bytes\n", __func__, count);
	mutex_lock(&nfc_dev->read_mutex);
	irq_gpio_val = gpio_get_value(nfc_dev->irq_gpio);
	if (irq_gpio_val == 0) {
		if (filp->f_flags & O_NONBLOCK) {
			dev_err(&nfc_dev->client->dev,
			":f_flags has O_NONBLOCK. EAGAIN\n");
			ret = -EAGAIN;
			goto err;
		}
		while (1) {
			ret = 0;
			if (!nfc_dev->irq_enabled) {
				nfc_dev->irq_enabled = true;
				enable_irq(nfc_dev->client->irq);
			}
			if (gpio_get_value(nfc_dev->clkreq_gpio)) {
				__pm_wakeup_event(nfc_wake_lock, 0 * MSEC_PER_SEC);
			}
			if (!gpio_get_value(nfc_dev->irq_gpio)) {
				pr_err("%s: wait_event_interruptible start!\n", __func__);
				ret = wait_event_interruptible(nfc_dev->read_wq,
					!nfc_dev->irq_enabled);
			}
			if (ret)
				goto err;
			nfc_disable_irq(nfc_dev);
			if (gpio_get_value(nfc_dev->irq_gpio))
				break;
			pr_warn("%s: spurious interrupt detected\n", __func__);
		}
	}
	memset(tmp, 0x00, count);
	/* Read data */
	ret = i2c_master_recv(nfc_dev->client, tmp, count);
	mutex_unlock(&nfc_dev->read_mutex);
	/* delay of 1ms for slow devices*/
	udelay(1000);
	if (ret < 0) {
		pr_err("%s: i2c_master_recv returned %d\n", __func__, ret);
		return ret;
	}
	if (ret > count) {
		pr_err("%s: received too many bytes from i2c (%d)\n",
				__func__, ret);
		ret = -EIO;
		return ret;
	}
	if (copy_to_user(buf, tmp, ret)) {
		pr_warn("%s : failed to copy to user space\n", __func__);
		ret = -EFAULT;
		return ret;
	}
	pr_debug("%s: Success in reading %zu bytes\n", __func__, count);
	return ret;
err:
	mutex_unlock(&nfc_dev->read_mutex);
	return ret;
}

static ssize_t nfc_dev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offset)
{
	struct nfc_dev *nfc_dev = filp->private_data;
	char tmp[MAX_BUFFER_SIZE];
	int ret = 0;

	if (!nfc_dev) {
		return -ENODEV;
	}
	if (count > MAX_BUFFER_SIZE) {
		count = MAX_BUFFER_SIZE;
	}
	pr_debug("%s: start writing of %zu bytes\n", __func__, count);
	if (copy_from_user(tmp, buf, count)) {
		pr_err("%s : failed to copy from user space\n", __func__);
		return -EFAULT;
	}
	ret = i2c_master_send(nfc_dev->client, tmp, count);
	if (ret != count) {
		pr_err("%s: i2c_master_send returned %d\n", __func__, ret);
		ret = -EIO;
	}
	pr_debug("%s: Success in writing %zu bytes\n", __func__, count);
	/* delay of 1ms for slow devices*/
	udelay(1000);
	return ret;
}

/* Callback to claim the embedded secure element
 * It is a blocking call, in order to protect the ese
 * from being reset from outside when it is in use.
 */
void nfc_ese_acquire(struct nfc_dev *nfc_dev)
{
	mutex_lock(&nfc_dev->ese_status_mutex);
	pr_debug("%s: ese acquired\n", __func__);
}

/* Callback to release the  embedded secure element
 * it should be released, after completion of any
 * operation (usage or reset) of ese.
 */
void nfc_ese_release(struct nfc_dev *nfc_dev)
{
	mutex_unlock(&nfc_dev->ese_status_mutex);
	pr_debug("%s: ese released\n", __func__);
}

static int nfc_dev_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct nfc_dev *nfc_dev = container_of(filp->private_data,
			struct nfc_dev, nfc_device);

	filp->private_data = nfc_dev;
	pr_info("%s: %d,%d\n", __func__, imajor(inode), iminor(inode));
	return ret;
}

long nfc_dev_ioctl(struct file *filep, unsigned int cmd,
		unsigned long arg)
{
	long ret = 0;
	struct nfc_dev *nfc_dev = filep->private_data;

	ret = func(NFC_PLATFORM, _nfc_ioctl)(nfc_dev, cmd, arg);
	if (ret != 0)
		pr_err("%s: ioctl: cmd = %u, arg = %lu\n", __func__, cmd, arg);
	return ret;
}

static const struct file_operations nfc_dev_fops = {
		.owner  = THIS_MODULE,
		.llseek = no_llseek,
		.read   = nfc_dev_read,
		.write  = nfc_dev_write,
		.open   = nfc_dev_open,
		.unlocked_ioctl  = nfc_dev_ioctl,
};

struct nfc_platform_data {
	unsigned int irq_gpio;
	unsigned int ven_gpio;
	unsigned int firm_gpio;
	unsigned int ese_pwr_gpio;
	unsigned int clkreq_gpio;
};

static int nfc_parse_dt(struct device *dev,
	struct nfc_platform_data *data)
{
	int ret = 0;
	struct device_node *np = dev->of_node;

	data->irq_gpio = of_get_named_gpio(np, "nxp,pn544-irq", 0);
	if ((!gpio_is_valid(data->irq_gpio)))
			return -EINVAL;

	data->ven_gpio = of_get_named_gpio(np, "nxp,pn544-ven", 0);
	if ((!gpio_is_valid(data->ven_gpio)))
			return -EINVAL;

	data->firm_gpio = of_get_named_gpio(np, "nxp,pn544-fw-dwnld", 0);
	if ((!gpio_is_valid(data->firm_gpio)))
			return -EINVAL;

	/*required for old platform only*/
	data->ese_pwr_gpio = of_get_named_gpio(np, "nxp,pn544-ese-pwr", 0);
	if ((!gpio_is_valid(data->ese_pwr_gpio)))
		data->ese_pwr_gpio =  -EINVAL;

	data->clkreq_gpio = of_get_named_gpio(np, "nxp,pn544-clkreq", 0);
	if ((!gpio_is_valid(data->clkreq_gpio)))
			data->clkreq_gpio =  -EINVAL;

	pr_info("%s: %d, %d, %d, %d,%d, the error:%d\n", __func__,
				data->irq_gpio, data->ven_gpio, data->firm_gpio,
				data->ese_pwr_gpio, data->clkreq_gpio, ret);
	return ret;
}

static int nfcc_hw_check(struct i2c_client *client, unsigned int enable_gpio, unsigned int firm_gpio)
{
	int ret = 0;

	unsigned char raw_fw_get_version_cmd[] =  {0x00, 0x04, 0xF1, 0x00, 0x00, 0x00, 0x6E, 0xEF};
	unsigned char fw_get_version_rsp[14];

	gpio_set_value(firm_gpio, 1);
	msleep(20);
	/* making sure that the NFCC starts in a clean state. */
	gpio_set_value(enable_gpio, 0);/* ULPM: Disable */
	/* hardware dependent delay */
	msleep(20);
	gpio_set_value(enable_gpio, 1);/* HPD : Enable*/
	/* hardware dependent delay */
	msleep(20);
	/* send get FW Version CMD */
	ret = i2c_master_send(client, raw_fw_get_version_cmd,
						sizeof(raw_fw_get_version_cmd));
	if (ret < 0) {
		dev_err(&client->dev,
		"%s: - i2c_master_send Error\n", __func__);
		goto err_nfcc_hw_check;
	}
	/* hardware dependent delay */
	msleep(30);

	/* Read Response of get fw version */
	ret = i2c_master_recv(client, fw_get_version_rsp,
						sizeof(fw_get_version_rsp));
	dev_err(&client->dev,
	"%s: - nq - firm cmd answer : NfcNciRx %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			__func__, fw_get_version_rsp[0], fw_get_version_rsp[1],
			fw_get_version_rsp[2], fw_get_version_rsp[3], fw_get_version_rsp[4],
			fw_get_version_rsp[5], fw_get_version_rsp[6], fw_get_version_rsp[7],
			fw_get_version_rsp[8], fw_get_version_rsp[9], fw_get_version_rsp[10],
			fw_get_version_rsp[11], fw_get_version_rsp[12], fw_get_version_rsp[13]);
	if (ret < 0) {
		dev_err(&client->dev,
		"%s: - i2c_master_recv Error\n", __func__);
		goto err_nfcc_hw_check;
	}
	gpio_set_value(firm_gpio, 0);
	gpio_set_value(enable_gpio, 0);/* ULPM: Disable */
	ret = 0;
	pr_err("%s: raw_fw_get_version success\n", __func__);
	goto done;

err_nfcc_hw_check:
	ret = -ENXIO;
	dev_err(&client->dev,
		"%s: - NFCC HW not available\n", __func__);
done:
	return ret;
}

static int nfc_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = 0;
	int irqn = 0;
	int clkirqn = 0;
	struct nfc_platform_data platform_data;
	struct nfc_dev *nfc_dev;

	pr_debug("%s: enter\n", __func__);

	ret = nfc_parse_dt(&client->dev, &platform_data);
	if (ret) {
		pr_err("%s : failed to parse\n", __func__);
		goto err;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s : need I2C_FUNC_I2C\n", __func__);
		ret = -ENODEV;
		goto err;
	}
	nfc_dev = kzalloc(sizeof(*nfc_dev), GFP_KERNEL);
	if (nfc_dev == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	nfc_dev->client = client;
	if (gpio_is_valid(platform_data.ven_gpio)) {
		ret = gpio_request(platform_data.ven_gpio, "nfc_reset_gpio");
		if (ret) {
			pr_err("%s: unable to request nfc reset gpio [%d]\n",
						__func__, platform_data.ven_gpio);
			goto err_mem;
		}
		ret = gpio_direction_output(platform_data.ven_gpio, 0);
		if (ret) {
			pr_err("%s: unable to set direction for nfc reset gpio [%d]\n",
						__func__, platform_data.ven_gpio);
			goto err_en_gpio;
		}
	} else {
		pr_err("%s: nfc reset gpio not provided\n", __func__);
		goto err_mem;
	}
	if (gpio_is_valid(platform_data.irq_gpio)) {
		ret = gpio_request(platform_data.irq_gpio, "nfc_irq_gpio");
		if (ret) {
			pr_err("%s: unable to request nfc irq gpio [%d]\n",
						__func__, platform_data.irq_gpio);
			goto err_en_gpio;
		}
		ret = gpio_direction_input(platform_data.irq_gpio);
		if (ret) {
			pr_err("%s: unable to set direction for nfc irq gpio [%d]\n",
						__func__, platform_data.irq_gpio);
			goto err_irq_gpio;
		}
		irqn = gpio_to_irq(platform_data.irq_gpio);
		if (irqn < 0) {
			ret = irqn;
			goto err_irq_gpio;
		}
		client->irq = irqn;
	} else {
		pr_err("%s: irq gpio not provided\n", __func__);
		goto err_en_gpio;
	}
	if (gpio_is_valid(platform_data.firm_gpio)) {
		ret = gpio_request(platform_data.firm_gpio, "nfc_firm_gpio");
		if (ret) {
			pr_err("%s: unable to request nfc firmware gpio [%d]\n",
						__func__, platform_data.firm_gpio);
			goto err_irq_gpio;
		}
		ret = gpio_direction_output(platform_data.firm_gpio, 0);
		if (ret) {
			pr_err("%s: cannot set direction for nfc firmware gpio [%d]\n",
					__func__, platform_data.firm_gpio);
			goto err_firm_gpio;
		}
	} else {
		pr_err("%s: firm gpio not provided\n", __func__);
		goto err_irq_gpio;
	}
	if (gpio_is_valid(platform_data.ese_pwr_gpio)) {
		ret = gpio_request(platform_data.ese_pwr_gpio, "nfc-ese_pwr");
		if (ret) {
			pr_err("%s: unable to request nfc ese gpio [%d]\n",
					__func__, platform_data.ese_pwr_gpio);
			goto err_firm_gpio;
		}
		ret = gpio_direction_output(platform_data.ese_pwr_gpio, 0);
		if (ret) {
			pr_err("%s: cannot set direction for nfc ese gpio [%d]\n",
					__func__, platform_data.ese_pwr_gpio);
			goto err_ese_pwr_gpio;
		}
	} else {
		/* ese gpio not required for latest platform*/
		pr_info("%s: ese pwr gpio not provided\n", __func__);
	}
	#ifdef CONFIG_NFC_ENABLE_BB_CLK
	/*set wakeup_source*/
	nfc_wake_lock = wakeup_source_register(NULL, "nfctimer");
	/*register clkirq to wake up ap*/
	if (gpio_is_valid(platform_data.clkreq_gpio)) {
		ret = gpio_request(platform_data.clkreq_gpio, "nfc_clkirq_gpio");
		if (ret) {
			pr_err("%s: unable to request nfc clkreq_gpio [%d]\n",
						__func__, platform_data.clkreq_gpio);
			goto err_clkirq_gpio;
		}
		ret = gpio_direction_input(platform_data.clkreq_gpio);
		if (ret) {
			pr_err("%s: unable to set direction for nfc irq gpio [%d]\n",
						__func__, platform_data.clkreq_gpio);
			goto err_clkirq_gpio;
		}
		clkirqn = gpio_to_irq(platform_data.clkreq_gpio);
		if (clkirqn < 0) {
			pr_info("%s clkreq_gpio to clkirqn failed", __func__);
			goto err_clkirq_gpio;
		}
		ret = request_irq(clkirqn, nfc_dev_clkirq_handler,
			IRQF_TRIGGER_RISING, "nfc_clk_irq", nfc_dev);
		if (ret) {
			pr_err("request_clkirq failed\n");
			goto err_request_irq_failed;
		}

		} else {
		pr_err("%s: clkirqn gpio not provided\n", __func__);
		goto err_clkirq_gpio;
		}
		/*enable  bb_clk*/
		bb_clk = devm_clk_get(&client->dev, "bb_clk");
		if (IS_ERR(bb_clk)) {
			pr_err("can't get nfc clock dts config: bb_clk\n");
		}
		source = devm_clk_get(&client->dev, "source");
		if (IS_ERR(source)) {
			pr_err("can't get nfc clock dts config: source\n");
		}
		clk_set_parent(bb_clk, source);

		enable = devm_clk_get(&client->dev, "enable");
		if (IS_ERR(enable)) {
			pr_err("can't get nfc clock dts config: enable\n");
		}

		clk_prepare_enable(bb_clk);
		clk_prepare_enable(enable);
		pr_info("%s: aux1 clk enable\n", __func__);

#endif
	nfc_dev->ven_gpio = platform_data.ven_gpio;
	nfc_dev->irq_gpio = platform_data.irq_gpio;
	nfc_dev->firm_gpio  = platform_data.firm_gpio;
	nfc_dev->ese_pwr_gpio  = platform_data.ese_pwr_gpio;
	nfc_dev->clkreq_gpio = platform_data.clkreq_gpio;
	/* init mutex and queues */
	init_waitqueue_head(&nfc_dev->read_wq);
	mutex_init(&nfc_dev->read_mutex);
	mutex_init(&nfc_dev->ese_status_mutex);
	spin_lock_init(&nfc_dev->irq_enabled_lock);

	nfc_dev->nfc_device.minor = MISC_DYNAMIC_MINOR;
	nfc_dev->nfc_device.name = "pn553";
	nfc_dev->nfc_device.fops = &nfc_dev_fops;

	
	ret = nfcc_hw_check(client, platform_data.ven_gpio, platform_data.firm_gpio);
	if (ret != 0) {
	pr_err("%s : don't have nfc hw!\n", __func__);
	goto err;
	}
	

	ret = misc_register(&nfc_dev->nfc_device);
	if (ret) {
		pr_err("%s: misc_register failed\n", __func__);
		goto err_misc_register;
	}
	/* NFC_INT IRQ */
	nfc_dev->irq_enabled = true;
	ret = request_irq(client->irq, nfc_dev_irq_handler,
			IRQF_TRIGGER_HIGH, client->name, nfc_dev);
	if (ret) {
		pr_err("request_irq failed\n");
		goto err_request_irq_failed;
	}
	device_init_wakeup(&client->dev, true);
	device_set_wakeup_capable(&client->dev, true);
	i2c_set_clientdata(client, nfc_dev);
	/*Enable IRQ and VEN*/
	pr_info("%s: enable irq & clkirq! NXP NFC exited successfully\n", __func__);
	nfc_enable_irq(nfc_dev);
	disable_irq(clkirqn);/*disenable clkirq*/
	enable_irq(clkirqn);/*enable clkirq*/
	/*call to platform specific probe*/
	ret = func(NFC_PLATFORM, _nfc_probe)(nfc_dev);
	if (ret != 0) {
		pr_err("%s: probing platform failed\n", __func__);
		goto err_request_irq_failed;
	};
	pr_info("%s: probing NXP NFC exited successfully\n", __func__);
	return 0;

err_request_irq_failed:
	misc_deregister(&nfc_dev->nfc_device);
err_misc_register:
	mutex_destroy(&nfc_dev->read_mutex);
	mutex_destroy(&nfc_dev->ese_status_mutex);
err_ese_pwr_gpio:
	gpio_free(platform_data.ese_pwr_gpio);
err_firm_gpio:
	gpio_free(platform_data.firm_gpio);
err_irq_gpio:
	gpio_free(platform_data.irq_gpio);
err_en_gpio:
	gpio_free(platform_data.ven_gpio);
err_mem:
	kfree(nfc_dev);
err_clkirq_gpio:
	gpio_free(platform_data.clkreq_gpio);
err:
	pr_err("%s: probing NXP NFC driver failed, check hardware\n", __func__);
	return ret;
}

static int nfc_remove(struct i2c_client *client)
{
	int ret = 0;
	struct nfc_dev *nfc_dev;

	pr_info("%s: remove device\n", __func__);
	nfc_dev = i2c_get_clientdata(client);
	if (!nfc_dev) {
		pr_err("%s: device doesn't exist anymore\n", __func__);
		ret = -ENODEV;
		goto err;
	}

	/*call to platform specific remove*/
	ret = func(NFC_PLATFORM, _nfc_remove)(nfc_dev);
	if (ret != 0) {
		pr_err("%s: platform failed\n", __func__);
		goto err;
	}
#ifdef CONFIG_NFC_ENABLE_BB_CLK
	clk_disable_unprepare(bb_clk);
	clk_disable_unprepare(enable);
	pr_err("%s BBCLK OFF\n", __func__);
#endif
	free_irq(client->irq, nfc_dev);
	misc_deregister(&nfc_dev->nfc_device);
	mutex_destroy(&nfc_dev->read_mutex);
	mutex_destroy(&nfc_dev->ese_status_mutex);
	gpio_free(nfc_dev->ese_pwr_gpio);
	gpio_free(nfc_dev->firm_gpio);
	gpio_free(nfc_dev->irq_gpio);
	gpio_free(nfc_dev->clkreq_gpio);
	gpio_free(nfc_dev->ven_gpio);
	kfree(nfc_dev);
err:
	return ret;
}

static const struct i2c_device_id nfc_id[] = {
		{ "pn544", 0 },
		{ }
};

static struct of_device_id nfc_match_table[] = {
	{.compatible = "nxp,pn544",},
	{}
};
MODULE_DEVICE_TABLE(of, nfc_match_table);

static struct i2c_driver nfc_driver = {
		.id_table   = nfc_id,
		.probe	  = nfc_probe,
		.remove	 = nfc_remove,
		.driver	 = {
				.owner = THIS_MODULE,
				.name  = "pn544",
				.of_match_table = nfc_match_table,
		},
};

static int __init nfc_dev_init(void)
{
	pr_info("Loading NXP NFC driver\n");
	return i2c_add_driver(&nfc_driver);
}
module_init(nfc_dev_init);

static void __exit nfc_dev_exit(void)
{
	pr_info("Unloading NXP NFC driver\n");
	i2c_del_driver(&nfc_driver);
}
module_exit(nfc_dev_exit);

MODULE_DESCRIPTION("NXP NFC driver");
MODULE_LICENSE("GPL");
