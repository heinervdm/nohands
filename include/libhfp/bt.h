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

#if !defined(__LIBHFP_BT_H__)
#define __LIBHFP_BT_H__

#include <sys/types.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include <libhfp/list.h>
#include <libhfp/events.h>

/**
 * @file libhfp/bt.h
 */

namespace libhfp {

extern bool SetNonBlock(int fh, bool nonblock);

/**
 * @defgroup hfp Bluetooth Hands-Free Profile Implementation
 *
 * This group contains objects required to use the Bluetooth
 * Hands-Free Profile service.  It includes a complete object model
 * for Bluetooth devices, service classes, and sessions, along with
 * life cycle management policies for device and session objects.
 *
 * @section objectmodel Object Model Overview
 *
 * @dot
 * digraph objectmodel { rankdir="LR"
 *	node [shape=record, fontname=Helvetica fontsize=24];
 *	hub [ label = "BtHub" color="blue" URL="\ref BtHub" ];
 *	subgraph devices { label="Device Objects"
 *	dev1 [ label = "BtDevice" color="green" URL="\ref BtDevice" ];
 *	dev2 [ label = "BtDevice" color="green" URL="\ref BtDevice" ];
 *	dev3 [ label = "BtDevice" color="green" URL="\ref BtDevice" ];
 *	}
 *	subgraph svcs { label="Service Objects" rankdir="BT" rank="min"
 *	svc1 [ label = "HfpService" color="red" URL="\ref HfpService" ];
 *	svc2 [ label = "ObexService" color="red" URL="\ref BtService" ];
 *	}
 *	subgraph sessions { label="Session Objects"
 *	sess1 [ label = "HfpSession" color="purple" URL="\ref HfpSession" ];
 *	sess2 [ label = "HfpSession" color="purple" URL="\ref HfpSession" ];
 *	sess3 [ label = "ObexSession" color="purple" URL="\ref BtSession" ];
 *	}
 *	dev1 -> hub
 *     	dev2 -> hub
 *	dev3 -> hub
 *	svc1 -> hub
 *	svc2 -> hub
 *	sess1 -> svc1
 *	sess1 -> dev1
 *	sess2 -> svc1
 *	sess2 -> dev2
 *	sess3 -> svc2
 *	sess3 -> dev2
 * }
 * @enddot
 *
 * The BtHub is a single-instance object.  It must be instantiated by
 * the client application at start time.  BtHub maintains per-application
 * resources, including a connection to the local Service Discovery
 * Protocol daemon used to register and advertise available services.
 *
 * BtService derived objects encapsulate service-specific single instance
 * resources.  They are instantiated as needed by the client application,
 * and registered with BtHub via BtHub::AddService() and
 * BtHub::RemoveService().  Specifically, HfpService maintains a listening
 * RFCOMM socket so that audio gateway devices may initiate connections.
 * Service state is managed collectively by BtHub.  BtHub will invoke
 * the BtService::Start() and BtService::Stop() methods of each service
 * object when Bluetooth service availability changes, i.e. when a USB
 * Bluetooth HCI is inserted or removed.
 *
 * BtDevice objects are indexed and maintained by BtHub.  All BtDevice
 * objects in the index have unique addresses.  BtHub::GetDevice() can
 * be used to look up or instantiate a BtDevice record for a specific
 * Bluetooth address.  BtHub will also instantiate BtDevice objects
 * automatically when reporting device inquiry results.
 *
 * BtSession derived objects, including HfpSession, encapsulate resources
 * associated with an session of a service.  As an example, HfpSession
 * includes:
 * - The RFCOMM socket used to communicate with the device.
 * - Methods for controlling the device, e.g. HfpSession::CmdDial().
 * - Callbacks for notifying changes of the device state, e.g.
 * HfpSession::cb_NotifyConnection.
 *
 * BtSession objects are @ref managed "managed objects" and are always
 * instantiated by their presiding BtService derived object.  They
 * can be instantiated manually, e.g. HfpService::GetSession(), or
 * automatically in response to remotely initiated connections.
 *
 * @section managed Managed Objects
 *
 * BtDevice and BtSession are managed objects, which derive from
 * BtManaged.  These objects are reference-counted.  When they
 * are deleted, they are deleted in a deferred fashion from a timer
 * event.  BtManaged objects also contain a pointer value reserved
 * for client use.  See BtManaged::SetPrivate().
 *
 * @section callbacks Callbacks
 *
 * As a rule, none of the callbacks provided by Bluetooth components
 * are ever invoked in a nested fashion from client method calls.  This
 * breaks potential recursive loops and complex situations that might
 * form if clients invoke method calls from callbacks, e.g.
 *
 * @code
 * Connect() ->
 *    HfpSession::cb_NotifyConnection -> 
 *       Disconnect() ->
 *          HfpSession::cb_NotifyConnection ->
 *             Connect() -> ...
 * @endcode
 *
 * A client may therefore invoke any method call from any callback without
 * having to worry about nested callbacks.  However, this requires
 * clients to be mindful of state transitions that they originate directly
 * with method calls.  For example, HfpSession::cb_NotifyConnection
 * is invoked when the state of the device connection changes, but only if
 * it changes by means other than the Connect() or Disconnect() methods.
 * The client must directly notice the effects of its calls to Connect()
 * and Disconnect(), rather than expecting all changes to be reported
 * through HfpSession::cb_NotifyConnection.
 */

/*
 * This is an obtuse mess at the moment, partly because we use a
 * helper thread for SDP lookup chores.
 */
struct SdpTaskParams {
	enum sdp_tasktype_t {
		ST_SDP_LOOKUP,
	};

