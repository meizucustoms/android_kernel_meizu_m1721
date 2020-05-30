/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
*  Copyright (c) 2020, MeizuCustoms team
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 and
* only version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/


#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/compat.h>
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

/* warning! need write-all permission so overriding check */ 
#undef VERIFY_OCTAL_PERMISSIONS
#define VERIFY_OCTAL_PERMISSIONS(perms) (perms)

#include "msm-cirrus-playback.h"

static struct device *crus_gb_device;
static struct bus_type opsl_virt_bus;

static atomic_t crus_gb_misc_usage_count;

static struct crus_single_data_t crus_enable;
static struct crus_dual_data_t crus_current_temp;
static struct crus_triple_data_t crus_set_cal_parametes;
static struct crus_triple_data_t crus_temp_cal;
static struct crus_dual_data_t crus_excursion;
static unsigned int crus_pass;

static struct crus_gb_ioctl_header crus_gb_hdr;
static int32_t *crus_gb_get_buffer;
static atomic_t crus_gb_get_param_flag;
struct mutex crus_gb_lock;
struct mutex crus_gb_get_param_lock;
static int crus_gb_enable;
static int music_config_loaded;
static int voice_config_loaded;
static int crus_config_set;
static int crus_ext_config_set;
static int crus_ambient;
static int crus_count;
static int crus_f0;
static int crus_temp_acc;
static int cirrus_fb_port_ctl;
static int cirrus_fb_port = AFE_PORT_ID_QUATERNARY_TDM_TX;
static int cirrus_ff_port = AFE_PORT_ID_QUATERNARY_TDM_RX;

static int crus_afe_get_param(int port, int module, int param, int length,
			      void *data)
{
    unsigned short payload_size = (short)(length & 0xffff) + 0xc;
    int32_t *buf;
    uint32_t index = afe_get_port_index(port);
    unsigned int result = 0;
    struct afe_custom_crus_get_config_t *config =
        (struct afe_custom_crus_get_config_t *)kmem_cache_alloc_trace(kmalloc_caches[7], 0x80d0, 0x38);
    
    pr_info("CRUS CRUS_AFE_GET_PARAM: PAYLOAD_SIZE = %d; DATA_SIZE = %d PORT = %x\n",
            payload_size, (length & 0xffff), port);
    
    if (config == 0) {
        pr_err("CRUS %s:  Memory allocation failed!\n", __func__);
        result = 1;
    } else {
        config->hdr.hdr_field = 0x250;
        config->hdr.pkt_size = 0x38;
        config->hdr.src_svc = '\x04';
        config->hdr.dest_svc = '\x04';
        config->hdr.dest_domain = '\x04';
        config->hdr.opcode = 0x100f0;
        config->param.port_id = port;
        config->hdr.src_domain = '\x05';
        config->param.payload_size = payload_size;
        config->data.param_size = length;
        config->hdr.src_port = 0;
        config->hdr.dest_port = 0;
        config->hdr.token = index;
        config->param.payload_address_lsw = 0;
        config->param.payload_address_msw = 0;
        config->param.mem_map_handle = 0;
        config->param.module_id = module;
        config->param.param_id = param;
        config->data.module_id = module;
        config->data.param_id = param;
        config->data.reserved = 0;
        
        pr_info("CRUS %s: Preparing to send apr packet\n", __func__);
        mutex_lock(&crus_gb_get_param_lock);
        atomic_set(&crus_gb_get_param_flag, 0);
        crus_gb_get_buffer = kmalloc(config->param.payload_size + 0x10, 0x80d0);
        
        result = afe_apr_send_pkt_crus(config, index, 0);
        if (result != 0) {
            pr_err("CRUS %s: crus get_param for port %d failed with code %d\n", __func__, port, result);
        }
        
        while (!atomic_read(&crus_gb_get_param_flag)) {
            msleep(1);
        }
        
        pr_info("CRUS CRUS_AFE_GET_PARAM: returned data = [4]: %d, [5]: %d\n",
               crus_gb_get_buffer[4], crus_gb_get_buffer[5]);
        buf = crus_gb_get_buffer;
        memcpy(data, crus_gb_get_buffer + 4, length);
        kfree(buf);
        mutex_unlock(&crus_gb_get_param_lock);
        kfree(config);
    }
    
    return result;
}

