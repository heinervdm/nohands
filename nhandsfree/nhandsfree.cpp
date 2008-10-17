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
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>

#include <sstream>

#include <ndebug.h>
#include "nhandsfree.h"

using namespace std;


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

namespace nghost {

NHandsFree::
NHandsFree(void)
	: GenericAction(), m_ag_state(NHS_AG_INVALID),
	  m_ag_connect_sent(false), m_service(true), m_signal_bars(-1), 
	  m_call_state(NHS_CALL_NONE), m_call_waiting(false),
	  m_did_incoming_call_popup(false), m_incoming_call_popup_open(false),
	  m_voice_state(NHS_VOICE_DISCON),
	  m_cmd_head(0), m_cmd_tail(0),
	  m_sock(-1), m_rsp_start(0), m_rsp_len(0)
{
}

NHandsFree::
~NHandsFree()
{
	if (m_sock >= 0)
		ConnectionLost();
}

bool NHandsFree::
Init(void)
{
	m_ActionClass = "handsfree";
	return InetConnect(1234);
}

void NHandsFree::
UpdateIcon(int num, const char *image)
{
	ostringstream ost;
	ost << "screen:null changeImage \"handsfree" << num << "\" "
		"\"" << get_skin_dir("nghost", getOption("skin")) <<
		"/screens/images/hf_" << image << ".png\"";
	ActionClass ac(ost.str());
	outqueue->add(ac);
}

void NHandsFree::
IncomingCallPopUp(bool show)
{
	if (show && !m_incoming_call_popup_open) {
		ostringstream ost;
		ost << "popup:hf-incomingcall " << m_ActionClass << ":answer "
			"\"Incoming Call From:\"";
		if (m_callerid != "")
			ost << " \"" << m_callerid << "\"";
		else
			ost << " \"(Private Number)\"";
		ost << " \"Accept?\"";

		ActionClass ac(ost.str());
		outqueue->add(ac);
		m_incoming_call_popup_open = true;
	}

	if (!show && m_incoming_call_popup_open) {
		ActionClass ac("link:@prevscreen@");
		outqueue->add(ac);
		m_incoming_call_popup_open = false;
	}
}

void NHandsFree::
UpdateStateIcons(void)
{
	int lasticon = 2;

	switch (m_ag_state) {
	case NHS_AG_INVALID:
		UpdateIcon(1, "blank");
		break;
	case NHS_AG_NOBT:
		UpdateIcon(1, "nobt");
		break;
	case NHS_AG_NODEV:
		UpdateIcon(1, "nodev");
		break;
	case NHS_AG_DISCON:
	case NHS_AG_CONNECTING:
		UpdateIcon(1, "discon");
		break;
	case NHS_AG_CONN:
		UpdateIcon(1, "conn");
		goto connected;
	default:
		abort();
	}

	UpdateIcon(2, "blank");
	UpdateIcon(3, "blank");
	return;

connected:
	if (!m_service) {
		UpdateIcon(2, "noservice");
		lasticon = 3;
	}
	else if (m_signal_bars >= 0) {
		char buf[16];
		snprintf(buf, sizeof(buf), "service_%dbar", m_signal_bars);
		UpdateIcon(2, buf);
		lasticon = 3;
	}

	switch (m_call_state) {
	case NHS_CALL_NONE:
		UpdateIcon(lasticon, "blank");
		break;
	case NHS_CALL_CONN:
		UpdateIcon(lasticon,
			   (m_voice_state != NHS_VOICE_DISCON)
			   ? "call_conn_voice" : "call_conn");
		break;
	case NHS_CALL_ESTAB:
		UpdateIcon(lasticon,
			   (m_voice_state != NHS_VOICE_DISCON)
			   ? "call_estab_voice" : "call_estab");
		break;
	}

	if (lasticon == 2)
		UpdateIcon(lasticon + 1, "blank");
}

bool NHandsFree::
InetConnect(int port)
{
	int sock;
	sockaddr_in sa;

	assert(m_hfpd_state == NHS_HFPD_DISCONNECTED);
	assert(m_sock < 0);

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		ndebug::out(ndebug::LOG_ERR, "NHandsFree: %s:%s(%d) - "
			    "Error creating socket(%d) - %s",
			    __FILE__, __FUNCTION__, __LINE__,
			    errno, strerror(errno));
		return false;
	}

	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (!SetNonBlock(sock, true)) {
		ndebug::out(ndebug::LOG_ERR, "NHandsFree: %s:%s(%d) - "
			    "Error setting nonblock on socket(%d) - %s",
			    __FILE__, __FUNCTION__, __LINE__,
			    errno, strerror(errno));
		close(sock);
		return false;
	}

