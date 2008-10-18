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
SetLinkModeOptions(int rsock, bool master, rfcomm_secmode_t sec)
{
	uint32_t linkmode;
	socklen_t sl;

	sl = sizeof(linkmode);
	if (getsockopt(rsock, SOL_RFCOMM, RFCOMM_LM, &linkmode, &sl) < 0) {
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
SetSecMode(rfcomm_secmode_t secmode)
{
	if (m_secmode != secmode) {
		if ((m_rfcomm_listen >= 0) &&
		    !SetLinkModeOptions(m_rfcomm_listen, m_bt_master,
					secmode)) {
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

	if (!sessp->RfcommAccept(rsock)) {
		close(rsock);
		sessp->Put();
	}
}

bool RfcommService::
RfcommListen(uint8_t channel)
{
	struct sockaddr_rc raddr;
	int rsock = -1;
	socklen_t al;

	assert(GetHub());
	assert(m_rfcomm_listen == -1);

	rsock = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if (rsock < 0) {
		GetDi()->LogWarn("Create RFCOMM socket");
		return false;
	}

	memset(&raddr, 0, sizeof(raddr));
	raddr.rc_family = AF_BLUETOOTH;
	bacpy(&raddr.rc_bdaddr, BDADDR_ANY);

	if (channel) {
		raddr.rc_channel = channel;
		if (bind(rsock, (struct sockaddr*)&raddr,
			 sizeof(raddr)) < 0) {
			if (errno != EADDRINUSE) {
				GetDi()->LogDebug("Bind RFCOMM socket: "
						  "Channel %d is in use\n",
						  channel);
			} else {
				GetDi()->LogWarn("Bind RFCOMM socket: %d\n",
						 errno);
			}
			goto failed;
		}
	}
	else for (raddr.rc_channel = 3;
		  raddr.rc_channel < 32;
		  raddr.rc_channel++) {

		if (bind(rsock, (struct sockaddr*)&raddr, sizeof(raddr)) < 0) {
			if (errno != EADDRINUSE) {
				GetDi()->LogWarn("Bind RFCOMM socket: %d\n",
						 errno);
				goto failed;
			}
			continue;
		}
		break;
	}

	if (!SetLinkModeOptions(rsock, m_bt_master, m_secmode)) {
		GetDi()->LogWarn("Error setting RFCOMM link mode "
				 "options: %d\n", errno);
		goto failed;
	}
	
	if (listen(rsock, 1) < 0) {
		GetDi()->LogWarn("Set RFCOMM socket to listen: %d\n", errno);
		goto failed;
	}

	/* Query the assigned channel of the RFCOMM */
	al = sizeof(raddr);
	if (getsockname(rsock, (struct sockaddr*)&raddr, &al) < 0) {
		GetDi()->LogWarn("Query RFCOMM listener local address: %d\n",
				 errno);
		goto failed;
	}

	m_rfcomm_listen_not = GetDi()->NewSocket(rsock, false);
	if (!m_rfcomm_listen_not) {
		GetDi()->LogWarn("Could not create RFCOMM listen notifier\n");
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
RfcommSdpLookupChannel(void)
{
	SdpTask *taskp;

	assert(GetHub());
	assert(m_rfcomm_state == RFC_SdpLookupChannel);

	taskp = new SdpTask;
	if (taskp == 0)
		return false;

	taskp->m_params.m_tasktype = SdpTaskParams::ST_SDP_LOOKUP;
	bacpy(&taskp->m_params.m_bdaddr, &(GetDevice()->GetAddr()));
	taskp->m_params.m_svclass_id = GetService()->m_search_svclass_id;
	taskp->m_params.m_timeout_ms = 30000;
	taskp->cb_Result.Register(this, &RfcommSession::
				  RfcommSdpLookupChannelComplete);

	if (GetHub()->SdpTaskSubmit(taskp)) {
		delete taskp;
		m_rfcomm_state = RFC_Disconnected;
		return false;
	}

	Get();
	m_rfcomm_sdp_task = taskp;
	return true;
}

void RfcommSession::
RfcommSdpLookupChannelComplete(SdpTask *taskp)
{
	uint16_t channel;

	assert(taskp == m_rfcomm_sdp_task);
	assert(m_rfcomm_state == RFC_SdpLookupChannel);

	m_rfcomm_inbound = false;

	if (taskp->m_params.m_errno) {
		GetDi()->LogDebug("SDP lookup failure: %d\n",
				  taskp->m_params.m_errno);
		delete taskp;
		m_rfcomm_sdp_task = 0;
		__Disconnect(true, false);
		return;
	}

	channel = taskp->m_params.m_channel;
	if (taskp->m_params.m_supported_features_present) {
		GetDi()->LogDebug("SDP: Supported features: %x\n",
			       taskp->m_params.m_supported_features);
		SdpSupportedFeatures(taskp->m_params.m_supported_features);
	}

	delete taskp;
	m_rfcomm_sdp_task = 0;

	m_rfcomm_state = RFC_Disconnected;
	if (!RfcommConnect(channel)) {
		Get();
		__Disconnect(true, false);
	}

	/*
	 * Drop the SDP lookup reference:
	 * Either RfcommConnect() acquired its own, or we acquired
	 * another one in the error path.
	 */
	Put();
}


bool RfcommSession::
RfcommConnect(uint8_t channel)
{
	struct sockaddr_rc raddr;
	int hciid;
	int rsock = -1;

	assert(m_rfcomm_state == RFC_Disconnected);

	hciid = hci_get_route((bdaddr_t *) &(GetDevice()->GetAddr()));
	if (hciid < 0) {
		GetDi()->LogWarn("No HCI route for BDADDR\n");
		goto failure;
	}

	rsock = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if (rsock < 0) {
		GetDi()->LogWarn("Create RFCOMM socket: %d\n", errno);
		goto failure;
	}

	memset(&raddr, 0, sizeof(raddr));
	raddr.rc_family = AF_BLUETOOTH;
	if (hci_devba(hciid, &raddr.rc_bdaddr) < 0) {
		GetDi()->LogWarn("Get HCI adapter address: %d\n", errno);
		goto failure;
	}
	raddr.rc_channel = 0;

	if (bind(rsock, (struct sockaddr*)&raddr, sizeof(raddr)) < 0) {
		GetDi()->LogWarn("Bind RFCOMM socket: %d\n", errno);
		goto failure;
	}

	if (!SetLinkModeOptions(rsock,
				GetService()->m_bt_master,
				GetService()->m_secmode)) {
		GetDi()->LogWarn("Error setting RFCOMM link mode options");
		goto failure;
	}

	m_rfcomm_secmode = GetService()->m_secmode;

	if (!SetNonBlock(rsock, true)) {
		GetDi()->LogWarn("Set socket nonblocking: %d\n", errno);
		goto failure;
	}

	raddr.rc_family = AF_BLUETOOTH;
	bacpy(&raddr.rc_bdaddr, &(GetDevice()->GetAddr()));
	raddr.rc_channel = channel;

	if (connect(rsock, (struct sockaddr*)&raddr, sizeof(raddr)) < 0) {
		if ((errno != EINPROGRESS) && (errno != EAGAIN)) {
			GetDi()->LogWarn("Connect RFCOMM socket: %d\n", errno);
			goto failure;
		}

		/* We wait for the socket to become _writable_ */
		m_rfcomm_not = GetDi()->NewSocket(rsock, true);
		if (!m_rfcomm_not) {
			GetDi()->LogWarn("Could not create RFCOMM listening"
					 "socket notifier\n");
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
	return false;
}

bool RfcommSession::
RfcommAccept(int sock)
{
	if (m_rfcomm_state != RFC_Disconnected) {
		char bda[32];

		ba2str(&GetDevice()->GetAddr(), bda);
		GetDi()->LogWarn("Refusing connection from "
				 "non-disconnected device %s\n", bda);
		return false;
	}

	m_rfcomm_sock = sock;
	m_rfcomm_inbound = true;

	(void) SetNonBlock(m_rfcomm_sock, false);

	m_rfcomm_state = RFC_Connected;
	Get();
	NotifyConnectionState(true);
	return true;
}

void RfcommSession::
RfcommConnectNotify(SocketNotifier *notp, int fh)
{
	assert(m_rfcomm_state == RFC_Connecting);

	if (notp) {
		assert(notp == m_rfcomm_not);
		delete m_rfcomm_not;
		m_rfcomm_not = 0;
	}
	assert(!m_rfcomm_not);

	(void) SetNonBlock(m_rfcomm_sock, false);

	m_rfcomm_state = RFC_Connected;
	NotifyConnectionState(true);
}

bool RfcommSession::
RfcommConnect(void)
{
	if (m_rfcomm_state != RFC_Disconnected)
		return false;

	m_rfcomm_state = RFC_SdpLookupChannel;
	return RfcommSdpLookupChannel();
}

void RfcommSession::
SdpSupportedFeatures(uint16_t features)
{
	/* We don't care about this, but subclasses might */
}

void RfcommSession::
__Disconnect(bool notify, bool voluntary)
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

		NotifyConnectionState(notify);

		Put();
	}
}

} /* namespace libhfp */
