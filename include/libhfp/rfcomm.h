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

#if !defined(__LIBHFP_RFCOMM_H__)
#define __LIBHFP_RFCOMM_H__

#include <stdint.h>
#include <errno.h>

#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include <libhfp/bt.h>

/**
 * @file libhfp/rfcomm.h
 */

namespace libhfp {

/**
 * @brief Bluetooth RFCOMM connection security modes
 * @ingroup hfp
 *
 * This enumeration describes levels of security to be applied to
 * Bluetooth sockets when communicating with devices.
 *
 * @sa RfcommService::SetSecMode(), HfpSession::GetSecMode()
 */
enum rfcomm_secmode_t {
	/**
	 * @brief No authentication or encryption are mandatory
	 */
	RFCOMM_SEC_NONE,
	/**
	 * @brief Authentication is mandatory but encryption is not
	 */
	RFCOMM_SEC_AUTH,
	/**
	 * @brief Authentication and encryption are mandatory
	 */
	RFCOMM_SEC_CRYPT
};

extern bool SetLinkModeOptions(int rsock, bool master, rfcomm_secmode_t sec);

class RfcommSession;

/**
 * @brief Service building block for RFCOMM profiles
 * @ingroup hfp
 *
 * RfcommService is a proto-service that contains utility code
 * useful for creating a service handler for an RFCOMM profile.
 * It works in conjunction with RfcommSession.
 *
 * @sa HfpService
 */
class RfcommService : public BtService {
	friend class RfcommSession;

protected:
	RfcommSession *FindSession(BtDevice const *devp) const
		{ return (RfcommSession *) BtService::FindSession(devp); }

	int			m_rfcomm_listen;
	uint8_t			m_rfcomm_listen_channel;
	SocketNotifier		*m_rfcomm_listen_not;

	/* Policies for associated sessions */
	rfcomm_secmode_t	m_secmode;
	uint16_t		m_search_svclass_id;
	bool			m_bt_master;

	static bool RfcommSdpSetAccessProto(sdp_record_t *svcp,
					    uint8_t channel);

	uint8_t RfcommGetListenChannel(void) const
		{ return m_rfcomm_listen_channel; }

	void RfcommListenNotify(SocketNotifier *, int fh);
	virtual RfcommSession *SessionFactory(BtDevice *devp) = 0;

	/* Call me from Start() */
	bool RfcommListen(ErrorInfo *error = 0, uint8_t channel = 0);

	/* Call me from Stop() */
	void RfcommCleanup(void);

	ListItem		m_autoreconnect_list;
	int			m_autoreconnect_timeout;
	bool			m_autoreconnect_set;
	TimerNotifier		*m_autoreconnect_timer;

	ListItem		m_autoreconnect_now_list;
	bool			m_autoreconnect_now_set;
	TimerNotifier		*m_autoreconnect_now_timer;

	void AddAutoReconnect(RfcommSession *sessp, bool now = false);
	void RemoveAutoReconnect(RfcommSession *sessp);
	void AutoReconnectTimeout(TimerNotifier*);

	RfcommService(uint16_t search_svclass_id = 0);
	virtual ~RfcommService();

	bool Start(ErrorInfo *error);
	void Stop(void);

public:
	RfcommSession *GetSession(BtDevice *devp, bool create = true);
	RfcommSession *GetSession(bdaddr_t const &addr, bool create = true);
	RfcommSession *GetSession(const char *addrstr, bool create = true);

	/**
	 * @brief Query the security mode of the listening socket
	 *
	 * @return The security mode applied to the listening socket
	 *
	 * @note The security mode set on the listening socket is also
	 * used for all new outbound connections.
	 *
	 * @sa SetSecMode()
	 */
	rfcomm_secmode_t GetSecMode(void) const { return m_secmode; }

	/**
	 * @brief Set the security mode of the listening socket
	 *
	 * The security mode of the listening socket sets the security
	 * mode of all inbound connections.  The RfcommSession and
	 * HfpSession objects also apply it to all outbound connections,
	 * and it is effectively used as policy for all RFCOMM connections.
	 *
	 * @param[in] sec Security mode to be applied
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @em false, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 *
	 * @retval true Security mode was successfully applied
	 * @retval false Error attempting to apply security mode
	 *
	 * @note Changes to the security mode of the listening socket
	 * do not affect existing connections to audio gateway devices.
	 * Changes will take effect on the next inbound or outbound
	 * connection.  To determine the security mode in effect for a
	 * specific connection, use HfpService::GetSecMode().
	 *
	 * @sa GetSecMode(), HfpSession::GetSecMode().
	 */
	bool SetSecMode(rfcomm_secmode_t sec, ErrorInfo *error = 0);
};


/**
 * @brief Session building block for RFCOMM profiles
 * @ingroup hfp
 *
 * RfcommSession is a proto-session that contains utility code
 * useful for creating a service handler for an RFCOMM profile.
 * It works in conjunction with RfcommService.
 *
 * @sa HfpService
 */
class RfcommSession : public BtSession {
	friend class RfcommService;
protected:
	enum {
		RFC_Disconnected = 1,
		RFC_SdpLookupChannel,
		RFC_Connecting,
		RFC_Connected
	}			m_rfcomm_state;
	SdpTask			*m_rfcomm_sdp_task;
	bool			m_rfcomm_inbound;
	bool			m_rfcomm_dcvoluntary;

