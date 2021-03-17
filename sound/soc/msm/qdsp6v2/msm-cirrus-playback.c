/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#include "msm-cirrus-playback.h"

static struct device *crus_gb_device;
static atomic_t crus_gb_misc_usage_count;

static struct crus_single_data_t crus_enable;
static struct crus_dual_data_t crus_excursion;
static struct crus_dual_data_t crus_current_temp;
static struct crus_triple_data_t crus_temp_cal;
static struct crus_triple_data_t crus_set_cal_parametes;

static struct crus_gb_ioctl_header crus_gb_hdr;
static int32_t *crus_gb_get_buffer;
static atomic_t crus_gb_get_param_flag;
struct mutex crus_gb_get_param_lock;
struct mutex crus_gb_lock;
static int cirrus_ff_chan_swap_sel;
static int cirrus_ff_chan_swap_dur;
static int crus_pass;
static int cirrus_fb_port_ctl;
static int crus_ext_config_set;
static int crus_config_set;
static int crus_gb_enable;
static int crus_temp_acc;
static int crus_count;
static int crus_ambient;
static int crus_f0;
static int music_config_loaded;
static int voice_config_loaded;
static int cirrus_fb_port = AFE_PORT_ID_QUATERNARY_MI2S_TX;
static int cirrus_ff_port = AFE_PORT_ID_QUATERNARY_MI2S_RX;

static void *crus_gen_afe_get_header(int length, int port, int module,
				     int param)
{
	struct afe_custom_crus_get_config_t *config = NULL;
	int size = sizeof(struct afe_custom_crus_get_config_t);
	int index = afe_get_port_index(port);
	uint16_t payload_size = sizeof(struct afe_port_param_data_v2) +
				length;

	/* Allocate memory for the message */
	config = kzalloc(size, GFP_KERNEL);
	if (!config)
		return NULL;

	/* Set header section */
	config->hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
					APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	config->hdr.pkt_size = size;
	config->hdr.src_svc = APR_SVC_AFE;
	config->hdr.src_domain = APR_DOMAIN_APPS;
	config->hdr.src_port = 0;
	config->hdr.dest_svc = APR_SVC_AFE;
	config->hdr.dest_domain = APR_DOMAIN_ADSP;
	config->hdr.dest_port = 0;
	config->hdr.token = index;
	config->hdr.opcode = AFE_PORT_CMD_GET_PARAM_V2;

	/* Set param section */
	config->param.port_id = (uint16_t) port;
	config->param.payload_address_lsw = 0;
	config->param.payload_address_msw = 0;
	config->param.mem_map_handle = 0;
	config->param.module_id = (uint32_t) module;
	config->param.param_id = (uint32_t) param;
	/* max data size of the param_ID/module_ID combination */
	config->param.payload_size = payload_size;

	/* Set data section */
	config->data.module_id = (uint32_t) module;
	config->data.param_id = (uint32_t) param;
	config->data.reserved = 0; /* Must be set to 0 */
	/* actual size of the data for the module_ID/param_ID pair */
	config->data.param_size = length;

	return (void *)config;
}

static void *crus_gen_afe_set_header(int length, int port, int module,
				     int param)
{
	struct afe_custom_crus_set_config_t *config = NULL;
	int size = sizeof(struct afe_custom_crus_set_config_t) + length;
	int index = afe_get_port_index(port);
	uint16_t payload_size = sizeof(struct afe_port_param_data_v2) +
				length;

	/* Allocate memory for the message */
	config = kzalloc(size, GFP_KERNEL);
	if (!config)
		return NULL;

	/* Set header section */
	config->hdr.hdr_field = 592;
	config->hdr.pkt_size = size;
	config->hdr.src_svc = 4;
	config->hdr.src_domain = 5;
	config->hdr.src_port = 0;
	config->hdr.dest_svc = 4;
	config->hdr.dest_domain = 4;
	config->hdr.dest_port = 0;
	config->hdr.token = index;
	config->hdr.opcode = 65775;

	/* Set param section */
	config->param.port_id = (uint16_t) port;
	config->param.payload_address_lsw = 0;
	config->param.payload_address_msw = 0;
	config->param.mem_map_handle = 0;
	/* max data size of the param_ID/module_ID combination */
	config->param.payload_size = payload_size;

	/* Set data section */
	config->data.module_id = (uint32_t) module;
	config->data.param_id = (uint32_t) param;
	config->data.reserved = 0; /* Must be set to 0 */
	/* actual size of the data for the module_ID/param_ID pair */
	config->data.param_size = length;

	return (void *)config;
}

static int crus_afe_get_param(int port, int module, int param, int length,
			      void *data)
{
	struct afe_custom_crus_get_config_t *config = NULL;
	int index = afe_get_port_index(port);
	int ret = 0;

	pr_info("%s: port = %d module = %d param = 0x%x length = %d\n",
		__func__, port, module, param, length);

	config = (struct afe_custom_crus_get_config_t *)
		 crus_gen_afe_get_header(length, port, module, param);
	if (config == NULL) {
		pr_err("%s: Memory allocation failed!\n", __func__);
		return -ENOMEM;
	}

	pr_info("%s: Preparing to send apr packet\n", __func__);

	mutex_lock(&crus_gb_get_param_lock);
	atomic_set(&crus_gb_get_param_flag, 0);

	crus_gb_get_buffer = kzalloc(config->param.payload_size + 16,
				     GFP_KERNEL);

	ret = afe_apr_send_pkt_crus(config, index, 0);
	if (ret)
		pr_err("%s: crus get_param for port %d failed with code %d\n",
						__func__, port, ret);
	else
		pr_info("%s: crus get_param sent packet with param id 0x%08x to module 0x%08x.\n",
			__func__, param, module);

	/* Wait for afe callback to populate data */
	while (!atomic_read(&crus_gb_get_param_flag))
		msleep(1);

	pr_info("CRUS CRUS_AFE_GET_PARAM: returned data = [4]: %d, [5]: %d\n",
               crus_gb_get_buffer[4], crus_gb_get_buffer[5]);
	
	/* Copy from dynamic buffer to return buffer */
	memcpy((u8 *)data, &crus_gb_get_buffer[4], length);

	kfree(crus_gb_get_buffer);

	mutex_unlock(&crus_gb_get_param_lock);

	kfree(config);
	return ret;
}

