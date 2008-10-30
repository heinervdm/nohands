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
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include <libhfp/bt.h>

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


/*
 * Communicating with SDP servers requires a lot more parsing logic
 * than using the HCI command/event interfaces.  The libbluetooth SDP
 * functions do all of this.  Presently, as of time of writing,
 * commonly available libbluetooth does not have asynchronous SDP
 * interfaces.  Rather than reimplementing them, we use a helper thread.
 *
 * Eventually this will be replaced with an asynchronous alternative.
 */
int SdpAsyncTaskHandler::
SdpLookupChannel(SdpTaskParams &htp)
{
	sdp_session_t *sdp;
	sdp_list_t *srch = NULL, *attrs = NULL, *rsp = NULL;
	uuid_t svclass;
	uint16_t attr, attr2;
	int res, chan, brsf;
	int status = ENOENT;

	sdp = sdp_connect(BDADDR_ANY, &htp.m_bdaddr, SDP_RETRY_IF_BUSY);
	if (sdp == NULL) {
		return errno;
	}

	sdp_uuid16_create(&svclass, htp.m_svclass_id);
	srch = sdp_list_append(NULL, &svclass);
	attr = SDP_ATTR_PROTO_DESC_LIST;
	attrs = sdp_list_append(NULL, &attr);
	attr2 = SDP_ATTR_SUPPORTED_FEATURES;
	attrs = sdp_list_append(attrs, &attr2);

	res = sdp_service_search_attr_req(sdp, srch,
					  SDP_ATTR_REQ_INDIVIDUAL,
					  attrs, &rsp);

	if (res != 0) {
		status = errno;
		goto done;
	}

	for (; rsp && status; rsp = rsp->next) {
		sdp_record_t *rec = (sdp_record_t *) rsp->data;
		sdp_list_t *protos = 0;

		if (!sdp_get_access_protos(rec, &protos) &&
		    ((chan = sdp_get_proto_port(protos, RFCOMM_UUID)) != 0)) {

			htp.m_supported_features_present = false;

			/* See if there's a capabilities record */
			if (!sdp_get_int_attr(rec, SDP_ATTR_SUPPORTED_FEATURES,
					      &brsf)) {
				htp.m_supported_features_present = true;
				htp.m_supported_features = brsf;
			}

			htp.m_channel = chan;
			status = 0;
		}

		sdp_list_free(protos, 0);
	}

done:
	sdp_list_free(srch, 0);
	sdp_list_free(attrs, 0);
	if (rsp) { sdp_list_free(rsp, 0); }
	sdp_close(sdp);
	return status;
}


void SdpAsyncTaskHandler::
SdpTaskThread(int rqfd, int rsfd)
{
	SdpTaskParams itask;
	ssize_t res;

	/*
	 * Breaking the main process in GDB seems to result in
	 * SIGINT sent to children.  This is fatal unless we
	 * block the signal.
	 */
	signal(SIGINT, SIG_IGN);

	while (1) {
		res = read(rqfd, &itask, sizeof(itask));
		if (res < 0) {
			if ((errno == EINTR) ||
			    (errno == ENOMEM) ||
			    (errno == ENOBUFS))
				continue;
			break;
		}
			
		if (res != sizeof(itask))
			break;

		switch (itask.m_tasktype) {
		case SdpTaskParams::ST_SDP_LOOKUP:
			itask.m_errno = SdpLookupChannel(itask);
			break;
		default:
			assert(0);
		}

		itask.m_complete = true;
		if (write(rsfd, &itask, sizeof(itask)) != sizeof(itask))
			break;
	}
}

void SdpAsyncTaskHandler::
SdpDataReadyNot(SocketNotifier *notp, int fh)
{
	SdpTask *taskp;
	SdpTaskParams itask, *paramsp;
	ssize_t res;

	assert(fh == m_rspipe);
	assert(notp == m_rspipe_not);

	if (!m_current_aborted) {
		taskp = GetContainer(m_tasks.next, SdpTask, m_sdpt_links);
		paramsp = &taskp->m_params;
	} else {
		taskp = 0;
		paramsp = &itask;
	}

	res = read(m_rspipe, paramsp, sizeof(*paramsp));
	if (res != sizeof(*paramsp)) {
		m_ei->LogWarn("SDP thread terminated unexpectedly (%zd)\n",
			      res);
		m_hub->InvoluntaryStop();
		return;
	}

	if (paramsp->m_complete) {
		if (taskp) {
			assert(!m_current_aborted);
			taskp->m_sdpt_links.Unlink();
		} else {
			assert(m_current_aborted);
			m_current_aborted = false;
		}

		if (!m_tasks.Empty())
			SdpNextQueue();
	}

	if (taskp)
		taskp->cb_Result(taskp);
}

void SdpAsyncTaskHandler::
SdpNextQueue(void)
{
	SdpTask *taskp;
	sighandler_t sigsave;
	ssize_t res;

	assert(m_rqpipe >= 0);
	assert(!m_current_aborted);
	assert(!m_tasks.Empty());

	taskp = GetContainer(m_tasks.next, SdpTask, m_sdpt_links);

	/*
	 * We never put more than sizeof(SdpTaskParams) through the pipe
	 * before reading a reply, so it should never block.
	 */
	sigsave = signal(SIGPIPE, SIG_IGN);
	res = write(m_rqpipe, &taskp->m_params, sizeof(taskp->m_params));
	(void) signal(SIGPIPE, sigsave);

	if (res != sizeof(taskp->m_params)) {
		/* TODO: async involuntary stop */
		m_hub->InvoluntaryStop();
	}
}

int SdpAsyncTaskHandler::
SdpQueue(SdpTask *taskp)
{
	bool was_idle;

	assert(taskp->m_sdpt_links.Empty());

	if (m_rqpipe < 0)
		return -ESHUTDOWN;

	was_idle = (m_tasks.Empty() && !m_current_aborted);
	m_tasks.AppendItem(taskp->m_sdpt_links);
	if (was_idle)
		SdpNextQueue();
	return 0;
}

