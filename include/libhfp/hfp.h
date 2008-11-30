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

#if !defined(__LIBHFP_HFP_H__)
#define __LIBHFP_HFP_H__

#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include <stdint.h>

#include <libhfp/bt.h>
#include <libhfp/rfcomm.h>
#include <libhfp/soundio.h>
#include <libhfp/soundio-buf.h>

/**
 * @file libhfp/hfp.h
 */

namespace libhfp {

class HfpSession;

/**
 * @brief Service Handler for Hands-Free Profile
 * @ingroup hfp
 *
 *
 * HfpService is a building block of Hands-Free Profile support,
 * implementing single-instance functions including inbound connection
 * handling and automatic reconnection.  As such, one instance of
 * HfpService is created per system.
 *
 * Each attachable Audio Gateway device (typically a cell phone) is
 * represented by a BtDevice object.  HfpService supports the creation
 * of an HfpSession object attached to the BtDevice, to support a Hands
 * Free Profile session with the device.  The life cycle of HfpSession
 * objects is described in the
 * @ref aglifecycle "Audio Gateway Life Cycle" section.
 *
 * @section hfpserviceoperation Service Operation
 *
 * Clients will typically instantiate one HfpService object, register
 * callbacks, e.g. HfpService::cb_HfpSessionFactory, read known devices
 * from a configuration file, and instantiate an HfpSession object for
 * each with GetSession().  The HfpSession objects can be marked
 * auto-reconnect, via HfpSession::SetAutoReconnect(), so that they may
 * be automatically connected when they enter radio range.
 *
 * Whenever Hands-Free Profile service is active, HfpService maintains
 * a listening socket for inbound HFP service level connections.  Devices
 * not previously known to HfpService may initiate connections, and will
 * have HfpSession objects created for them.  The HfpService client may
 * control access by:
 * - Refusing to create HfpSession objects for unknown Bluetooth addresses,
 * using HfpService::cb_HfpSessionFactory
 * - Forcing disconnections when devices initiate connections and their
 * HfpSession objects enter the Connecting state, using
 * HfpSession::cb_NotifyDeviceConnection
 * - Requiring authentication, using SetSecMode().
 *
 * Authentication is managed at the system level and is beyond the scope
 * of HfpService or HfpSession other than imposing a requirement that
 * system authentication be upheld.  Additionally, Bluetooth HFP 1.5 does
 * not mandate any level of security on its RFCOMM connections.
 */
class HfpService : public RfcommService {
	friend class HfpSession;

private:
	int			m_sco_listen;
	SocketNotifier		*m_sco_listen_not;

	int			m_brsf_my_caps;
	char			*m_svc_name;
	char			*m_svc_desc;
	sdp_record_t		*m_sdp_rec;

	TimerNotifier		*m_timer;

	void Timeout(TimerNotifier*);

	bool			m_sco_enable;

	ListItem		m_autoreconnect_list;
	int			m_autoreconnect_timeout;
	bool			m_autoreconnect_set;

	bool			m_complaint_sco_mtu;
	bool			m_complaint_sco_vs;
	bool			m_complaint_sco_listen;

	void AddAutoReconnect(HfpSession *sessp);
	void RemoveAutoReconnect(HfpSession *sessp);

	bool ScoListen(ErrorInfo *error);
	void ScoCleanup(void);
	void ScoListenNotify(SocketNotifier *, int fh);

	bool SdpRegister(ErrorInfo *error);
	void SdpUnregister(void);

	HfpSession *FindSession(BtDevice *devp) const
		{ return (HfpSession*) BtService::FindSession(devp); }

	bool Start(ErrorInfo *error);
	void Stop(void);

	virtual RfcommSession *SessionFactory(BtDevice *devp);

public:

	HfpService(int caps = 15);
	virtual ~HfpService();

	/**
	 * @brief Factory for HfpSession objects, implemented as a callback
	 *
	 * Clients of HfpService may use this callback to construct
	 * derivatives of HfpSession with additional functionality, or
	 * to pre-register callbacks before any of the object's methods
	 * are invoked.
	 *
	 * This callback specifically violates the
	 * @ref callbacks "rule of not invoking callbacks in a nested fashion from client method calls,"
	 * as it may be called nested from Connect() and GetDevice() with
	 * create=true.
	 * Clients choosing to receive this callback may not make additional
	 * HfpService / HfpSession method calls aside from constructors
	 * and DefaultSessionFactory().
	 *
	 * @param baddr_t& Bluetooth address of the device to be represented.
	 * @return Newly created HfpSession object representing the device,
	 * or NULL on failure.
	 */
	Callback<HfpSession*, BtDevice*>	cb_HfpSessionFactory;

	/**
	 * @brief Default factory method for HfpSession objects
	 *
	 * Clients wishing to override HfpService::cb_HfpSessionFactory
	 * may use this method to construct base HfpSession objects.
	 */
	HfpSession *DefaultSessionFactory(BtDevice *);

	/**
	 * @brief Look up and possibly create an HfpSession for a
	 * Bluetooth address
	 *
	 * This function can be used to search for an existing HfpSession
	 * object associated with a known Bluetooth address, and possibly
	 * create a new one if none is found.
	 *
	 * @param devp BtDevice object with which the HfpSession must
	 * be associated.
	 * @param create Set to @c true to create a new HfpSession for the
	 * target BtDevice if none already exists.
	 *
	 * @return The associated HfpSession object, which may have been
	 * newly constructed if @c create = @c true.  NULL on memory
	 * allocation failure or other error.
	 *
	 * @note If a session is found or created successfully, it is
	 * returned with an additional reference added to it, to prevent
	 * it from being inadvertently destroyed as per
	 * @ref aglifecycle "life cycle management."  The caller is
	 * responsible for releasing the reference with HfpSession::Put().
	 */
	HfpSession *GetSession(BtDevice *devp, bool create = true) {
		return (HfpSession*) RfcommService::GetSession(devp, create);
	}

	/**
	 * @brief Look up and possibly create an HfpSession for a
	 * Bluetooth address
	 *
	 * This function can be used to search for an existing HfpSession
	 * object associated with a known Bluetooth address, and possibly
	 * create a new one if none is found.
	 *
	 * @param [in] addr Bluetooth address of the device with which the
	 * HfpSession must be associated.  A BtDevice object representing
	 * the address will be looked up and possibly created as the first
	 * step.
	 * @param create Set to @c true to create a new HfpSession, and
	 * possibly a new BtDevice, if none already exists.
	 *
	 * @return The associated HfpSession object, which may have been
	 * newly constructed if @c create = @c true.  NULL on memory
	 * allocation failure or other error.
	 *
	 * @note If a session is found or created successfully, it is
	 * returned with an additional reference added to it, to prevent
	 * it from being inadvertently destroyed as per
	 * @ref aglifecycle "life cycle management."  The caller is
	 * responsible for releasing the reference with HfpSession::Put().
	 */
	HfpSession *GetSession(bdaddr_t const &addr, bool create = true) {
		return (HfpSession*) RfcommService::GetSession(addr, create);
	}

