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
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <libhfp/bt.h>
#include <libhfp/hfp.h>
#include <libhfp/soundio.h>
#include <libhfp/events-indep.h>
#include <libhfp/list.h>

#include "net.h"
#include "util.h"
#include "configfile.h"

using namespace libhfp;


struct Command {
	const char *name;
	void (HandsFree::*func)(Session *targ, int argc,
				const char * const *argv);
};

class HandsFree {
public:
	DispatchInterface	*m_di;
	BtHub			*m_hub;
	HfpService		*m_hfp;
	Server			*m_srv;

	SoundIoManager		*m_sound;
	enum {
		SOUND_DECONFIGURED,
		SOUND_NONE,
		SOUND_CALL,
		SOUND_RING,
	}			m_sound_user;
	SoundIo			*m_sound_ringtone;
	SoundIoFltSpeex		*m_sound_sigproc;

	SoundIoSpeexProps	m_sound_procprops;
	bool			m_sessionbind_default;

	Session			*m_inquirer;

	HfpSession		*m_current;
	Session			*m_session_binder;
	bool			m_sessionbind;
	bool			m_unbind_on_voice_close;

	bool			m_autoreconnect;

	char			*m_config_savefile;
	ConfigFile		m_config;
	bool			m_config_dirty;
	bool			m_config_autosave;

	static Command		s_cmds[];

	HandsFree(DispatchInterface *dip)
		: m_di(dip), m_hub(0), m_hfp(0), m_srv(0),
		  m_sound(0), m_sound_user(SOUND_DECONFIGURED),
		  m_sound_ringtone(0), m_sound_sigproc(0),
		  m_sessionbind_default(false),
		  m_inquirer(0),
		  m_current(0), m_session_binder(0),
		  m_sessionbind(false), m_unbind_on_voice_close(false),
		  m_autoreconnect(false),
		  m_config_savefile(0), m_config_dirty(false),
		  m_config_autosave(false) {}

	~HandsFree() {
		if (m_config_savefile) {
			free(m_config_savefile);
			m_config_savefile = 0;
		}
	}

	bool Init(const char *cfgfile) {
		m_config.Clear();

		if (!m_config.Load("/etc/hfpd.conf", 1) &&
		    !m_config.Load("/usr/local/etc/hfpd.conf", 1)) {
			/* No defaults */
		}

		if (m_config_savefile) {
			free(m_config_savefile);
			m_config_savefile = 0;
		}

		if (cfgfile) {
			if (!m_config.Load(cfgfile, 2) &&
			    !m_config.Create(cfgfile)) {
				fprintf(stderr, "Could not open or create "
					"specified config file \"%s\"\n",
					cfgfile);
				return false;
			}
			m_config_savefile = strdup(cfgfile);
			if (!m_config_savefile)
				goto failed;
		}

		if (!m_config_savefile) {
			m_config_savefile = strdup("~/.hfpdrc");
			if (!m_config_savefile)
				goto failed;
			(void) m_config.Load(m_config_savefile, 2);
		}

		m_hub = new BtHub(m_di);
		if (!m_hub)
			goto failed;

		m_hub->cb_BtDeviceFactory.Register(this,
					&HandsFree::DeviceFactory);
		m_hub->cb_NotifySystemState.Register(this,
					&HandsFree::NotifySystemState);
		m_hub->cb_InquiryResult.Register(this,
						 &HandsFree::InquiryResult);

		m_hfp = new HfpService;
		if (!m_hfp)
			goto failed;

		m_hfp->cb_HfpSessionFactory.Register(this,
					&HandsFree::SessionFactory);

		m_hub->AddService(m_hfp);

		m_sound = new SoundIoManager(m_di);
		if (!m_sound)
			goto failed;

		m_sound->cb_NotifyAsyncState.Register(this,
					&HandsFree::NotifySoundStop);

		if (!SoundConfigure())
			goto failed;

		LoadDeviceConfig();

		return true;

	failed:
		Cleanup();
		m_config.Clear();
		return false;
	}

	void Cleanup(void) {
		if (m_hub) {
			m_hub->Stop();
			Stopped();
		}
		if (m_sound) {
			delete m_sound;
			m_sound = 0;
			m_sound_user = SOUND_DECONFIGURED;
		}
		if (m_sound_sigproc) {
			delete m_sound_sigproc;
			m_sound_sigproc = 0;
		}
		if (m_hfp) {
			delete m_hfp;
			m_hfp = 0;
		}
		if (m_hub) {
			delete m_hub;
			m_hub = 0;
		}
	}

	bool SaveConfig(bool force = false) {
		if (!force && !m_config_autosave) {
			m_config_dirty = true;
			return true;
		}
		if (!m_config.Save(m_config_savefile, 2))
			return false;
		m_config_dirty = false;
		return true;
	}

	bool SoundConfigure(void) {
		const char *driver, *driveropts;

		assert(m_sound_user == SOUND_DECONFIGURED);

		m_config.Get("audio", "driver", driver, 0);
		m_config.Get("audio", "driveropts", driveropts, 0);

		if (driver && !driver[0])
			driver = 0;
		if (driveropts && driveropts[0])
			driveropts = 0;
		if (!driver && driveropts)
			driveropts = 0;

		if (!m_sound->SetDriver(driver, driveropts))
			return false;

		m_config.Get("dsp", "noisereduce",
			     m_sound_procprops.noisereduce, true);
		m_config.Get("dsp", "echocancel_ms",
			     m_sound_procprops.echocancel_ms, 100);
		m_config.Get("dsp", "agc_level",
			     m_sound_procprops.agc_level, 10);
		m_config.Get("dsp", "dereverb_level",
			     m_sound_procprops.dereverb_level, 0.0);
		m_config.Get("dsp", "dereverb_decay",
			     m_sound_procprops.dereverb_decay, 0.0);

		m_sound_sigproc = SoundIoFltCreateSpeex(m_di);
		if (!m_sound_sigproc ||
		    !m_sound_sigproc->Configure(m_sound_procprops)) {
			return false;
		}

		m_sound->SetDsp(m_sound_sigproc);

		m_sound_user = SOUND_NONE;
		return true;
	}

