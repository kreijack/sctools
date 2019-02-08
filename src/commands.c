/**
 * sctools: Commands
 * Copyright (C) 2016 Tim Hentenaar.
 *
 * This code is licenced under the Simplified BSD License.
 * See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <hidapi/hidapi.h>
#include "rawhid_defs.h"
#include "hid_tokens.h"
#include "commands.h"

#define VER_PROTOCOL 0x0100
#define VER_SETTINGS 0x0101

static unsigned char buf[PACKET_LEN];
static unsigned char filebuf[BUFSIZ];

/* {{{ SIZE_MAX */
/**
 * \def SIZE_MAX
 *
 * Maximum value of a \a size_t since this constant
 * isn't guarenteed to be available. SSIZE_MAX is
 * required to be provided by limits.h, however.
 */
#ifndef SIZE_MAX
#if SSIZE_MAX == LONG_MAX
#define SIZE_MAX ULONG_MAX
#else
#define SIZE_MAX (SSIZE_MAX << 1)
#endif /* SSIZE_MAX == LONG_MAX */
#endif /* }}} */

/* {{{ send_report */
/**
 * Send a report to the device, and read the response.
 *
 * \param[in] dev     Device to send to
 * \param[in] report  Report number to send
 * \return 0 on success, -1 on error.
 */
static int send_report(hid_device *dev, unsigned char report)
{
	buf[0] = report;
	if (hid_write(dev, buf, PACKET_LEN) < 0)
		goto err;

	/* Read the response */
	memset(buf, 0, PACKET_LEN);
	if (hid_read_timeout(dev, buf, PACKET_LEN, 250) < 0)
		goto err;

	return 0;

err:
	return -1;
}
/* }}} */

/* {{{ do_boot */
/**
 * Cause the microcontroller to reboot to its bootloader.
 *
 * \param[in] dev  Device
 * \param[in| argc Argument count (unused)
 * \param[in] argv Arguments (unused)
 * \return 0 on success, -1 on error.
 */
static int do_boot(hid_device *dev, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	memset(buf, 0, PACKET_LEN);
	return send_report(dev, RQ_BOOT);
}
/* }}} */

/* {{{ do_info */
/**
 * Get device info.
 *
 * \param[in] dev  Device
 * \param[in| argc Argument count (unused)
 * \param[in] argv Arguments (unused)
 * \return 0 on success, -1 on error.
 */
static int do_info(hid_device *dev, int argc, char *argv[])
{
	int i;

	(void)argc;
	(void)argv;

	memset(buf, 0, PACKET_LEN);
	if (send_report(dev, RQ_INFO) || buf[0] != RC_OK)
		return -1;

	/* Parse the response */
	puts("\n---- Info ----");
	for (i = 1; i < PACKET_LEN; i += 3) {
		if (buf[i] == IC_END)
			break;

		switch(buf[i]) {
		case IC_CODE_VERSION:
			printf("Code Version: v%d.%02d\n",
			       buf[i + 1], buf[i + 2]);
		break;
		case IC_PROTOCOL_VERSION:
			printf("Protocol Version: v%d.%02d\n",
			       buf[i + 1], buf[i + 2]);
		break;
		case IC_CONFIG_MAX_VERSION:
			printf("Max Settings Version: v%d.%02d\n",
			       buf[i + 1], buf[i + 2]);
		break;
		case IC_CONFIG_VERSION:
			printf("Settings Version: v%d.%02d\n",
			        buf[i + 1], buf[i + 2]);
		break;
		case IC_RAM_SIZE:
			printf("SRAM Size: %d bytes\n",
			       buf[i + 1] + 256 * buf[i + 2]);
		break;
		case IC_RAM_FREE:
			printf("SRAM Free: %d bnytes\n",
			       buf[i + 1] + 256 * buf[i + 2]);
		break;
		case IC_EEPROM_SIZE:
			printf("EEPROM Size: %d bytes\n",
			       buf[i + 1] + 256 * buf[i + 2]);
		break;
		case IC_EEPROM_FREE:
			printf("EEPROM Free: %d bytes\n",
			       buf[i + 1] + 256 * buf[i + 2]);
		break;
		default:
			printf("Unknown info item: 0x%02x\n", buf[i]);
		}
	}

	return 0;
}
/* }}} */

