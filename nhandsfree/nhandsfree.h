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

#if !defined(__NHANDSFREE_H__)
#define __NHANDSFREE_H__

#include <stdarg.h>
#include <plugin_genericAction.h>
#include <string>

namespace nghost {

	class NHandsFree;

	struct NHandsFreeCmd {
		NHandsFreeCmd *next;
		char text[64];
		virtual bool Response(NHandsFree *, const char *) = 0;
		virtual ~NHandsFreeCmd() {};
	};

	class NHandsFree : public GenericAction {
	protected:
		/* State handling */
		enum {
			NHS_HFPD_DISCONNECTED,
			NHS_HFPD_CONNECTING,
			NHS_HFPD_CONNECTED
		}		m_hfpd_state;

		enum {
			NHS_AG_INVALID,
			NHS_AG_NOBT,
			NHS_AG_NODEV,
			NHS_AG_DISCON,
			NHS_AG_CONNECTING,
			NHS_AG_CONN,
		}		m_ag_state;

		bool		m_ag_connect_sent;
		bool		m_service;
		int		m_signal_bars;

		enum {
			NHS_CALL_NONE,
			NHS_CALL_CONN,
			NHS_CALL_ESTAB,
		}		m_call_state;
		bool		m_call_waiting;

		bool		m_did_incoming_call_popup;
		bool		m_incoming_call_popup_open;		

		enum {
			NHS_VOICE_DISCON,
			NHS_VOICE_CONN,
			NHS_VOICE_MUTE,
		}		m_voice_state;

		std::string	m_callerid;

		void UpdateIcon(int num, const char *image);
		void UpdateStateIcons(void);

		void IncomingCallPopUp(bool show);


		/* Connection handling */
		bool InetConnect(int port);
		bool UnixConnect(const char *path);
		void ConnectNotify(void);
		void Connected(void);
		void ConnectionLost(void);

		/* Outbound command queue */
		NHandsFreeCmd	*m_cmd_head, *m_cmd_tail;

		bool SendCommand(NHandsFreeCmd *cmd);
		bool QueueCommand(NHandsFreeCmd *cmd);
		void DropCommands(void);

		/* Input buffer processing */
		int		m_sock;
		bool		m_cmd_pend;
		size_t		m_rsp_start;
		size_t		m_rsp_len;
		char		m_rsp_buf[512];

		void DataReady(void);
		ssize_t ParseLine(char *data, size_t len);
		bool ParseResponse(const char *line);

		/* Output processing */
		bool Write(void *buf, size_t len);
		bool vprintf(const char *fmt, va_list ap);
		bool printf(const char *fmt, ...) {
			bool res;
			va_list ap;
			va_start(ap, fmt);
			res = vprintf(fmt, ap);
			va_end(ap);
			return res;
		}

	public:
		NHandsFree();
		~NHandsFree();

		virtual std::string pluginName() const {
			return "NHandsFree";
		}
		virtual std::string pluginAuthor() const {
			return "Sam Revitch <samr7@cs.washington.edu>";
		}
		virtual std::string pluginDescription() const {
			return "Controller plugin for hfpd Bluetooth "
				"Hands Free Profile daemon.";
		}
		virtual std::string pluginVersion() const {
			return "0.0.1";
		}

		bool Init(void);
		void NextCommand(NHandsFreeCmd *cmdp, bool success);
		void AbortConnection(void);

		virtual void onAction(ActionClass action);
		virtual void eventLoop();
	};

} /* namespace nghost */

#endif /* defined(__NHANDSFREE_H__) */
