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

/*
 * This package is specific to Linux and the Qualcomm BlueZ Bluetooth
 * stack for Linux.  It is not specific to any GUI or application
 * framework, and can be adapted to most by creating an appropriate
 * DispatchInterface class.
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>

#include <libhfp/rfcomm.h>

namespace libhfp {

bool
SetLinkModeOptions(int rsock, bool master, rfcomm_secmode_t sec,
		   ErrorInfo *error)
{
	uint32_t linkmode;
	socklen_t sl;

	sl = sizeof(linkmode);
	if (getsockopt(rsock, SOL_RFCOMM, RFCOMM_LM, &linkmode, &sl) < 0) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_BT,
				   LIBHFP_ERROR_BT_SYSCALL,
				   "getsockopt RFCOMM_LM: %s",
				   strerror(errno));
		return false;
	}

	if (master) {
		linkmode |= RFCOMM_LM_MASTER;
	} else {
		linkmode &= ~RFCOMM_LM_MASTER;
	}

	switch (sec) {
	case RFCOMM_SEC_NONE:
		linkmode &= ~(RFCOMM_LM_AUTH|RFCOMM_LM_ENCRYPT);
		break;
	case RFCOMM_SEC_AUTH:
		linkmode |= RFCOMM_LM_AUTH;
		linkmode &= ~RFCOMM_LM_ENCRYPT;
		break;
	case RFCOMM_SEC_CRYPT:
		linkmode |= (RFCOMM_LM_AUTH|RFCOMM_LM_ENCRYPT);
		break;
	}

	sl = sizeof(linkmode);
	if (setsockopt(rsock, SOL_RFCOMM, RFCOMM_LM, &linkmode, sl) < 0) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_BT,
				   LIBHFP_ERROR_BT_SYSCALL,
				   "setsockopt RFCOMM_LM: %s",
				   strerror(errno));
		return false;
	}

	return true;
}


RfcommService::
RfcommService(uint16_t search_svclass_id)
	: BtService(), m_rfcomm_listen(-1), m_rfcomm_listen_channel(0),
	  m_rfcomm_listen_not(0), m_secmode(RFCOMM_SEC_NONE),
	  m_search_svclass_id(search_svclass_id), m_bt_master(true)
{
}

RfcommService::
~RfcommService()
{
	/* Make sure that we have been cleaned up after */
	assert(m_rfcomm_listen < 0);
	assert(m_rfcomm_listen_not == 0);
}

bool RfcommService::
SetSecMode(rfcomm_secmode_t secmode, ErrorInfo *error)
{
	if ((secmode != RFCOMM_SEC_NONE) &&
	    (secmode != RFCOMM_SEC_AUTH) &&
	    (secmode != RFCOMM_SEC_CRYPT)) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_EVENTS,
				   LIBHFP_ERROR_EVENTS_BAD_PARAMETER,
				   "Invalid RFCOMM security mode");
		return false;
	}

	if (m_secmode != secmode) {
		if ((m_rfcomm_listen >= 0) &&
		    !SetLinkModeOptions(m_rfcomm_listen, m_bt_master,
					secmode, error)) {
			return false;
		}
		m_secmode = secmode;
	}
	return true;
}

void RfcommService::
RfcommListenNotify(SocketNotifier *notp, int fh)
{
	BtDevice *devp;
	RfcommSession *sessp = 0;
	struct sockaddr_rc raddr;
	socklen_t al;
	int rsock;
	bool res;

	assert(notp == m_rfcomm_listen_not);
	assert(fh == m_rfcomm_listen);

	al = sizeof(raddr);
	rsock = accept(fh, (struct sockaddr*)&raddr, &al);
	if (rsock < 0) {
		return;
	}

	devp = GetHub()->GetDevice(raddr.rc_bdaddr, true);
	if (!devp) {
		close(rsock);
		return;
	}

	/* Instantiate a session */
	sessp = SessionFactory(devp);
	if (!sessp) {
		close(rsock);
		devp->Put();
		return;
	}

	res = sessp->RfcommAccept(rsock);
	sessp->Put();

	if (!res)
		close(rsock);
}