	void SoundRelease(void) {
		switch (m_sound_user) {
		case SOUND_NONE:
			assert(!m_sound->IsStarted());
			assert(!m_sound->GetSecondary());
			break;
		case SOUND_CALL:
			assert(m_sound->GetSecondary() == m_current);
			m_sound->Stop();
			m_sound->SetSecondary(0);
			break;
		default:
			abort();
		}
		m_sound_user = SOUND_NONE;
	}

	bool SoundCall(void) {
		bool res;

		if (m_sound_user != SOUND_NONE)
			SoundRelease();
		assert(m_current);
		assert(m_sound_user == SOUND_NONE);
		SoundRelease();
		res = m_sound->SetSecondary(m_current);
		assert(res);
		if (!m_sound->Start()) {
			m_di->LogWarn("Could not start stream\n");
			m_sound->RemoveBottom();
			m_sound->SetSecondary(0);
			return false;
		}
		m_sound_user = SOUND_CALL;
		return true;
	}

	void LoadDeviceConfig(void) {
		HfpSession *sessp;
		const char *addr;
		bool autorestart;

		assert(!m_current);

		m_config.Get("daemon", "autosave", m_config_autosave, true);

		m_config.Get("daemon", "autorestart", autorestart, true);
		m_hub->SetAutoRestart(autorestart);

		m_config.Get("daemon", "autoreconnect",
			     m_autoreconnect, true);

		m_config.Get("daemon", "sessionbind",
			     m_sessionbind_default, true);

		if (m_config.Get("devices", "default", addr, 0)) {
			sessp = m_hfp->GetSession(addr);
			if (sessp)
				SetCurrent(sessp);
		}

		if (autorestart)
			(void) m_hub->Start();
	}

	BtDevice *DeviceFactory(bdaddr_t const &addr) {
		BtDevice *devp;
		devp = m_hub->DefaultDevFactory(addr);
		if (devp) {
			devp->cb_NotifyNameResolved.Register(this,
					&HandsFree::NotifyNameResolved);
		}
		return devp;
	}

	HfpSession *SessionFactory(BtDevice *devp) {
		HfpSession *sessp = m_hfp->DefaultSessionFactory(devp);
		if (!sessp)
			return 0;

		sessp->cb_NotifyConnection.Register(this,
					&HandsFree::NotifyConnection);
		sessp->cb_NotifyVoiceConnection.Register(this,
					&HandsFree::NotifyVoiceConnection);
		sessp->cb_NotifyCall.Register(this, &HandsFree::NotifyCall);
		sessp->cb_NotifyIndicator.Register(this,
					&HandsFree::NotifyIndicator);
		return sessp;
	}



	void Stopped(void) {
		if (m_inquirer) {
			m_inquirer->printf("ERROR bluetooth shut down\n");
			m_inquirer->SetPause(false);
			m_inquirer = 0;
		}
	}

	void NotifySystemState(void) {
		if (!m_hub->IsStarted()) {
			Stopped();
		}
		else if (m_hub->IsStarted() && m_current) {
			BtDevice *devp = m_current->GetDevice();
			devp->Get();
			if (!devp->ResolveName())
				devp->Put();
		}
		DumpAll(m_srv);
	}

	void NotifyConnection(HfpSession *sessp) {
		char buf[32];
		if (sessp != m_current) {
			if (sessp->IsConnecting() || sessp->IsConnected())
				sessp->Disconnect();
			return;
		}

		sessp->GetDevice()->GetAddr(buf);
			      
		if (sessp->IsConnected()) {
			m_di->LogInfo("Attached: %s\n", buf);
			DumpRemoteState(m_srv);
		} else {
			if (!sessp->IsConnecting())
				m_di->LogInfo("Detached: %s\n", buf);
			DumpAll(m_srv);
		}
	}

	void NotifyCall(HfpSession *sessp, bool act, bool waiting, bool ring) {
		if (act || waiting)
			DumpCallState(m_srv);
		if (ring)
			m_srv->printf("+R\n");
	}

	void NotifyIndicator(HfpSession *sessp, const char *indname, int val) {
		DumpIndicators(m_srv);
	}

	void NotifyNameResolved(BtDevice *devp, const char *name) {
		if (m_current &&
		    (devp == m_current->GetDevice()) &&
		    devp->IsNameResolved())
			DumpDevName(m_srv);
	}

	void NotifyVoiceConnection(HfpSession *sessp) {
		if (sessp != m_current) {
			/*
			 * If it's not the current device, close the
			 * voice connection.
			 */
			if (sessp->IsConnectingVoice() ||
			    sessp->IsConnectedVoice())
				sessp->SndClose();
			return;
		}

		if (sessp->IsConnectedVoice() &&
		    (m_sound_user == SOUND_NONE)) {
			SoundCall();
			DumpVoice(m_srv);
		}

		/* We let the pump stop notification deal with halting */
	}