	/**
	 * @brief Look up and possibly create an HfpSession for a
	 * Bluetooth address by address string
	 *
	 * This overload of GetDevice() is useful for configuration file
	 * parsing paths that create HfpSession objects for known devices.
	 * An ASCII hexadecimal Bluetooth address stored in a configuration
	 * file can be used directly by this routine.
	 *
	 * @param[in] addrst Address of the device represented as a string,
	 * e.g. "00:07:61:D2:55:37" but not "Motorola" or "Logitech."
	 * @param create Set to @c true to create a new HfpSession, and
	 * possibly a new BtDevice, if none already exists.
	 *
	 * @return The associated HfpSession object, which may have been
	 * newly constructed if @c create = @c true.  NULL on memory
	 * allocation failure or other error.
	 *
	 * @note If a session is found or created successfully, it is
	 * returned with an additional reference added to it, to prevent
	 * it from being inadvertently destroyed as per
	 * @ref aglifecycle "life cycle management."  The caller is
	 * responsible for releasing the reference with HfpSession::Put().
	 *
	 * @warning This function accepts a string representation of
	 * a Bluetooth address, e.g. "00:07:61:D2:55:37", NOT a Bluetooth
	 * name, e.g. "Motorola."
	 *
	 * @warning This function uses str2ba() to convert the string to a
	 * bdaddr_t, and malformed Bluetooth addresses are not reported.
	 */
	HfpSession *GetSession(const char *addrst, bool create = true) {
		return (HfpSession*) RfcommService::GetSession(addrst, create);
	}

	/**
	 * @brief Initiate a connection to an audio gateway device with
	 * a known address
	 *
	 * This function will:
	 * -# Search for an HfpSession record associated with a BtDevice.
	 * If none is found, one will be created.
	 * -# Initiate an outbound connection on the resulting HfpSession,
	 * if it is in the Disconnected state.  See HfpSession::Connect().
	 * -# Return the HfpSession.
	 *
	 * @param[in] devp BtDevice representing the target device.
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 *
	 * @return The HfpSession object associated with raddr, which
	 * is hopefully in the Connecting or Connected state, or @c 0
	 * on failure.
	 *
	 * @note If a device is found or created successfully, it is
	 * returned with an additional reference added to it, to prevent
	 * it from being inadvertently destroyed as per
	 * @ref aglifecycle "life cycle management."  The caller is
	 * responsible for releasing the reference with HfpSession::Put().
	 *
	 * @sa GetDevice(), HfpSession::Connect()
	 */
	HfpSession *Connect(BtDevice *devp, ErrorInfo *error = 0);

	/**
	 * @brief Initiate a connection to an audio gateway device with
	 * a known address
	 *
	 * This function will:
	 * -# Search for an HfpSession record associated with a
	 * Bluetooth address @c addr.  If none is found, one will be created.
	 * -# Initiate an outbound connection on the resulting HfpSession,
	 * if it is in the Disconnected state.  See HfpSession::Connect().
	 * -# Return the HfpSession.
	 *
	 * @param[in] addr Bluetooth address of the device to be created
	 * and connected to.
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 *
	 * @return The HfpSession object associated with raddr, which
	 * is hopefully in the Connecting or Connected state, or @c 0
	 * on failure.
	 *
	 * @note If a device is found or created successfully, it is
	 * returned with an additional reference added to it, to prevent
	 * it from being inadvertently destroyed as per
	 * @ref aglifecycle "life cycle management."  The caller is
	 * responsible for releasing the reference with HfpSession::Put().
	 *
	 * @sa GetDevice(), HfpSession::Connect()
	 */
	HfpSession *Connect(bdaddr_t const &addr, ErrorInfo *error = 0);

	/**
	 * @brief Initiate a connection to an audio gateway device by
	 * Bluetooth address string
	 *
	 * This overload of Connect() is useful for configuration file
	 * parsing paths that initiate connections to known devices.
	 * An ASCII hexadecimal Bluetooth address stored in a configuration
	 * file can be used directly by this routine.
	 *
	 * This function will:
	 * -# Search for an HfpSession record associated with the provided
	 * Bluetooth address.  If none is found, one will be created.
	 * -# Initiate an outbound connection on the resulting HfpSession,
	 * if it is in the Disconnected state.  See HfpSession::Connect().
	 * -# Return the HfpSession.
	 *
	 * @param[in] addrstr Address of the device represented as a string,
	 * e.g. "00:07:61:D2:55:37" but not "Motorola" or "Logitech."
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 *
	 * @return The HfpSession object associated with addrstr, which
	 * is hopefully in the Connecting or Connected state, or @c 0
	 * on failure.
	 *
	 * @note If a device is found or created successfully, it is
	 * returned with an additional reference added to it, to prevent
	 * it from being inadvertently destroyed as per
	 * @ref aglifecycle "life cycle management."  The caller is
	 * responsible for releasing the reference with HfpSession::Put().
	 *
	 * @warning This function accepts a string representation of
	 * a Bluetooth address, e.g. "00:07:61:D2:55:37", NOT a Bluetooth
	 * name, e.g. "Motorola."
	 *
	 * @warning This function uses str2ba() to convert the string to a
	 * bdaddr_t, and malformed Bluetooth addresses are not reported.
	 *
	 * @sa GetDevice(), HfpSession::Connect()
	 */
	HfpSession *Connect(const char *addrstr, ErrorInfo *error = 0);

	/**
	 * @brief Query the first session object
	 *
	 * @return The first HfpSession object associated with this
	 * service object, in enumeration order.
	 *
	 * @note The HfpSession returned does not have a reference added
	 * to it.  Callers should take care to avoid dangling pointers.
	 */
	HfpSession *GetFirstSession(void) const
		{ return (HfpSession *) BtService::GetFirstSession(); }

	/**
	 * @brief Query the next session object in enumeration order
	 *
	 * @return The HfpSession object associated with this
	 * service object, following @em sessp in the enumeration order.
	 *
	 * @note The HfpSession returned does not have a reference added
	 * to it.  Callers should take care to avoid dangling pointers.
	 */
	HfpSession *GetNextSession(HfpSession *sessp) const
		{ return (HfpSession *) BtService::
				GetNextSession((BtSession *)sessp); }