static int crus_afe_set_param(int port, int module, int param, int data_size,
			      void *data_ptr)
{
	struct afe_custom_crus_set_config_t *config = NULL;
	int index = afe_get_port_index(port);
	int ret = 0;

	pr_info("%s: port = %d module = %d param = 0x%x data_size = %d\n",
		__func__, port, module, param, data_size);

	config = crus_gen_afe_set_header(data_size, port, module, param);
	if (config == NULL) {
		pr_err("%s: Memory allocation failed!\n", __func__);
		return -ENOMEM;
	}

	memcpy((u8 *)config + sizeof(struct afe_custom_crus_set_config_t),
	       (u8 *) data_ptr, data_size);

	pr_info("%s: Preparing to send apr packet.\n", __func__);

	ret = afe_apr_send_pkt_crus(config, index, 1);
	if (ret) {
		pr_err("%s: crus set_param for port %d failed with code %d\n",
						__func__, port, ret);
	} else {
		pr_info("%s: crus set_param sent packet with param id 0x%08x to module 0x%08x.\n",
			 __func__, param, module);
	}

	kfree(config);
	return ret;
}

static int crus_afe_send_config(const char *data, int32_t length,
				int32_t port, int32_t module)
{
	struct afe_custom_crus_set_config_t *config = NULL;
	struct crus_external_config_t *payload = NULL;
	int size = sizeof(struct crus_external_config_t);
	int ret = 0;
	int index = afe_get_port_index(port);
	uint32_t param = 0;
	int mem_size = 0;
	int sent = 0;
	int chars_to_send = 0;

	pr_info("%s: called with module_id = %x, string length = %d\n",
						__func__, module, length);

	/* Destination settings for message */
	if (port == cirrus_ff_port)
		param = CRUS_PARAM_RX_SET_EXT_CONFIG;
	else if (port == cirrus_fb_port)
		param = CRUS_PARAM_TX_SET_EXT_CONFIG;
	else {
		pr_err("%s: Received invalid port parameter %d\n",
		       __func__, module);
		return -EINVAL;
	}

	if (length > APR_CHUNK_SIZE)
		mem_size = APR_CHUNK_SIZE;
	else
		mem_size = length;

	config = crus_gen_afe_set_header(size, port, module, param);
	if (config == NULL) {
		pr_err("%s: Memory allocation failed!\n", __func__);
		return -ENOMEM;
	}

	payload = (struct crus_external_config_t *)((u8 *)config +
			sizeof(struct afe_custom_crus_set_config_t));
	payload->total_size = (uint32_t)length;
	payload->reserved = 0;
	payload->config = PAYLOAD_FOLLOWS_CONFIG;
	    /* ^ This tells the algorithm to expect array */
	    /*   immediately following the header */

	/* Send config string in chunks of APR_CHUNK_SIZE bytes */
	while (sent < length) {
		chars_to_send = length - sent;
		if (chars_to_send > APR_CHUNK_SIZE) {
			chars_to_send = APR_CHUNK_SIZE;
			payload->done = 0;
		} else {
			payload->done = 1;
		}

		/* Configure per message parameter settings */
		memcpy(payload->data, data + sent, chars_to_send);
		payload->chunk_size = chars_to_send;

		/* Send the actual message */
		pr_debug("%s: Preparing to send apr packet.\n", __func__);
		ret = afe_apr_send_pkt_crus(config, index, 1);

		if (ret)
			pr_err("%s: crus set_param for port %d failed with code %d\n",
			       __func__, port, ret);
		else
			pr_debug("%s: crus set_param sent packet with param id 0x%08x to module 0x%08x.\n",
				 __func__, param, module);

		sent += chars_to_send;
	}

	kfree(config);
	return ret;
}

extern int crus_afe_callback(void *payload, int size)
{
	uint32_t *payload32 = payload;

	pr_info("Cirrus AFE CALLBACK: size = %d\n", size);

	switch (payload32[1]) {
	case CIRRUS_GB_FFPORT:
		memcpy(crus_gb_get_buffer, payload32, size);
		atomic_set(&crus_gb_get_param_flag, 1);
		break;
	default:
        pr_err("Cirrus AFE CALLBACK: ERROR: Invalid module parameter: %d", payload32[1]);
		return -EINVAL;
	}

	return 0;
}

int msm_routing_cirrus_fbport_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	pr_info("CRUS %s: cirrus_fb_port_ctl = %d", __func__, cirrus_fb_port_ctl);
	ucontrol->value.integer.value[0] = cirrus_fb_port_ctl;
	return 0;
}

int msm_routing_cirrus_fbport_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
    cirrus_fb_port_ctl = ucontrol->value.integer.value[0];
    pr_info("CRUS %s: cirrus_fb_port_ctl = %d", __func__, cirrus_fb_port_ctl);
    
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
        cirrus_fb_port = 0x1017;
        cirrus_ff_port = 0x1016;
        break;
    default:
        cirrus_fb_port_ctl = 4;
        cirrus_fb_port = 0x1017;
        cirrus_ff_port = 0x1016;
    }
    return 0;
}