	void NotifySoundStop(SoundIoManager *mgrp) {
		assert(mgrp == m_sound);
		SoundRelease();
		if (m_unbind_on_voice_close) {
			m_unbind_on_voice_close = false;
			SetCurrent(0);
		}
		DumpVoice(m_srv);
	}

	void NotifySessionClosed(Session *sessp) {
		if (sessp == m_session_binder) {
			m_session_binder = 0;
			if (m_sessionbind && m_current) {
				if (m_sound_user != SOUND_CALL) {
					m_current->Disconnect();
					SetCurrent(0);
				} else {
					m_unbind_on_voice_close = true;
				}
			}
		}
	}

	void InquiryResult(BtDevice *devp, int err) {
		char buf[32];
		if (!m_inquirer)
			return;

		if (!devp) {
			InquiryComplete();
			return;
		}

		devp->GetAddr(buf);
		m_inquirer->printf("+X INQUIRY %s %s\n", buf,
				   devp->GetName());
	}

	template <typename RecipT>
	void DumpConnState(RecipT *recip) {
		recip->printf("+D %s\n",
			      !m_hub->IsStarted() ? "STOP" :
			      (!m_current ? "NODEV" :
			       (m_current->IsConnected() ? "CONN" :
				(m_current->IsConnecting() ? "CONNECTING" :
				 "DISCON"))));
	}

	template <typename RecipT>
	void DumpDevAddr(RecipT *recip) {
		if (m_current) {
			char buf[32];
			ba2str(&m_current->GetDevice()->GetAddr(), buf);
			recip->printf("+A %s\n", buf);
		}
	}

	template <typename RecipT>
	void DumpDevName(RecipT *recip) {
		if (m_hub->IsStarted() && m_current) {
			recip->printf("+N %s\n",
				      m_current->GetDevice()->GetName());
		}
	}

	template <typename RecipT>
	void DumpCallState(RecipT *recip) {
		if (m_hub->IsStarted() &&
		    m_current &&
		    m_current->IsConnected()) {
			const char *descr;
			if (!m_current->HasEstablishedCall() &&
			    !m_current->HasConnectingCall()) {
				descr = "IDLE";
			} else if (m_current->HasEstablishedCall() &&
				   !m_current->HasConnectingCall()) {
				descr = "ESTAB";
			} else if (!m_current->HasEstablishedCall() &&
				   m_current->HasConnectingCall()) {
				descr = "CONNECTING";
			}

			recip->printf("+C %s%s\n", descr,
				      m_current->HasWaitingCall()
				      ? ":WAITING" : "");
		}
	}

	template <typename RecipT>
	void DumpCallerId(RecipT *recip) {
		if (m_current &&
		    m_current->IsConnected() &&
		    m_current->HasWaitingCall() &&
		    m_current->WaitingCallIdentity()) {
			recip->printf("+I %s\n",
				      m_current->WaitingCallIdentity());
		}
	}

	template <typename RecipT>
	void DumpVoice(RecipT *recip) {
		if (m_hub->IsStarted() &&
		    m_current &&
		    m_current->IsConnected()) {
			recip->printf("+V %s%s\n",
				      (m_sound_user != SOUND_CALL)
				      ? "DISCON" : "CONN",
				      m_sound->GetMute() ? ":MUTE" : "");
		}
	}

	template <typename RecipT>
	void DumpIndicators(RecipT *recip) {
		if (m_hub->IsStarted() &&
		    m_current &&
		    m_current->IsConnected()) {
			bool roam = false;
			if (m_current->GetRoaming() > 0)
				roam = true;
			if (!m_current->GetServiceState()) {
				recip->printf("+S NOSERVICE\n");
			} else if (m_current->GetSignalStrength() < 0) {
				recip->printf("+S %s\n",
					      roam ? "ROAM" : "SERVICE");
			} else {
				recip->printf("+S %s:%dBARS\n",
					      roam ? "ROAM" : "SERVICE",
					      m_current->GetSignalStrength());
			}

			if (m_current->GetBatteryCharge() >= 0) {
				recip->printf("+B %dBARS\n",
					      m_current->GetBatteryCharge());
			}
		}
	}

	template <typename RecipT>
	void DumpAll(RecipT *recip) {
		DumpConnState(recip);
		DumpDevAddr(recip);
		DumpDevName(recip);
		DumpCallState(recip);
		DumpCallerId(recip);
		DumpIndicators(recip);
		DumpVoice(recip);
	}

	template <typename RecipT>
	void DumpRemoteState(RecipT *recip) {
		DumpConnState(recip);
		DumpCallState(recip);
		DumpCallerId(recip);
		/* DumpIndicators(recip); */
	}

	bool SetDefaultDevice(HfpSession *sessp) {
		char buf[32];
		if (sessp) {
			sessp->GetDevice()->GetAddr(buf);
			return m_config.Set("devices", "default", buf);
		}
		return m_config.Delete("devices", "default");
	}

	void SetCurrent(HfpSession *sessp) {
		BtDevice *devp;

		if (sessp == m_current)
			return;

		if (m_current) {
			m_unbind_on_voice_close = false;
			if (m_sound_user == SOUND_CALL)
				SoundRelease();
			if (m_current->IsConnectingVoice() ||
			    m_current->IsConnectedVoice())
				m_current->SndClose();

			if (m_current->IsConnecting() ||
			    m_current->IsConnected())
				m_current->Disconnect();

			if (m_autoreconnect)
				m_current->SetAutoReconnect(false);
			m_current->Put();
		}

		m_current = sessp;
		if (sessp) {
			sessp->Get();
			devp = sessp->GetDevice();
			devp->Get();
			if (!devp->ResolveName())
				devp->Put();

			if (m_autoreconnect) {
				(void) sessp->Connect();
				sessp->SetAutoReconnect(true);
			}
		}

		DumpAll(m_srv);
	}