bool RfcommService::
RfcommListen(ErrorInfo *error, uint8_t channel)
{
	struct sockaddr_rc raddr;
	BtHci *hcip;
	int rsock = -1;
	socklen_t al;

	assert(GetHub());
	assert(m_rfcomm_listen == -1);

	hcip = GetHub()->GetHci();
	if (!hcip) {
		if (error)
			error->SetNoMem();
		return false;
	}

	rsock = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if (rsock < 0) {
		rsock = errno;
		if (rsock == EPROTONOSUPPORT) {
			GetDi()->LogError(error,
					  LIBHFP_ERROR_SUBSYS_BT,
					  LIBHFP_ERROR_BT_NO_SUPPORT,
					  "Your kernel is not configured with "
					  "support for RFCOMM sockets.");
			GetHub()->SetAutoRestart(false);
			return false;
		}
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_BT,
				 LIBHFP_ERROR_BT_SYSCALL,
				 "Create RFCOMM socket: %s",
				 strerror(rsock));
		return false;
	}

	memset(&raddr, 0, sizeof(raddr));
	raddr.rc_family = AF_BLUETOOTH;
	bacpy(&raddr.rc_bdaddr, &(hcip->GetAddr()));

	if (channel) {
		raddr.rc_channel = channel;
		if (bind(rsock, (struct sockaddr*)&raddr,
			 sizeof(raddr)) < 0) {
			if (errno != EADDRINUSE) {
				GetDi()->LogDebug(error,
						  LIBHFP_ERROR_SUBSYS_BT,
					  LIBHFP_ERROR_BT_SERVICE_CONFLICT,
						  "Bind RFCOMM socket: "
						  "Channel %d is in use",
						  channel);
			} else {
				GetDi()->LogWarn(error,
						 LIBHFP_ERROR_SUBSYS_BT,
						 LIBHFP_ERROR_BT_SYSCALL,
						 "Bind RFCOMM socket: %s",
						 strerror(errno));
			}
			goto failed;
		}
	}
	else for (raddr.rc_channel = 3;
		  raddr.rc_channel < 32;
		  raddr.rc_channel++) {

		if (bind(rsock, (struct sockaddr*)&raddr, sizeof(raddr)) < 0) {
			if (errno != EADDRINUSE) {
				GetDi()->LogWarn(error,
						 LIBHFP_ERROR_SUBSYS_BT,
						 LIBHFP_ERROR_BT_SYSCALL,
						 "Bind RFCOMM socket: %s",
						 strerror(errno));
				goto failed;
			}
			continue;
		}
		break;
	}

	if (!SetLinkModeOptions(rsock, m_bt_master, m_secmode, error)) {
		GetDi()->LogWarn("Error setting RFCOMM link mode "
				 "options");
		goto failed;
	}

	if (listen(rsock, 1) < 0) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_BT,
				 LIBHFP_ERROR_BT_SYSCALL,
				 "Set RFCOMM socket to listen: %s",
				 strerror(errno));
		goto failed;
	}

	/* Query the assigned channel of the RFCOMM */
	al = sizeof(raddr);
	if (getsockname(rsock, (struct sockaddr*)&raddr, &al) < 0) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_BT,
				 LIBHFP_ERROR_BT_SYSCALL,
				 "Query RFCOMM listener local address: %s",
				 strerror(errno));
		goto failed;
	}

	m_rfcomm_listen_not = GetDi()->NewSocket(rsock, false);
	if (!m_rfcomm_listen_not) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_BT,
				 LIBHFP_ERROR_BT_SYSCALL,
				 "Could not create RFCOMM listen notifier");
		goto failed;
	}	

	m_rfcomm_listen_not->Register(this, &RfcommService::
				      RfcommListenNotify);

	m_rfcomm_listen = rsock;
	m_rfcomm_listen_channel = raddr.rc_channel;
	return true;

failed:
	if (rsock >= 0)
		close(rsock);
	return false;
}

void RfcommService::
RfcommCleanup(void)
{
	if (m_rfcomm_listen_not) {
		delete m_rfcomm_listen_not;
		m_rfcomm_listen_not = 0;
	}
	if (m_rfcomm_listen >= 0) {
		close(m_rfcomm_listen);
		m_rfcomm_listen = -1;
	}
}