static int msm_routing_crus_gb_enable(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	const int crus_set = ucontrol->value.integer.value[0];
    
    pr_info("CRUS %s: crus_set = %d", __func__, crus_set);

	if (crus_set > 255) {
		pr_err("Cirrus GB Enable: Invalid entry; Enter 0 to DISABLE, 1 to ENABLE\n");
		return -EINVAL;
	}

	switch (crus_set) {
	case 0: /* "Config GB Disable" */
		pr_info("Cirrus GB Enable: Config DISABLE\n");
		crus_enable.value = 0;
		crus_gb_enable = 0;
		break;
	case 1: /* "Config GB Enable" */
		pr_info("Cirrus GB Enable: Config ENABLE\n");
		crus_enable.value = 1;
		crus_gb_enable = 1;
		break;
	default:
	return -EINVAL;
	}

	mutex_lock(&crus_gb_lock);
	crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
			   CRUS_AFE_PARAM_ID_ENABLE,
			   sizeof(struct crus_single_data_t),
			   (void *)&crus_enable);
	mutex_unlock(&crus_gb_lock);

	mutex_lock(&crus_gb_lock);
	crus_afe_set_param(cirrus_fb_port, CIRRUS_GB_FBPORT,
			   CRUS_AFE_PARAM_ID_ENABLE,
			   sizeof(struct crus_single_data_t),
			   (void *)&crus_enable);
	mutex_unlock(&crus_gb_lock);

	return 0;
}

static int msm_routing_crus_gb_enable_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	pr_info("Starting Cirrus GB Enable Get function call, crus_gb_enable = %d\n", crus_gb_enable);
    
    pr_info("CRUS %s: crus_gb_enable = %d", __func__, crus_gb_enable);
    
    ucontrol->value.integer.value[0] = crus_gb_enable;

	return 0;
}

static int msm_routing_crus_gb_config(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	const int crus_set = ucontrol->value.integer.value[0];
	struct crus_single_data_t opalum_ctl;
    opalum_ctl.value = 1;

	pr_info("Starting Cirrus GB Config function call %d\n", crus_set);

	switch (crus_set) {
    case 0: /* Opalum: Music */
        opalum_ctl.value = 0;
        pr_info("CRUS path of Opalum for Music\n");
        // set opalum parameters
        crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
                           CRUS_PARAM_RX_SET_USECASE,
                           sizeof(struct crus_single_data_t),
                           (void *)&opalum_ctl);
        // ability to load ext config for music
        music_config_loaded = 0;
		break;
    case 1: /* Config + Opalum: Get Temp Cal */
		pr_info("CRUS GB Config: Get Temp Cal Config\n");
        // Start speaker calibration
        crus_afe_set_param(cirrus_fb_port, CIRRUS_GB_FBPORT,
                           CRUS_PARAM_TX_SET_CALIB,
                           sizeof(struct crus_single_data_t),
                           (void *)&opalum_ctl);
        crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
                           CRUS_PARAM_RX_SET_CALIB,
                           sizeof(struct crus_single_data_t),
                           (void *)&opalum_ctl);
        // wait for calibration done
        msleep(7000);
        // read calibration result
        crus_afe_get_param(cirrus_fb_port, CIRRUS_GB_FBPORT,
                           CRUS_PARAM_TX_GET_TEMP_CAL,
                           sizeof(struct crus_triple_data_t),
                           (void *)&crus_temp_cal);
        // print result
        pr_info("CRUS_GB_CONFIG: temp cal complete: Temp-acc = %d, Count = %d, Ambient = %d\n",
                crus_temp_cal.data1, crus_temp_cal.data2, crus_temp_cal.data3);
		break;
	case 2: /* Config + Opalum: Set Temp Cal */
		pr_info("CRUS GB Config: Set Temp Calibration\n");

		// Check calibration values.
		// Calibration failed if:
		//   1) temp-acc > 335623
		//   2) count != 16
        if (crus_temp_acc < 335623 && crus_count == 16) {
            crus_temp_cal.data1 = crus_temp_acc;
        } else {
			pr_warn("CRUS_GB_CONFIG: Using default temp_acc... (temp_acc %d, count %d)\n",
					crus_temp_acc, crus_count);
            crus_temp_cal.data1 = 285000; // Set default temp_acc
        }

		crus_temp_cal.data2 = 16;     // Set default count
		crus_temp_cal.data3 = 28;     // Set default ambient
        
        crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
                           CRUS_PARAM_SET_TEMP_CAL,
                           sizeof(struct crus_triple_data_t),
                           (void *)&crus_temp_cal);
        pr_info("CRUS_GB_CONFIG: set temp cal with Temp-acc = %d, Count = %d, Ambient = %d\n",
                crus_temp_cal.data1, crus_temp_cal.data2, crus_temp_cal.data3);
        break;
    case 3: /* Config + Opalum: Get Current Temp */
		pr_info("CRUS GB Config: Get Current Temp\n");
        crus_afe_get_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
                           CRUS_PARAM_RX_GET_TEMP,
                           sizeof(struct crus_dual_data_t),
                           (void *)&crus_current_temp);
        pr_info("CRUS_GB_CONFIG: current temp updated; Temp-acc = %d, Count = %d\n",
                crus_current_temp.data1, crus_current_temp.data2);
        break;
    case 4: /* Config + Opalum: Set F0 Cal */
		pr_info("CRUS GB Config: Set F0 Calibration\n");
        crus_afe_get_param(cirrus_fb_port, CIRRUS_GB_FBPORT,
                           CRUS_PARAM_SET_F0_CAL,
                           sizeof(struct crus_dual_data_t),
                           (void *)&crus_excursion);
        pr_info("CRUS_GB_GET_F0: crus_excursion.data1 = %d, crus_excursion.data2 = %d\n",
                crus_excursion.data1, crus_excursion.data2);
        // Store F0 for Opalum
        crus_f0 = crus_excursion.data1;
        break;
    case 5: /* Opalum: Voice */
        opalum_ctl.value = 1;
        pr_info("CRUS path of Opalum for Voice\n");
        // set opalum parameters
        crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
                           CRUS_PARAM_RX_SET_USECASE,
                           sizeof(struct crus_single_data_t),
                           (void *)&opalum_ctl);
        // ability to load ext config for voice
        voice_config_loaded = 0;
        break;
    case 6: /* Opalum: Dolby (Dolbit Audio) */
        opalum_ctl.value = 2;
        pr_info("CRUS path of Opalum for Dolby (Dolbit Audio)\n");
        // set opalum parameters
        crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
                           CRUS_PARAM_RX_SET_USECASE,
                           sizeof(struct crus_single_data_t),
                           (void *)&opalum_ctl);
        break;
	default:
        pr_err("CRUS GB Config: Invalid entry %d; Enter 0 for DEFAULT, 1 for RUN_DIAGNOSTICS, 2 for SET_TEMP_CAL, 3 for GET_CURRENT_TEMP, 4 for SET_FO_CAL\n", crus_set);
		return -EINVAL;
	}

	return 0;
}