static int crus_afe_set_param(int port, int module, int param, int data_size,
			      void *data_ptr)
{
	struct afe_custom_crus_set_config_t *config = NULL;
	uint32_t index = afe_get_port_index(port);
	int ret = 0;
    unsigned int pkt_size = data_size + 0x30;
    
    pr_info("CRUS_AFE_SET_PARAM: PACKET_SIZE = %d; PAYLOAD_SIZE = %d, DATA_SIZE = %d\n",
           pkt_size, (data_size + 0xc), data_size);
    
    config = kmalloc(pkt_size, 0x80d0);
    if (config == 0) {
        printk("CRUS %s: Memory allocation failed!\n", __func__);
        ret = 1;
    } else {
        memcpy(config + 1, data_ptr, data_size);
        config->data.param_size = data_size;
        config->hdr.hdr_field = 0x250;
        config->hdr.src_svc = '\x04';
        config->hdr.dest_svc = '\x04';
        config->hdr.dest_domain = '\x04';
        config->hdr.opcode = 0x100ef;
        config->hdr.src_domain = '\x05';
        config->hdr.pkt_size = pkt_size;
        config->hdr.src_port = 0;
        config->hdr.dest_port = 0;
        config->hdr.token = index;
        config->param.port_id = port;
        config->param.payload_size = data_size + 0xc;
        config->param.payload_address_lsw = 0;
        config->param.payload_address_msw = 0;
        config->param.mem_map_handle = 0;
        config->data.module_id = module;
        config->data.param_id = param;
        config->data.reserved = 0;
        
        pr_info("CRUS %s: Preparing to send apr packet.\n", __func__);
        ret = afe_apr_send_pkt_crus(config, index, 1);
        if (ret != 0) {
            pr_err("%s: crus set_param for port %d failed with code %d\n", __func__, port, ret);
        }
        
        kfree(config);
    }
    
    return ret;
}

static int crus_afe_send_config(const char *data, int32_t module)
{
	bool plDone;
	uint32_t port;
	int ret = 0, ffPort;
	unsigned int curPort;
	unsigned int param, size, send, val;
	size_t chars_to_send;
	struct afe_custom_crus_set_config_t *config;
	
	ffPort = cirrus_ff_port;
	curPort = cirrus_fb_port;
	chars_to_send = strlen(data);
	send = chars_to_send + 1;
	
	pr_info("CRUS %s: called with module_id = %x, string length = %d\n",
			__func__, module, send);
	
	switch (module) {
	case 0xa1af00: 
		param = 0xaf05;
		port = afe_get_port_index(cirrus_ff_port);
		curPort = ffPort;
		break;
		
	case 0xa1bf00:
		param = 0xbf08;
		port = afe_get_port_index(cirrus_fb_port);
		break;
		
	default:
		pr_err("CRUS %s: Received invalid module parameter %d\n", __func__, module);
		return -EFAULT;
	}
	
	size = send;
	if (4000 < send) {
		size = 4000;
	}
	
	config = kmalloc((size + 0x48), 0x80d0);
	if (config == 0) {
		pr_err("CRUS %s: Memory allocation failed!\n", __func__);
		ret = 1;
	} else {
		config->hdr.hdr_field = 0x250;
		config->hdr.src_svc = '\x04';
		config->hdr.dest_svc = '\x04';
		config->hdr.dest_domain = '\x04';
		config->hdr.token = port;
		config->hdr.opcode = 0x100ef;
		config->param.payload_size = size + 0x24;
		config->data.param_size = size + 0x18;
		config->hdr.pkt_size = size + 0x48;
		ffPort = 0;
		config->hdr.src_domain = '\x05';
		config->hdr.src_port = 0;
		config->hdr.dest_port = 0;
		config->param.port_id = curPort;
		config->param.payload_address_lsw = 0;
		config->param.payload_address_msw = 0;
		config->param.mem_map_handle = 0;
		config->data.module_id = module;
		config->data.param_id = param | 0xa10000;
		config->data.reserved = 0;
		
		while (ret = 0, ffPort < send) {
			chars_to_send = send - ffPort;
			
			plDone = chars_to_send < 0xfa1;
			if (!plDone)
				chars_to_send = 4000;
			
			memcpy(&config->hdr.opcode, data + ffPort, chars_to_send);
			pr_info("CRUS %s: Preparing to send apr packet.\n", __func__);
			val = afe_apr_send_pkt_crus(config, port, 1);
			if (val == 0) {
				val = param | 0xa10000;
				pr_info("CRUS %s: crus set_param sent packet with param id 0x%08x to module 0x%08x.\n", __func__, val, module);
			} else {
				pr_err("CRUS %s: crus set_param for port %d failed with code %d\n", __func__, curPort, val);
			}
			ffPort = ffPort + chars_to_send;
		}
		kfree(config);
	}
	return ret;
}

