/*
 *Copyright (C) 2019 Spreadtrum Communications Inc.
 *
 *This software is licensed under the terms of the GNU General Public
 *License version 2, as published by the Free Software Foundation, and
 *may be copied, distributed, and modified under those terms.
 *
 *This program is distributed in the hope that it will be useful,
 *but WITHOUT ANY WARRANTY; without even the implied warranty of
 *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *GNU General Public License for more details.
 */

#define pr_fmt(fmt) "sprd-backlight: " fmt

#include <linux/backlight.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#include "sprd_bl.h"

#ifdef CONFIG_ZTE_LCD_COMMON_FUNCTION
#include "zte_lcd_common.h"
extern struct sprd_panel *g_zte_ctrl_pdata;
#define U_FULL_MAX_BL_LEVEL	1024
#endif

#define U_MAX_LEVEL	255
#define U_MIN_LEVEL	0

void sprd_backlight_normalize_map(struct backlight_device *bd, u16 *level)
{
	struct sprd_backlight *bl = bl_get_data(bd);

	if (!bl->num) {
		*level = DIV_ROUND_CLOSEST_ULL((bl->max_level - bl->min_level) *
			(bd->props.brightness - U_MIN_LEVEL),
			U_MAX_LEVEL - U_MIN_LEVEL) + bl->min_level;
	} else
		*level = bl->levels[bd->props.brightness];
}

int sprd_cabc_backlight_update(struct backlight_device *bd)
{
	struct sprd_backlight *bl = bl_get_data(bd);
	struct pwm_state state;
	u64 duty_cycle;

	mutex_lock(&bd->update_lock);

	if (bd->props.power != FB_BLANK_UNBLANK ||
	    bd->props.fb_blank != FB_BLANK_UNBLANK ||
	    bd->props.state & BL_CORE_FBBLANK) {
		mutex_unlock(&bd->update_lock);
		return 0;
	}

	pr_info("%s: cabc brightness level: %u\n", __func__, bl->cabc_level);

	pwm_get_state(bl->pwm, &state);
	duty_cycle = bl->cabc_level * state.period;
	do_div(duty_cycle, bl->scale);
	state.duty_cycle = duty_cycle;
	pwm_apply_state(bl->pwm, &state);

	mutex_unlock(&bd->update_lock);

	return 0;
}

static int sprd_pwm_backlight_update(struct backlight_device *bd)
{
	struct sprd_backlight *bl = bl_get_data(bd);
	struct pwm_state state;
	u64 duty_cycle;
	u16 level;

	#if defined(CONFIG_ZTE_LCD_COMMON_FUNCTION) && defined(CONFIG_ZTE_LCD_BACKLIGHT_LEVEL_CURVE)
	u16 bl_preset = bd->props.brightness;
	u16 bl_convert = bd->props.brightness;

	if (bl_preset != 0 && g_zte_ctrl_pdata != NULL
		&& g_zte_ctrl_pdata->zte_lcd_ctrl != NULL
		&& g_zte_ctrl_pdata->zte_lcd_ctrl->zte_convert_brightness != NULL) {
		bd->props.brightness =
			g_zte_ctrl_pdata->zte_lcd_ctrl->zte_convert_brightness(bl_preset, U_MAX_LEVEL);
	}
	#endif

	sprd_backlight_normalize_map(bd, &level);

	if (bd->props.power != FB_BLANK_UNBLANK ||
	    bd->props.fb_blank != FB_BLANK_UNBLANK ||
	    bd->props.state & BL_CORE_FBBLANK)
		level = 0;

	#if defined(CONFIG_ZTE_LCD_COMMON_FUNCTION) && defined(CONFIG_ZTE_LCD_BACKLIGHT_LEVEL_CURVE)
	bl_convert = level;

	if (g_zte_ctrl_pdata != NULL
		&& g_zte_ctrl_pdata->zte_lcd_ctrl != NULL
		&& g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_panel_name != NULL) {
		if (!strncmp(g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_panel_name,
				"lcd_tianma_hx83113a", strlen("lcd_tianma_hx83113a"))) {
			level = level * (U_FULL_MAX_BL_LEVEL) / bl->max_level;
			pr_info("%s: hx83113a_tianma lcd from %d to %d\n", __func__, bl_convert, level);
		}
	}

	if (level != 0 && level < bl->min_level)
		level = bl->min_level;
	pr_info("%s: brightness min:%u old: %u => zte:%u => %u => level:%u\n",
				__func__, bl->min_level, bl_preset, bd->props.brightness, bl_convert, level);
	#else
	pr_info("%s: brightness level:%u => %u\n",
				__func__, bd->props.brightness, level);
	#endif

	pwm_get_state(bl->pwm, &state);
	if (level > 0) {
		if (bl->cabc_en)
			duty_cycle = DIV_ROUND_CLOSEST_ULL(bl->cabc_level *
				level, bl->cabc_refer_level);
		else
			duty_cycle = level;

		pr_debug("pwm brightness level: %llu\n", duty_cycle);

		duty_cycle *= state.period;
		do_div(duty_cycle, bl->scale);
		state.duty_cycle = duty_cycle;
		state.enabled = true;
	} else {
		pr_debug("pwm brightness level: %u\n", level);

		state.duty_cycle = 0;
		state.enabled = false;
	}
	pwm_apply_state(bl->pwm, &state);

	return 0;
}