	if (connect(sock, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		if (errno == EINPROGRESS) {
			m_sock = sock;
			m_hfpd_state = NHS_HFPD_CONNECTING;
			return true;
		}

		ndebug::out(ndebug::LOG_ERR, "NHandsFree: %s:%s(%d) - "
			    "Error connecting socket(%d) - %s",
			    __FILE__, __FUNCTION__, __LINE__,
			    errno, strerror(errno));
		close(sock);
		return false;
	}

	m_hfpd_state = NHS_HFPD_CONNECTED;
	m_sock = sock;
	Connected();
	return true;
}

bool NHandsFree::
UnixConnect(const char *sockname)
{
	int sock;
	sockaddr_un sa;

	assert(m_hfpd_state == NHS_HFPD_DISCONNECTED);
	assert(m_sock < 0);

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		ndebug::out(ndebug::LOG_ERR, "NHandsFree: %s:%s(%d) - "
			    "Error creating socket(%d) - %s",
			    __FILE__, __FUNCTION__, __LINE__,
			    errno, strerror(errno));
		return false;
	}

	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, sockname, sizeof(sa.sun_path));

	if (!SetNonBlock(sock, true)) {
		ndebug::out(ndebug::LOG_ERR, "NHandsFree: %s:%s(%d) - "
			    "Error setting nonblock on socket(%d) - %s",
			    __FILE__, __FUNCTION__, __LINE__,
			    errno, strerror(errno));
		close(sock);
		return false;
	}

	if (connect(sock, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		if (errno == EINPROGRESS) {
			m_sock = sock;
			m_hfpd_state = NHS_HFPD_CONNECTING;
			return true;
		}

		ndebug::out(ndebug::LOG_ERR, "NHandsFree: %s:%s(%d) - "
			    "Error connecting socket(%d) - %s",
			    __FILE__, __FUNCTION__, __LINE__,
			    errno, strerror(errno));
		close(sock);
		return false;
	}

	m_hfpd_state = NHS_HFPD_CONNECTED;
	m_sock = sock;
	Connected();
	return true;
}

void NHandsFree::
ConnectNotify(void)
{
	assert(m_hfpd_state == NHS_HFPD_CONNECTING);

	m_hfpd_state = NHS_HFPD_CONNECTED;
	Connected();
	return;
}

void NHandsFree::
ConnectionLost(void)
{
	assert(m_hfpd_state != NHS_HFPD_DISCONNECTED);
	assert(m_sock >= 0);

	m_hfpd_state = NHS_HFPD_DISCONNECTED;
	close(m_sock);
	m_sock = -1;
	m_rsp_start = 0;
	m_rsp_len = 0;
	m_ag_state = NHS_AG_INVALID;
	m_call_state = NHS_CALL_NONE;
	m_ag_connect_sent = false;
	m_service = true;
	m_call_waiting = false;
	m_voice_state = NHS_VOICE_DISCON;
	m_did_incoming_call_popup = false;
	m_incoming_call_popup_open = false;
	m_signal_bars = -1;
	DropCommands();
	UpdateStateIcons();
}