bool RfcommService::
RfcommSdpSetAccessProto(sdp_record_t *svcrec, uint8_t channel)
{
	uuid_t l2cap_uuid, rfcomm_uuid;
	sdp_list_t *l2cap_list = 0, *rfcomm_list = 0, *root_list = 0,
		*proto_list = 0;
	sdp_data_t *rchannel = 0;
	bool result = false;

	do {
		sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
		if (!(l2cap_list = sdp_list_append(NULL, &l2cap_uuid)))
			break;
		if (!(proto_list = sdp_list_append(NULL, l2cap_list)))
			break;

		sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
		if (!(rchannel = sdp_data_alloc(SDP_UINT8, &channel)))
			break;
		if (!(rfcomm_list = sdp_list_append(NULL, &rfcomm_uuid)))
			break;
		if (!(rfcomm_list = sdp_list_append(rfcomm_list, rchannel)))
			break;
		if (!(proto_list = sdp_list_append(proto_list, rfcomm_list)))
			break;

		// attach protocol information to service record
		if (!(root_list = sdp_list_append(NULL, proto_list)))
			break;
		if (sdp_set_access_protos(svcrec, root_list) < 0)
			break;

		result = true;
	} while (0);

	if (l2cap_list)
		sdp_list_free(l2cap_list, 0);
	if (rchannel)
		sdp_data_free(rchannel);
	if (rfcomm_list)
		sdp_list_free(rfcomm_list, 0);
	if (proto_list)
		sdp_list_free(proto_list, 0);
	if (root_list)
		sdp_list_free(root_list, 0);

	return result;
}

RfcommSession *RfcommService::
GetSession(BtDevice *devp, bool create)
{
	RfcommSession *sessp;

	assert(GetHub());
	sessp = FindSession(devp);
	if (!sessp && create) {
		sessp = SessionFactory(devp);
		if (!sessp)
			return 0;
	}
	return sessp;
}

RfcommSession *RfcommService::
GetSession(bdaddr_t const &addr, bool create)
{
	BtDevice *devp;
	RfcommSession *sessp;

	assert(GetHub());
	devp = GetHub()->GetDevice(addr, create);
	if (!devp)
		return 0;
	sessp = GetSession(devp, create);
	devp->Put();
	return sessp;
}

RfcommSession *RfcommService::
GetSession(const char *addrstr, bool create)
{
	BtDevice *devp;
	RfcommSession *sessp;

	assert(GetHub());
	devp = GetHub()->GetDevice(addrstr, create);
	if (!devp)
		return 0;
	sessp = GetSession(devp, create);
	devp->Put();
	return sessp;
}


RfcommSession::
RfcommSession(RfcommService *svcp, BtDevice *devp)
	: BtSession(svcp, devp), m_rfcomm_state(RFC_Disconnected),
	  m_rfcomm_sdp_task(0), m_rfcomm_inbound(false),
	  m_rfcomm_dcvoluntary(false), m_rfcomm_sock(-1),
	  m_rfcomm_not(0), m_rfcomm_secmode(RFCOMM_SEC_NONE)
	  
{
}


RfcommSession::
~RfcommSession()
{
	assert(m_rfcomm_state == RFC_Disconnected);
	assert(!m_rfcomm_sdp_task);
	assert(!m_rfcomm_not);
}


bool RfcommSession::
RfcommSdpLookupChannel(ErrorInfo *error)
{
	SdpTask *taskp;

	assert(GetHub());
	assert(m_rfcomm_state == RFC_Disconnected);

	taskp = new SdpTask;
	if (taskp == 0) {
		if (error)
			error->SetNoMem();
		return false;
	}

	taskp->m_params.m_tasktype = SdpTaskParams::ST_SDP_LOOKUP;
	bacpy(&taskp->m_params.m_bdaddr, &(GetDevice()->GetAddr()));
	taskp->m_params.m_svclass_id = GetService()->m_search_svclass_id;
	taskp->m_params.m_timeout_ms = 30000;
	taskp->cb_Result.Register(this, &RfcommSession::
				  RfcommSdpLookupChannelComplete);

	if (!GetHub()->SdpTaskSubmit(taskp, error)) {
		delete taskp;
		m_rfcomm_state = RFC_Disconnected;
		return false;
	}

	Get();
	m_rfcomm_state = RFC_SdpLookupChannel;
	m_rfcomm_sdp_task = taskp;
	return true;
}

