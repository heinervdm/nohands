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

	bool SaveConfig(bool force = false) {
		if (!force && !m_config_autosave) {
			m_config_dirty = true;
			return true;
		}
		if (!Save(m_config_savefile, 2))
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
					 "specified config file \"%s\"\n",
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
	bool SetConfigFile(const char *val) {
		char *oldval = m_config_savefile;

		m_config_savefile = strdup(val);
		if (!m_config_savefile) {
			m_config_savefile = oldval;
			return false;
		}

		if (!SaveConfig()) {
			if (m_config_savefile)
				free(m_config_savefile);
			m_config_savefile = oldval;
		}

		if (oldval)
			free(oldval);
		return true;
	}
};


AudioGateway::
AudioGateway(HandsFree *hfp, HfpSession *sessp, char *name)
	: DbusExportObject(name, s_ifaces), m_sess(sessp),
	  m_name_free(name), m_known(false), 
	  m_unbind_on_voice_close(false), m_hf(hfp), m_owner(0),
	  m_voice_bind(0) {

	/*
	 * Attach ourselves to the HfpSession
	 */
	assert(sessp && sessp->GetDevice());
	assert(!sessp->GetPrivate());
	sessp->SetPrivate(this);

	sessp->cb_NotifyConnection.Register(this,
					&AudioGateway::NotifyConnection);
	sessp->cb_NotifyVoiceConnection.Register(this,
					&AudioGateway::NotifyVoiceConnection);
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
	char buf[32];

	assert(notp == m_owner);
	m_sess->GetDevice()->GetAddr(buf);
	GetDi()->LogInfo("AG %s: D-Bus owner disconnected\n", buf);
	delete notp;
	m_owner = 0;
	if (!m_known)
		m_sess->SetAutoReconnect(false);
	if (!m_known && (State() > HFPD_AG_DISCONNECTED)) {
		if ((VoiceState() == HFPD_AG_VOICE_CONNECTED) &&
		    m_hf->m_voice_persist) {
			/* Defer the disconnect until voice closes */
			m_unbind_on_voice_close = true;
		} else {
			m_sess->Disconnect();
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

AudioGatewayVoiceState AudioGateway::
VoiceState(void)
{
	if (m_sess->IsConnectedVoice())
		return HFPD_AG_VOICE_CONNECTED;

	if (m_sess->IsConnectingVoice())
		return HFPD_AG_VOICE_CONNECTING;

	return HFPD_AG_VOICE_DISCONNECTED;
}

bool AudioGateway::
UpdateState(AudioGatewayState st)
{
	const unsigned char state = (char) st;
	if ((st != m_state) &&
	    !SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
			    "StateChanged",
			    DBUS_TYPE_BYTE, &state,
			    DBUS_TYPE_INVALID))
		return false;

	m_state = st;
	return true;
}

bool AudioGateway::
UpdateCallState(AudioGatewayCallState st)
{
	const unsigned char state = (char) st;
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
UpdateVoiceState(AudioGatewayVoiceState st)
{
	const unsigned char state = (char) st;
	if ((st != m_voice_state) &&
	    !SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
			    "VoiceStateChanged",
			    DBUS_TYPE_BYTE, &state,
			    DBUS_TYPE_INVALID))
		return false;

	m_voice_state = st;
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

void AudioGateway::
DoDisconnect(void)
{
	char buf[32];
	assert(VoiceState() == HFPD_AG_VOICE_DISCONNECTED);
	m_unbind_on_voice_close = false;
	NotifyVoiceConnection(m_sess);

	m_sess->GetDevice()->GetAddr(buf);
	GetDi()->LogInfo("AG %s: Disconnected\n", buf);
	UpdateState(State());
}

void AudioGateway::
NotifyConnection(libhfp::HfpSession *sessp)
{
	AudioGatewayState st;
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

	if (st == HFPD_AG_DISCONNECTED)
		DoDisconnect();

	else if (st == HFPD_AG_CONNECTING) {
		/*
		 * Trigger a name lookup if one would be helpful.
		 * This is probably the best time to do it for
		 * devices-to-be-connected-to, causing the least
		 * impact on HCI task queues.
		 */
		if (!sessp->GetDevice()->IsNameResolved())
			(void) sessp->GetDevice()->ResolveName();
	}

	if (st == HFPD_AG_CONNECTED) {
		char buf[32];
		m_sess->GetDevice()->GetAddr(buf);
		GetDi()->LogInfo("AG %s: Connected\n", buf);
	}

	UpdateState(st);
}

void AudioGateway::
NotifyCall(libhfp::HfpSession *sessp, bool act, bool waiting, bool ring)
{
	if (act || waiting)
		UpdateCallState(CallState());

	if (ring) {
		const char *clip = sessp->WaitingCallIdentity();
		SendSignalArgs(HFPD_AUDIOGATEWAY_INTERFACE_NAME,
			       "Ring",
			       DBUS_TYPE_STRING, &clip,
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
NotifyVoiceConnection(libhfp::HfpSession *sessp)
{
	AudioGatewayVoiceState st;
	st = VoiceState();

	if (!m_owner && (!m_known || !m_hf->m_voice_autoconnect) &&
	    (st != HFPD_AG_VOICE_DISCONNECTED)) {
		/*
		 * Auto-refuse voice connections from unclaimed,
		 * unknown devices.
		 * Don't even report that the device is trying to connect.
		 * If they want a voice connection, they need to claim
		 * the device or mark it known.
		 */
		m_sess->SndClose();
		st = VoiceState();
	}

	UpdateVoiceState(st);

	if (m_voice_bind)
		m_voice_bind->EpAudioGatewayComplete(this);

	if (!m_owner && m_known && (st == HFPD_AG_VOICE_CONNECTED)) {
		/*
		 * The device is known and unclaimed.
		 * Make an effort to set up the audio pipe.
		 */
		if ((m_hf->m_sound->m_state != HFPD_SIO_STOPPED) ||
		    !m_hf->m_sound->EpAudioGateway(this)) {
			m_sess->SndClose();
			st = VoiceState();
		}
	}

	if (m_unbind_on_voice_close &&
	    (st == HFPD_AG_VOICE_DISCONNECTED)) {
		m_sess->Disconnect();
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
	if (!m_sess->Connect()) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Attempt to initiate connection failed");
	}

	UpdateState(State());

	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool AudioGateway::
Disconnect(DBusMessage *msgp)
{
	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID))
		return false;

	m_sess->Disconnect();
	DoDisconnect();
	return true;
}

bool AudioGateway::
Dial(DBusMessage *msgp)
{
	DBusMessageIter mi;
	const char *number;
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &number);

	if (number && !number[0])
		number = 0;

	if (!number) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Empty phone number specified");
	}

	if (!m_sess->CmdDial(number)) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Command could not be queued");
	}

	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool AudioGateway::
Redial(DBusMessage *msgp)
{
	if (!m_sess->CmdRedial()) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Command could not be queued");
	}

	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool AudioGateway::
HangUp(DBusMessage *msgp)
{
	if (!m_sess->CmdHangUp()) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Command could not be queued");
	}

	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool AudioGateway::
SendDtmf(DBusMessage *msgp)
{
	DBusMessageIter mi;
	char digit;
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_BYTE);
	dbus_message_iter_get_basic(&mi, &digit);

	if (!m_sess->CmdSendDtmf(digit)) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Command could not be queued");
	}

	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool AudioGateway::
GetState(DBusMessage *msgp, unsigned char &val)
{
	val = State();
	return true;
}

bool AudioGateway::
GetCallState(DBusMessage *msgp, unsigned char &val)
{
	val = CallState();
	return true;
}

bool AudioGateway::
GetVoiceState(DBusMessage *msgp, unsigned char &val)
{
	val = VoiceState();
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
	char addr[32];

	m_sess->GetDevice()->GetAddr(addr);
	if ((val && !m_hf->m_config->Set("devices", addr,
					 m_sess->IsAutoReconnect())) ||
	    (!val && !m_hf->m_config->Delete("devices", addr)) ||
	    !m_hf->SaveConfig()) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	char addr[32];

	if (val && !m_owner && !m_known) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Device not known or claimed");
	}

	if (m_known) {
		m_sess->GetDevice()->GetAddr(addr);
		if (!m_hf->m_config->Set("devices", addr, val) ||
		    !m_hf->SaveConfig()) {
			doreply = false;
			return SendReplyError(msgp,
					      DBUS_ERROR_FAILED,
					      "Could not save configuration");
		}
	}

	m_sess->SetAutoReconnect(val);
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
	: DbusExportObject(HFPD_HANDSFREE_OBJECT, s_ifaces),
	  m_di(dip), m_dbus(dbusp), m_hub(0), m_hfp(0),
	  m_sound(0),
	  m_accept_unknown(false), m_voice_persist(false),
	  m_voice_autoconnect(false)
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
SaveConfig(bool force)
{
	return m_config->SaveConfig(force);
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
	m_config->Get("daemon", "voicepersist", m_voice_persist, false);
	m_config->Get("daemon", "voiceautoconnect", m_voice_autoconnect,false);

	if (m_config->FirstInSection(it, "devices")) {
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
	}
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

	sessp = m_hfp->DefaultSessionFactory(devp);
	if (!sessp)
		goto failed;

	devp->GetAddr(bda);
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
	if (!agp)
		goto failed;

	if (!m_dbus->ExportObject(agp))
		goto failed;

	m_gateways.AppendItem(agp->m_links);

	/* Announce our presence */
	agp->NotifyConnection(sessp);

	(void) SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			      "AudioGatewayAdded",
			      DBUS_TYPE_OBJECT_PATH, &path,
			      DBUS_TYPE_INVALID);

	return sessp;

failed:
	if (agp)
		delete agp;
	if (path)
		free(path);
	if (sessp)
		delete sessp;
	return 0;
}


struct NameResolveRequest {
	ListItem	links;
	DBusMessage	*req;
	BtDevice	*dev;

	void Complete(DbusExportObject *objp, const char *name) {
		assert(req);
		assert(dev);
		objp->SendReplyArgs(req,
				    DBUS_TYPE_STRING, &name,
				    DBUS_TYPE_INVALID);
		dbus_message_unref(req);
		req = 0;
	}

	void Failed(DbusExportObject *objp, const char *reason) {
		assert(req);
		assert(dev);
		objp->SendReplyError(req,
				     DBUS_ERROR_FAILED,
				     reason);
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

	GetDi()->LogInfo("Bluetooth System Shut Down\n");

	res = false;
	(void) SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			      "SystemStateChanged",
			      DBUS_TYPE_BOOLEAN, &res,
			      DBUS_TYPE_INVALID);

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
			reqp->Failed(this, "Bluetooth system shut down");
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

	GetDi()->LogInfo("Bluetooth System Started\n");

	(void) SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			      "SystemStateChanged",
			      DBUS_TYPE_BOOLEAN, &a,
			      DBUS_TYPE_INVALID);

	if (m_hub->GetDeviceClassLocal(devclass) &&
	    !m_hfp->IsDeviceClassHf(devclass)) {
		m_hfp->SetDeviceClassHf(devclass);
		GetDi()->LogWarn("*** Your configured device class may "
				 "not be recognized as a hands-free\n");
		GetDi()->LogWarn("*** Edit /etc/bluetooth/hcid.conf "
				 "and change:\n");
		GetDi()->LogWarn("*** class 0x%06x;\n", devclass);
	}
}