	/**
	 * @brief Query whether SCO audio functionality is enabled
	 *
	 * This method retrieves the state of the SCO audio
	 * functionality.  For more information, see SetScoEnabled().
	 */
	bool GetScoEnabled(void) const { return m_sco_enable; }

	/**
	 * @brief Set whether SCO audio functionality is enabled
	 *
	 * In some situations, it is desirable to use a different
	 * subsystem to handle SCO audio connections and audio
	 * processing.  This option allows SCO audio support to be
	 * completely disabled, presumably so that libhfp may coexist
	 * with another subsystem that already handles SCO audio.
	 *
	 * Unfortunately, this is not terribly useful.  Most packages
	 * that implement SCO audio support do so properly in the sense
	 * of only permitting it with a service-level connection of
	 * some sort.  The BlueZ 4.x audio components are an example
	 * of this.
	 *
	 * If SCO support is enabled:
	 * - A SCO listener socket will be registered, and bound to
	 * the local address of the selected HCI.
	 * - Inbound SCO connections will be accepted for connected
	 * sessions, and notified through the session's
	 * HfpSession::cb_NotifyAudioConnection callback.  For
	 * disconnected or incompletely connected sessions, SCO
	 * audio connections will be refused.
	 * - Outbound SCO connection requests from connected
	 * HfpSession objects, through HfpSession::SndOpen(), will
	 * not automatically fail.
	 *
	 * @param[in] sco_enable Set to @c true to enable SCO audio
	 * handling support, @c false to disable.
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @em false, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 *
	 * @retval true SCO enablement value successfully changed
	 * @retval false SCO enablement value could not be changed.
	 * This can be caused by attempting to enable SCO support
	 * when the HfpSession is active and the SCO listener socket
	 * could not be created.  The reason for the failure will be
	 * reported through @em error, if @em error is not @c 0.
	 */
	bool SetScoEnabled(bool sco_enable, ErrorInfo *error = 0);

	int GetCaps(void) const { return m_brsf_my_caps; }
	void SetCaps(int caps) { m_brsf_my_caps = caps; }

	const char *GetServiceName(void) const
		{ return m_svc_name ? m_svc_name : "Handsfree"; }
	bool SetServiceName(const char *desc, ErrorInfo *error = 0);

	const char *GetServiceDesc(void) const
		{ return m_svc_desc ? m_svc_desc : ""; }
	bool SetServiceDesc(const char *desc, ErrorInfo *error = 0);

	static bool IsDeviceClassHf(uint32_t devclass) {
		return (devclass & 0x1ffc) == 0x408;
	}
	static void SetDeviceClassHf(uint32_t &devclass) {
		devclass = (devclass & ~0x1ffc) | 0x408;
	}
};

class GsmClipPhoneNumber {
	void *operator new(size_t nb, size_t extra);
	size_t		extra;

	static GsmClipPhoneNumber *Create(const char *src);

public:
	const char	*number;
	int		type;
	const char	*subaddr;
	int		satype;
	const char	*alpha;
	int		cli_validity;

	void operator delete(void *mem);
	static GsmClipPhoneNumber *Parse(const char *buffer);
	static GsmClipPhoneNumber *ParseCcwa(const char *buffer);
	bool Compare(const GsmClipPhoneNumber *clip) const;
	GsmClipPhoneNumber *Duplicate(void) const;
};

class AtCommand;

/**
 * @brief Audio Gateway Pending Command Object
 * @ingroup hfp
 *
 * Some HfpSession methods control the connection state to the audio
 * gateway.  Others, such as HfpSession::CmdDial(), relay commands to
 * the audio gateway device.  All HfpSession methods are asynchronous,
 * and for longer-running operations, the method will return either
 * having started the operation, or having failed to start the
 * operation for whatever reason.
 *
 * HfpSession methods that relay commands to the audio gateway,
 * including HfpSession::CmdDial(), will return an HfpPendingCommand
 * object.  This object functions as a callback that is invoked when a
 * reply is received from the audio gateway for the queued command.
 *
 * Every HfpPendingCommand object returned is guaranteed to have its
 * registered callback invoked exactly once.
 *
 * The caller may also use the Cancel() method of the HfpPendingCommand
 * object to cancel the command, although this may fail if the command
 * has already been sent.
 *
 * The caller of the HfpSession method is entirely responsible for
 * the lifecycle of the HfpPendingCommand object.  If the caller is
 * not interested in the result of the command, it should delete the
 * HfpPendingCommand object.  Likewise, it must also delete the
 * HfpPendingCommand object after receiving a callback, or after a
 * successful call to the Cancel() method.
 */
class HfpPendingCommand
	: public Callback<void, HfpPendingCommand*, ErrorInfo*, const char *> {
public:
	/**
	 * @brief Request that the command be canceled and not sent
	 *
	 * @retval true Command has been canceled and will not be sent.
	 * @retval false Command has already been sent or has already
	 * completed.
	 */
	virtual bool Cancel(void) = 0;
	virtual ~HfpPendingCommand();
};

/**
 * @brief Session object for Hands-Free Profile
 * @ingroup hfp
 *
 * This class represents a Hands Free Profile session and connection to
 * a remote audio gateway device (typically a cell phone).  An HFP session
 * through this object can allow the audio gateway to use a speakerphone
 * facility provided by the local system.  It supports numerous use cases:
 * - Interrogating the supported audio gateway features of the device
 * - Disseminating notifications from the device, e.g. signal strength
 * - Sending commands to the device, e.g. dial number, hangup
 * - Streaming audio to and from the device
 *
 * @section aglifecycle Life Cycle
 *
 * The associated HfpService object manages the life cycle of HfpSession
 * objects.  The HfpService keeps an index of HfpSession objects, and
 * ensures that at most one exists per attached BtDevice device object.
 * HfpSession objects may be instantiated through one of three paths:
 *
 * - Through HfpService::GetSession(), with @c create = @c true.  This
 * method requires a reference to an existing BtDevice object, or other
 * explicit knowledge of the Bluetooth address of the target device.
 * This method will also only instantiate a new HfpSession object if
 * one doesn't already exist for the associated BtDevice.
 * - Through HfpService::Connect().  This method is similar to
 * HfpService::GetSession() above, but also initiates an outbound
 * connection to the device.
 * - As part of an inbound connection, HfpService will automatically
 * instantiate an HfpSession for the device if one does not already exist.
 *
 * HfpSession objects are reference-counted
 * @ref managed "Bluetooth managed objects," and are always destroyed
 * under circumstances:
 * - Client reference count drops to zero.  Put().
 * - The HfpSession is in the Disconnected state.  See IsConnected(),
 * IsConnecting().
 * - Auto-reconnect is not enabled.  See SetAutoReconnect().
 * - As with all @ref managed "managed objects," the actual destruction
 * is performed in the context of a timer event.
 *
 * The HfpSession class is designed so that clients may interact with it
 * without defining derived classes.  All notifications provided by
 * HfpSession are performed through Callback callback objects.
 *
 * Clients may override the instantiation path for HfpSession objects by
 * registering their own factory method to HfpService::cb_HfpSessionFactory.
 * As part of a specialized factory, clients may use the default factory
 * method, HfpService::DefaultSessionFactory().  A specialized factory
 * may be used to:
 * - Register all relevant callbacks with the HfpSession object from a
 * single path, and before any may be invoked.
 * - Instantiate a class derived from HfpSession with additional
 * functionality.  This is recommended only as a last resort.
 *
 * @section hfpscallbacks Callbacks
 *
 * The HfpSession object provides numerous callbacks, and uses the
 * @ref callbacks "Bluetooth module rules for non-nesting."
 *
 * @section hfpsstate State
 *
 * The session may be in one of three states:
 * - Disconnected: ! IsConnecting() && ! IsConnected()
 * - Connecting: IsConnecting()
 * - Connected: IsConnected()
 *
 * When Disconnected or Connecting, none of the feature inquiries are
 * meaningful, no state indicator and call state notifications will
 * be delivered, the audio connection must be Disconnected, and no
 * commands may be issued on the device.
 *
 * Once connected, the device will accept commands, state indicator
 * notifications will be delivered and certain values retained, and the
 * voice audio channel may be connected.
 */
class HfpSession : public RfcommSession, public SoundIoBufferBase {
	friend class HfpService;
	friend class AtCommand;

private:
	enum {
		BTS_Disconnected = 1,
		BTS_RfcommConnecting,
		BTS_Handshaking,
		BTS_Connected
	}			m_conn_state;
	bool			m_conn_autoreconnect;
	ListItem		m_autoreconnect_links;

