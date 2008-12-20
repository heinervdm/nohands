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

#include <libhfp/events.h>
#include <libhfp/hfp.h>

#include "dbus.h"

#define HFPD_HANDSFREE_DEFINE_INTERFACES
#define HFPD_SOUNDIO_DEFINE_INTERFACES
#define HFPD_AUDIOGATEWAY_DEFINE_INTERFACES
#include "objects.h"

#include "util.h"

using namespace libhfp;


class ConfigHandler : public ConfigFile {
public:
	char			*m_config_savefile;
	bool			m_config_dirty;
	bool			m_config_autosave;
	DispatchInterface	*m_di;

	DispatchInterface *GetDi(void) const { return m_di; }

	ConfigHandler(DispatchInterface *dip)
		: m_config_savefile(0), m_config_dirty(false),
		  m_config_autosave(false), m_di(dip) {}

	~ConfigHandler() {
		if (m_config_savefile) {
			free(m_config_savefile);
			m_config_savefile = 0;
		}
	}

	bool SaveConfig(ErrorInfo *error = 0, bool force = false) {
		if (!force && !m_config_autosave) {
			m_config_dirty = true;
			return true;
		}
		if (!Save(m_config_savefile, 2, error))
			return false;
		m_config_dirty = false;
		return true;
	}

	bool Init(const char *cfgfile) {
		Clear();

		if (!Load("/etc/hfpd.conf", 1) &&
		    !Load("/usr/local/etc/hfpd.conf", 1)) {
			/* No defaults */
		}

		if (m_config_savefile) {
			free(m_config_savefile);
			m_config_savefile = 0;
		}

		if (cfgfile) {
			if (!Load(cfgfile, 2) &&
			    !Create(cfgfile)) {
				GetDi()->LogWarn("Could not open or create "
					 "specified config file \"%s\"",
						 cfgfile);
				goto failed;
			}
			m_config_savefile = strdup(cfgfile);
			if (!m_config_savefile)
				goto failed;
		}

		if (!m_config_savefile) {
			m_config_savefile = strdup("~/.hfpdrc");
			if (!m_config_savefile)
				goto failed;
			(void) Load(m_config_savefile, 2);
		}

		return true;

	failed:
		return false;
	}

	bool GetAutoSave(void) const { return m_config_autosave; }
	void SetAutoSave(bool val) { m_config_autosave = val; }

	const char *GetConfigFile(void) const { return m_config_savefile; }
	bool SetConfigFile(const char *val, ErrorInfo *error) {
		char *oldval = m_config_savefile;

		if (!val || !val[0]) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_EVENTS,
					   LIBHFP_ERROR_EVENTS_BAD_PARAMETER,
					   "Configuration file name is empty");
			return false;
		}

		m_config_savefile = strdup(val);
		if (!m_config_savefile) {
			m_config_savefile = oldval;
			if (error)
				error->SetNoMem();
			return false;
		}

		if (!SaveConfig(error)) {
			if (m_config_savefile)
				free(m_config_savefile);
			m_config_savefile = oldval;
			return false;
		}

		if (oldval)
			free(oldval);
		return true;
	}
};

const char *HfpdExportObject::
DbusErrorName(libhfp::ErrorInfo &error) const
{
	const char *exname;

	exname = HFPD_ERROR_FAILED;

	switch (error.Subsys()) {
	case LIBHFP_ERROR_SUBSYS_EVENTS:
		switch (error.Code()) {
		case LIBHFP_ERROR_EVENTS_NO_MEMORY:
			exname = DBUS_ERROR_NO_MEMORY;
			break;
		case LIBHFP_ERROR_EVENTS_BAD_PARAMETER:
			exname = DBUS_ERROR_INVALID_ARGS;
			break;
		case LIBHFP_ERROR_EVENTS_IO_ERROR:
			exname = DBUS_ERROR_IO_ERROR;
			break;
		default:
			break;
		}
	case LIBHFP_ERROR_SUBSYS_BT:
		switch (error.Code()) {
		case LIBHFP_ERROR_BT_NO_SUPPORT:
			exname = HFPD_ERROR_BT_NO_KERNEL_SUPPORT;
			break;
		case LIBHFP_ERROR_BT_SERVICE_CONFLICT:
			exname = HFPD_ERROR_BT_SERVICE_CONFLICT;
			break;
		case LIBHFP_ERROR_BT_BAD_SCO_CONFIG:
			exname = HFPD_ERROR_BT_BAD_SCO_CONFIG;
			break;
		default:
			break;
		}
		break;
	case LIBHFP_ERROR_SUBSYS_SOUNDIO:
		switch (error.Code()) {
		case LIBHFP_ERROR_SOUNDIO_SOUNDCARD_FAILED:
			exname = HFPD_ERROR_SOUNDIO_SOUNDCARD_FAILED;
			break;
		default:
			break;
		}
		break;
	}
	return exname;
}

bool HfpdExportObject::
SendReplyErrorInfo(DBusMessage *msgp, libhfp::ErrorInfo &error)
{
	assert(error.IsSet());
	return SendReplyError(msgp, DbusErrorName(error), error.Desc());
}

struct AgPendingCommand {
	ListItem		m_links;
	DBusMessage		*m_msg;
	HfpPendingCommand	*m_pend;
	AudioGateway		*m_ag;

	void HfpCommandResult(HfpPendingCommand *pendp,
			      ErrorInfo *error, const char *info) {
		assert(pendp == m_pend);
		assert(m_msg);
		assert(m_ag);

		/*
		 * We could try to allocate a must succeed
		 * structure here.
		 */
		if (!error) {
			(void) m_ag->SendReplyArgs(m_msg, DBUS_TYPE_INVALID);
		} else {
			(void) m_ag->SendReplyErrorInfo(m_msg, *error);
		}

		delete this;
	}

	void Attach(HfpPendingCommand *cmdp) {
		assert(!m_pend);
		m_pend = cmdp;
		cmdp->Register(this, &AgPendingCommand::HfpCommandResult);
	}

	AgPendingCommand(AudioGateway *agp, DBusMessage *msgp)
		: m_msg(msgp), m_pend(0), m_ag(agp) {
		dbus_message_ref(m_msg);
		m_ag->Get();
	}

	~AgPendingCommand() {
		if (m_pend)
			delete m_pend;
		if (m_msg)
			dbus_message_unref(m_msg);
		if (m_ag)
			m_ag->Put();
	}
};

AudioGateway::
AudioGateway(HandsFree *hfp, HfpSession *sessp, char *name)
	: HfpdExportObject(name, s_ifaces), m_sess(sessp),
	  m_name_free(name), m_known(false), 
	  m_unbind_on_audio_close(false),
	  m_state(HFPD_AG_INVALID),
	  m_call_state(HFPD_AG_CALL_INVALID),
	  m_audio_state(HFPD_AG_AUDIO_INVALID),
	  m_hf(hfp), m_owner(0),
	  m_audio_bind(0) {

	/*
	 * Attach ourselves to the HfpSession
	 */
	assert(sessp && sessp->GetDevice());
	assert(!sessp->GetPrivate());
	sessp->SetPrivate(this);

	sessp->cb_NotifyConnection.Register(this,
					&AudioGateway::NotifyConnection);
	sessp->cb_NotifyAudioConnection.Register(this,
					&AudioGateway::NotifyAudioConnection);
	sessp->cb_NotifyCall.Register(this, &AudioGateway::NotifyCall);
	sessp->cb_NotifyIndicator.Register(this,
					&AudioGateway::NotifyIndicator);
	sessp->cb_NotifyDestroy.Register(this, &AudioGateway::NotifyDestroy);
}

AudioGateway::
~AudioGateway()
{
	assert(m_sess->GetPrivate() == this);
	m_sess->SetPrivate(0);

	/* Unexport before freeing m_path */
	if (GetDbusSession())
		GetDbusSession()->UnexportObject(this);

	if (m_name_free)
		free(m_name_free);
	m_links.Unlink();
}

void AudioGateway::
OwnerDisconnectNotify(DbusPeerDisconnectNotifier *notp)
{
	dbus_bool_t claim;
	char buf[32];

	assert(notp == m_owner);
	m_sess->GetDevice()->GetAddr(buf);
	GetDi()->LogInfo("AG %s: D-Bus owner disconnected", buf);

	claim = false;
	(void) SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
			      "ClaimStateChanged",
			      DBUS_TYPE_BOOLEAN, &claim,
			      DBUS_TYPE_INVALID);

	delete notp;
	m_owner = 0;
	if (!m_known) {
		(void) DoSetAutoReconnect(false);
	}
	if (!m_known && (State() > HFPD_AG_DISCONNECTED)) {
		if (m_sess->SndIsAsyncStarted() && m_hf->m_voice_persist) {
			/* Defer the disconnect until audio closes */
			m_unbind_on_audio_close = true;
		} else {
			DoDisconnect();
		}
	}
	m_sess->Put();
}

AudioGatewayState AudioGateway::
State(void)
{
	if (m_sess->IsConnected())
		return HFPD_AG_CONNECTED;

	if (m_sess->IsConnecting())
		return HFPD_AG_CONNECTING;

	return HFPD_AG_DISCONNECTED;
}

AudioGatewayCallState AudioGateway::
CallState(void)
{
	if (!m_sess->HasConnectingCall() &&
	    !m_sess->HasEstablishedCall() &&
	    !m_sess->HasWaitingCall())
		return HFPD_AG_CALL_IDLE;

	if (m_sess->HasConnectingCall())
		return HFPD_AG_CALL_CONNECTING;

	if (m_sess->HasEstablishedCall() &&
		 !m_sess->HasWaitingCall())
		return HFPD_AG_CALL_ESTAB;

	if (!m_sess->HasEstablishedCall() &&
	    m_sess->HasWaitingCall())
		return HFPD_AG_CALL_WAITING;

	return HFPD_AG_CALL_ESTAB_WAITING;
}

AudioGatewayAudioState AudioGateway::
AudioState(void)
{
	if (m_sess->IsConnectedAudio())
		return HFPD_AG_AUDIO_CONNECTED;

	if (m_sess->IsConnectingAudio())
		return HFPD_AG_AUDIO_CONNECTING;

	return HFPD_AG_AUDIO_DISCONNECTED;
}

bool AudioGateway::
UpdateState(AudioGatewayState st)
{
	const uint8_t state = (uint8_t) st;
	dbus_bool_t dc;

	dc = ((st == HFPD_AG_DISCONNECTED) &&
	      m_sess->IsPriorDisconnectVoluntary());

	if ((st != m_state) &&
	    !SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
			    "StateChanged",
			    DBUS_TYPE_BYTE, &state,
			    DBUS_TYPE_BOOLEAN, &dc,
			    DBUS_TYPE_INVALID))
		return false;

	m_state = st;
	return true;
}

bool AudioGateway::
UpdateCallState(AudioGatewayCallState st)
{
	const uint8_t state = (uint8_t) st;
	if ((st != m_call_state) &&
	    !SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
			    "CallStateChanged",
			    DBUS_TYPE_BYTE, &state,
			    DBUS_TYPE_INVALID))
		return false;

	m_call_state = st;
	return true;
}

bool AudioGateway::
UpdateAudioState(AudioGatewayAudioState st)
{
	const uint8_t state = (uint8_t) st;
	if ((st != m_audio_state) &&
	    !SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
			    "AudioStateChanged",
			    DBUS_TYPE_BYTE, &state,
			    DBUS_TYPE_INVALID))
		return false;

	m_audio_state = st;
	return true;
}


void AudioGateway::
DoSetKnown(bool known)
{
	if (known && !m_known) {
		m_known = known;
		m_sess->Get();
	}
	else if (!known && m_known) {
		m_known = known;
		m_sess->Put();
	}
}

