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

#include <linux/acpi.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include "msm-cirrus-playback.h"

extern int afe_apr_send_pkt_crus(void *data, int index, int set);

static struct device *crus_gb_device;
static atomic_t crus_gb_misc_usage_count;

static struct crus_single_data_t crus_enable;

static int32_t *crus_gb_get_buffer;
static atomic_t crus_gb_get_param_flag;
struct mutex crus_gb_get_param_lock;
struct mutex crus_gb_lock;
static int crus_gb_enable;
static int music_config_loaded;
static int cirrus_fb_port = AFE_PORT_ID_QUATERNARY_MI2S_TX;
static int cirrus_ff_port = AFE_PORT_ID_QUATERNARY_MI2S_RX;

void *crus_gen_afe_set_header(int length, int port, int module, int param) {
  struct afe_custom_crus_set_config_t *config = NULL;
  int size = sizeof(struct afe_custom_crus_set_config_t) + length;
  int index = afe_get_port_index(port);
  uint16_t payload_size = sizeof(struct afe_port_param_data_v2) + length;

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
  config->param.port_id = (uint16_t)port;
  config->param.payload_address_lsw = 0;
  config->param.payload_address_msw = 0;
  config->param.mem_map_handle = 0;
  /* max data size of the param_ID/module_ID combination */
  config->param.payload_size = payload_size;

  /* Set data section */
  config->data.module_id = (uint32_t)module;
  config->data.param_id = (uint32_t)param;
  config->data.reserved = 0; /* Must be set to 0 */
  /* actual size of the data for the module_ID/param_ID pair */
  config->data.param_size = length;

  return (void *)config;
}

int crus_afe_set_param(int port, int module, int param, int data_size,
                       void *data_ptr) {
  struct afe_custom_crus_set_config_t *config = NULL;
  int index = afe_get_port_index(port);
  int ret = 0;

  pr_info("%s: port = %d module = %d param = 0x%x data_size = %d\n", __func__,
          port, module, param, data_size);

  config = crus_gen_afe_set_header(data_size, port, module, param);
  if (config == NULL) {
    pr_err("%s: Memory allocation failed!\n", __func__);
    return -ENOMEM;
  }

  memcpy((u8 *)config + sizeof(struct afe_custom_crus_set_config_t),
         (u8 *)data_ptr, data_size);

  pr_info("%s: Preparing to send apr packet.\n", __func__);

  ret = afe_apr_send_pkt_crus(config, index, 1);
  if (ret) {
    pr_err("%s: crus set_param for port %d failed with code %d\n", __func__,
           port, ret);
  } else {
    pr_info("%s: crus set_param sent packet with param id 0x%08x to module "
            "0x%08x.\n",
            __func__, param, module);
  }

  kfree(config);
  return ret;
}

void *crus_gen_afe_get_header(int length, int port, int module, int param) {
  struct afe_custom_crus_get_config_t *config = NULL;
  int size = sizeof(struct afe_custom_crus_get_config_t);
  int index = afe_get_port_index(port);
  uint16_t payload_size = sizeof(struct afe_port_param_data_v2) + length;

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
  config->param.port_id = (uint16_t)port;
  config->param.payload_address_lsw = 0;
  config->param.payload_address_msw = 0;
  config->param.mem_map_handle = 0;
  config->param.module_id = (uint32_t)module;
  config->param.param_id = (uint32_t)param;
  /* max data size of the param_ID/module_ID combination */
  config->param.payload_size = payload_size;

  /* Set data section */
  config->data.module_id = (uint32_t)module;
  config->data.param_id = (uint32_t)param;
  config->data.reserved = 0; /* Must be set to 0 */
  /* actual size of the data for the module_ID/param_ID pair */
  config->data.param_size = length;

  return (void *)config;
}

