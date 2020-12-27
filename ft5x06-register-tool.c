/*
 * Copyright (C) 2017, Boundary Devices <info@boundarydevices.com>
 *
 * SPDX-License-Identifier:      GPL-2.0+
 *
 * Tool to read/write FT5x06 touch controller firmware
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Documented registers */
#define ID_G_THGROUP		0x80
#define ID_G_THPEAK		0x81
#define ID_G_THCAL		0x82
#define ID_G_THWATER		0x83
#define ID_G_THTEMP		0x84
#define ID_G_CTRL		0x86
#define ID_G_TIME_ENTER_MONITOR	0x87
#define ID_G_PERIODACTIVE	0x88
#define ID_G_PERIODMONITOR	0x89
#define ID_G_AUTO_CLB_MODE	0xa0
#define ID_G_LIB_VERSION_H	0xa1
#define ID_G_LIB_VERSION_L	0xa2
#define ID_G_CIPHER		0xa3
#define ID_G_MODE		0xa4
#define ID_G_FIRMID		0xa6
#define ID_G_FT5201ID		0xa8
#define ID_G_ERR		0xa9
#define ID_G_CLB		0xaa
#define ID_G_B_AREA_TH		0xae
#define FT5x06_MAX_REG_OFFSET	0xae

/* Undocumented registers */
#define FT_FW_READ_REG		0x03
#define FT_REG_RESET_FW		0x07
#define FT_ERASE_APP_REG	0x61
#define FT_ERASE_PANEL_REG	0x63
#define FT_FLASH_STATUS		0x6a
#define FT_PARAM_READ_REG	0x85
#define FT_READ_ID_REG		0x90
#define FT_FW_START_REG		0xbf
#define FT_REG_ECC		0xcc
#define FT_RST_CMD_REG1		0xfc

/* Undocumented firmware update values */
#define FT_UPGRADE_AA		0xAA
#define FT_UPGRADE_55		0x55
#define FT_UPGRADE_LOOP		30
#define FT_FW_MIN_SIZE		8
#define FT_FW_MAX_SIZE		64*1024
#define FT_FW_NAME_MAX_LEN	50
#define FT_MAX_TRIES		5
#define FT_RETRY_DLY		20
#define FT_MAX_WR_BUF		10
#define FT_MAX_RD_BUF		2
#define FT_FW_PKT_LEN		128
#define FT_FW_PKT_READ_LEN	256
#define FT_FW_PKT_META_LEN	6
#define FT_FW_PKT_DLY_MS	20

/* Chip ID that we consider correct */
#define FT5x06_ID	0x55
#define FT5x16_ID	0x0a
#define FT5x26_ID	0x54

/* Print macros */
#define LOG(fmt, arg...) fprintf(stdout, "[%s]: " fmt "\n" , __func__ , ## arg)
#define ERR(fmt, arg...) fprintf(stderr, "[%s]: " fmt "\n" , __func__ , ## arg)
#ifndef DEBUG
#define DBG(fmt, arg...) {}
#else
#define DBG(fmt, arg...) fprintf(stdout, "[%s]: " fmt "\n" , __func__ , ## arg)
#endif

static inline void msleep(int delay) { usleep(delay*1000); }

struct ft5x06_ts {
	int fd;
	uint8_t bus;
	uint8_t addr;
	uint8_t chip_id;
	uint8_t	fw_ver;
};

static int ft5x06_i2c_read(struct ft5x06_ts *ts, uint8_t *wrbuf, uint16_t wrlen,
			   uint8_t *rdbuf, uint16_t rdlen)
{
	struct i2c_rdwr_ioctl_data data;
	int ret;

	if (wrlen > 0) {
		struct i2c_msg msgs[] = {
			{ ts->addr, 0, wrlen, wrbuf },
			{ ts->addr, I2C_M_RD, rdlen, rdbuf },
		};
		data.msgs  = msgs;
		data.nmsgs = ARRAY_SIZE(msgs);
		ret = ioctl(ts->fd, I2C_RDWR, &data);
	} else {
		struct i2c_msg msgs[] = {
			{ ts->addr, I2C_M_RD, rdlen, rdbuf },
		};
		data.msgs  = msgs;
		data.nmsgs = ARRAY_SIZE(msgs);
		ret = ioctl(ts->fd, I2C_RDWR, &data);
	}

	if (ret < 0)
		ERR("Error %d", ret);

	return ret;
}