bool AudioGateway::
DoSetAutoReconnect(bool value, libhfp::ErrorInfo *error)
{
	char addr[32];
	dbus_bool_t state;

	if (value == m_sess->IsAutoReconnect())
		return true;

	if (m_known) {
		m_sess->GetDevice()->GetAddr(addr);
		if (!m_hf->m_config->Set("devices", addr, value, error) ||
		    !m_hf->SaveConfig(error))
			return false;
	}

	state = value;
	(void) SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
			      "AutoReconnectChanged",
			      DBUS_TYPE_BOOLEAN, &state,
			      DBUS_TYPE_INVALID);

	m_sess->SetAutoReconnect(value);
	return true;
}

void AudioGateway::
DoDisconnect(void)
{
	m_sess->Disconnect();
	NotifyConnection(m_sess, 0);
}

void AudioGateway::
NotifyConnection(libhfp::HfpSession *sessp, ErrorInfo *reason)
{
	AudioGatewayState st;
	char buf[32];

	st = State();

	if (!m_hf->m_accept_unknown &&
	    !m_owner && !m_known &&
	    (st != HFPD_AG_DISCONNECTED)) {
		/*
		 * We have been configured to refuse connections from
		 * unknown devices.
		 * Don't even report that the device is trying to connect.
		 */
		m_sess->Disconnect();
		st = State();
	}

	if (st == HFPD_AG_DISCONNECTED) {
		/*
		 * If the user pushed the "disconnect bluetooth" button
		 * on the device, don't keep trying to reconnect.
		 * Instead, disable auto-reconnect.
		 */
		if (m_sess->IsPriorDisconnectVoluntary() &&
		    m_sess->IsAutoReconnect()) {
			(void) DoSetAutoReconnect(false);
		}

		assert(AudioState() == HFPD_AG_AUDIO_DISCONNECTED);
		m_unbind_on_audio_close = false;
		NotifyAudioConnection(m_sess, 0);

		m_sess->GetDevice()->GetAddr(buf);
		GetDi()->LogInfo("AG %s: Disconnected", buf);
	}

	else if (st == HFPD_AG_CONNECTED) {
		m_sess->GetDevice()->GetAddr(buf);
		GetDi()->LogInfo("AG %s: Connected", buf);

		/*
		 * Trigger a name lookup if one would be helpful.
		 * This is probably the best time to do it for
		 * devices-to-be-connected-to, causing the least
		 * impact on HCI task queues.
		 */
		if (!sessp->GetDevice()->IsNameResolved())
			(void) sessp->GetDevice()->ResolveName();
	}

	UpdateState(st);
}

void AudioGateway::
NotifyCall(libhfp::HfpSession *sessp, bool act, bool waiting, bool ring)
{
	if (act || waiting)
		UpdateCallState(CallState());

	if (ring) {
		const char *num = 0, *alpha = 0;
		const GsmClipPhoneNumber *clip;

		clip = sessp->WaitingCallIdentity();
		if (clip) {
			num = clip->number;
			alpha = clip->alpha;
		}
		if (!num)
			num = "";
		if (!alpha)
			alpha = "";

		SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
			       "Ring",
			       DBUS_TYPE_STRING, &num,
			       DBUS_TYPE_STRING, &alpha,
			       DBUS_TYPE_INVALID);
	}
}

void AudioGateway::
NotifyIndicator(libhfp::HfpSession *sessp, const char *indname, int val)
{
	dbus_int32_t ival = val;
	SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
		       "IndicatorChanged",
		       DBUS_TYPE_STRING, &indname,
		       DBUS_TYPE_INT32, &ival,
		       DBUS_TYPE_INVALID);
}

void AudioGateway::
NotifyAudioConnection(libhfp::HfpSession *sessp, libhfp::ErrorInfo *error)
{
	AudioGatewayAudioState st;
	st = AudioState();

	if (!m_owner && (!m_known || !m_hf->m_voice_autoconnect) &&
	    (st != HFPD_AG_AUDIO_DISCONNECTED)) {
		/*
		 * Auto-refuse audio connections from unclaimed,
		 * unknown devices.
		 * Don't even report that the device is trying to connect.
		 * If they want an audio connection, they need to claim
		 * the device or mark it known.
		 */
		m_sess->SndClose();
		st = AudioState();
	}

	UpdateAudioState(st);

	if (m_audio_bind)
		m_audio_bind->EpAudioGatewayComplete(this, 0);

	if (!m_owner && m_known && (st == HFPD_AG_AUDIO_CONNECTED)) {
		/*
		 * The device is known and unclaimed.
		 * Make an effort to set up the audio pipe.
		 */
		if ((m_hf->m_sound->m_state != HFPD_SIO_STOPPED) ||
		    !m_hf->m_sound->EpAudioGateway(this, false, 0)) {
			m_sess->SndClose();
			st = AudioState();
		}
	}

	if (m_unbind_on_audio_close &&
	    (st == HFPD_AG_AUDIO_DISCONNECTED)) {
		DoDisconnect();
		return;
	}
}

void AudioGateway::
NotifyDestroy(BtManaged *objp)
{
	const char *path;

	assert(objp == m_sess);
	UpdateState(HFPD_AG_DESTROYED);

	path = GetDbusPath();
	(void) m_hf->SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
				    "AudioGatewayRemoved",
				    DBUS_TYPE_OBJECT_PATH, &path,
				    DBUS_TYPE_INVALID);

	delete this;
}

void AudioGateway::
NameResolved(void)
{
	const char *name;
	name = m_sess->GetDevice()->GetName();
	if (!name)
		name = "";
	SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
		       "NameResolved",
		       DBUS_TYPE_STRING, &name,
		       DBUS_TYPE_INVALID);
}


