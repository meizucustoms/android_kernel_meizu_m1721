/*
 * Copyright (C) 2020 MeizuCustoms enthusiasts
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */ 

#include <linux/module.h>
#include <linux/export.h>
#include "aw36413.h"

static struct i2c_adapter *aw36413_dev_1;
static struct i2c_adapter *aw36413_dev_2;
static int msm_flash_en;
static int aw36413_vendor_id;
static int aw36413_probe_done;

static int32_t msm_led_aw36413_i2c_trigger_get_subdev_id(struct msm_led_flash_ctrl_t *fctrl, void *arg);
static int32_t msm_led_aw36413_i2c_trigger_config(struct msm_led_flash_ctrl_t *fctrl, void *data);
static int msm_flash_aw36413_led_off(struct msm_led_flash_ctrl_t *fctrl);
static int msm_flash_aw36413_led_init(struct msm_led_flash_ctrl_t *fctrl);
static int msm_flash_aw36413_led_release(struct msm_led_flash_ctrl_t *fctrl);
static int msm_flash_aw36413_led_high(struct msm_led_flash_ctrl_t *fctrl);
static int msm_flash_aw36413_led_low(struct msm_led_flash_ctrl_t *fctrl);

static struct msm_flash_fn_t aw36413_func_tbl = {
    .flash_get_subdev_id = msm_led_aw36413_i2c_trigger_get_subdev_id,
	.flash_led_config = msm_led_aw36413_i2c_trigger_config,
	.flash_led_init = msm_aw36413_flash_led_init,
	.flash_led_release = msm_aw36413_flash_led_release,
	.flash_led_off = msm_aw36413_flash_led_off,
	.flash_led_low = msm_aw36413_flash_led_low,
	.flash_led_high = msm_aw36413_flash_led_high,
};

static struct msm_camera_i2c_client aw36413_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
};

static struct msm_led_flash_ctrl_t fctrl = {
    .flash_i2c_client = aw36413_i2c_client,
    .func_tbl = aw36413_func_tbl,
};

static struct i2c_device_id aw36413_device_id {
    { .name = "awinic,aw36413" },
    { }
};

static const struct of_device_id aw36413_i2c_trigger_dt_match[] = {
	{ .compatible = "awinic,aw36413" },
	{ }
};

MODULE_DEVICE_TABLE(of, aw36413_trigger_dt_match);

static unsigned char aw36413_read_reg(unsigned int reg, int mode) {
    byte data;
    int ret;
    char *device;
    
    // Set what aw36413 we will use...
    switch (mode) {
    case AW36413_FIRST:
        aw_info("read reg from first aw36413...\n");
        device = "first";
        fctrl.flash_i2c_client->client->adapter = aw36413_dev_1; // set first aw36413 as i2c client
        ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_read_seq(fctrl.flash_i2c_client,
                                                            reg, &data,
                                                            MSM_CAMERA_I2C_BYTE_DATA);
        break;
    case AW36413_SECOND:
        aw_info("read reg from second aw36413...\n");
        device = "second";
        fctrl.flash_i2c_client->client->adapter = aw36413_dev_2; // set second aw36413 as i2c client
        ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_read_seq(fctrl.flash_i2c_client,
                                                            reg, &data,
                                                            MSM_CAMERA_I2C_BYTE_DATA);
        break;
    case AW36413_DOUBLE:
        aw_err("double read not supported!\n");
        ret = -EINVAL;
        break;
    default:
        aw_err("invalid mode %d!\n", mode);
        ret = -EINVAL;
        break;
    }
    
    if (ret < 0) {
        aw_err("read error! ret = %d\n", ret);
    } else {
        aw_info("read successful, data: 0x%02x, reg: 0x%02x, device: %s\n", 
                            (unsigned int)data, reg, device);
    }
    
    return (unsigned char)data;
}

