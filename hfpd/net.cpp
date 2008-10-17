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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <assert.h>

#include <libhfp/events-indep.h>
#include <libhfp/list.h>

#include "net.h"

using namespace libhfp;

namespace libhfp {
extern bool SetNonBlock(int fh, bool nonblock);
}

bool Server::
UnixListen(const char *sockname)
{
	int sock;
	sockaddr_un sa;

	(void) unlink(sockname);

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		GetDi()->LogWarn("Create UNIX socket: %d\n", errno);
		return false;
	}

	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, sockname, sizeof(sa.sun_path));

	if (bind(sock, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		GetDi()->LogWarn("Bind UNIX socket: %d\n", errno);
		close(sock);
		return false;
	}

	return FinishListen(sock);
}

bool Server::
InetListen(uint16_t port, bool non_loopback)
{
	int sock;
	unsigned long v = 1;
	sockaddr_in sa;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		GetDi()->LogWarn("Create TCP socket: %d\n", errno);
		return false;
	}

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)) < 0) {
		GetDi()->LogWarn("Set SO_REUSEADDR on socket: %d\n", errno);
		close(sock);
		return false;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(non_loopback
				   ? INADDR_ANY : INADDR_LOOPBACK);
	sa.sin_port = htons(port);

	if (bind(sock, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		GetDi()->LogWarn("Bind TCP socket: %d\n", errno);
		close(sock);
		return false;
	}

	return FinishListen(sock);
}

bool Server::
FinishListen(int sock)
{
	struct Listener *lp;

	if (listen(sock, 5) < 0) {
		GetDi()->LogWarn("Listen on socket: %d\n", errno);
		close(sock);
		return false;
	}

	if (!SetNonBlock(sock, true)) {
		GetDi()->LogWarn("Could not set socket nonblocking\n");
		close(sock);
		return false;
	}

	lp = new Listener;
	if (!lp) {
		GetDi()->LogWarn("Could not create listener structure\n");
		close(sock);
		return false;
	}

	lp->notifier = GetDi()->NewSocket(sock, false);
	if (!lp->notifier) {
		GetDi()->LogWarn("Could not create socket notifier\n");
		close(sock);
		return false;
	}

	lp->notifier->Register(this, &Server::ListenNotify);
	lp->sock = sock;
	m_listeners.AppendItem(lp->links);
	return true;
}

void Server::
SocketCleanup(void)
{
	Listener *lp;

	while (!m_listeners.Empty()) {
		lp = GetContainer(m_listeners.next, Listener, links);
		lp->links.Unlink();
		delete lp->notifier;
		close(lp->sock);
		delete lp;
	}
}

void Server::
ListenNotify(SocketNotifier *notp, int fh)
{
	int sock;
	ListItem *listp;
	Listener *lp;
	struct sockaddr addr;
	socklen_t len;
	SocketNotifier *anotp;
	Session *sessp;

	lp = 0;
	ListForEach(listp, &m_listeners) {
		lp = GetContainer(listp, Listener, links);
		if (lp->notifier == notp) {
			assert(lp->sock == fh);
			break;
		}
		lp = 0;
	}

	/* We better have the listener on our list */
	assert(lp);

	CleanSessions();

	len = sizeof(addr);
	sock = accept(lp->sock, &addr, &len);
	if (sock < 0)
		return;

	if (!SetNonBlock(sock, true)) {
		GetDi()->LogWarn("Could not set accepted socket nonblocking\n");
		close(sock);
		return;
	}

	anotp = GetDi()->NewSocket(sock, false);
	if (!anotp) {
		GetDi()->LogWarn("Could not create UNIX socket notifier\n");
		close(sock);
		return;
	}

	sessp = new Session(this, sock, anotp);
	if (!sessp) {
		GetDi()->LogWarn("Could not create session\n");
		close(sock);
		delete anotp;
		return;
	}
	m_sessions.AppendItem(sessp->m_links);
}

void Server::
SessionClosed(Session *sessp)
{
	if (sessp->m_delete_cb)
		(m_handsfree->*(sessp->m_delete_cb))(sessp);
	sessp->m_links.Unlink();
	delete sessp;
}

void Server::
SessionDefunct(Session *sessp)
{
	assert(sessp->m_defunct);
	sessp->m_links.UnlinkOnly();
	m_sessions_defunct.AppendItem(sessp->m_links);
}

void Server::
DefunctAll(void)
{
	Session *sessp;
	while (!m_sessions.Empty()) {
		sessp = GetContainer(m_sessions_defunct.next,
				     Session, m_links);
		assert(!sessp->m_defunct);
		sessp->SetDefunct();
	}
}

void Server::
CleanSessions(void)
{
	Session *sessp;
	while (!m_sessions_defunct.Empty()) {
		sessp = GetContainer(m_sessions_defunct.next,
				     Session, m_links);
		assert(sessp->m_defunct);
		SessionClosed(sessp);
	}
}

void Server::
Broadcast(void *buf, size_t len)
{
	ListItem *listp;
	Session *sessp;

	listp = m_sessions.next;
	while (listp != &m_sessions) {
		sessp = GetContainer(listp, Session, m_links);
		listp = listp->next;
		(void) sessp->Write(buf, len);
	}
}

void Server::
vprintf(const char *fmt, va_list ap)
{
	char buf[4096];
	int len;
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (len <= 0)
		return;
	Broadcast(buf, len);
}

bool Server::
DispatchCommand(Session *sessp, int argc, char * const *argv)
{
	return (m_handsfree->*(m_dispatch))(sessp, argc, argv);
}


DispatchInterface *Session::
GetDi(void) const
{
	return m_server->GetDi();
}

static bool ReadErrorFatal(int err) {
	return ((err != EAGAIN) &&
		(err != EINTR) &&
		(err != ENOMEM) &&
		(err != ENOBUFS));
}

void Session::
DataReady(SocketNotifier *notp, int fh)
{
	ssize_t ret, cons;

	assert(notp == m_not);
	assert(fh == m_sock);
	assert(!m_pause);
	assert(m_req_start + m_req_len <= sizeof(m_req_buf));

	ret = 0;
	if (m_req_start || (m_req_len != sizeof(m_req_buf))) {
		/* Fill m_req_buf, try to parse things */
		ret = read(m_sock, &m_req_buf[m_req_start],
			   sizeof(m_req_buf) - (m_req_start + m_req_len));

		if ((ret < 0) && (errno == EAGAIN)) {
			ret = 0;
		}
		else if (ret < 0) {
			GetDi()->LogWarn("Read from socket: %d\n", errno);

			/*
			 * Some errors do not indicate loss of connection
			 * Others do not indicate voluntary loss of connection
			 */
			if (ReadErrorFatal(errno))
				m_server->SessionClosed(this);
			return;
		}
		else if (ret == 0) {
			m_server->SessionClosed(this);
			return;
		}
	}

	assert((size_t)ret <= (sizeof(m_req_buf) - (m_req_start + m_req_len)));

	m_req_len += ret;

	/* Try to consume the buffer */
	do {
		cons = ParseLine(&m_req_buf[m_req_start], m_req_len);

		if (m_pause || m_defunct)
			goto done;

		if (!cons) {
			/* Don't tolerate lines that are too long */
			if ((m_req_start + m_req_len) == sizeof(m_req_buf)) {
				if (m_req_start == 0) {
					GetDi()->LogWarn("Line is too long\n");
					m_server->SessionClosed(this);
					return;
				}

				/* Compact the buffer */
				memmove(m_req_buf, &m_req_buf[m_req_start],
					m_req_len);
				m_req_start = 0;
			}

			goto done;
		}

		assert((size_t)cons <= m_req_len);

		if (cons == (ssize_t) m_req_len) {
			m_req_start = m_req_len = 0;
		} else {
			m_req_start += cons;
			m_req_len -= cons;
		}

	} while (m_req_len);

done:
	m_server->CleanSessions();
}


static bool IsWS(char c) { return ((c == ' ') || (c == '\t')); }
static bool IsNL(char c) { return ((c == '\r') || (c == '\n')); }

ssize_t Session::
ParseLine(char *buf, size_t len)
{
	size_t pos = 0;
	char c;

	/* Are we looking at white space?  Trim it! */
	c = buf[0];
	assert(c);
	assert(buf[len - 1]);
	if (IsWS(c) || IsNL(c)) {
		do {
			c = buf[++pos];
		} while ((pos < len) && (IsWS(c) || IsNL(c)));
		return pos;
	}

	/* Is there a newline anywhere in the buffer? */
	for (pos = 1; pos < len; pos++) {
		if (IsNL(buf[pos])) {
			buf[pos] = '\0';

			GetDi()->LogDebug("CMD>> %s\n", buf);

			if (!ParseCommand(buf))
				return 0;

			if (pos < (len - 1)) {
				assert(buf[len - 1]);
			}
			return pos + 1;
		}
	}

	/* No newline, nothing to consume */
	return 0;
}

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

bool Session::
ParseCommand(char *buf)
{
	char *saveptr = 0;
	unsigned int argc = 0;
	char *argv[16];

	argv[argc++] = strtok_r(buf, " \t\r\n", &saveptr);

	while (argv[argc - 1]) {
		if (argc >= ARRAY_SIZE(argv)) {
			GetDi()->LogWarn("Too many parameters\n");
			SetDefunct();
			return false;
		}
		argv[argc++] = strtok_r(NULL, " \t\r\n", &saveptr);
	}

	argc--;
	if (!argc)
		/* Empty line */
		return true;

	return m_server->DispatchCommand(this, argc, argv);
}

void Session::
DoUnpause(TimerNotifier *timerp)
{
	assert(timerp == m_unpause);
	assert(!m_pause);
	delete m_unpause;
	m_unpause = 0;

	/* Fake a readiness notification to encourage some line processing */
	DataReady(m_not, m_sock);
}

bool Session::
Write(void *buf, size_t len)
{
	sigset_t sigs;
	ssize_t res;

	if (m_defunct)
		return false;

	sigemptyset(&sigs);
	sigaddset(&sigs, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &sigs, &sigs);
	res = write(m_sock, buf, len);
	pthread_sigmask(SIG_SETMASK, &sigs, NULL);

	if ((res >= 0) && (res == (ssize_t) len))
		return true;

	SetDefunct();
	return false;
}

bool Session::
vprintf(const char *fmt, va_list ap)
{
	char buf[4096];
	int len;
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (len < 0)
		return false;

	if (len == 0)
		return true;

	return Write(buf, len);
}

void Session::
SetPause(bool pause)
{
	if (m_defunct)
		return;

	if (pause == m_pause)
		return;

	if (pause) {
		m_pause = true;
		if (m_not) {
			delete m_not;
			m_not = 0;
		}
		if (m_unpause) {
			m_unpause->Cancel();
			delete m_unpause;
			m_unpause = 0;
		}
		return;
	}

	m_pause = false;
	assert(!m_not);
	m_not = m_server->GetDi()->NewSocket(m_sock, false);
	if (!m_not) {
		SetDefunct();
		return;
	}
	m_not->Register(this, &Session::DataReady);

	m_unpause = m_server->GetDi()->NewTimer();
	if (!m_unpause) {
		SetDefunct();
		return;
	}
	m_unpause->Register(this, &Session::DoUnpause);
	m_unpause->Set(0);
}

void Session::
SetDefunct(void)
{
	if (!m_defunct) {
		m_defunct = true;
		m_server->SessionDefunct(this);
	}
}