bool AudioGateway::
Connect(DBusMessage *msgp)
{
	ErrorInfo error;

	if (!m_sess->Connect(&error))
		return SendReplyErrorInfo(msgp, error);

	UpdateState(State());

	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool AudioGateway::
Disconnect(DBusMessage *msgp)
{
	DoDisconnect();
	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool AudioGateway::
OpenAudio(DBusMessage *msgp)
{
	ErrorInfo error;

	GetDi()->LogDebug("AG %s: OpenAudio", GetDbusPath());

	if (!m_sess->IsConnectingAudio() &&
	    !m_sess->IsConnectedAudio() &&
	    !m_sess->SndOpen(true, true, &error))
		return SendReplyErrorInfo(msgp, error);

	UpdateAudioState(AudioState());

	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool AudioGateway::
CloseAudio(DBusMessage *msgp)
{
	GetDi()->LogDebug("AG %s: CloseAudio", GetDbusPath());

	if (m_sess->SndIsAsyncStarted())
		m_hf->m_sound->EpRelease();
	m_sess->SndClose();
	NotifyAudioConnection(m_sess, 0);

	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool AudioGateway::
CreatePendingCommand(DBusMessage *msgp, AgPendingCommand *&agpendp)
{
	agpendp = new AgPendingCommand(this, msgp);
	if (!agpendp)
		return false;

	return true;
}

bool AudioGateway::
DoPendingCommand(AgPendingCommand *agpendp, ErrorInfo &error,
		 HfpPendingCommand *cmdp)
{
	bool res;

	if (!cmdp) {
		res = SendReplyErrorInfo(agpendp->m_msg, error);
		delete agpendp;
		return res;
	}

	agpendp->Attach(cmdp);
	return true;
}


bool AudioGateway::
Dial(DBusMessage *msgp)
{
	DBusMessageIter mi;
	const char *number;
	AgPendingCommand *agpcp = 0;
	ErrorInfo error;
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &number);

	if (number && !number[0])
		number = 0;

	if (!number) {
		return SendReplyError(msgp,
				      HFPD_ERROR_FAILED,
				      "Empty phone number specified");
	}

	return (CreatePendingCommand(msgp, agpcp) &&
		DoPendingCommand(agpcp, error,
				 m_sess->CmdDial(number, &error)));
}

bool AudioGateway::
Redial(DBusMessage *msgp)
{
	AgPendingCommand *agpcp = 0;
	ErrorInfo error;

	return (CreatePendingCommand(msgp, agpcp) &&
		DoPendingCommand(agpcp, error,
				 m_sess->CmdRedial(&error)));
}

bool AudioGateway::
HangUp(DBusMessage *msgp)
{
	AgPendingCommand *agpcp = 0;
	ErrorInfo error;

	return (CreatePendingCommand(msgp, agpcp) &&
		DoPendingCommand(agpcp, error,
				 m_sess->CmdHangUp(&error)));
}

bool AudioGateway::
SendDtmf(DBusMessage *msgp)
{
	DBusMessageIter mi;
	char digit;
	AgPendingCommand *agpcp = 0;
	ErrorInfo error;
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_BYTE);
	dbus_message_iter_get_basic(&mi, &digit);

	return (CreatePendingCommand(msgp, agpcp) &&
		DoPendingCommand(agpcp, error,
				 m_sess->CmdSendDtmf(digit, &error)));
}

bool AudioGateway::
Answer(DBusMessage *msgp)
{
	AgPendingCommand *agpcp = 0;
	ErrorInfo error;

	return (CreatePendingCommand(msgp, agpcp) &&
		DoPendingCommand(agpcp, error,
				 m_sess->CmdAnswer(&error)));
}

bool AudioGateway::
CallDropHeldUdub(DBusMessage *msgp)
{
	AgPendingCommand *agpcp = 0;
	ErrorInfo error;

	return (CreatePendingCommand(msgp, agpcp) &&
		DoPendingCommand(agpcp, error,
				 m_sess->CmdCallDropHeldUdub(&error)));
}

bool AudioGateway::
CallSwapDropActive(DBusMessage *msgp)
{
	AgPendingCommand *agpcp = 0;
	ErrorInfo error;

	return (CreatePendingCommand(msgp, agpcp) &&
		DoPendingCommand(agpcp, error,
				 m_sess->CmdCallSwapDropActive(&error)));
}

bool AudioGateway::
CallSwapHoldActive(DBusMessage *msgp)
{
	AgPendingCommand *agpcp = 0;
	ErrorInfo error;

	return (CreatePendingCommand(msgp, agpcp) &&
		DoPendingCommand(agpcp, error,
				 m_sess->CmdCallSwapHoldActive(&error)));
}

bool AudioGateway::
CallLink(DBusMessage *msgp)
{
	AgPendingCommand *agpcp = 0;
	ErrorInfo error;

	return (CreatePendingCommand(msgp, agpcp) &&
		DoPendingCommand(agpcp, error,
				 m_sess->CmdCallLink(&error)));
}

bool AudioGateway::
CallTransfer(DBusMessage *msgp)
{
	AgPendingCommand *agpcp = 0;
	ErrorInfo error;

	return (CreatePendingCommand(msgp, agpcp) &&
		DoPendingCommand(agpcp, error,
				 m_sess->CmdCallTransfer(&error)));
}


bool AudioGateway::
GetState(DBusMessage *msgp, uint8_t &val)
{
	val = State();
	return true;
}

bool AudioGateway::
GetCallState(DBusMessage *msgp, uint8_t &val)
{
	val = CallState();
	return true;
}

bool AudioGateway::
GetAudioState(DBusMessage *msgp, uint8_t &val)
{
	val = AudioState();
	return true;
}

bool AudioGateway::
GetClaimed(DBusMessage *msgp, bool &val)
{
	val = (m_owner != 0);
	return true;
}

bool AudioGateway::
GetVoluntaryDisconnect(DBusMessage *msgp, bool &val)
{
	val = m_sess->IsPriorDisconnectVoluntary();
	return true;
}

bool AudioGateway::
GetAddress(DBusMessage *msgp, const DbusProperty *propp, DBusMessageIter &mi)
{
	char bdaddr[32], *bap;
	m_sess->GetDevice()->GetAddr(bdaddr);
	bap = bdaddr;
	return dbus_message_iter_append_basic(&mi,
					      DBUS_TYPE_STRING,
					      &bap);
}

bool AudioGateway::
GetName(DBusMessage *msgp, const char * &val)
{
	val = m_sess->GetDevice()->GetName();
	return true;
}

bool AudioGateway::
GetKnown(DBusMessage *msgp, bool &val)
{
	val = m_known;
	return true;
}

bool AudioGateway::
SetKnown(DBusMessage *msgp, const bool &val, bool &doreply)
{
	ErrorInfo error;
	char addr[32];

	m_sess->GetDevice()->GetAddr(addr);
	if ((val && !m_hf->m_config->Set("devices", addr,
					 m_sess->IsAutoReconnect())) ||
	    (!val && !m_hf->m_config->Delete("devices", addr, &error)) ||
	    !m_hf->SaveConfig(&error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}

	DoSetKnown(val);
	return true;
}

bool AudioGateway::
GetAutoReconnect(DBusMessage *msgp, bool &val)
{
	val = m_sess->IsAutoReconnect();
	return true;
}

bool AudioGateway::
SetAutoReconnect(DBusMessage *msgp, const bool &val, bool &doreply)
{
	ErrorInfo error;

	if (val && !m_owner && !m_known) {
		return SendReplyError(msgp,
				      HFPD_ERROR_FAILED,
				      "Device not known or claimed");
	}

	if (!DoSetAutoReconnect(val, &error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}

	return true;
}

static bool
AddStringBool(DBusMessageIter &ami, const char *str, dbus_bool_t val)
{
	DBusMessageIter dmi;
	if (!dbus_message_iter_open_container(&ami,
					      DBUS_TYPE_DICT_ENTRY,
					      0,
					      &dmi) ||
	    !dbus_message_iter_append_basic(&dmi,
					    DBUS_TYPE_STRING,
					    &str) ||
	    !dbus_message_iter_append_basic(&dmi,
					    DBUS_TYPE_BOOLEAN,
					    &val) ||
	    !dbus_message_iter_close_container(&ami, &dmi))
		return false;
	return true;
}

bool AudioGateway::
GetFeatures(DBusMessage *msgp, const DbusProperty *propp,
	    DBusMessageIter &mi)
{
	DBusMessageIter ami;

	if (!dbus_message_iter_open_container(&mi,
					      DBUS_TYPE_ARRAY,
			      DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
			      DBUS_TYPE_STRING_AS_STRING
			      DBUS_TYPE_BOOLEAN_AS_STRING
			      DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
					      &ami))
		return false;

	if (!AddStringBool(ami, "ThreeWayCalling",
			   m_sess->FeatureThreeWayCalling()) ||
	    !AddStringBool(ami, "ECNR",
			   m_sess->FeatureEcnr()) ||
	    !AddStringBool(ami, "VoiceRecognition",
			   m_sess->FeatureVoiceRecog()) ||
	    !AddStringBool(ami, "InBandRingTone",
			   m_sess->FeatureInBandRingTone()) ||
	    !AddStringBool(ami, "VoiceTag",
			   m_sess->FeatureVoiceTag()) ||
	    !AddStringBool(ami, "RejectCall",
			   m_sess->FeatureRejectCall()) ||
	    !AddStringBool(ami, "EnhancedCallStatus",
			   m_sess->FeatureEnhancedCallStatus()) ||
	    !AddStringBool(ami, "EnhancedCallControl",
			   m_sess->FeatureEnhancedCallControl()) ||
	    !AddStringBool(ami, "DropHeldUdub",
			   m_sess->FeatureDropHeldUdub()) ||
	    !AddStringBool(ami, "SwapDropActive",
			   m_sess->FeatureSwapDropActive()) ||
	    !AddStringBool(ami, "DropActive",
			   m_sess->FeatureDropActive()) ||
	    !AddStringBool(ami, "SwapHoldActive",
			   m_sess->FeatureSwapHoldActive()) ||
	    !AddStringBool(ami, "PrivateConsult",
			   m_sess->FeaturePrivateConsult()) ||
	    !AddStringBool(ami, "Link",
			   m_sess->FeatureLink()) ||
	    !AddStringBool(ami, "Transfer",
			   m_sess->FeatureTransfer()) ||
	    !AddStringBool(ami, "CallSetupIndicator",
			   m_sess->FeatureIndCallSetup()) ||
	    !AddStringBool(ami, "SignalStrengthIndicator",
			   m_sess->FeatureIndSignalStrength()) ||
	    !AddStringBool(ami, "RoamingIndicator",
			   m_sess->FeatureIndRoaming()) ||
	    !AddStringBool(ami, "BatteryChargeIndicator",
			   m_sess->FeatureIndBatteryCharge()))
		return false;

	if (!dbus_message_iter_close_container(&mi, &ami))
		return false;

	return true;
}

bool AudioGateway::
GetRawFeatures(DBusMessage *msgp, dbus_uint32_t &val)
{
	val = (State() == HFPD_AG_CONNECTED) ? m_sess->GetFeatures() : 0;
	return true;
}


HandsFree::
HandsFree(DispatchInterface *dip, DbusSession *dbusp)
	: HfpdExportObject(HFPD_HANDSFREE_OBJECT, s_ifaces),
	  m_di(dip), m_dbus(dbusp), m_hub(0), m_hfp(0),
	  m_sound(0), m_inquiry_state(false),
	  m_accept_unknown(false), m_voice_persist(false),
	  m_voice_autoconnect(false), m_client_create(false), m_config(0)
{
}

HandsFree::
~HandsFree()
{
}

bool HandsFree::
Init(const char *cfgfile)
{
	bool res;

	m_config = new ConfigHandler(GetDi());
	if (!m_config)
		goto failed;

	if (!m_config->Init(cfgfile))
		goto failed;

	m_hub = new BtHub(GetDi());
	if (!m_hub)
		goto failed;

	m_hub->cb_BtDeviceFactory.Register(this, &HandsFree::DeviceFactory);
	m_hub->cb_NotifySystemState.Register(this,
					     &HandsFree::NotifySystemState);
	m_hub->cb_InquiryResult.Register(this,
					 &HandsFree::NotifyInquiryResult);

	m_hfp = new HfpService;
	if (!m_hfp)
		goto failed;

	m_hfp->cb_HfpSessionFactory.Register(this,
					     &HandsFree::SessionFactory);

	res = m_hub->AddService(m_hfp);
	assert(res);

	m_sound = new SoundIoObj(this);
	if (!m_sound)
		goto failed;
	if (!m_sound->Init(m_dbus))
		goto failed;

	LoadDeviceConfig();

	if (!m_dbus->ExportObject(this))
		goto failed;

	return true;

failed:
	Cleanup();
	return false;
}

void HandsFree::
Cleanup(void)
{
	if (m_dbus)
		m_dbus->UnexportObject(this);
	if (m_hub) {
		m_hub->Stop();
		DoStopped();
	}
	if (m_sound) {
		delete m_sound;
		m_sound = 0;
	}
	if (m_hfp) {
		m_hub->RemoveService(m_hfp);
		delete m_hfp;
		m_hfp = 0;
	}
	if (m_hub) {
		delete m_hub;
		m_hub = 0;
	}
	if (m_config) {
		delete m_config;
		m_config = 0;
	}
}

bool HandsFree::
SaveConfig(ErrorInfo *error, bool force)
{
	return m_config->SaveConfig(error, force);
}


void HandsFree::
LoadDeviceConfig(void)
{
	HfpSession *sessp;
	AudioGateway *agp;
	const char *addr;
	bool val;
	bool autorestart;
	int secmode;
	ConfigFile::Iterator it;

	m_config->Get("daemon", "autosave", val, false);
	m_config->SetAutoSave(val);
	m_config->Get("daemon", "secmode", secmode, RFCOMM_SEC_AUTH);
	if ((secmode != RFCOMM_SEC_NONE) &&
	    (secmode != RFCOMM_SEC_AUTH) &&
	    (secmode != RFCOMM_SEC_CRYPT)) {
		/* Repair invalid secmodes */
		secmode = RFCOMM_SEC_AUTH;
		(void) m_config->Set("daemon", "secmode", secmode);
		(void) m_config->SaveConfig();
	}
	m_hfp->SetSecMode((rfcomm_secmode_t) secmode);
	m_config->Get("daemon", "autorestart", autorestart, true);
	m_hub->SetAutoRestart(autorestart);
	m_config->Get("daemon", "acceptunknown", m_accept_unknown, false);
	m_config->Get("daemon", "scoenabled", val, true);
	val = m_hfp->SetScoEnabled(val);
	assert(val);
	m_config->Get("daemon", "voicepersist", m_voice_persist, false);
	m_config->Get("daemon", "voiceautoconnect", m_voice_autoconnect,false);

	if (m_config->FirstInSection(it, "devices")) {
		m_client_create = true;
		do {
			addr = it.GetKey();
			val = it.GetValueBool();
			sessp = m_hfp->GetSession(addr);
			if (sessp) {
				agp = GetAudioGateway(sessp);
				if (agp) {
					agp->DoSetKnown(true);
					sessp->SetAutoReconnect(val);
				}
				sessp->Put();
			}
		} while (m_config->Next(it) &&
			 !strcmp(it.GetSection(), "devices"));
		m_client_create = false;
	}
}

void HandsFree::
LogMessage(libhfp::DispatchInterface::logtype_t lt, const char *msg)
{
	dbus_uint32_t ltu;

	ltu = (dbus_uint32_t) lt;
	(void) SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			      "LogMessage",
			      DBUS_TYPE_UINT32, &ltu,
			      DBUS_TYPE_STRING, &msg,
			      DBUS_TYPE_INVALID);
}

AudioGateway *HandsFree::
FindAudioGateway(const char *agpath)
{
	AudioGateway *agp;
	ListItem *listp;
	ListForEach(listp, &m_gateways) {
		agp = GetContainer(listp, AudioGateway, m_links);
		if (!strcmp(agpath, agp->GetDbusPath()))
			return agp;
	}

	return 0;
}

BtDevice *HandsFree::
DeviceFactory(bdaddr_t const &addr)
{
	BtDevice *devp;
	devp = m_hub->DefaultDevFactory(addr);
	if (devp) {
		devp->cb_NotifyNameResolved.Register(this,
					     &HandsFree::NotifyNameResolved);
	}
	return devp;
}

HfpSession *HandsFree::
SessionFactory(BtDevice *devp)
{
	char bda[32], pathbuf[128], *path = 0;
	AudioGateway *agp = 0;
	HfpSession *sessp = 0;

	devp->GetAddr(bda);

	if (!m_accept_unknown && !m_client_create) {
		/*
		 * Our client didn't ask for this session to be created,
		 * and requested that we refuse devices it hasn't told
		 * us about.  So we refuse.
		 */
		GetDi()->LogInfo("AG %s: Refusing connection", bda);
		return 0;
	}

	sessp = m_hfp->DefaultSessionFactory(devp);
	if (!sessp)
		goto failed;

	for (path = bda; *path; path++) {
		if (*path == ':')
			*path = '_';
	}
	snprintf(pathbuf, sizeof(pathbuf), "%s/%s",
		 HFPD_HANDSFREE_OBJECT, bda);
	path = strdup(pathbuf);
	if (!path)
		goto failed;

	agp = new AudioGateway(this, sessp, path);
	if (!agp) {
		free(path);
		goto failed;
	}

	if (!m_dbus->ExportObject(agp))
		goto failed;

	m_gateways.AppendItem(agp->m_links);

	/* Announce our presence */
	agp->NotifyConnection(sessp, 0);

	(void) SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			      "AudioGatewayAdded",
			      DBUS_TYPE_OBJECT_PATH, &path,
			      DBUS_TYPE_INVALID);

	return sessp;

failed:
	if (agp)
		delete agp;
	if (sessp)
		delete sessp;
	return 0;
}


struct NameResolveRequest {
	ListItem	links;
	DBusMessage	*req;
	BtDevice	*dev;

	void Complete(HfpdExportObject *objp, const char *name) {
		assert(req);
		assert(dev);
		objp->SendReplyArgs(req,
				    DBUS_TYPE_STRING, &name,
				    DBUS_TYPE_INVALID);
		dbus_message_unref(req);
		req = 0;
	}

	void Failed(HfpdExportObject *objp, ErrorInfo &error) {
		assert(req);
		assert(dev);
		objp->SendReplyErrorInfo(req, error);
		dbus_message_unref(req);
		req = 0;
	}

	NameResolveRequest(DBusMessage *msgp, BtDevice *devp)
		: req(msgp), dev(devp) {
		dbus_message_ref(msgp);
		devp->Get();
	}
	~NameResolveRequest() {
		if (req) {
			dbus_message_unref(req);
		        req = 0;
		}
		if (dev) {
			dev->Put();
			dev = 0;
		}
	}
};

void HandsFree::
DoStopped(void)
{
	BtDevice *devp;
	NameResolveRequest *reqp, *nextp;
	dbus_bool_t res;
	ErrorInfo error;

	GetDi()->LogInfo("Bluetooth System Shut Down");

	res = false;
	(void) SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			      "SystemStateChanged",
			      DBUS_TYPE_BOOLEAN, &res,
			      DBUS_TYPE_INVALID);

	error.Set(LIBHFP_ERROR_SUBSYS_BT,
		  LIBHFP_ERROR_BT_SHUTDOWN,
		  "Bluetooth system shut down");

	/* Find all the name resolution requests and cancel them */
	for (devp = m_hub->GetFirstDevice();
	     devp;
	     devp = m_hub->GetNextDevice(devp)) {
		reqp = (NameResolveRequest *) devp->GetPrivate();
		while (reqp) {
			nextp = reqp->links.Empty()
				? 0
				: GetContainer(reqp->links.next,
					       NameResolveRequest, links);
			reqp->Failed(this, error);
			reqp->links.Unlink();
			delete reqp;
			reqp = nextp;
			devp->SetPrivate(reqp);
		}
	}
}


void HandsFree::
DoStarted(void)
{
	dbus_bool_t a = true;
	uint32_t devclass;

	GetDi()->LogInfo("Bluetooth System Started");

	(void) SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			      "SystemStateChanged",
			      DBUS_TYPE_BOOLEAN, &a,
			      DBUS_TYPE_INVALID);

	if (m_hub->GetHci() &&
	    m_hub->GetHci()->GetDeviceClassLocal(devclass) &&
	    !m_hfp->IsDeviceClassHf(devclass)) {
		m_hfp->SetDeviceClassHf(devclass);
		GetDi()->LogWarn("*** Your configured device class may "
				 "not be recognized as a hands-free\n"
				 "*** Edit /etc/bluetooth/hcid.conf "
				 "and change:\n"
				 "*** class 0x%06x;", devclass);
	}
}