	int		m_seqid;
	sdp_tasktype_t	m_tasktype;
	bdaddr_t	m_bdaddr;
	uint16_t	m_svclass_id;
	int		m_timeout_ms;

	bool		m_complete;
	int		m_errno;
	bool		m_supported_features_present;
	uint8_t		m_channel;
	uint16_t	m_supported_features;
};

struct SdpTask {
	ListItem			m_sdpt_links;
	bool				m_submitted;
	bool				m_resubmit;
	SdpTaskParams			m_params;
	Callback<void, SdpTask*>	cb_Result;

	SdpTask(void) { memset(&m_params, 0, sizeof(m_params)); }
};

class SdpAsyncTaskHandler {
public:
	friend class BtHub;

	/*
	 * Synchronous task handler thread -
	 * Currently, the only function performed by this
	 * module is SDP record lookups.
	 *
	 * As soon as asynchronous SDP interfaces become more
	 * common, this class will be replaced with one that
	 * doesn't use a helper thread.
	 */

private:
	class BtHub			*m_hub;
	DispatchInterface		*m_ei;
	int				m_rqpipe;
	int				m_rspipe;
	pid_t				m_pid;
	SocketNotifier			*m_rspipe_not;
	ListItem			m_tasks;
	bool				m_current_aborted;

	/* The below four run in the context of the SDP thread */
	void SdpTaskThread(int rqfd, int rsfd);
	static int SdpLookupChannel(SdpTaskParams &htp);

	/* This alerts the main thread when the SDP thread replies */
	void SdpDataReadyNot(SocketNotifier *, int fh);
	void SdpNextQueue(void);

public:
	/* Routines for maintaining the SDP thread */
	int SdpCreateThread(void);
	void SdpShutdown(void);
        int SdpQueue(SdpTask *in_task);
	void SdpCancel(SdpTask *taskp);

	SdpAsyncTaskHandler(BtHub *hubp, DispatchInterface *eip)
		: m_hub(hubp), m_ei(eip), m_rqpipe(-1), m_rspipe(-1),
		  m_pid(-1), m_rspipe_not(0),
		  m_current_aborted(false) {}

	~SdpAsyncTaskHandler() { SdpShutdown(); }
};


struct HciTask {
	ListItem			m_hcit_links;

	enum hci_tasktype_t {
		HT_INQUIRY,
		HT_READ_NAME,
	};

	hci_tasktype_t			m_tasktype;

	bool				m_complete;
	int				m_errno;
	uint8_t				m_hci_status;

	bdaddr_t			m_bdaddr;

	uint32_t			m_devclass;
	uint8_t				m_pscan;
	uint8_t				m_pscan_rep;
	uint16_t			m_clkoff;
	uint16_t			m_opcode;
	int				m_timeout_ms;

	char				m_name[249];

	bool				m_submitted;
	bool				m_resubmit;
	Callback<void, HciTask*>	cb_Result;

	HciTask(void)
		: m_complete(false), m_errno(0), m_hci_status(0),
		  m_devclass(0), m_pscan(0), m_pscan_rep(0),
		  m_clkoff(0), m_opcode(0), m_timeout_ms(0),
		  m_submitted(false), m_resubmit(false) {}
};

class HciAsyncTaskHandler {
public:
	friend class BtHub;

	/*
	 * Async task handler for performing HCI jobs that can be
	 * done asynchronously using the HCI raw socket.
	 */

private:
	class BtHub			*m_hub;
	DispatchInterface		*m_ei;
	int				m_hci_fh;
	SocketNotifier			*m_hci_not;
	TimerNotifier			*m_resubmit;
	ListItem			m_hci_tasks;
	bool				m_resubmit_needed;
	bool				m_resubmit_set;

