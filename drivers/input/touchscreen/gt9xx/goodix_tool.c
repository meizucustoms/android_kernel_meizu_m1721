/* drivers/input/touchscreen/goodix_tool.c
 *
 * 2010 - 2012 Goodix Technology.
 * Copyright (c) 2013-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be a reference 
 * to you, when you are integrating the GOODiX's CTP IC into your system, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
 * General Public License for more details.
 * 
 * Version: 2.4.0.1
 * Release Date: 2016/10/26
 */

   
#include "gt9xx.h"

#define DATA_LENGTH_UINT    512
#define CMD_HEAD_LENGTH     (sizeof(struct st_cmd_head))
static char procname[20] = {0};

struct st_cmd_head {
	u8  wr;		/* write read flag 0:R 1:W 2:PID 3: */
	u8  flag;	/* 0:no need flag/int 1: need flag  2:need int */
	u8 flag_addr[2];/* flag address */
	u8  flag_val;	/* flag val */
	u8  flag_relation; /* flag_val:flag 0:not equal 1:equal 2:> 3:< */
	u16 circle;	/* polling cycle */
	u8  times;	/* plling times */
	u8  retry;	/* I2C retry times */
	u16 delay;	/* delay before read or after write */
	u16 data_len;	/* data length */
	u8  addr_len;	/* address length */
	u8  addr[2];	/* address */
	u8  res[3];	/* reserved */
} __packed;

static struct st_cmd_head cmd_head;
static u8 *cmd_data;

static struct i2c_client *gt_client;

static struct proc_dir_entry *goodix_proc_entry;

static ssize_t goodix_tool_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t goodix_tool_write(struct file *, const char __user *, size_t, loff_t *);
static const struct file_operations tool_ops = {
    .owner = THIS_MODULE,
    .read = goodix_tool_read,
    .write = goodix_tool_write,
};

//static s32 goodix_tool_write(struct file *filp, const char __user *buff, unsigned long len, void *data);
//static s32 goodix_tool_read( char *page, char **start, off_t off, int count, int *eof, void *data );
static s32 (*tool_i2c_read)(u8 *, u16);
static s32 (*tool_i2c_write)(u8 *, u16);

#if GTP_ESD_PROTECT
extern void gtp_esd_switch(struct i2c_client *, s32);
#endif
s32 DATA_LENGTH = 0;
s8 IC_TYPE1[16] = "GT9XX";

static void tool_set_proc_name(char * procname)
{
    char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", 
        "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char date[20] = {0};
    char month[4] = {0};
    int i = 0, n_month = 1, n_day = 0, n_year = 0;
    
    sprintf(date, "%s", __DATE__);
    
    //GTP_DEBUG("compile date: %s", date);
    
    sscanf(date, "%s %d %d", month, &n_day, &n_year);
    
    for (i = 0; i < 12; ++i)
    {
        if (!memcmp(months[i], month, 3))
        {
            n_month = i+1;
            break;
        }
    }
    n_year = 2017;
    n_month = 6;
    n_day = 6;
    sprintf(procname, "gmnode%04d%02d%02d", n_year, n_month, n_day);
    //sprintf(procname, "goodix_tool");
    //GTP_DEBUG("procname = %s", procname);
}


static s32 tool_i2c_read_no_extra(u8* buf, u16 len)
{
    s32 ret = -1;
    s32 i = 0;
    struct i2c_msg msgs[2];
    
    msgs[0].flags = !I2C_M_RD;
    msgs[0].addr  = gt_client->addr;
    msgs[0].len   = cmd_head.addr_len;
    msgs[0].buf   = &buf[0];
    
    msgs[1].flags = I2C_M_RD;
    msgs[1].addr  = gt_client->addr;
    msgs[1].len   = len;
    msgs[1].buf   = &buf[GTP_ADDR_LENGTH];

    for (i = 0; i < cmd_head.retry; i++)
    {
        ret=i2c_transfer(gt_client->adapter, msgs, 2);
        if (ret > 0)
        {
            break;
        }
    }
    return ret;
}

static s32 tool_i2c_write_no_extra(u8* buf, u16 len)
{
    s32 ret = -1;
    s32 i = 0;
    struct i2c_msg msg;

    msg.flags = !I2C_M_RD;
    msg.addr  = gt_client->addr;
    msg.len   = len;
    msg.buf   = buf;

    for (i = 0; i < cmd_head.retry; i++)
    {
        ret=i2c_transfer(gt_client->adapter, &msg, 1);
        if (ret > 0)
        {
            break;
        }
    }
    return ret;
}