	bool SetAutoRestart(bool state, bool force = false) {
		if (force || (state != m_hub->GetAutoRestart())) {
			m_hub->SetAutoRestart(state);
			m_config.Set("devices", "autorestart", state);
			return SaveConfig();
		}

		return true;
	}

	bool SetAutoReconnect(bool state) {
		if (state != m_autoreconnect) {
			m_autoreconnect = state;
			if (m_current)
				m_current->SetAutoReconnect(state);
			m_config.Set("devices", "autoreconnect", state);
			return SaveConfig();
		}

		return true;
	}

	bool SetSessionBind(Session *sessp, bool val) {
		if (val != m_sessionbind_default) {
			m_sessionbind_default = val;
			if (!m_config.Set("devices", "sessionbind", val))
				return false;
			return SaveConfig();
		}
		return true;
	}

	bool CheckIsConnected(Session *sessp) {
		if (!m_hub->IsStarted()) {
			sessp->printf("ERROR not started\n");
			return false;
		}
		if (!m_current) {
			sessp->printf("ERROR no bound device\n");
			return false;
		}
		if (!m_current->IsConnected()) {
			sessp->printf("ERROR device not connected\n");
			return false;
		}
		return true;
	}

	bool ParseParam(const char *param, int &result) {
		char *ep;
		result = strtol(param, &ep, 0);
		return (ep != param);
	}

	bool ParseParam(const char *param, float &result) {
		char *ep;
		result = strtof(param, &ep);
		return (ep != param);
	}

	bool ParseParam(const char *param, bool &result) {
		int intval;
		char *ep;
		if (!strcasecmp(param, "ON")) {
			result = true;
			return true;
		}
		if (!strcasecmp(param, "OFF")) {
			result = false;
			return true;
		}
		intval = strtol(param, &ep, 0);
		if ((ep == param) || ((intval != 0) && (intval != 1)))
			return false;
		result = (intval == 1);
		return true;
	}

	void CmdSound(Session *sessp, int argc, const char * const *argv) {
		int old_user;
		/* Sound card configuration */
		if (argc < 2)
			goto bad_parameters;
		old_user = m_sound_user;
		if (!strcasecmp(argv[1], "DRIVER")) {
			const char *dname = 0, *dopt = 0;
			if (argc == 2) {
				sessp->printf("+X SOUND DRIVER %s %s\n",
					      m_sound->GetDriverName()
					      ? m_sound->GetDriverName()
					      : "DEFAULT",
					      m_sound->GetDriverOpts() ?
					      m_sound->GetDriverOpts() : "");
				sessp->printf("OK\n");
				return;
			}
			if (argc > 4)
				goto bad_parameters;
			if (argc >= 3)
				dname = argv[2];
			if (argc == 4)
				dopt = argv[3];
			if (!strcasecmp(dname, "DEFAULT")) {
				if (argc != 3)
					goto bad_parameters;
				dname = 0;
				dopt = 0;
			}

			if (m_sound_user != SOUND_NONE)
				SoundRelease();

			if (!m_sound->SetDriver(dname, dopt)) {
				sessp->printf("ERROR driver rejected\n");
				goto restore;
			}

			m_config.Set("audio", "driver", dname);
			m_config.Set("audio", "driveropts", dopt);
			goto success;
		}

		if (!strcasecmp(argv[1], "NOISEREDUCE")) {
			bool val, old;

			if (argc == 2) {
				sessp->printf("+X SOUND NOISEREDUCE %s\n",
					      m_sound_procprops.noisereduce
					      ? "ON" : "OFF");
				sessp->printf("OK\n");
				return;
			}

			if ((argc != 3) || !ParseParam(argv[2], val))
				goto bad_parameters;

			if (m_sound_user != SOUND_NONE)
				SoundRelease();

			old = m_sound_procprops.noisereduce;
			m_sound_procprops.noisereduce = val;

			if (!m_sound_sigproc->Configure(m_sound_procprops)) {
				m_sound_procprops.noisereduce = old;
				sessp->printf("ERROR dsp rejected\n");
				goto restore;
			}

			m_config.Set("dsp", "noisereduce", val);
			goto success;
		}

		if (!strcasecmp(argv[1], "ECHOCANCEL")) {
			int val, old;

			if (argc == 2) {
				sessp->printf("+X SOUND ECHOCANCEL %d\n",
					      m_sound_procprops.echocancel_ms);
				sessp->printf("OK\n");
				return;
			}

			if ((argc != 3) || !ParseParam(argv[2], val))
				goto bad_parameters;

			if (val == m_sound_procprops.echocancel_ms)
				goto success_nochange;

			if (m_sound_user != SOUND_NONE)
				SoundRelease();

			old = m_sound_procprops.echocancel_ms;
			m_sound_procprops.echocancel_ms = val;

			if (!m_sound_sigproc->Configure(m_sound_procprops)) {
				m_sound_procprops.echocancel_ms = old;
				sessp->printf("ERROR dsp rejected\n");
				goto restore;
			}

			m_config.Set("dsp", "echocancel_ms", val);
			goto success;
		}

		if (!strcasecmp(argv[1], "AUTOGAIN")) {
			int val, old;

			if (argc == 2) {
				sessp->printf("+X SOUND AUTOGAIN %d\n",
					      m_sound_procprops.agc_level);
				sessp->printf("OK\n");
				return;
			}

			if ((argc != 3) || !ParseParam(argv[2], val))
				goto bad_parameters;

			if (val == m_sound_procprops.agc_level)
				goto success_nochange;

			if (m_sound_user != SOUND_NONE)
				SoundRelease();

			old = m_sound_procprops.agc_level;
			m_sound_procprops.agc_level = val;

			if (!m_sound_sigproc->Configure(m_sound_procprops)) {
				m_sound_procprops.agc_level = old;
				sessp->printf("ERROR dsp rejected\n");
				goto restore;
			}

			m_config.Set("dsp", "agc_level", val);
			goto success;
		}

		if (!strcasecmp(argv[1], "DEREVERB")) {
			float oldl, oldd, level, decay;

			if (argc == 2) {
				sessp->printf("+X SOUND DEREVERB %f %f\n",
				      m_sound_procprops.dereverb_level,
				      m_sound_procprops.dereverb_decay);
				sessp->printf("OK\n");
				return;
			}

			if ((argc != 4) ||
			    !ParseParam(argv[2], level) ||
			    !ParseParam(argv[3], decay))
				goto bad_parameters;


			if (m_sound_user != SOUND_NONE)
				SoundRelease();

			oldl = m_sound_procprops.dereverb_level;
			oldd = m_sound_procprops.dereverb_decay;
			m_sound_procprops.dereverb_level = level;
			m_sound_procprops.dereverb_decay = decay;

			if (!m_sound_sigproc->Configure(m_sound_procprops)) {
				m_sound_procprops.dereverb_level = oldl;
				m_sound_procprops.dereverb_decay = oldd;
				sessp->printf("ERROR dsp rejected\n");
				goto restore;
			}

			m_config.Set("dsp", "dereverb_level", level);
			m_config.Set("dsp", "dereverb_decay", decay);
			goto success;
		}

	bad_parameters:
		sessp->printf("ERROR bad parameters\n");
		sessp->SetDefunct();
		return;

	success:
		SaveConfig();
	success_nochange:
		sessp->printf("OK\n");
	restore:
		if (m_sound_user != old_user) {
			switch (old_user) {
			case SOUND_CALL:
				if (!SoundCall() &&
				    m_unbind_on_voice_close) {
					m_unbind_on_voice_close = false;
					SetCurrent(0);
				}
				break;
			default:
				abort();
			}
		}
		return;
	}

