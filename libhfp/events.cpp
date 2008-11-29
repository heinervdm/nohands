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

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <libhfp/events.h>

namespace libhfp {

bool
SetNonBlock(int fh, bool nonblock)
{
	int flags = fcntl(fh, F_GETFL);
	if (nonblock) {
		if (flags & O_NONBLOCK) { return true; }
		flags |= O_NONBLOCK;
	} else {
		if (!(flags & O_NONBLOCK)) { return true; }
		flags &= ~O_NONBLOCK;
	}

	return (fcntl(fh, F_SETFL, flags) >= 0);
}


bool StringBuffer::
Enlarge(void)
{
	char *newbuf;
	size_t newsize;

	newsize = nalloc * 2;
	if (!newsize)
		newsize = 4096;
	if (buf && !notmybuf)
		newbuf = (char *) realloc(buf, newsize);
	else {
		newbuf = (char *) malloc(newsize);
		if (newbuf) {
			if (nalloc)
				strcpy(newbuf, buf);
			else
				newbuf[0] = '\0';
		}
	}

	if (!newbuf)
		return false;

	buf = newbuf;
	nalloc = newsize;
	notmybuf = false;
	return true;
}

void StringBuffer::
Clear(void)
{
	if (nalloc && !notmybuf) {
		assert(buf);
		free(buf);
		nalloc = 0;
	}
	nused = 0;
	buf = 0;
	notmybuf = true;
}

bool StringBuffer::
AppendFmtVa(const char *fmt, va_list ap)
{
	int nlen;
	va_list cap;

	if ((!nalloc || (nalloc == nused)) && !Enlarge())
		return false;

	while (1) {
		va_copy(cap, ap);
		nlen = vsnprintf(&buf[nused], nalloc - nused, fmt, cap);
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


static class ErrorInfoGlobal {
public:
	ErrorInfo::Container	e_nomem;

	ErrorInfoGlobal()
		: e_nomem(LIBHFP_ERROR_SUBSYS_EVENTS,
			  LIBHFP_ERROR_EVENTS_NO_MEMORY,
			  "Memory allocation failure")
		{}
	
} g_errors;

void ErrorInfo::
SetVa(uint16_t subsys, uint16_t code, const char *fmt, va_list ap)
{
	Container *contp;

	if (IsSet())
		abort();

	contp = new Container(subsys, code);
	if (!contp) {
		SetNoMem();
		return;
	}

	if (!contp->m_desc.AppendFmtVa(fmt, ap)) {
		delete contp;
		SetNoMem();
		return;
	}

	m_error = contp;
}

void ErrorInfo::
Clear(void)
{
	if (m_error) {
		if (m_error != &g_errors.e_nomem) {
			delete m_error;
		}
		m_error = 0;
	}
}

void ErrorInfo::
SetNoMem(void)
{
	if (IsSet())
		abort();

	m_error = &g_errors.e_nomem;
}

ErrorInfo::Container *ErrorInfo::
CopyContainer(const ErrorInfo::Container *src)
{
	Container *copy;

	assert(src);
	copy = new Container(src->m_subsys, src->m_code);
	if (!copy)
		return 0;
	if (!copy->m_desc.AppendFmt("%s", src->m_desc.Contents())) {
		delete copy;
		return 0;
	}

	return copy;
}

ErrorInfo &ErrorInfo::
operator=(const ErrorInfo &rhs)
{
	Clear();
	if (rhs.IsSet()) {
		m_error = CopyContainer(rhs.m_error);
		if (!m_error)
			SetNoMem();
	}

	return *this;
}

} /* namespace libhfp */