	ListItem		m_commands;

	int			m_brsf;

	/* Methods reimplemented from RfcommSession */
	virtual void __Disconnect(ErrorInfo *reason, bool voluntary = false);
	virtual void NotifyConnectionState(ErrorInfo *async_error);
	virtual void SdpSupportedFeatures(uint16_t features);

	/* New methods for HFP */
	void AutoReconnect(void);
	bool HfpHandshake(ErrorInfo *error);
	void HfpHandshakeDone(void);
	void HfpDataReady(SocketNotifier *notp, int fh);
	size_t HfpConsume(char *buf, size_t len);
	void DeleteFirstCommand(bool do_start = true);
	bool AppendCommand(AtCommand *cmdp, ErrorInfo *error);
	bool StartCommand(ErrorInfo *error);
	bool CancelCommand(AtCommand *cmdp);
	void ResponseDefault(char *buf);
	HfpPendingCommand *PendingCommand(AtCommand *cmdp, ErrorInfo *error);

	friend class CindRCommand;	
	friend class AtdCommand;
	friend class AtCommandClearCallSetup;
	void UpdateIndicator(int inum, const char *ival);
	void UpdateCallSetup(int val, int ring = 0,
			     GsmClipPhoneNumber *clip = 0,
			     int timeout_ms = 0);

	friend class ChldTCommand;
	void SetSupportedHoldRange(int start, int end);
	void SetSupportedHoldModes(const char *hold_mode_list);
	bool		m_chld_0: 1,
			m_chld_1: 1,
			m_chld_1x: 1,
			m_chld_2: 1,
			m_chld_2x: 1,
			m_chld_3: 1,
			m_chld_4: 1;

	friend class BrsfCommand;
	void SetSupportedFeatures(int ag_features) { m_brsf = ag_features; };

	friend class CindTCommand;
	void SetIndicatorNum(int inum, const char *name, int namelen);

	friend class ClipCommand;
	bool		m_clip_enabled;

	friend class CcwaCommand;
	bool		m_ccwa_enabled;

	int		m_inum_service;
	int		m_inum_call;	
	int		m_inum_callsetup;
	int		m_inum_signal;
	int		m_inum_roam;
	int		m_inum_battchg;

	/*
	 * Indicator name/slot mapping table -- quite primitive!
	 */
	const char	**m_inum_names;
	int		m_inum_names_len;

	void CleanupIndicators(void);
	void ExpandIndicators(int min_size);


	/* Call state trackers */
	bool		m_state_service;
	bool		m_state_call;
	int		m_state_callsetup;
	int		m_state_signal;
	int		m_state_roam;
	int		m_state_battchg;

	enum { PHONENUM_MAX_LEN = 31, };
	GsmClipPhoneNumber	*m_state_incomplete_clip;

	static bool ValidPhoneNumChar(char c, ErrorInfo *error);
	static bool ValidPhoneNum(const char *ph, ErrorInfo *error);

	/* SCO and audio-related members */
	enum { SCO_MAX_PKTSIZE = 512 };
	enum {
		BVS_Invalid,
		BVS_SocketConnecting,
		BVS_Connected,
	}				m_sco_state;
	int				m_sco_sock;
	bool				m_sco_use_tiocoutq;
	bool				m_sco_nonblock;
	uint16_t			m_sco_handle;
	uint16_t			m_sco_mtu;
	uint16_t			m_sco_packet_samps;

	bool				m_sco_nvs_pending;
	bool				m_sco_nas_pending;

	SocketNotifier			*m_sco_not;

	void __DisconnectSco(bool notifyvs, bool notifyp, bool async,
			     ErrorInfo &error);
	bool ScoGetParams(int ssock, ErrorInfo *error);
	bool ScoAccept(int ssock);
	bool ScoConnect(ErrorInfo *error);
	void ScoConnectNotify(SocketNotifier *notp, int fh);
	void ScoDataNotify(SocketNotifier *, int fh);
	bool ScoSocketExists(void) const { return (m_sco_sock >= 0); }

	/* Event handling stuff */
	TimerNotifier			*m_timer;

	void Timeout(TimerNotifier *notp);

protected:
	/* Timeouts for dealing with devices without the callsetup indicator */
	int		m_timeout_ring;
	int		m_timeout_ring_ccwa;
	int		m_timeout_dial;

public:
	HfpService *GetService(void) const
		{ return (HfpService*) BtSession::GetService(); }

	HfpSession(HfpService *svcp, BtDevice *devp);
	virtual ~HfpSession();