static int msm_routing_crus_gb_config_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	pr_info("Starting Cirrus GB Config Get function call\n");
	ucontrol->value.integer.value[0] = crus_config_set;
    pr_info("CRUS %s: crus_config_set = %d", __func__, crus_config_set);

	return 0;
}

static int msm_routing_crus_ext_config(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	const int crus_set = ucontrol->value.integer.value[0];
	const struct firmware *firmware;
    int ret = 0;
    size_t fw_size;
    char *fw_data;
    
    crus_ext_config_set = crus_set;
    pr_info("Starting Cirrus GB External Config function call %d\n", crus_ext_config_set);
    
    switch (crus_ext_config_set) {
    case 0: // Default RX Config
        ret = request_firmware(&firmware, "crus_gb_config_default.bin", crus_gb_device);
        if (ret != 0) {
            pr_err("CRUS GB EXT Config: ERROR: CANNOT LOAD DEFAULT RX CONFIG, ERR CODE: %d", ret);
            break;
        }
        
        fw_size = firmware->size;
        fw_data = kmalloc(fw_size, GFP_KERNEL);
        pr_info("CRUS %s: length = %d; dataptr = %lx\n", __func__, (unsigned int)fw_size, (long unsigned int)firmware->data);
        memcpy(fw_data, firmware->data, fw_size);
        fw_data[fw_size] = '\0';
        
        if (music_config_loaded == 0) {
            music_config_loaded = 1;
            pr_info("CRUS GB EXT Config: Config RX Default\n");
            crus_afe_send_config(fw_data, fw_size, cirrus_ff_port, CIRRUS_GB_FFPORT);
            goto exit;
        }
        
        pr_info("CRUS GB EXT Config: Config RX Default ignored\n");
        goto exit;
        break;
    case 1: // Default TX Config (placeholder)
        ret = request_firmware(&firmware, "crus_gb_config_default.bin", crus_gb_device);
        if (ret != 0) {
            pr_err("CRUS GB EXT Config: ERROR: CANNOT LOAD DEFAULT TX CONFIG, ERR CODE: %d", ret);
            break;
        }
        
        fw_size = firmware->size;
        fw_data = kmalloc(fw_size, GFP_KERNEL);
        pr_info("CRUS %s: length = %d; dataptr = %lx\n", __func__, (unsigned int)fw_size, (long unsigned int)firmware->data);
        memcpy(fw_data, firmware->data, fw_size);
        fw_data[fw_size] = '\0';
        
        music_config_loaded = 1;
        pr_info("CRUS GB EXT Config: Config TX Default: function not implemented\n");
        goto exit;
        break;
    case 2: // New RX Config
        ret = request_firmware(&firmware, "crus_gb_config_new_rx.bin", crus_gb_device);
        if (ret != 0) {
            pr_err("CRUS GB EXT Config: ERROR: CANNOT LOAD NEW RX CONFIG, ERR CODE: %d", ret);
            break;
        }
        
        fw_size = firmware->size;
        fw_data = kmalloc(fw_size, GFP_KERNEL);
        pr_info("CRUS %s: length = %d; dataptr = %lx\n", __func__, (unsigned int)fw_size, (long unsigned int)firmware->data);
        memcpy(fw_data, firmware->data, fw_size);
        fw_data[fw_size] = '\0';
        
        if (voice_config_loaded == 0) {
            voice_config_loaded = 1;
            pr_info("CRUS GB EXT Config: Config RX New\n");
            crus_afe_send_config(fw_data, fw_size, cirrus_ff_port, CIRRUS_GB_FFPORT);
            goto exit;
        }
        
        pr_info("CRUS GB EXT Config: Config RX New ignored\n");
        goto exit;
        break;
        break;
    case 3: // New TX Config
        ret = request_firmware(&firmware, "crus_gb_config_new_tx.bin", crus_gb_device);
        if (ret != 0) {
            pr_err("CRUS GB EXT Config: ERROR: CANNOT LOAD NEW TX CONFIG, ERR CODE: %d", ret);
            break;
        }
        
        fw_size = firmware->size;
        fw_data = kmalloc(fw_size, GFP_KERNEL);
        pr_info("CRUS %s: length = %d; dataptr = %lx\n", __func__, (unsigned int)fw_size, (long unsigned int)firmware->data);
        memcpy(fw_data, firmware->data, fw_size);
        fw_data[fw_size] = '\0';
        
        pr_info("CRUS GB EXT Config: Config TX New\n");
        crus_afe_send_config(fw_data, fw_size, cirrus_fb_port, CIRRUS_GB_FBPORT);
        goto exit;
        break;
    default:
        pr_err("CRUS GB External Config: Invalid entry %d; Enter 0 for RX_DEFAULT, 1 for TX_DEFAULT, 2 for RX_NEW, 3 for TX_NEW\n",
                crus_ext_config_set);
        return -EFAULT;
    }

exit:
    release_firmware(firmware);
    kfree(fw_data);
    
    return 0;
}

