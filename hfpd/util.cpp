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
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#ifdef  SIGTSTP
#include <sys/file.h>
#include <sys/ioctl.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>

#ifndef _PATH_TTY
#define _PATH_TTY "/dev/tty"
#endif

#include "util.h"


bool
Daemonize(void)
{
        int fd, pid;

        signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

#ifdef SIGTTOU
        signal(SIGTTOU, SIG_IGN);
#endif
#ifdef SIGTTIN
        signal(SIGTTIN, SIG_IGN);
#endif
#ifdef SIGTSTP
        signal(SIGTSTP, SIG_IGN);
#endif

	/*
	 * Now fork().  If pid comes back negative, fork
	 * failed and we report back to the caller.
	 * If pid comes back >0, we are in the original
	 * process and we exit.
	 */
        pid = fork();
	if (pid < 0)
		return false;
        if (pid > 0)
                exit(0);

#if defined(SIGTSTP) && defined(TIOCNOTTY)
        if(setpgid(0, getpid()) < 0)
                return false;
        fd = open(_PATH_TTY, O_RDWR);
	if (fd >= 0) {
                ioctl(fd, TIOCNOTTY, NULL);
                close(fd);
        }
#else  /* defined(SIGTSTP) && defined(TIOCNOTTY) */
        if (setpgrp() < 0)
		return false;
#endif  /* defined(SIGTSTP) && defined(TIOCNOTTY) */

	/* Replace stdin/out/err with /dev/null */
	fd = open("/dev/null", O_RDWR);
	if (fd >= 0) {
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		close(fd);
	}
	return true;
}


SyslogDispatcher::
SyslogDispatcher(void)
	: libhfp::IndepEventDispatcher(),
	  m_level(DispatchInterface::EVLOG_DEBUG),
	  m_stderr(false), m_syslog(false)
{
}

SyslogDispatcher::
~SyslogDispatcher()
{
	SetSyslog(false);
}

void SyslogDispatcher::
SetSyslog(bool enable, libhfp::DispatchInterface::logtype_t elevate)
{
	if (enable && !m_syslog) {
		openlog("hfpd", LOG_NDELAY|LOG_PID, LOG_DAEMON);
		m_syslog = true;
		m_syslog_elevate = elevate;
	}

	if (!enable && m_syslog) {
		closelog();
		m_syslog = false;
	}
}

void SyslogDispatcher::
DoLog(libhfp::DispatchInterface::logtype_t lt, const char *msg)
{
	libhfp::DispatchInterface::logtype_t syslog_lt;
	int syslog_prio;

	if (cb_LogExt.Registered())
		cb_LogExt(lt, msg);

	if (m_syslog) {
		syslog_lt = lt;
		if (m_syslog && (syslog_lt > m_syslog_elevate))
			syslog_lt = m_syslog_elevate;
	
		switch (syslog_lt) {
		case libhfp::DispatchInterface::EVLOG_ERROR:
			syslog_prio = LOG_ERR;
			break;
		case libhfp::DispatchInterface::EVLOG_WARNING:
			syslog_prio = LOG_WARNING;
			break;
		case libhfp::DispatchInterface::EVLOG_INFO:
			syslog_prio = LOG_INFO;
			break;
		case libhfp::DispatchInterface::EVLOG_DEBUG:
			syslog_prio = LOG_DEBUG;
			break;
		default:
			if (m_stderr)
				fprintf(stderr, "Invalid log level %d set\n",
					syslog_lt);
			if (m_syslog)
				syslog(LOG_ERR, "Invalid log level %d set\n",
				       syslog_lt);
			abort();
		}

		syslog(syslog_prio, "%s", (char *) msg);
	}
	if (m_stderr) {
		fprintf(stderr, "%s\n", (char *) msg);
	}
}

void SyslogDispatcher::
LogVa(libhfp::DispatchInterface::logtype_t lt, const char *fmt, va_list ap)
{
	char stackbuf[512];
	libhfp::StringBuffer sb(stackbuf, sizeof(stackbuf));
	char *cont;

	if (lt > m_level)
		return;

	if (!sb.AppendFmtVa(fmt, ap)) {
		DoLog(lt, "Memory exhausted writing to log");
		DoLog(lt, fmt);
		return;
	}

	cont = sb.Contents();

	/*
	 * Log messages using this interface should not be
	 * newline-terminated.
	 *
	 * The below code exists to attempt to make up for
	 * substandard clients.
	 */
	if (0) {
		size_t len;
		len = strlen(cont);
		while (len &&
		       ((cont[len - 1] == '\n') || (cont[len - 1] == '\r'))) {
			cont[len] = '\0';
			len--;
		}
	}

	if (cont && cont[0])
		DoLog(lt, cont);
}