static int aw36413_write_reg(unsigned int reg, unsigned int val, int mode) {
    int ret;
    char *device;
    
    // Set what aw36413 we will use...
    switch (mode) {
    case AW36413_FIRST:
        aw_info("write reg to first aw36413...\n");
        device = "first";
        fctrl.flash_i2c_client->client->adapter = aw36413_dev_1; // set first aw36413 as i2c client
        ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_write(fctrl.flash_i2c_client,
                                                            reg, val,
                                                            MSM_CAMERA_I2C_BYTE_DATA);
        break;
    case AW36413_SECOND:
        aw_info("write reg to second aw36413...\n");
        device = "second";
        fctrl.flash_i2c_client->client->adapter = aw36413_dev_2; // set second aw36413 as i2c client
        ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_write(fctrl.flash_i2c_client, reg, val,
                                                            MSM_CAMERA_I2C_BYTE_DATA);
        break;
    case AW36413_DOUBLE:
        aw_err("write reg to both aw36413...\n");
        device = "double";
        fctrl.flash_i2c_client->client->adapter = aw36413_dev_1; // set first aw36413 as i2c client
        ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_write(fctrl.flash_i2c_client, reg, val,
                                                            MSM_CAMERA_I2C_BYTE_DATA);
        if (ret < 0) { // check both write operations for error
            aw_err("write error! ret = %d\n", ret);
            return ret;
        }
        fctrl.flash_i2c_client->client->adapter = aw36413_dev_2; // set second aw36413 as i2c client
        ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_write(fctrl.flash_i2c_client, reg, val,
                                                            MSM_CAMERA_I2C_BYTE_DATA);
        break;
    default:
        aw_err("invalid mode %d!\n", mode);
        ret = -EINVAL;
        break;
    }
    
    if (ret < 0) {
        aw_err("write error! ret = %d\n", ret);
        return ret;
    } else {
        aw_info("write successful, val: 0x%02x, reg: 0x%02x, device: %s\n", 
                            val, reg, device);
    }
    
    return ret;
}

static int aw36413_gpio_set(int val, int mode) {
    char *device;
    
    switch (mode) {
    case AW36413_FIRST:
        device = "first";
        gpio_set_value_cansleep(power_info->gpio_conf->gpio_num_info->gpio_num[7]);
        break;
    case AW36413_SECOND:
        device = "second";
        gpio_set_value_cansleep(power_info->gpio_conf->gpio_num_info->gpio_num[8]);
        break;
    case AW36413_DOUBLE:
        device = "double";
        gpio_set_value_cansleep(power_info->gpio_conf->gpio_num_info->gpio_num[7]);
        gpio_set_value_cansleep(power_info->gpio_conf->gpio_num_info->gpio_num[8]);
        break;
    default:
        aw_err("invalid mode!\n");
        return -EINVAL;
        break;
    }
    
    aw_info("GPIO set success, value: 0x%02x, device: %s\n", val, device);
    return 0;
}

static void aw36413_get_vendor_id() {
    aw36413_gpio_set(0, AW36413_FIRST);
    msleep(2);
    aw36413_gpio_set(2, AW36413_FIRST);
    msleep(2);
    
    if (aw36413_read_reg(REG_AW36413_DEVICE_ID, AW36413_FIRST) != 28) {
        fctrl.flash_i2c_client->client->addr = 214;
        aw36413_vendor_id = 0;
    } else {
        fctrl.flash_i2c_client->client->addr = 198;
        aw36413_vendor_id = 1;
    }
    
    aw36413_gpio_set(0, AW36413_FIRST);
}

static int msm_flash_aw36413_i2c_probe(struct i2c_client *client,
                                       struct i2c_device_id *id) {
    int ret = 0;
    
    aw_info("enter\n");
    if (!id) {
        id = aw36413_device_id;
        aw_warn("id not found, was set default.\n");
    }
    
    aw36413_dev_1 = i2c_get_adapter(6);
    aw36413_dev_2 = i2c_get_adapter(5);
    if (!aw36413_dev_1) aw_err("first aw36413 not found\n");
    if (!aw36413_dev_2) aw_err("second aw36413 not found\n");
    
    msm_flash_i2c_probe(client, id);
    aw_info("i2c probe done\n");
    ret = msm_camera_request_gpio_table(fctrl.flashdata->power_info.gpio_conf->cam_gpio_req_tbl,
                                fctrl.flashdata->power_info.gpio_conf->cam_gpio_req_tbl_size, 1);
    if (ret) {
        aw_err("request GPIO failed\n");
        return ret;
    }
    
    if (fctrl.pinctrl_info.use_pinctrl != false) {
        aw_info("set pinctrl state to active\n");
        ret = pinctrl_select_state(fctrl.pinctrl_info.pinctrl, 
                        fctrl.pinctrl_info.gpio_state_active);
        if (ret) {
            aw_err("error during pinctrl state changing\n");
            return ret;
        }
    }
    
    msm_led_torch_brightness_set(&msm_torch_led, 0);
    aw36413_get_vendor_id();
    aw_info("done\n");
    aw36413_probe_done = 1;
    return ret;
}

