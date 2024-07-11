
#include <asm/uaccess.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif

/*#define STK_QUALCOMM*/
#define STK_SPREADTRUM

#define STK_HEADER_VERSION "0.0.4"

/*****************************************************************************
 * Global variable
 *****************************************************************************/
#define STK_INTERRUPT_MODE
 /*#define STK_POLLING_MODE*/
/*#define STK_SENSORS_DEV*/ /*if can't find <sensors.h> or not android system, don't use this define.*/
#define STK_FIX_CADC

/*#define STK_TEMP_CALI_START*/ /*before cali, want to get cali raw*/
/*#define STK_TEMP_CALI_DONE */ /*after cali, want to write cali recipe*/
#define STK_COEF_T_SIGN -1 /*coef sign(1/-1)*/
#define STK_COEF_T 0x35    /*calculate temp and raw coef, is 8bit data*/

#define DEFAULT_SENSE_GAIN 1
#define DEFAULT_CFB 3

#define STK_SAR_THD_0 0x00000032
#define STK_SAR_THD_1 0x00000032
#define STK_SAR_THD_2 0x00000032
#define STK_SAR_THD_3 0x00000032

#ifdef STK_QUALCOMM
#ifdef STK_SENSORS_DEV
#include <linux/sensors.h>
#endif /* STK_SENSORS_DEV*/
#undef STK_SPREADTRUM
#elif defined STK_MTK
#undef STK_INTERRUPT_MODE
/*#undef STK_POLLING_MODE*/
#elif defined STK_SPREADTRUM
#include <linux/limits.h>
#include <linux/version.h>
/*
#undef STK_INTERRUPT_MODE
#define STK_POLLING_MODE
*/
#elif defined STK_ALLWINNER
#undef STK_INTERRUPT_MODE
#define STK_POLLING_MODE
#endif /* STK_QUALCOMM, STK_MTK, STK_SPREADTRUM, or STK_ALLWINNER */

/* STK5XXX_REG_CHIPID */
#define STK5013_ID 0x2164

#define STK5XXX_NAME    "sar_sensor"
#define STK_POLLING_TIME 100000 /*us*/
#define STKINITERR    -1

typedef struct stk5xxx_register_table {
	u16 address;
	u32 value;
} stk5xxx_register_table;

typedef enum {
	STK5XXX_PRX_FAR_AWAY,
	STK5XXX_PRX_NEAR_BY,
	STK5XXX_PRX_NEAR_BY_UNKNOWN
} stk5xxx_prx_nearby_type;

/*****************************************************************************
 * stk5xxx register, start
 *****************************************************************************/

#define STK_ADDR__CHIP_INDEX 0x0038
#define STK_CHIP_INDEX_CHIP_ID__MASK 0xFFFF0000
#define STK_CHIP_INDEX_CHIP_ID__SHIFT 16
#define STK_CHIP_INDEX_VER__MASK 0x000000FF

#define STK_ADDR__IRQ_SOURCE 0x0000
#define STK_IRQ_SOURCE__CONVDONE_IRQ__MASK 0x10
#define STK_IRQ_SOURCE__PROG0_IRQ__MASK    0x80
#define STK_IRQ_SOURCE__PROG1_IRQ__MASK    0x40
#define STK_IRQ_SOURCE__PROG2_IRQ__MASK    0x20

#define STK_ADDR__STATE1_REG 0x0170
#define STK_STATE1__FAIL_STATE__MASK  0x00000001
#define STK_STATE1__PHRST_STATE__MASK 0x00003F00
#define STK_STATE1__SAT_STATE__MASK   0x003F0000
#define STK_STATE1__CONV_STATE__MASK  0x01000000

#define STK_ADDR__STATE2_REG 0x0174
#define STK_ADDR__STATE3_REG 0x0178
#define STK_STATE3__CUSTOM_A__MASK 0x100
#define STK_STATE3__CUSTOM_B__MASK 0x200

#define STK_ADDR__TRIGGER_REG 0x0118
#define STK_TRIGGER_REG_PHEN__SHIFT 0
#define STK_TRIGGER_REG_PHEN__MASK 0xFF
#define STK_TRIGGER_REG_PHEN__DISABLE_ALL 0x00000000
#define STK_TRIGGER_REG_PHEN__ENABLE_PH0123 0xF

#define STK_TRIGGER_REG_COMPEN__SHIFT 8
#define STK_TRIGGER_REG_COMPEN__MASK 0xFF
#define STK_TRIGGER_REG_COMPEN__DISABLE_ALL 0x00000000
#define STK_TRIGGER_REG_COMPEN__ENABLE_PH0123 0xF
#define STK_TRIGGER_REG__INIT_ALL 0x00003F03

#define STK_ADDR__TRIGGER_CMD_REG 0x0114
#define STK_TRIGGER_CMD_REG__SHIFT 0
#define STK_TRIGGER_CMD_REG__MASK 0xF
#define STK_TRIGGER_CMD_REG__EXIT_PAUSE_MODE 0x0000000C
#define STK_TRIGGER_CMD_REG__ENTER_PAUSE_MODE 0x0000000D
#define STK_TRIGGER_CMD_REG__PHEN_ENABLE 0x0000000F
#define STK_TRIGGER_CMD_REG__INIT_ALL 0xFFFFFFFF

#define STK_ADDR__SOFT_RESET_REG 0x002C
#define STK_SOFT_RESET_REG__SOFT_RESET_CMD 0x000000A5

#define STK_ADDR__TRIM_LOCK_REG 0x0008
#define STK_TRIM_UNLOCK_REG__VALUE 0x000000A5
#define STK_TRIM_LOCK_REG__VALUE 0x0000005A

#define STK_ADDR__TRIM_REG 0x0024
#define STK_TRIM_REG__VALUE 0x1B008073

#define STK_ADDR__DISABLE_TRIM_RX_WEIGHT_REG 0x0080
#define STK_DISABLE_TRIM_RX_WEIGHT_REG__VALUE 0x0

#define STK_ADDR__ADC_DEGLITCH 0x0090
#define STK_ADC_DEGLITCH__VALUE 0x00222222

#define STK_ADDR__MOVEMENT_0 0x0094
#define STK_MOVEMENT_0__VALUE 0x005A0000

#define STK_ADDR__MOVEMENT_2 0x009c
#define STK_MOVEMENT_2__VALUE 0x04040404

#define STK_ADDR__MOVEMENT_3 0x00A0
#define STK_MOVEMENT_3__VALUE 0x00000404

#define STK_ADDR__ADAPITVE_BASELINE_0 0x00B0
#define STK_ADAPITVE_BASELINE_0__VALUE 0x00000000

#define STK_ADDR__ADAPITVE_BASELINE_1 0x00B4
#define STK_ADAPITVE_BASELINE_1__VALUE 0x40404040

#define STK_ADDR__ADAPITVE_BASELINE_2 0x00B8
#define STK_ADAPITVE_BASELINE_2__VALUE 0x00014040

#define STK_ADDR__CLK_SPREAD_CTRL 0x0110
#define STK_CLK_SPREAD_CTRL__VALUE 0x00000022

#define STK_ADDR__SCAN_FACTOR 0x011C
#define STK_SCAN_FACTOR__VALUE 0x00022000

#define STK_ADDR__DOZE_MODE 0x0120
#define STK_DOZE_MODE__SHIFT 0
#define STK_DOZE_MODE__MASK 0xFFFFFFFF
#define STK_DOZE_MODE__TURN_OFF 0x00000030
#define STK_DOZE_MODE__TURN_ON 0x00000001

#define STK_ADDR__SCAN_PERIOD 0x0124
#define STK_SCAN_PERIOD__VALUE 0x000007D0

#define STK_ADDR__AFE_CTRL_PH0_REG 0x0128
#define STK_AFE_CTRL_PH0_REG__VALUE 0x00200410

#define STK_ADDR__AFE_CTRL_PH00_REG 0x012C
#define STK_AFE_CTRL_PH00_REG__VALUE 0x00000070

#define STK_ADDR__AFE_CTRL_PH1_REG 0x0130
#define STK_AFE_CTRL_PH1_REG__VALUE 0x00200610

#define STK_ADDR__AFE_CTRL_PH11_REG 0x0134
#define STK_AFE_CTRL_PH11_REG__VALUE 0x00000070

#define STK_ADDR__AFE_CTRL_PH2_REG 0x0138
#define STK_AFE_CTRL_PH2_REG__VALUE 0x00200402

#define STK_ADDR__AFE_CTRL_PH22_REG 0x013C
#define STK_AFE_CTRL_PH22_REG__VALUE 0x00001070

#define STK_ADDR__AFE_CTRL_PH3_REG 0x0140
#define STK_AFE_CTRL_PH3_REG__VALUE 0x00200402

#define STK_ADDR__AFE_CTRL_PH33_REG 0x0144
#define STK_AFE_CTRL_PH33_REG__VALUE 0x00001070

#define STK_ADDR__AFE_CTRL_PH4_REG 0x0148
#define STK_AFE_CTRL_PH4_REG__VALUE 0x00200610 /*0x00200402*/

#define STK_ADDR__AFE_CTRL_PH44_REG 0x014C
#define STK_AFE_CTRL_PH44_REG__VALUE 0x00000070

#define STK_ADDR__ANA_CTRL0_REG 0x019C
#define STK_ANA_CTRL0_REG__VALUE 0x02311213 /*0x02511213*/

#define STK_ADDR__ANA_CTRL1_REG 0x01A0
#define STK_ANA_CTRL1_REG__VALUE 0x01218011 /*0x01208031*/

#define STK_ADDR__ANA_CTRL2_REG 0x01A4
#define STK_ANA_CTRL2_REG__VALUE 0x00000124

#define STK_ADDR__RX_TIMING0_REG 0x01A8
#define STK_RX_TIMING0_REG__VALUE 0x20C60C66

#define STK_ADDR__RX_TIMING1_REG 0x01AC
#define STK_RX_TIMING1_REG__VALUE 0x000001FF

#define STK_ADDR__RX_TIMING2_REG 0x01B0
#define STK_RX_TIMING2_REG__VALUE 0x12000000

#define STK_ADDR__RX_TIMING3_REG 0x01B4
#define STK_RX_TIMING3_REG__VALUE 0x00001101 /*0x00000112*/

#define STK_ADDR__RX_TIMING4_REG 0x01B8
#define STK_RX_TIMING4_REG__VALUE 0x00001136 /*0x00001116*/

#define STK_ADDR__TCON0_REG 0x01C4
#define STK_TCON0_REG__VALUE 0x00053010

#define STK_ADDR__TCON1_REG 0x01C8
#define STK_TCON1_REG__VALUE 0x00000B00


#define STK_ADDR__C_UNIT_CTRL_REG 0x01D0
#define STK_C_UNIT_CTRL_REG__VALUE 0x00000001

#define STK_ADDR__STATE_DET4_PH0_REG 0x0318
#define STK_STATE_DET4_PH0_REG__VALUE 0x2AF80020

#define STK_ADDR__CADC_REG 0x0330
#define STK_CADC_REG__FIX_VALUE__MASK 0x00003FF0
#define STK_CADC_REG__FIX_EN__MASK 0x00000001
#define STK_CADC_REG__VALUE 0x000004E1

#define STK_ADDR__INHOUSE_CMD0_REG 0x0600
#define STK_INHOUSE_CMD0_REG__VALUE 0x0000000A

#define STK_ADDR__INHOUSE_CMD1_REG 0x0604
#define STK_INHOUSE_CMD1_REG__VALUE 0x00000000

#define STK_ADDR__INHOUSE_CMD2_REG 0x0608
#define STK_INHOUSE_CMD2_REG__VALUE 0x00000001

#define STK_ADDR__RXIO0_MUX_REG 0x0058
#define STK_RXIO0_MUX_REG__VALUE 0x00777777 /*0x00777757*/
#define STK_RXIO0_MUX_REG_AFE_PH0__SHIFT 0
#define STK_RXIO0_MUX_REG_AFE_PH0__MASK 0x3
#define STK_RXIO0_MUX_REG_AFE_PH1__SHIFT 4
#define STK_RXIO0_MUX_REG_AFE_PH1__MASK 0x7
#define STK_RXIO0_MUX_REG_AFE_PH2__SHIFT 8
#define STK_RXIO0_MUX_REG_AFE_PH2__MASK 0x7
#define STK_RXIO0_MUX_REG_AFE_PH3__SHIFT 12
#define STK_RXIO0_MUX_REG_AFE_PH3__MASK 0x7
#define STK_RXIO0_MUX_REG_AFE_PH4__SHIFT 16
#define STK_RXIO0_MUX_REG_AFE_PH4__MASK 0x7
#define STK_RXIO0_MUX_REG_AFE_PH5__SHIFT 20
#define STK_RXIO0_MUX_REG_AFE_PH5__MASK 0x7

#define STK_ADDR__RXIO1_MUX_REG 0x005C
#define STK_RXIO1_MUX_REG__VALUE 0x00777755
#define STK_RXIO1_MUX_REG_AFE_PH0__SHIFT 0
#define STK_RXIO1_MUX_REG_AFE_PH0__MASK 0x3
#define STK_RXIO1_MUX_REG_AFE_PH1__SHIFT 4
#define STK_RXIO1_MUX_REG_AFE_PH1__MASK 0x7
#define STK_RXIO1_MUX_REG_AFE_PH2__SHIFT 8
#define STK_RXIO1_MUX_REG_AFE_PH2__MASK 0x7
#define STK_RXIO1_MUX_REG_AFE_PH3__SHIFT 12
#define STK_RXIO1_MUX_REG_AFE_PH3__MASK 0x7
#define STK_RXIO1_MUX_REG_AFE_PH4__SHIFT 16
#define STK_RXIO1_MUX_REG_AFE_PH4__MASK 0x7
#define STK_RXIO1_MUX_REG_AFE_PH5__SHIFT 20
#define STK_RXIO1_MUX_REG_AFE_PH5__MASK 0x7

#define STK_ADDR__RXIO2_MUX_REG 0x0060
#define STK_RXIO2_MUX_REG__VALUE 0x00777777
#define STK_RXIO2_MUX_REG_AFE_PH0__SHIFT 0
#define STK_RXIO2_MUX_REG_AFE_PH0__MASK 0x3
#define STK_RXIO2_MUX_REG_AFE_PH1__SHIFT 4
#define STK_RXIO2_MUX_REG_AFE_PH1__MASK 0x7
#define STK_RXIO2_MUX_REG_AFE_PH2__SHIFT 8
#define STK_RXIO2_MUX_REG_AFE_PH2__MASK 0x7
#define STK_RXIO2_MUX_REG_AFE_PH3__SHIFT 12
#define STK_RXIO2_MUX_REG_AFE_PH3__MASK 0x7
#define STK_RXIO2_MUX_REG_AFE_PH4__SHIFT 16
#define STK_RXIO2_MUX_REG_AFE_PH4__MASK 0x7
#define STK_RXIO2_MUX_REG_AFE_PH5__SHIFT 20
#define STK_RXIO2_MUX_REG_AFE_PH5__MASK 0x7