static int ft5x06_i2c_write(struct ft5x06_ts *ts, uint8_t *buf, uint16_t len)
{
	int ret;
	struct i2c_rdwr_ioctl_data data;
	struct i2c_msg msgs[] = {
		{ ts->addr, 0, len, buf },
	};

	data.msgs  = msgs;
	data.nmsgs = ARRAY_SIZE(msgs);

	ret = ioctl(ts->fd, I2C_RDWR, &data);
	if (ret < 0)
		ERR("Error %d", ret);

	return ret;
}

static void ft5x06_write_reg(struct ft5x06_ts *ts, uint8_t regnum, uint8_t value)
{
	uint8_t regnval[] = {
		regnum,
		value
	};

	ft5x06_i2c_write(ts, regnval, ARRAY_SIZE(regnval));
}

static void show_help(const char *name)
{
	printf
	    ("FT5x06 tool usage: %s [OPTIONS]\nOPTIONS:\n"
	     "\t-a, --address\n\t\tI2C address of the FT5x06 controller (hex). "
	     "Default is 0x38.\n"
	     "\t-b, --bus\n\t\tI2C bus the FT5x06 controller is on. "
	     "Default is 3.\n"
	     "\t-r, --read\n\t\tAddress to read from.\n"
	     "\t-w, --write\n\t\tAddress to write to.\n"
		 "\t-v, --value\n\t\tValue to write\n"
	     "\t-h, --help\n\t\tShow this help and exit.\n", name);
	return;
}

int main(int argc, const char *argv[])
{
	struct ft5x06_ts ts = {-1, 3, 0x38, 0xff, 0xff};
	int readaddr = -1, writeaddr = -1, writevalue = -1;
	char dev[16];
	uint8_t wbuf, rbuf;
	int ret;
	int arg_count = 1;

	/* Parse all parameters */
	while (arg_count < argc) {
		if ((strcmp(argv[arg_count], "-a") == 0)
		    || (strcmp(argv[arg_count], "--address") == 0)) {
			ts.addr = strtol(argv[++arg_count], NULL, 16);
		} else if ((strcmp(argv[arg_count], "-b") == 0)
			   || (strcmp(argv[arg_count], "--bus") == 0)) {
			ts.bus = strtol(argv[++arg_count], NULL, 10);
		} else if ((strcmp(argv[arg_count], "-r") == 0)
			   || (strcmp(argv[arg_count], "--read") == 0)) {
			readaddr = strtol(argv[++arg_count], NULL, 16);
		} else if ((strcmp(argv[arg_count], "-w") == 0)
			   || (strcmp(argv[arg_count], "--write") == 0)) {
			writeaddr = strtol(argv[++arg_count], NULL, 16);
		} else if ((strcmp(argv[arg_count], "-v") == 0)
			   || (strcmp(argv[arg_count], "--value") == 0)) {
			writevalue = strtol(argv[++arg_count], NULL, 16);
		} else {
			show_help(argv[0]);
			exit(1);
		}
		arg_count++;
	}

	sprintf(dev, "/dev/i2c-%d", ts.bus);
	DBG("Opening %s", dev);
	ts.fd = open(dev, O_RDWR);
	if (ts.fd < 0) {
		LOG("Couldn't open %s: %s", dev, strerror(errno));
		return ts.fd;
	}

	DBG("Setting addr to %#02x", ts.addr);
	ret = ioctl(ts.fd, I2C_SLAVE_FORCE, ts.addr);
	if (ret != 0) {
		LOG("Couldn't set slave addr: %s", strerror(errno));
		return -1;
	}

	if (writeaddr >= 0 && readaddr >= 0) {
		LOG("Received both read and write");
		goto end;
	} else if (writeaddr <= 0 && writevalue >= 0) {
		LOG("Didn't receive write address");
		goto end;
	} else if (writeaddr >= 0 && writevalue <= 0) {
		LOG("Didn't receive write value");
		goto end;
	}

	if (readaddr >= 0) {
		wbuf = readaddr;
		ret = ft5x06_i2c_read(&ts, &wbuf, 1, &rbuf, 1);

		LOG("%02x\n", rbuf);
	} else if (writeaddr >= 0) {
		ft5x06_write_reg(&ts, writeaddr, writevalue);

		LOG("%02x = %02x", writeaddr, writevalue);
	}
end:
	close(ts.fd);
	return 0;
}