	/* This alerts the main thread when the HCI thread replies */
	void HciDataReadyNot(SocketNotifier *, int fh);
	int HciSend(int fh, HciTask *paramsp, void *data, size_t len);
	int HciSubmit(int fh, HciTask *paramsp);
	void HciResubmit(TimerNotifier *notp);

public:
	int HciInit(void);
	void HciShutdown(void);
	int HciQueue(HciTask *in_task);
	void HciCancel(HciTask *taskp);

	HciAsyncTaskHandler(BtHub *hubp, DispatchInterface *eip)
		: m_hub(hubp), m_ei(eip), m_hci_fh(-1), m_hci_not(0),
		  m_resubmit(0), m_resubmit_set(false) {}

	~HciAsyncTaskHandler() { HciShutdown(); }
};


class BtManaged;
class BtDevice;
class BtService;
class BtSession;

/**
 * @brief Bluetooth Device Manager
 * @ingroup hfp
 *
 * One instance of BtHub is created per system.
 *
 * BtHub implements single-instance Bluetooth-related functions including
 * device list management, device inquiry, and service registration.
 *
 * Each Bluetooth device is represented by a BtDevice object.  The life
 * cycle of BtDevice objects is described in the
 * @ref bdlifecycle "BtDevice Life Cycle" section.
 *
 * @section huboperation Hub Operation
 *
 * A typical client might start up by performing steps:
 * -# Instantiate one BtHub object
 * -# Register callbacks, e.g. BtHub::cb_NotifySystemState
 * -# Instantiate service objects, e.g. HfpService
 * -# Associate service objects with BtHub, i.e. BtHub::AddService().
 * -# Read known devices from a configuration file
 * -# Instantiate session objects for known devices, e.g.
 * HfpService::GetSession().
 *
 * Authentication is managed at the system level by the BlueZ hcid, and
 * is beyond the scope of BtHub or BtDevice.  At the connection level, it
 * is possible to set policies for the system-level authentication be
 * upheld, and this is not handled by either BtHub or BtDevice.
 * See RfcommService::SetSecMode().
 */
class BtHub {
	friend class SdpAsyncTaskHandler;
	friend class HciAsyncTaskHandler;
	friend class BtManaged;
	friend class BtDevice;

private:
	sdp_session_t			*m_sdp;
	DispatchInterface		*m_ei;
	HciTask				*m_inquiry_task;

	ListItem			m_dead_objs;

	ListItem			m_devices;
	ListItem			m_services;

	bool SdpRegister(uint8_t channel);

	SdpAsyncTaskHandler		m_sdp_handler;
	HciAsyncTaskHandler		m_hci_handler;
	int				m_hci_seqid;

	/* Service routines for use in the UI context */
	void HciInquiryResult(HciTask *taskp);
	void ClearInquiryFlags(void);

	BtDevice *FindClientDev(bdaddr_t const &bdaddr);
	BtDevice *CreateClientDev(bdaddr_t const &bdaddr);
	void DeadObject(BtManaged *objp);
	void UnreferencedClientDev(BtDevice *devp);

	void SdpConnectionLost(SocketNotifier *, int fh);
	void __Stop(void);
	void InvoluntaryStop(void);

	SocketNotifier			*m_sdp_not;
	TimerNotifier			*m_timer;

	void Timeout(TimerNotifier*);

	/* Members related to timer-based autoreconnection */
	bool			m_autorestart;
	int			m_autorestart_timeout;
	bool			m_autorestart_set;
	bool			m_cleanup_set;

	int HciTaskSubmit(HciTask *taskp);
	void HciTaskCancel(HciTask *taskp);

public:
	DispatchInterface *GetDi(void) const { return m_ei; }

	int SdpTaskSubmit(SdpTask *taskp);
	void SdpTaskCancel(SdpTask *taskp);

	bool SdpRecordRegister(sdp_record_t *recp);
	void SdpRecordUnregister(sdp_record_t *recp);

	/**
	 * @brief Notification that the Bluetooth system has stopped
	 * or restarted asynchronously
	 *
	 * The callback implementation can distinguish between the
	 * system having been stopped or restarted by checking IsStarted().
	 *
	 * Reasons for stopping typically include:
	 * - The last Bluetooth HCI was disconnected
	 * - The local SDP daemon became unavailable
	 *
	 * This callback may also be invoked if the system is successfully
	 * started by the auto-restart mechanism.
	 *
	 * As a rule, @ref callbacks "no callbacks are invoked in a nested fashion from method calls."
	 * This callback is never invoked in a nested fashion from
	 * Stop() or Start().
	 *
	 * @sa SetAutoRestart()
	 */
	Callback<void>					cb_NotifySystemState;