#define STK_ADDR__RXIO3_MUX_REG 0x0064
#define STK_RXIO3_MUX_REG__VALUE 0x00777777
#define STK_RXIO3_MUX_REG_AFE_PH0__SHIFT 0
#define STK_RXIO3_MUX_REG_AFE_PH0__MASK 0x3
#define STK_RXIO3_MUX_REG_AFE_PH1__SHIFT 4
#define STK_RXIO3_MUX_REG_AFE_PH1__MASK 0x7
#define STK_RXIO3_MUX_REG_AFE_PH2__SHIFT 8
#define STK_RXIO3_MUX_REG_AFE_PH2__MASK 0x7
#define STK_RXIO3_MUX_REG_AFE_PH3__SHIFT 12
#define STK_RXIO3_MUX_REG_AFE_PH3__MASK 0x7
#define STK_RXIO3_MUX_REG_AFE_PH4__SHIFT 16
#define STK_RXIO3_MUX_REG_AFE_PH4__MASK 0x7
#define STK_RXIO3_MUX_REG_AFE_PH5__SHIFT 20
#define STK_RXIO3_MUX_REG_AFE_PH5__MASK 0x7

#define STK_ADDR__RXIO4_MUX_REG 0x0068
#define STK_RXIO4_MUX_REG__VALUE 0x00777777
#define STK_RXIO4_MUX_REG_AFE_PH0__SHIFT 0
#define STK_RXIO4_MUX_REG_AFE_PH0__MASK 0x3
#define STK_RXIO4_MUX_REG_AFE_PH1__SHIFT 4
#define STK_RXIO4_MUX_REG_AFE_PH1__MASK 0x7
#define STK_RXIO4_MUX_REG_AFE_PH2__SHIFT 8
#define STK_RXIO4_MUX_REG_AFE_PH2__MASK 0x7
#define STK_RXIO4_MUX_REG_AFE_PH3__SHIFT 12
#define STK_RXIO4_MUX_REG_AFE_PH3__MASK 0x7
#define STK_RXIO4_MUX_REG_AFE_PH4__SHIFT 16
#define STK_RXIO4_MUX_REG_AFE_PH4__MASK 0x7
#define STK_RXIO4_MUX_REG_AFE_PH5__SHIFT 20
#define STK_RXIO4_MUX_REG_AFE_PH5__MASK 0x7

#define STK_ADDR__RXIO5_MUX_REG 0x006C
#define STK_RXIO5_MUX_REG__VALUE 0x00777777
#define STK_RXIO5_MUX_REG_AFE_PH0__SHIFT 0
#define STK_RXIO5_MUX_REG_AFE_PH0__MASK 0x3
#define STK_RXIO5_MUX_REG_AFE_PH1__SHIFT 4
#define STK_RXIO5_MUX_REG_AFE_PH1__MASK 0x7
#define STK_RXIO5_MUX_REG_AFE_PH2__SHIFT 8
#define STK_RXIO5_MUX_REG_AFE_PH2__MASK 0x7
#define STK_RXIO5_MUX_REG_AFE_PH3__SHIFT 12
#define STK_RXIO5_MUX_REG_AFE_PH3__MASK 0x7
#define STK_RXIO5_MUX_REG_AFE_PH4__SHIFT 16
#define STK_RXIO5_MUX_REG_AFE_PH4__MASK 0x7
#define STK_RXIO5_MUX_REG_AFE_PH5__SHIFT 20
#define STK_RXIO5_MUX_REG_AFE_PH5__MASK 0x7

/*FLIT_PH0*/
#define STK_ADDR__REGFILT0_PH0_REG 0x0300
#define STK_REGFILT0_PH0_REG__VALUE 0x00011340
#define STK_ADDR__REGSTATEDET0_PH0_REG 0x0308
#define STK_REGSTATEDET0_PH0_REG__VALUE 0x0000201C
#define STK_ADDR__REGSTATEDET2_PH0_REG 0x0310
#define STK_REGSTATEDET2_PH0_REG__VALUE 0x00000030
#define STK_ADDR__REGSTATEDET3_PH0_REG 0x0314
#define STK_REGSTATEDET3_PH0_REG__VALUE 0x00000000
#define STK_ADDR__REGADVDIG0_PH0 0x0320
#define STK_REGADVDIG0_PH0__VALUE 0x00000000
#define STK_ADDR__REGADVDIG1_PH0 0x0324
#define STK_REGADVDIG1_PH0__VALUE 0x00000000
#define STK_ADDR__REGADVDIG2_PH0 0x0328
#define STK_REGADVDIG2_PH0__VALUE 0x00000000
#define STK_ADDR__REGADVDIG3_PH0 0x032C
#define STK_REGADVDIG3_PH0__VALUE 0x00000000

/*FLIT_PH1*/
#define STK_ADDR__REGFILT0_PH1_REG 0x0340
#define STK_REGFILT0_PH1_REG__VALUE 0x00011340
#define STK_ADDR__REGSTATEDET0_PH1_REG 0x0348
#define STK_REGSTATEDET0_PH1_REG__VALUE 0x0000201C
#define STK_ADDR__REGSTATEDET2_PH1_REG 0x0350
#define STK_REGSTATEDET2_PH1_REG__VALUE 0x00000030
#define STK_ADDR__REGSTATEDET3_PH1_REG 0x0354
#define STK_REGSTATEDET3_PH1_REG__VALUE 0x00000000
#define STK_ADDR__REGADVDIG0_PH1 0x0360
#define STK_REGADVDIG0_PH1__VALUE 0x00000000
#define STK_ADDR__REGADVDIG1_PH1 0x0364
#define STK_REGADVDIG1_PH1__VALUE 0x00000000
#define STK_ADDR__REGADVDIG2_PH1 0x0368
#define STK_REGADVDIG2_PH1__VALUE 0x00000000
#define STK_ADDR__REGADVDIG3_PH1 0x036C
#define STK_REGADVDIG3_PH1__VALUE 0x00000000

/*FLIT_PH2*/
#define STK_ADDR__REGFILT0_PH2_REG 0x0380
#define STK_REGFILT0_PH2_REG__VALUE 0x00011100
#define STK_ADDR__REGSTATEDET0_PH2_REG 0x0388
#define STK_REGSTATEDET0_PH2_REG__VALUE 0x00000000
#define STK_ADDR__REGSTATEDET2_PH2_REG 0x0390
#define STK_REGSTATEDET2_PH2_REG__VALUE 0x00000000
#define STK_ADDR__REGSTATEDET3_PH2_REG 0x0394
#define STK_REGSTATEDET3_PH2_REG__VALUE 0x00000000
#define STK_ADDR__REGADVDIG0_PH2 0x03A0
#define STK_REGADVDIG0_PH2__VALUE 0x00000000
#define STK_ADDR__REGADVDIG1_PH2 0x03A4
#define STK_REGADVDIG1_PH2__VALUE 0x00000000
#define STK_ADDR__REGADVDIG2_PH2 0x03A8
#define STK_REGADVDIG2_PH2__VALUE 0x00000000
#define STK_ADDR__REGADVDIG3_PH2 0x03AC
#define STK_REGADVDIG3_PH2__VALUE 0x00000000

/*FLIT_PH3*/
#define STK_ADDR__REGFILT0_PH3_REG 0x03C0
#define STK_REGFILT0_PH3_REG__VALUE 0x00011300
#define STK_ADDR__REGSTATEDET0_PH3_REG 0x03C8
#define STK_REGSTATEDET0_PH3_REG__VALUE 0x00000000
#define STK_ADDR__REGSTATEDET2_PH3_REG 0x03D0
#define STK_REGSTATEDET2_PH3_REG__VALUE 0x00000000
#define STK_ADDR__REGSTATEDET3_PH3_REG 0x03D4
#define STK_REGSTATEDET3_PH3_REG__VALUE 0x00000000
#define STK_ADDR__REGADVDIG0_PH3 0x03E0
#define STK_REGADVDIG0_PH3__VALUE 0x00000000
#define STK_ADDR__REGADVDIG1_PH3 0x03E4
#define STK_REGADVDIG1_PH3__VALUE 0x00000000
#define STK_ADDR__REGADVDIG2_PH3 0x03E8
#define STK_REGADVDIG2_PH3__VALUE 0x00000000
#define STK_ADDR__REGADVDIG3_PH3 0x03EC
#define STK_REGADVDIG3_PH3__VALUE 0x00000000

/*FLIT_PH4*/
#define STK_ADDR__REGFILT0_PH4_REG 0x0400
#define STK_REGFILT0_PH4_REG__VALUE 0x00011100
#define STK_ADDR__REGSTATEDET0_PH4_REG 0x0408
#define STK_REGSTATEDET0_PH4_REG__VALUE 0x00000000
#define STK_ADDR__REGSTATEDET2_PH4_REG 0x0410
#define STK_REGSTATEDET2_PH4_REG__VALUE 0x00000000
#define STK_ADDR__REGSTATEDET3_PH4_REG 0x0414
#define STK_REGSTATEDET3_PH4_REG__VALUE 0x00000000
#define STK_ADDR__REGADVDIG0_PH4 0x0420
#define STK_REGADVDIG0_PH4__VALUE 0x00000000
#define STK_ADDR__REGADVDIG1_PH4 0x0424
#define STK_REGADVDIG1_PH4__VALUE 0x00000000
#define STK_ADDR__REGADVDIG2_PH4 0x0428
#define STK_REGADVDIG2_PH4__VALUE 0x00000000
#define STK_ADDR__REGADVDIG3_PH4 0x042C
#define STK_REGADVDIG3_PH4__VALUE 0x00000000

/*TEMP_VAR*/
#define STK_ADDR__REG_TEMP_VAR0 0x04AC
#define STK_REG_TEMP_VAR0__VALUE 0x00000000
#define STK_ADDR__REG_TEMP_VAR1 0x04B0
#define STK_REG_TEMP_VAR1__VALUE 0x00000000
#define STK_ADDR__REG_TEMP_VAR2 0x04B4
#define STK_REG_TEMP_VAR2__VALUE 0x00000000
#define STK_ADDR__REG_TEMP_VAR3 0x04B8
#define STK_REG_TEMP_VAR3__VALUE 0x00000000

/*RAW_DES*/
#define STK_ADDR__REG_RAW_DESCEND0 0x04BC
#define STK_REG_RAW_DESCEND0__VALUE 0x00000000
#define STK_ADDR__REG_RAW_DESCEND1 0x04C0
#define STK_REG_RAW_DESCEND1__VALUE 0x00000000
#define STK_ADDR__REG_RAW_DESCEND2 0x04C4
#define STK_REG_RAW_DESCEND2__VALUE 0x00000000

/*BOUNDARY*/
#define STK_ADDR__REG_BOUNDARY0 0x04C8
#define STK_REG_BOUNDARY0__VALUE 0x00000000
#define STK_ADDR__REG_BOUNDARY1 0x04CC
#define STK_REG_BOUNDARY1__VALUE 0x00000000
#define STK_ADDR__REG_BOUNDARY2 0x04D0
#define STK_REG_BOUNDARY2__VALUE 0x00000000

#define STK_ADDR__IRQ_SOURCE_ENABLE_REG 0x0004
#define STK_IRQ_SOURCE_ENABLE_REG_CLOSE_ANY_IRQ_EN__SHIFT 0
#define STK_IRQ_SOURCE_ENABLE_REG_CLOSE_ANY_IRQ_EN__MASK 0x00000001

#define STK_IRQ_SOURCE_ENABLE_REG_FAR_ANY_IRQ_EN__SHIFT 1
#define STK_IRQ_SOURCE_ENABLE_REG_FAR_ANY_IRQ_EN__MASK 0x00000002

#define STK_IRQ_SOURCE_ENABLE_REG_CONVDONE_IRQ_EN__SHIFT 3
#define STK_IRQ_SOURCE_ENABLE_REG_CONVDONE_IRQ_EN__MASK 0x00000008

#define STK_IRQ_SOURCE_ENABLE_REG_PROG2_IRQ_EN__SHIFT 4
#define STK_IRQ_SOURCE_ENABLE_REG_PROG2_IRQ_EN__MASK 0x00000010

#define STK_IRQ_SOURCE_ENABLE_REG_PROG1_IRQ_EN__SHIFT 5
#define STK_IRQ_SOURCE_ENABLE_REG_PROG1_IRQ_EN__MASK 0x00000020

#define STK_IRQ_SOURCE_ENABLE_REG_PROG0_IRQ_EN__SHIFT 6
#define STK_IRQ_SOURCE_ENABLE_REG_PROG0_IRQ_EN__MASK 0x00000040

#define STK_IRQ_SOURCE_ENABLE_REG_PAUSE_IRQ_EN__SHIFT 7
#define STK_IRQ_SOURCE_ENABLE_REG_PAUSE_IRQ_EN__MASK 0x00000080

#define STK_ADDR__REG_IRQ_STATE0_REG 0x016C
#define STK_REG_IRQ_STATE0_REG_PROX_STATE__MASK 0x00000001
#define STK_REG_IRQ_STATE0_REG_TABLE_STATE__MASK 0x0000FF00
#define STK_REG_IRQ_STATE0_REG_BODY_STATE__MASK 0x00FF0000
#define STK_REG_IRQ_STATE0_REG_STEADY_STATE__MASK 0xFF000000


#define STK_ADDR__REG_RAW_PH0_REG 0x0500
#define STK_ADDR__REG_RAW_PH1_REG 0x0504
#define STK_ADDR__REG_RAW_PH2_REG 0x0508
#define STK_ADDR__REG_RAW_PH3_REG 0x050C
#define STK_ADDR__REG_RAW_PH4_REG 0x0510
#define STK_ADDR__REG_RAW_PH5_REG 0x0514

#define STK_REG_BASE_PH0_REG_ADDR   0x0518
#define STK_REG_BASE_PH1_REG_ADDR   0x051C
#define STK_REG_BASE_PH2_REG_ADDR   0x0520
#define STK_REG_BASE_PH3_REG_ADDR   0x0524
#define STK_REG_BASE_PH4_REG_ADDR   0x0528
#define STK_REG_BASE_PH5_REG_ADDR   0x052C