	void CmdSave(Session *sessp, int argc, const char * const *argv) {
		bool val;

		if ((argc == 2) &&
		    !strcasecmp(argv[1], "AUTO")) {
			sessp->printf("+X SAVE AUTO %s\n",
				      m_config_autosave ? "ON" : "OFF");
			sessp->printf("OK\n");
			return;
		}

		if ((argc == 3) &&
		    !strcasecmp(argv[1], "AUTO") &&
		    ParseParam(argv[2], val)) {
			m_config_autosave = val;
			if (!m_config.Set("daemon", "autosave", val) ||
			    (val && !SaveConfig())) {
				sessp->printf("ERROR internal\n");
				return;
			}
			sessp->printf("OK\n");
			return;
		}

		if (argc != 1) {
			sessp->printf("ERROR bad parameters\n");
			sessp->SetDefunct();
			return;
		}

		if (!SaveConfig(true)) {
			sessp->printf("ERROR internal\n");
			return;
		}
		sessp->printf("OK\n");
	}

	void CmdState(Session *sessp, int argc, const char * const *argv) {
		DumpAll(sessp);
		sessp->printf("OK\n");
	}

	void CmdStart(Session *sessp, int argc, const char * const *argv) {
		bool val;

		if ((argc == 2) &&
		    !strcasecmp(argv[1], "AUTO")) {
			sessp->printf("+X START AUTO %s\n",
				      m_hub->GetAutoRestart() ? "ON" : "OFF");
			sessp->printf("OK\n");
			return;
		}

		if ((argc == 3) &&
		    !strcasecmp(argv[1], "AUTO") &&
		    ParseParam(argv[2], val)) {
			if (!SetAutoRestart(val)) {
				sessp->printf("ERROR internal\n");
				return;
			}
			(void) m_hub->Start();
			sessp->printf("OK\n");
			return;
		}
		if (argc != 1) {
			sessp->printf("ERROR bad parameters\n");
			sessp->SetDefunct();
			return;
		}

		if (m_hub->IsStarted()) {
			sessp->printf("ERROR already started\n");
			return;
		}

		if (!m_hub->Start()) {
			sessp->printf("ERROR could not start\n");
			return;
		}

		DumpAll(m_srv);
		sessp->printf("OK\n");
	}

	void CmdStop(Session *sessp, int argc, const char * const *argv) {
		if (!m_hub->IsStarted()) {
			sessp->printf("ERROR already stopped\n");
			return;
		}

		m_hub->Stop();
		Stopped();
		(void) SetAutoRestart(false, true);

		SaveConfig();
		DumpConnState(m_srv);
		sessp->printf("OK\n");
	}

	void InquiryComplete(void) {
		if (!m_inquirer)
			return;
		m_inquirer->printf("OK\n");
		m_inquirer->SetPause(false);
		m_inquirer = 0;
	}

	void CmdInquiry(Session *sessp, int argc, const char * const *argv) {
		if (!m_hub->IsStarted()) {
			sessp->printf("ERROR not started\n");
			return;
		}
		if (m_inquirer) {
			sessp->printf("ERROR inquiry already in progress\n");
			return;
		}

		if (!m_hub->IsScanning() && !m_hub->StartInquiry()) {
			sessp->printf("ERROR internal\n");
			return;
		}

		m_inquirer = sessp;
	}