void HandsFree::
NotifySystemState(ErrorInfo *reason)
{
	if (!m_hub->IsStarted()) {
		DoStopped();
		return;
	}

	DoStarted();
}

void HandsFree::
NotifyInquiryResult(BtDevice *devp, ErrorInfo *error)
{
	char buf[32];
	dbus_uint32_t dclass;
	char *cp;

	if (!devp) {
		if (m_inquiry_state) {
			dbus_bool_t st = false;
			if (SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
					   "InquiryStateChanged",
					   DBUS_TYPE_BOOLEAN, &st,
					   DBUS_TYPE_INVALID))
				m_inquiry_state = false;
		}
		return;
	}

	devp->GetAddr(buf);
	dclass = devp->GetDeviceClass();
	cp = buf;
	(void) SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			      "InquiryResult",
			      DBUS_TYPE_STRING, &cp,
			      DBUS_TYPE_UINT32, &dclass,
			      DBUS_TYPE_INVALID);
}


void HandsFree::
NotifyNameResolved(BtDevice *devp, const char *name, ErrorInfo *reason)
{
	NameResolveRequest *reqp, *nextp;
	HfpSession *sessp;
	AudioGateway *agp;

	/* Are there outstanding client requests? */
	reqp = (NameResolveRequest *) devp->GetPrivate();
	while (reqp) {
		nextp = reqp->links.Empty()
			? 0
			: GetContainer(reqp->links.next,
				       NameResolveRequest, links);
		if (name) {
			assert(!reason);
			reqp->Complete(this, name);
		} else {
			assert(reason);
			reqp->Failed(this, *reason);
		}
		reqp->links.Unlink();
		delete reqp;
		reqp = nextp;
		devp->SetPrivate(reqp);
	}

	/* Deliver the notification to a target audio gateway */
	if (name) {
		sessp = m_hfp->GetSession(devp, false);
		if (sessp) {
			agp = GetAudioGateway(sessp);
			if (agp)
				agp->NameResolved();
			sessp->Put();
		}
	}
}


bool HandsFree::
SaveSettings(DBusMessage *msgp)
{
	ErrorInfo error;

	if (!SaveConfig(&error, true))
		return SendReplyErrorInfo(msgp, error);

	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool HandsFree::
Start(DBusMessage *msgp)
{
	bool oldstate;
	ErrorInfo hfperror;

	oldstate = m_hub->IsStarted();
	if (!m_hub->Start(&hfperror))
		return SendReplyErrorInfo(msgp, hfperror);

	DoStarted();

	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID))
		return false;

	return true;
}

bool HandsFree::
Stop(DBusMessage *msgp)
{
	bool oldstate;
	dbus_bool_t res;

	oldstate = m_hub->IsStarted();
	if (oldstate)
		m_hub->Stop();

	res = true;
	if (!SendReplyArgs(msgp,
			   DBUS_TYPE_INVALID))
		return false;

	if (oldstate)
		DoStopped();

	return true;
}

bool HandsFree::
StartInquiry(DBusMessage *msgp)
{
	dbus_bool_t st;
	ErrorInfo error;

	if (!m_hub->StartInquiry(5000, &error))
		return SendReplyErrorInfo(msgp, error);

	if (!m_inquiry_state) {
		st = true;
		if (!SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
				    "InquiryStateChanged",
				    DBUS_TYPE_BOOLEAN, &st,
				    DBUS_TYPE_INVALID))
			return false;
		m_inquiry_state = true;
	}

	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID))
		return false;

	return true;
}

bool HandsFree::
StopInquiry(DBusMessage *msgp)
{
	dbus_bool_t st;

	if (m_inquiry_state) {
		st = false;
		if (!SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
				    "InquiryStateChanged",
				    DBUS_TYPE_BOOLEAN, &st,
				    DBUS_TYPE_INVALID))
			return false;
		m_inquiry_state = false;
	}

	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID))
		return false;

	m_hub->StopInquiry();
	return true;
}

bool HandsFree::
GetName(DBusMessage *msgp)
{
	NameResolveRequest *nrrp, *prevp;
	DBusMessageIter mi;
	BtDevice *devp;
	const char *addr;
	ErrorInfo error;
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &addr);
	devp = m_hub->GetDevice(addr);
	if (!devp)
		return false;

	if (devp->IsNameResolved()) {
		addr = devp->GetName();
		res = SendReplyArgs(msgp,
				    DBUS_TYPE_STRING, &addr,
				    DBUS_TYPE_INVALID);
		goto done;
	}

	if (!devp->ResolveName(&error)) {
		res = SendReplyErrorInfo(msgp, error);
		goto done;
	}

	/*
	 * In this path, the request is handled asynchronously by
	 * NotifyNameResolved() or DoStopped().
	 */
	nrrp = new NameResolveRequest(msgp, devp);
	if (!nrrp) {
		res = false;
		goto done;
	}

	if (devp->GetPrivate()) {
		prevp = (NameResolveRequest *) devp->GetPrivate();
		prevp->links.AppendItem(nrrp->links);
	} else {
		devp->SetPrivate(nrrp);
	}

	res = true;
done:
	devp->Put();
	return res;
}


bool HandsFree::
AddDevice(DBusMessage *msgp)
{
	AudioGateway *agp;
	HfpSession *sessp;
	DBusMessageIter mi;
	DbusPeer *peerp = 0;
	const char *addr, *path;
	ErrorInfo error;
	bool remove_dn = false, unsetknown = false, res;
	dbus_bool_t setknown, claim;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &addr);
	res = dbus_message_iter_next(&mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_BOOLEAN);
	dbus_message_iter_get_basic(&mi, &setknown);
	m_client_create = true;
	sessp = m_hfp->GetSession(addr);
	m_client_create = false;
	if (!sessp)
		return false;

	agp = GetAudioGateway(sessp);
	if (!agp) {
		res = false;
		goto done;
	}

	peerp = GetDbusSession()->GetPeer(msgp);
	if (!peerp) {
		res = false;
		goto done;
	}

	if (!setknown && agp->m_owner && (agp->m_owner->GetPeer() != peerp)) {
		res = SendReplyError(msgp,
				     HFPD_ERROR_FAILED,
				     "Device claimed by another client");
		goto done;
	}

	if (!agp->m_owner) {
		agp->m_owner = peerp->NewDisconnectNotifier();
		if (!agp->m_owner) {
			res = false;
			goto done;
		}
		remove_dn = true;
		sessp->Get();
		agp->m_owner->Register(agp,
				       &AudioGateway::
				       OwnerDisconnectNotify);

		GetDi()->LogInfo("AG %s: claimed by D-Bus peer %s",
				 addr, peerp->GetName());

		claim = true;
		(void) agp->SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
					   "ClaimStateChanged",
					   DBUS_TYPE_BOOLEAN, &claim,
					   DBUS_TYPE_INVALID);
	}

	if (setknown && !agp->m_known) {
		if (!m_config->Set("devices", addr,
				   agp->m_sess->IsAutoReconnect(), &error) ||
		    !SaveConfig(&error)) {
			res = SendReplyErrorInfo(msgp, error);
			goto done;
		}
		agp->DoSetKnown(true);
		unsetknown = true;
	}

	path = agp->GetDbusPath();
	if (!SendReplyArgs(msgp,
			   DBUS_TYPE_OBJECT_PATH, &path,
			   DBUS_TYPE_INVALID)) {
		res = false;
		goto done;
	}

done:
	if (!res && unsetknown) {
		/* We will try */
		(void) m_config->Delete("devices", addr);
		(void) SaveConfig();
		agp->DoSetKnown(false);
	}
	if (!res && remove_dn) {
		delete agp->m_owner;
		agp->m_owner = 0;
		sessp->Put();
	}
	if (peerp)
		peerp->Put();

	sessp->Put();
	return res;
}