void HandsFree::
NotifySystemState(void)
{
	if (!m_hub->IsStarted()) {
		DoStopped();
		return;
	}

	DoStarted();
}

void HandsFree::
NotifyInquiryResult(BtDevice *devp, int error)
{
	char buf[32];
	dbus_uint32_t dclass;
	char *cp;

	if (!devp) {
		dbus_bool_t st = false;
		SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			       "InquiryStateChanged",
			       DBUS_TYPE_BOOLEAN, &st,
			       DBUS_TYPE_INVALID);
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
NotifyNameResolved(BtDevice *devp, const char *name)
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
		if (name)
			reqp->Complete(this, name);
		else
			reqp->Failed(this, "Name resolution failure");
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
	if (!SaveConfig(true)) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save config file");
	}

	return SendReplyArgs(msgp, DBUS_TYPE_INVALID);
}

bool HandsFree::
Start(DBusMessage *msgp)
{
	bool oldstate;
	dbus_bool_t res;

	oldstate = m_hub->IsStarted();
	res = m_hub->Start();

	if (!res) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not start Bluetooth system");
	}

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
	int res;

	res = m_hub->StartInquiry();

	if (res)
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not start inquiry: %s",
				      strerror(-res));

	st = true;

	if (!SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			    "InquiryStateChanged",
			    DBUS_TYPE_BOOLEAN, &st,
			    DBUS_TYPE_INVALID) ||
	    !SendReplyArgs(msgp, DBUS_TYPE_INVALID))
		return false;

	return true;
}

