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
#include "aw36413.h"
#include "msm_camera_io_util.h"
#include "../msm_sensor.h"
#include "msm_led_flash.h"
#include "../cci/msm_cci.h"
#include <linux/debugfs.h>

#define CAM_FLASH_PINCTRL_STATE_SLEEP "cam_flash_suspend"
#define CAM_FLASH_PINCTRL_STATE_DEFAULT "cam_flash_default"

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

static void msm_led_i2c_torch_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness value) {
	if (value > LED_OFF) {
		if (fctrl.func_tbl->flash_led_init)
			fctrl.func_tbl->flash_led_init(&fctrl);
		if (fctrl.func_tbl->flash_led_low)
			fctrl.func_tbl->flash_led_low(&fctrl);
	} else {
        if (fctrl.func_tbl->flash_led_off)
			fctrl.func_tbl->flash_led_off(&fctrl);
		if (fctrl.func_tbl->flash_led_release)
			fctrl.func_tbl->flash_led_release(&fctrl);
	}
};

static struct led_classdev msm_torch_i2c_led = {
    .name		    = "torch-light",
    .brightness_set	= msm_led_i2c_torch_brightness_set,
    .brightness	    = LED_OFF,
};

static int32_t msm_led_get_dt_data(struct device_node *of_node,
		struct msm_led_flash_ctrl_t *fctrl)
{
	int32_t ret = 0, i = 0;
	struct msm_camera_gpio_conf *gconf = NULL;
	struct device_node *flash_src_node = NULL;
	struct msm_camera_sensor_board_info *flashdata = NULL;
	struct msm_camera_power_ctrl_t *power_info = NULL;
	uint32_t count = 0;
	uint16_t *gpio_array = NULL;
	uint16_t gpio_array_size = 0;
	uint32_t id_info[3];

	if (!of_node) {
		aw_err("of_node not found\n");
		return -EINVAL;
	}

	fctrl->flashdata = kzalloc(sizeof(
		struct msm_camera_sensor_board_info),
		GFP_KERNEL);
	if (!fctrl->flashdata) {
		aw_err("flashdata memory allocation failed\n");
		return -ENOMEM;
	}

	flashdata = fctrl->flashdata;
	power_info = &flashdata->power_info;

	ret = of_property_read_u32(of_node, "cell-index", &fctrl->subdev_id);
	if (ret < 0) {
		aw_err("failed on line %d", __LINE__);
		return -EINVAL;
	}

	ret = of_property_read_string(of_node, "label",
		&flashdata->sensor_name);
	if (ret < 0) {
		aw_err("failed on line %d", __LINE__);
		goto ERROR1;
	}

	ret = of_property_read_u32(of_node, "qcom,cci-master",
		&fctrl->cci_i2c_master);
	if (ret < 0) {
		/* Set default master 0 */
		fctrl->cci_i2c_master = MASTER_0;
		ret = 0;
	}