#define STK_ADDR__REG_DELTA_PH0_REG 0x0530
#define STK_ADDR__REG_DELTA_PH1_REG 0x0534
#define STK_ADDR__REG_DELTA_PH2_REG 0x0538
#define STK_ADDR__REG_DELTA_PH3_REG 0x053C
#define STK_ADDR__REG_DELTA_PH4_REG 0x0540
#define STK_ADDR__REG_DELTA_PH5_REG 0x0544


#define STK_ADDR__REG_CADC_PH0_REG 0x0548
#define STK_ADDR__REG_CADC_PH1_REG 0x054C
#define STK_ADDR__REG_CADC_PH2_REG 0x0550
#define STK_ADDR__REG_CADC_PH3_REG 0x0554
#define STK_ADDR__REG_CADC_PH4_REG 0x0558
#define STK_ADDR__REG_CADC_PH5_REG 0x055C
/*****************************************************************************
 * stk5xxx register, end
 *****************************************************************************/

static const u16 STK_ID[1] = { STK5013_ID };

struct stk_data {

	struct spi_device *spi;
	struct i2c_client *client;
	const struct stk_bus_ops *bops;
	struct mutex i2c_lock; /* mutex lock for register R/W */
	u8 *spi_buffer;        /* SPI buffer, used for SPI transfer. */
	atomic_t enabled;      /* chip is enabled or not */
	bool last_enable;      /* record current power status. For Suspend/Resume used. */
	u8 power_mode;
	u16 pid;
	u8 recv;
	stk5xxx_prx_nearby_type last_nearby;
	int last_data[6];
	int temperature;
#ifdef STK_INTERRUPT_MODE
	struct work_struct stk_work;
	int int_pin;
	int irq;
#elif defined STK_POLLING_MODE
	struct work_struct stk_work;
	struct hrtimer stk_timer;
	ktime_t poll_delay;
#endif                           /* STK_INTERRUPT_MODE, STK_POLLING_MODE */
	struct input_dev *input_dev; /* data */
#if defined STK_QUALCOMM || defined STK_SPREADTRUM
#ifdef STK_QUALCOMM
#ifdef STK_SENSORS_DEV
	struct sensors_classdev sar_cdev;
#endif /* STK_SENSORS_DEV*/
	u64 fifo_start_ns;
#endif /* STK_QUALCOMM */
	ktime_t timestamp;
#elif defined STK_MTK
	/*(struct sar_hw               hw;*/
	/*struct hwmsen_convert       cvt;*/
#endif /* STK_QUALCOMM, STK_SPREADTRUM, STK_MTK */
#ifdef STK_FIX_CADC
	struct work_struct stk_reset_work;
	struct hrtimer stk_reset_timer;
	bool is_first_boot;
	bool is_sat_state;
	bool wait_reset;
	u8 frame_count;
	u32 sat_data;
	u32 fix_cadc_value;
#endif /* STK_FIX_CADC*/
};
struct stk_data *stk_sar_ptr;
static char chip_info[20];

/*
#define STK_SAR_TAG                 "[stkSAR]"
#define STK_SAR_FUN(f)              printk(KERN_INFO STK_SAR_TAG" %s\n", __FUNCTION__)
#define dev_info(fmt, args...)   printk(KERN_INFO STK_SAR_TAG" %s/%d: "fmt"\n", __FUNCTION__, __LINE__, ##args)
#define dev_err(fmt, args...)   printk(KERN_ERR STK_SAR_TAG" %s/%d: "fmt"\n", __FUNCTION__, __LINE__, ##args)
*/
struct stk_bus_ops {
	int (*read)(struct stk_data*, unsigned short, unsigned char*);
	int (*read_block)(struct stk_data*, unsigned short, int, unsigned char*);
	int (*write)(struct stk_data*, unsigned short, unsigned char*);
};

#define STK_REG_READ(stk, addr, val) (stk->bops->read(stk, addr, val))
#define STK_REG_READ_BLOCK(stk, addr, len, val) (stk->bops->read_block(stk, addr, len, val))
#define STK_REG_WRITE(stk, addr, val) (stk->bops->write(stk, addr, val))


stk5xxx_register_table stk5xxx_default_register_table[] = {
	/*Trigger_CMD*/
	{STK_ADDR__TRIGGER_REG,          STK_TRIGGER_REG_PHEN__DISABLE_ALL},
	{STK_ADDR__TRIGGER_CMD_REG,      STK_TRIGGER_CMD_REG__INIT_ALL    },

	/*IRQ*/
	{STK_ADDR__IRQ_SOURCE_ENABLE_REG, (1 << STK_IRQ_SOURCE_ENABLE_REG_CLOSE_ANY_IRQ_EN__SHIFT) |
		(1 << STK_IRQ_SOURCE_ENABLE_REG_FAR_ANY_IRQ_EN__SHIFT)},
	/*|(1 << STK_IRQ_SOURCE_ENABLE_REG_CONVDONE_IRQ_EN__SHIFT)},
	don't init conv_done here, if too fast, it will lock irq*/

	/*RXIO 0~5*/
	{STK_ADDR__RXIO0_MUX_REG,        STK_RXIO0_MUX_REG__VALUE            },
	{STK_ADDR__RXIO1_MUX_REG,        STK_RXIO1_MUX_REG__VALUE            }, /*mapping ph0*/
	{STK_ADDR__RXIO2_MUX_REG,        STK_RXIO2_MUX_REG__VALUE            },
	{STK_ADDR__RXIO3_MUX_REG,        STK_RXIO3_MUX_REG__VALUE            }, /*mapping ph2*/
	{STK_ADDR__RXIO4_MUX_REG,        STK_RXIO4_MUX_REG__VALUE            },
	{STK_ADDR__RXIO5_MUX_REG,        STK_RXIO5_MUX_REG__VALUE            },

	/*DEGLITCH*/
	{STK_ADDR__ADC_DEGLITCH,         STK_ADC_DEGLITCH__VALUE             },

	/*MOVEMENT*/
	{STK_ADDR__MOVEMENT_0,         STK_MOVEMENT_0__VALUE                 },
	{STK_ADDR__MOVEMENT_2,         STK_MOVEMENT_2__VALUE                 },
	{STK_ADDR__MOVEMENT_3,         STK_MOVEMENT_3__VALUE                 },

	/*ADAPITVE_BASELINE*/
	{STK_ADDR__ADAPITVE_BASELINE_0,         STK_ADAPITVE_BASELINE_0__VALUE   },
	{STK_ADDR__ADAPITVE_BASELINE_1,         STK_ADAPITVE_BASELINE_1__VALUE   },
	{STK_ADDR__ADAPITVE_BASELINE_2,         STK_ADAPITVE_BASELINE_2__VALUE   },

	/*CLK_SPREAD*/
	{STK_ADDR__CLK_SPREAD_CTRL,         STK_CLK_SPREAD_CTRL__VALUE   },

	/*SCAN_FACTOR*/
	{STK_ADDR__SCAN_FACTOR,         STK_SCAN_FACTOR__VALUE               },

	/*DOZE_MODE*/
	{STK_ADDR__DOZE_MODE,         STK_DOZE_MODE__TURN_OFF                },

	/*SCAN_PERIOD*/
	{STK_ADDR__SCAN_PERIOD,         STK_SCAN_PERIOD__VALUE                },

	/*AFE_CTRL 0~4*/
	{STK_ADDR__AFE_CTRL_PH0_REG,     STK_AFE_CTRL_PH0_REG__VALUE         },
	{STK_ADDR__AFE_CTRL_PH00_REG,    STK_AFE_CTRL_PH00_REG__VALUE        },
	{STK_ADDR__AFE_CTRL_PH1_REG,     STK_AFE_CTRL_PH1_REG__VALUE         },
	{STK_ADDR__AFE_CTRL_PH11_REG,    STK_AFE_CTRL_PH11_REG__VALUE        },
	{STK_ADDR__AFE_CTRL_PH2_REG,     STK_AFE_CTRL_PH2_REG__VALUE         },
	{STK_ADDR__AFE_CTRL_PH22_REG,    STK_AFE_CTRL_PH22_REG__VALUE        },
	{STK_ADDR__AFE_CTRL_PH3_REG,     STK_AFE_CTRL_PH3_REG__VALUE         },
	{STK_ADDR__AFE_CTRL_PH33_REG,    STK_AFE_CTRL_PH33_REG__VALUE        },
	{STK_ADDR__AFE_CTRL_PH4_REG,     STK_AFE_CTRL_PH4_REG__VALUE         },
	{STK_ADDR__AFE_CTRL_PH44_REG,    STK_AFE_CTRL_PH44_REG__VALUE        },

	/*ANA_CTRL 0~2*/
	{STK_ADDR__ANA_CTRL0_REG,        STK_ANA_CTRL0_REG__VALUE            },
	{STK_ADDR__ANA_CTRL1_REG,        STK_ANA_CTRL1_REG__VALUE            },
	{STK_ADDR__ANA_CTRL2_REG,          STK_ANA_CTRL2_REG__VALUE              },

	/*RX_TIMING 0~4*/
	{STK_ADDR__RX_TIMING0_REG,       STK_RX_TIMING0_REG__VALUE           },
	{STK_ADDR__RX_TIMING1_REG,       STK_RX_TIMING1_REG__VALUE           },
	{STK_ADDR__RX_TIMING2_REG,       STK_RX_TIMING2_REG__VALUE           },
	{STK_ADDR__RX_TIMING3_REG,       STK_RX_TIMING3_REG__VALUE           },
	{STK_ADDR__RX_TIMING4_REG,       STK_RX_TIMING4_REG__VALUE           },

	{STK_ADDR__TCON0_REG,            STK_TCON0_REG__VALUE                },
	{STK_ADDR__TCON1_REG,            STK_TCON1_REG__VALUE                },

	/*FLIT_PH0*/
	{STK_ADDR__REGFILT0_PH0_REG,              STK_REGFILT0_PH0_REG__VALUE        },
	{STK_ADDR__REGSTATEDET0_PH0_REG,          STK_REGSTATEDET0_PH0_REG__VALUE    },
	{STK_ADDR__REGSTATEDET2_PH0_REG,          STK_REGSTATEDET2_PH0_REG__VALUE    },
	{STK_ADDR__REGSTATEDET3_PH0_REG,          STK_REGSTATEDET3_PH0_REG__VALUE    },
	{STK_ADDR__REGADVDIG0_PH0,          0x00010000              },
	{STK_ADDR__REGADVDIG1_PH0,          STK_REGADVDIG1_PH0__VALUE              },
	{STK_ADDR__REGADVDIG2_PH0,          STK_REGADVDIG2_PH0__VALUE              },
	{STK_ADDR__REGADVDIG3_PH0,          STK_REGADVDIG3_PH0__VALUE              },

	/*FLIT_PH1*/
	{STK_ADDR__REGFILT0_PH1_REG,       STK_REGFILT0_PH1_REG__VALUE           },
	{STK_ADDR__REGSTATEDET0_PH1_REG,          STK_REGSTATEDET0_PH1_REG__VALUE    },
	{STK_ADDR__REGSTATEDET2_PH1_REG,          STK_REGSTATEDET2_PH1_REG__VALUE    },
	{STK_ADDR__REGSTATEDET3_PH1_REG,          STK_REGSTATEDET3_PH1_REG__VALUE    },
	{STK_ADDR__REGADVDIG0_PH1,          STK_REGADVDIG0_PH1__VALUE              },
	{STK_ADDR__REGADVDIG1_PH1,          STK_REGADVDIG1_PH1__VALUE              },
	{STK_ADDR__REGADVDIG2_PH1,          STK_REGADVDIG2_PH1__VALUE              },
	{STK_ADDR__REGADVDIG3_PH1,          STK_REGADVDIG3_PH1__VALUE              },

	/*FLIT_PH2*/
	{STK_ADDR__REGFILT0_PH2_REG,       STK_REGFILT0_PH2_REG__VALUE           },
	{STK_ADDR__REGSTATEDET0_PH2_REG,          STK_REGSTATEDET0_PH2_REG__VALUE    },
	{STK_ADDR__REGSTATEDET2_PH2_REG,          STK_REGSTATEDET2_PH2_REG__VALUE    },
	{STK_ADDR__REGSTATEDET3_PH2_REG,          STK_REGSTATEDET3_PH2_REG__VALUE    },
	{STK_ADDR__REGADVDIG0_PH2,          STK_REGADVDIG0_PH2__VALUE              },
	{STK_ADDR__REGADVDIG1_PH2,          STK_REGADVDIG1_PH2__VALUE              },
	{STK_ADDR__REGADVDIG2_PH2,          STK_REGADVDIG2_PH2__VALUE              },
	{STK_ADDR__REGADVDIG3_PH2,          STK_REGADVDIG3_PH2__VALUE              },

	/*FLIT_PH3*/
	{STK_ADDR__REGFILT0_PH3_REG,       STK_REGFILT0_PH3_REG__VALUE           },
	{STK_ADDR__REGSTATEDET0_PH3_REG,          STK_REGSTATEDET0_PH3_REG__VALUE    },
	{STK_ADDR__REGSTATEDET2_PH3_REG,          STK_REGSTATEDET2_PH3_REG__VALUE    },
	{STK_ADDR__REGSTATEDET3_PH3_REG,          STK_REGSTATEDET3_PH3_REG__VALUE    },
	{STK_ADDR__REGADVDIG0_PH3,          STK_REGADVDIG0_PH3__VALUE              },
	{STK_ADDR__REGADVDIG1_PH3,          STK_REGADVDIG1_PH3__VALUE              },
	{STK_ADDR__REGADVDIG2_PH3,          STK_REGADVDIG2_PH3__VALUE              },
	{STK_ADDR__REGADVDIG3_PH3,          STK_REGADVDIG3_PH3__VALUE              },

	/*FLIT_PH4*/
	{ STK_ADDR__REGFILT0_PH4_REG,       STK_REGFILT0_PH4_REG__VALUE },
	{ STK_ADDR__REGSTATEDET0_PH4_REG,          STK_REGSTATEDET0_PH4_REG__VALUE },
	{ STK_ADDR__REGSTATEDET2_PH4_REG,          STK_REGSTATEDET2_PH4_REG__VALUE },
	{ STK_ADDR__REGSTATEDET3_PH4_REG,          STK_REGSTATEDET3_PH4_REG__VALUE },
	{ STK_ADDR__REGADVDIG0_PH4,          STK_REGADVDIG0_PH4__VALUE },
	{ STK_ADDR__REGADVDIG1_PH4,          STK_REGADVDIG1_PH4__VALUE },
	{ STK_ADDR__REGADVDIG2_PH4,          STK_REGADVDIG2_PH4__VALUE },
	{ STK_ADDR__REGADVDIG3_PH4,          STK_REGADVDIG3_PH4__VALUE },

	/*TEMP_VAR*/
	{STK_ADDR__REG_TEMP_VAR0,          STK_REG_TEMP_VAR0__VALUE },
	{STK_ADDR__REG_TEMP_VAR1,          STK_REG_TEMP_VAR1__VALUE },
	{STK_ADDR__REG_TEMP_VAR2,          STK_REG_TEMP_VAR2__VALUE },
	{STK_ADDR__REG_TEMP_VAR3,          STK_REG_TEMP_VAR3__VALUE },

	/*RAW_DES*/
	{STK_ADDR__REG_RAW_DESCEND0,          STK_REG_RAW_DESCEND0__VALUE },
	{STK_ADDR__REG_RAW_DESCEND1,          STK_REG_RAW_DESCEND1__VALUE },
	{STK_ADDR__REG_RAW_DESCEND2,          STK_REG_RAW_DESCEND2__VALUE },

	/*BOUNDARY*/
	{STK_ADDR__REG_BOUNDARY0,          STK_REG_BOUNDARY0__VALUE },
	{STK_ADDR__REG_BOUNDARY1,          STK_REG_BOUNDARY1__VALUE },
	{STK_ADDR__REG_BOUNDARY2,          STK_REG_BOUNDARY2__VALUE },

	/*Auto cali times*/
	{ STK_ADDR__C_UNIT_CTRL_REG,       STK_C_UNIT_CTRL_REG__VALUE },

	{STK_ADDR__TRIGGER_REG,          STK_TRIGGER_REG__INIT_ALL           },
	{STK_ADDR__TRIGGER_CMD_REG,     STK_TRIGGER_CMD_REG__INIT_ALL        },
};