bool HandsFree::
RemoveDevice(DBusMessage *msgp)
{
	AudioGateway *agp;
	HfpSession *sessp = 0;
	DBusMessageIter mi;
	DbusPeer *peerp = 0;
	const char *addr;
	dbus_bool_t claim;
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &addr);
	sessp = m_hfp->GetSession(addr, false);
	if (!sessp) {
		res = SendReplyError(msgp,
				     HFPD_ERROR_FAILED,
				     "No such audio gateway");
		goto done;
	}

	peerp = GetDbusSession()->GetPeer(msgp);
	if (!peerp) {
		res = false;
		goto done;
	}

	agp = GetAudioGateway(sessp);
	if (!agp) {
		res = SendReplyError(msgp,
				     HFPD_ERROR_FAILED,
				     "No such audio gateway");
		goto done;
	}

	if (agp->m_owner && (agp->m_owner->GetPeer() != peerp)) {
		res = SendReplyError(msgp,
				     HFPD_ERROR_FAILED,
				     "This audio gateway has been claimed by "
				     "another client");
		goto done;
	}

	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID)) {
		res = false;
		goto done;
	}

	if (!agp->m_known)
		agp->DoDisconnect();

	if (agp->m_owner) {
		GetDi()->LogInfo("AG %s: disowned by D-Bus peer %s",
				 addr, agp->m_owner->GetPeer()->GetName());

		claim = false;
		(void) agp->SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
					   "ClaimStateChanged",
					   DBUS_TYPE_BOOLEAN, &claim,
					   DBUS_TYPE_INVALID);

		delete agp->m_owner;
		agp->m_owner = 0;
		sessp->Put();
	}
	res = true;

done:
	if (peerp)
		peerp->Put();
	if (sessp)
		sessp->Put();
	return res;
}

bool HandsFree::
GetVersion(DBusMessage *msgp, dbus_uint32_t &val)
{
	val = 3;
	return true;
}

bool HandsFree::
GetAutoSave(DBusMessage *msgp, bool &val)
{
	val = m_config->GetAutoSave();
	return true;
}

bool HandsFree::
SetAutoSave(DBusMessage *msgp, const bool &val, bool &doreply)
{
	ErrorInfo error;

	if (m_config->GetAutoSave() == val)
		return true;

	if (!m_config->Set("daemon", "autosave", val, &error) ||
	    !SaveConfig(&error, val)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}

	m_config->SetAutoSave(val);
	return true;
}

bool HandsFree::
GetSaveFile(DBusMessage *msgp, const char * &val)
{
	val = m_config->GetConfigFile();
	return true;
}