static int msm_flash_aw36413_i2c_remove(struct i2c_client *client,
                                        struct i2c_device_id *id) {
    int ret = 0;
    
    aw_info("enter\n");
    ret = msm_camera_request_gpio_table(fctrl.flashdata->power_info.gpio_conf->cam_gpio_req_tbl,
                                fctrl.flashdata->power_info.gpio_conf->cam_gpio_req_tbl_size, 0);
    if (ret) {
        aw_err("request GPIO failed\n");
        return ret;
    }
    
    if (fctrl.pinctrl_info.use_pinctrl != false) {
        aw_info("set pinctrl state to suspend\n");
        ret = pinctrl_select_state(fctrl.pinctrl_info.pinctrl, 
                        fctrl.pinctrl_info.gpio_state_suspend);
        if (ret) {
            aw_err("error during pinctrl state changing\n");
            return ret;
        }
    }
    aw_info("done\n");
    return ret;
}

static struct i2c_driver aw36413_i2c_driver = {
	.id_table = aw36413_i2c_id,
	.probe  = msm_flash_aw36413_i2c_probe,
	.remove = msm_flash_aw36413_i2c_remove,
	.driver = {
		.name = "awinic,aw36413",
		.owner = THIS_MODULE,
		.of_match_table = aw36413_i2c_trigger_dt_match,
	},
};

static int msm_flash_aw36413_led_init(struct msm_led_flash_ctrl_t *fctrl) {
    aw_info("enter\n");
    aw36413_gpio_set(0, AW36413_DOUBLE);
    msleep(2);
    aw36413_gpio_set(2, AW36413_DOUBLE);
    msleep(8);
    aw36413_write_reg(REG_AW36413_ENABLE, 0, AW36413_DOUBLE);
    msm_flash_en = 1;
    return 0;
}

static int msm_flash_aw36413_led_release(struct msm_led_flash_ctrl_t *fctrl) {
    aw_info("enter\n");
    if (!fctrl) || (!fctrl->flashdata) {
        aw_err("fctrl not found\n");
        return -EINVAL;
    }
    
    aw36413_gpio_set(0, AW36413_DOUBLE);
    msleep(8);
    msm_flash_en = 0;
    return 0;
}

static int msm_flash_aw36413_led_off(struct msm_led_flash_ctrl_t *fctrl) {
    aw_info("enter\n");
    if (!fctrl) || (!fctrl->flashdata) {
        aw_err("fctrl not found\n");
        return -EINVAL;
    }
    
    if (aw36413_write_reg(REG_AW36413_ENABLE, 0, AW36413_FIRST)) {
        aw36413_gpio_set(2, AW36413_FIRST);
        msleep(2);
        aw36413_write_reg(REG_AW36413_ENABLE, 0, AW36413_FIRST);
    }
    
    if (aw36413_write_reg(REG_AW36413_ENABLE, 0, AW36413_SECOND)) {
        aw36413_gpio_set(2, AW36413_SECOND);
        msleep(2);
        aw36413_write_reg(REG_AW36413_ENABLE, 0, AW36413_SECOND);
    }
    
    return 0;
}