extern int crus_afe_callback(void *payload, int size)
{
	uint32_t *payload32 = payload;

	pr_debug("Cirrus AFE CALLBACK: size = %d\n", size);

	if ((payload32[1] == 0xa1af00) || (payload32[1] == 0xa1bf00)) {
		memcpy(crus_gb_get_buffer, payload32, size);
		atomic_set(&crus_gb_get_param_flag, 1);
        return 0;
    } else {
        return -EINVAL;
    }

	return 0;
}

int msm_routing_cirrus_fbport_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("CRUS %s: cirrus_fb_port_ctl = %d", __func__, cirrus_fb_port_ctl);
	ucontrol->value.integer.value[0] = cirrus_fb_port_ctl;
	return 0;
}

int msm_routing_cirrus_fbport_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	cirrus_fb_port_ctl = ucontrol->value.integer.value[0];

	switch (cirrus_fb_port_ctl) {
	case 0:
		cirrus_fb_port = 0x1001;
		cirrus_ff_port = 0x1000;
		break;
	case 1:
		cirrus_fb_port = 0x1003;
		cirrus_ff_port = 0x1002;
		break;
	case 2:
		cirrus_fb_port = 0x1005;
		cirrus_ff_port = 0x1004;
		break;
	case 3:
		cirrus_fb_port = 0x1007;
		cirrus_ff_port = 0x1006;
		break;
	case 4:
		cirrus_fb_port = 0x1003;
		cirrus_ff_port = 0x1002;
		break;
	default:   
		cirrus_fb_port_ctl = 4;
		cirrus_fb_port = 0x1007;
		cirrus_ff_port = 0x1006;
		break;
	}
	return 0;
}

static ssize_t opsl_pass_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
    int ret;
    
	ret = crus_pass;
    pr_info("CRUS opalum enter %s\n", __func__);
    ret = sprintf(buf, "%d", ret);
    return ret;
}

static ssize_t opsl_f0_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	int ret;
	
	ret = crus_f0;
	pr_info("opalum enter %s\n", __func__);
	ret = sprintf(buf, "%d", ret);
	return ret;
}
static ssize_t opsl_ambient_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	int ret;
	
	ret = crus_ambient;
	pr_info("opalum enter %s\n", __func__);
	ret = sprintf(buf, "%d", ret);
	return ret;
}
static ssize_t opsl_count_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	int ret;
	
	ret = crus_count;
	pr_info("opalum enter %s\n", __func__);
	ret = sprintf(buf, "%d", ret);
	return ret;
}
static ssize_t opsl_temp_acc_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	int ret;
	
	ret = crus_temp_acc;
	pr_info("opalum enter %s\n", __func__);
	ret = sprintf(buf, "%d", ret);
	return ret;
}
static ssize_t opsl_f0_store(struct device *dev,
					struct device_attribute *attr, const char *buf,
					size_t count)
{
	int val;
	ssize_t ret;
	int newVarData;
	
	val = kstrtoint(buf, 0, &newVarData);
	ret = 0xffffffffffffffea;
	if (val == 0) {
		crus_f0 = newVarData;
		pr_info("CRUS set f0 to %d\n", newVarData);
		ret = count;
	}
	return ret;
}
static ssize_t opsl_ambient_store(struct device *dev,
					struct device_attribute *attr, const char *buf,
					size_t count)
{
	int val;
	ssize_t ret;
	int newVarData;
	
	val = kstrtoint(buf, 0, &newVarData);
	ret = 0xffffffffffffffea;
	if (val == 0) {
		crus_ambient = newVarData;
		pr_info("CRUS set f0 to %d\n", newVarData);
		ret = count;
	}
	return ret;
}
static ssize_t opsl_count_store(struct device *dev,
					struct device_attribute *attr, const char *buf,
					size_t count)
{
	int val;
	ssize_t ret;
	int newVarData;
	
	val = kstrtoint(buf, 0, &newVarData);
	ret = 0xffffffffffffffea;
	if (val == 0) {
		crus_count = newVarData;
		pr_info("CRUS set f0 to %d\n", newVarData);
		ret = count;
	}
	return ret;
}
static ssize_t opsl_temp_acc_store(struct device *dev,
					struct device_attribute *attr, const char *buf,
					size_t count)
{
	int val;
	ssize_t ret;
	int newVarData;
	
	val = kstrtoint(buf, 0, &newVarData);
	ret = 0xffffffffffffffea;
	if (val == 0) {
		crus_temp_acc = newVarData;
		pr_info("CRUS set f0 to %d\n", newVarData);
		ret = count;
	}
	return ret;
}