stk5xxx_register_table stk5xxx_default_sar_irq_table[] = {
		{STK_ADDR__REGSTATEDET0_PH0_REG,  STK_REGSTATEDET0_PH0_REG__VALUE},
		{STK_ADDR__REGSTATEDET0_PH1_REG,  STK_REGSTATEDET0_PH1_REG__VALUE},
		/*{STK_ADDR__REGSTATEDET0_PH2_REG,  STK_REGSTATEDET0_PH2_REG__VALUE},*/
		/*{STK_ADDR__REGSTATEDET0_PH3_REG,  STK_REGSTATEDET0_PH3_REG__VALUE},*/
		{STK_ADDR__IRQ_SOURCE_ENABLE_REG, (1 << STK_IRQ_SOURCE_ENABLE_REG_CLOSE_ANY_IRQ_EN__SHIFT) |
			(1 << STK_IRQ_SOURCE_ENABLE_REG_FAR_ANY_IRQ_EN__SHIFT)}
};

#define STK_QUALCOMM_VERSION "0.0.2"
int stk_i2c_probe(struct i2c_client *client, const struct stk_bus_ops *stk5xxx_bus_ops);
int stk_i2c_remove(struct i2c_client *client);

#ifdef CONFIG_OF
static struct of_device_id stk5xxx_match_table[] = {
	{.compatible = "stk,stk5xxx", },
	{}
};
#endif /* CONFIG_OF */
#define STK_ATTR_VERSION "0.0.1"
struct attribute_group stk_attribute_sar_group;
#define STK_DRV_I2C_VERSION "0.0.2"
#define STK_C_VERSION "0.0.3"
static int stk5xxx_ps_set_thd(struct stk_data *stk);
static void stk_clr_intr(struct stk_data *stk, u32 *flag);
void stk_set_enable(struct stk_data *stk, char enable);
void stk_read_temp_data(struct stk_data *stk);
void stk_read_sar_data(struct stk_data *stk);
void stk_data_initialize(struct stk_data *stk);
int stk_get_pid(struct stk_data *stk);
int stk_show_all_reg(struct stk_data *stk, char *show_buffer);
int stk_reg_init(struct stk_data *stk);
int stk5xxx_suspend(struct device *dev);
int stk5xxx_resume(struct device *dev);
static int stk_sw_reset(struct stk_data *stk);
#ifdef STK_FIX_CADC
void stk_fix_cadc(struct stk_data *stk, u32 *flag);
#endif /* STK_FIX_CADC*/
#if defined STK_INTERRUPT_MODE || defined STK_POLLING_MODE
void stk_report_sar_data(struct stk_data *stk);
void stk_work_queue(struct work_struct *work);
#endif /* defined STK_INTERRUPT_MODE || defined STK_POLLING_MODE */
#ifdef STK_INTERRUPT_MODE
int stk_irq_setup(struct stk_data *stk);
void stk_exit_irq_setup(struct stk_data *stk);
#endif /* STK_INTERRUPT_MODE */

static int stk5xxx_ps_set_thd(struct stk_data *stk)
{
	int err = 0;
	u16 reg;
	u32 val;

	reg = STK_ADDR__REGSTATEDET0_PH0_REG;
	val = STK_REGSTATEDET0_PH0_REG__VALUE;
	err = STK_REG_WRITE(stk, reg, (u8 *)&val);
	if (err) {
		dev_err(&stk->client->dev, "stk5xxx_ps_set_thd [0] fail");
		return err;
	}

	reg = STK_ADDR__REGSTATEDET0_PH1_REG;
	val = STK_REGSTATEDET0_PH1_REG__VALUE;
	err = STK_REG_WRITE(stk, reg, (u8 *)&val);
	if (err) {
		dev_err(&stk->client->dev, "stk5xxx_ps_set_thd [1] fail");
		return err;
	}

	reg = STK_ADDR__REGSTATEDET0_PH2_REG;
	val = STK_REGSTATEDET0_PH2_REG__VALUE;
	err = STK_REG_WRITE(stk, reg, (u8 *)&val);
	if (err) {
		dev_err(&stk->client->dev, "stk5xxx_ps_set_thd [2] fail");
		return err;
	}

	reg = STK_ADDR__REGSTATEDET0_PH3_REG;
	val = STK_REGSTATEDET0_PH3_REG__VALUE;
	err = STK_REG_WRITE(stk, reg, (u8 *)&val);
	if (err) {
		dev_err(&stk->client->dev, "stk5xxx_ps_set_thd [3] fail");
		return err;
	}

	return 0;
}

static void stk_clr_intr(struct stk_data *stk, u32 *flag)
{
	STK_REG_READ(stk, STK_ADDR__IRQ_SOURCE, (u8 *)flag);

	dev_info(&stk->client->dev, "stk_clr_intr:: state = 0x%x", *flag);
}

void stk_set_enable(struct stk_data *stk, char enable)
{
	u16 reg = 0;
	u32 val = 0;
	u32 flag = 0;

	dev_info(&stk->client->dev, "stk_set_enable en=%d", enable);
	if (enable) {
		stk5xxx_ps_set_thd(stk);

		reg = STK_ADDR__TRIGGER_CMD_REG;
		val = STK_TRIGGER_CMD_REG__EXIT_PAUSE_MODE;
		STK_REG_WRITE(stk, reg, (u8 *)&val);

#ifdef STK_INTERRUPT_MODE
		/* do nothing */
#elif defined STK_POLLING_MODE
		hrtimer_start(&stk->stk_timer, stk->poll_delay, HRTIMER_MODE_REL);
#endif /* STK_INTERRUPT_MODE, STK_POLLING_MODE */
		} else {
#ifdef STK_INTERRUPT_MODE
		/* do nothing */
#elif defined STK_POLLING_MODE
		hrtimer_cancel(&stk->stk_timer);
#endif /* STK_INTERRUPT_MODE, STK_POLLING_MODE */
		stk->last_nearby = STK5XXX_PRX_NEAR_BY_UNKNOWN;

		reg = STK_ADDR__TRIGGER_CMD_REG;
		val = STK_TRIGGER_CMD_REG__ENTER_PAUSE_MODE;
		STK_REG_WRITE(stk, reg, (u8 *)&val);
	}
	atomic_set(&stk->enabled, enable);
	stk_clr_intr(stk, &flag);

	dev_info(&stk->client->dev, "stk_set_enable DONE");
}

void stk_read_temp_data(struct stk_data *stk)
{
	u16 reg;
	u32 val = 0;
	int output_data = 0;

	/* Phase 1 is defined to temperature*/
	reg = STK_ADDR__REG_RAW_PH1_REG;
	STK_REG_READ(stk, reg, (u8 *)&val);

	if (val & 0x80000000) {
		/*2's complement = 1's complement +1*/
		output_data = ((~val + 1) & 0xFFFFFF80) >> 7;
		output_data *= -1;
	} else {
		output_data = (int)((val & 0xFFFFFF80) >> 7);
	}
	stk->temperature = output_data;
	dev_info(&stk->client->dev, "stk_read_temp_data:: temp = %d(0x%X)", output_data, val);
}

void stk_read_sar_data(struct stk_data *stk)
{
	u16 reg;
	u32 raw_val[6], delta_val[6], flag, cadc_val[2];
	int raw_conv_data[6] = { 0 };
	int delta_conv_data[6] = { 0 };
	int i = 0;

	dev_info(&stk->client->dev, "stk5xxx_ps_get_data start");
	/*stk->timestamp = ktime_get_boottime();*/

	/*phase 0(sensing)*/
	reg = STK_ADDR__REG_RAW_PH0_REG;
	STK_REG_READ(stk, reg, (u8 *)&raw_val[0]);
	reg = STK_ADDR__REG_DELTA_PH0_REG;
	STK_REG_READ(stk, reg, (u8 *)&delta_val[0]);

	/*phase 1(temp)*/
	reg = STK_ADDR__REG_RAW_PH1_REG;
	STK_REG_READ(stk, reg, (u8 *)&raw_val[1]);
	reg = STK_ADDR__REG_DELTA_PH1_REG;
	STK_REG_READ(stk, reg, (u8 *)&delta_val[1]);

	/*cadc*/
	reg = STK_ADDR__REG_CADC_PH1_REG;
	STK_REG_READ(stk, reg, (u8 *)&cadc_val[0]);
#if 0 /*if need more phase, can turn on this*/
	/*phase 2*/
	reg = STK_ADDR__REG_RAW_PH2_REG;
	STK_REG_READ(stk, reg, (u8 *)&raw_val[2]);
	reg = STK_ADDR__REG_DELTA_PH2_REG;
	STK_REG_READ(stk, reg, (u8 *)&val[2]);

	/*phase 3*/
	reg = STK_ADDR__REG_RAW_PH3_REG;
	STK_REG_READ(stk, reg, (u8 *)&raw_val[3]);
	reg = STK_ADDR__REG_DELTA_PH3_REG;
	STK_REG_READ(stk, reg, (u8 *)&val[3]);

	/*phase 4*/
	reg = STK_ADDR__REG_RAW_PH4_REG;
	STK_REG_READ(stk, reg, (u8 *)&raw_val[4]);
	reg = STK_ADDR__REG_DELTA_PH4_REG;
	STK_REG_READ(stk, reg, (u8 *)&val[4]);

	/*phase 5*/
	reg = STK_ADDR__REG_RAW_PH5_REG;
	STK_REG_READ(stk, reg, (u8 *)&raw_val[5]);
	reg = STK_ADDR__REG_DELTA_PH5_REG;
	STK_REG_READ(stk, reg, (u8 *)&val[5]);
#endif
	for (i = 0; i < 2; i++) {

		if (raw_val[i] & 0x80000000) {
			/*2's complement = 1's complement +1*/
			raw_conv_data[i] = ((~raw_val[i] + 1) & 0xFFFFFF80) >> 7;
			raw_conv_data[i] *= -1;
		} else {
			raw_conv_data[i] = (int)((raw_val[i] & 0xFFFFFF80) >> 7);
		}

		dev_info(&stk->client->dev, "stk_read_sar_data:: raw[%d] = %d(0x%X)", i, raw_conv_data[i], raw_val[i]);

		if (delta_val[i] & 0x80000000) {
			/*2's complement = 1's complement +1*/
			delta_conv_data[i] = ((~delta_val[i] + 1) & 0xFFFFFF80) >> 7;
			delta_conv_data[i] *= -1;
		} else {
			delta_conv_data[i] = (int)((delta_val[i] & 0xFFFFFF80) >> 7);
		}

		dev_err(&stk->client->dev, "stk_read_sar_data:: delta[%d] = %d(0x%X)",
			i, delta_conv_data[i], delta_val[i]);
		stk->last_data[i] = delta_conv_data[i];
	}
	dev_info(&stk->client->dev, "stk_fix_cadc:: Fixed CADC Value: Ph[%d] old cadc = 0x%x", i, stk->fix_cadc_value);

	/*read flag*/
	reg = STK_ADDR__REG_IRQ_STATE0_REG;
	STK_REG_READ(stk, reg, (u8 *)&flag);
	dev_info(&stk->client->dev, "stk_read_sar_data:: Read flag =0x%x", flag);

	if (flag & STK_REG_IRQ_STATE0_REG_PROX_STATE__MASK) {
		stk->last_nearby = STK5XXX_PRX_NEAR_BY;
	} else {
		stk->last_nearby = STK5XXX_PRX_FAR_AWAY;
	}
}

#ifdef STK_FIX_CADC
static enum hrtimer_restart stk_reset_timer_func(struct hrtimer *timer)
{
	struct stk_data *stk = container_of(timer, struct stk_data, stk_reset_timer);

	schedule_work(&stk->stk_reset_work);
	return HRTIMER_NORESTART;
}

void stk_reset_phase_queue(struct work_struct *work)
{

	struct stk_data *stk = container_of(work, struct stk_data, stk_reset_work);
	u16 reg = 0;
	u32 value = 0;

	dev_info(&stk->client->dev, "stk_reset_phase:: Reset phase");
	reg = STK_ADDR__TRIGGER_REG;
	value = STK_TRIGGER_REG__INIT_ALL;
	STK_REG_WRITE(stk, reg, (u8 *)&value);
	reg = STK_ADDR__TRIGGER_CMD_REG;
	value = STK_TRIGGER_CMD_REG__INIT_ALL;
	STK_REG_WRITE(stk, reg, (u8 *)&value);
	stk->wait_reset = false;
}