static s32 tool_i2c_read_with_extra(u8* buf, u16 len)
{
    s32 ret = -1;
    u8 pre[2] = {0x0f, 0xff};
    u8 end[2] = {0x80, 0x00};

    tool_i2c_write_no_extra(pre, 2);
    ret = tool_i2c_read_no_extra(buf, len);
    tool_i2c_write_no_extra(end, 2);

    return ret;
}

static s32 tool_i2c_write_with_extra(u8* buf, u16 len)
{
    s32 ret = -1;
    u8 pre[2] = {0x0f, 0xff};
    u8 end[2] = {0x80, 0x00};

    tool_i2c_write_no_extra(pre, 2);
    ret = tool_i2c_write_no_extra(buf, len);
    tool_i2c_write_no_extra(end, 2);

    return ret;
}

static void register_i2c_func(void)
{
//    if (!strncmp(IC_TYPE, "GT818", 5) || !strncmp(IC_TYPE, "GT816", 5) 
//        || !strncmp(IC_TYPE, "GT811", 5) || !strncmp(IC_TYPE, "GT818F", 6) 
//        || !strncmp(IC_TYPE, "GT827", 5) || !strncmp(IC_TYPE,"GT828", 5)
//        || !strncmp(IC_TYPE, "GT813", 5))
    if (strncmp(IC_TYPE1, "GT8110", 6) && strncmp(IC_TYPE1, "GT8105", 6)
        && strncmp(IC_TYPE1, "GT801", 5) && strncmp(IC_TYPE1, "GT800", 5)
        && strncmp(IC_TYPE1, "GT801PLUS", 9) && strncmp(IC_TYPE1, "GT811", 5)
        && strncmp(IC_TYPE1, "GTxxx", 5) && strncmp(IC_TYPE1, "GT9XX", 5))
    {
        tool_i2c_read = tool_i2c_read_with_extra;
        tool_i2c_write = tool_i2c_write_with_extra;
        GTP_DEBUG("I2C function: with pre and end cmd!");
    }
    else
    {
        tool_i2c_read = tool_i2c_read_no_extra;
        tool_i2c_write = tool_i2c_write_no_extra;
        GTP_INFO("I2C function: without pre and end cmd!");
    }
}

static void unregister_i2c_func(void)
{
    tool_i2c_read = NULL;
    tool_i2c_write = NULL;
    GTP_INFO("I2C function: unregister i2c transfer function!");
}

s32 init_wr_node(struct i2c_client *client)
{
    s32 i;

    gt_client = client;
    memset(&cmd_head, 0, sizeof(cmd_head));
    cmd_head.data = NULL;

    i = 5;
    while ((!cmd_head.data) && i)
    {
        cmd_head.data = kzalloc(i * DATA_LENGTH_UINT, GFP_KERNEL);
        if (NULL != cmd_head.data)
        {
            break;
        }
        i--;
    }
    if (i)
    {
        DATA_LENGTH = i * DATA_LENGTH_UINT + GTP_ADDR_LENGTH;
        GTP_INFO("Applied memory size:%d.", DATA_LENGTH);
    }
    else
    {
        GTP_ERROR("Apply for memory failed.");
        return FAIL;
    }

    cmd_head.addr_len = 2;
    cmd_head.retry = 5;

    register_i2c_func();

    tool_set_proc_name(procname);
    goodix_proc_entry = proc_create(procname, 0666, NULL, &tool_ops);
    if (goodix_proc_entry == NULL)
    {
        GTP_ERROR("Couldn't create proc entry!");
        return FAIL;
    }
    else
    {
        GTP_INFO("Create proc entry success!");
    }

    return SUCCESS;
}

void uninit_wr_node(void)
{
	cmd_data = NULL;
	unregister_i2c_func();
	proc_remove(goodix_proc_entry);
}