bool HandsFree::
SetSaveFile(DBusMessage *msgp, const char * const &val, bool &doreply)
{
	ErrorInfo error;

	if (!m_config->SetConfigFile(val, &error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}

	return true;
}


bool HandsFree::
GetSystemState(DBusMessage *msgp, bool &val)
{
	val = m_hub->IsStarted();
	return true;
}

bool HandsFree::
GetAutoRestart(DBusMessage *msgp, bool &val)
{
	val = m_hub->GetAutoRestart();
	return true;
}

bool HandsFree::
SetAutoRestart(DBusMessage *msgp, const bool &val, bool &doreply)
{
	ErrorInfo error;

	if (val == m_hub->GetAutoRestart())
		return true;

	if (!m_config->Set("daemon", "autorestart", val, &error) ||
	    !SaveConfig(&error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}

	m_hub->SetAutoRestart(val);
	return true;
}

bool HandsFree::
GetSecMode(DBusMessage *msgp, uint8_t &val)
{
	val = m_hfp->GetSecMode();
	return true;
}

bool HandsFree::
SetSecMode(DBusMessage *msgp, const uint8_t &val, bool &doreply)
{
	ErrorInfo error;
	rfcomm_secmode_t old;

	old = m_hfp->GetSecMode();
	if (old == (rfcomm_secmode_t) val)
		return true;

	if (!m_hfp->SetSecMode((rfcomm_secmode_t)val, &error) ||
	    !m_config->Set("daemon", "secmode", val, &error) ||
	    !SaveConfig(&error)) {
		doreply = false;
		(void) m_hfp->SetSecMode(old);
		return SendReplyErrorInfo(msgp, error);
	}

	return true;
}

bool HandsFree::
GetAcceptUnknown(DBusMessage *msgp, bool &val)
{
	val = m_accept_unknown;
	return true;
}

bool HandsFree::
SetAcceptUnknown(DBusMessage *msgp, const bool &val, bool &doreply)
{
	ErrorInfo error;

	if (m_accept_unknown == val)
		return true;

	if (!m_config->Set("daemon", "acceptunknown", val, &error) ||
	    !SaveConfig(&error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}

	m_accept_unknown = val;
	return true;
}

bool HandsFree::
GetScoEnabled(DBusMessage *msgp, bool &val)
{
	val = m_hfp->GetScoEnabled();
	return true;
}

bool HandsFree::
SetScoEnabled(DBusMessage *msgp, const bool &val, bool &doreply)
{
	ErrorInfo error;

	if (m_hfp->GetScoEnabled() == val)
		return true;

	if (!m_hfp->SetScoEnabled(val, &error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}

	if (!m_config->Set("daemon", "scoenabled", val, &error) ||
	    !SaveConfig(&error)) {
		(void) m_hfp->SetScoEnabled(!val);
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}

	return true;
}

bool HandsFree::
GetVoicePersist(DBusMessage *msgp, bool &val)
{
	val = m_voice_persist;
	return true;
}

bool HandsFree::
SetVoicePersist(DBusMessage *msgp, const bool &val, bool &doreply)
{
	ErrorInfo error;

	if (m_voice_persist == val)
		return true;

	if (!m_config->Set("daemon", "voicepersist", val, &error) ||
	    !SaveConfig(&error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}

	m_voice_persist = val;
	return true;
}

bool HandsFree::
GetVoiceAutoConnect(DBusMessage *msgp, bool &val)
{
	val = m_voice_autoconnect;
	return true;
}

bool HandsFree::
SetVoiceAutoConnect(DBusMessage *msgp, const bool &val, bool &doreply)
{
	ErrorInfo error;

	if (m_voice_autoconnect == val)
		return true;

	if (!m_config->Set("daemon", "voiceautoconnect", val, &error) ||
	    !SaveConfig(&error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}

	m_voice_autoconnect = val;
	return true;
}

bool HandsFree::
GetAudioGateways(DBusMessage *msgp, const DbusProperty *propp,
		 DBusMessageIter &mi)
{
	DBusMessageIter ami;
	const char *name;
	ListItem *listp;
	AudioGateway *agp;

	if (!dbus_message_iter_open_container(&mi,
					      DBUS_TYPE_ARRAY,
					      DBUS_TYPE_OBJECT_PATH_AS_STRING,
					      &ami))
		return false;

	ListForEach(listp, &m_gateways) {
		agp = GetContainer(listp, AudioGateway, m_links);
		name = agp->GetDbusPath();
		if (!dbus_message_iter_append_basic(&ami,
						    DBUS_TYPE_OBJECT_PATH,
						    &name))
			return false;
	}

	if (!dbus_message_iter_close_container(&mi, &ami))
		return false;

	return true;
}

bool HandsFree::
GetReportCapabilities(DBusMessage *msgp, dbus_uint32_t &val)
{
	val = m_hfp->GetCaps();
	return true;
}

bool HandsFree::
SetReportCapabilities(DBusMessage *msgp, const dbus_uint32_t &val,
		      bool &doreply)
{
	m_hfp->SetCaps(val);
	return true;
}

bool HandsFree::
GetServiceName(DBusMessage *msgp, const char * &val)
{
	val = m_hfp->GetServiceName();
	return true;
}

bool HandsFree::
SetServiceName(DBusMessage *msgp, const char * const &val, bool &doreply)
{
	ErrorInfo error;

	if (!m_hfp->SetServiceName(val, &error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	return true;
}

bool HandsFree::
GetServiceDesc(DBusMessage *msgp, const char * &val)
{
	val = m_hfp->GetServiceDesc();
	return true;
}

bool HandsFree::
SetServiceDesc(DBusMessage *msgp, const char * const &val, bool &doreply)
{
	ErrorInfo error;

	if (!m_hfp->SetServiceDesc(val, &error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	return true;
}



/*
 * SoundIo related method calls
 */

class MicVolumeFilter : public SoundIoFilter {
public:
	SoundIoObj	*m_target;
	sio_sampnum_t	m_period;
	sio_sampnum_t	m_position;
	uint16_t	m_high;
	uint16_t	m_low;
	bool		m_doup;
	bool		m_dovol;

	MicVolumeFilter(SoundIoObj *targp, sio_sampnum_t period)
		: m_target(targp), m_period(period) {
		Reset();
	}

	void Reset(void) {
		m_position = 0;
		m_high = 0;
		m_low = 0xffff;
	}

	void SendEvent(uint16_t amp) {
		dbus_uint32_t pos;
		pos = m_position;
		(void) m_target->SendSignalArgs(HFPD_SOUNDIO_INTERFACE_NAME,
						"MonitorNotify",
						DBUS_TYPE_UINT32, &pos,
						DBUS_TYPE_UINT16, &amp,
						DBUS_TYPE_INVALID);
	}

	static inline uint16_t GetSample(const void *sampp) {
		/* Correct endianness assumed */
		return *(uint16_t *) sampp;
	}

	virtual bool FltPrepare(SoundIoFormat const &fmt,
				bool up, bool dn, ErrorInfo *error) {
		if ((fmt.sampletype != SIO_PCM_S16_LE) ||
		    (fmt.nchannels != 1)) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_FORMAT_MISMATCH,
					   "MicVolumeFilter requires "
					   "S16_LE, 1ch");
			return false;
		}
		assert(fmt.bytes_per_record == 2);
		m_doup = up;
		m_dovol = up;
		return true;
	}

	virtual void FltCleanup(void) {
		/* Nothing to do! */
	}

	virtual SoundIoBuffer const *FltProcess(bool up,
						SoundIoBuffer const &src,
						SoundIoBuffer &dest) {
		sio_sampnum_t count, remain, pd;
		uint16_t *sampp, *endp, samp, h, l;

		if (up != m_doup)
			return &src;

		remain = m_period - (m_position % m_period);
		count = src.m_size;
		h = m_high;
		l = m_low;
		sampp = (uint16_t *) src.m_data;
		while (count) {
			pd = count;
			if (pd > remain)
				pd = remain;
			count -= pd;
			remain -= pd;
			m_position += pd;
			if (m_dovol) {
				for (endp = sampp + pd;
				     sampp < endp;
				     sampp++) {
					samp = GetSample(sampp);
					if (samp > h)
						h = samp;
					if (samp < l)
						l = samp;
				}
			}
			if (!remain) {
				SendEvent(m_dovol ? (h - l) : 0);
				h = 0;
				l = 0xffff;
				remain = m_period;
			}
		}
		m_high = h;
		m_low = l;
		return &src;
	}
};


SoundIoObj::
SoundIoObj(HandsFree *hfp)
	: HfpdExportObject(HFPD_SOUNDIO_OBJECT, s_ifaces),
	  m_hf(hfp), m_sound(0),
	  m_state(HFPD_SIO_DECONFIGURED), m_state_sent(HFPD_SIO_DECONFIGURED),
	  m_ringtone(0), m_sigproc(0),
	  m_membuf(0), m_membuf_size(0),
	  m_config(hfp->m_config),
	  m_snoop(0), m_snoop_ep(0), m_snoop_filename(0),
	  m_state_owner(0)
{
}

SoundIoObj::
~SoundIoObj()
{
	if (m_state > HFPD_SIO_DECONFIGURED)
		EpRelease();
	Cleanup();
}


void SoundIoObj::
NotifySoundStop(SoundIoManager *mgrp, ErrorInfo &error)
{
	assert(mgrp == m_sound);
	EpRelease(HFPD_SIO_INVALID, &error);
}

void SoundIoObj::
NotifySkew(SoundIoManager *mgrp, sio_stream_skewinfo_t reason, double value)
{
	uint8_t val = (uint8_t) reason;

	(void) SendSignalArgs(HFPD_SOUNDIO_INTERFACE_NAME,
			      "SkewNotify",
			      DBUS_TYPE_BYTE, &val,
			      DBUS_TYPE_DOUBLE, &value,
			      DBUS_TYPE_INVALID);
}

bool SoundIoObj::
Init(DbusSession *dbusp)
{
	const char *driver, *driveropts;
	int val;

	assert(m_state == HFPD_SIO_DECONFIGURED);

	m_sound = new SoundIoManager(GetDi());
	if (!m_sound)
		goto failed;

	m_sound->cb_NotifyAsyncState.Register(this,
					      &SoundIoObj::NotifySoundStop);
	m_sound->cb_NotifySkew.Register(this,
					&SoundIoObj::NotifySkew);

	m_config->Get("audio", "driver", driver, 0);
	m_config->Get("audio", "driveropts", driveropts, 0);

	if (driver && !driver[0])
		driver = 0;
	if (driveropts && !driveropts[0])
		driveropts = 0;
	if (!driver && driveropts)
		driveropts = 0;

	if (!m_sound->SetDriver(driver, driveropts)) {
		GetDi()->LogWarn("Could not configure sound driver \"%s\" "
				 "with options \"%s\"",
				 driver ? driver : "",
				 driveropts ? driveropts : "");
		goto failed;
	}

	m_config->Get("audio", "packetinterval", val, 20);
	m_sound->SetPacketIntervalHint(val);
	m_config->Get("audio", "minbufferfill", val, 0);
	m_sound->SetMinBufferFillHint(val);
	m_config->Get("audio", "jitterwindow", val, 0);
	m_sound->SetJitterWindowHint(val);

#if defined(USE_SPEEXDSP)
	m_sigproc = SoundIoFltCreateSpeex(GetDi());
	if (!m_sigproc) {
		GetDi()->LogWarn("Could not create DSP filter object");
		goto failed;
	}

	m_config->Get("dsp", "denoise",
		      m_procprops.noisereduce, true);
	m_config->Get("dsp", "echocancel_ms",
		      m_procprops.echocancel_ms, 100);
	m_config->Get("dsp", "autogain",
		      m_procprops.agc_level, 10000);
	m_config->Get("dsp", "dereverb_level",
		      m_procprops.dereverb_level, 0.0);
	m_config->Get("dsp", "dereverb_decay",
		      m_procprops.dereverb_decay, 0.0);

	if (!m_sigproc->Configure(m_procprops)) {
		GetDi()->LogWarn("Could not configure DSP settings");
		goto failed;
	}

	m_sound->SetDsp(m_sigproc);
#endif /* defined(USE_SPEEXDSP) */

	if (!dbusp->ExportObject(this))
		goto failed;

	UpdateState(HFPD_SIO_STOPPED);
	return true;

failed:
	Cleanup();
	return false;
}

void SoundIoObj::
Cleanup(void)
{
	CleanupSnoop();
	if (GetDbusSession()) {
		GetDbusSession()->UnexportObject(this);
	}
	if (m_sound) {
		delete m_sound;
		m_sound = 0;
	}
	if (m_sigproc) {
		delete m_sigproc;
		m_sigproc = 0;
	}
}

void SoundIoObj::
CleanupSnoop(void)
{
	if (m_snoop) {
		m_sound->RemoveFilter(m_snoop);
		delete m_snoop;
		m_snoop = 0;
	}
	if (m_snoop_ep) {
		delete m_snoop_ep;
		m_snoop_ep = 0;
	}
	if (m_snoop_filename) {
		free(m_snoop_filename);
		m_snoop_filename = 0;
	}
}

bool SoundIoObj::
UpdateState(SoundIoState st, ErrorInfo *reason)
{
	const char *str1, *str2;
	uint8_t stx = st;

	m_state = st;

	/*
	 * This can skip transitions, but it won't if everybody
	 * cooperates to handle the errors.  Unfortunately this is
	 * unlikely to ever be the case.  :-/
	 */
	if (m_state_sent == st)
		return true;

	if (((st == HFPD_SIO_AUDIOGATEWAY_CONNECTING) ||
	     (st == HFPD_SIO_AUDIOGATEWAY)) &&
	    (m_state_sent != HFPD_SIO_AUDIOGATEWAY_CONNECTING) &&
	    (m_state_sent != HFPD_SIO_AUDIOGATEWAY)) {
		assert(m_bound_ag);
		str1 = m_bound_ag->GetDbusPath();
		if (!SendSignalArgs(HFPD_SOUNDIO_INTERFACE_NAME,
				    "AudioGatewaySet",
				    DBUS_TYPE_OBJECT_PATH, &str1,
				    DBUS_TYPE_INVALID))
			return false;
	}

	if (reason &&
	    !reason->Matches(LIBHFP_ERROR_SUBSYS_SOUNDIO,
			     LIBHFP_ERROR_SOUNDIO_DATA_EXHAUSTED)) {
		assert(st == HFPD_SIO_STOPPED);

		/*
		 * This is as close as we can get to a signalled exception.
		 */
		str1 = DbusErrorName(*reason);
		str2 = reason->Desc();

		if (!SendSignalArgs(HFPD_SOUNDIO_INTERFACE_NAME,
				    "StreamAborted",
				    DBUS_TYPE_STRING, &str1,
				    DBUS_TYPE_STRING, &str2,
				    DBUS_TYPE_INVALID))
			return false;

	} else {
		if (!SendSignalArgs(HFPD_SOUNDIO_INTERFACE_NAME,
				    "StateChanged",
				    DBUS_TYPE_BYTE, &stx,
				    DBUS_TYPE_INVALID))
			return false;
	}

	m_state_sent = st;
	return true;
}

bool SoundIoObj::
SetupStateOwner(DBusMessage *msgp)
{
	DbusPeer *peerp;
	DbusPeerDisconnectNotifier *owner;

	/*
	 * Register a D-Bus peer as the owner of whatever
	 * state the audio device is in.  If the owner disconnects
	 * from D-Bus and does not issue a command to halt the audio
	 * device itself, streaming will be halted and the audio
	 * device released.
	 *
	 * This can get in the way of using the dbus-send command
	 * line tool to control hfpd.  If support for this is
	 * desired, just alter this method to return true without
	 * doing anything.
	 */

	assert(!m_state_owner);

	peerp = GetDbusSession()->GetPeer(msgp);
	if (!peerp)
		return false;

	owner = peerp->NewDisconnectNotifier();
	if (!owner) {
		peerp->Put();
		return false;
	}

	owner->Register(this, &SoundIoObj::StateOwnerDisconnectNotify);
	m_state_owner = owner;
	return true;
}

void SoundIoObj::
StateOwnerDisconnectNotify(DbusPeerDisconnectNotifier *notp)
{
	assert(notp == m_state_owner);

	GetDi()->LogInfo("SoundIo: D-Bus state owner disconnected");

	EpRelease();
	assert(!m_state_owner);
}

void SoundIoObj::
EpRelease(SoundIoState st, ErrorInfo *reason)
{
	SoundIo *ep;
	SoundIoFilter *fltp;
	bool res;

	if (st == HFPD_SIO_INVALID)
		st = m_state;

	switch (st) {
	case HFPD_SIO_STOPPED:
		assert(!m_sound->IsStarted());
		assert(!m_sound->GetSecondary());
		assert(m_sound->IsDspEnabled());
		assert(!m_bound_ag);
		assert(!m_state_owner);
		break;
	case HFPD_SIO_AUDIOGATEWAY:
		assert(m_bound_ag);
		assert(m_sound->GetSecondary() == m_bound_ag->GetSoundIo());
		assert(m_sound->IsDspEnabled());
		m_sound->Stop();
		m_sound->SetSecondary(0);
		/* fall-thru */
	case HFPD_SIO_AUDIOGATEWAY_CONNECTING:
		assert(m_bound_ag);
		assert(!m_sound->GetSecondary());
		assert(m_sound->IsDspEnabled());
		if (m_bound_ag->m_audio_bind == this)
			m_bound_ag->m_audio_bind = 0;
		m_bound_ag->GetSoundIo()->SndClose();
		m_bound_ag->NotifyAudioConnection(0, 0);
		m_bound_ag->Put();
		m_bound_ag = 0;
		break;
	case HFPD_SIO_FILE:
		ep = m_sound->GetSecondary();
		assert(ep);
		m_sound->Stop();
		m_sound->SetSecondary(0);
		assert(!m_sound->IsDspEnabled());
		res = m_sound->SetDspEnabled(true);
		assert(res);
		ep->SndClose();
		delete ep;
		if (m_state_owner) {
			delete m_state_owner;
			m_state_owner = 0;
		}
		break;
	case HFPD_SIO_LOOPBACK:
		assert(!m_sound->GetSecondary());
		m_sound->Stop();
		if (m_state_owner) {
			delete m_state_owner;
			m_state_owner = 0;
		}
		break;
	case HFPD_SIO_MEMBUF:
		assert(m_membuf);
		assert(m_sound->GetSecondary() == m_membuf);
		m_sound->Stop();
		m_sound->SetSecondary(0);
		m_membuf->SndClose();
		fltp = m_sound->RemoveTop();
		if (fltp)
			delete fltp;
		if (m_state_owner) {
			delete m_state_owner;
			m_state_owner = 0;
		}
		break;
	default:
		abort();
	}
	(void) UpdateState(HFPD_SIO_STOPPED, reason);
}

bool SoundIoObj::
EpAudioGateway(AudioGateway *agp, bool can_connect, ErrorInfo *error)
{
	if (m_state != HFPD_SIO_STOPPED)
		EpRelease();
	assert(m_state == HFPD_SIO_STOPPED);
	EpRelease();		/* Call again to run the assertions */
	agp->Get();
	assert(!m_bound_ag);
	m_bound_ag = agp;

	if (m_bound_ag->AudioState() == HFPD_AG_AUDIO_DISCONNECTED) {
		if (!can_connect) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_BT,
					   LIBHFP_ERROR_BT_NOT_CONNECTED_SCO,
					   "Audio connection not established");
			goto fail;
		}
		if (!m_bound_ag->GetSoundIo()->SndOpen(true, true, error))
			goto fail;
	}

	assert(m_bound_ag->AudioState() != HFPD_AG_AUDIO_DISCONNECTED);
	assert(!agp->m_audio_bind);
	agp->m_audio_bind = this;
	return EpAudioGatewayComplete(agp, error);

fail:
	m_bound_ag = 0;
	agp->Put();
	return false;
}

bool SoundIoObj::
EpAudioGatewayComplete(AudioGateway *agp, ErrorInfo *error)
{
	ErrorInfo local_error, *throwme;
	bool res;

	/*
	 * If the caller is interested in why we failed, we collect it
	 * for them and pass it back to them.
	 *
	 * Otherwise, we collect the error locally, and pass it to
	 * EpRelease(), which sends it out via the StreamAborted signal.
	 */
	throwme = 0;
	if (!error) {
		error = &local_error;
		throwme = &local_error;
	}

	AudioGatewayAudioState st;
	assert(agp == m_bound_ag);
	st = agp->AudioState();

	assert(agp->m_audio_bind == this);
	switch (st) {
	case HFPD_AG_AUDIO_DISCONNECTED:
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_BT,
				   LIBHFP_ERROR_BT_NOT_CONNECTED_SCO,
				   "Audio connection not established");
		agp->m_audio_bind = 0;
		EpRelease();
		return false;
	case HFPD_AG_AUDIO_CONNECTING:
		(void) UpdateState(HFPD_SIO_AUDIOGATEWAY_CONNECTING);
		agp->UpdateAudioState(st);
		return true;
	case HFPD_AG_AUDIO_CONNECTED:
		agp->m_audio_bind = 0;
		break;
	default:
		abort();
	}

	assert((m_state == HFPD_SIO_AUDIOGATEWAY_CONNECTING) ||
	       (m_state == HFPD_SIO_STOPPED));

	res = m_sound->SetSecondary(agp->GetSoundIo());
	assert(res);
	if (!m_sound->Start(false, false, error)) {
		GetDi()->LogWarn("Could not start stream");
		EpRelease(HFPD_SIO_AUDIOGATEWAY, throwme);
		return false;
	}

	(void) UpdateState(HFPD_SIO_AUDIOGATEWAY);
	return true;
}

bool SoundIoObj::
EpFile(const char *filename, bool writing, libhfp::ErrorInfo *error)
{
	static const SoundIoFormat file_out_fmt = {
		SIO_PCM_S16_LE,
		8000,
		0,
		1,
		2
	};

	SoundIoFormat xfmt;
	SoundIo *ep;
	bool res;

	ep = SoundIoCreateFileHandler(GetDi(), filename, writing, error);
	if (!ep)
		return false;

	if (writing) {
		memcpy(&xfmt, &file_out_fmt, sizeof(xfmt));
		if (!ep->SndSetFormat(xfmt, error)) {
			delete ep;
			return false;
		}
	}

	if (!ep->SndOpen(writing, !writing, error)) {
		delete ep;
		return false;
	}

	if (m_state != HFPD_SIO_STOPPED)
		EpRelease();
	assert(m_state == HFPD_SIO_STOPPED);
	EpRelease();		/* Call again to run the assertions */

	res = m_sound->SetDspEnabled(false);
	assert(res);
	res = m_sound->SetSecondary(ep);
	assert(res);
	if (!m_sound->Start(false, false, error)) {
		GetDi()->LogWarn("Could not start stream in file %s mode",
				 writing ? "capture" : "playback");
		EpRelease(HFPD_SIO_FILE);
		return false;
	}
	(void) UpdateState(HFPD_SIO_FILE);
	return true;
}

bool SoundIoObj::
EpLoopback(ErrorInfo *error)
{
	if (m_state != HFPD_SIO_STOPPED)
		EpRelease();
	assert(m_state == HFPD_SIO_STOPPED);
	EpRelease();		/* Call again to run the assertions */
	if (!m_sound->Loopback(error)) {
		GetDi()->LogWarn("Could not configure loopback mode");
		return false;
	}
	if (!m_sound->Start(false, false, error)) {
		GetDi()->LogWarn("Could not start stream in loopback mode");
		return false;
	}
	(void) UpdateState(HFPD_SIO_LOOPBACK);
	return true;
}

bool SoundIoObj::
EpMembuf(bool in, bool out, SoundIoFilter *fltp, ErrorInfo *error)
{
	bool res;

	if (m_state != HFPD_SIO_STOPPED)
		EpRelease();
	assert(m_membuf);
	assert(m_state == HFPD_SIO_STOPPED);
	EpRelease();		/* Call again to run the assertions */
	res = m_sound->SetSecondary(m_membuf, error);
	assert(res);
	if (!m_membuf->SndOpen(in, out, error))
		return false;
	if (fltp) {
		res = m_sound->AddTop(fltp, error);
		assert(res);
	}
	if (!m_sound->Start(false, false, error)) {
		GetDi()->LogWarn("Could not start stream with membuf");
		if (fltp) {
			m_sound->RemoveTop();
			delete fltp;
		}
		m_sound->SetSecondary(0);
		m_membuf->SndClose();
		return false;
	}
	(void) UpdateState(HFPD_SIO_MEMBUF);
	return true;
}


bool SoundIoObj::
SetDriver(DBusMessage *msgp)
{
	DBusMessageIter mi;
	char *driver, *driveropts, *old_driver, *old_opts;
	ErrorInfo error;
	bool res;

	if (m_sound->IsStarted()) {
		return SendReplyError(msgp,
				      HFPD_ERROR_FAILED,
				      "Cannot change driver while streaming");
	}

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &driver);
	res = dbus_message_iter_next(&mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &driveropts);

	old_driver = (char *) m_sound->GetDriverName();
	if (old_driver && !old_driver[0])
		old_driver = 0;
	if (old_driver) {
		old_driver = strdup(old_driver);
		if (!old_driver)
			return false;
	}

	old_opts = (char *) m_sound->GetDriverOpts();
	if (old_opts && !old_opts[0])
		old_opts = 0;
	if (old_opts) {
		old_opts = strdup(old_opts);
		if (!old_opts) {
			if (old_driver)
				free(old_driver);
			return false;
		}
	}

	/*
	 * Set the driver options with the SoundIoManager
	 */
	if (!m_sound->SetDriver(driver, driveropts, &error)) {
		if (old_driver)
			free(old_driver);
		if (old_opts)
			free(old_opts);
		return SendReplyErrorInfo(msgp, error);
	}

	/*
	 * Save the driver options to the config file
	 */
	if (!m_config->Set("audio", "driver", driver, &error) ||
	    !m_config->Set("audio", "driveropts", driveropts, &error) ||
	    !SaveConfig(&error)) {
		(void) m_sound->SetDriver(old_driver, old_opts);
		if (old_driver)
			free(old_driver);
		if (old_opts)
			free(old_opts);
		return SendReplyErrorInfo(msgp, error);
	}

	res = SendReplyArgs(msgp, DBUS_TYPE_INVALID);
	if (!res)
		(void) m_sound->SetDriver(old_driver, old_opts);

	if (old_driver)
		free(old_driver);
	if (old_opts)
		free(old_opts);
	return res;
}

bool SoundIoObj::
ProbeDevices(DBusMessage *msgp)
{
	DBusMessageIter mi, ami, smi;
	DBusMessage *replyp = 0;
	SoundIoDeviceList *devlist = 0;
	const char *driver, *name, *desc;
	int i;
	bool res;
	ErrorInfo error;

	/*
	 * The main reason to disallow this is to avoid operations
	 * with long latencies.  Even if there are such operations,
	 * it's going to be part of an interactive process, and the
	 * user will understand if the sound skips.
	if (m_sound->IsStarted()) {
		return SendReplyError(msgp,
				      HFPD_ERROR_FAILED,
				      "Refusing request to probe devices "
				      "while streaming");
	}
	 */

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &driver);

	for (i = 0; m_sound->GetDriverInfo(i, &name, 0, 0); i++) {
		if (!strcasecmp(name, driver))
			break;
		name = 0;
	}

	if (!name) {
		return SendReplyError(msgp,
				      HFPD_ERROR_FAILED,
				      "Unknown driver \"%s\"", driver);
	}

	if (!m_sound->GetDriverInfo(i, 0, 0, &devlist, &error)) {
		assert(!error.Matches(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_NO_DRIVER));
		return SendReplyErrorInfo(msgp, error);
	}

	replyp = NewMethodReturn(msgp);
	if (!replyp)
		goto failed;

	dbus_message_iter_init_append(replyp, &mi);
	if (!dbus_message_iter_open_container(&mi,
					      DBUS_TYPE_ARRAY,
					      DBUS_STRUCT_BEGIN_CHAR_AS_STRING
					      DBUS_TYPE_STRING_AS_STRING
					      DBUS_TYPE_STRING_AS_STRING
					      DBUS_STRUCT_END_CHAR_AS_STRING,
					      &ami))
		goto failed;

	if (devlist->First()) do {
		name = devlist->GetName();
		desc = devlist->GetDesc();

		if (!dbus_message_iter_open_container(&ami,
						      DBUS_TYPE_STRUCT,
						      0,
						      &smi) ||
		    !dbus_message_iter_append_basic(&smi,
						    DBUS_TYPE_STRING,
						    &name) ||
		    !dbus_message_iter_append_basic(&smi,
						    DBUS_TYPE_STRING,
						    &desc) ||
		    !dbus_message_iter_close_container(&ami, &smi)) {
			goto failed;
		}

	} while (devlist->Next());

	delete devlist;
	devlist = 0;

	if (!dbus_message_iter_close_container(&mi, &ami) ||
	    !SendMessage(replyp))
		goto failed;

	return true;

failed:
	if (replyp)
		dbus_message_unref(replyp);
	if (devlist)
		delete devlist;
	return false;
}

bool SoundIoObj::
Stop(DBusMessage *msgp)
{
	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID))
		return false;

	EpRelease();
	return true;
}

bool SoundIoObj::
AudioGatewayStart(DBusMessage *msgp)
{
	DBusMessageIter mi;
	char *agpath;
	AudioGateway *agp;
	dbus_bool_t can_connect;
	bool res;
	ErrorInfo error;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_OBJECT_PATH);
	dbus_message_iter_get_basic(&mi, &agpath);
	res = dbus_message_iter_next(&mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_BOOLEAN);
	dbus_message_iter_get_basic(&mi, &can_connect);

	agp = m_hf->FindAudioGateway(agpath);
	if (!agp) {
		return SendReplyError(msgp,
				      HFPD_ERROR_FAILED,
				      "Audio Gateway Path Invalid");
	}

	GetDi()->LogDebug("AudioGatewayStart: %s", agpath);

	if (m_bound_ag == agp) {
		/*
		 * Ignore re-requests to bind to the same audiogateway
		 */
		assert((m_state == HFPD_SIO_AUDIOGATEWAY) ||
		       (m_state == HFPD_SIO_AUDIOGATEWAY_CONNECTING));
		return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
	}

	if (!EpAudioGateway(agp, can_connect, &error))
		return SendReplyErrorInfo(msgp, error);

	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID)) {
		EpRelease();
		return false;
	}

	return true;
}