bool HandsFree::
StopInquiry(DBusMessage *msgp)
{
	dbus_bool_t st;
	int res;

	res = m_hub->StopInquiry();

	if (res)
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not stop inquiry: %s",
				      strerror(-res));

	st = false;
	if (!SendSignalArgs(HFPD_HANDSFREE_INTERFACE_NAME,
			    "InquiryStateChanged",
			    DBUS_TYPE_BOOLEAN, &st,
			    DBUS_TYPE_INVALID) ||
	    !SendReplyArgs(msgp, DBUS_TYPE_INVALID))
		return false;

	return true;
}

bool HandsFree::
GetName(DBusMessage *msgp)
{
	NameResolveRequest *nrrp, *prevp;
	DBusMessageIter mi;
	BtDevice *devp;
	const char *addr;
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

	if (!devp->ResolveName()) {
		res = SendReplyError(msgp,
				     DBUS_ERROR_FAILED,
				     "Could not start name resolution");
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
	bool remove_dn = false, unsetknown = false, res;
	dbus_bool_t setknown;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &addr);
	res = dbus_message_iter_next(&mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_BOOLEAN);
	dbus_message_iter_get_basic(&mi, &setknown);
	sessp = m_hfp->GetSession(addr);
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
				     DBUS_ERROR_FAILED,
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

		GetDi()->LogInfo("AG %s: claimed by D-Bus peer %s\n",
				 addr, peerp->GetName());
	}

	if (setknown && !agp->m_known) {
		if (!m_config->Set("devices", addr,
				   agp->m_sess->IsAutoReconnect()) ||
		    !SaveConfig()) {
			res = SendReplyError(msgp,
					     DBUS_ERROR_FAILED,
					     "Could not save configuration");
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
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_STRING);
	dbus_message_iter_get_basic(&mi, &addr);
	sessp = m_hfp->GetSession(addr, false);
	if (!sessp) {
		res = SendReplyError(msgp,
				     DBUS_ERROR_FAILED,
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
				     DBUS_ERROR_FAILED,
				     "No such audio gateway");
		goto done;
	}

	if (agp->m_owner && (agp->m_owner->GetPeer() != peerp)) {
		res = SendReplyError(msgp,
				     DBUS_ERROR_FAILED,
				     "This audio gateway has been claimed by "
				     "another client");
		goto done;
	}

	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID)) {
		res = false;
		goto done;
	}

	if (!agp->m_known) {
		sessp->SetAutoReconnect(false);
		sessp->Disconnect();
		agp->DoDisconnect();
	}

	if (agp->m_owner) {
		GetDi()->LogInfo("AG %s: disowned by D-Bus peer %s\n",
				 addr, agp->m_owner->GetPeer()->GetName());
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
GetAutoSave(DBusMessage *msgp, bool &val)
{
	val = m_config->GetAutoSave();
	return true;
}

bool HandsFree::
SetAutoSave(DBusMessage *msgp, const bool &val, bool &doreply)
{
	if (m_config->GetAutoSave() == val)
		return true;

	if (!m_config->Set("daemon", "autosave", val) ||
	    !SaveConfig(val)) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	if (!strlen(val)) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Configuration file name is empty");
	}

	if (!m_config->SetConfigFile(val)) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not write new config file");
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
	if (val == m_hub->GetAutoRestart())
		return true;

	if (!m_config->Set("daemon", "autorestart", val) ||
	    !SaveConfig()) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
	}

	m_hub->SetAutoRestart(val);
	return true;
}