/* {{{ do_read */
/**
 * Read the current configuration from EEPROM.
 *
 * \param[in] dev  Device
 * \param[in| argc Argument count (1)
 * \param[in] argv Arguments (file to write)
 * \return 0 on success, -1 on error.
 */
static int do_read(hid_device *dev, int argc, char *argv[])
{
	FILE *fp;
	size_t len, bytes_read = 0;

	memset(buf, 0, PACKET_LEN);
	if (argc != 1 || !argv[0] || send_report(dev, RQ_READ) ||
	    buf[0] != RC_OK)
		goto err;

	/* Get the data */
	memset(filebuf, 0, BUFSIZ);
	len = (size_t)(buf[2] << 8) | buf[1];
	printf("\n---- Read (%lu bytes) ----\n", len);
	fflush(stdout);

	while (bytes_read < len) {
		if (send_report(dev, RC_READY)) {
			fputs("Failed to send READY packet\n", stderr);
			goto err;
		}

		memcpy(filebuf + bytes_read, buf, PACKET_LEN);
		bytes_read += PACKET_LEN;

		if (send_report(dev, RC_OK) < 0) {
			fputs("Failed to acknowledge data packet\n", stderr);
			goto err;
		}
	}

	bytes_read = 0;
	if (send_report(dev, RC_COMPLETED) < 0) {
		fputs("Failed to send COMPLETED packet\n", stderr);
		goto err;
	}

	/* Write the file */
	printf("Writing to '%s'\n", argv[0]);
	if (!(fp = fopen(argv[0], "w+"))) {
		perror("failed to open file: ");
		goto err;
	}

	fputs("SC", fp);
	do {
		bytes_read += fwrite(filebuf + bytes_read, 1,
		                     len - bytes_read, fp);
	} while (bytes_read < len && !ferror(fp));

	if (ferror(fp))
		fputs("Error writing the file\n", stderr);
	else printf("%lu bytes written\n", bytes_read);
	fclose(fp);
	return 0;

err:
	return -1;
}
/* }}} */

/* {{{ do_write */
/**
 * Write a configuration file to EEPROM.
 *
 * \param[in] dev  Device
 * \param[in| argc Argument count (1)
 * \param[in] argv Arguments (file to read)
 * \return 0 on success, -1 on error.
 */