	fctrl->pinctrl_info.use_pinctrl = false;
	fctrl->pinctrl_info.use_pinctrl = of_property_read_bool(of_node,
						"qcom,enable_pinctrl");
	if (of_get_property(of_node, "qcom,flash-source", &count)) {
		count /= sizeof(uint32_t);
		if (count > MAX_LED_TRIGGERS) {
			pr_err("failed\n");
			return -EINVAL;
		}
		for (i = 0; i < count; i++) {
			flash_src_node = of_parse_phandle(of_node,
				"qcom,flash-source", i);
			if (!flash_src_node) {
				pr_err("flash_src_node NULL\n");
				continue;
			}

			ret = of_property_read_string(flash_src_node,
				"linux,default-trigger",
				&fctrl->flash_trigger_name[i]);
			if (ret < 0) {
				of_node_put(flash_src_node);
				continue;
			}

			ret = of_property_read_u32(flash_src_node,
				"qcom,max-current",
				&fctrl->flash_op_current[i]);
			if (ret < 0) {
				of_node_put(flash_src_node);
				continue;
			}

			of_node_put(flash_src_node);

			led_trigger_register_simple(
				fctrl->flash_trigger_name[i],
				&fctrl->flash_trigger[i]);
		}

	} else { /*Handle LED Flash Ctrl by GPIO*/
		power_info->gpio_conf =
			 kzalloc(sizeof(struct msm_camera_gpio_conf),
				 GFP_KERNEL);
		if (!power_info->gpio_conf) {
			aw_err("failed on line %d", __LINE__);
			ret = -ENOMEM;
			return ret;
		}
		gconf = power_info->gpio_conf;

		gpio_array_size = of_gpio_count(of_node);

		if (gpio_array_size) {
			gpio_array = kzalloc(sizeof(uint16_t) * gpio_array_size,
				GFP_KERNEL);
			if (!gpio_array) {
				aw_err("failed on line %d", __LINE__);
				ret = -ENOMEM;
				goto ERROR4;
			}
			for (i = 0; i < gpio_array_size; i++) {
				gpio_array[i] = of_get_gpio(of_node, i);
			}

			ret = msm_camera_get_dt_gpio_req_tbl(of_node, gconf,
				gpio_array, gpio_array_size);
			if (ret < 0) {
				aw_err("failed on line %d", __LINE__);
				goto ERROR4;
			}

			ret = msm_camera_init_gpio_pin_tbl(of_node, gconf,
				gpio_array, gpio_array_size);
			if (ret < 0) {
				aw_err("failed on line %d", __LINE__);
				goto ERROR5;
			}
		}

		/* Read the max current for an LED if present */
		if (of_get_property(of_node, "qcom,max-current", &count)) {
			count /= sizeof(uint32_t);

			if (count > MAX_LED_TRIGGERS) {
				aw_err("failed on line %d", __LINE__);
				ret = -EINVAL;
				goto ERROR8;
			}

			fctrl->flash_num_sources = count;
			fctrl->torch_num_sources = count;

			ret = of_property_read_u32_array(of_node,
				"qcom,max-current",
				fctrl->flash_max_current, count);
			if (ret < 0) {
				aw_err("failed on line %d", __LINE__);
				goto ERROR8;
			}

			for (; count < MAX_LED_TRIGGERS; count++)
				fctrl->flash_max_current[count] = 0;

			for (count = 0; count < MAX_LED_TRIGGERS; count++)
				fctrl->torch_max_current[count] =
					fctrl->flash_max_current[count] >> 1;
		}

		/* Read the max duration for an LED if present */
		if (of_get_property(of_node, "qcom,max-duration", &count)) {
			count /= sizeof(uint32_t);

			if (count > MAX_LED_TRIGGERS) {
				aw_err("failed on line %d", __LINE__);
				ret = -EINVAL;
				goto ERROR8;
			}

			ret = of_property_read_u32_array(of_node,
				"qcom,max-duration",
				fctrl->flash_max_duration, count);
			if (ret < 0) {
				aw_err("failed on line %d", __LINE__);
				goto ERROR8;
			}

			for (; count < MAX_LED_TRIGGERS; count++)
				fctrl->flash_max_duration[count] = 0;
		}

		flashdata->slave_info =
			kzalloc(sizeof(struct msm_camera_slave_info),
				GFP_KERNEL);
		if (!flashdata->slave_info) {
			aw_err("failed on line %d", __LINE__);
			ret = -ENOMEM;
			goto ERROR8;
		}

		ret = of_property_read_u32_array(of_node, "qcom,slave-id",
			id_info, 3);
		if (ret < 0) {
			aw_err("failed on line %d", __LINE__);
			goto ERROR9;
		}
		fctrl->flashdata->slave_info->sensor_slave_addr = id_info[0];
		fctrl->flashdata->slave_info->sensor_id_reg_addr = id_info[1];
		fctrl->flashdata->slave_info->sensor_id = id_info[2];

		kfree(gpio_array);
		return ret;
ERROR9:
		kfree(fctrl->flashdata->slave_info);
ERROR8:
		kfree(fctrl->flashdata->power_info.gpio_conf->gpio_num_info);
ERROR5:
		kfree(gconf->cam_gpio_req_tbl);
ERROR4:
		kfree(gconf);
ERROR1:
		kfree(fctrl->flashdata);
		kfree(gpio_array);
	}
	return ret;
}

static struct v4l2_file_operations msm_led_flash_v4l2_subdev_fops;