void SdpAsyncTaskHandler::
SdpCancel(SdpTask *taskp)
{
	assert(!taskp->m_sdpt_links.Empty());
	if (taskp == GetContainer(m_tasks.next, SdpTask, m_sdpt_links))
		m_current_aborted = true;
	taskp->m_sdpt_links.Unlink();
}


int SdpAsyncTaskHandler::
SdpCreateThread(void)
{
	int rqpipe[2], rspipe[2];
	pid_t cpid;
	int fd;

	assert(m_rqpipe == -1);
	assert(m_rspipe == -1);
	assert(m_pid == -1);
	assert(m_tasks.Empty());
	assert(!m_current_aborted);

	if (pipe(rqpipe) < 0) {
		return -errno;
	}

	if (pipe(rspipe) < 0) {
		fd = -errno;
		close(rqpipe[0]); close(rqpipe[1]);
		return fd;
	}

	cpid = fork();
	if (cpid < 0) {
		fd = -errno;
		close(rqpipe[0]); close(rqpipe[1]);
		close(rspipe[0]); close(rspipe[1]);
		return fd;
	}

	if (cpid > 0) {
		/* Close the unused pipe ends, save them, return */
		close(rqpipe[0]); close(rspipe[1]);
		m_rqpipe = rqpipe[1];
		m_rspipe = rspipe[0];
		m_pid = cpid;

		m_rspipe_not = m_ei->NewSocket(m_rspipe, false);
		if (m_rspipe_not == 0) {
			close(m_rspipe);
			m_rspipe = -1;
			SdpShutdown();
			return -ENOMEM;
		}

		m_rspipe_not->Register(this, &SdpAsyncTaskHandler::
				       SdpDataReadyNot);
		return 0;
	}

	/*
	 * SDP thread:
	 * Close all file handles except pipes
	 * Run main loop
	 */
	for (fd = 3; fd < 255; fd++) {
		if ((fd != rqpipe[0]) && (fd != rspipe[1]))
			close(fd);
	}

	SdpTaskThread(rqpipe[0], rspipe[1]);
	exit(0);
	return -EIO;
}

void SdpAsyncTaskHandler::
SdpShutdown(void)
{
	if (m_rqpipe >= 0) {
		close(m_rqpipe);
		m_rqpipe = -1;
	}
	if (m_rspipe >= 0) {
		assert(m_rspipe_not != 0);
		delete m_rspipe_not;
		m_rspipe_not = 0;
		close(m_rspipe);
		m_rspipe = -1;
	}
	if (m_pid >= 0) {
		int err, status;
		err = kill(m_pid, SIGKILL);
		if (err)
			m_ei->LogWarn("Send sig to SDP helper process: %s\n",
				      strerror(errno));
		err = waitpid(m_pid, &status, 0);
		if (err < 0)
			m_ei->LogWarn("Reap SDP helper process: %s\n",
				      strerror(errno));
		m_pid = -1;
	}

	m_current_aborted = false;

	/* Notify failure of all pending tasks */
	while (!m_tasks.Empty()) {
		SdpTask *taskp;
		taskp = GetContainer(m_tasks.next, SdpTask, m_sdpt_links);
		taskp->m_params.m_complete = true;
		taskp->m_params.m_errno = ECONNRESET;
		taskp->m_sdpt_links.Unlink();
		taskp->cb_Result(taskp);
	}
}


