/**
 * sctools
 * Copyright (C) 2016 Tim Hentenaar.
 *
 * This code is licenced under the Simplified BSD License.
 * See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <hidapi/hidapi.h>
#include "rawhid_defs.h"
#include "commands.h"

static const char *usage =
	"Soarer's Converter Tool 1.0\n"
	"Usage: %s command [command options...]\n\n"
	"  Options:\n"
	"    -h                   Show this message.\n\n"
	"  Commands:\n"
	"     boot                Cause the device to reboot to bootloader\n"
	"     info                Get device info\n"
	"     listen              Listen for keypresses\n"
	"     read <output file>  Read the current config from EEPROM\n"
	"     write <input file>  Write the given file to EEPROM\n";

/**
 * Handle command-line switches.
 *
 * \return -1 on error, otherwise the position within \a argv where
 *        the actual command starts.
 */
static int parse_args(int argc, char *argv[])
{
	int n_args = 1;

	do {
		/* End of switches */
		if (argv[n_args][0] != '-')
			break;

		/* Show the help text (-h) */
		if (argv[n_args][1] == 'h')
			goto err;

		/* ... or --help */
		if (argv[n_args][1] == '-' && argv[n_args][2] == 'h')
			goto err;
	} while (++n_args < argc);
	return n_args;

err:
	return -1;
}

static void do_usage(const char *progname);

int main(int argc, char *argv[])
{
	int n_args, retval = 0;

	if (argc == 1 || (n_args = parse_args(argc, argv)) < 0)
		goto show_usage;
	if (n_args) argc -= n_args;

	puts("Soarer's Converter Tool v1.0");
	hid_init();

	/* Do command */
	retval = run_command(argc, &argv[n_args]);
	if (retval == -EINVAL) {
		if (argv[n_args]) fprintf(stderr, "%s: ", argv[n_args]);
		fputs("invalid command\n", stderr);
	}

	hid_exit();
	return retval ? EXIT_FAILURE : EXIT_SUCCESS;

show_usage:
	do_usage(argv[0]);
	hid_exit();
	exit(EXIT_FAILURE);
}


/* {{{ GCC >= 3.x: ignore -Wformat-security here
 * We know that the contents of usage are okay, even if they
 * aren't passed as string literals.
 */
#ifdef __GNUC__
#if (__GNUC__ >= 3) && (((__GNUC__ * 100) + __GNUC_MINOR__) < 402)
#pragma GCC system_header
#endif /* GCC >= 3 && GCC <= 4.2 */
#if ((__GNUC__ * 100) + __GNUC_MINOR__) >= 406
#pragma GCC diagnostic push
#endif /* GCC >= 4.6 */
#if ((__GNUC__ * 100) + __GNUC_MINOR__) >= 402
#pragma GCC diagnostic ignored "-Wformat-security"
#endif /* GCC >= 4.2 */
#endif /* }}} */

/**
 * Show usage info.
 */
static void do_usage(const char *progname)
{
	printf(usage, progname);
}

/* {{{ GCC >= 4.6: restore -Wformat-security */
#ifdef __GNUC__
#if ((__GNUC__ * 100) + __GNUC_MINOR__) >= 406
#pragma GCC diagnostic pop
#endif /* GCC >= 4.6 */
#endif /* }}} */