	/**
	 * @brief Factory for BtDevice objects, implemented as a callback
	 *
	 * As per the @ref bdlifecycle "BtDevice life cycle," clients
	 * of BtHub may use this callback to construct derivatives of
	 * BtDevice with additional functionality or with pre-registered
	 * callbacks.
	 *
	 * This callback specifically violates the
	 * @ref callbacks "rule of not invoking callbacks in a nested fashion from client method calls,"
	 * as it may be called nested from GetDevice() with create=true.
	 * Clients choosing to receive this callback must avoid additional
	 * BtHub / BtDevice method calls aside from constructors and
	 * DefaultDevFactory().
	 *
	 * @param baddr_t& Bluetooth address of the device to be represented.
	 * @return Newly created BtDevice object representing the device,
	 * or NULL on failure.
	 */
	Callback<BtDevice*, bdaddr_t const &>		cb_BtDeviceFactory;


	/**
	 * @brief Notification of new inquiry result or completion of
	 * inquiry
	 *
	 * As devices are discovered during an inquiry, they are
	 * reported through this callback.
	 *
	 * @param BtDevice* The BtDevice object representing the device,
	 * or @c 0 to indicate that the inquiry has completed.
	 * @param int If an inquiry was aborted for reasons other than
	 * StopInquiry() before the time limit elapsed, this parameter
	 * will provide an error code indicating why the inquiry failed.
	 * Otherwise, this parameter will be @c 0.
	 *
	 * @sa StartInquiry(), IsScanning()
	 */
	Callback<void, BtDevice *, int>			cb_InquiryResult;

	/**
	 * @brief Standard constructor
	 *
	 * @param eip Event dispatcher interface suitable for the
	 * environment in which BtHub is being used.
	 *
	 * @note The Bluetooth system is not started by the constructor.
	 * The client must call Start() to initiate it, or enable the
	 * auto-restart mechanism with SetAutoRestart().
	 */
	BtHub(DispatchInterface *eip);

	/**
	 * @brief Standard destructor
	 */
	~BtHub();

	/**
	 * @brief Default factory method for BtDevice objects
	 */
	BtDevice *DefaultDevFactory(bdaddr_t const &);

	/**
	 * @brief Register a BtService derived service handler
	 *
	 * @param svcp Service object to be registered.  Must not currently
	 * be registered.
	 *
	 * This function will add the parameter service object to the
	 * hub's list of registered services.
	 *
	 * If the Bluetooth system is started -- see IsStarted(), an
	 * attempt will be made to start the service, and if
	 * unsuccessful, the registration will be aborted.
	 *
	 * @retval true Service was successfully registered
	 * @retval false The Bluetooth system is in the started state,
	 * but the service could not be started.  The service was
	 * not registered in this case.
	 */
	bool AddService(BtService *svcp);

	/**
	 * @brief Unregister a BtService derived service handler
	 *
	 * @param svcp Service object to be unregistered.  Must be
	 * currently registered.
	 *
	 * This function will remove its parameter service object from
	 * the hub's list of registered services.
	 *
	 * If the Bluetooth system is started -- see IsStarted(), the
	 * service will be stopped before it is removed.
	 */
	void RemoveService(BtService *svcp);

	/**
	 * @brief Attempt to start the Bluetooth system.
	 *
	 * The Bluetooth system has two components:
	 * - A connection to the local Service Discovery Protocol daemon
	 * - Worker threads to process asynchronous requests.
	 *
	 * This function synchronously establishes both components.
	 *
	 * @retval true Bluetooth system started
	 * @retval false Bluetooth system could not be started
	 * for reasons that might include:
	 * - No Bluetooth HCIs are connected
	 * - The local SDP daemon is unavailable
	 *
	 * @note This function never results in a call to
	 * BtHub::cb_NotifySystemState.
	 *
	 * @sa Stop(), IsStarted(), BtHub::cb_NotifySystemState
	 */
	bool Start(void);

	/**
	 * @brief Stop Bluetooth system
	 *
	 * If the Bluetooth system is started, this function will cause it
	 * to be stopped.  The connection to the local Service Discovery
	 * Protocol daemon will be closed, and worker threads will be
	 * stopped.
	 *
	 * @note This function performs a synchronous stop of the
	 * Bluetooth system and never results in a call to
	 * BtHub::cb_NotifySystemState.
	 * @note This function will disable autorestart.  See
	 * SetAutoRestart().
	 *
	 * @sa Start(), IsStarted()
	 */
	void Stop(void);

	/**
	 * @brief Query whether the Bluetooth system is started
	 *
	 * @retval true Bluetooth system is started
	 * @retval false Bluetooth system is not started
	 *
	 * @sa Start(), Stop(), BtHub::cb_NotifySystemState
	 */
	bool IsStarted(void) { return (m_sdp != NULL); }

	/**
	 * @brief Query whether Bluetooth auto-restart is enabled
	 */
	bool GetAutoRestart(void) const { return m_autorestart; }