	void CmdBind(Session *sessp, int argc, const char * const *argv) {
		bdaddr_t ba;
		HfpSession *hsp;
		bool bval;

		if ((argc == 2) &&
		    !strcasecmp(argv[1], "SESSION")) {
			sessp->printf("+X BIND SESSION %s\n",
				      m_sessionbind_default ? "ON" : "OFF");
			sessp->printf("OK\n");
			return;
		}

		if ((argc == 2) &&
		    !strcasecmp(argv[1], "DEFAULT")) {
			const char *dfldev;

			m_config.Get("devices", "default", dfldev, 0);
			sessp->printf("+X BIND DEFAULT %s\n",
				      dfldev ? dfldev : "NONE");
			sessp->printf("OK\n");
			return;
		}

		if ((argc == 3) &&
		    !strcasecmp(argv[1], "SESSION") &&
		    ParseParam(argv[2], bval)) {
			if (!SetSessionBind(sessp, bval)) {
				sessp->printf("ERROR internal\n");
				return;
			}
			sessp->printf("OK\n");
			return;
		}

		m_sessionbind = m_sessionbind_default;
		if ((argc == 3) &&
		    !strcasecmp(argv[2], "SESSION")) {
			m_sessionbind = true;
		}
		else if ((argc == 3) &&
			 !strcasecmp(argv[2], "DEFAULT")) {
			m_sessionbind = false;
		}
		else if (argc != 2) {
			sessp->printf("ERROR bad parameters\n");
			sessp->SetDefunct();
			return;
		}

		str2ba(argv[1], &ba);
		hsp = m_hfp->GetSession(ba);
		if (!hsp) {
			sessp->printf("ERROR internal\n");
			return;
		}

		SetCurrent(hsp);
		if (!m_sessionbind) {
			if (!SetDefaultDevice(hsp) ||
			    !SaveConfig()) {
				sessp->printf("ERROR internal\n");
				return;
			}
		}
		if (m_sessionbind) {
			m_session_binder = sessp;
			sessp->SetDeleteCallback(&HandsFree::
						 NotifySessionClosed);
		}
		hsp->Put();
		sessp->printf("OK\n");
	}

	void CmdUnbind(Session *sessp, int argc, const char * const *argv) {
		bool do_config;
		if (!m_current) {
			sessp->printf("ERROR no bound device\n");
			return;
		}

		do_config = !m_sessionbind_default && !m_sessionbind;
		if ((argc == 2) &&
		    !strcasecmp(argv[1], "DEFAULT")) {
			/*
			 * We don't actually unbind the device, we just
			 * remove it as the default.
			 */
			if (!SetDefaultDevice(0) ||
			    !SaveConfig()) {
				sessp->printf("ERROR internal\n");
				return;
			}
			sessp->printf("OK\n");
			return;
		}

		else if ((argc == 2) &&
			 !strcasecmp(argv[1], "SESSION")) {
			do_config = false;
		}
		else if (argc != 1) {
			sessp->printf("ERROR bad parameters\n");
			sessp->SetDefunct();
			return;
		}

		if (do_config) {
			if (!SetDefaultDevice(0) ||
			    !SaveConfig()) {
				sessp->printf("ERROR internal\n");
				return;
			}
		}
		SetCurrent(0);
		if (m_session_binder)
			m_session_binder->SetDeleteCallback(0);
		DumpConnState(m_srv);
		sessp->printf("OK\n");
	}

	void CmdConnect(Session *sessp, int argc, const char * const *argv) {
		bool val;

		if ((argc == 2) &&
		    !strcasecmp(argv[1], "AUTO")) {
			sessp->printf("+X CONNECT AUTO %s\n",
				      m_autoreconnect ? "ON" : "OFF");
			sessp->printf("OK\n");
			return;
		}
		if ((argc == 3) &&
		    !strcasecmp(argv[1], "AUTO") &&
		    ParseParam(argv[2], val)) {
			if (!SetAutoReconnect(val)) {
				sessp->printf("ERROR internal\n");
				return;
			}
			sessp->printf("OK\n");
			return;
		}

		if (argc != 1) {
			sessp->printf("ERROR bad parameters\n");
			sessp->SetDefunct();
			return;
		}
		if (!m_hub->IsStarted()) {
			sessp->printf("ERROR not started\n");
			return;
		}
		if (!m_current) {
			sessp->printf("ERROR no bound device\n");
			return;
		}
		if (m_current->IsConnecting()) {
			sessp->printf("ERROR connection in progress\n");
			return;
		}
		if (m_current->IsConnected()) {
			sessp->printf("ERROR already connected\n");
			return;
		}

		if (!m_current->Connect()) {
			sessp->printf("ERROR internal\n");
			return;
		}
		sessp->printf("OK\n");
	}

	void CmdDisconnect(Session *sessp, int argc, const char *const *argv) {
		if (!m_hub->IsStarted()) {
			sessp->printf("ERROR not started\n");
			return;
		}
		if (!m_current) {
			sessp->printf("ERROR no bound device\n");
			return;
		}
		if (!m_current->IsConnecting() &&
		    !m_current->IsConnected()) {
			sessp->printf("ERROR already disconnected\n");
			return;
		}

		m_current->Disconnect();
		DumpConnState(m_srv);
		sessp->printf("OK\n");
	}