static int msm_flash_aw36413_led_high(struct msm_led_flash_ctrl_t *fctrl) {
    int torch_op_current_0, torch_op_current_1;
    int reg_current_0, reg_current_1;
    int temp_led1_brightness, temp_led2_brightness;
    int led1_brightness, led2_brightness, enable;
    
    aw_info("enter\n");
    if (!fctrl) || (!fctrl->flashdata) {
        aw_err("fctrl not found\n");
        return -EINVAL;
    }
    
    if (fctrl->cci_i2c_master) {
        aw_info("cci_master = %d\n", fctrl->cci_i2c_master);
    }
    
    flash_op_current_0 = fcrtl->flash_op_current[0];
    flash_op_current_1 = fcrtl->flash_op_current[1];
    reg_current_0 = 1000;
    if (flash_op_current_0 <= 3000)
    {
        if (flash_op_current_0 > 1500)
            reg_current_0 = 1500;
        else
            reg_current_0 = flash_op_current_0;
    }
    reg_current_1 = 1000;
    if (flash_op_current_1 <= 3000)
    {
        if (flash_op_current_1 > 1500)
            reg_current_1 = 1500;
        else
            reg_current_1 = flash_op_current_1;
    }
    
    if (msm_flash_en) {
        aw36413_gpio_set(2, AW36413_DOUBLE);
        msleep(2);
        temp_led1_brightness = (100 * reg_current_0 + 1170) / 2344;
        temp_led2_brightness = (100 * reg_current_1 + 1170) / 2344;
        if (temp_led1_brightness)
            led1_brightness = temp_led1_brightness - 1;
        else
            led1_brightness = 0;
        
        aw36413_write_reg(REG_AW36413_LED1_FLASH, led1_brightness, AW36413_DOUBLE);
        
        if (temp_led2_brightness)
            led2_brightness = temp_led2_brightness - 1;
        else
            led2_brightness = 0;
        
        aw36413_write_reg(REG_AW36413_LED2_FLASH, led2_brightness, AW36413_DOUBLE);
        aw36413_write_reg(REG_AW36413_TIMING, AW36413_TIMING_COUNT, AW36413_DOUBLE);
        
        if (temp_led1_brightness)
            enable = 15;
        else
            enable = 14;
        
        if (!temp_led2_brightness)
            enable &= 253;
        
        aw36413_write_reg(REG_AW36413_ENABLE, enable, AW36413_DOUBLE);
    }
    return 0;
}

static int msm_flash_aw36413_led_low(struct msm_led_flash_ctrl_t *fctrl) {
    int torch_op_current_0, torch_op_current_1;
    int reg_current_0, reg_current_1;
    int temp_led1_brightness, temp_led2_brightness;
    int led1_brightness, led2_brightness, enable;
    
    aw_info("enter\n");
    if (!fctrl) || (!fctrl->flashdata) {
        aw_err("fctrl not found\n");
        return -EINVAL;
    }
    
    if (fctrl->cci_i2c_master) {
        aw_info("cci_master = %d\n", fctrl->cci_i2c_master);
    }
    
    torch_op_current_0 = fcrtl->flash_op_current[0];
    torch_op_current_1 = fcrtl->flash_op_current[1];
    if ( torch_op_current_0 <= 3000 ) {
        if ( torch_op_current_0 > 300 )
            reg_current_0 = 300;
        else
            reg_current_0 = torch_op_current_0;
    }
    reg_current_1 = 200;
    if ( torch_op_current_1 <= 3000 ) {
        if ( torch_op_current_1 > 300 )
            reg_current_1 = 300;
        else
            reg_current_1 = torch_op_current_1;
    }
    
    if (msm_flash_en) {
        aw36413_gpio_set(2, AW36413_DOUBLE);
        msleep(2);
        if (flash_vendor_id == 1) {
            temp_led1_brightness = (100 * reg_current_0 + 280) / 560;
            temp_led2_brightness = (100 * reg_current_1 + 280) / 560;
        } else {
            temp_led1_brightness = (100 * reg_current_0 + 291) / 582;
            temp_led2_brightness = (100 * reg_current_1 + 291) / 582;
        }
        if (temp_led1_brightness)
            led1_brightness = temp_led1_brightness - 1;
        else
            led1_brightness = 0;
        
        aw36413_write_reg(REG_AW36413_LED1_TORCH, led1_brightness, AW36413_DOUBLE);
        
        if (temp_led2_brightness)
            led2_brightness = temp_led2_brightness - 1;
        else
            led2_brightness = 0;
        
        aw36413_write_reg(REG_AW36413_LED2_TORCH, led2_brightness, AW36413_DOUBLE);
        aw36413_write_reg(REG_AW36413_TIMING, AW36413_TIMING_COUNT, AW36413_DOUBLE);
        
        if (temp_led1_brightness)
            enable = 11;
        else
            enable = 10;
        if (!temp_led2_brightness)
            enable &= 253;
        
        aw36413_write_reg(REG_AW36413_ENABLE, enable, AW36413_DOUBLE);
    }
    return 0;
}