	/**
	 * @brief Configure auto-restart of the Bluetooth system
	 *
	 * If the Bluetooth system is shut down, e.g. due to
	 * removal of the last Bluetooth HCI, auto-restart can automatically
	 * restart it if another Bluetooth HCI is connected.
	 *
	 * Specifically, for hosts that use suspend/resume, where USB
	 * Bluetooth HCIs are effectively disconnected prior to suspend
	 * and reconnected following resume, the auto-reconnect feature can
	 * improve the resilience of the BtHub Bluetooth system.
	 *
	 * @param autostart true to enable auto-restart, false otherwise.
	 *
	 * @sa BtHub::cb_NotifySystemState, GetAutoRestart();
	 */
	void SetAutoRestart(bool autostart);

	/**
	 * @brief Look up and create a BtDevice object
	 *
	 * @param[in] raddr Address of remote Bluetooth device
	 * @param create If no BtDevice object exists for the device,
	 * set this parameter to true to have a new one created.
	 *
	 * @return The BtDevice object associated with raddr, or NULL
	 * if no BtDevice object existed previously and create was set
	 * to false.
	 *
	 * @note If a device is found or created successfully, it is
	 * returned with an additional reference added to it, to prevent
	 * it from being inadvertently destroyed as per
	 * @ref bdlifecycle "life cycle management."  The caller is
	 * responsible for releasing the reference with BtDevice::Put().
	 */
	BtDevice *GetDevice(bdaddr_t const &raddr, bool create = true);

	/**
	 * @brief Look up and create a BtDevice object by Bluetooth
	 * address string
	 *
	 * This overload of GetDevice() is useful for configuration file
	 * parsing paths that create BtDevice objects for known devices.
	 * An ASCII hexadecimal Bluetooth address stored in a configuration
	 * file can be used directly by this routine.
	 *
	 * @param[in] raddr Address of the device represented as a string,
	 * e.g. "00:07:61:D2:55:37" but not "Motorola" or "Logitech."
	 * @param create If no BtDevice object exists for the device,
	 * set this parameter to true to have a new one created.
	 *
	 * @return The BtDevice object associated with raddr, or NULL
	 * if no BtDevice object existed previously and create was set
	 * to false.
	 *
	 * @note If a device is found or created successfully, it is
	 * returned with an additional reference added to it, to prevent
	 * it from being inadvertently destroyed as per
	 * @ref bdlifecycle "life cycle management."  The caller is
	 * responsible for releasing the reference with BtDevice::Put().
	 *
	 * @warning This function accepts a string representation of
	 * a Bluetooth address, e.g. "00:07:61:D2:55:37", NOT a Bluetooth
	 * name, e.g. "Motorola."
	 *
	 * @warning This function uses str2ba() to convert the string to a
	 * bdaddr_t, and malformed Bluetooth addresses are not reported.
	 */
	BtDevice *GetDevice(const char *raddr, bool create = true);

	/**
	 * @brief Query the first enumerable Bluetooth device record
	 *
	 * This interface, plus GetNextDevice(), permits clients to
	 * enumerate all known BtDevice objects.  This can be useful
	 * for producing a list or globally applying a preference change.
	 *
	 * @return A pointer to the first BtDevice in enumeration order,
	 * or NULL if no BtDevice objects exist.
	 *
	 * A Bluetooth device enumeration typically appears as:
	 * @code
	 *	for (BtDevice *dev = hubp->GetFirstDevice();
	 *	     dev != NULL;
	 *	     dev = hubp->GetNextDevice(dev)) {
	 *		// Do stuff with dev
	 *	}
	 * @endcode
	 *
	 * @warning The BtDevice object returned is not referenced.
	 * If the caller re-enters the event loop without adding a reference,
	 * the BtDevice may be destroyed, leaving the caller with a
	 * dangling pointer.  See
	 * @ref bdlifecycle "BtDevice life cycle management."
	 *
	 * @sa GetNextDevice()
	 */
	BtDevice *GetFirstDevice(void);

	/**
	 * @brief Query the succeeding enumerable Bluetooth device record
	 *
	 * @param devp Pointer to preceding device

	 * @return The BtDevice object that is next in enumeration order
	 * relative to devp, or NULL if devp is the last device object
	 * in the enumeration order.
	 *
	 * @warning The BtDevice object returned is not referenced.
	 * If the caller re-enters the event loop without adding a reference,
	 * the BtDevice may be destroyed, leaving the caller with a
	 * dangling pointer.  See
	 * @ref bdlifecycle "BtDevice life cycle management."
	 *
	 * @sa GetFirstDevice()
	 */
	BtDevice *GetNextDevice(BtDevice *devp);