	void CmdVoice(Session *sessp, int argc, const char * const *argv) {
		bool enable;
		if ((argc == 2) && !strcasecmp(argv[1], "ON")) {
			enable = true;
		} else if ((argc == 2) && !strcasecmp(argv[1], "OFF")) {
			enable = false;
		} else if ((argc == 2) && !strcasecmp(argv[1], "MUTE")) {
			if (!m_sound->SetMute(true)) {
				sessp->printf("ERROR internal\n");
				return;
			}
			DumpVoice(m_srv);
			sessp->printf("OK\n");
			return;
		} else if ((argc == 2) && !strcasecmp(argv[1], "UNMUTE")) {
			if (!m_sound->SetMute(false)) {
				sessp->printf("ERROR internal\n");
				return;
			}
			DumpVoice(m_srv);
			sessp->printf("OK\n");
			return;
		} else {
			sessp->printf("ERROR bad parameters\n");
			sessp->SetDefunct();
			return;
		}
		if (!CheckIsConnected(sessp))
			return;
		if (enable) {
			if (m_sound_user == SOUND_CALL) {
				assert(m_current->IsConnectedVoice());
				sessp->printf("ERROR already on\n");
				return;
			}
			/* Maybe initiate a voice connection */
			if (!m_current->IsConnectingVoice() &&
			    !m_current->IsConnectedVoice()) {
				m_current->SndOpen(true, true);
			}
		} else {
			if ((m_sound_user != SOUND_CALL) &&
			    !m_current->IsConnectingVoice() &&
			    !m_current->IsConnectedVoice()) {
				sessp->printf("ERROR already off\n");
				return;
			}
			SoundRelease();
			m_current->SndClose();
			if (m_unbind_on_voice_close) {
				m_unbind_on_voice_close = false;
				SetCurrent(0);
			}
			DumpVoice(m_srv);
		}

		sessp->printf("OK\n");
	}

	void CmdDial(Session *sessp, int argc, const char * const *argv) {
		if ((argc != 1) && (argc != 2)) {
			sessp->printf("ERROR bad parameters\n");
			sessp->SetDefunct();
			return;
		}
		if (!CheckIsConnected(sessp))
			return;
		if (argc == 2) {
			if (!m_current->CmdDial(argv[1])) {
				sessp->printf("ERROR bad phone number\n");
				return;
			}
		} else if (!m_current->CmdRedial()) {
			sessp->printf("ERROR internal\n");
			return;
		}

		sessp->printf("OK\n");
	}

	void CmdAnswer(Session *sessp, int argc, const char * const *argv) {
		if (!CheckIsConnected(sessp))
			return;
		if (!m_current->CmdAnswer()) {
			sessp->printf("ERROR internal\n");
			return;
		}

		sessp->printf("OK\n");
	}

	void CmdHangup(Session *sessp, int argc, const char * const *argv) {
		if (!CheckIsConnected(sessp))
			return;
		if (!m_current->CmdHangUp()) {
			sessp->printf("ERROR internal\n");
			return;
		}

		sessp->printf("OK\n");
	}

	void CmdDrop(Session *sessp, int argc, const char * const *argv) {
		if (!CheckIsConnected(sessp))
			return;
		if (!m_current->CmdCallDropWaiting()) {
			sessp->printf("ERROR internal\n");
			return;
		}
		sessp->printf("OK\n");
	}

	void CmdSwap(Session *sessp, int argc, const char * const *argv) {
		bool drop;
		if (argc == 1) {
			drop = false;
		} else if ((argc == 2) &&
			   !strcasecmp(argv[1], "DROP")) {
			drop = true;
		} else if ((argc == 2) &&
			   !strcasecmp(argv[1], "HOLD")) {
			drop = false;
		} else {
			sessp->printf("ERROR bad parameters\n");
			sessp->SetDefunct();
			return;
		}
		if (!CheckIsConnected(sessp))
			return;
		if (drop) {
			if (!m_current->CmdCallSwapDropActive()) {
				sessp->printf("ERROR internal\n");
				return;
			}
		} else {
			if (!m_current->CmdCallSwapHoldActive()) {
				sessp->printf("ERROR internal\n");
				return;
			}
		}
		sessp->printf("OK\n");
	}

	void CmdHold(Session *sessp, int argc, const char * const *argv) {
		if (!CheckIsConnected(sessp))
			return;
		if (!m_current->CmdCallSwapHoldActive()) {
			sessp->printf("ERROR internal\n");
			return;
		}
		sessp->printf("OK\n");
	}

	void CmdLink(Session *sessp, int argc, const char * const *argv) {
		if (!CheckIsConnected(sessp))
			return;
		if (!m_current->CmdCallLink()) {
			sessp->printf("ERROR internal\n");
			return;
		}
		sessp->printf("OK\n");
	}

	void CmdDtmf(Session *sessp, int argc, const char * const *argv) {
		if ((argc != 2) || (strlen(argv[1]) != 1)) {
			sessp->printf("ERROR bad parameters\n");
			sessp->SetDefunct();
			return;
		}
		if (!CheckIsConnected(sessp))
			return;
		if (!m_current->CmdSendDtmf(argv[1][0])) {
			sessp->printf("ERROR internal\n");
			return;
		}

		sessp->printf("OK\n");
	}

	void CmdAbort(Session *sessp, int argc, const char * const *argv) {
		/* Abort is invoked in a nonstandard way vs. other CmdXxx */
		if (sessp == m_inquirer) {
			m_inquirer = 0;
			sessp->printf("ERROR inquiry aborted\n");
			sessp->printf("OK\n");
			return;
		}

		sessp->printf("ERROR no pending action\n");
	}