static int msm_routing_crus_ext_config_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	pr_info("Starting Cirrus GB External Config Get function call\n");
	ucontrol->value.integer.value[0] = crus_ext_config_set;
    
    pr_info("CRUS %s: crus_ext_config_set = %d", __func__, crus_ext_config_set);

	return 0;
}

static int cirrus_transfer_params(struct snd_kcontrol *kcontrol, 
                        struct snd_ctl_elem_value *ucontrol)
{
    pr_info("CRUS %s: transfer_params_value = %ld", __func__, ucontrol->value.integer.value[0]);
    
    crus_set_cal_parametes.data1 = ucontrol->value.integer.value[0];
    crus_set_cal_parametes.data2 = ucontrol->value.integer.value[1];
    crus_set_cal_parametes.data3 = ucontrol->value.integer.value[2];
    
    pr_info("CRUS PARAMS TRANSFER +++++++++++++  =%d, count=%d, ambient=%d \n",
        crus_set_cal_parametes.data1,
        crus_set_cal_parametes.data2,
        crus_set_cal_parametes.data3);
    
    if ( (crus_set_cal_parametes.data1 - 331201) > 73598
        || crus_set_cal_parametes.data2 != 16
        || (crus_set_cal_parametes.data3 - 21) > 7 ) {
        pr_info("CRUS wrong range for temp-acc=%d, count=%d, ambient=%d \n",
            crus_set_cal_parametes.data1,
            crus_set_cal_parametes.data2,
            crus_set_cal_parametes.data3);
        return 1;
    } else {
        crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
                           CRUS_PARAM_SET_TEMP_CAL,
                           sizeof(struct crus_triple_data_t),
                           &crus_set_cal_parametes);
        return 0;
    }
}

static int msm_routing_crus_chan_swap(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct crus_dual_data_t data;
	const int crus_set = ucontrol->value.integer.value[0];

	pr_debug("Starting Cirrus GB Channel Swap function call %d\n",
		 crus_set);

	switch (crus_set) {
	case 0: /* L/R */
		data.data1 = 1;
		break;
	case 1: /* R/L */
		data.data1 = 2;
		break;
	default:
		return -EINVAL;
	}

	data.data2 = cirrus_ff_chan_swap_dur;

	crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
			   CRUS_PARAM_RX_CHANNEL_SWAP,
			   sizeof(struct crus_dual_data_t), &data);

	cirrus_ff_chan_swap_sel = crus_set;

	return 0;
}

static int msm_routing_crus_chan_swap_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("Starting Cirrus GB Channel Swap Get function call\n");
	ucontrol->value.integer.value[0] = cirrus_ff_chan_swap_sel;

	return 0;
}

static int msm_routing_crus_chan_swap_dur(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	int crus_set = ucontrol->value.integer.value[0];

	pr_debug("Starting Cirrus GB Channel Swap Duration function call\n");

	if ((crus_set < 0) || (crus_set > MAX_CHAN_SWAP_SAMPLES)) {
		pr_err("%s: Value out of range (%d)\n", __func__, crus_set);
		return -EINVAL;
	}

	if (crus_set < MIN_CHAN_SWAP_SAMPLES) {
		pr_info("%s: Received %d, rounding up to min value %d\n",
			__func__, crus_set, MIN_CHAN_SWAP_SAMPLES);
		crus_set = MIN_CHAN_SWAP_SAMPLES;
	}

	cirrus_ff_chan_swap_dur = crus_set;

	return 0;
}

static int msm_routing_crus_chan_swap_dur_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("Starting Cirrus GB Channel Swap Duration Get function call\n");
	ucontrol->value.integer.value[0] = cirrus_ff_chan_swap_dur;

	return 0;
}

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
                        "Get Current Temp",
                        "Set Fo Cal",
                        "Path_for_Voice",
                        "Path_for_Dolby"};
                        
static const char * const crus_ext_text[] = {"Config RX Default",
					    "Config TX Default",
                        "Config RX New",
                        "Config TX New"};

static const char * const crus_chan_swap_text[] = {"LR", "RL"};

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

static const struct soc_enum crus_chan_swap_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, crus_chan_swap_text),
};