static long msm_led_flash_subdev_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	struct msm_led_flash_ctrl_t *lfctrl = NULL;
	void *argp = (void *)arg;

	if (!sd) {
		pr_err("sd NULL\n");
		return -EINVAL;
	}
	lfctrl = v4l2_get_subdevdata(sd);
	if (!lfctrl) {
		pr_err("lfctrl NULL\n");
		return -EINVAL;
	}
	switch (cmd) {
	case VIDIOC_MSM_SENSOR_GET_SUBDEV_ID:
		return lfctrl->func_tbl->flash_get_subdev_id(lfctrl, argp);
	case VIDIOC_MSM_FLASH_LED_DATA_CFG:
		return lfctrl->func_tbl->flash_led_config(lfctrl, argp);
	case MSM_SD_NOTIFY_FREEZE:
		return 0;
	case MSM_SD_SHUTDOWN:
		return lfctrl->func_tbl->flash_led_release(lfctrl);
	default:
		pr_err_ratelimited("%s: invalid cmd %d\n", __func__, cmd);
		return -ENOIOCTLCMD;
	}
}

static struct v4l2_subdev_core_ops msm_flash_subdev_core_ops = {
	.ioctl = msm_led_flash_subdev_ioctl,
};

static struct v4l2_subdev_ops msm_flash_subdev_ops = {
	.core = &msm_flash_subdev_core_ops,
};

static const struct v4l2_subdev_internal_ops msm_flash_internal_ops;