bool SoundIoObj::
FileStart(DBusMessage *msgp)
{
	DBusMessageIter mi;
	char *filename;
	dbus_bool_t for_write;
	ErrorInfo error;
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &filename);
	res = dbus_message_iter_next(&mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_BOOLEAN);
	dbus_message_iter_get_basic(&mi, &for_write);

	if (!EpFile(filename, for_write, &error))
		return SendReplyErrorInfo(msgp, error);

	if (!SetupStateOwner(msgp) ||
	    !SendReplyArgs(msgp, DBUS_TYPE_INVALID)) {
		EpRelease();
		return false;
	}

	return true;
}

bool SoundIoObj::
LoopbackStart(DBusMessage *msgp)
{
	ErrorInfo error;

	if (!EpLoopback(&error))
		return SendReplyErrorInfo(msgp, error);

	if (!SetupStateOwner(msgp) ||
	    !SendReplyArgs(msgp, DBUS_TYPE_INVALID)) {
		EpRelease();
		return false;
	}

	return true;
}

bool SoundIoObj::
MembufClear(DBusMessage *msgp)
{
	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID))
		return false;

	if (m_state == HFPD_SIO_MEMBUF)
		EpRelease();

	if (m_membuf) {
		delete m_membuf;
		m_membuf = 0;
		m_membuf_size = 0;
	}

	return true;
}