void RfcommSession::
RfcommSdpLookupChannelComplete(SdpTask *taskp)
{
	uint16_t channel;
	ErrorInfo error;

	assert(taskp == m_rfcomm_sdp_task);
	assert(m_rfcomm_state == RFC_SdpLookupChannel);

	m_rfcomm_inbound = false;

	if (taskp->m_params.m_errno) {
		GetDi()->LogDebug(&error,
				  LIBHFP_ERROR_SUBSYS_BT,
				  LIBHFP_ERROR_BT_SYSCALL,
				  "SDP lookup failure: %s",
				  strerror(taskp->m_params.m_errno));
		delete taskp;
		m_rfcomm_sdp_task = 0;
		__Disconnect(&error, false);
		return;
	}

	channel = taskp->m_params.m_channel;
	if (taskp->m_params.m_supported_features_present) {
		GetDi()->LogDebug("SDP: Supported features: %x",
			       taskp->m_params.m_supported_features);
		SdpSupportedFeatures(taskp->m_params.m_supported_features);
	}

	delete taskp;
	m_rfcomm_sdp_task = 0;

	m_rfcomm_state = RFC_Disconnected;
	if (!RfcommConnect(channel, &error)) {
		Get();
		m_rfcomm_state = RFC_SdpLookupChannel;
		__Disconnect(&error, false);
	}

	/*
	 * Drop the SDP lookup reference:
	 * If RfcommConnect() succeeded, it acquired its own.
	 */
	Put();
}


bool RfcommSession::
RfcommConnect(uint8_t channel, ErrorInfo *error)
{
	struct sockaddr_rc raddr;
	BtHci *hcip;
	int rsock = -1;

	assert(m_rfcomm_state == RFC_Disconnected);

	hcip = GetHub()->GetHci();
	if (!hcip) {
		error->SetNoMem();
		return false;
	}

	rsock = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if (rsock < 0) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_BT,
				 LIBHFP_ERROR_BT_SYSCALL,
				 "Create RFCOMM socket: %s",
				 strerror(errno));
		goto failure;
	}

	memset(&raddr, 0, sizeof(raddr));
	raddr.rc_family = AF_BLUETOOTH;
	bacpy(&raddr.rc_bdaddr, &(hcip->GetAddr()));
	raddr.rc_channel = 0;

	if (bind(rsock, (struct sockaddr*)&raddr, sizeof(raddr)) < 0) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_BT,
				 LIBHFP_ERROR_BT_SYSCALL,
				 "Bind RFCOMM socket: %s",
				 strerror(errno));
		goto failure;
	}

	if (!SetLinkModeOptions(rsock,
				GetService()->m_bt_master,
				GetService()->m_secmode, error)) {
		GetDi()->LogWarn("Error setting RFCOMM link mode options");
		goto failure;
	}

	m_rfcomm_secmode = GetService()->m_secmode;

	if (!SetNonBlock(rsock, true)) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_BT,
				 LIBHFP_ERROR_BT_SYSCALL,
				 "Set socket nonblocking: %s",
				 strerror(errno));
		goto failure;
	}

	raddr.rc_family = AF_BLUETOOTH;
	bacpy(&raddr.rc_bdaddr, &(GetDevice()->GetAddr()));
	raddr.rc_channel = channel;

	if (connect(rsock, (struct sockaddr*)&raddr, sizeof(raddr)) < 0) {
		if ((errno != EINPROGRESS) && (errno != EAGAIN)) {
			GetDi()->LogWarn(error,
					 LIBHFP_ERROR_SUBSYS_BT,
					 LIBHFP_ERROR_BT_SYSCALL,
					 "Connect RFCOMM socket: %s",
					 strerror(errno));
			goto failure;
		}

		/* We wait for the socket to become _writable_ */
		m_rfcomm_not = GetDi()->NewSocket(rsock, true);
		if (!m_rfcomm_not) {
			if (error)
				error->SetNoMem();
			goto failure;
		}

		m_rfcomm_not->Register(this, &RfcommSession::
				       RfcommConnectNotify);
	}

	Get();

	m_rfcomm_state = RFC_Connecting;
	m_rfcomm_inbound = false;
	m_rfcomm_sock = rsock;

	if (!m_rfcomm_not)
		RfcommConnectNotify(NULL, rsock);

	return true;