	/**
	 * @brief Notification of an asynchronous change to the device
	 * connection state
	 *
	 * The connection state, from the perspective of HfpSession
	 * clients, has three states:
	 * - Disconnected: ! IsConnecting() && ! IsConnected()
	 * - Connecting: IsConnecting()
	 * - Connected: IsConnected()
	 *
	 * The audio gateway may transition from Disconnected to
	 * Connecting, from Connecting to Connected, and from either
	 * Connecting or Connected to Disconnected.  There is no legal
	 * transition directly from Disconnected to Connected.
	 *
	 * As a rule, @ref callbacks "no callbacks are invoked in a nested fashion from method calls."
	 * State changes effected directly by Connect() and Disconnect()
	 * are not reported through this mechanism, and clients must notice
	 * such state changes directly.  State changes caused by
	 * connections initiated by the audio gateway or the auto-reconnect
	 * mechanism, connections that have completed negotiation,
	 * and connection attempts that have failed for reasons other than
	 * Disconnect() are reported.
	 *
	 * For example, if a HfpSession object in the Disconnected state
	 * has its Connect() method invoked, it will transition to the
	 * Connecting state, but no notification will be generated through
	 * HfpSession::cb_NotifyConnection.  Later, after negotiation
	 * completes, and the object transitions to the Connected state,
	 * a notification WILL be generated because the event causing the
	 * transition was system-generated and while it may have been the
	 * result of an earlier call to Connect(), the notification is not
	 * being generated from directly underneath that call.
	 *
	 * The transition to the Connected state is always done from the
	 * Connecting state, and will always be notified through this
	 * callback.  Because most queries and commands cannot be made of
	 * the device until it enters the Connected state, it is critical
	 * that clients receive notification of it.
	 *
	 * @param HfpSession* The HfpSession object that has had an
	 * asynchronous state transition to its service-level connection.
	 * @param error Error information structure.  If the state
	 * transition was to the Disconnected state, the ErrorInfo*
	 * parameter is nonzero, and contains information about why the
	 * connection was lost.  Otherwise, it is @c 0.
	 *
	 * @sa IsConnecting(), IsConnected(), IsConnectionRemoteInitiated(),
	 * IsPriorDisconnectVoluntary().
	 */
	Callback<void, HfpSession *, ErrorInfo *>	cb_NotifyConnection;

	/**
	 * @brief Notification of an asynchronous change to the voice
	 * audio connection state
	 *
	 * The voice audio connection states include:
	 * - Disconnected: ! IsConnectingAudio() && ! IsConnectedAudio()
	 * - Connecting: IsConnectingAudio()
	 * - Connected: IsConnectedAudio()
	 *
	 * Similar to the device connection state, the audio connection
	 * state can transition from Disconnected to Connecting, e.g. by
	 * SoundIo::SndOpen() or a remote connection attempt.  It can
	 * transition from Connecting to Connected following negotiation.
	 * It can transition from Connecting or Connected to Disconnected
	 * by SoundIo::SndClose() or Disconnect().  It cannot transition
	 * directly from Disconnected to Connected.
	 *
	 * As a rule, @ref callbacks "no callbacks are invoked in a nested fashion from method calls."
	 * Similar to HfpSession::cb_NotifyConnection, this callback
	 * is only invoked on asynchronous changes to the audio connection
	 * state above and never directly through method calls.
	 *
	 * The audio connection will always be Disconnected whenever the
	 * device is not Connected.
	 *
	 * @param HfpSession* The HfpSession object that has had an
	 * asynchronous state transition to its audio connection.
	 * @param error Error information structure.  If the state
	 * transition of the device audio was to the Disconnected state,
	 * the ErrorInfo* parameter is nonzero, and contains information
	 * about why the audio connection was lost.  Otherwise, it is @c 0.
	 *
	 * @sa IsConnectingAudio(), IsConnectedAudio()
	 */
	Callback<void, HfpSession*, ErrorInfo*>	cb_NotifyAudioConnection;

	/**
	 * @brief Notification of a change to the established call state
	 *
	 * @param HfpSession* Pointer to the HfpSession originating the
	 * call.  Largely redundant as Callback can store and substitute
	 * parameters.
	 * @param bool Set to @em true if the device's active call state
	 * has changed.  Use HasEstablishedCall() to test wiether an
	 * established call exists or not.
	 * @param bool Set to @em true if the device's waiting or held
	 * call state has changed.  Use HasConnectingCall() to test
	 * whether an outbound call is in progress, and HasWaitingCall()
	 * to test whether an inbound call is ready to be answered.
	 * Use WaitingCallIdentity() to retrieve the Caller ID value
	 * provided in the case of an incoming call.
	 * @param bool Set to @em true when the device signals a ring.
	 * Clients may use this to play a ring tone or execute alerting
	 * activity.
	 *
	 * @note In the place of a single ring notification, this
	 * notification may be triggered twice in short succession,
	 * initially without a known calling line identity, and then with
	 * identity.
	 *
	 * @sa HasEstablishedCall(), HasConnectingCall(), HasWaitingCall(),
	 * WaitingCallIdentity()
	 */
	Callback<void, HfpSession *, bool, bool, bool> cb_NotifyCall;

	/**
	 * @brief Notification of a change to a miscellaneous audio gateway
	 * state indicator
	 *
	 * @param char* Name of indicator
	 * @param int New value of indicator
	 *
	 * Some well-known indicator names include:
	 * - "service" -- (0,1) depending on the state of wireless service
	 * to the audio gateway.  This value is also reported by
	 * GetServiceState().
	 * - "roam" -- (0,1) depending on whether the audio gateway is
	 * "roaming".  This value is also reported by GetRoaming().
	 * - "signal" -- (0-5) where 5 is the best wireless signal
	 * quality to the audio gateway.  This value is also reported by
	 * GetSignalStrength().
	 * - "battchg" -- (0-5) where 5 is the highest level of battery
	 * charge on the audio gateway.  This value is also reported by
	 * GetBatteryCharge().
	 *
	 * @sa GSM 07.07 section 8.9
	 */
	Callback<void, HfpSession *, const char *, int> cb_NotifyIndicator;

private:
	/* Response buffer */
	enum { RFCOMM_MAX_LINELEN = 512 };
	size_t			m_rsp_start;
	size_t			m_rsp_len;
	char			m_rsp_buf[RFCOMM_MAX_LINELEN];

public:
	/*
	 * Basic connection state queries
	 */

	/**
	 * @brief Query whether a connection attempt to the device is in
	 * progress
	 *
	 * @retval true A connection attempt is in progress
	 * @retval false The device is either fully connected or disconnected
	 * @sa IsConnected()
	 */
	bool IsConnecting(void) const {
		return ((m_conn_state != BTS_Disconnected) &&
			(m_conn_state != BTS_Connected));
	}

