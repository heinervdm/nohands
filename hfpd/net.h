/* -*- C++ -*- */
/*
 * Software Bluetooth Hands-Free Implementation
 *
 * Copyright (C) 2006-2008 Sam Revitch <samr7@cs.washington.edu>
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

#if !defined(__HFPD_NET_H__)
#define __HFPD_NET_H__

#include <sys/types.h>

#include <libhfp/events.h>
#include <libhfp/list.h>

class Server;
class Session;
class HandsFree;

class Session {
	friend class Server;

	typedef void (HandsFree::*delete_cb_t)(Session *);

	libhfp::ListItem	m_links;
	Server			*m_server;
	int			m_sock;
	libhfp::SocketNotifier	*m_not;
	delete_cb_t		m_delete_cb;
	libhfp::TimerNotifier	*m_unpause;

	bool			m_defunct;
	bool			m_pause;
	size_t			m_req_start;
	size_t			m_req_len;
	char			m_req_buf[512];

	libhfp::DispatchInterface *GetDi(void) const;

	void DataReady(libhfp::SocketNotifier *notp, int fh);
	ssize_t ParseLine(char *buf, size_t len);
	bool ParseCommand(char *buf);
	void DoUnpause(libhfp::TimerNotifier *timerp);

public:
	bool Write(void *buf, size_t len);
	bool vprintf(const char *fmt, va_list ap);
	bool printf(const char *fmt, ...) {
		va_list ap;
		bool result;
		va_start(ap, fmt);
		result = vprintf(fmt, ap);
		va_end(ap);
		return result;
	}

	void SetPause(bool pause);
	void SetDefunct(void);
	void SetDeleteCallback(delete_cb_t cb) { m_delete_cb = cb; }

	Session(Server *servp, int sock, libhfp::SocketNotifier *notp)
		: m_server(servp), m_sock(sock), m_not(notp),
		  m_delete_cb(0), m_unpause(0), m_defunct(false),
		  m_pause(false), m_req_start(0), m_req_len(0) {
		notp->Register(this, &Session::DataReady);
	}
	~Session() {
		if (m_not)
			delete m_not;
		if (m_sock)
			close(m_sock);
	}
};

class Server {
public:
	libhfp::ListItem		m_sessions;
	libhfp::ListItem		m_sessions_defunct;
	libhfp::DispatchInterface	*m_di;

	HandsFree			*m_handsfree;

	typedef bool (HandsFree::*dispatch_cb_t)(Session *,
						 int argc, char *const * argv);

	dispatch_cb_t			m_dispatch;

	struct Listener {
		libhfp::ListItem	links;
		int			sock;
		libhfp::SocketNotifier	*notifier;
	};

	libhfp::ListItem		m_listeners;

	libhfp::DispatchInterface *GetDi(void) const { return m_di; }

	bool InetListen(uint16_t port, bool non_loopback = false);
	bool UnixListen(const char *sockname);
	bool FinishListen(int sock);
	void SocketCleanup(void);

	void ListenNotify(libhfp::SocketNotifier *notp, int fh);
	void SessionClosed(Session *sessp);
	void SessionDefunct(Session *sessp);
	void DefunctAll(void);
	void CleanSessions(void);

	void Broadcast(void *buf, size_t len);
	void vprintf(const char *fmt, va_list ap);
	void printf(const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}

	bool DispatchCommand(Session *sessp, int argc, char * const *argv);

	Server(libhfp::DispatchInterface *di, HandsFree *hf)
		: m_di(di), m_handsfree(hf), m_dispatch(0) {}
	~Server() {
		DefunctAll();
		SocketCleanup();
		CleanSessions();
	}
};

#endif /* !defined(__HFPD_NET_H__) */