void stk_fix_cadc(struct stk_data *stk, u32 *flag)
{
	u16 reg = 0;
	u32 SenseGain = 0, CFBData = 0, weight = 0, new_weight = 0, value = 0;
	u8 i = 0;
	int GainData; /* float*1000 to int */

	dev_info(&stk->client->dev, "stk_fix_cadc:: Start! is_first_boot = %d, frame_count = %d",
		stk->is_first_boot, stk->frame_count);
	dev_info(&stk->client->dev, "stk_fix_cadc:: IRQ flag = 0x%x, is_sat_state = %d",
		*flag, stk->is_sat_state);

#if 0 /* for FADC debug*/
	reg = 0x0568;
	STK_REG_READ(stk, reg, (u8 *)&value);
	STK_SAR_ERR("stk_fix_cadc:: FADC = %d", value);
#endif

	if (*flag & STK_IRQ_SOURCE__CONVDONE_IRQ__MASK) {
		dev_info(&stk->client->dev, "STK_IRQ_SOURCE__CONVDONE_IRQ__MASK");
		if (stk->is_first_boot) {
			if (stk->frame_count == 0) {
				/* Enable AutoK, Disable Fixed Value(0x0330 + PhN)*/
				for (i = 0; i < 6; i++) {
					/* Disable Fixed Value(0x0330 + PhN)*/
					reg = STK_ADDR__CADC_REG + (i * 0x40);
					STK_REG_READ(stk, reg, (u8 *)&stk->fix_cadc_value);
					stk->fix_cadc_value &= STK_CADC_REG__FIX_VALUE__MASK;
					STK_REG_WRITE(stk, reg, (u8 *)&stk->fix_cadc_value);
				}

				/* Because Same Sense Gain for Every Phase, Using Phase1 for New Weighting Calculation*/
				/* Read Sense Gain*/
				reg = STK_ADDR__REGFILT0_PH1_REG;
				STK_REG_READ(stk, reg, (u8 *)&SenseGain);
				dev_info(&stk->client->dev, "stk_fix_cadc:: SenseGain: 0x%x", SenseGain);

				SenseGain = (SenseGain >> 4) & 0x07;
				if (SenseGain == 0) {
					GainData = 1000;
					dev_info(&stk->client->dev, "stk_fix_cadc:: GainData == 0: %d", GainData);
				} else if ((SenseGain >= 1) && (SenseGain <= 3)) {
					GainData = 1000 / (2 << (SenseGain - 1));
					dev_info(&stk->client->dev, "stk_fix_cadc:: GainData [1~3]: %d", GainData);
				} else if ((SenseGain >= 4) && (SenseGain <= 7)) {
					GainData = (2 << (SenseGain - 4)) * 1000;
					dev_info(&stk->client->dev, "stk_fix_cadc:: GainData [4~7]: %d", GainData);
				}

				/* Read CFB (0x130)*/
				reg = STK_ADDR__AFE_CTRL_PH1_REG;
				STK_REG_READ(stk, reg, (u8 *)&CFBData);
				CFBData = ((CFBData >> 0x14) & 0x0F) + 1;

				/* Read and Calculate New Weight (0x0080)*/
				reg = STK_ADDR__DISABLE_TRIM_RX_WEIGHT_REG;
				STK_REG_READ(stk, reg, (u8 *)&weight);
				weight &= 0xFFFFF;
				dev_info(&stk->client->dev, "stk_fix_cadc:: Old Weight: 0x%05X", weight);

				new_weight = ((GainData / DEFAULT_SENSE_GAIN)
					* (CFBData / DEFAULT_CFB) * weight) / 1000;
				new_weight &= 0xFFFFF;
				dev_info(&stk->client->dev, "stk_fix_cadc:: New Weight: 0x%05X", new_weight);

				if (weight != new_weight) {
					/* Unlock OTP, Write New Weight, Lock OTP*/
					reg = STK_ADDR__TRIM_LOCK_REG;
					value = STK_TRIM_UNLOCK_REG__VALUE;
					STK_REG_WRITE(stk, reg, (u8 *)&value);

					reg = STK_ADDR__DISABLE_TRIM_RX_WEIGHT_REG;
					STK_REG_WRITE(stk, reg, (u8 *)&new_weight);

					reg = STK_ADDR__TRIM_LOCK_REG;
					value = STK_TRIM_LOCK_REG__VALUE;
					STK_REG_WRITE(stk, reg, (u8 *)&value);

					/* phase init(must delay)*/
					reg = STK_ADDR__TRIGGER_REG;
					value = STK_TRIGGER_REG_PHEN__DISABLE_ALL;
					STK_REG_WRITE(stk, reg, (u8 *)&value);

					reg = STK_ADDR__TRIGGER_CMD_REG;
					value = STK_TRIGGER_CMD_REG__INIT_ALL;
					STK_REG_WRITE(stk, reg, (u8 *)&value);
#if 0 /* if in the feature, FSM support reset, no require delay, can use this.*/
					/*delay(25);*/

					reg = STK_ADDR__TRIGGER_REG;
					value = STK_TRIGGER_REG__INIT_ALL;
					STK_REG_WRITE(stk, reg, (u8 *)&value);
					reg = STK_ADDR__TRIGGER_CMD_REG;
					value = STK_TRIGGER_CMD_REG__INIT_ALL;
					STK_REG_WRITE(stk, reg, (u8 *)&value);
#else
					INIT_WORK(&stk->stk_reset_work, stk_reset_phase_queue);
					hrtimer_init(&stk->stk_reset_timer,
						CLOCK_MONOTONIC, HRTIMER_MODE_REL);
					stk->stk_reset_timer.function = stk_reset_timer_func;
					hrtimer_start(&stk->stk_reset_timer,
						ns_to_ktime(100000 * NSEC_PER_USEC), HRTIMER_MODE_REL);
					stk->wait_reset = true;
					dev_info(&stk->client->dev, "stk_fix_cadc:: Wait 100ms for resetting phase");
#endif
				}
			} else if (stk->frame_count == 2) {
				for (i = 0; i < 6; i++) {
					/* Read CADC Value(0x0548 + PhN)*/
					/* Write Back to CADC Fixed Value, and Enable Fixed Value(0x0330 + PhN)*/
					reg = STK_ADDR__REG_CADC_PH0_REG + (i * 0x4);
					STK_REG_READ(stk, reg, (u8 *)&stk->fix_cadc_value);

					stk->fix_cadc_value = (stk->fix_cadc_value << 4)
						& STK_CADC_REG__FIX_VALUE__MASK;
					dev_info(&stk->client->dev, "stk_fix_cadc::First fixed CADC Value: Ph[%d]=0x%x",
						i, stk->fix_cadc_value);
					value = stk->fix_cadc_value | 0x01;

					reg = STK_ADDR__CADC_REG + i * 0x40;
					STK_REG_WRITE(stk, reg, (u8 *)&value);

					/* Saturation Configure(0x0318 + PhN)*/
					reg = STK_ADDR__STATE_DET4_PH0_REG;
					value = STK_STATE_DET4_PH0_REG__VALUE;
					STK_REG_WRITE(stk, (reg + i * 0x40), (u8 *)&value);
				}

				/* Enable Saturation State on CUSTOM_A_STATE(ph0)/(Ph1~4).*/
				reg = 0x017C;
				value = 0x36261606;/*for ph[0~3];*/
				/*value = 0x46362616;  for ph[1~4]*/
				STK_REG_WRITE(stk, reg, (u8 *)&value);
				reg = 0x0180;
				value = 00000116; /*for ph[0]*/

				STK_REG_WRITE(stk, reg, (u8 *)&value);

				reg = 0x0168;
				value = 0x00004500;
				STK_REG_WRITE(stk, reg, (u8 *)&value);

				/* Enable Saturation State on PROG0_IRG & PROG1_IRG*/
				value = (1 << STK_IRQ_SOURCE_ENABLE_REG_CLOSE_ANY_IRQ_EN__SHIFT) |
					(1 << STK_IRQ_SOURCE_ENABLE_REG_FAR_ANY_IRQ_EN__SHIFT) |
					(1 << STK_IRQ_SOURCE_ENABLE_REG_CONVDONE_IRQ_EN__SHIFT) |
					(1 << STK_IRQ_SOURCE_ENABLE_REG_PROG1_IRQ_EN__SHIFT) |
					(1 << STK_IRQ_SOURCE_ENABLE_REG_PROG0_IRQ_EN__SHIFT);

				reg = STK_ADDR__IRQ_SOURCE_ENABLE_REG;
				STK_REG_WRITE(stk, reg, (u8 *)&value);

				stk->is_first_boot = false;
			}
			stk->frame_count++;
		} else if (stk->is_sat_state) {
			for (i = 0; i < 6; i++) {
				if (stk->sat_data & (1 << i)) {
					/* Read CADC Value(0x0548 + PhN)*/
					/* Write Back to CADC Fixed Value, and Enable Fixed Value(0x0330 + PhN)*/
					reg = STK_ADDR__REG_CADC_PH0_REG + (i * 0x4);
					STK_REG_READ(stk, reg, (u8 *)&stk->fix_cadc_value);

					stk->fix_cadc_value = (stk->fix_cadc_value << 4)
						& STK_CADC_REG__FIX_VALUE__MASK;
					dev_info(&stk->client->dev, "stk_fix_cadc:: Fixed CADC Value: Ph[%d] old cadc = 0x%x",
								i, stk->fix_cadc_value);
					value = stk->fix_cadc_value | 0x01;
					reg = STK_ADDR__CADC_REG + (i * 0x40);
					STK_REG_WRITE(stk, reg, (u8 *)&value);
				}
			}
			stk->sat_data = 0;
			stk->is_sat_state = false;
			return;
		}
	}

	/* Saturation IRQ on PROG0_IRQ & PROG1_IRQ*/
	if ((*flag & STK_IRQ_SOURCE__PROG0_IRQ__MASK) ||
		(*flag & STK_IRQ_SOURCE__PROG1_IRQ__MASK)) {
		dev_info(&stk->client->dev, "stk_fix_cadc:: Sat trigger!");
		if (!stk->is_sat_state) {
			/*reg = STK_ADDR__STATE1_REG;*/
			/*STK_REG_READ(stk, reg, (u8*)&stk->sat_data);*/
			/*stk->sat_data = (stk->sat_data & STK_STATE1__SAT_STATE__MASK) >> 16;*/
			/*STK_SAR_LOG("stk_fix_cadc::  SAT Data: 0x%02X", stk->sat_data);*/

			/*read custom for workaround*/
			if (*flag & STK_IRQ_SOURCE__PROG0_IRQ__MASK) {
			/*workaround for ph0/4  */
			stk->sat_data = 0x2;
			} else if (*flag & STK_IRQ_SOURCE__PROG1_IRQ__MASK) {
				/*no use now*/
				/*stk->sat_data = 0x01;*/
			}

			if (stk->sat_data != 0) {
				for (i = 0; i < 6; i++) {
					if (stk->sat_data & (1 << i)) {
						/* Enable AutoK, Disable Fixed Value(0x0330 + PhN)*/
						/*reg = STK_ADDR__CADC_REG + (i * 0x40);*/
						/*STK_REG_READ(stk, reg, (u8*)&stk->fix_cadc_value);*/

						stk->fix_cadc_value &= STK_CADC_REG__FIX_VALUE__MASK;
						STK_REG_WRITE(stk, reg, (u8 *)&stk->fix_cadc_value);
						dev_info(&stk->client->dev, "stk_fix_cadc:: Enable AutoK ph[%d] old cadc = 0x%x",
									i, stk->fix_cadc_value);
					}
				}
			}
			stk->is_sat_state = true;
		}
	}
}
#endif /*STK_FIX_CADC*/

/*
 * @brief: Initialize some data in stk_data.
 *
 * @param[in/out] stk: struct stk_data *
 */
void stk_data_initialize(struct stk_data *stk)
{
	atomic_set(&stk->enabled, 0);
#ifdef STK_FIX_CADC
	stk->is_first_boot = true;
	stk->is_sat_state = false;
	stk->wait_reset = false;
	stk->frame_count = 0;
	stk->sat_data = 0;
#endif /* STK_FIX_CADC*/
	memset(stk->last_data, 0, sizeof(stk->last_data));
	stk->last_nearby = STK5XXX_PRX_NEAR_BY_UNKNOWN;
	dev_info(&stk->client->dev, "stk_data_initialize done\n");
}

/*
 * @brief: Read PID and write to stk_data.pid.
 *
 * @param[in/out] stk: struct stk_data *
 *
 * @return: Success or fail.
 *          0: Success
 *          others: Fail
 */
int stk_get_pid(struct stk_data *stk)
{
	int err = 0;
	u32 val = 0;
	err = STK_REG_READ(stk, STK_ADDR__CHIP_INDEX, (u8 *)&val);
	dev_info(&stk->client->dev, "PID reg=0x%x", val);

	if (err) {
		dev_err(&stk->client->dev, "failed to read PID");
		return -EIO;
	} else {
		stk->pid = (val & STK_CHIP_INDEX_CHIP_ID__MASK) >> STK_CHIP_INDEX_CHIP_ID__SHIFT;
	}

	return err;
}

/*
 * @brief: Read all register (0x0 ~ 0x3F)
 *
 * @param[in/out] stk: struct stk_data *
 * @param[out] show_buffer: record all register value
 *
 * @return: buffer length or fail
 *          positive value: return buffer length
 *          -1: Fail
 */
int stk_show_all_reg(struct stk_data *stk, char *show_buffer)
{
	int reg_num, reg_count = 0;
	u32 val = 0;
	u16 reg_array[] = {
		STK_ADDR__CHIP_INDEX,
		STK_ADDR__IRQ_SOURCE,
		STK_ADDR__IRQ_SOURCE_ENABLE_REG,
		STK_ADDR__TRIGGER_REG,
		STK_ADDR__TRIGGER_CMD_REG,
		STK_ADDR__RXIO0_MUX_REG,
		STK_ADDR__RXIO1_MUX_REG,
		STK_ADDR__RXIO2_MUX_REG,
		STK_ADDR__RXIO3_MUX_REG,
		STK_ADDR__RXIO4_MUX_REG,
		STK_ADDR__RXIO5_MUX_REG,
		STK_ADDR__ADC_DEGLITCH,
		STK_ADDR__AFE_CTRL_PH0_REG,
		STK_ADDR__AFE_CTRL_PH00_REG,
		STK_ADDR__ANA_CTRL0_REG,
		STK_ADDR__ANA_CTRL1_REG,
		STK_ADDR__ANA_CTRL2_REG,
		STK_ADDR__RX_TIMING0_REG,
		STK_ADDR__RX_TIMING1_REG,
		STK_ADDR__RX_TIMING2_REG,
		STK_ADDR__RX_TIMING3_REG,
		STK_ADDR__RX_TIMING4_REG,
		STK_ADDR__TCON0_REG,
		STK_ADDR__TCON1_REG,
		STK_ADDR__REGFILT0_PH0_REG,
		STK_ADDR__REGSTATEDET0_PH0_REG,
		STK_ADDR__C_UNIT_CTRL_REG,
		0x017C,
		0x0180,
		0x0024,
		0x0320,
		0x0408,
	};

	if (show_buffer == NULL)
		return STKINITERR;

	reg_num = sizeof(reg_array) / sizeof(u16);

	dev_info(&stk->client->dev, "stk_show_all_reg::");
	for (reg_count = 0; reg_count < reg_num; reg_count++) {
		STK_REG_READ(stk, reg_array[reg_count], (u8 *)&val);

		dev_info(&stk->client->dev, "reg_array[0x%04x] = 0x%x", reg_array[reg_count], val);
	}

	return 0;
}