void NHandsFree::
DataReady(void)
{
	ssize_t ret, cons;

	assert(m_hfpd_state == NHS_HFPD_CONNECTED);
	assert(m_rsp_start + m_rsp_len < sizeof(m_rsp_buf));

	/* Fill m_rsp_buf, try to parse things */
	ret = read(m_sock, &m_rsp_buf[m_rsp_start],
		   sizeof(m_rsp_buf) - (m_rsp_start + m_rsp_len));

	if (ret < 0) {
		if ((errno == EAGAIN) || (errno == EINTR))
			return;

		ndebug::out(ndebug::LOG_ERR, "NHandsFree: %s:%s(%d) - "
			    "Read from socket (%d) - %s",
			    __FILE__, __FUNCTION__, __LINE__,
			    errno, strerror(errno));
		ConnectionLost();
		return;
	}

	if (ret == 0) {
		ndebug::out(ndebug::LOG_ERR, "NHandsFree: %s:%s(%d) - "
			    "Connection closed remotely",
			    __FILE__, __FUNCTION__, __LINE__);
		ConnectionLost();
		return;
	}

	assert((size_t)ret <= (sizeof(m_rsp_buf) - (m_rsp_start + m_rsp_len)));

	m_rsp_len += ret;

	/* Try to consume the buffer */
	do {
		cons = ParseLine(&m_rsp_buf[m_rsp_start], m_rsp_len);
		if (cons < 0)
			return;

		if (!cons) {
			/* Don't tolerate lines that are too long */
			if ((m_rsp_start + m_rsp_len) == sizeof(m_rsp_buf)) {
				if (m_rsp_start == 0) {
					ndebug::out(ndebug::LOG_ERR,
						    "NHandsFree: %s:%s(%d) - "
						    "line is too long",
						    __FILE__, __FUNCTION__,
						    __LINE__);
					ConnectionLost();
					return;
				}

				/* Compact the buffer */
				memmove(m_rsp_buf, &m_rsp_buf[m_rsp_start],
					m_rsp_len);
				m_rsp_start = 0;
			}

			return;
		}

		assert((size_t)cons <= m_rsp_len);

		if (cons == (ssize_t) m_rsp_len) {
			m_rsp_start = m_rsp_len = 0;
		} else {
			m_rsp_start += cons;
			m_rsp_len -= cons;
		}

	} while (m_rsp_len);
}


static bool IsWS(char c) { return ((c == ' ') || (c == '\t')); }
static bool IsNL(char c) { return ((c == '\r') || (c == '\n')); }

ssize_t NHandsFree::
ParseLine(char *buf, size_t len)
{
	size_t pos = 0;
	char c;

	/* Are we looking at white space?  Trim it! */
	c = buf[0];
	assert(c);
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

			ndebug::out(ndebug::LOG_DEBUG,
				    "NHandsFree: << %s", buf);

			if (!ParseResponse(buf))
				return -1;

			return pos + 1;
		}
	}

	/* No newline, nothing to consume */
	return 0;
}