int crus_afe_get_param(int port, int module, int param, int length,
                       void *data) {
  struct afe_custom_crus_get_config_t *config = NULL;
  int index = afe_get_port_index(port);
  int ret = 0;

  pr_info("%s: port = %d module = %d param = 0x%x length = %d\n", __func__,
          port, module, param, length);

  config = (struct afe_custom_crus_get_config_t *)crus_gen_afe_get_header(
      length, port, module, param);
  if (config == NULL) {
    pr_err("%s: Memory allocation failed!\n", __func__);
    return -ENOMEM;
  }

  pr_info("%s: Preparing to send apr packet\n", __func__);

  mutex_lock(&crus_gb_get_param_lock);
  atomic_set(&crus_gb_get_param_flag, 0);

  crus_gb_get_buffer = kzalloc(config->param.payload_size + 16, GFP_KERNEL);

  ret = afe_apr_send_pkt_crus(config, index, 0);
  if (ret)
    pr_err("%s: crus get_param for port %d failed with code %d\n", __func__,
           port, ret);
  else
    pr_info("%s: crus get_param sent packet with param id 0x%08x to module "
            "0x%08x.\n",
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

int crus_afe_send_config(const char *data, int32_t length, int32_t port,
                         int32_t module) {
  struct afe_custom_crus_set_config_t *config = NULL;
  struct crus_external_config_t *payload = NULL;
  int size = sizeof(struct crus_external_config_t);
  int ret = 0;
  int index = afe_get_port_index(port);
  uint32_t param = 0;
  int mem_size = 0;
  int sent = 0;
  int chars_to_send = 0;

  pr_info("%s: called with module_id = %x, string length = %d\n", __func__,
          module, length);

  /* Destination settings for message */
  if (port == cirrus_ff_port)
    param = CRUS_PARAM_RX_SET_EXT_CONFIG;
  else if (port == cirrus_fb_port)
    param = CRUS_PARAM_TX_SET_EXT_CONFIG;
  else {
    pr_err("%s: Received invalid port parameter %d\n", __func__, module);
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

  payload = (struct crus_external_config_t
                 *)((u8 *)config + sizeof(struct afe_custom_crus_set_config_t));
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
      pr_err("%s: crus set_param for port %d failed with code %d\n", __func__,
             port, ret);
    else
      pr_debug("%s: crus set_param sent packet with param id 0x%08x to module "
               "0x%08x.\n",
               __func__, param, module);

    sent += chars_to_send;
  }

  kfree(config);
  return ret;
}

int crus_afe_callback(void *payload, int size) {
  uint32_t *payload32 = payload;

  pr_info("Cirrus AFE CALLBACK: size = %d\n", size);

  switch (payload32[1]) {
  case CIRRUS_GB_FFPORT:
    memcpy(crus_gb_get_buffer, payload32, size);
    atomic_set(&crus_gb_get_param_flag, 1);
    break;
  default:
    pr_err("Cirrus AFE CALLBACK: ERROR: Invalid module parameter: %d",
           payload32[1]);
    return -EINVAL;
  }

  return 0;
}

static int msm_routing_crus_gb_enable(struct snd_kcontrol *kcontrol,
                                      struct snd_ctl_elem_value *ucontrol) {
  const int crus_set = ucontrol->value.integer.value[0];

  pr_info("CRUS %s: crus_set = %d", __func__, crus_set);

  if (crus_set > 255) {
    pr_err(
        "Cirrus GB Enable: Invalid entry; Enter 0 to DISABLE, 1 to ENABLE\n");
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
  crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT, CRUS_AFE_PARAM_ID_ENABLE,
                     sizeof(struct crus_single_data_t), (void *)&crus_enable);
  mutex_unlock(&crus_gb_lock);

  mutex_lock(&crus_gb_lock);
  crus_afe_set_param(cirrus_fb_port, CIRRUS_GB_FBPORT, CRUS_AFE_PARAM_ID_ENABLE,
                     sizeof(struct crus_single_data_t), (void *)&crus_enable);
  mutex_unlock(&crus_gb_lock);

  return 0;
}

static int msm_routing_crus_gb_enable_get(struct snd_kcontrol *kcontrol,
                                          struct snd_ctl_elem_value *ucontrol) {
  pr_info("Starting Cirrus GB Enable Get function call, crus_gb_enable = %d\n",
          crus_gb_enable);

  pr_info("CRUS %s: crus_gb_enable = %d", __func__, crus_gb_enable);

  ucontrol->value.integer.value[0] = crus_gb_enable;

  return 0;
}

static const char *const crus_en_text[] = {"Disable", "Enable"};

static const struct soc_enum crus_en_enum[] = {
    SOC_ENUM_SINGLE_EXT(2, crus_en_text),
};

static const struct snd_kcontrol_new crus_mixer_controls[] = {
    SOC_ENUM_EXT("Cirrus GB Enable", crus_en_enum[0],
                 msm_routing_crus_gb_enable_get, msm_routing_crus_gb_enable),
};

void msm_crus_pb_add_controls(struct snd_soc_platform *platform) {
  crus_gb_device = platform->dev;

  if (crus_gb_device == NULL)
    pr_err("%s: platform->dev is NULL!\n", __func__);
  else
    pr_info("%s: platform->dev = %lx\n", __func__,
            (unsigned long)crus_gb_device);

  snd_soc_add_platform_controls(platform, crus_mixer_controls, 1);
  return;
}

struct crus_gb_cali_data msm_cirrus_get_speaker_calibration_data(void) {
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

  fp = filp_open("/dev/block/bootdevice/by-name/proinfo", O_RDWR | O_CREAT,
                 0660);
  if (IS_ERR(fp)) {
    ret.ret = PTR_ERR(fp);
    printk("%s: Failed to open proinfo pseudo-file (ret = %d)\n", __func__,
           ret.ret);
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
    printk("%s: Failed to read calibration data from proinfo pseudo-file (ret "
           "= %d)\n",
           __func__, ret.ret);
    return ret;
  }

  ret.ret =
      sscanf(proinfo_data, "%d,%d,%d", &ret.temp_acc, &ret.count, &ret.ambient);
  if (ret.ret != 3) {
    printk("%s: Failed to parse calibration data (ret = %d, data = %s)\n",
           __func__, ret.ret, proinfo_data);
    return ret;
  }

  // sscanf successful return code is 3,
  // so set return code to 0 if all is right
  ret.ret = 0;

  printk("%s: exit\n", __func__);

  return ret;
}

int msm_cirrus_flash_tx_config(void) {
  char *data;
  const struct firmware *fw;
  int ret = 0;

  printk("%s: enter\n", __func__);

  ret = request_firmware(&fw, "crus_gb_config_new_tx.bin", crus_gb_device);
  if (ret != 0) {
    pr_err("%s: failed to load TX config: %d", __func__, ret);
    return ret;
  }

  data = kmalloc(fw->size, GFP_KERNEL);
  memcpy(data, fw->data, fw->size);
  data[fw->size] = '\0';
  pr_debug("%s: length = %d; data = %lx\n", __func__, (unsigned int)fw->size,
           (long unsigned int)fw->data);

  ret = crus_afe_send_config(data, fw->size, cirrus_fb_port, CIRRUS_GB_FBPORT);
  pr_debug("%s: ret: %d", __func__, ret);

  printk("%s: exit\n", __func__);

  release_firmware(fw);
  kfree(data);

  return 0;
}

int msm_cirrus_flash_rx_config(void) {
  char *data;
  const struct firmware *fw;
  int ret = 0;

  printk("%s: enter\n", __func__);

  ret = request_firmware(&fw, "crus_gb_config_default.bin", crus_gb_device);
  if (ret != 0) {
    pr_err("%s: failed to load RX config: %d\n", __func__, ret);
    return ret;
  }

  data = kmalloc(fw->size, GFP_KERNEL);
  memcpy(data, fw->data, fw->size);
  data[fw->size] = '\0';
  pr_debug("%s: length = %d; data = %lx\n", __func__, (unsigned int)fw->size,
           (long unsigned int)fw->data);

  // Protection for double load
  if (!music_config_loaded) {
    music_config_loaded = 1;
    ret =
        crus_afe_send_config(data, fw->size, cirrus_fb_port, CIRRUS_GB_FBPORT);
    pr_debug("%s: ret: %d\n", __func__, ret);
  } else {
    pr_warn("%s: skip config load\n", __func__);
  }

  printk("%s: exit\n", __func__);

  release_firmware(fw);
  kfree(data);

  return 0;
}

int msm_cirrus_write_speaker_calibration_data(struct crus_gb_cali_data *cali) {
  struct crus_single_data_t opalum_ctl;
  struct crus_triple_data_t cal_data;
  // struct crus_triple_data_t crus_temp_cal;

  printk("%s: enter\n", __func__);

  if (cali->ret) {
    printk("%s: calibration data error\n", __func__);
    return cali->ret;
  }

  if (cali->temp_acc == 0x9dead || cali->count == 0x9dead ||
      cali->ambient == 0x9dead) {
    printk("%s: calibration data values error\n", __func__);
    return -EINVAL;
  }

  cal_data.data1 = cali->temp_acc;
  cal_data.data2 = cali->count;
  cal_data.data3 = cali->ambient;

  /*opalum_ctl.value = 1;
  crus_afe_set_param(cirrus_fb_port, CIRRUS_GB_FBPORT,
                      CRUS_PARAM_TX_SET_CALIB,
                      sizeof(struct crus_single_data_t),
                      (void *)&opalum_ctl);
  crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
                      CRUS_PARAM_RX_SET_CALIB,
                      sizeof(struct crus_single_data_t),
                      (void *)&opalum_ctl);
  msleep(7000);
  crus_afe_get_param(cirrus_fb_port, CIRRUS_GB_FBPORT,
                      CRUS_PARAM_TX_GET_TEMP_CAL,
                      sizeof(struct crus_triple_data_t),
                      (void *)&crus_temp_cal);

  crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT,
                         CRUS_PARAM_SET_TEMP_CAL,
                         sizeof(struct crus_triple_data_t),
                         (void *)&cal_data);*/
  opalum_ctl.value = 0;

  crus_afe_set_param(cirrus_ff_port, CIRRUS_GB_FFPORT, CRUS_PARAM_OPALUM,
                     sizeof(struct crus_single_data_t), (void *)&opalum_ctl);

  printk("%s: exit\n", __func__);

  return 0;
}

static int __init crus_gb_init(void) {
  pr_info("%s: enter\n", __func__);
  atomic_set(&crus_gb_get_param_flag, 0);
  atomic_set(&crus_gb_misc_usage_count, 0);
  mutex_init(&crus_gb_get_param_lock);
  mutex_init(&crus_gb_lock);
  pr_info("%s: exit\n", __func__);
  return 0;
}
module_init(crus_gb_init);

static void __exit crus_gb_exit(void) {
  mutex_destroy(&crus_gb_get_param_lock);
  mutex_destroy(&crus_gb_lock);
}
module_exit(crus_gb_exit);