static u8 relation(u8 src, u8 dst, u8 rlt)
{
    u8 ret = 0;
    
    switch (rlt)
    {
    case 0:
        ret = (src != dst) ? true : false;
        break;

    case 1:
        ret = (src == dst) ? true : false;
        GTP_DEBUG("equal:src:0x%02x   dst:0x%02x   ret:%d.", src, dst, (s32)ret);
        break;

    case 2:
        ret = (src > dst) ? true : false;
        break;

    case 3:
        ret = (src < dst) ? true : false;
        break;

    case 4:
        ret = (src & dst) ? true : false;
        break;

    case 5:
        ret = (!(src | dst)) ? true : false;
        break;

    default:
        ret = false;
        break;    
    }

    return ret;
}

/*******************************************************    
Function:
    Comfirm function.
Input:
  None.
Output:
    Return write length.
********************************************************/
static u8 comfirm(void)
{
    s32 i = 0;
    u8 buf[32];
    
    memcpy(buf, cmd_head.flag_addr, cmd_head.addr_len);
   
    for (i = 0; i < cmd_head.times; i++)
    {
        if (tool_i2c_read(buf, 1) <= 0)
        {
            GTP_ERROR("Read flag data failed!");
            return FAIL;
        }
        if (true == relation(buf[GTP_ADDR_LENGTH], cmd_head.flag_val, cmd_head.flag_relation))
        {
            GTP_DEBUG("value at flag addr:0x%02x.", buf[GTP_ADDR_LENGTH]);
            GTP_DEBUG("flag value:0x%02x.", cmd_head.flag_val);
            break;
        }

        msleep(cmd_head.circle);
    }

    if (i >= cmd_head.times)
    {
        GTP_ERROR("Didn't get the flag to continue!");
        return FAIL;
    }

    return SUCCESS;
}