bool NHandsFree::
ParseResponse(const char *buf)
{
	NHandsFreeCmd *cmd;

	assert(m_hfpd_state == NHS_HFPD_CONNECTED);

	cmd = m_cmd_head;
	if (cmd && cmd->Response(this, buf)) {
		if (m_hfpd_state != NHS_HFPD_CONNECTED)
			return false;
		m_cmd_head = cmd->next;
		if (!m_cmd_head) {
			assert(m_cmd_tail == cmd);
			m_cmd_tail = 0;
		} else {
			(void) SendCommand(m_cmd_head);
		}
		delete cmd;
		return true;
	}

	if (!strcmp(buf, "OK") ||!strncmp(buf, "ERROR", 5)) {
		/* We didn't ask for this */
		ConnectionLost();
		return false;
	}


	if ((buf[0] != '+') || (buf[2] && (buf[2] != ' '))) {
		/* What the hell is this? */
		return true;
	}

	switch (buf[1]) {
	case 'A':
		/* Bluetooth address of the associated device */
		break;

	case 'B':
		/* Battery charge level */
		break;

	case 'C':
		/* Call state */
		if (!strncmp(&buf[3], "IDLE", 4)) {
			m_call_state = NHS_CALL_NONE;
		}
		else if (!strncmp(&buf[3], "CONNECTING", 10)) {
			m_call_state = NHS_CALL_CONN;
		}
		else if (!strncmp(&buf[3], "ESTAB", 5)) {
			m_call_state = NHS_CALL_ESTAB;
		}
		else {
			return true;
		}

		m_call_waiting = !strcmp(&buf[7], ":WAITING");

		if (!m_call_waiting) {
			if (m_did_incoming_call_popup)
				m_did_incoming_call_popup = false;
			IncomingCallPopUp(false);
			m_callerid = "";
		}
		break;

	case 'D':
		/* Device connection state */
		if (!strcmp(&buf[3], "STOP")) {
			m_ag_state = NHS_AG_NOBT;
		}
		else if (!strcmp(&buf[3], "NODEV")) {
			m_ag_state = NHS_AG_NODEV;
		}
		else if (!strcmp(&buf[3], "DISCON")) {
			m_ag_state = NHS_AG_DISCON;
		}
		else if (!strcmp(&buf[3], "CONNECTING")) {
			m_ag_state = NHS_AG_CONNECTING;
		}
		else if (!strcmp(&buf[3], "CONN")) {
			m_ag_state = NHS_AG_CONN;
		}
		break;

	case 'I':
		/* Caller ID */
		m_callerid = &buf[3];
		return true;

	case 'R':
		/* Ringing! */
		if (!m_did_incoming_call_popup) {
			m_did_incoming_call_popup = true;
			IncomingCallPopUp(true);
		}
		break;

	case 'S':
		if (!strcmp(&buf[3], "NOSERVICE")) {
			m_service = false;
		}
		else if (!strncmp(&buf[3], "SERVICE", 7)) {
			m_service = true;
			if ((buf[10] == ':') && !strcmp(&buf[12], "BARS")) {
				m_signal_bars = buf[11] - '0';
			}
		}
		else if (!strncmp(&buf[3], "ROAM", 4)) {
			m_service = true;
			if ((buf[7] == ':') && !strcmp(&buf[9], "BARS")) {
				m_signal_bars = buf[8] - '0';
			}
		}
		break;

	case 'V':
		if (!strncmp(&buf[3], "DISCON", 5)) {
			m_voice_state = NHS_VOICE_DISCON;
		}
		else if (!strcmp(&buf[3], "CONN")) {
			m_voice_state = NHS_VOICE_CONN;
		}
		else if (!strcmp(&buf[3], "CONN:MUTE")) {
			m_voice_state = NHS_VOICE_MUTE;
		}
		break;

	default:
		return true;
	}

	UpdateStateIcons();
	return true;
}

bool NHandsFree::
Write(void *buf, size_t len)
{
	ssize_t res;

	if (m_hfpd_state != NHS_HFPD_CONNECTED)
		return false;

	res = write(m_sock, buf, len);
	if ((res >= 0) && (res == (ssize_t) len))
		return true;

	ConnectionLost();
	return false;
}

bool NHandsFree::
vprintf(const char *fmt, va_list ap)
{
	char buf[4096];
	int len;
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (len < 0)
		return false;
	if (!len)
		return true;
	return Write(buf, len);
}

bool NHandsFree::
SendCommand(NHandsFreeCmd *cmd)
{
	return printf("%s\n", cmd->text);
}

bool NHandsFree::
QueueCommand(NHandsFreeCmd *cmd)
{
	if (m_hfpd_state != NHS_HFPD_CONNECTED)
		return false;

	cmd->next = 0;
	if (m_cmd_tail)
		m_cmd_tail->next = cmd;
	else
		m_cmd_head = cmd;
	m_cmd_tail = cmd;

	if (m_cmd_head == cmd)
		return SendCommand(cmd);

	return true;
}

void NHandsFree::
DropCommands(void)
{
	NHandsFreeCmd *cmd;

	while (m_cmd_head) {
		cmd = m_cmd_head;
		m_cmd_head = m_cmd_head->next;
		if (!m_cmd_head) {
			assert(m_cmd_tail == cmd);
			m_cmd_tail = 0;
		}
		delete cmd;
	}
}