static ssize_t opsl_cali_start(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	unsigned int val = 1;
	int ret;
	
	msleep(2000);
	pr_info("CRUS Opalum calibration starts ... after delay\n");
	crus_afe_set_param(cirrus_fb_port, 0xa1bf00, 0xa1bf03, 4, &val);
	crus_afe_set_param(cirrus_ff_port, 0xa1af00, 0xa1af03, 4, &val);
	msleep(7000);
	crus_afe_get_param(cirrus_fb_port, 0xa1bf00, 0xa1bf06, 0xc, &crus_temp_cal);
	pr_info("CRUS_GB_CONFIG: temp cal complete: Temp-acc = %d, Count = %d, Ambient = %d\n",
				crus_temp_cal.data1, crus_temp_cal.data2, crus_temp_cal.data3);
	
	crus_temp_acc = crus_temp_cal.data1;
	crus_count = crus_temp_cal.data2;
	
	msleep(100);
	pr_info("Step 3 CRUS Opalum starts F0 Calibration\n");
	crus_afe_get_param(cirrus_fb_port, 0xa1bf00, 0xa1bf05, 8, &crus_excursion);
	pr_info("CRUS_GB_GET_F0:crus_excursion.data1 = %d, crus_excursion.data2 = %d\n",
				crus_excursion.data1, crus_excursion.data2);
	crus_f0 = crus_excursion.data1;
	crus_pass = 0;
	
	if ((crus_temp_acc - 0x3d937 < 0x145d0) && (crus_count == 0x10))
		crus_pass = (crus_excursion.data1 - 0x191 < 499 && crus_ambient < 0x1d);
	
	pr_info("CRUS end of calibration; temp-acc=%d, count=%d, ambient=%d, f0=%d, pass=%d \n",
				crus_temp_acc, crus_count, crus_ambient, crus_f0, crus_pass);
	
	ret = sprintf(buf, "%d", crus_pass);
	return ret;
};

static const struct device_attribute opalum_dev_attr[] = {
	__ATTR("temp-acc", 0777, opsl_temp_acc_show, opsl_temp_acc_store),
	__ATTR("count", 0777, opsl_count_show, opsl_count_store),
	__ATTR("ambient", 0777, opsl_ambient_show, opsl_ambient_store),
	__ATTR("f0", 0777, opsl_f0_show, opsl_f0_store),
	__ATTR("pass", 0777, opsl_pass_show, NULL),
	__ATTR("start", 0777, opsl_cali_start, NULL),
};

static struct bus_type opsl_virt_bus = {
	.name = "opsl-cali",
	.dev_name = "opsl-cali",
};

static struct device opalum_virt = {
	.bus = &opsl_virt_bus,
};
 
static int msm_routing_crus_gb_enable(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
    int crus_set = ucontrol->value.integer.value[0];
    
    if (crus_set > 1) {
		pr_err("CRUS GB Enable: Invalid entry %d; Enter 0 to DISABLE, 1 to ENABLE\n", crus_set);
		return -EINVAL;
	}
	
	switch (crus_set) {
	case 0: /* "Config GB Disable" */
		pr_info("CRUS GB Enable: Config DISABLE\n");
		crus_enable.value = 0;
		crus_gb_enable = 0;
		break;
	case 1: /* "Config GB Enable" */
		pr_info("CRUS GB Enable: Config ENABLE\n");
		crus_enable.value = 1;
		crus_gb_enable = 1;
		break;
	default:
        return -EINVAL;
	}
	
	mutex_lock(&crus_gb_lock);
	crus_afe_set_param(cirrus_ff_port, CRUS_GB,
			   CRUS_AFE_PARAM_ID_ENABLE,
			   sizeof(struct crus_single_data_t),
			   (void *)&crus_enable);
	mutex_unlock(&crus_gb_lock);

	mutex_lock(&crus_gb_lock);
	crus_afe_set_param(cirrus_fb_port, CRUS_GB,
			   CRUS_AFE_PARAM_ID_ENABLE,
			   sizeof(struct crus_single_data_t),
			   (void *)&crus_enable);
	mutex_unlock(&crus_gb_lock);
    
    return 0;
};