	/**
	 * @brief Initiate an inquiry for discoverable Bluetooth devices
	 *
	 * This method begins the process of searching for discoverable
	 * Bluetooth devices.
	 *
	 * If an inquiry is successfully initiated, any devices discovered
	 * will be reported through BtHub::cb_InquiryResult.  The inquiry
	 * will persist for a fixed amount amount of time.  Completion is
	 * signaled by invoking BtHub::cb_InquiryResult with the BtDevice
	 * pointer set to @c 0.
	 *
	 * If the inquiry scan fails to start, either an error code will
	 * be returned from this method, or BtHub::cb_InquiryResult will
	 * be invoked after a short period of time with the BtDevice
	 * pointer set to @c 0, and an error code set as the second
	 * parameter.
	 *
	 * @param timeout_ms Time limit to place on inquiry.  Longer time
	 * limits will increase the chance of finding discoverable
	 * devices, up to a point.
	 *
	 * @retval 0 Inquiry has been initiated.  IsScanning() will
	 * now return true.
	 * @retval -EALREADY Inquiry is already in progress
	 * @retval -ESHUTDOWN Bluetooth system has been shut down
	 *
	 * @sa BtHub::cb_InquiryState, BtHub::cb_InquiryResult, IsScanning()
	 */
	int StartInquiry(int timeout_ms = 5000);

	/**
	 * @brief Halt and cancel an in-progress inquiry for discoverable
	 * Bluetooth devices
	 *
	 * If an inquiry is in progress, this method will cause it to be
	 * aborted.
	 *
	 * @retval 0 Inquiry has been successfully aborted.
	 * @retval -EALREADY No inquiry was in progress.
	 * @retval -ESHUTDOWN Bluetooth system has been shut down
	 */
	int StopInquiry(void);

	/**
	 * @brief Query whether a Bluetooth device inquiry is in progress
	 *
	 * When an inquiry scan is in progress, it is forbidden to
	 * request another scan, and StartInquiry() will fail.
	 *
	 * @retval true Device scan is in progress
	 * @retval false Device scan is not in progress
	 */
	bool IsScanning(void) const { return m_inquiry_task != 0; }

	static bool GetDeviceClassLocal(uint32_t &devclass);
	static bool SetDeviceClassLocal(uint32_t devclass);
};


/**
 * @brief Base Class for Bluetooth Reference-Counted Objects
 * @ingroup hfp
 *
 * This class forms the base class of all reference-counted
 * objects used in this module.  See also
 * @ref bdlifecycle "BtDevice life cycle management."
 *
 * These objects have three standard features:
 * - Reference counting
 * - Asynchronous deletion when the reference count drops to zero
 * - Private pointers for the use of clients
 *
 * @sa BtDevice, BtSession, HfpSession
 */
class BtManaged {
	friend class BtHub;
private:
	ListItem		m_del_links;
	BtHub			*m_hub;
	int			m_refs;
	void			*m_priv;

	void DeadRemove(void);

public:
	/** Standard constructor */
	BtManaged(BtHub *hubp) : m_hub(hubp), m_refs(1), m_priv(0) {}
	/** Standard destructor */
	virtual ~BtManaged(void);

	/**
	 * @brief Object destruction notification callback
	 *
	 * This callback is invoked proir to deletion of the managed
	 * object, so that clients may release associated resources.
	 * This callback occurs in the context of a timer event, and
	 * complies with the @ref callbacks "rule of not invoking client callbacks in a nested fashion."
	 *
	 * The decision to destroy the object is final.  The client may
	 * not attempt to preserve the object by acquiring additional
	 * references from this callback.
	 */
	Callback<void, BtManaged*> cb_NotifyDestroy;

	/** Query the presiding BtHub */
	BtHub *GetHub(void) const { return m_hub; }

	/** Query the dispatcher interface of the presiding BtHub */
	DispatchInterface *GetDi(void) const { return m_hub->GetDi(); }

	/**
	 * @brief Query the private pointer associated with the object
	 *
	 * All BtManaged derived objects have a single pointer field
	 * reserved for the use of clients.  It is always initialized
	 * to zero when the object is constructed, and clients may
	 * assign it as they wish.
	 *
	 * @returns The current value of the private pointer
	 */
	void *GetPrivate(void) const { return m_priv; }

	/**
	 * @brief Assign the private pointer associated with the object
	 *
	 * All BtManaged derived objects have a single pointer field
	 * reserved for the use of clients.  It is always initialized
	 * to zero when the object is constructed, and clients may
	 * assign it as they wish.
	 *
	 * @param priv New value to assign to the private pointer
	 */
	void SetPrivate(void *priv) { m_priv = priv; }

	/**
	 * @brief Increment the reference count
	 *
	 * As per @ref managed "managed objects,"
	 * BtManaged derived objects are not deleted so long as they
	 * have a positive reference count.
	 *
	 * @sa Put()
	 */
	void Get(void) {
		assert(m_refs >= 0);
		if (!m_refs++)
			DeadRemove();
	}