	/**
	 * @brief Query whether the device is fully connected
	 *
	 * @retval true The device is fully connected and accepting commands
	 * @retval false The device is not fully connected
	 * @sa IsConnecting()
	 */
	bool IsConnected(void) const {
		return (m_conn_state == BTS_Connected);
	}

	/**
	 * @brief Query whether a voice audio (SCO) connection is being
	 * initiated
	 *
	 * @retval true An audio connection is in progress but incomplete.
	 * @retval false An audio connection is complete or nonexistant.
	 * @sa IsConnectedAudio(), HfpSession::cb_NotifyAudioConnection
	 */
	bool IsConnectingAudio(void) const {
		return (ScoSocketExists() &&
			(m_sco_state == BVS_SocketConnecting));
	}

	/**
	 * @brief Query whether a voice audio (SCO) connection is completed
	 * and available for audio streaming
	 *
	 * @retval true Audio is connected and operational.
	 * @retval false Audio is disconnected or connecting.
	 * @sa IsConnectingAudio(), HfpSession::cb_NotifyAudioConnection
	 */
	bool IsConnectedAudio(void) const {
		return (ScoSocketExists() &&
			(m_sco_state == BVS_Connected));
	}


	/*
	 * Connection and autoreconnection verbs
	 */

	/**
	 * @brief Initiate a connection attempt to the device
	 *
	 * This function will attempt to transition the device to the
	 * Connecting state.
	 * The process of connecting is always asynchronous, and if this
	 * function succeeds, a later transition to the Connected state will
	 * occur asynchronously after negotiation.  As long as the client
	 * does not invoke Disconnect(), a future call to
	 * HfpSession::cb_NotifyConnection should be expected, to
	 * notify a transition to either the Connected or Disconnected state.
	 *
	 * As a rule, @ref callbacks "no callbacks are invoked in a nested fashion from method calls."
	 * This function can affect the connection state, but will not
	 * directly generate a HfpSession::cb_NotifyConnection
	 * callback.  Callers must notice state changes in-line.
	 *
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @em false, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 *
	 * @retval true The connection attempt is in progress, and the
	 * device has transitioned to the Connecting state.
	 * @retval false The connection attempt failed.  Reasons could
	 * include:
	 * - The device is already connecting or connected.
	 * - The local SDP daemon is unavailable.
	 * - There are no available Bluetooth HCIs.
	 * - The RFCOMM socket could not be created, e.g. because part or
	 * all of the bluetooth stack could not be loaded.
	 * @sa Disconnect(), SetAutoReconnect()
	 * @sa HfpSession::cb_NotifyConnection
	 */
	bool Connect(ErrorInfo *error = 0);

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
	 * Auto-reconnection is useful for phones that regularly move in
	 * and out of range.
	 *
	 * This function can affect the @ref aglifecycle "life cycle management"
	 * of the object it is called on.
	 * @param enable Set to true to enable, false to disable.
	 * @sa IsAutoReconnect(), Connect()
	 */
	void SetAutoReconnect(bool enable);


	/*
	 * Call state queries
	 */

	/**
	 * @brief Query whether the device has an incomplete outgoing call
	 *
	 * @retval true Unanswered outgoing call exists
	 * @retval false No unanswered outgoing call exists
	 */
	bool HasConnectingCall(void) const {
		return (m_state_callsetup > 1) && !m_state_call;
	}
	/**
	 * @brief Query whether the device has an established call
	 *
	 * @retval true Established call exists
	 * @retval false No established call exists
	 */
	bool HasEstablishedCall(void) const {
		return m_state_call;
	}
	/**
	 * @brief Query whether the device has an incomplete incoming call
	 *
	 * @retval true Unanswered incoming call exists
	 * @retval false No unanswered incoming call exists
	 */
	bool HasWaitingCall(void) const {
		return (m_state_callsetup == 1);
	}

	/**
	 * @brief Retrieve the caller ID value of the last incomplete call,
	 * either incoming or outgoing
	 *
	 * The string value may be used
	 * until the global event handler is invoked again or
	 * CmdDial() / CmdRedial() is invoked.
	 * @return String pointer to the remote identity of the last
	 * incomplete call, or NULL if the identity was not known.
	 */
	const GsmClipPhoneNumber *WaitingCallIdentity(void) const {
		return m_state_incomplete_clip;
	}


	/*
	 * Saved indicator value queries
	 */

	/**
	 * @brief Query whether the attached device has wireless service
	 * available
	 *
	 * Queries the last value reported by the audio gateway for
	 * service availability.
	 *
	 * Wireless service may be unavailable because the phone is out of
	 * range of a cell tower.
	 *
	 * @note This function will only return meaningful values for
	 * connected devices that support service indication.
	 * @retval true Service available
	 * @retval false Service unavailable
	 *
	 * @sa HfpSession::cb_NotifyIndicator
	 * @sa GSM 07.07 section 8.9
	 */
	bool GetServiceState(void) const {
		return m_state_service;
	}

	/**
	 * @brief Query the signal strength (bars) from an audio gateway
	 *
	 * Queries the last value reported by the audio gateway for
	 * signal strength.
	 *
	 * @return An integer in the range 0-5, where 5 is the highest
	 * level of signal, or -1 if the signal strength is unknown.
	 * @note This function will only return meaningful values for
	 * connected devices that support signal strength indication.
	 *
	 * @sa FeatureIndSignalStrength(), HfpSession::cb_NotifyIndicator
	 * @sa GSM 07.07 section 8.9
	 */
	int GetSignalStrength(void) const { return m_state_signal; }

	/**
	 * @brief Query whether the audio gateway is roaming
	 *
	 * Queries the last value reported by the audio gateway for
	 * roaming.
	 *
	 * @retval 0 Audio gateway is not roaming
	 * @retval 1 Audio gateway is roaming
	 * @retval -1 Roaming state is unknown
	 * @note This function will only return meaningful values for
	 * connected devices that support roaming indication.
	 *
	 * @sa FeatureIndRoaming(), HfpSession::cb_NotifyIndicator
	 * @sa GSM 07.07 section 8.9
	 */
	int GetRoaming(void) const { return m_state_roam; }

	/**
	 * @brief Query the battery charge level of the audio gateway
	 *
	 * Queries the last value reported by the audio gateway for
	 * battery charge level.
	 *
	 * @return An integer in the range 0-5, where 5 is the highest
	 * battery charge level, or -1 if the battery charge is unknown.
	 * @note This function will only return meaningful values for
	 * connected devices that support battery charge indication.
	 *
	 * @sa FeatureIndBatteryCharge(), HfpSession::cb_NotifyIndicator
	 * @sa GSM 07.07 section 8.9
	 */
	int GetBatteryCharge(void) const { return m_state_battchg; }