static int do_write(hid_device *dev, int argc, char *argv[])
{
	FILE *fp = NULL;
	size_t i, len, bytes_in = 4, bytes_out = 4, max_len = 0;

	if (argc != 1|| !argv[0] || send_report(dev, RQ_INFO) ||
	    buf[0] != RC_OK)
		goto err;

	/* Check the version numbers, and EEPROM size */
	for (i = 1; i < PACKET_LEN; i += 3) {
		if (buf[i] == IC_END)
			break;

		switch(buf[i]) {
		case IC_PROTOCOL_VERSION:
			if (((buf[i + 1] << 8) | buf[i + 2]) < VER_PROTOCOL) {
				fprintf(stderr,
				        "Protocol version mismatch (%d.%02d)\n",
				        buf[i + 1], buf[i + 2]);
				goto err;
			}
		break;
		case IC_CONFIG_MAX_VERSION:
			if (((buf[i + 1] << 8) | buf[i + 2]) < VER_SETTINGS) {
				fprintf(stderr,
				        "Settings version mismatch (%d.%02d)\n",
				        buf[i + 1], buf[i + 2]);
				goto err;
			}
		break;
		case IC_EEPROM_SIZE:
			max_len = (size_t)(buf[i + 1] + 256 * buf[i + 2] - 6);
		break;
		}
	}

	if (!max_len) {
		fputs("Unable to determine EEPROM size\n", stderr);
		goto err;
	}

	/* Read in the file*/
	if (!(fp = fopen(argv[0], "r"))) {
		perror("Unable to open file: ");
		goto err;
	}

	/* Get the file size */
	fseek(fp, 0, SEEK_END);
	len = (size_t)ftell(fp);
	if (len == SIZE_MAX) {
		perror("Unable to get file size: ");
		goto err;
	}

	/* Ensure we have a header at least */
	if (len <= 4) {
		fprintf(stderr, "The file is too small (%lu bytes)\n", len);
		goto err;
	}

	/* Ensure it's not larger than the EEPROM */
	if (len - 2 > max_len) {
		fprintf(stderr,
		        "The file is larger than the EEPROM (%lu bytes).\n",
		        max_len);
		goto err;
	}

	/* Verify the header */
	len -= 2;
	rewind(fp);
	if (!(i = fread(filebuf, 1, 4, fp))) {
		fputs("Failed to read the file header\n", stderr);
		goto err;
	}

	if (filebuf[0] != 'S' || filebuf[1] != 'C') {
		fputs("Invalid file header\n", stderr);
		goto err;
	}

	if (((filebuf[2] << 8) | filebuf[3]) < VER_SETTINGS) {
		fprintf(stderr, "File version mismatch (%d.%02d)\n", filebuf[2],
		        filebuf[3]);
		goto err;
	}

	/* Tell the device to get ready */
	printf("\n---- Write (%lu bytes) ----\n", len);
	memset(buf, 0, PACKET_LEN);
	buf[1] = len & 0xff;
	buf[2] = (len >> 8) & 0xff;
	if (send_report(dev, RQ_WRITE) || buf[0] != RC_OK) {
		fputs("Failed to send WRITE packet\n", stderr);
		goto err;
	}

	/* Buffer in the data and write it out */
	do {
		if (!feof(fp) &&
		   !(i = fread(filebuf + bytes_in, 1, BUFSIZ - bytes_in, fp))) {
			fputs("Failed to read from file\n", stderr);
			goto err;
		}

		if (hid_read_timeout(dev, buf, PACKET_LEN + 1, 2500) < 0 ||
		    buf[0] != RC_READY) {
			fputs("Device not ready\n", stderr);
			goto err;
		} else printf("Device ready\n");

		max_len = 2 + (bytes_in - bytes_out) + i;
		max_len =(max_len > 60) ? 60 : max_len;
		buf[1] = max_len & 0xff;
		buf[2] = bytes_out & 0xff;
		buf[3] = (bytes_out >> 8) & 0xff;
		memcpy(buf + 4, filebuf + bytes_out - 2, max_len);
		if (send_report(dev, RQ_WRITE | RQ_CONTINUATION) ||
		    buf[0] != RC_OK) {
			fputs("Failed to write to device\n", stderr);
			goto err;
		}

		bytes_in  += i;
		bytes_out += max_len;
		printf("%lu / %lu bytes written\n", bytes_out - 4, len);
	} while (bytes_out < len && !ferror(fp));

	if (hid_read_timeout(dev, buf, PACKET_LEN + 1, 2500) < 0 ||
	    buf[0] != RC_COMPLETED) {
		fputs("Transfer not completed\n", stderr);
		goto err;
	} else puts("Transfer complete");

	return 0;

err:
	if (fp) fclose(fp);
	return -1;
}
/* }}} */

/* {{{ xlate_keys */
/**
 * Translate any key codes in the buffer to symbolic names.
 * \param[in] count Number of bytes in the buffer
 */
static void xlate_keys(int count)
{
	int i;
	unsigned char c;
	const char *token;
	long key;

	for (i = 0; i < count; i++) {
		/* A key event */
		if ((buf[i] == 'd' || buf[i] == 'u' || buf[i] == '-' ||
		     buf[i] == '+') && i + 3 < count) {
			c = buf[i + 3];
			buf[i + 3] = 0;
			key = strtol((const char *)(buf + i + 1),  NULL, 16);
			token = lookup_hid_token_by_value(key & 0xff);
			if (token) {
				printf("%c%c%c (%s) ", buf[i], buf[i + 1],
				       buf[i + 2], token);
				i += 2;
			} else {
				buf[i + 3] = c;
				putchar(buf[i]);
			}
		} else putchar(buf[i]);
	}

	fflush(stdout);
}
/* }}} */