static const struct backlight_ops sprd_backlight_ops = {
	.update_status = sprd_pwm_backlight_update,
};

static int sprd_backlight_parse_dt(struct device *dev,
			struct sprd_backlight *bl)
{
	struct device_node *node = dev->of_node;
	struct property *prop;
	u32 value;
	int length;
	int ret;

	if (!node)
		return -ENODEV;

	/* determine the number of brightness levels */
	prop = of_find_property(node, "brightness-levels", &length);
	if (prop) {
		bl->num = length / sizeof(u32);

		/* read brightness levels from DT property */
		if (bl->num > 0) {
			size_t size = sizeof(*bl->levels) * bl->num;

			bl->levels = devm_kzalloc(dev, size, GFP_KERNEL);
			if (!bl->levels)
				return -ENOMEM;

			ret = of_property_read_u32_array(node,
							"brightness-levels",
							bl->levels, bl->num);
			if (ret < 0)
				return ret;
		}
	}

	ret = of_property_read_u32(node, "sprd,max-brightness-level", &value);
	if (!ret)
		bl->max_level = value;
	else
		bl->max_level = 255;

	ret = of_property_read_u32(node, "sprd,min-brightness-level", &value);
	if (!ret)
		bl->min_level = value;
	else
		bl->min_level = 0;

	ret = of_property_read_u32(node, "default-brightness-level", &value);
	if (!ret)
		bl->dft_level = value;
	else
		bl->dft_level = 25;

	ret = of_property_read_u32(node, "sprd,brightness-scale",
				   &value);
	if (!ret)
		bl->scale = value;
	else
		bl->scale = bl->max_level;

	return 0;
}

static int sprd_backlight_probe(struct platform_device *pdev)
{
	struct backlight_device *bd;
	struct pwm_state state;
	struct sprd_backlight *bl;
	int div, ret;

	bl = devm_kzalloc(&pdev->dev,
			sizeof(struct sprd_backlight), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;

	ret = sprd_backlight_parse_dt(&pdev->dev, bl);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to parse sprd backlight\n");
		return ret;
	}

	bl->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(bl->pwm)) {
		ret = PTR_ERR(bl->pwm);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "unable to request PWM\n");
		return ret;
	}

	pwm_init_state(bl->pwm, &state);

	ret = pwm_apply_state(bl->pwm, &state);
	if (ret) {
		dev_err(&pdev->dev, "failed to apply initial PWM state: %d\n",
			ret);
		return ret;
	}

	bd = devm_backlight_device_register(&pdev->dev,
			"sprd_backlight", &pdev->dev, bl,
			&sprd_backlight_ops, NULL);
	if (IS_ERR(bd)) {
		dev_err(&pdev->dev, "failed to register sprd backlight ops\n");
		return PTR_ERR(bd);
	}

	bd->props.max_brightness = 255;
	bd->props.state &= ~BL_CORE_FBBLANK;
	bd->props.power = FB_BLANK_UNBLANK;

	div = ((bl->max_level - bl->min_level) << 8) / 255;
	if (div > 0) {
		bd->props.brightness = (bl->dft_level << 8) / div;
	} else {
		dev_err(&pdev->dev, "failed to calc default brightness level\n");
		return -EINVAL;
	}

	backlight_update_status(bd);

	platform_set_drvdata(pdev, bd);

	pr_info("%s: done successfully!\n", __func__);

	return 0;
}

static const struct of_device_id sprd_backlight_of_match[] = {
	{ .compatible = "sprd,sharkl5pro-backlight" },
	{ }
};

MODULE_DEVICE_TABLE(of, pwm_backlight_of_match);

static struct platform_driver sprd_backlight_driver = {
	.driver		= {
		.name		= "sprd-backlight",
		.of_match_table	= sprd_backlight_of_match,
	},
	.probe		= sprd_backlight_probe,
};

module_platform_driver(sprd_backlight_driver);

MODULE_AUTHOR("Kevin Tang <kevin.tang@unisoc.com>");
MODULE_DESCRIPTION("SPRD Base Backlight Driver");
MODULE_LICENSE("GPL v2");