	/*
	 * Reported feature presence queries
	 */

	/**
	 * @brief Query the feature bit set of the attached device
	 *
	 * This will return the raw reported features of the device,
	 * as defined in the Bluetooth Hands Free Profile specification.
	 * It is up to the client to interpret them.  For simpler use,
	 * the FeatureXXX() methods are available, e.g.
	 * FeatureThreeWayCalling().
	 * 
	 * @return The feature bit set reported by the device.
	 * @note This function will only return meaningful values
	 * when the device is in the connected state.
	 */
	int GetFeatures(void) const { return m_brsf; }

	/**
	 * @brief Query whether the attached device supports three-way calling
	 *
	 * If supported, CmdCallDropWaiting() may be used.
	 *
	 * @note This information is only valid when the device is in the
	 * connected state.
	 * @retval true Three way calling supported
	 * @retval false Three way calling not supported
	 */
	bool FeatureThreeWayCalling(void) const
		{ return (m_brsf & 1) ? true : false; }
	/**
	 * @brief Query whether the attached device supports echo cancelation
	 * and/or noise reduction signal processing
	 *
	 * @note This information is only valid when the device is in the
	 * connected state.
	 * @retval true EC/NR supported
	 * @retval false EC/NR not supported
	 */
	bool FeatureEcnr(void) const
		{ return (m_brsf & 2) ? true : false; }
	/**
	 * @brief Query whether the attached device supports voice recognition
	 *
	 * @note This information is only valid when the device is in the
	 * connected state.
	 * @todo Voice recognition is not yet supported by libhfp.
	 *
	 * @retval true Voice recognition supported
	 * @retval false Voice recognition not supported
	 */
	bool FeatureVoiceRecog(void) const
		{ return (m_brsf & 4) ? true : false; }
	/**
	 * @brief Query whether the attached device supports in-band ringtones
	 *
	 * If the audio gateway supports in-band ringtones, it is able
	 * to play its own ringtone through the voice audio link, and
	 * will do so when in-band ringtones are enabled.
	 *
	 * @note This information is only valid when the device is in the
	 * connected state.
	 * @retval true In-band ringtones supported
	 * @retval false In-band ringtones not supported
	 */
	bool FeatureInBandRingTone(void) const
		{ return (m_brsf & 8) ? true : false; }
	bool FeatureVoiceTag(void) const
		{ return (m_brsf & 16) ? true : false; }
	/**
	 * @brief Query whether the attached device supports waiting call
	 * rejection
	 *
	 * If supported, CmdCallDropWaiting() may be used.
	 *
	 * @note This information is only valid when the device is in the
	 * connected state.
	 * @retval true Call rejection supported
	 * @retval false Call rejection not supported
	 */
	bool FeatureRejectCall(void) const
		{ return (m_brsf & 32) ? true : false; }
	bool FeatureEnhancedCallStatus(void) const
		{ return (m_brsf & 64) ? true : false; }
	bool FeatureEnhancedCallControl(void) const
		{ return (m_brsf & 128) ? true : false; }
	bool FeatureExtendedErrors(void) const
		{ return (m_brsf & 256) ? true : false; }
	bool FeatureIndCallSetup(void) const
		{ return (m_inum_callsetup != 0); }

	bool FeatureDropHeldUdub(void) const { return m_chld_0; }
	bool FeatureSwapDropActive(void) const { return m_chld_1; }
	bool FeatureDropActive(void) const { return m_chld_1x; }
	bool FeatureSwapHoldActive(void) const { return m_chld_2; }
	bool FeaturePrivateConsult(void) const { return m_chld_2x; }
	bool FeatureLink(void) const { return m_chld_3; }
	bool FeatureTransfer(void) const { return m_chld_4; }

	/**
	 * @brief Query whether the attached device supports signal strength
	 * indication
	 *
	 * If supported, the signal strength may be queried with
	 * GetSignalStrength().
	 *
	 * @note This information is only valid when the device is in the
	 * connected state.
	 * @retval true Signal strength indication supported
	 * @retval false Signal strength indication not supported
	 * @sa HfpSession::cb_NotifyIndicator
	 */
	bool FeatureIndSignalStrength(void) const
		{ return (m_inum_signal != 0); }
	/**
	 * @brief Query whether the attached device supports roaming indication
	 *
	 * If roaming indication is supported, the roaming state may be
	 * queried with GetRoaming().
	 *
	 * @note This information is only valid when the device is in the
	 * connected state.
	 * @retval true Roaming state indication supported
	 * @retval false Roaming state indication not supported
	 * @sa HfpSession::cb_NotifyIndicator
	 */
	bool FeatureIndRoaming(void) const
		{ return (m_inum_roam != 0); }
	/**
	 * @brief Query whether the attached device supports battery charge
	 * indication
	 *
	 * If battery charge indication is supported, the battery charge
	 * state may be queried with GetBatteryCharge().
	 *
	 * @note This information is only valid when the device is in the
	 * connected state.
	 * @retval true Battery charge indication supported
	 * @retval false Battery charge indication not supported
	 * @sa HfpSession::cb_NotifyIndicator
	 */
	bool FeatureIndBatteryCharge(void) const
		{ return (m_inum_battchg != 0); }


	/*
	 * Telephony Commands
	 */

	/**
	 * @brief Query whether a service level command to the device is
	 * queued or otherwise pending completion
	 *
	 * @retval true At least one command is pending
	 * @retval false The command queue is empty
	 */
	bool IsCommandPending(void) const { return !m_commands.Empty(); }

	HfpPendingCommand *CmdSetVoiceRecog(bool enabled,
					    ErrorInfo *error = 0);
	HfpPendingCommand *CmdSetEcnr(bool enabled, ErrorInfo *error = 0);

	/**
	 * @brief Request that the audio gateway answer the unanswered
	 * incoming call
	 *
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdAnswer(ErrorInfo *error = 0);

	/**
	 * @brief Request that the audio gateway hang up the active call
	 *
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @retval true Command was queued
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdHangUp(ErrorInfo *error = 0);

	/**
	 * @brief Request that the audio gateway place a new outgoing call
	 *
	 * @param[in] phnum Phone number to be dialed
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost or the phone number is too long
	 */
	HfpPendingCommand *CmdDial(const char *phnum, ErrorInfo *error = 0);