/*******************************************************    
Function:
    Goodix tool write function.
Input:
  standard proc write function param.
Output:
    Return write length.
********************************************************/
static ssize_t goodix_tool_write(struct file *filp, const char __user *userbuf,
						size_t count, loff_t *ppos)
{
	s32 ret = 0;

	mutex_lock(&lock);
	ret = copy_from_user(&cmd_head, userbuf, CMD_HEAD_LENGTH);
	if (ret) {
		dev_err(&gt_client->dev, "copy_from_user failed.");
		ret = -EACCES;
		goto exit;
	}

	dev_dbg(&gt_client->dev,
		"wr: 0x%02x, flag:0x%02x, flag addr:0x%02x%02x\n", cmd_head.wr,
		cmd_head.flag, cmd_head.flag_addr[0], cmd_head.flag_addr[1]);
	dev_dbg(&gt_client->dev,
		"flag val:0x%02x, flag rel:0x%02x,\n", cmd_head.flag_val,
		cmd_head.flag_relation);
	dev_dbg(&gt_client->dev, "circle:%u, times:%u, retry:%u, delay:%u\n",
		(s32) cmd_head.circle, (s32) cmd_head.times,
		(s32) cmd_head.retry, (s32)cmd_head.delay);
	dev_dbg(&gt_client->dev,
		"data len:%u, addr len:%u, addr:0x%02x%02x, write len: %u\n",
		(s32)cmd_head.data_len,	(s32)cmd_head.addr_len,
		cmd_head.addr[0], cmd_head.addr[1], (s32)count);

	if (cmd_head.data_len > (data_length - GTP_ADDR_LENGTH)) {
		dev_err(&gt_client->dev, "data len %u > data buff %d, rejected!\n",
			cmd_head.data_len, (data_length - GTP_ADDR_LENGTH));
		ret = -EINVAL;
		goto exit;
	}
	if (cmd_head.addr_len > GTP_ADDR_LENGTH) {
		dev_err(&gt_client->dev, "addr len %u > data buff %d, rejected!\n",
			cmd_head.addr_len, GTP_ADDR_LENGTH);
		ret = -EINVAL;
		goto exit;
	}

	if (cmd_head.wr == GTP_RW_WRITE) {
		ret = copy_from_user(&cmd_data[GTP_ADDR_LENGTH],
				&userbuf[CMD_HEAD_LENGTH], cmd_head.data_len);
		if (ret) {
			dev_err(&gt_client->dev, "copy_from_user failed.");
			goto exit;
		}

		memcpy(&cmd_data[GTP_ADDR_LENGTH - cmd_head.addr_len],
					cmd_head.addr, cmd_head.addr_len);

		if (cmd_head.flag == GTP_NEED_FLAG) {
			if (comfirm() ==  FAIL) {
				dev_err(&gt_client->dev, "Comfirm fail!");
				ret = -EINVAL;
				goto exit;
			}
		} else if (cmd_head.flag == GTP_NEED_INTERRUPT) {
			/* Need interrupt! */
		}
		if (tool_i2c_write(
		&cmd_data[GTP_ADDR_LENGTH - cmd_head.addr_len],
		cmd_head.data_len + cmd_head.addr_len) <= 0) {
			dev_err(&gt_client->dev, "Write data failed!");
			ret = -EIO;
			goto exit;
		}

		if (cmd_head.delay)
			msleep(cmd_head.delay);

		ret = cmd_head.data_len + CMD_HEAD_LENGTH;
		goto exit;
	} else if (cmd_head.wr == GTP_RW_WRITE_IC_TYPE) {  /* Write ic type */
		ret = copy_from_user(&cmd_data[0],
				&userbuf[CMD_HEAD_LENGTH],
				cmd_head.data_len);
		if (ret) {
			dev_err(&gt_client->dev, "copy_from_user failed.");
			goto exit;
		}

		if (cmd_head.data_len > sizeof(ic_type)) {
			dev_err(&gt_client->dev,
				"data len %u > data buff %zu, rejected!\n",
				cmd_head.data_len, sizeof(ic_type));
			ret = -EINVAL;
			goto exit;
		}
		memcpy(ic_type, cmd_data, cmd_head.data_len);

		register_i2c_func();

		ret = cmd_head.data_len + CMD_HEAD_LENGTH;
		goto exit;
	} else if (cmd_head.wr == GTP_RW_NO_WRITE) {
		ret = cmd_head.data_len + CMD_HEAD_LENGTH;
		goto exit;
	} else if (cmd_head.wr == GTP_RW_DISABLE_IRQ) { /* disable irq! */
		gtp_irq_disable(i2c_get_clientdata(gt_client));

		#if GTP_ESD_PROTECT
		gtp_esd_switch(gt_client, SWITCH_OFF);
		#endif
		ret = CMD_HEAD_LENGTH;
		goto exit;
	} else if (cmd_head.wr == GTP_RW_ENABLE_IRQ) { /* enable irq! */
		gtp_irq_enable(i2c_get_clientdata(gt_client));

		#if GTP_ESD_PROTECT
		gtp_esd_switch(gt_client, SWITCH_ON);
		#endif
		ret = CMD_HEAD_LENGTH;
		goto exit;
	} else if (cmd_head.wr == GTP_RW_CHECK_RAWDIFF_MODE) {
		struct goodix_ts_data *ts = i2c_get_clientdata(gt_client);

		ret = copy_from_user(&cmd_data[GTP_ADDR_LENGTH],
				&userbuf[CMD_HEAD_LENGTH], cmd_head.data_len);
		if (ret) {
			pr_debug("copy_from_user failed.");
			goto exit;
		}
		if (cmd_data[GTP_ADDR_LENGTH]) {
			pr_debug("gtp enter rawdiff.");
			ts->gtp_rawdiff_mode = true;
		} else {
			ts->gtp_rawdiff_mode = false;
			pr_debug("gtp leave rawdiff.");
		}
		ret = CMD_HEAD_LENGTH;
		goto exit;
	} else if (cmd_head.wr == GTP_RW_ENTER_UPDATE_MODE) {
		/* Enter update mode! */
		if (gup_enter_update_mode(gt_client) ==  FAIL) {
			ret = -EBUSY;
			goto exit;
		}
	} else if (cmd_head.wr == GTP_RW_LEAVE_UPDATE_MODE) {
		/* Leave update mode! */
		gup_leave_update_mode(gt_client);
	} else if (cmd_head.wr == GTP_RW_UPDATE_FW) {
		/* Update firmware! */
		show_len = 0;
		total_len = 0;
		if (cmd_head.data_len + 1 > data_length) {
			dev_err(&gt_client->dev, "data len %u > data buff %d, rejected!\n",
			cmd_head.data_len + 1, data_length);
			ret = -EINVAL;
			goto exit;
		}
		memset(cmd_data, 0, cmd_head.data_len + 1);
		memcpy(cmd_data, &userbuf[CMD_HEAD_LENGTH],
					cmd_head.data_len);

		if (gup_update_proc((void *)cmd_data) == FAIL) {
			ret = -EBUSY;
			goto exit;
		}
	}
	ret = CMD_HEAD_LENGTH;

exit:
	memset(&cmd_head, 0, sizeof(cmd_head));
	cmd_head.wr = 0xFF;

	mutex_unlock(&lock);
	return ret;
}

