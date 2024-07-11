#include "zte_lcd_common.h"

#ifdef CONFIG_ZTE_LCD_REG_DEBUG
extern void zte_lcd_reg_debug_func(void);
#endif

struct sprd_panel *g_zte_ctrl_pdata = NULL;
const char *zte_get_lcd_panel_name(void)
{
	if (g_zte_ctrl_pdata == NULL)
		return "no_panel_info";
	else
		return g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_panel_name;
}

static int zte_lcd_proc_info_show(struct seq_file *m, void *v)
{
	if (g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_panel_name) {
		seq_printf(m, "panel name=%s\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_panel_name);
	} else {
		seq_printf(m, "%s\n", "panel name is not detect");
	}

	if (g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_init_code_version) {
		seq_printf(m, "version=%s\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_init_code_version);
	} else {
		seq_printf(m, "%s\n", "version is not detect");
	}

	return 0;
}
static int zte_lcd_proc_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, zte_lcd_proc_info_show, NULL);
}
static const struct file_operations zte_lcd_common_func_proc_fops = {
	.owner = THIS_MODULE,
	.open = zte_lcd_proc_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
static int zte_lcd_proc_info_display(struct panel_info *panel_info)
{
	struct proc_dir_entry *proc_lcd_id = NULL;

	proc_lcd_id = proc_create_data("driver/lcd_id", 0, NULL, &zte_lcd_common_func_proc_fops, NULL);
	if (!proc_lcd_id) {
		ZTE_LCD_ERROR("%s:create driver/lcd_id error!\n", __func__);
		return -EPERM;
	}

	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_panel_name = of_get_property(panel_info->of_node,
		"zte,panel-name", NULL);

	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_init_code_version = of_get_property(panel_info->of_node,
		"zte,lcd-init-code-version", NULL);

	ZTE_LCD_INFO("%s:Panel Name = %s,init code version=%s\n", __func__,
		g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_panel_name,
		g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_init_code_version);
	return 0;
}

/********************lcd backlight level curve begin*****************/
#ifdef CONFIG_ZTE_LCD_BACKLIGHT_LEVEL_CURVE

const int zte_backlight_curve_matrix_max_350_lux[256] = {
#ifdef CONFIG_ZTE_LCD_BACKLIGHT_MIN_TO_1_SUPPORT
0, 1, 2, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,
#else
0, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,
#endif
10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 17, 18,
18, 19, 19, 20, 21, 21, 22, 22, 23, 23, 24, 25, 25, 26, 26, 27,
28, 28, 29, 29, 30, 31, 31, 32, 33, 33, 34, 35, 35, 36, 36, 37,
38, 38, 39, 40, 40, 41, 42, 43, 43, 44, 45, 45, 46, 47, 47, 48,
49, 50, 50, 51, 52, 52, 53, 54, 55, 55, 56, 57, 58, 58, 59, 60,
61, 61, 62, 63, 64, 64, 65, 66, 67, 68, 68, 69, 70, 71, 72, 73,
74, 75, 76, 77, 77, 78, 79, 80, 81, 82, 82, 83, 84, 85, 86, 87,
88, 88, 89, 90, 91, 92, 93, 94, 95, 96, 96, 97, 98, 99, 100, 101,
102, 103, 104, 105, 106, 107, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116,
117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132,
133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 146, 147,
148, 149, 151, 152, 153, 155, 156, 157, 159, 160, 162, 163, 164, 166, 167, 169,
170, 172, 173, 175, 176, 178, 179, 181, 183, 184, 186, 187, 189, 191, 192, 194,
196, 197, 199, 201, 203, 204, 206, 208, 210, 212, 214, 215, 217, 219, 221, 223,
225, 227, 229, 231, 233, 235, 237, 239, 241, 243, 245, 248, 250, 252, 254, 255
};

const int zte_backlight_curve_matrix_max_400_lux[256] = {
#ifdef CONFIG_ZTE_LCD_BACKLIGHT_MIN_TO_1_SUPPORT
0, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 8,
#else
0, 3, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 8,
#endif
8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16,
16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24,
25, 25, 26, 26, 27, 27, 28, 28, 29, 30, 30, 31, 31, 32, 32, 33,
34, 34, 35, 35, 36, 37, 37, 38, 38, 39, 40, 40, 41, 42, 42, 43,
43, 44, 45, 45, 46, 47, 47, 48, 49, 49, 50, 51, 51, 52, 53, 53,
54, 55, 55, 56, 57, 57, 58, 59, 59, 60, 61, 61, 62, 63, 64, 64,
65, 66, 66, 67, 68, 69, 69, 70, 71, 72, 72, 73, 74, 74, 75, 76,
77, 78, 78, 79, 80, 81, 81, 82, 83, 84, 84, 85, 86, 87, 88, 88,
89, 90, 91, 92, 92, 93, 94, 95, 96, 96, 97, 98, 99, 100, 101, 101,
102, 103, 104, 105, 106, 107, 107, 108, 109, 110, 111, 112, 113, 113, 114, 115,
116, 117, 118, 119, 120, 121, 121, 122, 123, 124, 125, 126, 127, 128, 129, 129,
130, 132, 133, 134, 136, 137, 139, 140, 142, 143, 145, 147, 148, 150, 151, 153,
155, 156, 158, 160, 162, 163, 165, 167, 169, 170, 172, 174, 176, 178, 180, 182,
184, 186, 188, 190, 192, 194, 196, 198, 200, 203, 205, 207, 209, 212, 214, 216,
219, 221, 223, 226, 228, 231, 233, 236, 238, 241, 243, 246, 249, 252, 254, 255
};

const int zte_backlight_curve_matrix_max_450_lux[256] = {
#ifdef CONFIG_ZTE_LCD_BACKLIGHT_MIN_TO_1_SUPPORT
0, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 6, 7, 7,
#else
0, 3, 3, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 6, 7, 7,
#endif
8, 8, 8, 9, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 13, 14,
14, 15, 15, 16, 16, 17, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21,
22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29,
30, 30, 31, 31, 32, 32, 33, 33, 34, 34, 35, 36, 36, 37, 37, 38,
38, 39, 39, 40, 41, 41, 42, 42, 43, 43, 44, 45, 45, 46, 46, 47,
48, 48, 49, 49, 50, 51, 51, 52, 52, 53, 54, 54, 55, 56, 56, 57,
57, 58, 59, 59, 60, 61, 61, 62, 63, 63, 64, 65, 65, 66, 67, 67,
68, 69, 69, 70, 71, 71, 72, 73, 73, 74, 75, 75, 76, 77, 78, 78,
79, 80, 80, 81, 82, 83, 83, 84, 85, 85, 86, 87, 88, 88, 89, 90,
91, 91, 92, 93, 94, 94, 95, 96, 97, 97, 98, 99, 100, 101, 101, 102,
103, 104, 105, 105, 106, 107, 108, 109, 109, 110, 111, 112, 112, 112, 113, 113,
114, 116, 117, 119, 120, 122, 123, 125, 126, 128, 130, 131, 133, 135, 136, 138,
140, 142, 143, 145, 147, 149, 151, 153, 155, 157, 159, 161, 163, 165, 167, 169,
171, 173, 176, 178, 180, 183, 185, 187, 190, 192, 194, 197, 199, 202, 205, 207,
210, 213, 215, 218, 221, 224, 226, 229, 232, 235, 238, 241, 244, 248, 251, 255
};
static int zte_convert_backlevel_function(int level, u32 bl_max)
{
	int bl, convert_level;

	if (level == 0)
		return 0;

	if (bl_max > 1023) {
		if (bl_max == 2047) {
			bl = level>>3;
		} else {
			bl = level>>4;
		}
	} else if (bl_max == 1023) {
		bl = level >> 2;
	} else {
		bl = level;
	}

	if (!bl && level)
		bl = 1;/*ensure greater than 0 and less than 16 equal to 1*/

	switch (g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_curve_mode) {
	case CURVE_MATRIX_MAX_350_LUX:
		convert_level = zte_backlight_curve_matrix_max_350_lux[bl];
		break;
	case CURVE_MATRIX_MAX_400_LUX:
		convert_level = zte_backlight_curve_matrix_max_400_lux[bl];
		break;
	case CURVE_MATRIX_MAX_450_LUX:
		convert_level = zte_backlight_curve_matrix_max_450_lux[bl];
		break;
	default:
		convert_level = zte_backlight_curve_matrix_max_450_lux[bl];
		break;
	}
	if (bl_max > 1023) {
		if (bl_max == 2047) {
			convert_level = (convert_level >= 255) ? 2047 : (convert_level<<3);
		} else {
			convert_level = (convert_level >= 255) ? 4095 : (convert_level<<4);
		}
	} else if (bl_max == 1023) {
		convert_level = (convert_level >= 255) ? 1023 : (convert_level << 2);
	}

	#if defined(CONFIG_ZTE_LCD_BACKLIGHT_MIN_LEVEL_LIMIT)
	if (g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_register_len == 12) {
		if (convert_level != 0 && convert_level < 64) {
			convert_level = 64;
		}
	} else if (g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_register_len == 11) {
		if (convert_level != 0 && convert_level < 32) {
			convert_level = 32;
		}
	} else if (g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_register_len == 10) {
		if (convert_level != 0 && convert_level < 16) {
			convert_level = 16;
		}
	} else {
		if (convert_level != 0 && convert_level < 4) {
			convert_level = 4;
		}
	}
	#endif
	if (convert_level <= g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_min_value)
		convert_level = g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_min_value;

	return convert_level;
}
#endif
/********************lcd backlight level curve end*****************/

/********************lcd common function start*****************/

#ifdef CONFIG_ZTE_LCD_TP_GESTURE_POWER_OFF_SEQ
static int of_parse_tp_gesture_power_off_seq(struct device_node *node,
				struct panel_info *panel){
	struct property *prop = NULL;
	int bytes = 0;
	unsigned int *p = NULL;
	int rc = 0;

	g_zte_ctrl_pdata->zte_lcd_ctrl->have_tp_gesture_power_off_seq =
			of_property_read_bool(node, "zte,have-tp-gesture-power_off-sequence");
	if (g_zte_ctrl_pdata->zte_lcd_ctrl->have_tp_gesture_power_off_seq) {
		prop = of_find_property(node, "power-off-tp-gesture-sequence", &bytes);
		if (!prop) {
			pr_err("error: power-off-tp-gesture-sequence property not found");
			return -EINVAL;
		}

		p = kzalloc(bytes, GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		rc = of_property_read_u32_array(node,
				"power-off-tp-gesture-sequence", p, bytes / 4);
		if (rc) {
			pr_err("get power-off-sequence failed\n");
			kfree(p);
			return -EINVAL;
		}

		panel->pwr_off_tp_gesture_seq.items = bytes / 12;
		panel->pwr_off_tp_gesture_seq.timing = (struct gpio_timing *)p;

	}
	return 0;
}
#endif

static void zte_lcd_panel_parse_dt(struct mipi_dsi_device *dispc, struct device_node *node)
{
	int rc;
	u32 tmp;
#ifdef CONFIG_ZTE_LCD_BACKLIGHT_LEVEL_CURVE
	const char *data;
#endif
#ifdef CONFIG_ZTE_LCD_CABC3_EXTREME_POWER_SAVE
	const void *p = NULL;
	int bytes = 0;
#endif

#ifdef CONFIG_ZTE_LCD_BACKLIGHT_LEVEL_CURVE
	data = of_get_property(node, "zte,lcm_backlight_curve_mode", NULL);
	if (data) {
		if (!strcmp(data, "lcd_brightness_max_350_lux"))
			g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_curve_mode = CURVE_MATRIX_MAX_350_LUX;
		else if (!strcmp(data, "lcd_brightness_max_400_lux"))
			g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_curve_mode = CURVE_MATRIX_MAX_400_LUX;
		else if (!strcmp(data, "lcd_brightness_max_450_lux"))
			g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_curve_mode = CURVE_MATRIX_MAX_450_LUX;
		else
			g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_curve_mode = CURVE_MATRIX_MAX_450_LUX;
	} else
		g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_curve_mode = CURVE_MATRIX_MAX_450_LUX;

	ZTE_LCD_INFO("%s:dtsi_mode=%s matrix_mode=%d\n", __func__, data,
			g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_curve_mode);
#endif

#ifdef CONFIG_ZTE_LCDBL_I2C_CTRL_VSP_VSN
	rc = of_property_read_u32(node, "zte,lcd_bl_vsp_vsn_voltage", &tmp);
	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_vsp_vsn_voltage = (!rc ? tmp : 0x0f);
	ZTE_LCD_INFO("lcd vsp_vsn=%x\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_vsp_vsn_voltage);

	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_vsp_vsn_enhance_disable = of_property_read_bool(node,
		"zte,lcd-bl-vsp-vsn-enhance-disable");
	ZTE_LCD_INFO("enhance_disable=%d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_vsp_vsn_enhance_disable);
#endif

#ifdef CONFIG_ZTE_LCD_DCSBL_CABC_GRADIENT
	g_zte_ctrl_pdata->zte_lcd_ctrl->close_dynamic_dimming = of_property_read_bool(node,
		"zte,lcd-close-dynamic-dimming");
	ZTE_LCD_INFO("close_dynamic_dimming is 0x%x\n", g_zte_ctrl_pdata->zte_lcd_ctrl->close_dynamic_dimming);
#endif

	rc = of_property_read_u32(node, "zte,lcd-backlight-register-bit-length", &tmp);
	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_register_len = (!rc ? tmp : 8);
	ZTE_LCD_INFO("tmp=%d lcd bl length=%d\n", tmp, g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_register_len);

	rc = of_property_read_u32(node, "zte,lcd-backlight-min-value", &tmp);
	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_min_value = (!rc ? tmp : 0);
	ZTE_LCD_INFO("rc=%d tmp=%d value=%d\n", rc, tmp, g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_min_value);

	rc = of_property_read_u32(node, "zte,lcd-backlight-register-is-ilitek", &tmp);
	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_register_len_is_ilitek = (!rc ? tmp : 0);
	ZTE_LCD_INFO("tmp=%d lcd bl is ilitek=%d\n", tmp,
			g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_bl_register_len_is_ilitek);
#ifdef CONFIG_ZTE_LCD_ESD_TWICE
	g_zte_ctrl_pdata->zte_lcd_ctrl->is_lcd_esd_twice = of_property_read_bool(node,
		"zte,lcd-esd-twice");
	ZTE_LCD_INFO("is_lcd_esd_twice is 0x%x\n", g_zte_ctrl_pdata->zte_lcd_ctrl->is_lcd_esd_twice);
#endif

#ifdef CONFIG_ZTE_LCD_DISABLE_SSC
	g_zte_ctrl_pdata->zte_lcd_ctrl->is_disable_ssc = of_property_read_bool(node,
		"zte,lcd-disable-ssc");
	ZTE_LCD_INFO("is_disable_ssc is 0x%x\n", g_zte_ctrl_pdata->zte_lcd_ctrl->is_disable_ssc);
#endif
#ifdef CONFIG_ZTE_LCD_DELAY_OPEN_BL
	rc = of_property_read_u32(node, "zte,lcd-delay-open-bl-value", &tmp);
	if (!rc) {
		g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_delay_open_bl_value = tmp;
	} else {
		g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_delay_open_bl_value = 0;
	}
	ZTE_LCD_INFO("lcd_delay_open_bl_value is 0x%x\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_delay_open_bl_value);
#endif
#ifdef CONFIG_ZTE_LCD_TP_GESTURE_POWER_OFF_SEQ
	rc = of_parse_tp_gesture_power_off_seq(node, g_zte_ctrl_pdata->zte_lcd_ctrl->panel_info);
	if (rc < 0) {
		g_zte_ctrl_pdata->zte_lcd_ctrl->have_tp_gesture_power_off_seq = false;
		ZTE_LCD_ERROR("parse tp gesture power off seq failed.\n");
	}
#endif
#ifdef CONFIG_ZTE_LCD_SUPPORT_ER68577
	g_zte_ctrl_pdata->zte_lcd_ctrl->is_support_er68577 = of_property_read_bool(node, "zte,lcd-support-er68577");
	ZTE_LCD_INFO("is_support_er68577 = %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->is_support_er68577);
#endif
	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_ctrl_tp_resetpin = of_property_read_bool(node,
		"zte,lcd-ctrl-tp-resetpin");
	ZTE_LCD_INFO("lcd_ctrl_tp_resetpin = %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_ctrl_tp_resetpin);

	g_zte_ctrl_pdata->zte_lcd_ctrl->reset_before_vsp = of_property_read_bool(node,
			"zte,lcd-reset-pin-before-vsp");
	ZTE_LCD_INFO("reset_before_vsp=%d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->reset_before_vsp);

	if (of_property_read_bool(node, "zte,lcd-reset-down-before-vsp")) {
		g_zte_ctrl_pdata->zte_lcd_ctrl->reset_down_before_vsp = true;
	} else {
		g_zte_ctrl_pdata->zte_lcd_ctrl->reset_down_before_vsp = false;
	}
	ZTE_LCD_INFO("reset_down_before_vsp=%d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->reset_down_before_vsp);

	rc = of_property_read_u32(node, "zte,lcd-reset-down-delay-time", &tmp);
	if (!rc) {
		g_zte_ctrl_pdata->zte_lcd_ctrl->reset_down_delay_time = tmp;
	} else {
		g_zte_ctrl_pdata->zte_lcd_ctrl->reset_down_delay_time = 0;
	}
	ZTE_LCD_INFO("reset_down_delay_time is %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->reset_down_delay_time);

	g_zte_ctrl_pdata->zte_lcd_ctrl->backlight_ctrl_by_lcdic_pwm = true;
}
#ifdef CONFIG_ZTE_LCD_SUPPORT_ER68577
uint32_t zte_abnormal_shutdown_times = 0;
extern uint32_t zte_abnormal_shutdown_flag;
extern uint32_t zte_er68577_need_bist_mode;
static ssize_t show_abnormal_shutdown_flag(struct device *d, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 80, "%d\n", zte_abnormal_shutdown_flag);
}
static ssize_t store_abnormal_shutdown_flag(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	input = input > 0 ? 1 : 0;
	DRM_INFO("%s val %d\n", __func__, input);

	if (input) {
		zte_abnormal_shutdown_flag = 1;
	} else {
		zte_abnormal_shutdown_flag = 0;
	}

	return count;
}
static DEVICE_ATTR(abnormal_shutdown_flag, 0660, show_abnormal_shutdown_flag, store_abnormal_shutdown_flag);
static ssize_t show_abnormal_shutdown_times(struct device *d, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 80, "%d\n", zte_abnormal_shutdown_times);
}
static ssize_t store_abnormal_shutdown_times(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	DRM_INFO("%s val %d\n", __func__, input);

	if (input) {
		zte_abnormal_shutdown_times = input;
	}

	return count;
}
static DEVICE_ATTR(abnormal_shutdown_times, 0660, show_abnormal_shutdown_times, store_abnormal_shutdown_times);
#endif


static ssize_t zte_show_esd_num(struct device *d, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 80, "esd num:0x%x\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_esd_num);
}
static DEVICE_ATTR(esd_num, 0660, zte_show_esd_num, NULL);

extern int sprd_panel_unprepare(struct drm_panel *p);
extern int sprd_panel_disable(struct drm_panel *p);
extern int sprd_panel_prepare(struct drm_panel *p);
extern int sprd_panel_enable(struct drm_panel *p);
static bool cali_mode;
static int boot_mode_check(char *str)
{
	if (str != NULL && !strncmp(str, "cali", strlen("cali")))
		cali_mode = true;
	else
		cali_mode = false;
	return 0;
}
__setup("androidboot.mode=", boot_mode_check);

int zte_cali_lcd_pm_suspend(void)
{
	DRM_INFO("%s", __func__);

	if (g_zte_ctrl_pdata) {
		sprd_panel_disable(&g_zte_ctrl_pdata->base);
		sprd_panel_unprepare(&g_zte_ctrl_pdata->base);
	}

	return 0;
}
int zte_cali_lcd_pm_resume(void)
{
	DRM_INFO("%s", __func__);
	if (g_zte_ctrl_pdata) {
		sprd_panel_prepare(&g_zte_ctrl_pdata->base);
		sprd_panel_enable(&g_zte_ctrl_pdata->base);
	}

	return 0;
}
static ssize_t zte_cali_lcd_suspend_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;

	if (!cali_mode) {
		DRM_INFO("%s Not Calibration Mode!\n", __func__);
		return -EINVAL;
	}

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	input = input > 0 ? 1 : 0;
	DRM_INFO("%s val %d\n", __func__, input);

	if (input) {
		zte_cali_lcd_pm_suspend();
	} else {
		zte_cali_lcd_pm_resume();
	}

	return count;
}
static DEVICE_ATTR(lcd_suspend, 0220, NULL, zte_cali_lcd_suspend_store);

static struct attribute *zte_lcd_fs_attrs[] = {
	&dev_attr_esd_num.attr,
	&dev_attr_lcd_suspend.attr,
	#ifdef CONFIG_ZTE_LCD_SUPPORT_ER68577
	&dev_attr_abnormal_shutdown_flag.attr,
	&dev_attr_abnormal_shutdown_times.attr,
	#endif
	NULL,
};

static struct attribute_group zte_lcd_attrs_group = {
	.attrs = zte_lcd_fs_attrs,
};

/********************HBM MODE begin****************/
/*file path: proc/driver/lcd_hbm/*/
#ifdef CONFIG_ZTE_LCD_HBM_CTRL
struct mipi_dsi_device *g_dispc = NULL;
struct kgsl_device *ksgl_uevent_device = NULL;
#define PARTIAL_HBM_SOURCE_EM_THRESHOLD     (0x155 + 1)
#define PARTIAL_HBM_SOURCE_EM_THRESHOLD_240NIT    (0x400 + 1)
#define HBM_ENABLE_CMD_ADDR     0X83

int panel_global_hbm_ctrl_RM692C9display(u32 setHbm)
{
	u8 cmd_dim_enable[2] = {0x53, 0x28};
	u8 cmd_dim_disable[2] = {0x53, 0xe8};
	u8 cmd_default_page[2] = {0xfe, 0x00};
	int err = 0;

	struct mipi_dsi_device *dsi;

	dsi = g_dispc;
	if (!dsi) {
		pr_info("HBM: No device\n");
		return -ENOMEM;
	}
	err = mipi_dsi_dcs_write(dsi, cmd_default_page[0], &cmd_default_page[1], sizeof(cmd_default_page) - 1);
	if (err < 0)
		goto error;
	if (setHbm == 1) {
		err = mipi_dsi_dcs_write(dsi, cmd_dim_disable[0], &cmd_dim_disable[1], sizeof(cmd_dim_disable) - 1);
		if (err < 0)
			goto error;
		pr_info("HBM: Send enable HBM cmds to panel\n");
	} else {
		err = mipi_dsi_dcs_write(dsi, cmd_dim_enable[0], &cmd_dim_enable[1], sizeof(cmd_dim_enable) - 1);
		if (err < 0)
			goto error;
		pr_info("HBM: Send disable HBM cmds to panel\n");
	}
	err = mipi_dsi_dcs_write(dsi, cmd_default_page[0], &cmd_default_page[1], sizeof(cmd_default_page) - 1);
	if (err < 0)
		goto error;
	usleep_range(5000, 5100);
	return err;
error:
	pr_err("HBM: send cmds failed\n");
	return err;
}

struct platform_device *sprd_uevent_device = NULL;


#define HBM_CMDS_ON_LEN     26
#define HBM_CMDS_OFF_LEN    24
int panel_hbm_ctrl_RM692C9display(u32 setHbm)
{
	int i, err = 0;
	int err_panel_off = -2;
	u8 hbm_cmds_on[HBM_CMDS_ON_LEN][3] = {
								{0xfe, 0xa1, 0x00},
								{0xe3, 0xa0, 0x01},
								{0xFE, 0x78, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFE, 0x78, 0x00},
								{0x44, 0xC0, 0x00},
								{0x47, 0x07, 0x00},
								{0x5C, 0x40, 0x00},
								{0x7D, 0x40, 0x00},
								{0x80, 0x0F, 0x00},
								{0x8A, 0xFF, 0x00},
								{0x9E, 0xC0, 0x00},
								{0xFE, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0x83, 0x01, 0x00},
								{0xFB, 0xAA, 0x00}
							};
	u8 hbm_cmds_off[HBM_CMDS_OFF_LEN][3] = {
								{0xFE, 0x78, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFE, 0x78, 0x00},
								{0x44, 0xFE, 0x00},
								{0x47, 0x02, 0x00},
								{0x5C, 0x06, 0x00},
								{0x7D, 0x7C, 0x00},
								{0x80, 0x00, 0x00},
								{0x8A, 0xCC, 0x00},
								{0x9E, 0xFE, 0x00},
								{0xfe, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0xFC, 0x00, 0x00},
								{0x83, 0x00, 0x00},
								{0xFB, 0xAA, 0x00}
							};

	struct mipi_dsi_device *dsi;

	dsi = g_dispc;
	if (!dsi) {
		pr_info("HBM: No device\n");
		return -ENOMEM;
	}

	if (setHbm == 1) {
		for (i = 0; i < HBM_CMDS_ON_LEN; i++) {
			if (!g_zte_ctrl_pdata->is_enabled) {
				pr_info("HBM: panel has been powered off, mipi_dsi_dcs_write skip\n");
				return err_panel_off;
			}

			err = mipi_dsi_dcs_write(dsi, hbm_cmds_on[i][0], &hbm_cmds_on[i][1], 1);
			if (err < 0)
				goto error;
			if (hbm_cmds_on[i][2])
				usleep_range(hbm_cmds_on[i][2]*1000, ((hbm_cmds_on[i][2]*1000)+10));
		}
		pr_info("HBM: Send enable HBM cmds to panel\n");
	} else {
		for (i = 0; i < HBM_CMDS_OFF_LEN; i++) {
			if (!g_zte_ctrl_pdata->is_enabled) {
				pr_info("HBM: panel has been powered off, mipi_dsi_dcs_write skip\n");
				return err_panel_off;
			}

			err = mipi_dsi_dcs_write(dsi, hbm_cmds_off[i][0], &hbm_cmds_off[i][1], 1);
			if (err < 0)
				goto error;
			if (hbm_cmds_off[i][2])
				usleep_range(hbm_cmds_off[i][2]*1000, ((hbm_cmds_off[i][2]*1000)+10));
		}
		pr_info("HBM: Send disable HBM cmds to panel\n");
	}
	/*usleep_range(5000, 6000);*/

error:
	return err;
}


static int zte_lcd_proc_hbm_show(struct seq_file *m, void *v)
{

	seq_printf(m, "%d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hbm_mode);
	pr_info("HBM: read value = %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hbm_mode);

	return 0;
}

static DEFINE_MUTEX(zte_panel_lock);
extern int sprd_panel_set_hbm(u32 setHbm);
static char *envp[3] = {
	"HBM_STATUS=OFF",
	"HBM_SET_RESULT=SUCCESSFUL",
	NULL
};
static ssize_t zte_lcd_proc_hbm_write(struct file *file, const char __user *buffer,
			size_t count, loff_t *f_pos)
{
	char *tmp = kzalloc((count+1), GFP_KERNEL);
	u32 mode;
	int ret = 0, retry_times = 3, retry_times_max = 3;
	int delay_times = 0, delay_times_max = 10;

	if (!tmp)
		return -ENOMEM;

	if (copy_from_user(tmp, buffer, count)) {
		kfree(tmp);
		return -EFAULT;
	}

	mode = *tmp - '0';
	pr_info("HBM: mode=%d lcd_hbm_mode=%d last_dpms=%d dpms=%d\n",
				mode, g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hbm_mode,
				g_zte_ctrl_pdata->last_dpms, g_zte_ctrl_pdata->dpms);

	mutex_lock(&zte_panel_lock);

	if (g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hbm_mode != mode) {
		while (g_zte_ctrl_pdata->last_dpms != DRM_MODE_DPMS_ON
			&& g_zte_ctrl_pdata->last_dpms != DRM_MODE_DPMS_STANDBY) {
			msleep(20);
			delay_times++;
			pr_info("HBM: panel is not alive, delay_times=%d last_dpms=%d dpms=%d\n",
					delay_times, g_zte_ctrl_pdata->last_dpms, g_zte_ctrl_pdata->dpms);

			if (!g_zte_ctrl_pdata->is_enabled) {
				pr_info("HBM: panel has been powered off, skip!\n");
				goto error;
			}

			if (delay_times == delay_times_max) {
				pr_info("HBM: panel is already not alive, could't set hbm!\n");
				goto error;
			}
		}

		while (retry_times) {
			if (retry_times == retry_times_max) {
				pr_info("HBM: Set new mode = %d\n",	mode);
			} else {
				pr_info("HBM: Set new mode = %d, retry_times = %d\n",
						mode, retry_times_max - retry_times);
			}

			if (!g_zte_ctrl_pdata->is_enabled) {
				pr_info("HBM: panel has been powered off, do't set hbm!\n");

				goto error;
			}

			ret = sprd_panel_set_hbm(mode);
			if (ret == 0) {
				if (g_zte_ctrl_pdata->last_dpms == DRM_MODE_DPMS_STANDBY)
					msleep(100);
				g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hbm_mode = mode;
				pr_info("HBM Set new mode successful, new mode %d\n", mode);
				break;
			}

			retry_times--;
		}

		if (g_zte_ctrl_pdata->is_enabled) {
			if (mode == 0)
				envp[0] = "HBM_STATUS=OFF";
			else
				envp[0] = "HBM_STATUS=ON";

			if (ret == 0)
				envp[1] = "HBM_SET_RESULT=SUCCESSFUL";
			else
				envp[1] = "HBM_SET_RESULT=FAILED";
			envp[2] = NULL;

			if (sprd_uevent_device && sprd_uevent_device != NULL) {
				pr_info("HBM set event: %s %s\n", envp[0], envp[1]);
				kobject_uevent_env(&sprd_uevent_device->dev.kobj, KOBJ_CHANGE, envp);
			} else {
				pr_info("HBM: sprd_uevent_device is NULL\n");
			}
		} else {
			pr_info("HBM: panel has been powered off, do't set event!\n");
		}
	} else
		pr_info("HBM: New mode is same as old ,do nothing! mode = %d\n",
				g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hbm_mode);

	kfree(tmp);

	mutex_unlock(&zte_panel_lock);
	return count;

error:
	if (g_zte_ctrl_pdata->is_enabled) {
		if (mode == 0)
			envp[0] = "HBM_STATUS=OFF";
		else
			envp[0] = "HBM_STATUS=ON";
		envp[1] = "HBM_SET_RESULT=FAILED";
		envp[2] = NULL;

		if (sprd_uevent_device && sprd_uevent_device != NULL) {
			pr_info("HBM set error event: %s %s\n", envp[0], envp[1]);
			kobject_uevent_env(&sprd_uevent_device->dev.kobj, KOBJ_CHANGE, envp);
		}
	} else {
		pr_info("HBM: panel has been powered off, do't set event!\n");
	}

	kfree(tmp);
	mutex_unlock(&zte_panel_lock);
	return count;
}

static int zte_lcd_proc_hbm_open(struct inode *inode, struct file *file)
{
	return single_open(file, zte_lcd_proc_hbm_show, NULL);
}

static const struct file_operations zte_lcd_hbm_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= zte_lcd_proc_hbm_open,
	.read		= seq_read,
	.write		= zte_lcd_proc_hbm_write,
	.llseek		= seq_lseek,
	.release		= single_release,
};

static int zte_lcd_hbm_ctrl(struct mipi_dsi_device *dispc)
{
	proc_create("driver/lcd_hbm", 0664, NULL, &zte_lcd_hbm_proc_fops);

	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hbm_mode = 0;
	pr_info("HBM:  = %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hbm_mode);

	return 0;
}

/********************AOD BRIGHTNESS begin****************/
/*file path: proc/driver/lcd_aod_bl/*/
#define AOD_BL_CMDS_LEN     2
int panel_set_aod_brightness(u32 level)
{
	u8 aod_bl_l_cmds[AOD_BL_CMDS_LEN][3] = {
								{0xFE, 0x00, 0x00},
								{0x51, 0x03, 0x20}
							};
	u8 aod_bl_m_cmds[AOD_BL_CMDS_LEN][3] = {
								{0xFE, 0x00, 0x00},
								{0x51, 0x07, 0xff}
							};
	u8 aod_bl_h_cmds[AOD_BL_CMDS_LEN][3] = {
								{0xFE, 0x00, 0x00},
								{0x51, 0x0f, 0xff}
							};
	int i = 0;
	int err = 0;
	int err_panel_off = -2;

	struct mipi_dsi_device *dsi;

	dsi = g_dispc;
	if (!dsi) {
		pr_info("AOD: No device");
		return -ENOMEM;
	}
	if (!g_zte_ctrl_pdata->is_enabled) {
		pr_info("AOD: panel has been powered off, skip\n");
		return -ENOMEM;
	}
	mutex_lock(&zte_panel_lock);
	if (level == 0) {
		if (!g_zte_ctrl_pdata->is_enabled) {
			pr_info("HBM: panel has been powered off, mipi_dsi_dcs_write skip\n");
			return err_panel_off;
		}

		err = mipi_dsi_dcs_write(dsi, aod_bl_l_cmds[0][0], &aod_bl_l_cmds[0][1], 1);
		if (err < 0)
			goto error;
		err = mipi_dsi_dcs_write(dsi, aod_bl_l_cmds[1][0], &aod_bl_l_cmds[1][1], 2);
		if (err < 0)
			goto error;

		pr_info("drm_lcd aod_bl: Send aod_bl_l_cmds cmds to panel\n");
	} else if (level == 1) {
		if (!g_zte_ctrl_pdata->is_enabled) {
			pr_info("HBM: panel has been powered off, mipi_dsi_dcs_write skip\n");
			return err_panel_off;
		}

		err = mipi_dsi_dcs_write(dsi, aod_bl_m_cmds[i][0], &aod_bl_m_cmds[i][1], 1);
		if (err < 0)
			goto error;
		err = mipi_dsi_dcs_write(dsi, aod_bl_m_cmds[1][0], &aod_bl_m_cmds[1][1], 2);
		if (err < 0)
			goto error;
		pr_info("drm_lcd aod_bl: Send aod_bl_m_cmds cmds to panel\n");
	} else {
		if (!g_zte_ctrl_pdata->is_enabled) {
			pr_info("HBM: panel has been powered off, mipi_dsi_dcs_write skip\n");
			return err_panel_off;
		}

		err = mipi_dsi_dcs_write(dsi, aod_bl_h_cmds[i][0], &aod_bl_h_cmds[i][1], 1);
		if (err < 0)
			goto error;
		err = mipi_dsi_dcs_write(dsi, aod_bl_h_cmds[1][0], &aod_bl_h_cmds[1][1], 2);
		if (err < 0)
			goto error;
		pr_info("drm_lcd aod_bl: Send aod_bl_h_cmds cmds to panel\n");
	}
	mutex_unlock(&zte_panel_lock);

	pr_info("drm_lcd: set aod_bl successful, new level  = %d\n",
			g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_aod_brightness);

	return err;

error:
	mutex_unlock(&zte_panel_lock);
	pr_err("drm_lcd: aod_bl send cmds failed");
	return err;

}

int panel_is_in_aod_mode(void)
{
	if (g_zte_ctrl_pdata->last_dpms == DRM_MODE_DPMS_STANDBY)
		return 1;
	else
		return 0;
}

extern int sprd_panel_set_aod_bl(u32 level);
static ssize_t zte_lcd_proc_aod_bl_write(struct file *file, const char __user *buffer,
			size_t count, loff_t *f_pos)
{
	char *tmp = kzalloc((count+1), GFP_KERNEL);
	u32 level;

	if (!tmp)
		return -ENOMEM;

	if (copy_from_user(tmp, buffer, count)) {
		kfree(tmp);
		return -EFAULT;
	}

	level = *tmp - '0';
	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_aod_brightness = level;
	pr_info("AOD set new bl level: %d\n", level);

	if (panel_is_in_aod_mode() == 1) /* set brightness here if already in aod mode*/
		sprd_panel_set_aod_bl(level);
	else
		pr_info("AOD save new bl level, not in aod mode now.\n");

	kfree(tmp);
	return count;
}
static int zte_lcd_proc_aod_bl_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_aod_brightness);
	pr_info("AOD brightness:  = %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_aod_brightness);
	return 0;
}
static int zte_lcd_proc_aod_bl_open(struct inode *inode, struct file *file)
{
	return single_open(file, zte_lcd_proc_aod_bl_show, NULL);
}
static const struct file_operations zte_lcd_aod_bl_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= zte_lcd_proc_aod_bl_open,
	.read		= seq_read,
	.write		= zte_lcd_proc_aod_bl_write,
	.llseek		= seq_lseek,
	.release		= single_release,
};
static int zte_lcd_set_aod_brightness(struct mipi_dsi_device *dispc)
{
	proc_create("driver/lcd_aod_bl", 0664, NULL, &zte_lcd_aod_bl_proc_fops);

	/* 0 - low level, 1 - middle level(default), 2 - high level*/
	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_aod_brightness = 1;

	pr_info("AOD brightness level:  = %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_aod_brightness);

	return 0;
}
/********************AOD BRIGHTNESS end***********************/

#ifdef CONFIG_ZTE_LCD_E1_PANEL
/*file path: proc/driver/lcd_elvdd/*/
#define ELVDD_CMDS_LEN     3
/*************************************************************
 * set ELVDD Voltage
 * file path: proc/driver/lcd_elvdd/
 * set ELVDD in different Volt
 * ELVDD 4.6V - 0 default
 * ELVDD 4.7V - 1 high
*************************************************************/
int panel_set_lcd_elvdd(u32 level)
{
	u8 lcd_elvdd_default_cmds[ELVDD_CMDS_LEN][3] = {
								{0xFE, 0x40, 0x00},
								{0xB3, 0x53, 0x11},
								{0xFE, 0x00, 0x00}
							};
	u8 lcd_elvdd_high_cmds[ELVDD_CMDS_LEN][3] = {
								{0xFE, 0x40, 0x00},
								{0xB3, 0x54, 0x11},
								{0xFE, 0x00, 0x00}
							};
	int i = 0;
	int err = 0;

	struct mipi_dsi_device *dsi;

	dsi = g_dispc;
	if (!dsi) {
		pr_info("lcd_elvdd: No device");
		return -ENOMEM;
	}

	if (level == g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot_ic) {
		pr_info("%s: lcd_elvdd is same as lcd_elvdd_ic, skip\n", __func__);
		return err;
	}

	mutex_lock(&zte_panel_lock);
	usleep_range(17000, 17010);
	if (level == 1) {
		for (i = 0; i < ELVDD_CMDS_LEN; i++) {
			err = mipi_dsi_dcs_write(dsi, lcd_elvdd_high_cmds[i][0], &lcd_elvdd_high_cmds[i][1], 1);
			if (err < 0)
				goto error;
			if (lcd_elvdd_high_cmds[i][2])
				usleep_range(lcd_elvdd_high_cmds[i][2]*1000, ((lcd_elvdd_high_cmds[i][2]*1000)+10));
		}

		pr_info("drm_lcd lcd_elvdd: Send lcd_elvdd_high_cmds cmds to panel\n");
	} else {
		for (i = 0; i < ELVDD_CMDS_LEN; i++) {
			err = mipi_dsi_dcs_write(dsi, lcd_elvdd_default_cmds[i][0], &lcd_elvdd_default_cmds[i][1], 1);
			if (err < 0)
				goto error;
			if (lcd_elvdd_default_cmds[i][2])
				usleep_range(lcd_elvdd_default_cmds[i][2]*1000, ((lcd_elvdd_default_cmds[i][2]*1000)+10));
		}

		pr_info("drm_lcd lcd_elvdd: Send lcd_elvdd_default_cmds cmds to panel\n");
	}
	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot_ic = level;
	mutex_unlock(&zte_panel_lock);

	pr_info("drm_lcd: set lcd_elvdd successful, new level  = %d\n",
			g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot_ic);

	return err;

error:
	mutex_unlock(&zte_panel_lock);
	pr_err("drm_lcd: lcd_elvdd send cmds failed");
	return err;
}

extern int sprd_panel_set_lcd_elvdd(u32 level);
static ssize_t zte_lcd_proc_elvdd_write(struct file *file, const char __user *buffer,
			size_t count, loff_t *f_pos)
{
	char *tmp = kzalloc((count+1), GFP_KERNEL);
	u32 mode;

	if (!tmp)
		return -ENOMEM;

	if (copy_from_user(tmp, buffer, count)) {
		kfree(tmp);
		return -EFAULT;
	}

	mode = *tmp - '0';
	if (g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot != mode) {
		sprd_panel_set_lcd_elvdd(mode);
	} else {
		pr_info("MSM_LCD lcd_elvdd_vlot is same as pre mode\n");
	}

	kfree(tmp);
	return count;
}
static int zte_lcd_proc_elvdd_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d ic_value %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot,
				g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot_ic);
	pr_info("MSM_LCD ELVDD: %d ic_value %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot,
				g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot_ic);
	return 0;
}
static int zte_lcd_proc_elvdd_open(struct inode *inode, struct file *file)
{
	return single_open(file, zte_lcd_proc_elvdd_show, NULL);
}
static const struct file_operations zte_lcd_elvdd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= zte_lcd_proc_elvdd_open,
	.read		= seq_read,
	.write		= zte_lcd_proc_elvdd_write,
	.llseek		= seq_lseek,
	.release		= single_release,
};
static int zte_lcd_set_elvdd_node(struct mipi_dsi_device *dispc)
{
	proc_create("driver/lcd_elvdd", 0664, NULL, &zte_lcd_elvdd_proc_fops);

	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot = 0;
	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot_ic = 0;
	pr_info("ELVDD: %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_elvdd_vlot);
	return 0;
}
#endif

extern int sprd_oled_set_brightness(struct backlight_device *bdev);
extern struct backlight_device *g_oeld_bdev;
/*
setHdr flag for LCD brightness:
HDR OFF - 0
HDR ON - 1
AUTO SENSOR ON - 2
AUTO SENSOR OFF - 3
These should be same as the defination in sensor file, do not change.
*/
void panel_set_hdr_flag(u32 setHdr)
{
	u32 bl_lvl_bak = g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_backlight_level;
	char cal_value = g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hdr_on;

	switch (setHdr) {
	case 0:
		cal_value &= 0xfe;/* clear bit(0) */
		break;
	case 1:
		cal_value |= 0x01;/* set bit(0) */
		break;
	case 2:
		cal_value |= 0x02;/* set bit(1) */
		break;
	case 3:
		cal_value &= 0xfd;/* clear bit(1) */
		break;
	default:
		pr_err("HDR invalid flag:  %d\n", setHdr);
	break;
	}

	if (!g_zte_ctrl_pdata->is_enabled) {
		pr_info("HDR flag:  panel is powered off, skip\n");
		return;
	}

	if (bl_lvl_bak == 0) {
		pr_info("HDR flag: lcd backlight is 0, skip\n");
		return;
	}

	if (g_oeld_bdev == NULL) {
		pr_info("HDR flag: g_oeld_bdev is NULL, skip\n");
		return;
	}

	if (g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hdr_on != cal_value) {
		g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hdr_on = cal_value;
		sprd_oled_set_brightness(g_oeld_bdev);
		pr_info("HDR flag:  new value: %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hdr_on);
	} else {
		pr_info("HDR flag is same as old: %d, do nothing\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hdr_on);
	}

}
static ssize_t zte_lcd_proc_hdr_write(struct file *file, const char __user *buffer,
			size_t count, loff_t *f_pos)
{
	char *tmp = kzalloc((count+1), GFP_KERNEL);
	u32 mode;

	if (!tmp)
		return -ENOMEM;

	if (copy_from_user(tmp, buffer, count)) {
		kfree(tmp);
		return -EFAULT;
	}

	mode = *tmp - '0';

	panel_set_hdr_flag(mode);

	kfree(tmp);
	return count;
}
static int zte_lcd_proc_hdr_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hdr_on);
	pr_info("HDR flag:  %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hdr_on);
	return 0;
}
static int zte_lcd_proc_hdr_open(struct inode *inode, struct file *file)
{
	return single_open(file, zte_lcd_proc_hdr_show, NULL);
}
static const struct file_operations zte_lcd_hdr_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= zte_lcd_proc_hdr_open,
	.read		= seq_read,
	.write		= zte_lcd_proc_hdr_write,
	.llseek		= seq_lseek,
	.release		= single_release,
};
static int zte_lcd_set_hdr_flag(struct mipi_dsi_device *dispc)
{
	proc_create("driver/lcd_hdr", 0664, NULL, &zte_lcd_hdr_proc_fops);

	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hdr_on = 0;
	pr_info("HDR flag:  = %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_hdr_on);

	return 0;
}
#endif

#ifdef CONFIG_ZTE_LCD_SEC_PANEL_CTRL
#define SEC_PANEL_OFF_CMDS_LEN	7
#define SEC_PANEL_ON_CMDS_LEN	11
int panel_sec_panel_set_state(u32 state)
{
	int i, err = 0;
	u8 sec_panel_off[SEC_PANEL_OFF_CMDS_LEN][2] = {
								{0xfe, 0x74},
								{0xbc, 0x0e},
								{0xc1, 0x0e},
								{0xc2, 0x0e},
								{0xc3, 0x0e},
								{0xc5, 0x0e},
								{0xfe, 0x00}
							};
	u8 sec_panel_on[SEC_PANEL_ON_CMDS_LEN][2] = {
								{0xfe, 0x74},
								{0x10, 0x05},
								{0x15, 0x05},
								{0x16, 0x01},
								{0x17, 0x01},
								{0xbc, 0x09},
								{0xc1, 0x09},
								{0xc2, 0x0d},
								{0xc3, 0x0d},
								{0xc5, 0x0d},
								{0xfe, 0x00}
							};

	struct mipi_dsi_device *dsi;

	dsi = g_dispc;

	if (!dsi) {
		pr_info("Sec panel: No device\n");
		return -ENOMEM;
	}

	if (state == 1) {
		for (i = 0; i < SEC_PANEL_ON_CMDS_LEN; i++) {
			err = mipi_dsi_dcs_write(dsi, sec_panel_on[i][0], &sec_panel_on[i][1],
				sizeof(sec_panel_on[i]) - 1);
			if (err < 0)
				goto error;
		}
		pr_info("Sec panel: Send sec panel on cmds\n");
	} else {
		for (i = 0; i < SEC_PANEL_OFF_CMDS_LEN; i++) {
			err = mipi_dsi_dcs_write(dsi, sec_panel_off[i][0], &sec_panel_off[i][1],
				sizeof(sec_panel_off[i]) - 1);
			if (err < 0)
				goto error;
		}
		pr_info("Sec panel: Send sec panel off cmds\n");
	}
	usleep_range(5000, 6000);

	return err;

error:
	pr_err("Sec panel: send cmds failed\n");
	return err;
}

static ssize_t zte_lcd_proc_sec_panel_write(struct file *file, const char __user *buffer,
			size_t count, loff_t *f_pos)
{
	char *tmp = kzalloc((count+1), GFP_KERNEL);
	u32 state;
	int ret = 0, retry_times = 3;

	if (!tmp)
		return -ENOMEM;

	if (copy_from_user(tmp, buffer, count)) {
		kfree(tmp);
		return -EFAULT;
	}

	pr_info("Sec panel: zte_lcd_proc_sec_panel_write\n");

	state = *tmp - '0';

	if (g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_sec_panel_state != state) {
		while (retry_times--) {
			ret = panel_sec_panel_set_state(state);
			if (ret == 0) {
				g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_sec_panel_state = state;
				pr_info("Sec panel: Set new state successful, new state = %d\n",
					g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_sec_panel_state);
				break;
			}
			pr_info("Sec panel: Set new state failed, state = %d, retry_times = %d\n",
					g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_sec_panel_state, retry_times);
		}

	} else {
		pr_info("Sec panel: New state is same as old ,do nothing! state = %d\n",
		g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_sec_panel_state);
	}
	kfree(tmp);
	return count;
}
static int zte_lcd_proc_sec_panel_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_sec_panel_state);
	pr_info("Sec panel: state = %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_sec_panel_state);
	return 0;
}
static int zte_lcd_proc_sec_panel_open(struct inode *inode, struct file *file)
{
	return single_open(file, zte_lcd_proc_sec_panel_show, NULL);
}

static const struct file_operations zte_lcd_sec_panel_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= zte_lcd_proc_sec_panel_open,
	.read		= seq_read,
	.write		= zte_lcd_proc_sec_panel_write,
	.llseek		= seq_lseek,
	.release		= single_release,
};

static int zte_lcd_sec_panel_ctrl(void)
{
	proc_create("driver/lcd_sec_panel", 0664, NULL, &zte_lcd_sec_panel_proc_fops);

	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_sec_panel_state = 1;
	pr_info("Sec panel: state = %d\n", g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_sec_panel_state);

	return 0;
}
#endif

static void zte_lcd_common_init(struct mipi_dsi_device *dispc, struct panel_info *panel_info)
{
	int ret = 0;

	g_zte_ctrl_pdata->zte_lcd_ctrl = kzalloc(sizeof(struct zte_lcd_ctrl_data), GFP_KERNEL);
	if (!g_zte_ctrl_pdata->zte_lcd_ctrl) {
		ZTE_LCD_ERROR("no mem to save zte_lcd_ctrl_data info: kzalloc fail\n");
		return;
	}

	ZTE_LCD_INFO("%s:alloc zte_lcd_ctrl_data success!\n", __func__);

	/*create /sys/lcd_sys/ path to add other lcd ctrl point*/
	g_zte_ctrl_pdata->zte_lcd_ctrl->kobj = kobject_create_and_add("lcd_sys", NULL);
	if (!g_zte_ctrl_pdata->zte_lcd_ctrl->kobj) {
		ZTE_LCD_ERROR("%s:create lcd_sys error!\n", __func__);
	} else {
		ret = sysfs_create_group(g_zte_ctrl_pdata->zte_lcd_ctrl->kobj, &zte_lcd_attrs_group);
		if (ret)
			ZTE_LCD_ERROR("sysfs group creation failed, rc=%d\n", ret);
	}

	

	g_zte_ctrl_pdata->zte_lcd_ctrl->lcd_powerdown_for_shutdown = false;

}
void zte_lcd_common_func(struct mipi_dsi_device *dispc, struct sprd_panel *panel)
{
	if (g_zte_ctrl_pdata) {
		return;
	}

	g_zte_ctrl_pdata = panel;

	zte_lcd_common_init(dispc, &panel->info);
	zte_lcd_panel_parse_dt(dispc, panel->info.of_node);

	zte_lcd_proc_info_display(&panel->info);
#ifdef CONFIG_ZTE_LCD_HBM_CTRL
	zte_lcd_hbm_ctrl(dispc);
	zte_lcd_set_hdr_flag(dispc);
	zte_lcd_set_aod_brightness(dispc);
#ifdef CONFIG_ZTE_LCD_E1_PANEL
	zte_lcd_set_elvdd_node(dispc);
#endif
	g_dispc = dispc;
#endif

#ifdef CONFIG_ZTE_LCD_REG_DEBUG
	zte_lcd_reg_debug_func();
#endif
#ifdef CONFIG_ZTE_LCD_BACKLIGHT_LEVEL_CURVE
	g_zte_ctrl_pdata->zte_lcd_ctrl->zte_convert_brightness = zte_convert_backlevel_function;
#endif
#ifdef CONFIG_ZTE_LCD_SEC_PANEL_CTRL
	zte_lcd_sec_panel_ctrl();
#endif
#ifdef CONFIG_ZTE_LCD_CABC3_EXTREME_POWER_SAVE
	mutex_init(&g_zte_ctrl_pdata->zte_lcd_ctrl->panel_sys_lock);
	zte_create_cabc_sys(g_zte_ctrl_pdata);
	g_zte_ctrl_pdata->zte_lcd_ctrl->zte_set_cabc_mode = zte_set_panel_cabc;
#endif

}