bool HandsFree::
GetSecMode(DBusMessage *msgp, unsigned char &val)
{
	val = m_hfp->GetSecMode();
	return true;
}

bool HandsFree::
SetSecMode(DBusMessage *msgp, const unsigned char &val, bool &doreply)
{
	if (m_hfp->GetSecMode() == (rfcomm_secmode_t) val)
		return true;

	if ((val != RFCOMM_SEC_NONE) &&
	    (val != RFCOMM_SEC_AUTH) &&
	    (val != RFCOMM_SEC_CRYPT)) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_INVALID_ARGS,
				      "Invalid secmode value specified");
	}

	if (!m_config->Set("daemon", "secmode", val) ||
	    !SaveConfig()) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
	}

	m_hfp->SetSecMode((rfcomm_secmode_t)val);
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
	if (m_accept_unknown == val)
		return true;

	if (!m_config->Set("daemon", "acceptunknown", val) ||
	    !SaveConfig()) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
	}

	m_accept_unknown = val;
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
	if (m_voice_persist == val)
		return true;

	if (!m_config->Set("daemon", "voicepersist", val) ||
	    !SaveConfig()) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	if (m_voice_autoconnect == val)
		return true;

	if (!m_config->Set("daemon", "voiceautoconnect", val) ||
	    !SaveConfig()) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	if (!m_hfp->SetServiceName(val)) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not set service name");
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
	if (!m_hfp->SetServiceDesc(val)) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not set service description");
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
				bool up, bool dn) {
		if ((fmt.sampletype != SIO_PCM_S16_LE) ||
		    (fmt.nchannels != 1))
			return false;
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
	: DbusExportObject(HFPD_SOUNDIO_OBJECT, s_ifaces),
	  m_hf(hfp), m_sound(0),
	  m_state(HFPD_SIO_DECONFIGURED), m_state_sent(HFPD_SIO_DECONFIGURED),
	  m_ringtone(0), m_sigproc(0),
	  m_membuf(0), m_membuf_size(0),
	  m_config(hfp->m_config)
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
NotifySoundStop(SoundIoManager *mgrp)
{
	GetDi()->LogInfo("Spontaneous audio stream halt\n");
	assert(mgrp == m_sound);
	EpRelease();
}

bool SoundIoObj::
SaveConfig(bool force)
{
	return m_config->SaveConfig(force);
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
				 "with options \"%s\"\n",
				 driver ? driver : "",
				 driveropts ? driveropts : "");
		goto failed;
	}

	m_config->Get("audio", "packetinterval", val, 0);
	m_sound->SetPacketIntervalHint(val);
	m_config->Get("audio", "minbufferfill", val, 0);
	m_sound->SetMinBufferFillHint(val);
	m_config->Get("audio", "jitterwindow", val, 0);
	m_sound->SetJitterWindowHint(val);