static const struct snd_kcontrol_new crus_mixer_controls[] = {
	SOC_ENUM_EXT("FBProtect Port", cirrus_fb_controls_enum[0],
	msm_routing_cirrus_fbport_get, msm_routing_cirrus_fbport_put),
	SOC_ENUM_EXT("CIRRUS GB ENABLE", crus_en_enum[0],
	msm_routing_crus_gb_enable_get, msm_routing_crus_gb_enable),
	SOC_ENUM_EXT("CIRRUS GB CONFIG", crus_gb_enum[0],
	msm_routing_crus_gb_config_get, msm_routing_crus_gb_config),
    SOC_ENUM_EXT("CIRRUS GB EXT CONFIG", crus_ext_enum[0],
	msm_routing_crus_ext_config_get, msm_routing_crus_ext_config),
    SOC_ENUM_EXT("Cirrus GB Channel Swap", crus_chan_swap_enum[0],
	msm_routing_crus_chan_swap_get, msm_routing_crus_chan_swap),
	SOC_SINGLE_EXT("Cirrus GB Channel Swap Duration", SND_SOC_NOPM, 0,
	MAX_CHAN_SWAP_SAMPLES, 0, msm_routing_crus_chan_swap_dur_get,
	msm_routing_crus_chan_swap_dur),
	SOC_SINGLE_MULTI_EXT("CIRRUS_TRANSFER_PARAMS", 0xFFFFFFFF, 0,
	0xFFFF, 0, 3, cirrus_transfer_params, NULL),
};

void msm_crus_pb_add_controls(struct snd_soc_platform *platform)
{
	crus_gb_device = platform->dev;

	if (crus_gb_device == NULL)
		pr_err("%s: platform->dev is NULL!\n", __func__);
	else
		pr_info("%s: platform->dev = %lx\n", __func__,
			 (unsigned long)crus_gb_device);
        
    snd_soc_add_platform_controls(platform, crus_mixer_controls, 5);
    return;
}

static long crus_gb_ioctl(struct file *f,
		unsigned int cmd, unsigned long arg)
{
    int port;
    unsigned int result;
    long resultLong;
    void *data;
    int param;
    uint32_t length;
    
	pr_info("%s@%d ++\n", __func__, __LINE__);

    if (arg == 0) {
        pr_err("CRUS %s: No data sent to driver!\n", __func__);
        return -EFAULT;
    }
    
    pr_info("CRUS %s: Command is %x\n", __func__, cmd);
    pr_info("CRUS %s: Argument is %lx\n", __func__, arg);
    
    result = copy_from_user(&arg, &crus_gb_hdr, sizeof(struct crus_gb_ioctl_header));
    if (result == 0) {
        length = crus_gb_hdr.data_length;
        data = kmalloc(crus_gb_hdr.data_length, GFP_KERNEL);
        if (cmd == 0) {
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
            
            result = copy_to_user(crus_gb_hdr.data, data, length);
            if (result != 0) {
                resultLong = 0xfffffff2;
                pr_err("CRUS %s: copy_to_user failed\n", __func__);
            }
        } else if (cmd == 1) {
            result = copy_from_user(crus_gb_hdr.data, data, length);
            if (result != 0) {
                resultLong = 0xfffffff2;
                pr_err("CRUS %s: copy_from_user failed\n", __func__);
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
            resultLong = 0;
        } else {
            resultLong = 0xffffffea;
            pr_err("CRUS %s: Invalid IOCTL, command = %d!\n", __func__, cmd);
        }
        kfree(data);
        goto exit;
    }
    resultLong = 0xfffffff2;
    pr_err("CRUS %s: copy_from_user failed\n", __func__);
exit:
    return resultLong;
}

static int crus_gb_open(struct inode *inode, struct file *f)
{
	int result = 0;

	pr_info("%s\n", __func__);

	atomic_inc(&crus_gb_misc_usage_count);
	return result;
}

static int crus_gb_release(struct inode *inode, struct file *f)
{
	int result = 0;

	pr_info("%s\n", __func__);

	atomic_dec(&crus_gb_misc_usage_count);
	pr_info("%s: ref count %d!\n", __func__,
		atomic_read(&crus_gb_misc_usage_count));

	return result;
}

static const struct file_operations crus_gb_fops = {
	.open = crus_gb_open,
	.release = crus_gb_release,
	.unlocked_ioctl = crus_gb_ioctl,
};

struct miscdevice crus_gb_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "CIRRUS_SPEAKER_PROTECTION",
	.fops = &crus_gb_fops,
};

static ssize_t opsl_pass_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    pr_info("CRUS opalum enter %s\n", __func__);
    return sprintf(buf, "%d\n", crus_pass);
}

static ssize_t opsl_f0_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    pr_info("CRUS opalum enter %s\n", __func__);
    return sprintf(buf, "%d\n", crus_f0);
}

static ssize_t opsl_ambient_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    pr_info("CRUS opalum enter %s\n", __func__);
    return sprintf(buf, "%d\n", crus_ambient);
}

static ssize_t opsl_count_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    pr_info("CRUS opalum enter %s\n", __func__);
    return sprintf(buf, "%d\n", crus_count);
}

static ssize_t opsl_temp_acc_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    pr_info("CRUS opalum enter %s\n", __func__);
    return sprintf(buf, "%d\n", crus_temp_acc);
}