	/**
	 * @brief Request that the audio gateway place a new outgoing call
	 * using the last dialed number
	 *
	 * The last dialed number is the number last dialed by the audio
	 * gateway, and may have been dialed before the audio gateway was
	 * connected to the hands-free.
	 *
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdRedial(ErrorInfo *error = 0);

	/**
	 * @brief Request that the audio gateway send a DTMF tone to the
	 * active call
	 *
	 * @param code DTMF code to be sent.  May be numeric, #, or *.
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @retval true Command was queued
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdSendDtmf(char code, ErrorInfo *error = 0);

	/**
	 * @brief Request that the audio gateway drop the held call, or
	 * reject the waiting call as User Declared User Busy.
	 *
	 * @note This command may only be expected to succeed if the device
	 * claims support for dropping waiting calls,
	 * i.e. FeatureDropHeldUdub() returns true.
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdCallDropHeldUdub(ErrorInfo *error = 0);

	/**
	 * @brief Request that the audio gateway drop the active call
	 * and activate the held or waiting call
	 *
	 * @note This command may only be expected to succeed if the
	 * device supports active call drop-swapping, i.e.
	 * FeatureSwapDropActive() returns true.
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdCallSwapDropActive(ErrorInfo *error = 0);

	/**
	 * @brief Drop a specific active call
	 *
	 * @note This command may only be expected to succeed if the
	 * device supports dropping of specific active calls, i.e.
	 * FeatureDropActive() returns true.
	 * @param[in] actnum Call number to be dropped
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdCallDropActive(unsigned int actnum,
					     ErrorInfo *error = 0);

	/**
	 * @brief Request that the audio gateway hold the active call
	 * and activate the held or waiting call
	 *
	 * @note This command may only be expected to succeed if the
	 * device supports active call hold-swapping, i.e.
	 * FeatureSwapHoldActive() returns true.
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdCallSwapHoldActive(ErrorInfo *error = 0);

	/**
	 * @brief Request private consultation mode with a call
	 *
	 * @note This command may only be expected to succeed if the
	 * device supports private consultation mode, i.e.
	 * FeaturePrivateConsult() returns true.
	 * @param[in] callnum Call number to be made the active call
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdCallPrivateConsult(unsigned int callnum,
						 ErrorInfo *error = 0);

	/**
	 * @brief Request that the audio gateway create a three-way call
	 * using the active call and the held or waiting call
	 *
	 * @note This command may only be expected to succeed if the
	 * device supports call linking, i.e. FeatureLink() returns true.
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdCallLink(ErrorInfo *error = 0);

	/**
	 * @brief Request that the audio gateway link the two calls
	 * and disconnect the subscriber from both calls.
	 *
	 * @note This command may only be expected to succeed if the
	 * device supports explicit call transfer,
	 * i.e. FeatureTransfer() returns true.
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @c 0, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 * @return An HfpPendingCommand to receive a notification when
	 * the command completes, with the command status, or @c 0 if
	 * the command could not be queued, e.g. because the device
	 * connection was lost
	 */
	HfpPendingCommand *CmdCallTransfer(ErrorInfo *error = 0);


	/*
	 * SoundIo interfaces
	 */

	/**
	 * @brief Initiate an audio connection to the connected device
	 *
	 * This method will initiate an outbound audio connection to
	 * the connected device.
	 *
	 * @param[in] play Must be @em true
	 * @param[in] capture Must be @em true
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @em false, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 *
	 * @retval true Audio connection has been initiated.  The
	 * connection will be incomplete, i.e. IsConnectingAudio(), and
	 * a callback to HfpSession::cb_NotifyAudioConnection will be
	 * pending.
	 * @retval false Audio is either already connected, the
	 * connection attempt failed, or the @em play and/or @em capture
	 * parameters were @em false.
	 *
	 * @note If SCO audio support is disabled at the service level,
	 * this method will fail.  See HfpService::SetScoEnabled().
	 */
	bool SndOpen(bool play, bool capture, ErrorInfo *error = 0);
	/**
	 * @brief Disconnect the audio connection
	 *
	 * Closes an existing audio connection, or cancels a pending audio
	 * connection.
	 *
	 * As a rule, @ref callbacks "no callbacks are invoked in a nested fashion from method calls."
	 * This method is no exception and does not result in a call to
	 * either HfpSession::cb_NotifyAudioConnection or
	 * SoundIo::cb_NotifyPacket.
	 */
	void SndClose(void);
	/**
	 * @brief Query the SoundIo properties of the connected device
	 *
	 * This method is provided as part of the audio handling interface.
	 */
	void SndGetProps(SoundIoProps &props) const;
	/**
	 * @brief Query the supported PCM format of the connected device
	 *
	 * This method is provided as part of the audio handling interface.
	 */
	void SndGetFormat(SoundIoFormat &format) const;
	/**
	 * @brief Check whether a PCM format is compatible with the
	 * connected device
	 *
	 * This method is provided as part of the audio handling interface.
	 */
	bool SndSetFormat(SoundIoFormat &format, ErrorInfo *error);
	void SndHandleAbort(ErrorInfo error);
	void SndPushInput(bool);
	void SndPushOutput(bool);
	/**
	 * @brief Initiate asynchronous audio handling
	 *
	 * Initiates periodic callbacks to SoundIo::cb_NotifyPacket
	 * as audio packets are sent to/received from the connected
	 * device.
	 *
	 * @param[in] play Must be @em true
	 * @param[in] capture Must be @em true
	 * @param[out] error Error information structure.  If this method
	 * fails and returns @em false, and @em error is not 0, @em error
	 * will be filled out with information on the cause of the failure.
	 *
	 * @retval true Asynchronous audio handling has been enabled.
	 * Expect future callbacks to SoundIo::cb_NotifyPacket.
	 * @retval false Error enabling asynchronous audio handling.
	 * Reasons might include:
	 * - Audio connection is not established, i.e. IsConnectedAudio()
	 * - The @em play and/or @em capture parameters were @em false.
	 */
	bool SndAsyncStart(bool play, bool capture, ErrorInfo *error);
	/**
	 * @brief Halt asynchronous audio handling
	 *
	 * This method is provided as part of the audio handling interface.
	 */
	void SndAsyncStop(void);
	/**
	 * @brief Query whether asynchronous audio handling is enabled
	 *
	 * This method is provided as part of the audio handling interface.
	 */
	bool SndIsAsyncStarted(void) const
		{ return IsConnectedAudio() && (m_sco_not != 0); }

	size_t AudioPacketNumSamples(void) const { return m_sco_packet_samps; }
};


} /* namespace libhfp */
#endif /* !defined(__LIBHFP_HFP_H__) */