#if defined(USE_SPEEXDSP)
	m_sigproc = SoundIoFltCreateSpeex(GetDi());
	if (!m_sigproc) {
		GetDi()->LogWarn("Could not create DSP filter object\n");
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
		GetDi()->LogWarn("Could not configure DSP settings\n");
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

bool SoundIoObj::
UpdateState(SoundIoState st)
{
	const char *agpath;
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
		agpath = m_bound_ag->GetDbusPath();
		if (!SendSignalArgs(HFPD_SOUNDIO_INTERFACE_NAME,
				    "AudioGatewaySet",
				    DBUS_TYPE_OBJECT_PATH, &agpath,
				    DBUS_TYPE_INVALID))
			return false;
	}

	if (!SendSignalArgs(HFPD_SOUNDIO_INTERFACE_NAME,
			    "StateChanged",
			    DBUS_TYPE_BYTE, &stx,
			    DBUS_TYPE_INVALID))
		return false;

	m_state_sent = st;
	return true;
}

void SoundIoObj::
EpRelease(SoundIoState st)
{
	SoundIoFilter *fltp;

	if (st == HFPD_SIO_INVALID)
		st = m_state;

	switch (st) {
	case HFPD_SIO_STOPPED:
		assert(!m_sound->IsStarted());
		assert(!m_sound->GetSecondary());
		assert(!m_bound_ag);
		break;
	case HFPD_SIO_AUDIOGATEWAY:
		assert(m_bound_ag);
		assert(m_sound->GetSecondary() == m_bound_ag->GetSoundIo());
		m_sound->Stop();
		m_sound->SetSecondary(0);
		/* fall-thru */
	case HFPD_SIO_AUDIOGATEWAY_CONNECTING:
		assert(m_bound_ag);
		assert(!m_sound->GetSecondary());
		if (m_bound_ag->m_voice_bind == this)
			m_bound_ag->m_voice_bind = 0;
		m_bound_ag->GetSoundIo()->SndClose();
		m_bound_ag->NotifyVoiceConnection(0);
		m_bound_ag->Put();
		m_bound_ag = 0;
		break;
	case HFPD_SIO_LOOPBACK:
		assert(!m_sound->GetSecondary());
		m_sound->Stop();
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
		break;
	default:
		abort();
	}
	(void) UpdateState(HFPD_SIO_STOPPED);
}