struct NormalCommand : NHandsFreeCmd {
	virtual bool Response(NHandsFree *nhp, const char *line) {
		if (!strncmp(line, "ERROR", 5)) {
			nhp->NextCommand(this, false);
			return true;
		}
		if (!strcmp(line, "OK")) {
			nhp->NextCommand(this, true);
			return true;
		}
		return false;
	}
	NormalCommand(const char *intext) {
		strcpy(text, intext);
	}
};

struct AbortOnFailCommand : NormalCommand {
	virtual bool Response(NHandsFree *nhp, const char *line) {
		if (!strncmp(line, "ERROR", 5)) {
			nhp->AbortConnection();
			return true;
		}
		if (!strcmp(line, "OK")) {
			nhp->NextCommand(this, true);
			return true;
		}
		return false;
	}
	AbortOnFailCommand(const char *intext) : NormalCommand(intext) {}
};

void NHandsFree::
Connected(void)
{
	NHandsFreeCmd *cmdp;
	cmdp = new AbortOnFailCommand("STATE");
	QueueCommand(cmdp);
}

void NHandsFree::
NextCommand(NHandsFreeCmd *cmdp, bool success)
{
	if (m_ag_state == NHS_AG_INVALID) {
		/*
		 * We should have received some state when we asked
		 * after the connection completed
		 */
		AbortConnection();
		return;
	}

	if (m_ag_state == NHS_AG_NOBT) {
		QueueCommand(new NormalCommand("START"));
		return;
	}

	if (m_ag_state == NHS_AG_NODEV) {
		/* Bind it to our device */
		char buf[64];
		sprintf(buf, "BIND 00:14:9A:48:5F:A8");
		QueueCommand(new AbortOnFailCommand(buf));
		return;
	}

	if ((m_ag_state == NHS_AG_DISCON) && !m_ag_connect_sent) {
		/* Initiate a connection attempt */
		m_ag_connect_sent = true;
		QueueCommand(new NormalCommand("CONNECT"));
		return;
	}

	/* Otherwise there isn't much for us to do */
}

void NHandsFree::
AbortConnection(void)
{
	ConnectionLost();
}

void NHandsFree::
onAction(ActionClass action)
{
	if (action.name == "connect") {
		if (m_hfpd_state == NHS_HFPD_DISCONNECTED) {
			/* Reconnect to hfpd */
			InetConnect(1234);
		}
		return;
	}

	if (action.name == "answer") {
		IncomingCallPopUp(false);
		QueueCommand(new AbortOnFailCommand("ANSWER"));
		return;
	}

	if (action.name == "drop") {
		IncomingCallPopUp(false);
		QueueCommand(new AbortOnFailCommand("HANGUP"));
		return;
	}
}

void NHandsFree::
eventLoop()
{
	/*
	 * Currently, this function's only purpose is to poll
	 * sockets and push I/O-bound processes.
	 * It's a hack.  It will be here until libnghost gets a
	 * proper I/O notification mechanism.
	 */
	switch (m_hfpd_state) {
	case NHS_HFPD_DISCONNECTED:
		/* Nothing to poll */
		return;

	case NHS_HFPD_CONNECTING: {
		fd_set wfds;
		struct timeval tv;

		/* Test if m_sock is writable */
		FD_ZERO(&wfds);
		FD_SET(m_sock, &wfds);
		tv.tv_sec = tv.tv_usec = 0;
		if (select(m_sock + 1, NULL, &wfds, NULL, &tv) <= 0)
			return;
	}
		ConnectNotify();
		return;

	case NHS_HFPD_CONNECTED:
		/* We just let read return EAGAIN every time here */
		DataReady();
		break;

	default:
		abort();
	}
}

} /* namespace nghost */

/*
 * Create function, an NHandsFree factory.
 */

extern "C" {
	extern nghost::nplugin *create(void);
}

nghost::nplugin *
create(void)
{
	nghost::NHandsFree *nhp;
	nhp = new nghost::NHandsFree;
	if (!nhp)
		return 0;

	if (!nhp->Init()) {
		delete nhp;
		return 0;
	}

	return nhp;
}