void HciAsyncTaskHandler::
HciDataReadyNot(SocketNotifier *notp, int fh)
{
	char evbuf[HCI_MAX_EVENT_SIZE + 1];
	hci_event_hdr *hdr;
	ListItem tasks_done, *listp;
	HciTask *taskp;
	inquiry_info *infop = 0;
	inquiry_info_with_rssi *rssip = 0;
	uint8_t count = 0;
	ssize_t ret;
	bool inq_result_rssi = false;

	assert(fh == m_hci_fh);
	assert(notp == m_hci_not);

	ret = read(m_hci_fh, evbuf, sizeof(evbuf));
	if (ret < 0) {
		if ((errno == EAGAIN) ||
		    (errno == EINTR) ||
		    (errno == ENOMEM) ||
		    (errno == ENOBUFS))
			return;

		m_ei->LogWarn("HCI socket read error: %s\n", strerror(errno));
		m_hub->InvoluntaryStop();
		return;
	}

	if (!ret) {
		m_ei->LogWarn("HCI socket spontaneously closed\n");
		m_hub->InvoluntaryStop();
		return;
	}

	if (evbuf[0] != HCI_EVENT_PKT)
		return;

	if (ret < (1 + HCI_EVENT_HDR_SIZE)) {
		m_ei->LogError("HCI short read, expect: %d got: %zd\n",
			       HCI_EVENT_HDR_SIZE + 1, ret);
		m_hub->InvoluntaryStop();
		return;
	}

	hdr = (hci_event_hdr *) &evbuf[1];
	if (ret < (1 + HCI_EVENT_HDR_SIZE + hdr->plen)) {
		m_ei->LogError("HCI short read, expect: %d got: %zd\n",
			       HCI_EVENT_HDR_SIZE + hdr->plen + 1, ret);
		m_hub->InvoluntaryStop();
		return;
	}

	switch (hdr->evt) {
	case EVT_CMD_STATUS: {
		evt_cmd_status *statusp = (evt_cmd_status *) (hdr + 1);
		ret = sizeof(*statusp);
		if (hdr->plen != ret)
			goto invalid_struct;

		m_ei->LogDebug("HCI Command status: 0x%02x 0x%02x 0x%04x\n",
			       statusp->status, statusp->ncmd,
			       statusp->opcode);

		/*
		 * Unfortunately the command status event isn't specific
		 * as to which command failed, it only lists an opcode.
		 * We may have many commands pending with the same opcode.
		 * So, we blast them all.
		 */
		listp = m_hci_tasks.next;
		while (listp != &m_hci_tasks) {
			taskp = GetContainer(listp, HciTask, m_hcit_links);
			listp = listp->next;

			if (taskp->m_opcode != statusp->opcode)
				continue;

			switch (statusp->status) {
			case 0:
				if (taskp->m_tasktype == HciTask::HT_INQUIRY)
					taskp->m_submitted = true;
				continue;

			case HCI_COMMAND_DISALLOWED:
				if (!taskp->m_submitted) {
					/* Mark for resubmit */
					taskp->m_resubmit = true;
					m_resubmit_needed = true;
				}
				continue;

			case HCI_REPEATED_ATTEMPTS:
				/* GOOD!! */
				continue;

			default:
				break;
			}

			/* End the task and return the error */
			taskp->m_complete = true;
			taskp->m_errno = EIO;
			taskp->m_hci_status = statusp->status;
			taskp->m_hcit_links.UnlinkOnly();
			tasks_done.AppendItem(taskp->m_hcit_links);
		}

		if (statusp->ncmd && m_resubmit_needed && !m_resubmit_set) {
			m_resubmit_set = true;
			m_resubmit->Set(1000);
		}

		break;
	}
	case EVT_INQUIRY_RESULT: {
		uint8_t *countp;
		countp = (uint8_t *) (hdr + 1);
		infop = (inquiry_info *) (countp + 1);
		ret = 1;
		if (hdr->plen < ret)
			goto invalid_struct;
		count = *countp;
		ret = 1 + (count * sizeof(*infop));
		if (hdr->plen != ret)
			goto invalid_struct;

		inq_result_rssi = false;

	do_next_inq:
		if (!count)
			break;
		listp = m_hci_tasks.next;
		while (listp != &m_hci_tasks) {
			taskp = GetContainer(listp, HciTask, m_hcit_links);
			listp = listp->next;

			if (taskp->m_tasktype == HciTask::HT_INQUIRY) {
				taskp->m_complete = false;
				taskp->m_errno = EAGAIN;
				taskp->m_hci_status = 0;
				bacpy(&taskp->m_bdaddr, &infop->bdaddr);
				taskp->m_pscan = infop->pscan_mode;
				taskp->m_pscan_rep = infop->pscan_rep_mode;
				taskp->m_clkoff = infop->clock_offset;
				taskp->m_devclass =
					(infop->dev_class[2] << 16) |
					(infop->dev_class[1] << 8) |
					infop->dev_class[0];
				taskp->m_hcit_links.UnlinkOnly();
				tasks_done.AppendItem(taskp->m_hcit_links);
			}
		}
		break;
	}
	case EVT_INQUIRY_RESULT_WITH_RSSI: {
		uint8_t *countp;
		countp = (uint8_t *) (hdr + 1);
		rssip = (inquiry_info_with_rssi *) (countp + 1);
		ret = 1;
		if (hdr->plen < ret)
			goto invalid_struct;
		count = *countp;
		ret = 1 + (count * sizeof(*rssip));
		if (hdr->plen != ret)
			goto invalid_struct;

		inq_result_rssi = true;

	do_next_inq_rssi:
		if (!count)
			break;
		listp = m_hci_tasks.next;
		while (listp != &m_hci_tasks) {
			taskp = GetContainer(listp, HciTask, m_hcit_links);
			listp = listp->next;

			if (taskp->m_tasktype == HciTask::HT_INQUIRY) {
				taskp->m_complete = false;
				taskp->m_errno = EAGAIN;
				taskp->m_hci_status = 0;
				bacpy(&taskp->m_bdaddr, &rssip->bdaddr);
				taskp->m_pscan_rep = rssip->pscan_rep_mode;
				taskp->m_clkoff = rssip->clock_offset;
				taskp->m_devclass =
					(rssip->dev_class[2] << 16) |
					(rssip->dev_class[1] << 8) |
					rssip->dev_class[0];
				taskp->m_hcit_links.UnlinkOnly();
				tasks_done.AppendItem(taskp->m_hcit_links);
			}
		}
		break;
	}
	case EVT_INQUIRY_COMPLETE: {
		uint8_t st;
		ret = 1;
		if (hdr->plen != ret)
			goto invalid_struct;
		st = *(uint8_t *) (hdr + 1);

		m_ei->LogDebug("HCI Inquiry complete: 0x%02x\n", st);

		listp = m_hci_tasks.next;
		while (listp != &m_hci_tasks) {
			taskp = GetContainer(listp, HciTask, m_hcit_links);
			listp = listp->next;

			if (taskp->m_tasktype == HciTask::HT_INQUIRY) {
				taskp->m_complete = true;
				taskp->m_errno = st ? EIO : 0;
				taskp->m_hci_status = st;
				taskp->m_hcit_links.UnlinkOnly();
				tasks_done.AppendItem(taskp->m_hcit_links);
			}
		}
		break;
	}
	case EVT_REMOTE_NAME_REQ_COMPLETE: {
		int namelen;
		evt_remote_name_req_complete *namep;
		namep =	(evt_remote_name_req_complete *) (hdr + 1);
		ret = EVT_REMOTE_NAME_REQ_COMPLETE_SIZE - sizeof(namep->name);
		if (hdr->plen < ret)
			goto invalid_struct;
		namelen = hdr->plen - ret;
		assert(namelen < (int) sizeof(taskp->m_name));
		namep->name[namelen] = '\0';
		{
			char addr[32];
			ba2str(&namep->bdaddr, addr);
			m_ei->LogDebug("HCI Name request complete (%d): "
				       "\"%s\" -> \"%s\"\n",
				       namep->status, addr, namep->name);
		}

		listp = m_hci_tasks.next;
		while (listp != &m_hci_tasks) {
			taskp = GetContainer(listp, HciTask, m_hcit_links);
			listp = listp->next;

			if ((taskp->m_tasktype == HciTask::HT_READ_NAME) &&
			    !bacmp(&taskp->m_bdaddr, &namep->bdaddr)) {
				taskp->m_complete = true;
				taskp->m_errno = namep->status ? EIO : 0;
				taskp->m_hci_status = namep->status;
				strcpy(taskp->m_name, (char *) namep->name);
				taskp->m_hcit_links.UnlinkOnly();
				tasks_done.AppendItem(taskp->m_hcit_links);
			}
		}
		break;
	}
	}

	while (!tasks_done.Empty()) {
		taskp = GetContainer(tasks_done.next, HciTask, m_hcit_links);
		taskp->m_hcit_links.Unlink();
		if (!taskp->m_complete)
			m_hci_tasks.AppendItem(taskp->m_hcit_links);
		taskp->cb_Result(taskp);
	}

	if (count--) {
		if (inq_result_rssi) {
			rssip++;
			goto do_next_inq_rssi;
		}
		infop++;
		goto do_next_inq;
	}

	return;

invalid_struct:
	m_ei->LogError("HCI structure size mismatch, expect: %d got: %zd\n",
		       hdr->plen, ret);
	m_hub->InvoluntaryStop();
}