int stk_reg_init(struct stk_data *stk)
{
	int err = 0;
	u16 reg_count = 0;
	u16 reg_num = 0;
	u16 reg;
	u32 val;


	/* SW reset */
	err = stk_sw_reset(stk);

	reg_num = sizeof(stk5xxx_default_register_table) / sizeof(stk5xxx_register_table);

	for (reg_count = 0; reg_count < reg_num; reg_count++) {
		reg = stk5xxx_default_register_table[reg_count].address;
		val = stk5xxx_default_register_table[reg_count].value;
		err = STK_REG_WRITE(stk, reg, (u8 *)&val);
		if (err)
			return err;
	}

	reg_num = sizeof(stk5xxx_default_sar_irq_table) / sizeof(stk5xxx_register_table);

	for (reg_count = 0; reg_count < reg_num; reg_count++) {
		reg = stk5xxx_default_sar_irq_table[reg_count].address;
		val = stk5xxx_default_sar_irq_table[reg_count].value;
		err = STK_REG_WRITE(stk, reg, (u8 *)&val);
		if (err)
			return err;
	}

	/* Write temp cali recipe*/
#ifdef STK_TEMP_CALI_START
	reg = 0x0320;
	val = 0x00010000;
	err = STK_REG_WRITE(stk, reg, (u8 *)&val);

#elif defined STK_TEMP_CALI_DONE
	reg = 0x0320;
	val = 0x00010000;

	err = STK_REG_WRITE(stk, reg, (u8 *)&val);

	/*Trim unlock*/
	reg = 0x0008;
	val = 0x000000A5;
	STK_REG_WRITE(stk, reg, (u8 *)&val);

	reg = 0x0024;
	STK_REG_READ(stk, reg, (u8 *)&val);
	dev_info(&stk->client->dev, "stk_read_temp_data:: before = 0x%X", val);

	/*temp coef sign*/
	reg = 0x0024;
	if (STK_COEF_T_SIGN < 0) {
		val = ~(~val | 0x00000004);
	} else {
		val |= 0x00000004;
	}
	STK_REG_WRITE(stk, reg, (u8 *)&val);

	/*Trim lock*/
	reg = 0x0008;
	val = 0x0000005A;
	STK_REG_WRITE(stk, reg, (u8 *)&val);

	reg = 0x0320;
	val = 0x00050000 | STK_COEF_T; /* COEF_T = 8bit raw*/
	STK_REG_WRITE(stk, reg, (u8 *)&val);

	reg = 0x0408;
	val = 0x01000000;
	STK_REG_WRITE(stk, reg, (u8 *)&val);

	reg = 0x0024;
	STK_REG_READ(stk, reg, (u8 *)&val);
	dev_info(&stk->client->dev, "stk_read_temp_data:: after = 0x%X", val);

#endif /* STK_TEMP_CALI_START STK_TEMP_CALI_DONE*/

	/* set power down for default*/
	stk_set_enable(stk, 1);

	return 0;
}

int stk5xxx_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct stk_data *stk = i2c_get_clientdata(client);

	if (atomic_read(&stk->enabled)) {
		stk_set_enable(stk, 0);
		stk->last_enable = true;
	} else
		stk->last_enable = false;

	return 0;
}

int stk5xxx_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct stk_data *stk = i2c_get_clientdata(client);

	if (stk->last_enable)
		stk_set_enable(stk, 1);

	stk->last_enable = false;
	return 0;
}

/*
 * @brief: SW reset for stk5xxx
 *
 * @param[in/out] stk: struct stk_data *
 *
 * @return: Success or fail.
 *          0: Success
 *          others: Fail
 */
static int stk_sw_reset(struct stk_data *stk)
{
	int err = 0;

	u16 reg = STK_ADDR__SOFT_RESET_REG;
	u32 val = STK_SOFT_RESET_REG__SOFT_RESET_CMD;

	err = STK_REG_WRITE(stk, reg, (u8 *)&val);

	if (err)
		return err;

	usleep_range(1000, 2000);
	return 0;
}

#if defined STK_INTERRUPT_MODE || defined STK_POLLING_MODE

void stk_report_sar_data(struct stk_data *stk)
{
	if (!stk->input_dev) {
		dev_info(&stk->client->dev, "No input device for sar data");
		return;
	}

	/*input_report_abs(stk->input_dev, ABS_DISTANCE, stk->last_data[0]);*/
	input_report_abs(stk->input_dev, ABS_DISTANCE, stk->last_nearby);
	input_sync(stk->input_dev);
	dev_info(&stk->client->dev, "stk_report_sar_data:: near/far flag =0x%x", stk->last_nearby);
}

void stk_work_queue(struct work_struct *work)
{
	u32 flag = 0;
	struct stk_data *stk = container_of(work, struct stk_data, stk_work);
#ifdef STK_INTERRUPT_MODE
	dev_info(&stk->client->dev, "stk_work_queue:: Interrupt mode");
#elif defined STK_POLLING_MODE
	dev_info(&stk->client->dev, "stk_work_queue:: Polling mode");
#endif /* STK_INTERRUPT_MODE*/

	stk_clr_intr(stk, &flag);

#ifdef STK_FIX_CADC
	if (stk->wait_reset) {
		dev_err(&stk->client->dev, "stk_work_queue:: Wait ph reset now = %d", stk->wait_reset);
	} else {
		stk_read_sar_data(stk);
		stk_fix_cadc(stk, &flag);
		stk_report_sar_data(stk);
	}
#else
	stk_read_sar_data(stk);
	stk_report_sar_data(stk);
#endif /* STK_FIX_CADC*/

#ifdef STK_INTERRUPT_MODE
	enable_irq(stk->irq);
#elif defined STK_POLLING_MODE
	/*hrtimer_forward_now(&stk->stk_timer, stk->poll_delay);*/
#endif /* defined STK_INTERRUPT_MODE || defined STK_POLLING_MODE */
}
#endif /* defined STK_INTERRUPT_MODE || defined STK_POLLING_MODE */

#ifdef STK_INTERRUPT_MODE
static irqreturn_t stk_irq_handler(int irq, void *data)
{
	struct stk_data *stk = data;
	dev_info(&stk->client->dev, "stk_irq_handler");
	disable_irq_nosync(irq);
	schedule_work(&stk->stk_work);
	return IRQ_HANDLED;
}

int stk_irq_setup(struct stk_data *stk)
{
	int irq = 0;

	gpio_direction_input(stk->int_pin);
	irq = gpio_to_irq(stk->int_pin);

	if (irq < 0) {
		dev_err(&stk->client->dev, "gpio_to_irq(%d) failed", stk->int_pin);
		return STKINITERR;
	}

	stk->irq = irq;
	dev_info(&stk->client->dev, "irq #=%d, int pin=%d", stk->irq, stk->int_pin);

	irq = request_irq(stk->irq, stk_irq_handler, IRQF_TRIGGER_FALLING, "stk_sar_irq", stk);

	if (irq < 0) {
		dev_err(&stk->client->dev, "request_irq(%d) failed for %d", stk->irq, irq);
		return STKINITERR;
	}

	return irq;
}

void stk_exit_irq_setup(struct stk_data *stk)
{
	free_irq(stk->irq, stk);
}

#elif defined STK_POLLING_MODE
static enum hrtimer_restart stk_timer_func(struct hrtimer *timer)
{
	struct stk_data *stk = container_of(timer, struct stk_data, stk_timer);

	schedule_work(&stk->stk_work);
	hrtimer_forward_now(&stk->stk_timer, stk->poll_delay);
	return HRTIMER_RESTART;
}
#endif /* STK_INTERRUPT_MODE, STK_POLLING_MODE */
/*
 * stk5xxx register write
 * @brief: Register writing via I2C
 *
 * @param[in/out] stk: struct stk_data *
 * @param[in] reg: Register address
 * @param[in] val: Data, what you want to write.
 *
 * @return: Success or fail
 *          0: Success
 *          others: Fail
 */
static int stk_reg_write(struct stk_data *stk, u16 reg, u8 *val)
{
	int error = 0;

	u8 buffer_inverse[6] = { reg >> 8, reg & 0xff, val[3], val[2], val[1], val[0] };

	struct i2c_msg msgs = {
			.addr = stk->client->addr,
			.flags = stk->client->flags & I2C_M_TEN,
			.len = 6,
			.buf = buffer_inverse
	};


	mutex_lock(&stk->i2c_lock);
	error = i2c_transfer(stk->client->adapter, &msgs, 1);
	mutex_unlock(&stk->i2c_lock);

	if (error == 1) {
		error = 0;
	} else if (error < 0) {
		dev_err(&stk->client->dev, "transfer failed to write reg:0x%x , error=%d", reg, error);
		return -EIO;
	}

	dev_err(&stk->client->dev, "size error in write reg:0x%x with error=%d", reg, error);

	return error;
}

/*
 * stk5xxx register read
 * @brief: Register reading via I2C
 *
 * @param[in/out] stk: struct stk_data *
 * @param[in] reg: Register address
 * @param[in] len: 0, for normal usage. Others, read length (FIFO used).
 * @param[out] val: Data, the register what you want to read.
 *
 * @return: Success or fail
 *          0: Success
 *          others: Fail
 */
static int stk_reg_read(struct stk_data *stk, u16 reg, int len, u8 *val)
{
	int error = 0;
	int i = 0;
	u16 reg_inverse = (reg & 0x00FF) << 8 | (reg & 0xFF00) >> 8;
	int read_length = (len <= 0) ? 1 * 4 : len * 4;
	u8 buffer_inverse[99] = { 0 };

	struct i2c_msg msgs[2] = {
		{
			.addr = stk->client->addr,
			.flags = 0,
			.len = 2,
			.buf = (u8 *)&reg_inverse
		},
		{
			.addr = stk->client->addr,
			.flags = I2C_M_RD,
			.len = read_length,
			.buf = buffer_inverse
		}
	};

	mutex_lock(&stk->i2c_lock);
	error = i2c_transfer(stk->client->adapter, msgs, 2);
	mutex_unlock(&stk->i2c_lock);

	if (error == 2) {
		error = 0;
		for (i = 0; i < read_length; i++) {
			val[i] = buffer_inverse[read_length - 1 - i];
		}
	} else if (error < 0) {
		dev_err(&stk->client->dev, "transfer failed to read reg:0x%x with addr=0x%x len:%d, error=%d",
						reg_inverse, stk->client->addr, read_length, error);
	} else {
		dev_err(&stk->client->dev, "size error in reading reg:0x%x with len:%d, error=%d", reg, len, error);
		return -EIO;
	}

	return error;
}

static int stk_read(struct stk_data *stk, unsigned short addr, unsigned char *val)
{
	return stk_reg_read(stk, addr, 0, val);
}

static int stk_read_block(struct stk_data *stk, unsigned short addr, int len, unsigned char *val)
{
	return stk_reg_read(stk, addr, len, val);
}

static int stk_write(struct stk_data *stk, unsigned short addr, unsigned char *val)
{
	return stk_reg_write(stk, addr, val);
}

/* Bus operations */
static const struct stk_bus_ops stk5xxx_bus_ops = {
	.read = stk_read,
	.read_block = stk_read_block,
	.write = stk_write,
};

/*
 * @brief: Proble function for i2c_driver.
 *
 * @param[in] client: struct i2c_client *
 * @param[in] id: struct i2c_device_id *
 *
 * @return: Success or fail
 *          0: Success
 *          others: Fail
 */
static int stk5xxx_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	return stk_i2c_probe(client, &stk5xxx_bus_ops);
}

/*
 * @brief: Remove function for i2c_driver.
 *
 * @param[in] client: struct i2c_client *
 *
 * @return: 0
 */
static int stk5xxx_i2c_remove(struct i2c_client *client)
{
	return stk_i2c_remove(client);
}

static int stk5xxx_i2c_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	strlcpy(info->type, STK5XXX_NAME, sizeof(info->type));
	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * @brief: Suspend function for dev_pm_ops.
 *
 * @param[in] dev: struct device *
 *
 * @return: 0
 */
static int stk5xxx_i2c_suspend(struct device *dev)
{
	return stk5xxx_suspend(dev);
}

/*
 * @brief: Resume function for dev_pm_ops.
 *
 * @param[in] dev: struct device *
 *
 * @return: 0
 */
static int stk5xxx_i2c_resume(struct device *dev)
{
	return stk5xxx_resume(dev);
}

