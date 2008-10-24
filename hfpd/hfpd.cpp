/*
 * Software Bluetooth Hands-Free Implementation
 *
 * Copyright (C) 2008 Sam Revitch <samr7@cs.washington.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <assert.h>

#include <libhfp/bt.h>
#include <libhfp/hfp.h>
#include <libhfp/soundio.h>
#include <libhfp/events-indep.h>
#include <libhfp/list.h>

#include "dbus.h"
#include "util.h"
#include "configfile.h"
#include "objects.h"

using namespace libhfp;


static void
usage(const char *argv0)
{
	const char *bn;

	bn = strrchr(argv0, '/');
	if (!bn)
		bn = argv0;
	else
		bn++;

	fprintf(stdout,
"Usage: %s [-c <file>] [-y] [-f] [-E] [-S] [-d <level>] [-v <level>]\n"
"Available Options:\n"
"-c <file>	Specify local read/write settings file\n"
"-y		Attach to D-Bus system bus (default session bus)\n"
"-f		Run in foreground, do not daemonize\n"
"-E		Log to stderr\n"
"-S		Log to syslog\n"
"-d <level>	Log level:\n"
"		0: No log messages\n"
"		1: Severe errors only\n"
"		2: Warnings, severe errors\n"
#if !defined(NDEBUG)
"		3: Information, warnings, errors\n"
"		4: Detailed debug messages (DEFAULT)\n"
#else
"		3: Information, warnings, errors (DEFAULT)\n"
"		4: Detailed debug messages (DISABLED BY BUILD)\n"
#endif
"-v <level>	Elevate the priority of all syslog messages to <level>,\n"
"		to support debugging without reconfiguring syslogd.\n"
"		This value defaults to the specified log level\n"
		"\n",
		bn);
}

typedef DispatchInterface::logtype_t loglev_t;

int
main(int argc, char **argv)
{
	loglev_t loglevel, elevlevel;
	const char *cfgfile = 0;
	bool elev_set = false;
	bool do_dbus_system = false;
	bool do_foreground = false;
	bool do_syslog = false;
	bool do_stderr = false;
	int c;

#if !defined(NDEBUG)
	loglevel = DispatchInterface::EVLOG_DEBUG;
#else
	loglevel = DispatchInterface::EVLOG_INFO;
#endif
#if defined(USE_VERBOSE_DEBUG)
	elev_set = true;
	elevlevel = DispatchInterface::EVLOG_WARNING;
#endif

	opterr = 0;
	while ((c = getopt(argc, argv, "hH?c:yfESd:v:")) != -1) {
		switch (c) {
		case 'h':
		case 'H':
		case '?':
			usage(argv[0]);
			return 0;

		case 'c':
			cfgfile = optarg;
			break;
		case 'y':
			do_dbus_system = true;
			break;
		case 'f':
			do_foreground = 1;
			break;
		case 'E':
			do_stderr = true;
			break;
		case 'S':
			do_syslog = true;
			break;
		case 'd':
			loglevel = (loglev_t) strtol(optarg, NULL, 0);
		case 'v':
			elevlevel = (loglev_t) strtol(optarg, NULL, 0);
			elev_set = true;
			break;
		}
	}

	if (!elev_set)
		elevlevel = loglevel;

	if (!do_stderr && !do_syslog) {
		if (do_foreground)
			do_stderr = true;
		else
			do_syslog = true;
	}

	SyslogDispatcher disp;

	/* Until we daemonize, we always use stderr for logging */
	disp.SetSyslog(do_syslog, elevlevel);
	disp.SetStderr(true);
	disp.SetLevel(loglevel);

	DbusSession dbus(&disp);
	HandsFree hf(&disp, &dbus);

	/* DBUS_BUS_STARTER seems to default to the session bus */
	if (!dbus.Connect(do_dbus_system
			  ? DBUS_BUS_SYSTEM : DBUS_BUS_STARTER)) {
		fprintf(stderr,
			"Could not connect to D-Bus.  "
			"Is dbus-daemon running?\n"
			"hfpd aborting\n");
		return 1;
	}

	if (!hf.Init(cfgfile)) {
		fprintf(stderr,
			"Could not initialize hands-free subsystem\n"
			"hfpd aborting\n");
		return 1;
	}

	if (!dbus.AddUniqueName(HFPD_SERVICE_NAME)) {
		fprintf(stderr,
			"Could not acquire D-Bus unique name.  "
			"Is another hfpd running?\n"
			"hfpd aborting\n");
		return 1;
	}

	/* Send log messages to D-Bus */
	disp.cb_LogExt.Register(&hf, &HandsFree::LogMessage);

	if (!do_foreground && !Daemonize())
		return 1;

	/* Maybe turn off stderr logging */
	disp.SetStderr(do_stderr);

	disp.Run();
	return 0;
}