	bool RfcommSdpLookupChannel(ErrorInfo *error);
	void RfcommSdpLookupChannelComplete(SdpTask *taskp);
	bool RfcommConnect(uint8_t channel, ErrorInfo *error,
			   int timeout = 15000);
	void RfcommConnectNotify(SocketNotifier *notp, int fh);

	/* This is the primary overload and handles SDP channel lookups */
	bool RfcommConnect(ErrorInfo *error, int timeout = 30000);

	virtual bool RfcommAccept(int sock);

	int			m_rfcomm_sock;
	SocketNotifier		*m_rfcomm_not;
	rfcomm_secmode_t	m_rfcomm_secmode;

	bool			m_conn_autoreconnect;
	ListItem		m_autoreconnect_links;

	virtual void AutoReconnect(void) = 0;

	TimerNotifier		*m_operation_timeout;

	bool RfcommSetOperationTimeout(int ms, ErrorInfo *error);
	void RfcommOperationTimeout(TimerNotifier *);

	bool RfcommSend(const uint8_t *buf, size_t len, ErrorInfo *error);

	RfcommSession(RfcommService *svcp, BtDevice *devp);
	virtual ~RfcommSession();

	bool IsRfcommConnecting(void) const {
		return ((m_rfcomm_state > RFC_Disconnected) &&
			(m_rfcomm_state < RFC_Connected));
	}
	bool IsRfcommConnected(void) const
		{ return (m_rfcomm_state == RFC_Connected); }

	virtual void SdpSupportedFeatures(uint16_t features);

	/* Subclasses: override this method! */
	virtual void __Disconnect(ErrorInfo *reason, bool voluntary = false);

	/* Subclasses: implement this method! */
	virtual void NotifyConnectionState(ErrorInfo *async_error) = 0;

	/* BlueZ error condition determinations */
	static bool ReadErrorFatal(int err) {
		return ((err != EAGAIN) &&
			(err != EINTR) &&
			(err != ENOMEM) &&
			(err != ENOBUFS));
	}
	static bool WriteErrorFatal(int err) {
		return (err == ENOTCONN);
	}
	static bool ReadErrorVoluntary(int err) {
		return (err == ECONNRESET);
	}

public:
	RfcommService *GetService(void) const
		{ return (RfcommService*) BtSession::GetService(); }

	/**
	 * @brief Query the security mode of the connection to the device
	 *
	 * For information on the security mode, see rfcomm_secmode_t.
	 *
	 * If the device is connected, this accessor will return the
	 * security mode associated with the socket.
	 *
	 * Security modes are configured at the service level, as
	 * they must be set at the socket level prior to connection in
	 * order to take effect.  For inbound connections, the service
	 * object is the keeper of the one listening socket that accepts
	 * inbound connections and assigns their security modes.
	 *
	 * While it may be possible to try to manage security modes at the
	 * level of each session, at least for outbound connections,
	 * no compelling reasons have been identified at the time of this
	 * writing and management only at the level of the service is
	 * supported.
	 *
	 * @return The security mode applied to the service-level socket,
	 * if the device is in the Connecting or Connected state.  If
	 * Disconnected, the return value is undefined.
	 *
	 * @sa RfcommService::SetSecMode()
	 */
	rfcomm_secmode_t GetSecMode(void) const { return m_rfcomm_secmode; }

	/**
	 * @brief Query whether the autoreconnect mechanism is enabled for
	 * this device
	 *
	 * @retval true Autoreconnect is enabled
	 * @retval false Autoreconnect is disabled.
	 * @sa SetAutoReconnect()
	 */
	bool IsAutoReconnect(void) const { return m_conn_autoreconnect; }

	/**
	 * @brief Enable or disable the autoreconnect mechanism for this
	 * device
	 *
	 * If enabled, whenever the device is disconnected, a reconnection
	 * attempt will be made periodically through a timer.
	 * Auto-reconnection is useful for devices such as phones that
	 * regularly move in and out of range.
	 *
	 * This function can affect the @ref aglifecycle "life cycle management"
	 * of the object it is called on.
	 * @param enable Set to true to enable, false to disable.
	 * @sa IsAutoReconnect(), Connect()
	 */
	void SetAutoReconnect(bool enable);

	/**
	 * @brief Query whether an in-progress or complete connection to the
	 * device was initiated by the device
	 *
	 * This is useful for connection auditing logic, e.g. to refuse
	 * connections from unknown devices.
	 *
	 * @retval true The device initiated the connection
	 * @retval false The local UI or autoreconnect mechanism initiated
	 * the connection
	 */
	bool IsConnectionRemoteInitiated(void) const {
		return m_rfcomm_inbound;
	}

	/**
	 * @brief Query whether the last disconnection was voluntary or
	 * involuntary
	 *
	 * When a connection is explicitly broken, e.g. selecting a
	 * disconnect function on the device or calling
	 * Disconnect(), it is considered voluntary.  When a connection
	 * fails for implicit reasons such as a device moving out of radio
	 * range or an HCI being disconnected, it is considered
	 * involuntary.  This function reports whether the last transition
	 * from the Connecting or Connected state to the Disconnected state
	 * was voluntary.
	 *
	 * @retval true The last transition to the Disconnected state was
	 * voluntary.
	 * @retval false The last transition to the Disconnected state was
	 * involuntary.
	 *
	 * @note This information is useful for deciding whether to
	 * disable auto-reconnection of a service to a particular device.
	 */
	bool IsPriorDisconnectVoluntary(void) const {
		return m_rfcomm_dcvoluntary;
	}
};

} /* namespace libhfp */
#endif /* !defined(__LIBHFP_RFCOMM_H__) */