static const struct dev_pm_ops stk5xxx_pm_ops = {
	.suspend = stk5xxx_i2c_suspend,
	.resume = stk5xxx_i2c_resume,
};
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_ACPI
static const struct acpi_device_id stk5xxx_acpi_id[] = {
	{"STK5XXX", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, stk5xxx_acpi_id);
#endif /* CONFIG_ACPI */

static const struct i2c_device_id stk5xxx_i2c_id[] = {
	{STK5XXX_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, stk5xxx_i2c_id);

static struct i2c_driver stk5xxx_i2c_driver = {
	.probe = stk5xxx_i2c_probe,
	.remove = stk5xxx_i2c_remove,
	.detect = stk5xxx_i2c_detect,
	.id_table = stk5xxx_i2c_id,
	.class = I2C_CLASS_HWMON,
	.driver = {
	.owner = THIS_MODULE,
	.name = STK5XXX_NAME,
#ifdef CONFIG_PM_SLEEP
	.pm = &stk5xxx_pm_ops,
#endif
#ifdef CONFIG_ACPI
	.acpi_match_table = ACPI_PTR(stk5xxx_acpi_id),
#endif /* CONFIG_ACPI */
#ifdef CONFIG_OF
	.of_match_table = stk5xxx_match_table,
#endif/* CONFIG_OF */
	}
};

/**
 * @brief: Get power status
 *          Send 0 or 1 to userspace.
 *
 * @param[in] dev: struct device *
 * @param[in] attr: struct device_attribute *
 * @param[in/out] buf: char *
 *
 * @return: ssize_t
 */
static ssize_t stk_enable_show(struct device *dev,
						struct device_attribute *attr, char *buf)
{
	struct stk_data *stk = dev_get_drvdata(dev);
	char en;

	en = atomic_read(&stk->enabled);
	return scnprintf(buf, PAGE_SIZE, "%d", en);
}

/**
 * @brief: Set power status
 *          Get 0 or 1 from userspace, then set stk8xxx power status.
 *
 * @param[in] dev: struct device *
 * @param[in] attr: struct device_attribute *
 * @param[in/out] buf: char *
 * @param[in] count: size_t
 *
 * @return: ssize_t
 */
static ssize_t stk_enable_store(struct device *dev,
							struct device_attribute *attr, const char *buf, size_t count)
{
	struct stk_data *stk = dev_get_drvdata(dev);
	unsigned int data;
	int error;

	error = kstrtouint(buf, 10, &data);

	if (error) {
		dev_err(&stk->client->dev, "kstrtoul failed, error=%d", error);
		return error;
	}

	dev_info(&stk->client->dev, "stk_enable_store, data=%d", data);

	if ((data == 1) || (data == 0))
		stk_set_enable(stk, data);
	else
		dev_err(&stk->client->dev, "invalid argument, en=%d", data);

	return count;
}

/**
 * @brief: Get sar data
 *          Send sar data to userspce.
 *
 * @param[in] dev: struct device *
 * @param[in] attr: struct device_attribute *
 * @param[in/out] buf: char *
 *
 * @return: ssize_t
 */
static ssize_t stk_value_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct stk_data *stk = dev_get_drvdata(dev);

	dev_info(&stk->client->dev, "stk_value_show");

	stk_read_sar_data(stk);
	return scnprintf(buf, PAGE_SIZE, "val[0]=%d", stk->last_data[0]);
}

static ssize_t stk_reg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct stk_data *stk = dev_get_drvdata(dev);
	int err = 0;
	u32 reg_address = 0;
	u32 val = 0;

	if (sscanf(buf,  "%x %x",  &reg_address,  &val) != 2) {
		pr_err("%s - The number of data are wrong\n", __func__);
		return -EINVAL;
	}

	err = STK_REG_WRITE(stk, reg_address, (u8 *)&val);
	if (err) {
		pr_err("%s - stk_reg_store write err!\n", __func__);
		return err;
	}

	return count;
}

static ssize_t stk_flag_show(struct device *dev,
						struct device_attribute *attr, char *buf)
{
	struct stk_data *stk = dev_get_drvdata(dev);

	dev_info(&stk->client->dev, "stk_flag_show");

	stk_read_sar_data(stk);
	return scnprintf(buf, PAGE_SIZE, "flag=%d", stk->last_nearby);
}

/**
 * @brief: Register writing
 *          Get address and content from userspace, then write to register.
 *
 * @param[in] dev: struct device *
 * @param[in] attr: struct device_attribute *
 * @param[in/out] buf: char *
 * @param[in] count: size_t
 *
 * @return: ssize_t
 */
static ssize_t stk_send_store(struct device *dev,
						struct device_attribute *attr, const char *buf, size_t count)
{
	struct stk_data *stk = dev_get_drvdata(dev);
	char *token[10];
	int err, i;
	u32 addr, cmd;
	bool enable = false;

	for (i = 0; i < 2; i++)
		token[i] = strsep((char **)&buf, " ");

	err = kstrtouint(token[0], 16, &addr);

	if (err) {
		dev_err(&stk->client->dev, "kstrtoint failed, err=%d", err);
		return err;
	}

	err = kstrtoint(token[1], 32, &cmd);

	if (err) {
		dev_info(&stk->client->dev, "kstrtoint failed, err=%d", err);
		return err;
	}

	dev_info(&stk->client->dev, "write reg[0x%X]=0x%X", addr, cmd);

	if (!atomic_read(&stk->enabled))
		stk_set_enable(stk, 1);
	else
		enable = true;

	if (STK_REG_WRITE(stk, (u16)addr, (u8 *)&cmd)) {
		err = -1;
		goto exit;
	}

exit:
	if (!enable)
		stk_set_enable(stk, 0);

	if (err)
		return STKINITERR;

	return count;
}

static ssize_t stk_temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct stk_data *stk = dev_get_drvdata(dev);

	dev_info(&stk->client->dev, "stk_temp_show");

	stk_read_temp_data(stk);
	return scnprintf(buf, PAGE_SIZE, "temperature=%d", stk->temperature);
}

/**
 * @brief: Read all register value, then send result to userspace.
 *
 * @param[in] dev: struct device *
 * @param[in] attr: struct device_attribute *
 * @param[in/out] buf: char *
 *
 * @return: ssize_t
 */
static ssize_t stk_allreg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct stk_data *stk = dev_get_drvdata(dev);
	int result;

	result = stk_show_all_reg(stk, buf);

	if (result < 0)
		return result;

	return (ssize_t)result;
}

/**
 * @brief: Check PID, then send chip number to userspace.
 *
 * @param[in] dev: struct device *
 * @param[in] attr: struct device_attribute *
 * @param[in/out] buf: char *
 *
 * @return: ssize_t
 */
static ssize_t stk_chipinfo_show(struct device *dev,
						struct device_attribute *attr, char *buf)
{
	struct stk_data *stk = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "pid=0x%x", stk->pid);
}
static u8 diff_ch_num = 1;
static ssize_t stk_diff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct stk_data *stk = dev_get_drvdata(dev);
	int32_t delta_val = 0, baseline_val = 0, raw_val = 0;

	STK_REG_READ(stk, STK_ADDR__REG_DELTA_PH0_REG + diff_ch_num * 4, (u8 *)&raw_val);
	STK_REG_READ(stk, STK_ADDR__REG_DELTA_PH0_REG + diff_ch_num * 4, (u8 *)&baseline_val);
	STK_REG_READ(stk, STK_ADDR__REG_DELTA_PH0_REG + diff_ch_num * 4, (u8 *)&delta_val);
	raw_val /= 128;
	baseline_val /= 128;
	delta_val /= 128;
	pr_err("%s read ph%d raw_rag:0x%x=%d, base_rag:0x%x=%d, delta_rag:0x%x=%d\n",
			__func__,  diff_ch_num, STK_ADDR__REG_DELTA_PH0_REG + diff_ch_num * 4, raw_val,
			STK_ADDR__REG_DELTA_PH0_REG + diff_ch_num * 4, baseline_val,
			STK_ADDR__REG_DELTA_PH0_REG + diff_ch_num * 4, delta_val);
	return snprintf(buf, 64, "%d,%d,%d\n", raw_val, baseline_val, delta_val);

}

static ssize_t stk_diff_phx_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int phx = 1;

	if (sscanf(buf, "%d", &phx) != 1) {
		pr_err("%s - The number of data are wrong\n", __func__);
		return -EINVAL;
	}
	diff_ch_num = phx;

	return count;
}

static ssize_t stk_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct stk_data *stk = dev_get_drvdata(dev);

	if (stk->last_nearby == STK5XXX_PRX_NEAR_BY) {
		return snprintf(buf, 64, "1\n");
	} else {
		return snprintf(buf, 64, "0\n");
	}
}

static DEVICE_ATTR(enable, 0664, stk_enable_show, stk_enable_store);
static DEVICE_ATTR(value, 0444, stk_value_show, NULL);
static DEVICE_ATTR(send, 0220, NULL, stk_send_store);
static DEVICE_ATTR(temp, 0444, stk_temp_show, NULL);
static DEVICE_ATTR(flag, 0444, stk_flag_show, NULL);
static DEVICE_ATTR(reg, 0664, stk_allreg_show, stk_reg_store);
static DEVICE_ATTR(chipinfo, 0444, stk_chipinfo_show, NULL);
static DEVICE_ATTR(diff, 0664, stk_diff_show, stk_diff_phx_store);
static DEVICE_ATTR(status, 0444, stk_status_show, NULL);

static struct attribute *stk_attribute_sar[] = {
	&dev_attr_enable.attr,
	&dev_attr_value.attr,
	&dev_attr_send.attr,
	&dev_attr_temp.attr,
	&dev_attr_flag.attr,
	&dev_attr_reg.attr,
	&dev_attr_chipinfo.attr,
	&dev_attr_diff.attr,
	&dev_attr_status.attr,
	NULL
};

struct attribute_group stk_attribute_sar_group = {
	.name = STK5XXX_NAME,
	.attrs = stk_attribute_sar,
};

struct stk5xxx_platform_data {
	unsigned char   direction;
	int             interrupt_int1_pin;
};

static struct stk5xxx_platform_data stk_plat_data = {
	.direction = 1,
	.interrupt_int1_pin = 117,
};

#ifdef STK_SENSORS_DEV
/* SAR information read by HAL */
static struct sensors_classdev stk_cdev = {
	.name = "stk5xxx",
	.vendor = "Sensortek",
	.version = 1,
	.type = 5013,
	.max_range = "1",  /* 4G mode: 4.0f*9.81f=39.24f */
	.resolution = "1", /* 4G mode,12-bit resolution: 9.81f/512.f=0.01916f */
	.sensor_power = "1",
	.min_delay = 0,
	.max_delay = 0,
	.delay_msec = 16,
	.fifo_reserved_event_count = 0,
	.fifo_max_event_count = 0,
	.enabled = 0,
	.max_latency = 0,
	.flags = 0, /* SENSOR_FLAG_CONTINUOUS_MODE */
	.sensors_enable = NULL,
	.sensors_poll_delay = NULL,
	.sensors_enable_wakeup = NULL,
	.sensors_set_latency = NULL,
	.sensors_flush = NULL,
	.sensors_calibrate = NULL,
	.sensors_write_cal_params = NULL,
};

/*
 * @brief: The handle for enable and disable sensor.
 *          include/linux/sensors.h
 *
 * @param[in] *sensors_cdev: struct sensors_classdev
 * @param[in] enabled:
 */
static int stk_cdev_sensors_enable(struct sensors_classdev *sensors_cdev,
	unsigned int enabled)
{
	struct stk_data *stk = container_of(sensors_cdev, struct stk_data, sar_cdev);

	if (enabled == 0) {
		stk_set_enable(stk, 0);
	} else if (enabled == 1) {
		stk_set_enable(stk, 1);
	} else {
		dev_err(&stk->client->dev, "Invalid vlaue of input, input=%d", enabled);
		return -EINVAL;
	}

	return 0;
}

/*
 * @brief: The handle for set the sensor polling delay time.
 *          include/linux/sensors.h
 *
 * @param[in] *sensors_cdev: struct sensors_classdev
 * @param[in] delay_msec:
 */
static int stk_cdev_sensors_poll_delay(struct sensors_classdev *sensors_cdev,
	unsigned int delay_msec)
{
	struct stk_data *stk = container_of(sensors_cdev, struct stk_data, sar_cdev);
#ifdef STK_INTERRUPT_MODE
	/* do nothing */
#elif defined STK_POLLING_MODE
	stk->poll_delay = ns_to_ktime(delay_msec * 1000 * NSEC_PER_USEC);
#endif /* STK_INTERRUPT_MODE, STK_POLLING_MODE */
	dev_info(&stk->client->dev, "stk_cdev_sensors_poll_delay ms=%d", delay_msec);
	return 0;
}

/*
 * @brief:
 *          include/linux/sensors.h
 *
 * @param[in] *sensors_cdev: struct sensors_classdev
 * @param[in] enable:
 */
static int stk_cdev_sensors_enable_wakeup(struct sensors_classdev *sensors_cdev,
	unsigned int enable)
{
	struct stk_data *stk = container_of(sensors_cdev, struct stk_data, sar_cdev);

	dev_info(&stk->client->dev, "enable=%d", enable);
	return 0;
}

/*
 * @brief: Flush sensor events in FIFO and report it to user space.
 *          include/linux/sensors.h
 *
 * @param[in] *sensors_cdev: struct sensors_classdev
 */
static int stk_cdev_sensors_flush(struct sensors_classdev *sensors_cdev)
{
	struct stk_data *stk = container_of(sensors_cdev, struct stk_data, sar_cdev);

	dev_info(&stk->client->dev, "stk_cdev_sensors_flush");
	return 0;
}
#endif /* STK_SENSORS_DEV*/

/*
 * @brief: File system setup for accel and any motion
 *
 * @param[in/out] stk: struct stk_data *
 *
 * @return: Success or fail
 *          0: Success
 *          others: Fail
 */
static int stk_input_setup(struct stk_data *stk)
{
	int err = 0;

	/* input device: setup for sar */
	stk->input_dev = input_allocate_device();

	if (!stk->input_dev) {
		dev_err(&stk->client->dev, "input_allocate_device for sar failed");
		return -ENOMEM;
	}

	stk->input_dev->name = STK5XXX_NAME;
	stk->input_dev->id.bustype = BUS_I2C;
	input_set_capability(stk->input_dev, EV_ABS, ABS_RX);
	input_set_capability(stk->input_dev, EV_ABS, ABS_RY);
	input_set_capability(stk->input_dev, EV_ABS, ABS_RZ);

	stk->input_dev->dev.parent = &stk->client->dev;
	input_set_drvdata(stk->input_dev, stk);

	err = input_register_device(stk->input_dev);

	if (err) {
		dev_err(&stk->client->dev, "Unable to register input device: %s", stk->input_dev->name);
		input_free_device(stk->input_dev);
		return err;
	}
	return 0;
}

/*
 * @brief:
 *
 * @param[in/out] stk: struct stk_data *
 *
 * @return:
 *      0: Success
 *      others: Fail
 */