/* {{{ do_listen */
/**
 * Listen for events from the device.
 *
 * \param[in] dev  Device
 * \param[in| argc Argument count (unused)
 * \param[in] argv Arguments (unused)
 * \return 0 on success, -1 on error.
 */
static int do_listen(hid_device *dev, int argc, char *argv[])
{
	int count;
	(void)argc;
	(void)argv;

	do {
		memset(buf, 0, PACKET_LEN);
		if ((count = hid_read_timeout(dev, buf, PACKET_LEN, 250)) < 0)
			goto err;
		xlate_keys(count);
	} while (1);

err:
	fputs("Unable to read from the device\n", stderr);
	return -1;
}
/* }}} */

#define N_COMMANDS 5
#define MIN_COMMAND_LEN 4
#define MAX_COMMAND_LEN 6

static const struct command {
	const char *name;
	size_t name_len;
	int argc;
	int usage_page;
	int usage;
	int interface;
	int (*proc)(hid_device *dev, int argc, char *argv[]);
} commands[N_COMMANDS] = {
	{ "boot",   4, 0, 0xff99, 0x2468, 3, do_boot,  },
	{ "info",   4, 0, 0xff99, 0x2468, 3, do_info,  },
	{ "read",   4, 1, 0xff99, 0x2468, 3, do_read   }, /* <output_file> */
	{ "write",  5, 1, 0xff99, 0x2468, 3, do_write  }, /* <input_file>  */
	{ "listen", 6, 0, 0xff31, 0x0074, 1, do_listen }
};

/* {{{ find_device */
/**
 * Find the converter
 *
 * \param[in] i Command index
 * \return the device handle, or NULL if the device can't be found.
 */
static hid_device *find_device(int i)
{
	int is_hidraw = 0;
	struct hid_device_info *devs = NULL, *cur_dev;
	hid_device *dev;

	/* Enumerate devices */
 	devs = hid_enumerate(SC_VID, SC_PID);
 	if (!devs) goto not_found;

 	cur_dev = devs;
 	do {
		/* XXX: hidapi's usage page info is worthless with hidraw */
		is_hidraw = strstr(cur_dev->path, "/dev/") != NULL;

		if (!is_hidraw &&
 		    cur_dev->usage      == commands[i].usage &&
 		    cur_dev->usage_page == commands[i].usage_page)
 			break;

 		/* Search by interface if we don't have the usage info */
 		if ((is_hidraw || (!cur_dev->usage && !cur_dev->usage_page)) &&
 		    cur_dev->interface_number == commands[i].interface)
 			break;
 		cur_dev = cur_dev->next;
 	} while (cur_dev);

	if (!cur_dev) goto not_found;
	if (!(dev = hid_open_path(cur_dev->path)))
		goto no_device;

	/* Free the enumeration data */
	hid_free_enumeration(devs);
	devs = NULL;
	return dev;

no_device:
	fputs("Unable to open device\n", stderr);
	goto err;

not_found:
	fputs("No devices found.\n", stderr);

err:
	if (devs) hid_free_enumeration(devs);
	return NULL;
}
/* }}} */

int run_command(int argc, char *argv[])
{
	int i = -1;
	size_t len;
	int retval = -EINVAL;
	hid_device *dev;

	if (argc < 1 || !argv || !argv[0])
		goto ret;

	/* Find the command */
	len = strlen(argv[0]);
	if (len < MIN_COMMAND_LEN || len > MAX_COMMAND_LEN)
		goto ret;

	while (++i < N_COMMANDS) {
		if (len != commands[i].name_len)
			continue;

		if (!memcmp(argv[0], commands[i].name, len))
			break;
	}

	/* Return if we didn't find the command */
	if (i >= N_COMMANDS)
		goto ret;

	/* Ensure we have sufficient args */
	if (argc - 1 < commands[i].argc)
		goto ret;

	/* Now, look for the device and run the command. */
	if ((dev = find_device(i))) {
		retval = commands[i].proc(dev, argc - 1, &argv[1]);
		hid_close(dev);
	} else retval = 0;

ret:
	return retval;
}