static ssize_t opsl_f0_store(struct device *dev,
					struct device_attribute *attr, const char *buf,
					size_t count)
{
	int data;
    ssize_t ret = -EINVAL;
	
	if (!kstrtoint(buf, 0, &data)) {
		crus_f0 = data;
		pr_info("CRUS set f0 to %d\n", data);
		ret = count;
	}
	return ret;
}
static ssize_t opsl_ambient_store(struct device *dev,
					struct device_attribute *attr, const char *buf,
					size_t count)
{
	int data;
    ssize_t ret = -EINVAL;
	
	if (!kstrtoint(buf, 0, &data)) {
		crus_ambient = data;
		pr_info("CRUS %s: set ambient temperature to %d\n", __func__, data);
		ret = count;
	}
	return ret;
}
static ssize_t opsl_count_store(struct device *dev,
					struct device_attribute *attr, const char *buf,
					size_t count)
{
	int data;
    ssize_t ret = -EINVAL;
    
	if (!kstrtoint(buf, 0, &data)) {
		crus_count = data;
		pr_info("CRUS set count to %d\n", data);
		ret = count;
	}
	return ret;
}
static ssize_t opsl_temp_acc_store(struct device *dev,
					struct device_attribute *attr, const char *buf,
					size_t count)
{
	int data;
    ssize_t ret = -EINVAL;
	
	if (!kstrtoint(buf, 0, &data)) {
		crus_temp_acc = data;
		pr_info("CRUS set temp acc to %d\n", data);
		ret = count;
	}
	return ret;
}

static ssize_t opsl_cali_start(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct crus_single_data_t calData;
    calData.value = 1;
    
    msleep(2000);
    pr_info("CRUS Opalum calibration starts ... after delay\n");
    crus_afe_set_param(cirrus_fb_port, CIRRUS_GB_FBPORT,
			   CRUS_PARAM_TX_SET_CALIB,
			   sizeof(struct crus_single_data_t),
			   (void *)&calData);
    crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
			   CRUS_PARAM_RX_SET_CALIB,
			   sizeof(struct crus_single_data_t),
			   (void *)&calData);
    msleep(7000);
    crus_afe_get_param(cirrus_fb_port, CIRRUS_GB_FBPORT,
			   CRUS_PARAM_TX_GET_TEMP_CAL,
			   sizeof(struct crus_triple_data_t),
			   (void *)&crus_temp_cal);
    pr_info("CRUS_GB_CONFIG: temp cal complete: Temp-acc = %d, Count = %d, Ambient = %d\n",
           crus_temp_cal.data1, crus_temp_cal.data2, crus_temp_cal.data3);
    crus_temp_acc = crus_temp_cal.data1;
    crus_count = crus_temp_cal.data2;
    
    msleep(100);
    
    pr_info("Step 3 CRUS Opalum starts F0 Calibration\n");
    crus_afe_get_param(cirrus_fb_port, CIRRUS_GB_FBPORT,
			   CRUS_PARAM_SET_F0_CAL,
			   sizeof(struct crus_dual_data_t),
			   (void *)&crus_excursion);
    pr_info("CRUS_GB_GET_F0: crus_excursion.data1 = %d, crus_excursion.data2 = %d\n",
            crus_excursion.data1, crus_excursion.data2);
    crus_f0 = crus_excursion.data1;
    
    if ((crus_temp_acc - 0x3d937U < 0x145d0) && (crus_count == 0x10))
        crus_pass = (crus_excursion.data1 - 0x191U < 499 && crus_ambient < 0x1d);
    else
        crus_pass = 0;
    
    pr_info("%s: CRUS end of calibration temp-acc=%d, count=%d, ambient=%d, f0=%d, pass=%d \n",
            __func__, crus_temp_acc, crus_count, crus_ambient, crus_f0, crus_pass);
    
    return sprintf(buf, "%d", crus_pass);
}

static const struct device_attribute opalum_dev_attr[] = {
	__ATTR(temp-acc, 0600, opsl_temp_acc_show, opsl_temp_acc_store),
	__ATTR(count, 0600, opsl_count_show, opsl_count_store),
	__ATTR(ambient, 0600, opsl_ambient_show, opsl_ambient_store),
	__ATTR(f0, 0600, opsl_f0_show, opsl_f0_store),
	__ATTR(pass, 0400, opsl_pass_show, NULL),
	__ATTR(start, 0400, opsl_cali_start, NULL),
};

static struct bus_type opal_virt_bus = {
	.name = "opsl-cali",
	.dev_name = "opsl-cali",
};

static struct device opalum_virt = {
	.bus = &opal_virt_bus,
};

struct crus_gb_cali_data msm_cirrus_get_speaker_calibration_data(void)
{
	struct crus_gb_cali_data ret;
	char proinfo_data[30];
	struct file *fp;
	mm_segment_t old_fs;

	printk("%s: enter\n", __func__);

	// Initialize blank structure data
	ret.temp_acc = 0x9dead;
	ret.count = 0x9dead;
	ret.ambient = 0x9dead;
	ret.ret = -EINVAL;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	msleep(50);

	fp = filp_open("/dev/block/bootdevice/by-name/proinfo", 
					O_RDWR | O_CREAT, 0660);
	if (IS_ERR(fp)) {
		ret.ret = PTR_ERR(fp);
		printk("%s: Failed to open proinfo pseudo-file (ret = %d)\n", 
			__func__, ret.ret);
		set_fs(old_fs);
		return ret;
	}

	fp->f_pos = SPK_CAL_DATA_START_ADDR;

	if (!fp->f_op->read)
		ret.ret = (int)vfs_read(fp, proinfo_data, SPK_CAL_DATA_LENGTH, &fp->f_pos);
	else
		ret.ret = fp->f_op->read(fp, proinfo_data, SPK_CAL_DATA_LENGTH, &fp->f_pos);

	filp_close(fp, NULL);
	set_fs(old_fs);

	if (ret.ret < 0) {
		printk("%s: Failed to read calibration data from proinfo pseudo-file (ret = %d)\n", __func__, ret.ret);
		return ret;
	}