	bool DispatchCommand(Session *sessp, int argc, char * const *argv) {
		int i;

		if (!strcasecmp(argv[0], "ABORT")) {
			CmdAbort(sessp, argc, argv);
			return true;
		}

		if (sessp == m_inquirer) {
			/* Silence the client until we're done */
			sessp->SetPause(true);
			return false;
		}

		for (i = 0; s_cmds[i].name; i++) {
			if (!strcasecmp(s_cmds[i].name, argv[0])) {
				(this->*(s_cmds[i].func))(sessp, argc, argv);
				return true;
			}
		}

		sessp->printf("ERROR unknown command\n");
		return true;
	}
};


/*
 * The command table, mapping an argv[0] to a handler method
 */
Command HandsFree::s_cmds[] = {
	{ "SOUND",		&HandsFree::CmdSound },
	{ "SAVE",		&HandsFree::CmdSave },
	{ "STATE",		&HandsFree::CmdState },
	{ "START",		&HandsFree::CmdStart },
	{ "STOP",		&HandsFree::CmdStop },
	{ "INQUIRY",		&HandsFree::CmdInquiry },
	{ "BIND",		&HandsFree::CmdBind },
	{ "UNBIND",		&HandsFree::CmdUnbind },
	{ "CONNECT",		&HandsFree::CmdConnect },
	{ "DISCONNECT",		&HandsFree::CmdDisconnect },
	{ "VOICE",		&HandsFree::CmdVoice },
	{ "ANSWER",		&HandsFree::CmdAnswer },
	{ "DIAL",		&HandsFree::CmdDial },
	{ "HANGUP",		&HandsFree::CmdHangup },
	{ "DROP",		&HandsFree::CmdDrop },
	{ "SWAP",		&HandsFree::CmdSwap },
	{ "HOLD",		&HandsFree::CmdHold },
	{ "LINK",		&HandsFree::CmdLink },
	{ "DTMF",		&HandsFree::CmdDtmf },
	{ 0 }
};


static void
usage(const char *argv0)
{
	const char *bn;

	bn = strrchr(argv0, '/');
	if (!bn)
		bn = argv0;
	else
		bn++;

	fprintf(stdout,
"Usage: %s [-f] [-p <port>] [-r] [-s <socketfile>] [-n] [-E] [-S] [-d <level>]\n"
"Available Options:\n"
"-c <file>	Specify local read/write settings file\n"
"-f		Run in foreground, do not daemonize\n"
"-p <port>	TCP port to listen on, 0 = disable TCP\n"
"-r		Allow remote TCP connections\n"
"-s <sockfile>	UNIX domain socket to listen on,\n"
"		default /tmp/hfpd.sock\n"
"-n		Do not listen on a UNIX domain socket\n"
"-E		Log to stderr\n"
"-S		Log to syslog\n"
"-d <level>	Log level:\n"
"		0: No log messages\n"
"		1: Severe errors only\n"
"		2: Warnings, severe errors\n"
#if !defined(NDEBUG)
"		3: Information, warnings, errors\n"
"		4: Detailed debug messages (DEFAULT)\n"
#else
"		3: Information, warnings, errors (DEFAULT)\n"
"		4: Detailed debug messages\n"
#endif
		"\n",
		bn);
}

typedef DispatchInterface::logtype_t loglev_t;

int main(int argc, char **argv)
{
	const char *unix_path = 0;
	int inet_port = 1234;
	loglev_t loglevel;
	const char *cfgfile = 0;
	bool do_inet_remote = false;
	bool do_foreground = false;
	bool do_syslog = false;
	bool do_stderr = false;
	int c;

#if !defined(NDEBUG)
	loglevel = DispatchInterface::EVLOG_DEBUG;
#else
	loglevel = DispatchInterface::EVLOG_INFO;
#endif

	opterr = 0;
	while ((c = getopt(argc, argv, "hH?fp:rs:nESd:")) != -1) {
		switch (c) {
		case 'h':
		case 'H':
		case '?':
			usage(argv[0]);
			return 0;

		case 'c':
			cfgfile = optarg;
			break;
		case 'f':
			do_foreground = 1;
			break;
		case 'p':
			inet_port = strtol(optarg, NULL, 0);
			break;
		case 'r':
			do_inet_remote = true;
			break;
		case 's':
			unix_path = optarg;
			break;
		case 'n':
			unix_path = 0;
			break;
		case 'E':
			do_stderr = true;
			break;
		case 'S':
			do_syslog = true;
			break;
		case 'd':
			loglevel = (loglev_t) strtol(optarg, NULL, 0);
			break;
		}
	}

	if (!do_stderr && !do_syslog) {
		if (do_foreground)
			do_stderr = true;
		else
			do_syslog = true;
	}

	SyslogDispatcher disp;

	/* Until we daemonize, we always use stderr for logging */
	disp.SetSyslog(do_syslog);
	disp.SetStderr(true);
	disp.SetLevel(loglevel);

	HandsFree hf(&disp);
	Server srv(&disp, &hf);
	srv.m_dispatch = &HandsFree::DispatchCommand;
	hf.m_srv = &srv;

	if (!hf.Init(cfgfile)) {
		fprintf(stderr,
			"Could not initialize\n"
			"hfpd aborting\n");
		return 1;
	}

	if (unix_path && !srv.UnixListen(unix_path)) {
		fprintf(stderr,
			"Could not create UNIX listener socket\n"
			"hfpd aborting\n");
		return 1;
	}

	if (inet_port && !srv.InetListen(inet_port, do_inet_remote)) {
		fprintf(stderr,
			"Could not create IP listener socket\n"
			"hfpd aborting\n");
		return 1;
	}

	if (!do_foreground && !Daemonize())
		return 1;

	/* Maybe turn off stderr logging */
	disp.SetStderr(do_stderr);

	disp.Run();
	return 0;
}