static int cirrus_transfer_params(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol) {
    
    crus_set_cal_parametes.data1 = ucontrol->value.integer.value[0];
    crus_set_cal_parametes.data2 = ucontrol->value.integer.value[0] + 8;
    crus_set_cal_parametes.data3 = ucontrol->value.integer.value[0] + 0x10;
    
    pr_info("CRUS PARAMS TRANSFER +++++++++++++  =%d, count=%d, ambient=%d \n", 
                    crus_set_cal_parametes.data1, 
                    crus_set_cal_parametes.data2, 
                    crus_set_cal_parametes.data3);
    
    if (((crus_set_cal_parametes.data1 - 0x50dc1 < 0x11f7f)
        && (crus_set_cal_parametes.data2 == 0x10))
            && (crus_set_cal_parametes.data3 - 0x15 < 8)) {
        
        crus_afe_set_param(cirrus_ff_port, CRUS_GB, 0xa1af08, 0xc, &crus_set_cal_parametes);
        return 0;
    } else {
        pr_err("CRUS wrong range for temp-acc=%d, count=%d, ambient=%d \n",
            crus_set_cal_parametes.data1,
            crus_set_cal_parametes.data2,
            crus_set_cal_parametes.data3);
        return 1;
    } 
};

static int msm_routing_crus_gb_config(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
    int crus_set = ucontrol->value.integer.value[0];
    unsigned int opalum_state = 1;
    crus_config_set = crus_set;
    
    pr_info("Starting CRUS GB Config function call %d\n", crus_set);
    
    switch (crus_set) {
    case 0:
        opalum_state = 0;
        pr_info("CRUS path of Opalum for Music\n");
        crus_afe_set_param(cirrus_ff_port, 0xa1bf00, 0xa1af02, 4, &opalum_state);
        music_config_loaded = 0;
        break;
        
    case 1:
        pr_info("CRUS GB Config: Get Temp Cal Config\n");
        crus_afe_set_param(cirrus_fb_port, 0xa1bf00, 0xa1bf03, 4, &opalum_state);
        crus_afe_set_param(cirrus_ff_port, 0xa1af00, 0xa1af03, 4, &opalum_state);
        msleep(7000);
        crus_afe_get_param(cirrus_fb_port, 0xa1bf00, 0xa1bf06, 0xc, &crus_temp_cal);
        
        pr_info("CRUS_GB_CONFIG: temp cal complete: Temp-acc = %d, Count = %d, Ambient = %d\n",
                        crus_temp_cal.data1,
                        crus_temp_cal.data2,
                        crus_temp_cal.data3);
        break;
        
    case 2:
        pr_info("CRUS GB Config: Set Temp Calibration\n");
        if ((crus_temp_acc - 0x3d937U < 0x145d0) && (crus_count == 0x10)) {
            
            crus_temp_cal.data1 = crus_temp_acc;
            crus_temp_cal.data2 = crus_count;
        } else {
            crus_temp_cal.data1 = 0x3d936;
            crus_temp_cal.data2 = 0x10;
        }
        crus_temp_cal.data3 = 0x1c;
        crus_afe_set_param(cirrus_ff_port, 0xa1af00, 0xa1af08, 0xc, &crus_temp_cal);
        
        pr_info("CRUS_GB_CONFIG: set temp cal with Temp-acc = %d, Count = %d, Ambient = %d\n",
                        crus_temp_cal.data1,
                        crus_temp_cal.data2,
                        crus_temp_cal.data3);
        break;
        
    case 3:
        pr_info("CRUS GB Config: Get Current Temp\n");
        crus_afe_get_param(cirrus_ff_port, 0xa1af00, 0xa1af07, 8, &crus_current_temp);
        pr_info("CRUS_GB_CONFIG: current temp updated; Temp-acc = %d, Count = %d\n",
                        crus_current_temp.data1,
                        crus_current_temp.data2);
        break;
        
    case 4:
        pr_info("CRUS GB Config: Set Fo Calibration\n");
        crus_afe_get_param(cirrus_fb_port, 0xa1bf00, 0xa1bf05, 8, &crus_excursion);
        pr_info("CRUS_GB_GET_F0:crus_excursion.data1 = %d, crus_excursion.data2 = %d\n",
                        crus_excursion.data1,
                        crus_excursion.data2);
        crus_f0 = crus_excursion.data1;
        break;
        
    case 5:
        opalum_state = 1;
        pr_info(" CRUS path of Opalum, for Voice \n");
        crus_afe_set_param(cirrus_ff_port, 0xa1af00, 0xa1af02, 4, &opalum_state);
        voice_config_loaded = 0;
        break;
        
    case 6:
        opalum_state = 2;
        pr_info(" CRUS path of Opalum for Dolby\n");
        crus_afe_set_param(cirrus_ff_port, 0xa1af00, 0xa1af02, 4, &opalum_state);
        break;
        
    default:
        pr_err("CRUS GB Config: Invalid entry; Enter 0 for DEFAULT, 1 for RUN_DIAGNOSTICS, 2 for SET_TEMP_CAL, 3 for GET_CURRENT_TEMP, 4 for SET_FO_CAL\n");
        return 0;
    }
    
    return 0;
};