int HciAsyncTaskHandler::
HciSend(int fh, HciTask *taskp, void *data, size_t len)
{
	uint8_t *buf;
	hci_command_hdr *hdrp;
	ssize_t expect;
	ssize_t ret;

	expect = 1 + sizeof(*hdrp) + len;
	buf = (uint8_t *) malloc(expect);
	if (!buf)
		return -ENOMEM;

	buf[0] = HCI_COMMAND_PKT;
	hdrp = (hci_command_hdr *) &buf[1];
	hdrp->opcode = taskp->m_opcode;
	hdrp->plen = len;
	if (len)
		memcpy(hdrp + 1, data, len);

	m_ei->LogDebug("HCI Submit 0x%04x\n", hdrp->opcode);

	while (1) {
		ret = send(fh, buf, expect, MSG_NOSIGNAL);
		if ((ret < 0) &&
		    ((errno == EAGAIN) ||
		     (errno == EINTR) ||
		     (errno == ENOMEM) ||
		     (errno == ENOBUFS)))
			continue;

		break;
	}

	free(buf);

	if (ret < 0) {
		m_ei->LogError("HCI write failed: %s\n", strerror(errno));
		return -errno;
	}

	if (ret != expect) {
		m_ei->LogError("HCI short write: expected: %zd got: %zd\n",
			       expect, ret);
		m_hub->InvoluntaryStop();
		return -EIO;
	}

	return 0;
}

int HciAsyncTaskHandler::
HciSubmit(int fh, HciTask *taskp)
{
	switch (taskp->m_tasktype) {
	case HciTask::HT_INQUIRY: {
		inquiry_cp req;
		taskp->m_opcode =
			htobs(cmd_opcode_pack(OGF_LINK_CTL, OCF_INQUIRY));
		req.lap[0] = 0x33;
		req.lap[1] = 0x8b;
		req.lap[2] = 0x9e;
		req.length = (taskp->m_timeout_ms + 1279) / 1280;
		req.num_rsp = 0;
		return HciSend(fh, taskp, &req, sizeof(req));
	}
	case HciTask::HT_READ_NAME: {
		remote_name_req_cp req;
		taskp->m_opcode =
			htobs(cmd_opcode_pack(OGF_LINK_CTL,
					      OCF_REMOTE_NAME_REQ));
		bacpy(&req.bdaddr, &taskp->m_bdaddr);
		req.pscan_mode = taskp->m_pscan;
		req.pscan_rep_mode = taskp->m_pscan_rep;
		req.clock_offset = taskp->m_clkoff;
		return HciSend(fh, taskp, &req, sizeof(req));
	}
	default:
		abort();
	}
}

void HciAsyncTaskHandler::
HciResubmit(TimerNotifier *notp)
{
	ListItem *listp;
	HciTask *taskp;

	assert(notp == m_resubmit);
	m_resubmit_needed = false;
	m_resubmit_set = false;

	listp = m_hci_tasks.next;
	while (listp != &m_hci_tasks) {
		taskp = GetContainer(listp, HciTask, m_hcit_links);
		listp = listp->next;

		if (!taskp->m_resubmit)
			continue;

		taskp->m_resubmit = false;
		(void) HciSubmit(m_hci_fh, taskp);
	}
}

int HciAsyncTaskHandler::
HciQueue(HciTask *taskp)
{
	int res;

	assert(taskp->m_hcit_links.Empty());

	if (m_hci_fh < 0)
		return -ESHUTDOWN;

	res = HciSubmit(m_hci_fh, taskp);
	if (res)
		return res;

	m_hci_tasks.AppendItem(taskp->m_hcit_links);
	return 0;
}

void HciAsyncTaskHandler::
HciCancel(HciTask *taskp)
{
	assert(!taskp->m_hcit_links.Empty());
	taskp->m_hcit_links.Unlink();

	if ((m_hci_fh >= 0) &&
	    (taskp->m_tasktype == HciTask::HT_INQUIRY)) {
		/* Send an INQUIRY CANCEL command to the HCI */
		taskp->m_opcode = htobs(cmd_opcode_pack(OGF_LINK_CTL,
							OCF_INQUIRY_CANCEL));
		(void) HciSend(m_hci_fh, taskp, 0, 0);
	}
}

int HciAsyncTaskHandler::
HciGetScoMtu(uint16_t &mtu, uint16_t &pkts)
{
	hci_dev_info di;

	if (m_hci_fh < 0)
		return -ESHUTDOWN;

	di.dev_id = m_hci_id;
	if (ioctl(m_hci_fh, HCIGETDEVINFO, (void *) &di) < 0)
		return -errno;

	mtu = di.sco_mtu;
	pkts = di.sco_pkts;
	return 0;
}