	ret.ret = sscanf(proinfo_data, "%d,%d,%d", &ret.temp_acc, &ret.count, &ret.ambient);
	if (ret.ret != 3) {
		printk("%s: Failed to parse calibration data (ret = %d, data = %s)\n", __func__, ret.ret, proinfo_data);
		return ret;
	}

	// sscanf successful return code is 3,
	// so set return code to 0 if all is right
	ret.ret = 0;

	printk("%s: exit\n", __func__);

	return ret;
}

int msm_cirrus_set_speaker_calibration_data(struct crus_gb_cali_data *cali)
{
	printk("%s: enter\n", __func__);

	if (cali->ret) {
		printk("%s: calibration data error\n", __func__);
		return cali->ret;
	}

	if (cali->temp_acc == 0x9dead || cali->count == 0x9dead || cali->ambient == 0x9dead) {
		printk("%s: calibration data values error\n", __func__);
		return -EINVAL;
	}

	crus_temp_acc = cali->temp_acc;
	crus_count = cali->count;
	crus_ambient = cali->ambient;

	printk("%s: exit\n", __func__);

	return 0;
}

static int __init crus_gb_init(void)
{
    int ret = 0;
    
	pr_info("CRUS_GB_INIT: function started (advanced debugging mode)\n");
	atomic_set(&crus_gb_get_param_flag, 0);
	atomic_set(&crus_gb_misc_usage_count, 0);
	mutex_init(&crus_gb_get_param_lock);
	mutex_init(&crus_gb_lock);

	pr_info("CRUS_GB_INIT: Opalum subsystem init...\n");
    ret = subsys_system_register(&opal_virt_bus, 0);
    if (ret == 0) {
        pr_info("CRUS_GB_INIT: registering opalum virt...\n");
        ret = device_register(&opalum_virt);
        if (ret != 0) {
            pr_err("CRUS_GB_INIT: error registering Opalum virt!");
            return ret;
        }
    } else {
        pr_err("CRUS_GB_INIT: error initializing Opalum sybsystem!");
        return ret;
    }
    
    pr_info("CRUS_GB_INIT: starting Opalum [0/6]\n");
    ret = device_create_file(&opalum_virt, &opalum_dev_attr[0]);
    if (ret < 0) {
        pr_err("CRUS_GB_INIT: error starting Opalum at stage 1/6!\n");
        return ret;
    }
    
    pr_info("CRUS_GB_INIT: starting Opalum [1/6], last exit code: %d\n", ret);
    ret = device_create_file(&opalum_virt, &opalum_dev_attr[1]);
    if (ret < 0) {
        pr_err("CRUS_GB_INIT: error starting Opalum at stage 2/6!\n");
        device_remove_file(&opalum_virt, &opalum_dev_attr[0]);
        return ret;
    }
    
    pr_info("CRUS_GB_INIT: starting Opalum [2/6], last exit code: %d\n", ret);
    ret = device_create_file(&opalum_virt, &opalum_dev_attr[2]);
    if (ret < 0) {
        pr_err("CRUS_GB_INIT: error starting Opalum at stage 3/6!\n");
        device_remove_file(&opalum_virt, &opalum_dev_attr[0]);
        device_remove_file(&opalum_virt, &opalum_dev_attr[1]);
        return ret;
    }
    
    pr_info("CRUS_GB_INIT: starting Opalum [3/6], last exit code: %d\n", ret);
    ret = device_create_file(&opalum_virt, &opalum_dev_attr[3]);
    if (ret < 0) {
        pr_err("CRUS_GB_INIT: error starting Opalum at stage 4/6!\n");
        device_remove_file(&opalum_virt, &opalum_dev_attr[0]);
        device_remove_file(&opalum_virt, &opalum_dev_attr[1]);
        device_remove_file(&opalum_virt, &opalum_dev_attr[2]);
        return ret;
    }
    
    pr_info("CRUS_GB_INIT: starting Opalum [4/6], last exit code: %d\n", ret);
    ret = device_create_file(&opalum_virt, &opalum_dev_attr[4]);
    if (ret < 0) {
        pr_err("CRUS_GB_INIT: error starting Opalum at stage 5/6!\n");
        device_remove_file(&opalum_virt, &opalum_dev_attr[0]);
        device_remove_file(&opalum_virt, &opalum_dev_attr[1]);
        device_remove_file(&opalum_virt, &opalum_dev_attr[2]);
        device_remove_file(&opalum_virt, &opalum_dev_attr[3]);
        return ret;
    }
    
    pr_info("CRUS_GB_INIT: starting Opalum [5/6], last exit code: %d\n", ret);
    ret = device_create_file(&opalum_virt, &opalum_dev_attr[5]);
    if (ret < 0) {
        pr_err("CRUS_GB_INIT: error starting Opalum at stage 6/6!\n");
        device_remove_file(&opalum_virt, &opalum_dev_attr[0]);
        device_remove_file(&opalum_virt, &opalum_dev_attr[1]);
        device_remove_file(&opalum_virt, &opalum_dev_attr[2]);
        device_remove_file(&opalum_virt, &opalum_dev_attr[3]);
        device_remove_file(&opalum_virt, &opalum_dev_attr[4]);
        return ret;
    }
    
    pr_info("CRUS_GB_INIT: Opalum successfully started! Last exit code: %d\n", ret);
    pr_info("CRUS_GB_INIT: expecting new steps from mmi_speaker_calibration!\n");
    return 0;
}
module_init(crus_gb_init);

static void __exit crus_gb_exit(void)
{
	mutex_destroy(&crus_gb_get_param_lock);
	mutex_destroy(&crus_gb_lock);
}
module_exit(crus_gb_exit);