static int msm_routing_crus_ext_config(struct snd_kcontrol *kcontrol,
											struct snd_ctl_elem_value *ucontrol)
{
    int ret;
    char *string;
    size_t size;
    const struct firmware *fw;
    int crus_set = ucontrol->value.integer.value[0];
    
    crus_ext_config_set = crus_set;
    pr_info("Starting CRUS GB EXT Config function call %d\n", crus_set);
    
    switch (crus_set) {
    case 0:
        ret = request_firmware(&fw, "crus_gb_config_default.bin", crus_gb_device);
        if (ret == 0) {
            crus_set = fw->size;
            size = crus_set;
            string = kmalloc(size, 0x80d0);
            memcpy(string, fw->data, size);
            string[size] = '\0';
            
            if (music_config_loaded == 0) {
                music_config_loaded = 1;
                pr_info("CRUS GB EXT Config: Config RX Default");
                // flash
                crus_afe_send_config(string, 0xaf00 | 0xa10000);
                // make vars clean
                release_firmware(fw);
                kfree(string);
                
                return 0;
            }    
            
            pr_info("CRUS GB EXT Config: Config RX Default ignored\n");
            // make vars clean
            release_firmware(fw);
            kfree(string);
            return 0;
        } else {
            string = NULL;
            pr_err("CRUS %s: Request firmware failed\n", __func__);
            return ret;
        }
        
        break;
    case 1:
        ret = request_firmware(&fw, "crus_gb_config_default.bin", crus_gb_device);
        if (ret == 0) {
            crus_set = fw->size;
            size = crus_set;
            string = kmalloc(size, 0x80d0);
            memcpy(string, fw->data, size);
            string[size] = '\0';
            
            pr_info("CRUS GB EXT Config: Config TX Default");
            // flash; wtf, check stock kernel in decompiler
            //crus_afe_send_config(string, 0xaf00 | 0xa10000);
            // make vars clean
            release_firmware(fw);
            kfree(string);
            return 0;
        } else {
            string = NULL;
            pr_err("CRUS %s: Request firmware failed\n", __func__);
            return ret;
        }
        
        break;
    case 2:
        ret = request_firmware(&fw, "crus_gb_config_new_rx.bin", crus_gb_device);
        if (ret == 0) {
            crus_set = fw->size;
            size = crus_set;
            string = kmalloc(size, 0x80d0);
            memcpy(string, fw->data, size);
            string[size] = '\0';
            
            if (voice_config_loaded == 0) {
                voice_config_loaded = 1;
                pr_info("CRUS GB EXT Config: Config RX New");
                // flash
                crus_afe_send_config(string, 0xaf00 | 0xa10000);
                // make vars clean
                release_firmware(fw);
                kfree(string);
                
                return 0;
            }
            pr_info("CRUS GB EXT Config: Config RX New ignored\n");
            // make vars clean
            release_firmware(fw);
            kfree(string);
            return 0;
        } else {
            string = NULL;
            pr_err("CRUS %s: Request firmware failed\n", __func__);
            return ret;
        }
        
        break;
    case 3:
        ret = request_firmware(&fw, "crus_gb_config_new_tx.bin", crus_gb_device);
        if (ret == 0) {
            crus_set = fw->size;
            size = crus_set;
            string = kmalloc(size, 0x80d0);
            memcpy(string, fw->data, size);
            string[size] = '\0';
            
            pr_info("CRUS GB EXT Config: Config TX New");
            // flash
            crus_afe_send_config(string, 0xbf00 | 0xa10000);
            // make vars clean
            release_firmware(fw);
            kfree(string);
            
            return 0;
        } else {
            string = NULL;
            pr_err("CRUS %s: Request firmware failed\n", __func__);
            return ret;
        }
        
        break;
    default:
        pr_err("CRUS GB External Config: Invalid entry - %d; Enter 0 for RX_DEFAULT, 1 for TX_DEFAULT, 2 for RX_NEW, 3 for TX_NEW\n",
                    crus_set);
        return 0;
    }
    
    string = NULL;
    pr_err("CRUS %s: Request firmware failed\n", __func__);
    return ret;
};

static int msm_routing_crus_gb_enable_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	int crus_set = ucontrol->value.integer.value[0];
    
	pr_info("Starting CRUS GB Enable Get function call\n");

	crus_set = crus_gb_enable;
    return 0;
};

static int msm_routing_crus_gb_config_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("Starting CRUS GB EXT Config Get function call\n");
	ucontrol->value.integer.value[0] = crus_config_set;

	return 0;
};