/* This only works as superuser */
int HciAsyncTaskHandler::
HciSetScoMtu(uint16_t mtu, uint16_t pkts)
{
	hci_dev_req dr;

	if (m_hci_fh < 0)
		return -ESHUTDOWN;

	dr.dev_id = m_hci_id;
	((uint16_t *) &dr.dev_opt)[0] = htobs(mtu);
	((uint16_t *) &dr.dev_opt)[1] = htobs(pkts);
	if (ioctl(m_hci_fh, HCISETSCOMTU, (void *) &dr) < 0)
		return -errno;

	return 0;
}

int HciAsyncTaskHandler::
HciInit(void)
{
	struct hci_filter flt;
	int did, fh, err;

	assert(m_hci_fh == -1);
	assert(!m_hci_not);
	assert(m_hci_tasks.Empty());

	did = hci_get_route(0);
	if (did < 0)
		return -ENODEV;
	fh = hci_open_dev(did);
	if (fh < 0) {
		err = -errno;
		m_ei->LogWarn("Could not open HCI: %s\n", strerror(errno));
		return err;
	}

	hci_filter_clear(&flt);
	hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
	hci_filter_set_event(EVT_CMD_STATUS, &flt);
	hci_filter_set_event(EVT_INQUIRY_RESULT, &flt);
	hci_filter_set_event(EVT_INQUIRY_RESULT_WITH_RSSI, &flt);
	hci_filter_set_event(EVT_INQUIRY_COMPLETE, &flt);
	hci_filter_set_event(EVT_REMOTE_NAME_REQ_COMPLETE, &flt);

	if (setsockopt(fh, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
		err = -errno;
		m_ei->LogWarn("Could not set filter on HCI: %s\n",
			      strerror(errno));
		close(fh);
		return err;
	}

	m_hci_not = m_ei->NewSocket(fh, false);
	if (!m_hci_not) {
		close(fh);
		return -ENOMEM;
	}

	m_resubmit = m_ei->NewTimer();
	if (!m_resubmit) {
		close(fh);
		delete m_hci_not;
		m_hci_not = 0;
		return -ENOMEM;
	}

	m_resubmit->Register(this, &HciAsyncTaskHandler::HciResubmit);
	m_hci_not->Register(this, &HciAsyncTaskHandler::HciDataReadyNot);

	m_hci_id = did;
	m_hci_fh = fh;
	return 0;
}

void HciAsyncTaskHandler::
HciShutdown(void)
{
	if (m_resubmit) {
		delete m_resubmit;
		m_resubmit = 0;
	}

	if (m_hci_not) {
		delete m_hci_not;
		m_hci_not = 0;
	}

	if (m_hci_fh >= 0) {
		close(m_hci_fh);
		m_hci_fh = -1;
	}

	/* Notify failure of all pending tasks */
	while (!m_hci_tasks.Empty()) {
		HciTask *taskp;
		taskp = GetContainer(m_hci_tasks.next, HciTask, m_hcit_links);
		taskp->m_complete = true;
		taskp->m_errno = ECONNRESET;
		taskp->m_hcit_links.Unlink();
		taskp->cb_Result(taskp);
	}
}


BtManaged::
~BtManaged()
{
	assert(m_del_links.Empty());
}

void BtManaged::
DeadRemove(void)
{
	assert(!m_del_links.Empty());
	m_del_links.Unlink();
}

void BtManaged::
Put(void)
{
	assert(m_refs);
	if (!--m_refs) {
		m_hub->DeadObject(this);
	}
}


BtHub::
BtHub(DispatchInterface *eip)
	: m_sdp(NULL), m_ei(eip), m_inquiry_task(0),
	  m_sdp_handler(this, eip), m_hci_handler(this, eip),
	  m_sdp_not(0), m_timer(0),
	  m_autorestart(false), m_autorestart_timeout(5000),
	  m_autorestart_set(false), m_cleanup_set(false)
{
	m_timer = eip->NewTimer();
	m_timer->Register(this, &BtHub::Timeout);
}

BtHub::
~BtHub()
{
	Stop();
	delete m_timer;
}

bool BtHub::
AddService(BtService *svcp)
{
	ListItem unstarted;

	assert(svcp->m_links.Empty());
	assert(!svcp->m_hub);
	svcp->m_hub = this;

	if (IsStarted()) {
		unstarted.AppendItem(svcp->m_links);
		if (!svcp->Start()) {
			if (!svcp->m_links.Empty())
				svcp->m_links.Unlink();
			svcp->m_hub = 0;
			return false;
		}

		svcp->m_links.UnlinkOnly();
	}

	m_services.AppendItem(svcp->m_links);
	return true;
}

void BtHub::
RemoveService(BtService *svcp)
{
	assert(svcp->m_hub == this);
	assert(!svcp->m_links.Empty());

	svcp->m_links.Unlink();
	if (IsStarted())
		svcp->Stop();
	svcp->m_hub = 0;
}

bool BtHub::
Start(void)
{
	ListItem unstarted;
	BtService *svcp;
	int res;

	if (IsStarted())
		return true;

	/*
	 * Simple test for Bluetooth protocol support
	 */
	if ((hci_get_route(NULL) < 0) && (errno == EAFNOSUPPORT)) {
		m_ei->LogError("Your kernel is not configured with support "
			       "for Bluetooth.\n");
		SetAutoRestart(false);
		return false;
	}

	m_sdp = sdp_connect(BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY);
	if (m_sdp == NULL) {
		/* Common enough to put in the debug pile */
		m_ei->LogDebug("Error connecting to local SDP\n");
		return false;
	}

	res = m_sdp_handler.SdpCreateThread();
	if (res) {
		m_ei->LogWarn("Could not create SDP task thread: %s\n",
			      strerror(-res));
		goto failed;
	}

	res = m_hci_handler.HciInit();
	if (res) {
		m_ei->LogWarn("Could not create HCI task handler: %s\n",
			      strerror(-res));
		goto failed;
	}

	m_sdp_not = m_ei->NewSocket(sdp_get_socket(m_sdp), false);
	if (m_sdp_not == 0)
		goto failed;
	m_sdp_not->Register(this, &BtHub::SdpConnectionLost);

	/* Start registered services */
	if (!m_services.Empty())
		unstarted.AppendItemsFrom(m_services);

	while (!unstarted.Empty()) {
		svcp = GetContainer(unstarted.next, BtService, m_links);
		svcp->m_links.UnlinkOnly();
		m_services.AppendItem(svcp->m_links);

		if (!svcp->Start()) {

			/* Ugh */
			ListItem started;
			if (!svcp->m_links.Empty()) {
				svcp->m_links.UnlinkOnly();
				unstarted.AppendItem(svcp->m_links);
			}
			if (!m_services.Empty())
				started.AppendItemsFrom(m_services);
			while (!started.Empty()) {
				svcp = GetContainer(started.next,
						    BtService,
						    m_links);
				svcp->m_links.UnlinkOnly();
				m_services.AppendItem(svcp->m_links);
				svcp->Stop();
			}

			if (!unstarted.Empty())
				m_services.AppendItemsFrom(unstarted);

			goto failed;
		}
	}

	return true;

failed:
	if (m_sdp_not) {
		delete m_sdp_not;
		m_sdp_not = 0;
	}
	m_sdp_handler.SdpShutdown();
	m_hci_handler.HciShutdown();
	sdp_close(m_sdp);
	m_sdp = NULL;
	return false;
}


void BtHub::
__Stop(void)
{
	ListItem started;
	BtService *svcp;

	if (m_sdp) {
		assert(m_sdp_not != 0);
		delete m_sdp_not;
		m_sdp_not = 0;
		sdp_close(m_sdp);
		m_sdp = NULL;
	}

	if (!m_services.Empty())
		started.AppendItemsFrom(m_services);

	while (!started.Empty()) {
		svcp = GetContainer(started.next, BtService, m_links);
		svcp->m_links.UnlinkOnly();
		m_services.AppendItem(svcp->m_links);
		svcp->Stop();
	}

	m_sdp_handler.SdpShutdown();
	m_hci_handler.HciShutdown();

	assert(m_inquiry_task == 0);
}

void BtHub::
Stop(void)
{
	__Stop();

	if (m_autorestart) {
		if (m_autorestart_set) {
			m_autorestart_set = false;
			m_timer->Cancel();
		}
		m_autorestart = false;
	}
}

void BtHub::
SetAutoRestart(bool autorestart)
{
	if (m_autorestart == autorestart)
		return;

	m_autorestart = autorestart;
	if (autorestart && !m_cleanup_set && !IsStarted()) {
		/*
		 * Initial autorestart attempts are zero-wait
		 */
		m_autorestart_set = true;
		m_timer->Set(0);
	}
}

void BtHub::
SdpConnectionLost(SocketNotifier *notp, int fh)
{
	assert(fh == sdp_get_socket(m_sdp));
	assert(notp == m_sdp_not);

	/* Lost our SDP connection?  Bad business */
	m_ei->LogWarn("Lost local SDP connection\n");
	InvoluntaryStop();
}

void BtHub::
InvoluntaryStop(void)
{
	ListItem *listp;
	bool was_started = IsStarted();

	__Stop();

	if (m_autorestart && !m_cleanup_set) {
		if (m_autorestart_set) {
			m_autorestart_set = false;
			m_timer->Cancel();
		}
		m_autorestart_set = true;
		m_timer->Set(m_autorestart_timeout);
	}
	ListForEach(listp, &m_devices) {
		BtDevice *devp = GetContainer(listp, BtDevice, m_index_links);
		devp->__DisconnectAll(true);
		if (IsStarted())
			return;
	}
	if (was_started && cb_NotifySystemState.Registered())
		cb_NotifySystemState();
}

void BtHub::
ClearInquiryFlags(void)
{
	BtDevice *devp;
	for (devp = GetFirstDevice(); devp; devp = GetNextDevice(devp)) {
		if (devp->m_inquiry_found) {
			devp->m_inquiry_found = false;
			devp->Put();
		}
	}
}

void BtHub::
HciInquiryResult(HciTask *taskp)
{
	BtDevice *devp;

	assert(taskp == m_inquiry_task);

	if (taskp->m_complete) {
		if (taskp->m_errno) {
			if (taskp->m_errno == EIO)
				m_ei->LogWarn("Inquiry failed: %s (%d)\n",
					      strerror(taskp->m_errno),
					      taskp->m_hci_status);
			else
				m_ei->LogWarn("Inquiry failed: %s\n",
					      strerror(taskp->m_errno));
		}
		delete taskp;
		m_inquiry_task = 0;

		ClearInquiryFlags();

		cb_InquiryResult(0, taskp->m_errno);
		return;
	}

	if (taskp->m_errno == EAGAIN) {
		/* Look up the device, maybe create it */
		devp = GetDevice(taskp->m_bdaddr, true);
		if (!devp)
			return;
		if (devp->m_inquiry_found) {
			devp->Put();
			return;
		}

		devp->m_inquiry_found = true;
		devp->m_inquiry_pscan = taskp->m_pscan;
		devp->m_inquiry_pscan_rep = taskp->m_pscan_rep;
		devp->m_inquiry_clkoff = taskp->m_clkoff;
		devp->m_inquiry_class = taskp->m_devclass;
		cb_InquiryResult(devp, 0);
		return;
	}

	assert(!taskp->m_errno);
}

int BtHub::
StartInquiry(int timeout_ms)
{
	HciTask *taskp;
	int res;

	if (!IsStarted())
		return -ESHUTDOWN;

	if (m_inquiry_task)
		return -EALREADY;

	/*
	 * If the client is going to request a scan, they had
	 * better have registered a result callback!
	 */
	assert(cb_InquiryResult.Registered());

	taskp = new HciTask;
	if (taskp == 0)
		return -ENOMEM;

	taskp->m_tasktype = HciTask::HT_INQUIRY;
	taskp->m_timeout_ms = timeout_ms;
	taskp->cb_Result.Register(this, &BtHub::HciInquiryResult);

	res = m_hci_handler.HciQueue(taskp);
	if (res) {
		delete taskp;
		return res;
	}

	m_inquiry_task = taskp;
	return 0;
}

int BtHub::
StopInquiry(void)
{
	HciTask *taskp;

	if (!IsStarted())
		return -ESHUTDOWN;

	if (!m_inquiry_task)
		return -EALREADY;

	taskp = m_inquiry_task;
	m_inquiry_task = 0;

	m_hci_handler.HciCancel(taskp);
	delete taskp;
	ClearInquiryFlags();

	return 0;
}

int BtHub::
HciTaskSubmit(HciTask *taskp)
{
	assert(taskp->cb_Result.Registered());

	if (!IsStarted())
		return -ESHUTDOWN;

	return m_hci_handler.HciQueue(taskp);
}

void BtHub::
HciTaskCancel(HciTask *taskp)
{
	return m_hci_handler.HciCancel(taskp);
}


int BtHub::
SdpTaskSubmit(SdpTask *taskp)
{
	assert(taskp->cb_Result.Registered());

	if (!IsStarted())
		return -ESHUTDOWN;

	return m_sdp_handler.SdpQueue(taskp);
}

void BtHub::
SdpTaskCancel(SdpTask *taskp)
{
	return m_sdp_handler.SdpCancel(taskp);
}

/*
 * FIXME: These sdp_record functions can block the caller forever
 * waiting on the local SDP daemon
 */
bool BtHub::
SdpRecordRegister(sdp_record_t *recp)
{
	if (!m_sdp)
		return false;

	return (sdp_record_register(m_sdp, recp, 0) >= 0);
}

void BtHub::
SdpRecordUnregister(sdp_record_t *recp)
{
	if (!m_sdp || sdp_record_unregister(m_sdp, recp) < 0)
		GetDi()->LogDebug("Failed to unregister SDP record\n");
}

BtDevice *BtHub::
FindClientDev(bdaddr_t const &bdaddr)
{
	ListItem *listp;
	BtDevice *devp;
	ListForEach(listp, &m_devices) {
		devp = GetContainer(listp, BtDevice, m_index_links);
		if (!bacmp(&bdaddr, &devp->m_bdaddr)) {
			devp->Get();
			return devp;
		}
	}
	return 0;
}

BtDevice *BtHub::
CreateClientDev(bdaddr_t const &bdaddr)
{
	BtDevice *devp;
	char buf[32];

	ba2str(&bdaddr, buf);
	m_ei->LogDebug("Creating record for BDADDR %s\n", buf);

	/* Call the factory */
	if (cb_BtDeviceFactory.Registered())
		devp = cb_BtDeviceFactory(bdaddr);
	else
		devp = DefaultDevFactory(bdaddr);

	if (devp != NULL) {
		devp->m_hub = this;
		m_devices.AppendItem(devp->m_index_links);
	}
	return devp;
}

BtDevice *BtHub::
DefaultDevFactory(bdaddr_t const &bdaddr)
{
	return new BtDevice(this, bdaddr);
}


void BtHub::
DeadObject(BtManaged *objp)
{
	assert(!objp->m_refs);
	assert(objp->m_del_links.Empty());
	m_dead_objs.AppendItem(objp->m_del_links);

	if (!m_cleanup_set) {
		/* Schedule immediate deletion */
		if (m_autorestart_set) {
			m_autorestart_set = false;
			m_timer->Cancel();
		}
		m_cleanup_set = true;
		m_timer->Set(0);
	}
}


BtDevice *BtHub::
GetDevice(bdaddr_t const &raddr, bool create)
{
	BtDevice *devp;
	devp = FindClientDev(raddr);
	if (!devp && create) {
		devp = CreateClientDev(raddr);
	}
	return devp;
}

BtDevice *BtHub::
GetDevice(const char *raddr, bool create)
{
	bdaddr_t bdaddr;
	str2ba(raddr, &bdaddr);
	return GetDevice(bdaddr, create);
}

BtDevice *BtHub::
GetFirstDevice(void)
{
	if (m_devices.Empty())
		return 0;
	return GetContainer(m_devices.next, BtDevice, m_index_links);
}

BtDevice *BtHub::
GetNextDevice(BtDevice *devp)
{
	if (devp->m_index_links.next == &m_devices)
		return 0;
	return GetContainer(devp->m_index_links.next, BtDevice, m_index_links);
}

void BtHub::
Timeout(TimerNotifier *notp)
{
	assert(notp == m_timer);

	m_autorestart_set = false;
	if (!IsStarted() && !m_cleanup_set && m_autorestart) {
		/* Do or do not.  There is no try. */
		Start();
		if (IsStarted() && cb_NotifySystemState.Registered())
			cb_NotifySystemState();
	}

	while (!m_dead_objs.Empty()) {
		BtManaged *objp;
		objp = GetContainer(m_dead_objs.next, BtManaged, m_del_links);
		assert(!objp->m_refs);
		objp->m_del_links.Unlink();
		if (objp->cb_NotifyDestroy.Registered())
			objp->cb_NotifyDestroy(objp);
		delete objp;
	}

	m_cleanup_set = false;

	if (!IsStarted() && m_autorestart) {
		m_autorestart_set = true;
		m_timer->Set(m_autorestart_timeout);
	}
}


bool BtHub::
GetDeviceClassLocal(uint32_t &devclass)
{
	int dh, did, res;
	uint8_t cls[3];

	/*
	 * This is a synchronous operation, and a broken HCI may be able
	 * to hang our main thread for the 1s timeout.
	 */

	did = hci_get_route(NULL);
	if (did < 0)
		return false;
	dh = hci_open_dev(did);
	if (dh < 0)
		return false;

	res = hci_read_class_of_dev(dh, cls, 1000);
	hci_close_dev(dh);

	if (res < 0)
		return false;

	devclass = (cls[2] << 16) | (cls[1] << 8) | cls[0];
	return true;
}

bool BtHub::
SetDeviceClassLocal(uint32_t devclass)
{
	int dh, did, res;

	did = hci_get_route(NULL);
	if (did < 0)
		return false;
	dh = hci_open_dev(did);
	if (dh < 0)
		return false;

	res = hci_write_class_of_dev(dh, devclass, 1000);
	hci_close_dev(dh);

	return (res < 0);
}


BtDevice::
BtDevice(BtHub *hubp, bdaddr_t const &bdaddr)
	: BtManaged(hubp), m_bdaddr(bdaddr), m_inquiry_found(false),
	  m_name_resolved(false), m_name_task(0)
{
	/* Scribble something down for the name */
	ba2str(&bdaddr, m_dev_name);
}

BtDevice::
~BtDevice()
{
	assert(!m_inquiry_found);
	GetDi()->LogDebug("Destroying record for %s\n", GetName());
	if (!m_index_links.Empty())
		m_index_links.Unlink();
	if (m_name_task) {
		GetHub()->HciTaskCancel(m_name_task);
		delete m_name_task;
		m_name_task = 0;
	}
}

void BtDevice::
GetAddr(char (&namebuf)[32]) const
{
	ba2str(&m_bdaddr, namebuf);
}

bool BtDevice::
ResolveName(void)
{
	HciTask *taskp;

	if (m_name_task)
		return true;

	taskp = new HciTask;
	if (taskp == 0)
		return false;

	taskp->m_tasktype = HciTask::HT_READ_NAME;
	bacpy(&taskp->m_bdaddr, &m_bdaddr);
	taskp->m_timeout_ms = 5000;
	if (m_inquiry_found) {
		taskp->m_pscan = m_inquiry_pscan;
		taskp->m_pscan_rep = m_inquiry_pscan_rep;
		taskp->m_clkoff = m_inquiry_clkoff;
	}
	taskp->cb_Result.Register(this, &BtDevice::NameResolutionResult);

	if (GetHub()->HciTaskSubmit(taskp)) {
		delete taskp;
		return false;
	}

	m_name_resolved = false;
	m_name_task = taskp;
	return true;
}

void BtDevice::
NameResolutionResult(HciTask *taskp)
{
	assert(taskp == m_name_task);

	m_name_resolved = !taskp->m_errno;

	if (m_name_resolved) {
		strncpy(m_dev_name, taskp->m_name, sizeof(m_dev_name));
		m_dev_name[sizeof(m_dev_name) - 1] = '\0';
	}

	m_name_task = 0;
	delete taskp;

	if (cb_NotifyNameResolved.Registered())
		cb_NotifyNameResolved(this, m_name_resolved
				      ? m_dev_name : NULL);
}

void BtDevice::
AddSession(BtSession *sessp)
{
	assert(sessp->m_dev_links.Empty());
	m_sessions.AppendItem(sessp->m_dev_links);
	Get();
}

void BtDevice::
RemoveSession(BtSession *sessp)
{
	assert(!sessp->m_dev_links.Empty());
	assert(sessp->m_dev == this);
	sessp->m_dev_links.Unlink();
	Put();
}

BtSession *BtDevice::
FindSession(BtService const *svcp) const
{
	ListItem *listp;
	ListForEach(listp, &m_sessions) {
		BtSession *sessp;
		sessp = GetContainer(listp, BtSession, m_dev_links);
		if (sessp->m_svc == svcp) {
			sessp->Get();
			return sessp;
		}
	}
	return 0;
}

void BtDevice::
__DisconnectAll(bool notify)
{
	ListItem sesslist;
	BtSession *sessp;

	if (!m_sessions.Empty())
		sesslist.AppendItemsFrom(m_sessions);

	while (!sesslist.Empty()) {
		sessp = GetContainer(sesslist.next, BtSession, m_dev_links);
		assert(sessp->m_dev == this);
		sessp->m_dev_links.UnlinkOnly();
		m_sessions.AppendItem(sessp->m_dev_links);
		sessp->__Disconnect(notify);
	}
}

BtService::
BtService(void)
	: m_hub(0)
{
}

BtService::
~BtService()
{
	/* We had best have been unregistered */
	assert(m_links.Empty());
}

void BtService::
AddSession(BtSession *sessp)
{
	assert(sessp->m_svc_links.Empty());
	m_sessions.AppendItem(sessp->m_svc_links);
}

void BtService::
RemoveSession(BtSession *sessp)
{
	assert(sessp->m_svc == this);
	assert(!sessp->m_svc_links.Empty());
	sessp->m_svc_links.Unlink();
}

BtSession *BtService::
FindSession(BtDevice const *devp) const
{
	return devp->FindSession(this);
}

BtSession *BtService::
GetFirstSession(void) const
{
	if (m_sessions.Empty())
		return 0;

	return GetContainer(m_sessions.next, BtSession, m_svc_links);
}

BtSession *BtService::
GetNextSession(BtSession *sessp) const
{
	if (sessp->m_svc_links.next == &m_sessions)
		return 0;

	return GetContainer(sessp->m_svc_links.next, BtSession, m_svc_links);
}


BtSession::
BtSession(BtService *svcp, BtDevice *devp)
	: BtManaged(devp->GetHub()), m_dev(devp), m_svc(svcp) {
	devp->AddSession(this);
	svcp->AddSession(this);
}

BtSession::
~BtSession()
{
	m_dev->RemoveSession(this);
	m_svc->RemoveSession(this);
}


} /* namespace libhfp */
