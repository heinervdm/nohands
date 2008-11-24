/* -*- C++ -*- */
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

#if !defined(__HFPD_UTIL_H__)
#define __HFPD_UTIL_H__

#include <libhfp/events.h>
#include <libhfp/events-indep.h>

#include <stdio.h>
#include <stdlib.h>

extern bool Daemonize(void);

class SyslogDispatcher : public libhfp::IndepEventDispatcher {
	DispatchInterface::logtype_t	m_level;
	bool				m_stderr;
	bool				m_syslog;
	DispatchInterface::logtype_t	m_syslog_elevate;

	void DoLog(DispatchInterface::logtype_t lt, const char *msg);

public:
	SyslogDispatcher(void);
	virtual ~SyslogDispatcher(void);

	libhfp::Callback<void, DispatchInterface::logtype_t, const char *>
		cb_LogExt;

	void SetLevel(DispatchInterface::logtype_t lt) {  m_level = lt; }
	void SetStderr(bool enable) { m_stderr = enable; }
	void SetSyslog(bool enable, DispatchInterface::logtype_t elevate =
		       DispatchInterface::EVLOG_DEBUG);
	virtual void LogVa(DispatchInterface::logtype_t lt,
			   const char *fmt, va_list ap);
};

#endif /* !defined(__HFPD_UTIL_H__) */