static int msm_routing_crus_ext_config_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("Starting CRUS GB Config Get function call\n");
	ucontrol->value.integer.value[0] = crus_ext_config_set;

	return 0;
};

static const char * const cirrus_fb_port_text[] = {"PRI_MI2S_RX",
						   "SEC_MI2S_RX",
						   "TERT_MI2S_RX",
						   "QUAT_MI2S_RX",
                           "QUIN_MI2S_RX"};

static const char * const crus_en_text[] = {"Config GB Disable",
					    "Config GB Enable"};
                        
static const char * const crus_gb_text[] = {"Path_for_Music",
                            "Run Diag",
                            "Set Temp Cal",
                            "Set Current Temp",
                            "Set Fo Cal",
                            "Path_for_Voice",
                            "Path_for_Dolby"};
                            
static const char * const crus_ext_text[] = {"Config RX Default",
                            "Config TX Default",
                            "Config RX New",
                            "Config TX New"};

static const struct soc_enum cirrus_fb_controls_enum[] = {
	SOC_ENUM_SINGLE_EXT(5, cirrus_fb_port_text),
};

static const struct soc_enum crus_en_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, crus_en_text),
};

static const struct soc_enum crus_gb_enum[] = {
	SOC_ENUM_SINGLE_EXT(7, crus_gb_text),
};

static const struct soc_enum crus_ext_enum[] = {
	SOC_ENUM_SINGLE_EXT(4, crus_ext_text),
};

static const struct snd_kcontrol_new crus_mixer_controls[] = {
	SOC_ENUM_EXT("FBProtect Port", cirrus_fb_controls_enum[0],
	msm_routing_cirrus_fbport_get, msm_routing_cirrus_fbport_put),
	SOC_ENUM_EXT("CIRRUS GB ENABLE", crus_en_enum[0],
	msm_routing_crus_gb_enable_get, msm_routing_crus_gb_enable),
	SOC_ENUM_EXT("CIRRUS GB CONFIG", crus_gb_enum[0],
	msm_routing_crus_gb_config_get, msm_routing_crus_gb_config),
    SOC_ENUM_EXT("CIRRUS EXT CONFIG", crus_ext_enum[0],
	msm_routing_crus_ext_config_get, msm_routing_crus_ext_config),
	SOC_SINGLE_MULTI_EXT("CIRRUS_TRANSFER_PARAMS", SND_SOC_NOPM, 0,
	0xFFFF, 0, 4, cirrus_transfer_params, NULL),
};

void msm_crus_pb_add_controls(struct snd_soc_platform *platform)
{
    crus_gb_device = platform->dev;
    
    if (crus_gb_device == 0) {
        pr_err("CRUS %s: platform->dev is NULL!\n", __func__);
    } else {
        pr_info("CRUS %s: platform->dev check success!\n", __func__);
    }
    
    snd_soc_add_platform_controls(platform, crus_mixer_controls, 5);
    
    return;
};

static long crus_gb_shared_ioctl(struct file *f, unsigned int cmd,
				 void __user *arg)
{
	int port, param;
	uint32_t size, length;
    unsigned int result = 0, val = 0;
    void *data = NULL;

	pr_info("CRUS %s ++\n", __func__);

    if (arg == 0) {
        pr_err("CRUS %s: No data sent to driver!\n", __func__);
        result = -EFAULT;
        goto exit;
    } 
    
    pr_info("CRUS %s: Command is %x\n", __func__, cmd);
    
	if (copy_from_user(&size, arg, sizeof(size))) {
		pr_err("CRUS %s: copy_from_user (size) failed\n", __func__);
		result = -EFAULT;
		goto exit;
	}

	/* Copy IOCTL header from usermode */
	if (copy_from_user(arg, &crus_gb_hdr, size)) {
		pr_err("CRUS %s: copy_from_user (struct) failed\n", __func__);
		result = -EFAULT;
		goto exit;
	}
    
	length = crus_gb_hdr.data_length;
    val = crus_gb_hdr.data_length;
    data = kmalloc(crus_gb_hdr.data_length, 0x80d0);

	switch (cmd) {
    case 0:
        if (crus_gb_hdr.param_id == 0xabcdee) {
            param = 0xabcdee;
            port = cirrus_fb_port;
            crus_afe_get_param(port, crus_gb_hdr.module_id,
                                    param, length, data);
        } else if (crus_gb_hdr.param_id == 0xabcdef) {
            param = 0xabcdef;
            port = cirrus_ff_port;
            crus_afe_get_param(port, crus_gb_hdr.module_id,
                                    param, length, data);
        }
        
        val = copy_to_user(crus_gb_hdr.data, data, val);
        if (val != 0) {
            length = 0xfffffff2;
            pr_err("CRUS %s: copy_to_user failed, ret = %d\n", __func__, val);
            result = val;
            kfree(data);
            goto exit;
        }
        result = val;
        kfree(data);
        goto exit;
        break; 
        
    case 1:
        memset(crus_gb_hdr.data, 0, length);
            
        val = copy_from_user(crus_gb_hdr.data, data, length);
        if (val != 0) {
            length = 0xfffffff2;
            pr_err("CRUS %s: copy_from_user failed, ret = %d\n", __func__, val);
            result = val;
            kfree(data);
            goto exit;
        }
        if (crus_gb_hdr.param_id == 0xabcdee) {
            param = 0xabcdee;
            port = cirrus_fb_port;
            crus_afe_set_param(port, crus_gb_hdr.module_id,
                                    param, length, data);
        } else if (crus_gb_hdr.param_id == 0xabcdef) {
            param = 0xabcdef;
            port = cirrus_ff_port;
            crus_afe_set_param(port, crus_gb_hdr.module_id,
                                    param, length, data);
        }
        length = 0;
        result = val;
        kfree(data);
        goto exit;
        break;
        
	default:
		pr_err("CRUS %s: Invalid IOCTL, command = %d! Avalible cmds: 0, 1.\n", __func__, cmd);
		result = -EINVAL;
	} 
	
exit:
	return result;
};