static int stk_init_qualcomm(struct stk_data *stk)
{
	int err = 0;

	if (stk_input_setup(stk)) {
		return STKINITERR;
	}

	/* sysfs: create file system */
	err = sysfs_create_group(&stk->client->dev.kobj,
		&stk_attribute_sar_group);
	if (err) {
		dev_err(&stk->client->dev, "Fail in sysfs_create_group, err=%d", err);
		goto err_sysfs_creat_group;
	}
#ifdef STK_SENSORS_DEV
	stk->sar_cdev = stk_cdev;
	stk->sar_cdev.name = "stk5xxx";
	/*mark*/
	stk->sar_cdev.sensors_enable = stk_cdev_sensors_enable;
	stk->sar_cdev.sensors_poll_delay = stk_cdev_sensors_poll_delay;
	stk->sar_cdev.sensors_enable_wakeup = stk_cdev_sensors_enable_wakeup;
	stk->sar_cdev.sensors_flush = stk_cdev_sensors_flush;

	err = sensors_classdev_register(&stk->input_dev->dev, &stk->sar_cdev);
#endif /* CLASSDEVICE*/
	if (err) {
		dev_err(&stk->client->dev, "Fail in sensors_classdev_register, err=%d", err);
		goto err_sensors_classdev_register;
	}

	return 0;

err_sensors_classdev_register:
	sysfs_remove_group(&stk->client->dev.kobj, &stk_attribute_sar_group);
err_sysfs_creat_group:
	input_free_device(stk->input_dev);
	input_unregister_device(stk->input_dev);
	return STKINITERR;
}

/*
 * @brief: Exit qualcomm related settings safely.
 *
 * @param[in/out] stk: struct stk_data *
 */
static void stk_exit_qualcomm(struct stk_data *stk)
{
#ifdef STK_SENSORS_DEV
	sensors_classdev_unregister(&stk->sar_cdev);
#endif /* CLASSDEVICE*/
	sysfs_remove_group(&stk->client->dev.kobj,
		&stk_attribute_sar_group);
	input_free_device(stk->input_dev);
	input_unregister_device(stk->input_dev);
}

#ifdef CONFIG_OF
/*
 * @brief: Parse data in device tree
 *
 * @param[in] dev: struct device *
 * @param[in/out] pdata: struct stk5xxx_platform_data *
 *
 * @return: Success or fail
 *          0: Success
 *          others: Fail
 */
static int stk_parse_dt(struct device *dev,
	struct stk5xxx_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	const int *p;
	uint32_t int_flags;

	p = of_get_property(np, "stk,direction", NULL);

	if (p)
		pdata->direction = be32_to_cpu(*p);

	pdata->interrupt_int1_pin = of_get_named_gpio_flags(np,
		"stk5xxx,irq-gpio", 0, &int_flags);

	if (pdata->interrupt_int1_pin < 0) {
		dev_err(dev, "Unable to read stk5xxx,irq-gpio");
#ifdef STK_INTERRUPT_MODE
		return pdata->interrupt_int1_pin;
#else /* no STK_INTERRUPT_MODE */
		return 0;
#endif /* STK_INTERRUPT_MODE */
	}

	return 0; /* SUCCESS */
}
#else
static int stk_parse_dt(struct device *dev,
	struct stk5xxx_platform_data *pdata)
{
	return -ENODEV
}
#endif /* CONFIG_OF */

/*
 * @brief: Get platform data
 *
 * @param[in/out] stk: struct stk_data *
 *
 * @return: Success or fail
 *          0: Success
 *          others: Fail
 */
static int get_platform_data(struct stk_data *stk)
{
	int err = 0;
	struct stk5xxx_platform_data *stk_platdata;

	if (stk->client->dev.of_node) {
		dev_info(&stk->client->dev,  "get_platform_data!\n");
		stk_platdata = devm_kzalloc(&stk->client->dev,
			sizeof(struct stk5xxx_platform_data), GFP_KERNEL);

		if (!stk_platdata) {
			dev_err(&stk->client->dev,  "Failed to allocate memory!\n");
			return -ENOMEM;
		}

		err = stk_parse_dt(&stk->client->dev, stk_platdata);

		if (err) {
			dev_err(&stk->client->dev,  "stk_parse_dt err=%d!\n", err);
			return err;
		}
	} else {
		if (stk->client->dev.platform_data != NULL) {
			dev_err(&stk->client->dev,  "probe with platform data.\n");
			stk_platdata = stk->client->dev.platform_data;
		} else {
			dev_err(&stk->client->dev, "probe with private platform data.\n");
			stk_platdata = &stk_plat_data;
		}
	}

#ifdef STK_INTERRUPT_MODE
	stk->int_pin = stk_platdata->interrupt_int1_pin;
#endif /* STK_INTERRUPT_MODE */
	/*stk->direction = stk_platdata->direction;*/
	return 0;
}

static struct class sar_sensor_class = {
	.name = "sarsensor",
	.owner = THIS_MODULE,
};

static ssize_t delay_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	dev_info(&stk_sar_ptr->client->dev, "delay_show");
	return snprintf(buf, 8, "%d\n", 200);
}

static ssize_t delay_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	dev_info(&stk_sar_ptr->client->dev, "delay_store");
	return count;
}

static CLASS_ATTR_RW(delay);

static ssize_t enable_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	dev_info(&stk_sar_ptr->client->dev, "enable_show");
	return snprintf(buf, 8, "%d\n", 1);
}

static ssize_t enable_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	dev_info(&stk_sar_ptr->client->dev, "enable_store");
	return count;
}

static CLASS_ATTR_RW(enable);

static ssize_t chip_info_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	dev_info(&stk_sar_ptr->client->dev, "chip_info_show, chip_info = %s\n", chip_info);
	return snprintf(buf, 25, "%s", chip_info);
}

static CLASS_ATTR_RO(chip_info);

static ssize_t status_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	dev_info(&stk_sar_ptr->client->dev, "status_show,status = %d\n", stk_sar_ptr->last_nearby);
	if (stk_sar_ptr->last_nearby == STK5XXX_PRX_NEAR_BY) {
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
	dev_info(&stk_sar_ptr->client->dev, "batch_show sar sensor\n");
	return snprintf(buf, 64, "200\n");
}

static ssize_t batch_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	dev_info(&stk_sar_ptr->client->dev, "batch_store sar sensor\n");
	return count;
}
static CLASS_ATTR_RW(batch);

static ssize_t flush_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	dev_info(&stk_sar_ptr->client->dev, "flush_show sar sensor\n");
	return snprintf(buf, 64, "0\n");
}

static ssize_t flush_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	dev_info(&stk_sar_ptr->client->dev, "flush_store sar sensor\n");
	return count;
}
static CLASS_ATTR_RW(flush);

static ssize_t diff_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	struct stk_data *stk = stk_sar_ptr;
	int32_t delta_val = 0, baseline_val = 0, raw_val = 0;

	STK_REG_READ(stk, STK_ADDR__REG_RAW_PH0_REG + diff_ch_num * 4, (u8 *)&raw_val);
	STK_REG_READ(stk, STK_REG_BASE_PH0_REG_ADDR + diff_ch_num * 4, (u8 *)&baseline_val);
	STK_REG_READ(stk, STK_ADDR__REG_DELTA_PH0_REG + diff_ch_num * 4, (u8 *)&delta_val);
	raw_val /= 128;
	baseline_val /= 128;
	delta_val /= 128;
	pr_err("%s read ph%d raw_rag:0x%x=%d, base_rag:0x%x=%d, delta_rag:0x%x=%d\n",
			__func__,  diff_ch_num, STK_ADDR__REG_RAW_PH0_REG + diff_ch_num * 4, raw_val,
			STK_REG_BASE_PH0_REG_ADDR + diff_ch_num * 4, baseline_val,
			STK_ADDR__REG_DELTA_PH0_REG + diff_ch_num * 4, delta_val);
	return snprintf(buf, 64, "%d,%d,%d\n", raw_val, baseline_val, delta_val);
}

static ssize_t diff_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	int phx = 1;

	if (sscanf(buf, "%d", &phx) != 1) {
		pr_err("%s - The number of data are wrong\n", __func__);
		return -EINVAL;
	}
	diff_ch_num = phx;

	return count;
}
static CLASS_ATTR_RW(diff);

/*
 * @brief: Probe function for i2c_driver.
 *
 * @param[in] client: struct i2c_client *
 * @param[in] stk_bus_ops: const struct stk_bus_ops *
 *
 * @return: Success or fail
 *          0: Success
 *          others: Fail
 */
int stk_i2c_probe(struct i2c_client *client, const struct stk_bus_ops *stk5xxx_bus_ops)
{
	int err = 0;
	struct stk_data *stk;

	dev_info(&client->dev, "STK_HEADER_VERSION: %s ", STK_HEADER_VERSION);
	dev_info(&client->dev, "STK_C_VERSION: %s ", STK_C_VERSION);
	dev_info(&client->dev, "STK_DRV_I2C_VERSION: %s ", STK_DRV_I2C_VERSION);
	dev_info(&client->dev, "STK_QUALCOMM_VERSION: %s ", STK_QUALCOMM_VERSION);

	if (client == NULL) {
		return -ENOMEM;
	} else if (stk5xxx_bus_ops == NULL) {
		dev_err(&client->dev, "cannot get stk_bus_ops. EXIT");
		return -EIO;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = i2c_get_functionality(client->adapter);
		dev_err(&client->dev, "i2c_check_functionality error, functionality=0x%x", err);
		return -EIO;
	}

	snprintf(chip_info, sizeof(chip_info), "%s", "sensortek");

	/* kzalloc: allocate memory and set to zero. */
	stk = kzalloc(sizeof(struct stk_data), GFP_KERNEL);

	if (!stk) {
		dev_err(&client->dev, "memory allocation error");
		return -ENOMEM;
	}

	client->addr = 0x28;
	stk->client = client;

	stk->bops = stk5xxx_bus_ops;
	i2c_set_clientdata(client, stk);
	mutex_init(&stk->i2c_lock);

	if (get_platform_data(stk))
		goto err_free_mem;

	err = stk_get_pid(stk);
	if (err)
		goto err_free_mem;

	dev_info(&client->dev, "PID 0x%x", stk->pid);


	stk_data_initialize(stk);

#ifdef STK_INTERRUPT_MODE
	INIT_WORK(&stk->stk_work, stk_work_queue);

	if (gpio_request(stk->int_pin, "stk_sar_int")) {
		dev_err(&client->dev, "gpio_request failed");
		goto err_free_mem;
	}

	err = stk_irq_setup(stk);
	if (err < 0) {
		goto err_cancel_work_sync;
	}
#elif defined STK_POLLING_MODE
	INIT_WORK(&stk->stk_work, stk_work_queue);
	hrtimer_init(&stk->stk_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	stk->poll_delay = ns_to_ktime(STK_POLLING_TIME * NSEC_PER_USEC);/*ktime_set(1, 0);*/
	stk->stk_timer.function = stk_timer_func;
#endif /* STK_INTERRUPT_MODE, STK_POLLING_MODE */

	if (stk_reg_init(stk)) {
		dev_err(&client->dev, "stk5xxx initialization failed");
		goto err_exit;
	}
	if (stk_init_qualcomm(stk)) {
		dev_err(&client->dev, "stk_init_qualcomm failed");
		goto err_exit;
	}

	stk_sar_ptr = stk;
/*add class sysfs*/
	err = class_register(&sar_sensor_class);
	if (err < 0) {
		dev_info(&client->dev, "Create fsys class failed (%d)\n", err);
		return err;
	}

	err = class_create_file(&sar_sensor_class, &class_attr_delay);
	if (err < 0) {
		dev_info(&client->dev, "Create delay file failed (%d)\n", err);
		goto err_class_creat;
	}

	err = class_create_file(&sar_sensor_class, &class_attr_enable);
	if (err < 0) {
		dev_info(&client->dev, "Create enable file failed (%d)\n", err);
		goto err_class_creat;
	}

	err = class_create_file(&sar_sensor_class, &class_attr_chip_info);
	if (err < 0) {
		dev_info(&client->dev, "Create chip_info file failed (%d)\n", err);
		goto err_class_creat;
	}

	err = class_create_file(&sar_sensor_class, &class_attr_batch);
	if (err < 0) {
		dev_info(&client->dev, "Create batch file failed (%d)\n", err);
		goto err_class_creat;
	}

	err = class_create_file(&sar_sensor_class, &class_attr_flush);
	if (err < 0) {
		dev_info(&client->dev, "Create flush file failed (%d)\n", err);
		goto err_class_creat;
	}

	err = class_create_file(&sar_sensor_class, &class_attr_diff);
	if (err < 0) {
		dev_info(&client->dev, "Create diff file failed (%d)\n", err);
		goto err_class_creat;
	}


	err = class_create_file(&sar_sensor_class, &class_attr_status);
	if (err < 0) {
		dev_info(&client->dev, "Create status file failed (%d)\n", err);
		goto err_class_creat;
	}

	dev_info(&client->dev, "Success");
	return 0;

err_exit:
#ifdef STK_INTERRUPT_MODE
	stk_exit_irq_setup(stk);
err_cancel_work_sync:
	gpio_free(stk->int_pin);
	cancel_work_sync(&stk->stk_work);
#elif defined STK_POLLING_MODE
	hrtimer_try_to_cancel(&stk->stk_timer);
	cancel_work_sync(&stk->stk_work);
#endif /* STK_INTERRUPT_MODE, STK_POLLING_MODE */
err_free_mem:
	mutex_destroy(&stk->i2c_lock);
	kfree(stk);
	return err;
err_class_creat:
	dev_info(&client->dev, "unregister sar_sensor_class.\n");
	class_unregister(&sar_sensor_class);
	return err;
}

/*
 * @brief: Remove function for i2c_driver.
 *
 * @param[in] client: struct i2c_client *
 *
 * @return: 0
 */
int stk_i2c_remove(struct i2c_client *client)
{
	struct stk_data *stk = i2c_get_clientdata(client);

	stk_exit_qualcomm(stk);
#ifdef STK_INTERRUPT_MODE
	stk_exit_irq_setup(stk);
	gpio_free(stk->int_pin);
	cancel_work_sync(&stk->stk_work);
#elif defined STK_POLLING_MODE
	hrtimer_try_to_cancel(&stk->stk_timer);
	cancel_work_sync(&stk->stk_work);
#endif /* STK_INTERRUPT_MODE, STK_POLLING_MODE */
	mutex_destroy(&stk->i2c_lock);
	kfree(stk);
	return 0;
}

static int32_t __init stk5xxx_init(void)
{
	return i2c_add_driver(&stk5xxx_i2c_driver);
}

static void __exit stk5xxx_exit(void)
{
	return i2c_del_driver(&stk5xxx_i2c_driver);
}

late_initcall(stk5xxx_init);
module_exit(stk5xxx_exit);

MODULE_AUTHOR("Sensortek");
MODULE_DESCRIPTION("stk5xxx sar driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(STK_QUALCOMM_VERSION);
