/*
 * Copyright (C) 2020 MeizuCustoms enthusiasts
 * 
 * Part of the MeizuSucks project
 * v2.0
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
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include "aw36413.h"
#include "../../common/msm_camera_io_util.h"

static struct msm_led_flash_ctrl_t fctrl;

static const struct i2c_device_id aw36413_i2c_id[] = {
	{ "awinic,aw36413", (kernel_ulong_t)&fctrl },
	{ },
};
static const struct of_device_id aw36413_i2c_trigger_dt_match[] = {
	{ .compatible = "awinic,aw36413" },
	{ },
};
MODULE_DEVICE_TABLE(of, aw36413_trigger_dt_match);

static struct aw36413_cfg *aw36413 = NULL;

static int aw36413_reg_write(int device, unsigned int reg, unsigned int val) {
	unsigned int ret;
	
	if (!fctrl.flash_i2c_client->i2c_func_tbl) {
		aw_err("I2C functions are not defined!\n");
		return -EINVAL;
	}
	
	switch (device) {
	case AW36413_FIRST:
		fctrl.flash_i2c_client->client->adapter = aw36413->a1;
		ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_write(fctrl.flash_i2c_client,
																 reg, val, MSM_CAMERA_I2C_BYTE_DATA);
		break;
	case AW36413_SECOND:
		fctrl.flash_i2c_client->client->adapter = aw36413->a2;
		ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_write(fctrl.flash_i2c_client,
																 reg, val, MSM_CAMERA_I2C_BYTE_DATA);
		break;
	case AW36413_BOTH:
		fctrl.flash_i2c_client->client->adapter = aw36413->a1;
		ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_write(fctrl.flash_i2c_client,
																 reg, val, MSM_CAMERA_I2C_BYTE_DATA);
		if (ret < 0) {
			aw_err("error: %d, device: %d, reg: 0x%02x, val: 0x%02x\n", ret, device, reg, val);
			return ret;
		}
		fctrl.flash_i2c_client->client->adapter = aw36413->a2;
		ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_write(fctrl.flash_i2c_client,
																 reg, val, MSM_CAMERA_I2C_BYTE_DATA);
		break;
	default:
		aw_err("Invalid device %d. Please use aw36413_ops values.\n", device);
		return -EINVAL;
		break;
	}
	
	if (ret < 0) {
		aw_err("error: %d, device: %d, reg: 0x%02x, val: 0x%02x\n", ret, device, reg, val);
		return ret;
	} else {
		aw_info("done %d, device: %d, reg: 0x%02x, val: 0x%02x\n", ret, device, reg, val);
	}
	
	return 0;
}

static unsigned int aw36413_reg_read(int device, unsigned int reg) {
	unsigned int ret;
	uint8_t val;
	
	if (!fctrl.flash_i2c_client->i2c_func_tbl) {
		aw_err("I2C functions are not defined!\n");
		return -EINVAL;
	}
	
	switch (device) {
	case AW36413_FIRST:
		fctrl.flash_i2c_client->client->adapter = aw36413->a1;
		ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_read_seq(fctrl.flash_i2c_client,
																 reg, &val, MSM_CAMERA_I2C_BYTE_DATA);
		break;
	case AW36413_SECOND:
		fctrl.flash_i2c_client->client->adapter = aw36413->a2;
		ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_read_seq(fctrl.flash_i2c_client,
																 reg, &val, MSM_CAMERA_I2C_BYTE_DATA);
		break;
	case AW36413_BOTH:
		aw_err("both mode not supported\n");
		break;
	default:
		aw_err("Invalid device %d. Please use aw36413_ops values.\n", device);
		return -EINVAL;
		break;
	}
	
	if (ret < 0) {
		aw_err("error: %d, device: %d, reg: 0x%02x, val: 0x%02x\n", ret, device, reg, val);
		return ret;
	} else {
		aw_info("done %d, device: %d, reg: 0x%02x, val: 0x%02x\n", ret, device, reg, val);
	}
	
	return val;
}

static void aw36413_hwen(int device, int state) {
	switch (device) {
	case AW36413_FIRST:
		if (gpio_is_valid(aw36413->hwen1)) {
			gpio_direction_output(aw36413->hwen1, state);
		} else {
			aw_err("HWEN1 invalid!\n");
			return;
		}
		break;
	case AW36413_SECOND:
		if (gpio_is_valid(aw36413->hwen1)) {
			gpio_direction_output(aw36413->hwen2, state);
		} else {
			aw_err("HWEN2 invalid!\n");
			return;
		}
		break;
	case AW36413_BOTH:
		if (gpio_is_valid(aw36413->hwen1)) {
			gpio_direction_output(aw36413->hwen1, state);
		} else {
			aw_err("HWEN1 invalid!\n");
			return;
		}
		if (gpio_is_valid(aw36413->hwen2)) {
			gpio_direction_output(aw36413->hwen2, state);
		} else {
			aw_err("HWEN1 invalid!\n");
			return;
		}
		break;
	default:
		aw_err("Invalid device %d. Please use aw36413_ops values.\n", device);
		return;
		break;
	}
	
	aw36413->hwenpwr = 1;
	if (state == AW36413_HWEN_OFF)
		aw36413->hwenpwr = 0;
	
	aw_info("Success, device: %d, state: %d\n", device, state);
	return;
}

static int aw36413_gpio_setup(void) {
	int ret = 0;
	
	if (!aw36413) {
		aw_err("main structure not found!\n");
		return -EINVAL;
	}
	
	if (gpio_is_valid(aw36413->hwen1)) {
		ret = gpio_request(aw36413->hwen1, "aw36413_hwen1");
        if (ret) {
            aw_err("could not request HWEN1 GPIO %d! %d\n", aw36413->hwen1, ret);
            return ret;
        }
	} else {
		aw_err("HWEN1 GPIO is invalid!\n");
		return -EIO;
	}
	
	if (gpio_is_valid(aw36413->hwen2)) {
		ret = gpio_request(aw36413->hwen2, "aw36413_hwen2");
        if (ret) {
            aw_err("could not request HWEN2 GPIO %d! %d\n", aw36413->hwen2, ret);
            return ret;
        }
	} else {
		aw_err("HWEN2 GPIO is invalid!\n");
		return -EIO;
	}
	
	if (gpio_is_valid(aw36413->strobe)) {
		ret = gpio_request(aw36413->strobe, "aw36413_strobe");
        if (ret) {
            aw_err("could not request STROBE GPIO %d! %d\n", aw36413->strobe, ret);
            return ret;
        }
	} else {
		aw_err("STROBE GPIO is invalid!\n");
		return -EIO;
	}
	
	aw_info("STROBE: %d. HWEN1: %d, HWEN2: %d\n", aw36413->strobe, aw36413->hwen1, aw36413->hwen2);
	return 0;
}

static void aw36413_get_vendor_id(void) {
	int devid;
	aw36413_hwen(AW36413_FIRST, AW36413_HWEN_OFF);
	mdelay(10);
	aw36413_hwen(AW36413_FIRST, AW36413_HWEN_ON);
	mdelay(10);
	aw36413->vendor = 0;
	fctrl.flash_i2c_client->client->addr = 0xc6;
	devid = aw36413_reg_read(AW36413_FIRST, REG_AW36413_DEVICE_ID);
	aw_info("device id: 0x%02x\n", devid);
	if (aw36413_reg_read(AW36413_FIRST, REG_AW36413_DEVICE_ID) != 0x1c) {
		fctrl.flash_i2c_client->client->addr = 0xd6;
		aw36413->vendor = 1;
	}
	aw_info("done\n");
}

static void msm_led_torch_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness value) {
    if (value == LED_OFF) {
        fctrl.func_tbl->flash_led_off(&fctrl);
    } else {
        fctrl.flash_op_current[0] = value;
        fctrl.flash_op_current[1] = value;
        fctrl.func_tbl->flash_led_low(&fctrl);
    }
};

static struct led_classdev msm_torch_led = {
    .name		    = "torch-light",
    .brightness_set	= msm_led_torch_brightness_set,
    .brightness	    = LED_OFF,
};

static int aw36413_led_breath1(struct msm_led_flash_ctrl_t *fctrl) {
    int torch_op_current_0, torch_op_current_1;
    int reg_current_0, reg_current_1;
    int temp_led1_brightness, temp_led2_brightness;
    int led1_brightness, led2_brightness, enable;
	int i;
    
    aw_info("enter\n");
    if (!fctrl || !fctrl->flashdata) {
        aw_err("fctrl not found\n");
        return -EINVAL;
    }
    
    if (fctrl->cci_i2c_master) {
        aw_info("cci_master = %d\n", fctrl->cci_i2c_master);
    }
    
    torch_op_current_0 = fctrl->flash_op_current[0];
    torch_op_current_1 = fctrl->flash_op_current[1];
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
    
    if (aw36413->ready) {
        aw36413_hwen(AW36413_SECOND, AW36413_HWEN_ON);
        mdelay(10);
        if (aw36413->vendor == 1) {
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
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED1_TORCH, 0);
        
        if (temp_led2_brightness)
            led2_brightness = temp_led2_brightness - 1;
        else
            led2_brightness = 0;
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED2_TORCH, 0);
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_TIMING, AW36413_TIMING_COUNT);
        
        if (temp_led1_brightness)
            enable = 11;
        else
            enable = 10;
        if (!temp_led2_brightness)
            enable &= 253;
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_ENABLE, enable);
		
		for (i=0;i<48;i++) {
			aw_info("i=%d\n", i);
			aw36413_reg_write(AW36413_FIRST, REG_AW36413_LED2_TORCH, i);
			aw36413_reg_write(AW36413_SECOND, REG_AW36413_LED2_TORCH, i);
			mdelay(25);
		}
		
		for (i=48;i>0;i--) {
			aw_info("i=%d\n", i);
			aw36413_reg_write(AW36413_FIRST, REG_AW36413_LED2_TORCH, i);
			aw36413_reg_write(AW36413_SECOND, REG_AW36413_LED2_TORCH, i);
			mdelay(25);
		}
		
		for (i=0;i<48;i++) {
			aw_info("i=%d\n", i);
			aw36413_reg_write(AW36413_FIRST, REG_AW36413_LED1_TORCH, i);
			aw36413_reg_write(AW36413_SECOND, REG_AW36413_LED1_TORCH, i);
			mdelay(25);
		}
		
		for (i=48;i>0;i--) {
			aw_info("i=%d\n", i);
			aw36413_reg_write(AW36413_FIRST, REG_AW36413_LED1_TORCH, i);
			aw36413_reg_write(AW36413_SECOND, REG_AW36413_LED1_TORCH, i);
			mdelay(25);
		}
		
		aw36413_reg_write(AW36413_BOTH, REG_AW36413_ENABLE, 0);
    } else {
		aw_info("aw36413 not ready\n");
	}
    return 0;
}

static int aw36413_led_breath(struct msm_led_flash_ctrl_t *fctrl) {
    int torch_op_current_0, torch_op_current_1;
    int reg_current_0, reg_current_1;
    int temp_led1_brightness, temp_led2_brightness;
    int led1_brightness, led2_brightness, enable;
	int i;
    
    aw_info("enter\n");
    if (!fctrl || !fctrl->flashdata) {
        aw_err("fctrl not found\n");
        return -EINVAL;
    }
    
    if (fctrl->cci_i2c_master) {
        aw_info("cci_master = %d\n", fctrl->cci_i2c_master);
    }
    
    torch_op_current_0 = fctrl->flash_op_current[0];
    torch_op_current_1 = fctrl->flash_op_current[1];
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
    
    if (aw36413->ready) {
        aw36413_hwen(AW36413_SECOND, AW36413_HWEN_ON);
        mdelay(10);
        if (aw36413->vendor == 1) {
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
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED1_TORCH, 0);
        
        if (temp_led2_brightness)
            led2_brightness = temp_led2_brightness - 1;
        else
            led2_brightness = 0;
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED2_TORCH, 0);
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_TIMING, AW36413_TIMING_COUNT);
        
        if (temp_led1_brightness)
            enable = 11;
        else
            enable = 10;
        if (!temp_led2_brightness)
            enable &= 253;
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_ENABLE, enable);
		
		for (i=0;i<48;i++) {
			aw_info("i=%d\n", i);
			aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED1_TORCH, i);
			aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED2_TORCH, i);
			mdelay(25);
		}
		
		for (i=48;i>0;i--) {
			aw_info("i=%d\n", i);
			aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED1_TORCH, i);
			aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED2_TORCH, i);
			mdelay(25);
		}
		
		for (i=0;i<48;i++) {
			aw_info("i=%d\n", i);
			aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED1_TORCH, i);
			aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED2_TORCH, i);
			mdelay(25);
		}
		
		for (i=48;i>0;i--) {
			aw_info("i=%d\n", i);
			aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED1_TORCH, i);
			aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED2_TORCH, i);
			mdelay(25);
		}
		
		aw36413_reg_write(AW36413_BOTH, REG_AW36413_ENABLE, 0);
    } else {
		aw_info("aw36413 not ready\n");
	}
    return 0;
}

static ssize_t aw36413_proc_write(struct file *filp, const char __user *buff,
    size_t len, loff_t *data) {
	aw_info("Enable LED\n");
	fctrl.func_tbl->flash_led_init(&fctrl);
	fctrl.flash_op_current[0] = 1500;
	fctrl.flash_op_current[1] = 1500;
	fctrl.func_tbl->flash_led_low(&fctrl);
	msleep(5000);
	fctrl.func_tbl->flash_led_off(&fctrl);
	aw_info("Enable LED done\n");
	return len;
}

static const struct file_operations aw36413_fops = {
    .owner          = THIS_MODULE,
    .write          = aw36413_proc_write,
};

static ssize_t aw36413_proc_write2(struct file *filp, const char __user *buff,
    size_t len, loff_t *data) {
	aw_info("Enable LED\n");
	fctrl.func_tbl->flash_led_init(&fctrl);
	fctrl.flash_op_current[0] = 300;
	fctrl.flash_op_current[1] = 300;
	aw36413_led_breath(&fctrl);
	fctrl.func_tbl->flash_led_off(&fctrl);
	aw_info("Enable LED done\n");
	return len;
}

static const struct file_operations aw36413_fops2 = {
    .owner          = THIS_MODULE,
    .write          = aw36413_proc_write2,
};

static ssize_t aw36413_proc_write3(struct file *filp, const char __user *buff,
    size_t len, loff_t *data) {
	aw_info("Enable LED\n");
	fctrl.func_tbl->flash_led_init(&fctrl);
	fctrl.flash_op_current[0] = 300;
	fctrl.flash_op_current[1] = 300;
	aw36413_led_breath1(&fctrl);
	fctrl.func_tbl->flash_led_off(&fctrl);
	aw_info("Enable LED done\n");
	return len;
}

static const struct file_operations aw36413_fops3 = {
    .owner          = THIS_MODULE,
    .write          = aw36413_proc_write3,
};

static int msm_flash_aw36413_i2c_probe(struct i2c_client *client,
        const struct i2c_device_id *id) {
	int ret;
	struct proc_dir_entry *proc_entry;
	
	aw36413 = kzalloc(sizeof(struct aw36413_cfg *), GFP_KERNEL);
	aw36413->id = aw36413_i2c_id;
	aw36413->hwen1 = 95;
	aw36413->hwen2 = 93;
	aw36413->strobe = 96;
	aw36413->a1 = i2c_get_adapter(6);
	aw36413->a2 = i2c_get_adapter(5);
	client->adapter = aw36413->a1;
	
	if (!id) id = aw36413->id;
	if (!aw36413->a1) {
		aw_err("no a1 found.\n");
		return -EINVAL;
	}
	if (!aw36413->a2) {
		aw_err("no a2 found.\n");
		return -EINVAL;
	}
	
	msm_flash_i2c_probe(client, id);
    ret = aw36413_gpio_setup();
	if (ret) {
		aw_err("GPIO validating error\n");
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
    
    aw36413_get_vendor_id();
	msm_led_torch_brightness_set(&msm_torch_led, LED_OFF);
    ret = led_classdev_register(&client->dev, &msm_torch_led);
    if (ret) {
        aw_err("Failed to register LED classdev. ret = %d\n", ret);
		gpio_free(aw36413->hwen1);
		gpio_free(aw36413->hwen2);
		gpio_free(aw36413->strobe);
		return ret;
    }
    
    proc_entry = proc_create_data("aw36413_switch", 0660, NULL, &aw36413_fops, NULL);
	if (!proc_entry) {
		aw_err("proc entry create error!\n");
	}
	proc_entry = proc_create_data("aw36413_effect1", 0660, NULL, &aw36413_fops2, NULL);
	if (!proc_entry) {
		aw_err("proc entry create error!\n");
	}
	proc_entry = proc_create_data("aw36413_effect2", 0660, NULL, &aw36413_fops3, NULL);
	if (!proc_entry) {
		aw_err("proc entry create error!\n");
	}
    
	aw_info("done\n");
	aw36413->probed = 1;
	return ret;
}

static int msm_flash_aw36413_i2c_remove(struct i2c_client *client) {
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
    gpio_free(aw36413->hwen1);
	gpio_free(aw36413->hwen2);
	gpio_free(aw36413->strobe);
    aw_info("done\n");
	aw36413->probed = 0;
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
    aw36413_hwen(AW36413_BOTH, AW36413_HWEN_OFF);
    mdelay(10);
    aw36413_hwen(AW36413_BOTH, AW36413_HWEN_ON);
    mdelay(10);
    aw36413_reg_write(AW36413_BOTH, REG_AW36413_ENABLE, 0);
    aw36413->ready = 1;
    return 0;
}

static int msm_flash_aw36413_led_release(struct msm_led_flash_ctrl_t *fctrl) {
    aw_info("enter\n");
    if (!fctrl || !fctrl->flashdata) {
        aw_err("fctrl not found\n");
        return -EINVAL;
    }
    
    aw36413_hwen(AW36413_BOTH, AW36413_HWEN_OFF);
    mdelay(10);
    aw36413->ready = 0;
    return 0;
}

static int msm_flash_aw36413_led_off(struct msm_led_flash_ctrl_t *fctrl) {
    int ret;
	
	aw_info("enter\n");
    if (!fctrl || !fctrl->flashdata) {
        aw_err("fctrl not found\n");
        return -EINVAL;
    }
    
    if (aw36413_reg_write(AW36413_FIRST, REG_AW36413_ENABLE, 0)) {
        aw36413_hwen(AW36413_FIRST, AW36413_HWEN_ON);
        mdelay(10);
        ret = aw36413_reg_write(AW36413_FIRST, REG_AW36413_ENABLE, 0);
		if (ret) {
			aw_err("failed to write reg\n");
			return ret;
		}
    }
    
    if (aw36413_reg_write(AW36413_SECOND, REG_AW36413_ENABLE, 0)) {
        aw36413_hwen(AW36413_SECOND, AW36413_HWEN_ON);
        mdelay(10);
        ret = aw36413_reg_write(AW36413_SECOND, REG_AW36413_ENABLE, 0);
		if (ret) {
			aw_err("failed to write reg\n");
			return ret;
		}
    }
    
    return 0;
}

static int msm_flash_aw36413_led_high(struct msm_led_flash_ctrl_t *fctrl) {
    int flash_op_current_0, flash_op_current_1;
    int reg_current_0, reg_current_1;
    int temp_led1_brightness, temp_led2_brightness;
    int led1_brightness, led2_brightness, enable;
    
    aw_info("enter\n");
    if (!fctrl || !fctrl->flashdata) {
        aw_err("fctrl not found\n");
        return -EINVAL;
    }
    
    if (fctrl->cci_i2c_master) {
        aw_info("cci_master = %d\n", fctrl->cci_i2c_master);
    }
    
    flash_op_current_0 = fctrl->flash_op_current[0];
    flash_op_current_1 = fctrl->flash_op_current[1];
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
    
    if (aw36413->ready) {
        aw36413_hwen(AW36413_SECOND, AW36413_HWEN_ON);
        mdelay(10);
        temp_led1_brightness = (100 * reg_current_0 + 1170) / 2344;
        temp_led2_brightness = (100 * reg_current_1 + 1170) / 2344;
        if (temp_led1_brightness)
            led1_brightness = temp_led1_brightness - 1;
        else
            led1_brightness = 0;
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED1_FLASH, led1_brightness);
        
        if (temp_led2_brightness)
            led2_brightness = temp_led2_brightness - 1;
        else
            led2_brightness = 0;
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED2_FLASH, led2_brightness);
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_TIMING, AW36413_TIMING_COUNT);
        
        if (temp_led1_brightness)
            enable = 15;
        else
            enable = 14;
        
        if (!temp_led2_brightness)
            enable &= 253;
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_ENABLE, enable);
    } else {
		aw_info("aw36413 not ready\n");
	}
    return 0;
}

static int msm_flash_aw36413_led_low(struct msm_led_flash_ctrl_t *fctrl) {
    int torch_op_current_0, torch_op_current_1;
    int reg_current_0, reg_current_1;
    int temp_led1_brightness, temp_led2_brightness;
    int led1_brightness, led2_brightness, enable;
    
    aw_info("enter\n");
    if (!fctrl || !fctrl->flashdata) {
        aw_err("fctrl not found\n");
        return -EINVAL;
    }
    
    if (fctrl->cci_i2c_master) {
        aw_info("cci_master = %d\n", fctrl->cci_i2c_master);
    }
    
    torch_op_current_0 = fctrl->flash_op_current[0];
    torch_op_current_1 = fctrl->flash_op_current[1];
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
    
    if (aw36413->ready) {
        aw36413_hwen(AW36413_SECOND, AW36413_HWEN_ON);
        mdelay(10);
        if (aw36413->vendor == 1) {
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
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED1_TORCH, led1_brightness);
        
        if (temp_led2_brightness)
            led2_brightness = temp_led2_brightness - 1;
        else
            led2_brightness = 0;
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_LED2_TORCH, led2_brightness);
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_TIMING, AW36413_TIMING_COUNT);
        
        if (temp_led1_brightness)
            enable = 11;
        else
            enable = 10;
        if (!temp_led2_brightness)
            enable &= 253;
        
        aw36413_reg_write(AW36413_BOTH, REG_AW36413_ENABLE, enable);
    } else {
		aw_info("aw36413 not ready\n");
	}
    return 0;
}

static int32_t msm_led_aw36413_i2c_trigger_get_subdev_id(struct msm_led_flash_ctrl_t *fctrl, void *arg) {
    aw_info("enter\n");
    
    if (!arg) {
        aw_err("arg not found\n");
        return -EINVAL;
    }
    
    arg = &fctrl->subdev_id;
    aw_info("subdev id = %d\n", fctrl->subdev_id);
    return 0;
}

static int32_t msm_led_aw36413_i2c_trigger_config(struct msm_led_flash_ctrl_t *fctrl, void *data) {
    int ret;
    struct msm_flash_cfg_data_t *cfg =(struct msm_flash_cfg_data_t *) data;
    
    aw_info("enter\n");
    if (!fctrl->func_tbl) {
        aw_err("no function table\n");
        return -EINVAL;
    }
    
    switch (cfg->cfg_type) {
    case CFG_FLASH_INIT:
        if (fctrl->func_tbl->flash_led_init)
            ret = fctrl->func_tbl->flash_led_init(fctrl);
        break;
    case CFG_FLASH_RELEASE:
        if (fctrl->func_tbl->flash_led_release)
            ret = fctrl->func_tbl->flash_led_release(fctrl);
        break;
    case CFG_FLASH_OFF:
        if (fctrl->func_tbl->flash_led_off)
            ret = fctrl->func_tbl->flash_led_off(fctrl);
        break;
    case CFG_FLASH_LOW:
        fctrl->flash_op_current[0] = cfg->flash_current[0];
        fctrl->flash_op_current[1] = cfg->flash_current[1];
        aw_info("[LED1 | %d mA] [LED2 | %d mA] [LED3 | %d mA] [LED4 | %d mA]\n", fctrl->flash_op_current[0],
                         fctrl->flash_op_current[0], fctrl->flash_op_current[1], fctrl->flash_op_current[1]);
        aw_info("[LED1, LED2 | max %d mA] [LED3, LED4 | max %d mA]\n", fctrl->flash_max_current[0],
                                                                     fctrl->flash_max_current[1]);
        if (fctrl->func_tbl->flash_led_low)
            ret = fctrl->func_tbl->flash_led_low(fctrl);
        break;
    case CFG_FLASH_HIGH:
        fctrl->flash_op_current[0] = cfg->flash_current[0];
        fctrl->flash_op_current[1] = cfg->flash_current[1];
        aw_info("[LED1 | %d mA] [LED2 | %d mA] [LED3 | %d mA] [LED4 | %d mA]\n", fctrl->flash_op_current[0],
                         fctrl->flash_op_current[0], fctrl->flash_op_current[1], fctrl->flash_op_current[1]);
        aw_info("[LED1, LED2 | max %d mA] [LED3, LED4 | max %d mA]\n", fctrl->flash_max_current[0],
                                                                     fctrl->flash_max_current[1]);
        if (fctrl->func_tbl->flash_led_high)
            ret = fctrl->func_tbl->flash_led_high(fctrl);
        break;
    default:
        aw_warn("invalid cfg_type = %d\n", cfg->cfg_type);
        ret = -EFAULT;
        break;
    }
    
    return ret;
}

static struct msm_flash_fn_t aw36413_func_tbl = {
	.flash_get_subdev_id = msm_led_aw36413_i2c_trigger_get_subdev_id,
	.flash_led_config = msm_led_aw36413_i2c_trigger_config,
	.flash_led_init = msm_flash_aw36413_led_init,
	.flash_led_release = msm_flash_aw36413_led_release,
	.flash_led_off = msm_flash_aw36413_led_off,
	.flash_led_low = msm_flash_aw36413_led_low,
	.flash_led_high = msm_flash_aw36413_led_high,
};

static struct msm_camera_i2c_client aw36413_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
};

static struct msm_led_flash_ctrl_t fctrl = {
    .flash_i2c_client = &aw36413_i2c_client,
    .func_tbl = &aw36413_func_tbl,
};

static int __init msm_flash_aw36413_init(void)
{
	int32_t rc = 0;
    aw_info("enter\n");
    rc = i2c_add_driver(&aw36413_i2c_driver);
    if (!rc)
        aw_info("done\n");
	return rc;
}
static void __exit msm_flash_aw36413_exit(void)
{
    i2c_del_driver(&aw36413_i2c_driver);
}

module_init(msm_flash_aw36413_init);
module_exit(msm_flash_aw36413_exit);
MODULE_DESCRIPTION("AW36413 (x2) LED driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Roman Rihter <teledurak@msucks.space>");