bool SoundIoObj::
EpAudioGateway(AudioGateway *agp)
{
	if (m_state != HFPD_SIO_STOPPED)
		EpRelease();
	assert(m_state == HFPD_SIO_STOPPED);
	EpRelease();		/* Call again to run the assertions */
	agp->Get();
	assert(!m_bound_ag);
	m_bound_ag = agp;

	if ((m_bound_ag->VoiceState() == HFPD_AG_VOICE_DISCONNECTED) &&
	    !m_bound_ag->GetSoundIo()->SndOpen(true, true)) {
		m_bound_ag = 0;
		agp->Put();
		return false;
	}

	assert(m_bound_ag->VoiceState() != HFPD_AG_VOICE_DISCONNECTED);
	assert(!agp->m_voice_bind);
	agp->m_voice_bind = this;
	return EpAudioGatewayComplete(agp);
}

bool SoundIoObj::
EpAudioGatewayComplete(AudioGateway *agp)
{
	bool res;

	AudioGatewayVoiceState st;
	assert(agp == m_bound_ag);
	st = agp->VoiceState();

	assert(agp->m_voice_bind == this);
	switch (st) {
	case HFPD_AG_VOICE_DISCONNECTED:
		agp->m_voice_bind = 0;
		EpRelease();
		return false;
	case HFPD_AG_VOICE_CONNECTING:
		(void) UpdateState(HFPD_SIO_AUDIOGATEWAY_CONNECTING);
		return true;
	case HFPD_AG_VOICE_CONNECTED:
		agp->m_voice_bind = 0;
		break;
	default:
		abort();
	}

	assert((m_state == HFPD_SIO_AUDIOGATEWAY_CONNECTING) ||
	       (m_state == HFPD_SIO_STOPPED));

	res = m_sound->SetSecondary(agp->GetSoundIo());
	assert(res);
	if (!m_sound->Start()) {
		GetDi()->LogWarn("Could not start stream\n");
		EpRelease(HFPD_SIO_AUDIOGATEWAY_CONNECTING);
		return false;
	}

	(void) UpdateState(HFPD_SIO_AUDIOGATEWAY);
	return true;
}

bool SoundIoObj::
EpLoopback(void)
{
	bool res;

	if (m_state != HFPD_SIO_STOPPED)
		EpRelease();
	assert(m_state == HFPD_SIO_STOPPED);
	EpRelease();		/* Call again to run the assertions */
	res = m_sound->Loopback();
	if (!res) {
		GetDi()->LogWarn("Could not configure loopback mode\n");
		return false;
	}
	if (!m_sound->Start()) {
		GetDi()->LogWarn("Could not start stream in loopback mode\n");
		return false;
	}
	(void) UpdateState(HFPD_SIO_LOOPBACK);
	return true;
}