static long crus_gb_ioctl(struct file *f,
		unsigned int cmd, unsigned long arg)
{
	return crus_gb_shared_ioctl(f, cmd, (void __user *)arg);
}

static int crus_gb_open(struct inode *inode, struct file *f)
{
	char string;
    bool monPass;

	pr_info("CRUS %s\n", __func__);
    
    do {
        string = '\x01';
        monPass = (bool)atomic_read(&crus_gb_misc_usage_count);
        
        if (monPass) {
            string = atomic_read(&crus_gb_misc_usage_count);
            atomic_inc(&crus_gb_misc_usage_count);
        }
        
    } while (string != '\0');
    
    pr_info("CRUS %s: done\n", __func__);

	return 0;
};

static int crus_gb_release(struct inode *inode, struct file *f)
{
	char string;
    bool monPass;

	pr_info("CRUS %s\n", __func__);
	
    do {
        string = '\x01';
        monPass = (bool)atomic_read(&crus_gb_misc_usage_count);
        
        if (monPass) {
            string = atomic_read(&crus_gb_misc_usage_count);
            atomic_dec(&crus_gb_misc_usage_count);
        }
        
    } while (string != '\0');
    
    pr_info("CRUS %s: ref count %d!\n", __func__, atomic_read(&crus_gb_misc_usage_count));

	return 0;
};

static const struct file_operations crus_gb_fops = {
	.owner = THIS_MODULE,
	.open = crus_gb_open,
	.release = crus_gb_release,
	.unlocked_ioctl = &crus_gb_ioctl,
};

struct miscdevice crus_gb_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "CIRRUS_SPEAKER_PROTECTION",
	.fops = &crus_gb_fops,
};

static int __init crus_gb_init(void)
{
    unsigned int pos, posSec;
    int ret;
    
	pr_info("CRUS_GB_INIT: initializing misc device\n");
	atomic_set(&crus_gb_get_param_flag, 0);
	atomic_set(&crus_gb_misc_usage_count, 0);
	mutex_init(&crus_gb_get_param_lock);
	mutex_init(&crus_gb_lock);

	ret = subsys_system_register(&opsl_virt_bus, 0);
    if (ret == 0) {
        ret = device_register(&opalum_virt);
        if (ret != 0) {
            dev_err(&opalum_virt, "opalum device register failed!\n");
            return ret;
        }
    } else {
        dev_err(&opalum_virt, "opalum sybsystem register failed!\n");
        return ret;
    }
    
    for (pos = 0; pos < 6; pos++) {
        
        ret = device_create_file(&opalum_virt, &opalum_dev_attr[pos]);
        if (ret < 0) {
            dev_err(&opalum_virt, "fail : node create, pos: %d\n", pos);
            
            for (posSec = pos; posSec >= 0; posSec--) 
                device_remove_file(&opalum_virt, &opalum_dev_attr[posSec]);
            
            return ret;    
        }    
    }
    
    return 0;
};
module_init(crus_gb_init);

static void __exit crus_gb_exit(void)
{
	mutex_destroy(&crus_gb_get_param_lock);
	mutex_destroy(&crus_gb_lock);
};
module_exit(crus_gb_exit);