static int32_t msm_led_aw36413_i2c_trigger_get_subdev_id(struct msm_led_flash_ctrl_t *fctrl, void *arg) {
    aw_info("enter\n");
    
    if (!arg) {
        aw_err("arg not found\n");
        return -EINVAL;
    }
    
    arg = fctrl->subdev_id;
    aw_info("subdev id = %d\n", fctrl->subdev_id);
    return 0;
}

static int32_t msm_led_aw36413_i2c_trigger_config(struct msm_led_flash_ctrl_t *fctrl, void *data) {
    int ret;
    int *vdata = (int *)data;
    
    aw_info("enter");
    if (!fctrl->func_tbl) {
        aw_err("no function table\n");
        return -EINVAL;
    }
    
    switch (*vdata) {
    case MSM_CAMERA_LED_INIT:
        if (fctrl->func_tbl->flash_led_init)
            ret = fctrl->func_tbl->flash_led_init(fctrl);
        break;
    case MSM_CAMERA_LED_RELEASE:
        if (fctrl->func_tbl->flash_led_release)
            ret = fctrl->func_tbl->flash_led_release(fctrl);
        break;
    case MSM_CAMERA_LED_OFF:
        if (fctrl->func_tbl->flash_led_off)
            ret = fctrl->func_tbl->flash_led_off(fctrl);
        break;
    case MSM_CAMERA_LED_LOW:
        fctrl->flash_op_current[0] = vdata[1];
        fctrl->flash_op_current[1] = vdata[2];
        aw_info("[LED1 | %d mA] [LED2 | %d mA] [LED3 | %d mA] [LED4 | %d mA]\n", fctrl->flash_op_current[0],
                         fctrl->flash_op_current[0], fctrl->flash_op_current[1], fctrl->flash_op_current[1]);
        aw_info("[LED1, LED2 | max %d mA] [LED3, LED4 | max %d mA]", fctrl->flash_max_current[0],
                                                                     fctrl->flash_max_current[1]);
        if (fctrl->func_tbl->flash_led_low)
            ret = fctrl->func_tbl->flash_led_low(fctrl);
        break;
    case MSM_CAMERA_LED_HIGH:
        fctrl->flash_op_current[0] = vdata[1];
        fctrl->flash_op_current[1] = vdata[2];
        aw_info("[LED1 | %d mA] [LED2 | %d mA] [LED3 | %d mA] [LED4 | %d mA]\n", fctrl->flash_op_current[0],
                         fctrl->flash_op_current[0], fctrl->flash_op_current[1], fctrl->flash_op_current[1]);
        aw_info("[LED1, LED2 | max %d mA] [LED3, LED4 | max %d mA]", fctrl->flash_max_current[0],
                                                                     fctrl->flash_max_current[1]);
        if (fctrl->func_tbl->flash_led_high)
            ret = fctrl->func_tbl->flash_led_high(fctrl);
        break;
    default:
        aw_warn("invalid vdata = %d\n", vdata);
        ret = -EFAULT;
        break;
    }
    
    return ret;
}

static int __init msm_flash_aw36413_init(void)
{
	int32_t rc = 0;
    rc = i2c_add_driver(&aw36413_i2c_driver);
    if (!rc)
        aw_info("done");
	return rc;
}

static void __exit msm_flash_aw36413_exit(void)
{
    i2c_del_driver(&aw36413_i2c_driver);
}

module_init(msm_flash_aw36413_init_module);
module_exit(msm_flash_aw36413_exit_module);
MODULE_DESCRIPTION("AW36413 (x2) LED driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Roman Rihter <teledurak@msucks.space>");