	/**
	 * @brief Decrement the reference count
	 *
	 * As per @ref managed "managed objects,"
	 * when the object's reference count reaches zero, the object
	 * will be destroyed in the context of a timer event.
	 *
	 * @sa Get()
	 */
	void Put(void);
};


/**
 * @brief Bluetooth Device Record
 * @ingroup hfp
 *
 * This class represents a remote Bluetooth device that can connect
 * to local services or be connected to by local service handlers.
 * It supports numerous use cases:
 * - Identifying a device to be connected to, or an incoming connection
 * - Interrogating the Bluetooth name of the device
 *
 * @section bdlifecycle Life Cycle
 *
 * The associated BtHub object manages the life cycle of BtDevice
 * objects.  The BtHub keeps an index of BtDevice objects, and ensures
 * that every object has a unique Bluetooth address key.
 *
 * All BtDevice objects are instantiated by GetDevice() with create=true.
 * Service handlers such as HfpService will perform the instantiation
 * themselves in the case of devices remotely initating connections.
 *
 * BtDevice objects are reference-counted, and are always destroyed
 * when their reference count drops to zero.  Destruction is not
 * immediate and is executed in the context of a timer event.
 *
 * Service frontends such as HfpSession will retain references on their
 * underlying BtDevice objects, and the BtDevice will not be destroyed
 * until the service frontend is destroyed and its reference is removed.
 *
 * Clients may override the instantiation path for BtDevice objects by
 * registering their own factory method to BtHub::cb_BtDeviceFactory.
 * As part of a specialized factory, clients may use the default factory
 * method, BtHub::DefaultDevFactory().  A specialized factory may be
 * used to:
 * - Instantiate a class derived from BtDevice with additional
 * functionality.
 * - Register all relevant callbacks with the BtDevice object from a
 * single path, and before any may be invoked.
 */
class BtDevice : public BtManaged {
	friend class BtHub;
	friend class BtService;
	friend class BtSession;

private:
	ListItem		m_index_links;
	bdaddr_t		m_bdaddr;
	int			m_refs;
	ListItem		m_sessions;
	bool			m_inquiry_found;
	uint16_t		m_inquiry_clkoff;
	uint8_t			m_inquiry_pscan;
	uint8_t			m_inquiry_pscan_rep;
	uint32_t		m_inquiry_class;
	bool			m_name_resolved;
	HciTask			*m_name_task;

	enum { DEVICE_MAX_NAMELEN = 249 };
	char			m_dev_name[DEVICE_MAX_NAMELEN];

	void NameResolutionResult(HciTask *taskp);

	void AddSession(BtSession *sessp);
	void RemoveSession(BtSession *sessp);
	BtSession *FindSession(BtService const *svcp) const;

	void __DisconnectAll(bool notify);

public:
	/**
	 * @brief Standard constructor
	 *
	 * This method will only be useful to clients that define
	 * subclasses of BtDevice and provide a callback for
	 * BtHub::cb_BtDeviceFactory.
	 *
	 * @param[in] hubp BtHub with which the object will be associated
	 * @param[in] bdaddr Bluetooth address of the device, also provided
	 * by BtHub::cb_BtDeviceFactory.
	 *
	 * @sa BtHub::cb_BtDeviceFactory
	 */
	BtDevice(BtHub *hubp, bdaddr_t const &bdaddr);

	/**
	 * @brief Standard destructor
	 */
	virtual ~BtDevice();

	/**
	 * @brief Disconnect all active sessions
	 *
	 * Causes all sessions associated with this device to be
	 * disconnected if they are in a connected state.  No client
	 * callbacks will be issued for any sessions.
	 */
	void DisconnectAll(void) { __DisconnectAll(false); }

	/**
	 * @brief Notification that a query of the Bluetooth name of the device
	 * has completed
	 *
	 * Name resolution is normally initiated by ResolveName().
	 *
	 * This is particularly useful for device scanning, where a
	 * BtDevice is created for each inquiry result, but no Bluetooth
	 * names are initially available.  As Bluetooth names are
	 * resolved, this callback can be used to update user interface
	 * widgets.
	 *
	 * @param char* Bluetooth name of the device, or NULL if name
	 * resolution failed.  This value is also available by calling
	 * GetName().
	 *
	 * @sa ResolveName(), GetName(), IsNameResolved()
	 */
	Callback<void, BtDevice *, const char *>	cb_NotifyNameResolved;

	/* Get device name */
	/**
	 * @brief Query the Bluetooth name of the device
	 *
	 * If the device's Bluetooth name has been resolved, it will be
	 * returned, otherwise a string representation of the device's
	 * bdaddr will be returned.  Name resolution may be requested by
	 * ResolveName().
	 *
	 * @return A pointer to a buffer containing the device name.
	 * The buffer will remain valid until the next event handling
	 * loop invocation.
	 *
	 * @sa IsNameResolved(), ResolveName(),
	 * BtDevice::cb_NotifyNameResolved
	 */
	const char *GetName(void) const { return m_dev_name; }