static int msm_flash_pinctrl_init(struct msm_led_flash_ctrl_t *ctrl)
{
	struct msm_pinctrl_info *flash_pctrl = NULL;

	flash_pctrl = &ctrl->pinctrl_info;

	if (ctrl->pdev != NULL)
		flash_pctrl->pinctrl = devm_pinctrl_get(&ctrl->pdev->dev);
	else
		flash_pctrl->pinctrl = devm_pinctrl_get(&ctrl->
					flash_i2c_client->
					client->dev);
	if (IS_ERR_OR_NULL(flash_pctrl->pinctrl)) {
		pr_err("%s:%d Getting pinctrl handle failed\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	flash_pctrl->gpio_state_active = pinctrl_lookup_state(
					       flash_pctrl->pinctrl,
					       CAM_FLASH_PINCTRL_STATE_DEFAULT);

	if (IS_ERR_OR_NULL(flash_pctrl->gpio_state_active)) {
		pr_err("%s:%d Failed to get the active state pinctrl handle\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	flash_pctrl->gpio_state_suspend = pinctrl_lookup_state(
						flash_pctrl->pinctrl,
						CAM_FLASH_PINCTRL_STATE_SLEEP);

	if (IS_ERR_OR_NULL(flash_pctrl->gpio_state_suspend)) {
		pr_err("%s:%d Failed to get the suspend state pinctrl handle\n",
				__func__, __LINE__);
		return -EINVAL;
	}
	return 0;
}

static int aw36413_connect_to_msm_camera_stack(struct i2c_client *client,
                                               const struct i2c_device_id *id) {
    int ret = 0;
    struct msm_led_flash_ctrl_t *lfctrl;
    
    aw_info("Start connecting with MSM camera stack\n");

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		aw_err("i2c_check_functionality failed\n");
		return -EIO;
	}

    lfctrl = (struct msm_led_flash_ctrl_t *)(id->driver_data);
    if (!lfctrl) {
        aw_err("lfctrl not found\n");
        return -EINVAL;
    }

    if (lfctrl->flash_i2c_client)
		lfctrl->flash_i2c_client->client = client;

    /* Set device type as I2C */
	lfctrl->flash_device_type = MSM_CAMERA_I2C_DEVICE;

    /* Assign name for sub device */
	snprintf(lfctrl->msm_sd.sd.name, sizeof(lfctrl->msm_sd.sd.name),
		"%s", id->name);

    ret = msm_led_get_dt_data(client->dev.of_node, lfctrl);
	if (ret) {
        aw_err("Failed to get dt data\n");
		return ret;
	}
    
    if (lfctrl->pinctrl_info.use_pinctrl == true)
		msm_flash_pinctrl_init(lfctrl);

    if (lfctrl->flash_i2c_client != NULL) {
		lfctrl->flash_i2c_client->client = client;
		if (lfctrl->flashdata->slave_info->sensor_slave_addr)
			lfctrl->flash_i2c_client->client->addr =
				lfctrl->flashdata->slave_info->
				sensor_slave_addr;
	} else {
		pr_err("%s %s sensor_i2c_client NULL\n",
			__func__, client->name);
		ret = -EFAULT;
		return ret;
	}

    /* Initialize sub device */
	v4l2_subdev_init(&lfctrl->msm_sd.sd, &msm_flash_subdev_ops);
	v4l2_set_subdevdata(&lfctrl->msm_sd.sd, lfctrl);

    lfctrl->msm_sd.sd.internal_ops = &msm_flash_internal_ops;
	lfctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(lfctrl->msm_sd.sd.name, ARRAY_SIZE(lfctrl->msm_sd.sd.name),
		id->name);
    
    media_entity_init(&lfctrl->msm_sd.sd.entity, 0, NULL, 0);
	lfctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_LED_FLASH;
	msm_sd_register(&lfctrl->msm_sd);

    msm_led_flash_v4l2_subdev_fops = v4l2_subdev_fops;
	lfctrl->msm_sd.sd.devnode->fops = &msm_led_flash_v4l2_subdev_fops;

    return ret;
}

static int msm_flash_aw36413_i2c_probe(struct i2c_client *client,
                                       const struct i2c_device_id *id) {
	int ret;
	
	aw36413 = kzalloc(sizeof(struct aw36413_cfg *), GFP_KERNEL);
	aw36413->id = aw36413_i2c_id;
	aw36413->hwen1 = 95;
	aw36413->hwen2 = 93;
	aw36413->strobe = 96;
	aw36413->a1 = i2c_get_adapter(6);
	aw36413->a2 = i2c_get_adapter(5);
	client->adapter = aw36413->a1;
	
	if (!id) 
        id = aw36413->id;

	if (!aw36413->a1) {
		aw_err("no first i2c adapter found.\n");
		return -EINVAL;
	}

	if (!aw36413->a2) {
		aw_err("no second i2c adapter found.\n");
		return -EINVAL;
	}
	
	// Check are aw36413 valid for usage
    ret = aw36413_gpio_setup();
	if (ret) {
		aw_err("GPIO validating error\n");
		return ret;
	}

    // Make necessary things for camera stack
    ret = aw36413_connect_to_msm_camera_stack(client, id);
    if (ret) {
        aw_err("Failed connecting to MSM camera stack\n");
		return ret;
    }
    
    // Activate pinctrl
    if (fctrl.pinctrl_info.use_pinctrl != false) {
        aw_info("set pinctrl state to active\n");
        ret = pinctrl_select_state(fctrl.pinctrl_info.pinctrl, 
                        fctrl.pinctrl_info.gpio_state_active);
        if (ret) {
            aw_err("error during pinctrl state change\n");
            return ret;
        }
    }
    
    // Get vendor ID to operate with aw36413 by I2C
    aw36413_get_vendor_id();

    // Register LED class device (/sys/class/leds/torch-light)
	msm_led_i2c_torch_brightness_set(&msm_torch_i2c_led, LED_OFF);
    ret = led_classdev_register(&client->dev, &msm_torch_i2c_led);
    if (ret) {
        aw_err("Failed to register LED classdev. ret = %d\n", ret);
		gpio_free(aw36413->hwen1);
		gpio_free(aw36413->hwen2);
		gpio_free(aw36413->strobe);
		return ret;
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

static struct msm_camera_i2c_fn_t msm_sensor_qup_func_tbl = {
	.i2c_read = msm_camera_qup_i2c_read,
	.i2c_read_seq = msm_camera_qup_i2c_read_seq,
	.i2c_write = msm_camera_qup_i2c_write,
	.i2c_write_table = msm_camera_qup_i2c_write_table,
	.i2c_write_seq_table = msm_camera_qup_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
		msm_camera_qup_i2c_write_table_w_microdelay,
};

static struct msm_camera_i2c_client aw36413_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
    .i2c_func_tbl = &msm_sensor_qup_func_tbl,
};

static struct msm_led_flash_ctrl_t fctrl = {
    .flash_i2c_client = &aw36413_i2c_client,
    .func_tbl = &aw36413_func_tbl,
    .flash_device_type = MSM_CAMERA_I2C_DEVICE,
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
MODULE_AUTHOR("Roman Rihter <tdrkDev@gmail.com>"); 