/*******************************************************    
Function:
    Goodix tool read function.
Input:
  standard proc read function param.
Output:
    Return read length.
********************************************************/
static ssize_t goodix_tool_read(struct file *file, char __user *user_buf,
					size_t count, loff_t *ppos)
{
	u16 data_len = 0;
	s32 ret;
	u8 buf[32];

	mutex_lock(&lock);
	if (cmd_head.wr & 0x1) {
		dev_err(&gt_client->dev, "command head wrong\n");
		ret = -EINVAL;
		goto exit;
	}

	switch (cmd_head.wr) {
	case GTP_RW_READ:
		if (cmd_head.flag == GTP_NEED_FLAG) {
			if (comfirm() == FAIL) {
				dev_err(&gt_client->dev, "Comfirm fail!");
				ret = -EINVAL;
				goto exit;
			}
		} else if (cmd_head.flag == GTP_NEED_INTERRUPT) {
			/* Need interrupt! */
		}

		memcpy(cmd_data, cmd_head.addr, cmd_head.addr_len);

		pr_debug("[CMD HEAD DATA] ADDR:0x%02x%02x.", cmd_data[0],
							cmd_data[1]);
		pr_debug("[CMD HEAD ADDR] ADDR:0x%02x%02x.", cmd_head.addr[0],
							cmd_head.addr[1]);

		if (cmd_head.delay)
			msleep(cmd_head.delay);

		data_len = cmd_head.data_len;
		if (data_len <= 0 || (data_len > data_length)) {
			dev_err(&gt_client->dev, "Invalid data length %d\n",
				data_len);
			ret = -EINVAL;
			goto exit;
		}
		if (data_len > count)
			data_len = count;

		if (tool_i2c_read(cmd_data, data_len) <= 0) {
			dev_err(&gt_client->dev, "Read data failed!\n");
			ret = -EIO;
			goto exit;
		}
		ret = simple_read_from_buffer(user_buf, count, ppos,
			&cmd_data[GTP_ADDR_LENGTH], data_len);
		break;
	case GTP_RW_FILL_INFO:
		ret = fill_update_info(user_buf, count, ppos);
		break;
	case GTP_RW_READ_VERSION:
		/* Read driver version */
		data_len = scnprintf(buf, sizeof(buf), "%s\n",
			GTP_DRIVER_VERSION);
		ret = simple_read_from_buffer(user_buf, count, ppos,
			buf, data_len);
		break;
	default:
		ret = -EINVAL;
		break;
	}

exit:
	mutex_unlock(&lock);
	return ret;
}

static const struct file_operations goodix_proc_fops = {
	.write = goodix_tool_write,
	.read = goodix_tool_read,
	.open = simple_open,
	.owner = THIS_MODULE,
};

s32 init_wr_node(struct i2c_client *client)
{
	u8 i;

	gt_client = client;
	memset(&cmd_head, 0, sizeof(cmd_head));
	cmd_data = NULL;

	i = GTP_I2C_RETRY_5;
	while ((!cmd_data) && i) {
		cmd_data = devm_kzalloc(&client->dev,
				i * DATA_LENGTH_UINT, GFP_KERNEL);
		if (cmd_data)
			break;
		i--;
	}
	if (i) {
		data_length = i * DATA_LENGTH_UINT;
		dev_dbg(&client->dev, "Applied memory size:%d.", data_length);
	} else {
		dev_err(&client->dev, "Apply for memory failed.");
		return FAIL;
	}

	cmd_head.addr_len = 2;
	cmd_head.retry = GTP_I2C_RETRY_5;

	register_i2c_func();

	mutex_init(&lock);
	tool_set_proc_name(procname);
	goodix_proc_entry = proc_create(procname,
			S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP,
			goodix_proc_entry,
			&goodix_proc_fops);
	if (goodix_proc_entry == NULL) {
		dev_err(&client->dev, "Couldn't create proc entry!");
		return FAIL;
	}

	return SUCCESS;
}