bool SoundIoObj::
MembufStart(DBusMessage *msgp)
{
	static const SoundIoFormat mb_fmt = {
		SIO_PCM_S16_LE,
		8000,
		0,
		1,
		2
	};

	MicVolumeFilter *fltp;
	DBusMessageIter mi;
	dbus_bool_t in, out;
	dbus_uint32_t npackets, interval;
	ErrorInfo error;
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_BOOLEAN);
	dbus_message_iter_get_basic(&mi, &in);
	res = dbus_message_iter_next(&mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_BOOLEAN);
	dbus_message_iter_get_basic(&mi, &out);
	res = dbus_message_iter_next(&mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_UINT32);
	dbus_message_iter_get_basic(&mi, &npackets);
	res = dbus_message_iter_next(&mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_UINT32);
	dbus_message_iter_get_basic(&mi, &interval);

	fltp = 0;
	if (interval) {
		fltp = new MicVolumeFilter(this, interval);
		if (!fltp)
			return false;
	}

	if (!m_membuf || (m_membuf_size != npackets)) {
		if (m_membuf)
			delete m_membuf;
		m_membuf = SoundIoCreateMembuf(&mb_fmt, npackets);
		if (!m_membuf) {
			if (fltp)
				delete fltp;
			return false;
		}

		m_membuf_size = npackets;
	}

	if (!EpMembuf(in, out, fltp, &error))
		return SendReplyErrorInfo(msgp, error);

	if (!SetupStateOwner(msgp) ||
	    !SendReplyArgs(msgp, DBUS_TYPE_INVALID)) {
		EpRelease();
		return false;
	}

	return true;
}

bool SoundIoObj::
SetSnoopFile(DBusMessage *msgp)
{
	DBusMessageIter mi;
	char *filename, *fncopy;
	dbus_bool_t in, out;
	ErrorInfo error;
	SoundIo *ep;
	SoundIoFilter *fltp;
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &filename);
	res = dbus_message_iter_next(&mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_BOOLEAN);
	dbus_message_iter_get_basic(&mi, &in);
	res = dbus_message_iter_next(&mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_BOOLEAN);
	dbus_message_iter_get_basic(&mi, &out);

	if (!filename || !filename[0]) {
		if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID))
			return false;
		CleanupSnoop();
		return true;
	}

	fncopy = strdup(filename);
	if (!fncopy)
		return false;

	ep = SoundIoCreateFileHandler(GetDi(), fncopy, true, &error);
	if (!ep) {
		free(fncopy);
		return SendReplyErrorInfo(msgp, error);
	}

	fltp = SoundIoCreateSnooper(ep, in, out);
	if (!fltp) {
		delete ep;
		free(fncopy);
		return false;
	}

	if (!m_sound->AddBottom(fltp, &error)) {
		delete fltp;
		delete ep;
		free(fncopy);
		return SendReplyErrorInfo(msgp, error);
	}

	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID)) {
		m_sound->RemoveFilter(fltp);
		delete fltp;
		delete ep;
		free(fncopy);
		return false;
	}

	CleanupSnoop();
	m_snoop = fltp;
	m_snoop_ep = ep;
	m_snoop_filename = fncopy;
	return true;
}


bool SoundIoObj::
GetState(DBusMessage *msgp, uint8_t &val)
{
	val = m_state;
	return true;
}

bool SoundIoObj::
GetAudioGateway(DBusMessage *msgp, const DbusProperty *propp,
		DBusMessageIter &mi)
{
	DBusMessageIter vmi;
	dbus_bool_t fval;
	const char *name, *sig;

	fval = false;
	sig = m_bound_ag ? "o" : "b";

	if (!dbus_message_iter_open_container(&mi,
					      DBUS_TYPE_VARIANT,
					      sig,
					      &vmi))
		return false;

	if (m_bound_ag) {
		name = m_bound_ag->GetDbusPath();
		if (!dbus_message_iter_append_basic(&vmi,
						    DBUS_TYPE_OBJECT_PATH,
						    &name))
			return false;
	} else {
		if (!dbus_message_iter_append_basic(&vmi,
						    DBUS_TYPE_BOOLEAN,
						    &fval))
			return false;
	}

	return dbus_message_iter_close_container(&mi, &vmi);
}

bool SoundIoObj::
GetMute(DBusMessage *msgp, bool &val)
{
	val = m_sound->GetMute();
	return true;
}

bool SoundIoObj::
SetMute(DBusMessage *msgp, const bool &val, bool &doreply)
{
	dbus_bool_t st;
	bool oldstate = m_sound->GetMute();
	if (oldstate == val)
		return true;
	if (!m_sound->SetMute(val))
		return false;

	st = val;
	(void) SendSignalArgs(HFPD_SOUNDIO_INTERFACE_NAME,
			      "MuteChanged",
			      DBUS_TYPE_BOOLEAN, &st,
			      DBUS_TYPE_INVALID);
	return true;
}

bool SoundIoObj::
GetSnoopFileName(DBusMessage *msgp, const char * &val)
{
	val = m_snoop_filename;
	if (!val)
		val = "";
	return true;
}

bool SoundIoObj::
GetDriverName(DBusMessage *msgp, const char * &val)
{
	val = m_sound->GetDriverName();
	if (!val)
		val = "";
	return true;
}

bool SoundIoObj::
GetDriverOpts(DBusMessage *msgp, const char * &val)
{
	val = m_sound->GetDriverOpts();
	if (!val)
		val = "";
	return true;
}

bool SoundIoObj::
GetDrivers(DBusMessage *msgp, const DbusProperty *propp,
		DBusMessageIter &mi)
{
	DBusMessageIter ami, smi;
	const char *name, *desc;
	int i;

	if (!dbus_message_iter_open_container(&mi,
					      DBUS_TYPE_ARRAY,
					      DBUS_STRUCT_BEGIN_CHAR_AS_STRING
					      DBUS_TYPE_STRING_AS_STRING
					      DBUS_TYPE_STRING_AS_STRING
					      DBUS_STRUCT_END_CHAR_AS_STRING,
					      &ami))
		return false;

	for (i = 0; m_sound->GetDriverInfo(i, &name, &desc, 0); i++) {
		if (!dbus_message_iter_open_container(&ami,
						      DBUS_TYPE_STRUCT,
						      0,
						      &smi) ||
		    !dbus_message_iter_append_basic(&smi,
						    DBUS_TYPE_STRING,
						    &name) ||
		    !dbus_message_iter_append_basic(&smi,
						    DBUS_TYPE_STRING,
						    &desc) ||
		    !dbus_message_iter_close_container(&ami, &smi))
			return false;
	}

	if (!dbus_message_iter_close_container(&mi, &ami))
		return false;

	return true;
}

bool SoundIoObj::
GetPacketInterval(DBusMessage *msgp, dbus_uint32_t &val)
{

	val = m_sound->GetPacketInterval();
	return true;
}

bool SoundIoObj::
GetMinBufferFill(DBusMessage *msgp, dbus_uint32_t &val)
{
	val = m_sound->GetMinBufferFill();
	return true;
}

bool SoundIoObj::
GetJitterWindow(DBusMessage *msgp, dbus_uint32_t &val)
{
	val = m_sound->GetJitterWindow();
	return true;
}

bool SoundIoObj::
GetPacketIntervalHint(DBusMessage *msgp, dbus_uint32_t &val)
{
	val = m_sound->GetPacketIntervalHint();
	return true;
}

bool SoundIoObj::
SetPacketIntervalHint(DBusMessage *msgp, const dbus_uint32_t &val,
		      bool &doreply)
{
	ErrorInfo error;

	if (!m_config->Set("audio", "packetinterval", val, &error) ||
	    !SaveConfig(&error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	m_sound->SetPacketIntervalHint(val);
	return true;
}

bool SoundIoObj::
GetMinBufferFillHint(DBusMessage *msgp, dbus_uint32_t &val)
{
	val = m_sound->GetMinBufferFillHint();
	return true;
}

bool SoundIoObj::
SetMinBufferFillHint(DBusMessage *msgp, const dbus_uint32_t &val,
		      bool &doreply)
{
	ErrorInfo error;

	if (!m_config->Set("audio", "minbufferfill", val, &error) ||
	    !SaveConfig(&error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	m_sound->SetMinBufferFillHint(val);
	return true;
}

bool SoundIoObj::
GetJitterWindowHint(DBusMessage *msgp, dbus_uint32_t &val)
{
	val = m_sound->GetJitterWindowHint();
	return true;
}

bool SoundIoObj::
SetJitterWindowHint(DBusMessage *msgp, const dbus_uint32_t &val,
			 bool &doreply)
{
	ErrorInfo error;

	if (!m_config->Set("audio", "jitterwindow", val, &error) ||
	    !SaveConfig(&error)) {
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	m_sound->SetJitterWindowHint(val);
	return true;
}


bool SoundIoObj::
GetDenoise(DBusMessage *msgp, bool &val)
{
	val = m_procprops.noisereduce;
	return true;
}

bool SoundIoObj::
SetDenoise(DBusMessage *msgp, const bool &val, bool &doreply)
{
	ErrorInfo error;
	SoundIoSpeexProps save = m_procprops;

	m_procprops.noisereduce = val;
	if (!m_sigproc->Configure(m_procprops, &error)) {
		m_procprops = save;
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	if (!m_config->Set("dsp", "denoise", val, &error) ||
	    !SaveConfig(&error)) {
		m_procprops = save;
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	return true;
}

bool SoundIoObj::
GetAutoGain(DBusMessage *msgp, dbus_uint32_t &val)
{
	val = m_procprops.agc_level;
	return true;
}

bool SoundIoObj::
SetAutoGain(DBusMessage *msgp, const dbus_uint32_t &val, bool &doreply)
{
	ErrorInfo error;
	SoundIoSpeexProps save = m_procprops;
	m_procprops.agc_level = val;
	if (!m_sigproc->Configure(m_procprops, &error)) {
		m_procprops = save;
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	if (!m_config->Set("dsp", "autogain", val, &error) ||
	    !SaveConfig(&error)) {
		m_procprops = save;
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	return true;
}

bool SoundIoObj::
GetEchoCancelTail(DBusMessage *msgp, dbus_uint32_t &val)
{
	val = m_procprops.echocancel_ms;
	return true;
}

bool SoundIoObj::
SetEchoCancelTail(DBusMessage *msgp, const dbus_uint32_t &val, bool &doreply)
{
	ErrorInfo error;
	SoundIoSpeexProps save = m_procprops;
	m_procprops.echocancel_ms = val;
	if (!m_sigproc->Configure(m_procprops, &error)) {
		m_procprops = save;
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	if (!m_config->Set("dsp", "echocancel_ms", val, &error) ||
	    !SaveConfig(&error)) {
		m_procprops = save;
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	return true;
}

bool SoundIoObj::
GetDereverbLevel(DBusMessage *msgp, float &val)
{
	val = m_procprops.dereverb_level;
	return true;
}

bool SoundIoObj::
SetDereverbLevel(DBusMessage *msgp, const float &val, bool &doreply)
{
	ErrorInfo error;
	SoundIoSpeexProps save = m_procprops;
	m_procprops.dereverb_level = val;
	if (!m_sigproc->Configure(m_procprops, &error)) {
		m_procprops = save;
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	if (!m_config->Set("dsp", "dereverb_level", val, &error) ||
	    !SaveConfig(&error)) {
		m_procprops = save;
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	return true;
}

bool SoundIoObj::
GetDereverbDecay(DBusMessage *msgp, float &val)
{
	val = m_procprops.dereverb_decay;
	return true;
}

bool SoundIoObj::
SetDereverbDecay(DBusMessage *msgp, const float &val, bool &doreply)
{
	ErrorInfo error;
	SoundIoSpeexProps save = m_procprops;
	m_procprops.dereverb_decay = val;
	if (!m_sigproc->Configure(m_procprops, &error)) {
		m_procprops = save;
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	if (!m_config->Set("dsp", "dereverb_decay", val, &error) ||
	    !SaveConfig(&error)) {
		m_procprops = save;
		doreply = false;
		return SendReplyErrorInfo(msgp, error);
	}
	return true;
}