failure:
	if (rsock >= 0) { close(rsock); }
	assert(!error || error->IsSet());
	return false;
}

bool RfcommSession::
RfcommAccept(int sock)
{
	if (m_rfcomm_state != RFC_Disconnected) {
		char bda[32];

		ba2str(&GetDevice()->GetAddr(), bda);
		GetDi()->LogWarn("Refusing connection from "
				 "non-disconnected device %s", bda);
		return false;
	}

	m_rfcomm_sock = sock;
	m_rfcomm_inbound = true;

	(void) SetNonBlock(m_rfcomm_sock, false);

	m_rfcomm_state = RFC_Connected;
	Get();
	NotifyConnectionState(0);
	return true;
}

void RfcommSession::
RfcommConnectNotify(SocketNotifier *notp, int fh)
{
	int sockerr;
	socklen_t sl;
	ErrorInfo error;

	assert(m_rfcomm_state == RFC_Connecting);

	if (notp) {
		assert(notp == m_rfcomm_not);
		delete m_rfcomm_not;
		m_rfcomm_not = 0;
	}
	assert(!m_rfcomm_not);

	(void) SetNonBlock(m_rfcomm_sock, false);

	m_rfcomm_state = RFC_Connected;

	/* Check for a connect error */
	sl = sizeof(sockerr);
	if (getsockopt(m_rfcomm_sock, SOL_SOCKET, SO_ERROR,
		       &sockerr, &sl) < 0) {
		GetDi()->LogWarn(&error,
				 LIBHFP_ERROR_SUBSYS_BT,
				 LIBHFP_ERROR_BT_SYSCALL,
				 "Retrieve status of RFCOMM connect: %s",
				 strerror(errno));
		__Disconnect(&error, false);
		return;
	}

	if (sockerr) {
		GetDi()->LogWarn(&error,
				 LIBHFP_ERROR_SUBSYS_BT,
				 LIBHFP_ERROR_BT_SYSCALL,
				 "RFCOMM connect: %s", strerror(sockerr));
		__Disconnect(&error, false);
		return;
	}

	NotifyConnectionState(0);
}

bool RfcommSession::
RfcommConnect(ErrorInfo *error)
{
	if (m_rfcomm_state != RFC_Disconnected)
		return false;

	return RfcommSdpLookupChannel(error);
}

void RfcommSession::
SdpSupportedFeatures(uint16_t features)
{
	/* We don't care about this, but subclasses might */
}

void RfcommSession::
__Disconnect(ErrorInfo *reason, bool voluntary)
{
	if (m_rfcomm_state != RFC_Disconnected) {
		if (m_rfcomm_not) {
			assert(m_rfcomm_state > RFC_SdpLookupChannel);
			assert(m_rfcomm_sock >= 0);
			delete m_rfcomm_not;
			m_rfcomm_not = 0;
		}
		if (m_rfcomm_sock >= 0) {
			assert(m_rfcomm_state > RFC_SdpLookupChannel);
			close(m_rfcomm_sock);
			m_rfcomm_sock = -1;
		}
		if (m_rfcomm_sdp_task) {
			assert(m_rfcomm_state == RFC_SdpLookupChannel);
			GetHub()->SdpTaskCancel(m_rfcomm_sdp_task);
			delete m_rfcomm_sdp_task;
			m_rfcomm_sdp_task = 0;
		}
		m_rfcomm_dcvoluntary = voluntary;
		m_rfcomm_state = RFC_Disconnected;

		NotifyConnectionState(reason);

		Put();
	}
}

} /* namespace libhfp */