	/**
	 * @brief Query the Bluetooth address of the device
	 *
	 * @return A reference to a bdaddr_t containing the Bluetooth
	 * address of the device.  Can be passed as an argument to
	 * bacmp() and ba2str().
	 *
	 * @sa GetName()
	 */
	bdaddr_t const &GetAddr(void) const { return m_bdaddr; }

	/**
	 * @brief Query the Bluetooth address of the device in string form
	 *
	 * This overload of GetAddr() is useful for configuration file
	 * writing paths that need to record a textual Bluetooth address.
	 *
	 * @param[out] namebuf A buffer to be filled with a string
	 * representation of the device's bdaddr.
	 *
	 * @sa GetName()
	 */
	void GetAddr(char (&namebuf)[32]) const;

	/**
	 * @brief Query whether name resolution is pending
	 */
	bool IsNameResolving(void) const { return m_name_task != 0; }

	/**
	 * @brief Query whether the Bluetooth name of the device has been
	 * resolved
	 *
	 * @retval true Bluetooth name has been resolved, GetName() will
	 * return the textual Bluetooth name.
	 * @retval false Bluetooth name has not been resolved, GetName()
	 * will return a string representation of the device's bdaddr.
	 *
	 * @sa GetName(), ResolveName(), BtDevice::cb_NotifyNameResolved
	 */
	bool IsNameResolved(void) const { return m_name_resolved; }

	/**
	 * @brief Request that the Bluetooth name of the device be resolved
	 *
	 * @retval true Name resolution has been initiated
	 * @retval false Name resolution not initiated.  Reasons might include:
	 * - A name resolution is already in progress
	 * - Bluetooth service has been shut down
	 *
	 * @sa GetName(), IsNameResolved(), BtDevice::cb_NotifyNameResolved
	 */
	bool ResolveName(void);

	uint32_t GetDeviceClass(void) const { return m_inquiry_class; }
};


/**
 * @brief Service object base class
 * @ingroup hfp
 */
class BtService {
	friend class BtHub;
	friend class BtSession;

private:
	ListItem		m_links;

protected:
	ListItem		m_sessions;
	BtHub			*m_hub;

	void AddSession(BtSession *sessp);
	void RemoveSession(BtSession *sessp);
	BtSession *FindSession(BtDevice const *devp) const;

	virtual bool Start(void) = 0;
	virtual void Stop(void) = 0;

	BtService(void);
	virtual ~BtService();

public:
	/** Query the presiding BtHub */
	BtHub *GetHub(void) const { return m_hub; }

	/** Query the dispatcher interface of the presiding BtHub */
	DispatchInterface *GetDi(void) const { return GetHub()->GetDi(); }

	/** Query the first session associated with the service */
	BtSession *GetFirstSession(void) const;

	/** Enumerate the next session associated with the service */
	BtSession *GetNextSession(BtSession *) const;
};


/**
 * @brief Session object base class
 * @ingroup hfp
 */
class BtSession : public BtManaged {
	friend class BtDevice;
	friend class BtService;

private:
	ListItem		m_dev_links;
	BtDevice		*m_dev;

protected:
	BtService		*m_svc;
	ListItem		m_svc_links;
	virtual void __Disconnect(bool notify, bool voluntary = false) = 0;

public:
	BtSession(BtService *svcp, BtDevice *devp);
	virtual ~BtSession();

	/**
	 * @brief Query the BtDevice associated with the session
	 *
	 * @returns A pointer to the BtDevice to which the session is
	 * attached.
	 *
	 * @note The BtDevice returned does not have a reference added
	 * to it.  Callers should take care to avoid dangling pointers.
	 */
	BtDevice *GetDevice(void) const { return m_dev; }

	/**
	 * @brief Query the service associated with the session
	 *
	 * @returns A pointer to the BtService object associated with
	 * the session.  A BtService object is not very useful, as the
	 * real service object is derived from BtServce, and added
	 * functionality is not exposed through BtService.
	 */
	BtService *GetService(void) const { return m_svc; }

	/**
	 * @brief Request disconnection of the session
	 *
	 * This abstract interface allows one to request any
	 * connection-oriented session object derived from BtSession
	 * to disconnect itself.
	 *
	 * @param voluntary Whether the disconnection should be
	 * presented as voluntary and explicit, i.e. selecting
	 * disconnect on a list, or involuntary, i.e. exiting
	 * Bluetooth radio range.
	 */
	void Disconnect(bool voluntary = true)
		{ __Disconnect(false, voluntary); }
};


} /* namespace libhfp */
#endif /* !defined(__LIBHFP_BT_H__) */