bool SoundIoObj::
EpMembuf(bool in, bool out, SoundIoFilter *fltp)
{
	bool res;

	if (m_state != HFPD_SIO_STOPPED)
		EpRelease();
	assert(m_membuf);
	assert(m_state == HFPD_SIO_STOPPED);
	EpRelease();		/* Call again to run the assertions */
	res = m_sound->SetSecondary(m_membuf);
	assert(res);
	res = m_membuf->SndOpen(in, out);
	assert(res);
	if (fltp) {
		res = m_sound->AddTop(fltp);
		assert(res);
	}
	if (!m_sound->Start()) {
		GetDi()->LogWarn("Could not start stream with membuf\n");
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
	bool res;

	if (m_sound->IsStarted()) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
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
	if (!m_sound->SetDriver(driver, driveropts)) {
		if (old_driver)
			free(old_driver);
		if (old_opts)
			free(old_opts);
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Sound driver configuration failed");
	}

	/*
	 * Save the driver options to the config file
	 */
	if (!m_config->Set("audio", "driver", driver) ||
	    !m_config->Set("audio", "driveropts", driveropts) ||
	    !SaveConfig()) {
		(void) m_sound->SetDriver(old_driver, old_opts);
		if (old_driver)
			free(old_driver);
		if (old_opts)
			free(old_opts);
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	bool res;

	res = dbus_message_iter_init(msgp, &mi);
	assert(res);
	assert(dbus_message_iter_get_arg_type(&mi) == DBUS_TYPE_OBJECT_PATH);
	dbus_message_iter_get_basic(&mi, &agpath);

	agp = m_hf->FindAudioGateway(agpath);
	if (!agp) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Audio Gateway Path Invalid");
	}

	if (!EpAudioGateway(agp)) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not start sound stream");
	}

	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID)) {
		EpRelease();
		return false;
	}

	return true;
}

bool SoundIoObj::
LoopbackStart(DBusMessage *msgp)
{
	if (!EpLoopback()) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not start sound stream");
	}

	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID)) {
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

	if (!EpMembuf(in, out, fltp)) {
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not start sound stream");
	}

	if (!SendReplyArgs(msgp, DBUS_TYPE_INVALID)) {
		EpRelease();
		return false;
	}

	return true;
}


bool SoundIoObj::
GetState(DBusMessage *msgp, unsigned char &val)
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

	for (i = 0; m_sound->GetDriverInfo(i, &name, &desc); i++) {

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
	if (!m_config->Set("audio", "packetinterval", val) ||
	    !SaveConfig()) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	if (!m_config->Set("audio", "minbufferfill", val) ||
	    !SaveConfig()) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	if (!m_config->Set("audio", "jitterwindow", val) ||
	    !SaveConfig()) {
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	SoundIoSpeexProps save = m_procprops;
	m_procprops.noisereduce = val;
	if (!m_sigproc->Configure(m_procprops)) {
		m_procprops = save;
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Change rejected by Speex");
	}
	if (!m_config->Set("dsp", "denoise", val) ||
	    !SaveConfig()) {
		m_procprops = save;
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	SoundIoSpeexProps save = m_procprops;
	m_procprops.agc_level = val;
	if (!m_sigproc->Configure(m_procprops)) {
		m_procprops = save;
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Change rejected by Speex");
	}
	if (!m_config->Set("dsp", "autogain", val) ||
	    !SaveConfig()) {
		m_procprops = save;
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	SoundIoSpeexProps save = m_procprops;
	m_procprops.echocancel_ms = val;
	if (!m_sigproc->Configure(m_procprops)) {
		m_procprops = save;
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Change rejected by Speex");
	}
	if (!m_config->Set("dsp", "echocancel_ms", val) ||
	    !SaveConfig()) {
		m_procprops = save;
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	SoundIoSpeexProps save = m_procprops;
	m_procprops.dereverb_level = val;
	if (!m_sigproc->Configure(m_procprops)) {
		m_procprops = save;
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Change rejected by Speex");
	}
	if (!m_config->Set("dsp", "dereverb_level", val) ||
	    !SaveConfig()) {
		m_procprops = save;
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
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
	SoundIoSpeexProps save = m_procprops;
	m_procprops.dereverb_decay = val;
	if (!m_sigproc->Configure(m_procprops)) {
		m_procprops = save;
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Change rejected by Speex");
	}
	if (!m_config->Set("dsp", "dereverb_decay", val) ||
	    !SaveConfig()) {
		m_procprops = save;
		doreply = false;
		return SendReplyError(msgp,
				      DBUS_ERROR_FAILED,
				      "Could not save configuration");
	}
	return true;
}
