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

	void LogExt(DispatchInterface::logtype_t, const char *fmt, va_list ap);

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

/*
 * StringBuffer provides a printf() style interface on an automatically
 * expanding buffer.  It is used by a small set of internal services
 * including Introspect.
 */

class StringBuffer {
	char	*buf;
	size_t	nalloc;
	size_t	nused;

	bool Enlarge(void) {
		char *newbuf;
		size_t newsize;

		newsize = nalloc * 2;
		if (!newsize)
			newsize = 4096;
		if (buf)
			newbuf = (char *) realloc(buf, newsize);
		else {
			newbuf = (char *) malloc(newsize);
			if (newbuf)
				newbuf[0] = '\0';
		}

		if (!newbuf)
			return false;

		buf = newbuf;
		nalloc = newsize;
		return true;
	}

public:
	StringBuffer(void) : buf(0), nalloc(0), nused(0) {}
	~StringBuffer() { Clear(); }

	void Clear(void) {
		if (buf) {
			free(buf);
			buf = 0;
		}
		nalloc = 0;
		nused = 0;
	}

	char *Contents(void) const { return buf; }

	bool AppendFmtVa(const char *fmt, va_list ap) {
		int nlen;
		va_list cap;

		if ((!nalloc || (nalloc == nused)) && !Enlarge())
			return false;

		while (1) {
			va_copy(cap, ap);
			nlen = vsnprintf(&buf[nused], nalloc - nused,
					 fmt, cap);
			va_end(cap);
			if (nlen < 0)
				return false;

			if (nlen < (int) (nalloc - nused)) {
				nused += nlen;
				break;
			}

			if (!Enlarge())
				return false;
		}
		return true;
	}

	bool AppendFmt(const char *fmt, ...)
		__attribute__((format(printf, 2, 3))) {
		va_list ap;
		bool res;
		va_start(ap, fmt);
		res = AppendFmtVa(fmt, ap);
		va_end(ap);
		return res;
	}
};

#endif /* !defined(__HFPD_UTIL_H__) */
