/*
 * Software Hands-Free with Crappy UI
 */

#include <stdio.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libhfp/hfp.h>
#include <libhfp/soundio.h>

#include "nohands.h"
#include "scandialog.h"
#include "prefs.h"

#include <qpushbutton.h>
#include <qsocketnotifier.h>
#include <qtimer.h>
#include <qapplication.h>
#include <qvbox.h>
#include <qvbox.h>
#include <qlayout.h>
#include <qscrollview.h>
#include <qlabel.h>
#include <qlistbox.h>
#include <qcheckbox.h>
#include <qradiobutton.h>
#include <qsettings.h>
#include <qmessagebox.h>
#include <qlineedit.h>
#include <qcombobox.h>

#include "events-qt.h"

/* The global notifier factory */
static QtEventDispatchInterface g_qt_ei;


/*
 * The SelectableBox widget is filled with the state entries associated
 * with an audio gateway.  We alter the frame to indicate whether the
 * audio gateway is focused, and allow it to make itself focused by
 * clicking.
 */
class SelectableBox : public QVBox {
	Q_OBJECT;
signals:
	void selected(void);
public:
	void setSelected(bool enable) {
		if (enable) {
			setFrameStyle(QFrame::WinPanel | QFrame::Sunken);
		} else {	
			setFrameStyle(QFrame::WinPanel | QFrame::Raised);
		}
		update();
	}
	virtual void mouseReleaseEvent(QMouseEvent *ev) {
		selected();
	}
	SelectableBox(QWidget *parent, const char *name)
		: QVBox(parent, name) {}
};


class ScanResult;
class RealUI;

class AgDevice : public QObject {
	Q_OBJECT;

public slots:
	void Selected(void);
	void DetachConnectionClicked(void);
	void HoldClicked(void);
	void HangupClicked(void);
	void AcceptCallClicked(void);
	void RejectCallClicked(void);
	void ReconnectDeviceClicked(void);

public:
	HfpSession		*m_sess;
	bool			m_known_device;

	/* State kept in UI */
	RealUI			*m_ui;
	QString			m_active_cli;
	QString			m_setup_cli;

	SelectableBox		*m_devlist_box;

	enum CallControlMode {
		CCM_DEAD,
		CCM_DISCONN,
		CCM_IDLE,
		CCM_RINGING,
		CCM_CONNECTED,
		CCM_CALLWAITING
	}		m_callmode;

	AgDevice(HfpSession *sessp)
		: m_sess(sessp), m_known_device(false), m_devlist_box(0),
		  m_callmode(CCM_DEAD) {

		m_sess->SetPrivate(this);

		/*
		 * We operate without an extra reference on the session
		 * and depend on the destruction callback to delete
		 * ourselves.
		 */
	}

	virtual ~AgDevice() {}

public:
	bool IsKnown(void) const { return m_known_device; }
	void SetKnown(bool known) {
		if (known && !m_known_device) {
			m_sess->Get();
		} else if (!known && m_known_device) {
			m_sess->Put();
		}
		m_known_device = known;
	}
};


class ScanResult : public QListBoxText {
	friend class RealUI;
	BtDevice 	*m_device;

public:
	ScanResult(BtDevice *devp)
		: QListBoxText(QString(devp->GetName())),
		  m_device(devp) {
		m_device->Get();
		m_device->SetPrivate(this);
		setText(QString(m_device->GetName()));
	}
	virtual ~ScanResult() {
		assert(m_device->GetPrivate() == (void *) this);
		m_device->SetPrivate(0);
		m_device->Put();
		m_device = 0;
	}

	void UpdateText(const char *name) {
		if (name) {
			setText(QString(name));
			listBox()->triggerUpdate(false);
		}
	}

	BtDevice *Device(void) const { return m_device; }
};


class RealUI : public NoHands {
	Q_OBJECT;
public:

	BtHub			*m_hub;
	HfpService		*m_hfpsvc;

	AgDevice		*m_all_devices;

	bool			m_known_devices_only;
	bool			m_autoreconnect;

	/* UI details */
	QScrollView		*m_device_scroll;
	QVBoxLayout		*m_device_layout;
	QWidget			*m_device_filler;

	AgDevice		*m_active_dev;

	AgDevice *GetAgp(BtDevice *devp) {
		AgDevice *agp = 0;
		HfpSession *sessp = m_hfpsvc->GetSession(devp);
		if (sessp) {
			agp = (AgDevice*)sessp->GetPrivate();
			sessp->Put();
			if (agp) {
				assert(agp->m_sess == sessp);
				return agp;
			}
		}
		return 0;
	}

	void CreateDeviceBox(AgDevice *devp) {
		int idx;
		QLabel *lp;
		QPushButton *pbp;
		QHBox *hbp;
		SelectableBox *sbp =
			new SelectableBox(m_device_scroll, 0);

		idx = m_device_layout->findWidget(m_device_filler);
		m_device_layout->insertWidget(idx, sbp);
		sbp->setSelected(false);
		connect(sbp, SIGNAL(selected()), devp, SLOT(Selected()));
		sbp->hide();
		devp->m_devlist_box = sbp;

		hbp = new QHBox(sbp, NULL);
		hbp->show();

		lp = new QLabel(hbp, "status");
		lp->setText("");
		lp->show();

		pbp = new QPushButton(hbp, "reconnect");
		pbp->setText("Reconnect");
		connect(pbp, SIGNAL(clicked()),
			devp, SLOT(ReconnectDeviceClicked()));
		pbp->hide();

		pbp = new QPushButton(hbp, "detach");
		pbp->setText("Detach");
		connect(pbp, SIGNAL(clicked()),
			devp, SLOT(DetachConnectionClicked()));
		pbp->hide();

		hbp = new QHBox(sbp, NULL);
		hbp->show();

		lp = new QLabel(hbp, "activecall");
		lp->setText("");
		lp->hide();

		hbp = new QHBox(sbp, NULL);
		hbp->show();

		pbp = new QPushButton(hbp, "hold");
		pbp->setText("Hold");
		connect(pbp, SIGNAL(clicked()), devp, SLOT(HoldClicked()));
		pbp->hide();

		pbp = new QPushButton(hbp, "hangup");
		pbp->setText("Hang Up");
		connect(pbp, SIGNAL(clicked()), devp, SLOT(HangupClicked()));
		pbp->hide();

		hbp = new QHBox(sbp, NULL);
		hbp->show();

		lp = new QLabel(hbp, "setupcall");
		lp->setText("");
		lp->hide();

		hbp = new QHBox(sbp, NULL);
		hbp->show();

		pbp = new QPushButton(hbp, "acceptcall");
		pbp->setText("Accept");
		connect(pbp, SIGNAL(clicked()),
			devp, SLOT(AcceptCallClicked()));
		pbp->hide();

		pbp = new QPushButton(hbp, "rejectcall");
		pbp->setText("Reject");
		connect(pbp, SIGNAL(clicked()),
			devp, SLOT(RejectCallClicked()));
		pbp->hide();

		devp->m_callmode = AgDevice::CCM_DEAD;
	}

	void NotifyConnection(AgDevice *agp, HfpSession *sessp) {
		assert(sessp == agp->m_sess);

		if (!sessp->IsConnecting() && !sessp->IsConnected()) {
			/*
			 * Device is disconnected and must have been
			 * connecting or connected before
			 */
			printf("%s device disconnect!\n",
			       sessp->IsPriorDisconnectVoluntary()
			       ? "Voluntary" : "Involuntary");
			if (sessp->IsPriorDisconnectVoluntary()) {
				VoluntaryDisconnect(agp);
			}
		}

		else if (sessp->IsConnecting()) {
			/* Connection is being established */
			AuditInboundConnection(agp);
		}

		else if (sessp->IsConnected()) {
			/* Connection is established */
			printf("Device connected!\n"
			       "ThreeWayCalling: %s\n"
			       "ECNR: %s\n"
			       "Voice Recognition: %s\n"
			       "InBandRingTone: %s\n"
			       "Voice Tags: %s\n"
			       "Reject Call: %s\n",
			       sessp->FeatureThreeWayCalling() ? "yes" : "no",
			       sessp->FeatureEcnr() ? "yes" : "no",
			       sessp->FeatureVoiceRecog() ? "yes" : "no",
			       sessp->FeatureInBandRingTone() ? "yes" : "no",
			       sessp->FeatureVoiceTag() ? "yes" : "no",
			       sessp->FeatureRejectCall() ? "yes" : "no");
		}

		/* Kick off a name resolution task */
		if ((sessp->IsConnecting() || sessp->IsConnected()) &&
		    !sessp->GetDevice()->IsNameResolved()) {
			(void) sessp->GetDevice()->ResolveName();
		}

		UpdateButtons(agp);
	}

	void NotifyVoiceConnection(AgDevice *agp, HfpSession *sessp) {
		assert(agp->m_sess == sessp);
		if (!sessp->IsConnectingVoice() &&
		    !sessp->IsConnectedVoice())
			VoiceDetached(agp);
		else if (sessp->IsConnectedVoice())
			VoiceAttached(agp);
	}

	void NotifyCall(AgDevice *agp, HfpSession *sessp,
			bool ac_changed, bool wc_changed, bool ring) {
		if (ac_changed) {
			printf("In Call: %s\n",
			       sessp->HasEstablishedCall() ? "yes" : "no");
			if (sessp->HasEstablishedCall()) {
				agp->m_active_cli = agp->m_setup_cli;
				if (!sessp->IsConnectedVoice()) {
					(void) sessp->SndOpen(true, true);
				}
			}
		}

		if (wc_changed) {
			printf("Call Setup: %sOutgoing %sIncoming\n",
			       sessp->HasConnectingCall() ? "" : "!",
			       sessp->HasWaitingCall() ? "" : "!");
			if (sessp->HasWaitingCall()) {
				agp->m_setup_cli =
					sessp->WaitingCallIdentity()
					? QString(sessp->
						  WaitingCallIdentity()->
						  number)
					: QString("");
				printf("WCLI: %s\n",agp->m_setup_cli.latin1());
			}
		}

		if (ring) {
			const char *clip = (sessp->WaitingCallIdentity() ?
				    sessp->WaitingCallIdentity()->number : 0);
			if (!clip)
				clip = "Private Number";
			printf("Ring!! %s\n", clip);
			agp->m_setup_cli = clip;
			if (!sessp->HasEstablishedCall())
				DoRing();
		}

		UpdateButtons(agp);
	}

	void NotifyIndicator(AgDevice *agp, HfpSession *sessp,
			     const char *indname, int value) {
		assert(agp->m_sess == sessp);
		printf("Indicator: \"%s\" = %d\n", indname, value);
		UpdateButtons(agp);
	}


	/*
	 * We trap the HFP session instantiation path and
	 * use it to create our AgDevice shadow objects.
	 */
	HfpSession *SessionFactory(BtDevice *devp) {
		HfpSession *sessp;
		AgDevice *agp;

		sessp = m_hfpsvc->DefaultSessionFactory(devp);
		if (!sessp)
			return 0;

		agp = new AgDevice(sessp);
		if (!agp) {
			sessp->Put();
			return 0;
		}

		agp->m_ui = this;
		CreateDeviceBox(agp);

		/* Register notification callbacks */
		sessp->cb_NotifyConnection.
			Bind(this, &RealUI::NotifyConnection,
			     agp, Arg1);
		sessp->cb_NotifyVoiceConnection.
			Bind(this, &RealUI::NotifyVoiceConnection,
			     agp, Arg1);
		sessp->cb_NotifyCall.
			Bind(this, &RealUI::NotifyCall,
			     agp, Arg1, Arg2, Arg3, Arg4);
		sessp->cb_NotifyIndicator.
			Bind(this, &RealUI::NotifyIndicator,
			     agp, Arg1, Arg2, Arg3);

		return sessp;
	}

	void NotifyNameResolved(BtDevice *devp, const char *name) {
		AgDevice *agp;

		if (devp->GetPrivate()) {
			ScanResult *srp = (ScanResult *) devp->GetPrivate();
			srp->UpdateText(name);
		}

		agp = GetAgp(devp);
		if (agp)
			UpdateButtons(agp);
	}

	BtDevice *DeviceFactory(bdaddr_t const &addr) {
		BtDevice *devp;
		devp = m_hub->DefaultDevFactory(addr);
		if (!devp)
			return 0;
		devp->cb_NotifyNameResolved.
			Register(this, &RealUI::NotifyNameResolved);
		return devp;
	}

	void SetButtonState(AgDevice *agp, const char *bname, bool visib) {
		QWidget *widgetp;
		widgetp = (QWidget*) agp->m_devlist_box->child(bname);
		assert(widgetp != NULL);
		if (visib) {
			widgetp->show();
		} else {
			widgetp->hide();
		}
	}

	SoundIo *OpenCallRecord(AgDevice *agp) {
		SoundIo *siop;
		/* Open a SoundIo for a call record WAV file? */
		siop = SoundIoCreateFileHandler(&g_qt_ei, "wiretap.wav", true);
		return siop;
	}

	void MaybeAttachVoice(void) {
		AgDevice *agp = m_active_dev;
		HfpSession *sessp;
		assert(agp);
		sessp = agp->m_sess;
		assert(sessp);

		if (!sessp->IsConnectingVoice() &&
		    !sessp->IsConnectedVoice() &&
		    (sessp->HasEstablishedCall() ||
		     sessp->HasConnectingCall())) {
			/* Try to raise the voice connection */
			sessp->SndOpen(true, true);
		}

		else if (sessp->IsConnectedVoice()) {
			if (m_sound_user == SC_RINGTONE) {
				/*
				 * Stop playing a ring tone and
				 * answer the call!
				 */
				SoundCardRelease();
			}

			if ((m_sound_user == SC_NONE) && !SoundCardCall()) {
				printf("Could not start audio pump\n");
				SoundCardRelease();
				sessp->SndClose();
			}
		}
	}

	void SetDeviceFocus(AgDevice *agp) {
		if (agp == m_active_dev) { return; }
		if (agp && !agp->m_sess->IsConnected()) { return; }
		if (m_active_dev) {
			m_active_dev->m_devlist_box->setSelected(false);

			/* Stop streaming audio from the device losing focus */
			if (m_sound_user == SC_CALL) {
				SoundCardRelease();
				m_active_dev->m_sess->SndClose();
			}
		}
		m_active_dev = agp;
		if (agp != NULL) {
			agp->m_devlist_box->setSelected(true);
			MaybeAttachVoice();
		}
	}

	void SetDeviceBox(AgDevice *agp, AgDevice::CallControlMode ccm,
			  QString &status_cap, QString &ac_cap,
			  QString &sc_cap) {
		QLabel *status, *ac, *sc;

		status = (QLabel*) agp->m_devlist_box->child("status");
		assert(status != NULL);
		ac = (QLabel*) agp->m_devlist_box->child("activecall");
		assert(ac != NULL);
		sc = (QLabel*) agp->m_devlist_box->child("setupcall");
		assert(sc != NULL);

		if (ccm == agp->m_callmode) {
			goto skip_buttons;
		}

		switch (ccm) {
		case AgDevice::CCM_DEAD:
			agp->m_devlist_box->hide();
			break;
		case AgDevice::CCM_DISCONN:
			ac->hide();
			sc->hide();
			SetButtonState(agp, "reconnect", true);
			SetButtonState(agp, "detach", true);
			SetButtonState(agp, "hold", false);
			SetButtonState(agp, "hangup", false);
			SetButtonState(agp, "acceptcall", false);
			SetButtonState(agp, "rejectcall", false);
			break;
		case AgDevice::CCM_IDLE:
			ac->hide();
			sc->hide();
			SetButtonState(agp, "reconnect", false);
			SetButtonState(agp, "detach", true);
			SetButtonState(agp, "hold", false);
			SetButtonState(agp, "hangup", false);
			SetButtonState(agp, "acceptcall", false);
			SetButtonState(agp, "rejectcall", false);
			break;
		case AgDevice::CCM_RINGING:
			ac->hide();
			sc->show();
			SetButtonState(agp, "reconnect", false);
			SetButtonState(agp, "detach", true);
			SetButtonState(agp, "hold", false);
			SetButtonState(agp, "hangup", false);
			SetButtonState(agp, "acceptcall", true);
			SetButtonState(agp, "rejectcall",
				       agp->m_sess->FeatureRejectCall());
			break;
		case AgDevice::CCM_CONNECTED:
			ac->show();
			sc->hide();
			SetButtonState(agp, "reconnect", false);
			SetButtonState(agp, "detach", true);
			SetButtonState(agp, "hold", true);
			SetButtonState(agp, "hangup", true);
			SetButtonState(agp, "acceptcall", false);
			SetButtonState(agp, "rejectcall", false);
			break;
		case AgDevice::CCM_CALLWAITING:
			ac->show();
			sc->show();
			SetButtonState(agp, "reconnect", false);
			SetButtonState(agp, "detach", true);
			SetButtonState(agp, "hold", true);
			SetButtonState(agp, "hangup", true);
			SetButtonState(agp, "acceptcall", true);
			SetButtonState(agp, "rejectcall", 
				       agp->m_sess->FeatureRejectCall());
			break;
		default:
			abort();
		}

	skip_buttons:
		if (ccm != AgDevice::CCM_DEAD) {
			status->setText(status_cap);
			ac->setText(ac_cap);
			sc->setText(sc_cap);
			agp->m_devlist_box->show();
		}

		agp->m_callmode = ccm;
	}

	void AuditInboundConnection(AgDevice *targag) {
		if (targag->m_sess->IsConnectionRemoteInitiated() &&
		    m_known_devices_only &&
		    !targag->IsKnown()) {
			/* Rejected! */
			targag->m_sess->Disconnect();
			return;
		}

		if (targag->m_sess->IsConnected() && !m_known_devices_only) {
			targag->SetKnown(true);
			targag->m_sess->SetAutoReconnect(m_autoreconnect);
			SaveConfiguration();
		}
	}

	void VoluntaryDisconnect(AgDevice *targag) {
		if (targag->IsKnown()) {
			targag->SetKnown(false);
			SaveConfiguration();
		}
		targag->m_sess->SetAutoReconnect(false);
	}

	void UpdateButtons(AgDevice *targag) {
		AgDevice::CallControlMode ccm = AgDevice::CCM_DEAD;

		if (targag) {
			HfpSession *sessp = targag->m_sess;
			QString status, ac, sc;

			status = sessp->GetDevice()->GetName();
			if (!sessp->IsConnected() &&
			    !sessp->IsConnecting()) {

				if (sessp->IsAutoReconnect() ||
				    targag->IsKnown()) {
					status += "\nDetached";
					ccm = AgDevice::CCM_DISCONN;
				} else {
					ccm = AgDevice::CCM_DEAD;
				}
			}
			else if (sessp->IsConnecting()) {
				ccm = AgDevice::CCM_IDLE;
				status += "\nAttaching";
			}
			else if (!sessp->GetService()) {
				ccm = AgDevice::CCM_IDLE;
				status += "\nNo Service";
			}
			else {
				if (sessp->IsConnectedVoice()) {
					status += "\nVoice Open";
				} else {
					status += "\nReady";
				}

				if (!sessp->HasEstablishedCall() &&
				    !sessp->HasConnectingCall() &&
				    !sessp->HasWaitingCall()) {
					ccm = AgDevice::CCM_IDLE;
				}
				else if (!sessp->HasEstablishedCall() &&
					 !sessp->HasConnectingCall()) {
					ccm = AgDevice::CCM_RINGING;
					sc = "Incoming Call: " +
						targag->m_setup_cli;
				}
				else if (!sessp->HasWaitingCall() &&
					 !sessp->HasConnectingCall()) {
					ccm = AgDevice::CCM_CONNECTED;
					ac = "Connected: " +
						targag->m_active_cli;
				}
				else if (!sessp->HasEstablishedCall()) {
					ccm = AgDevice::CCM_CONNECTED;
					ac = "Dialing: " +
						targag->m_setup_cli;
				}
				else {
					ccm = AgDevice::CCM_CALLWAITING;
					ac = "Connected: " +
						targag->m_active_cli;
					sc = "Incoming Call: " +
						targag->m_setup_cli;
				}
			}
			SetDeviceBox(targag, ccm, status, ac, sc);
		}

		if ((m_active_dev == NULL) ||
		    !m_active_dev->m_sess->IsConnected()) {
			/* Maybe choose a new device? */
			if (targag && targag->m_sess->IsConnected()) {
				SetDeviceFocus(targag);
			} else {
				BtDevice *devp;
				AgDevice *agp = 0;
				for (devp = m_hub->GetFirstDevice();
				     (devp != NULL);
				     devp = m_hub->GetNextDevice(devp)) {
					agp = GetAgp(devp);
					if (agp->m_sess->IsConnected())
						break;
				}

				if (agp)
					SetDeviceFocus(agp);
			}
		}

		if (m_active_dev == NULL) {
			disableAllButtons();
			if (m_hub->IsStarted())
				setStatus("No Audio Gateways Available");
			else
				setStatus("Bluetooth Unavailable");
			return;
		}

		setStatus("");

		if ((targag != NULL) &&
		    (targag != m_active_dev)) {
			/* Uninteresting */
			return;
		}

		if (!m_active_dev->m_sess->IsConnected() ||
		    !m_active_dev->m_sess->GetService()) {
			/* Gray everything! */
			disableAllButtons();
		} else {
			setDigitButtonsEnabled(true);
			setDialButtonsEnabled(!m_active_dev->m_sess->
					      HasEstablishedCall() &&
					      !m_active_dev->m_sess->
					      HasConnectingCall());
			setHandsetButtonEnabled(m_active_dev->m_sess->
						HasEstablishedCall() ||
						m_active_dev->m_sess->
						HasConnectingCall(),
						!m_active_dev->m_sess->
						IsConnectedVoice());
		}
	}

	/*
	 * Sound transfer + signal processing glue
	 */

	QString			m_sound_driver;
	QString			m_sound_driveropt;

	QString			m_ringtone_filename;
	SoundIo			*m_ringtone_src;

	SoundIo			*m_membuf;

	SoundIoManager		*m_sound;
	int			m_packet_size_ms;
	int			m_outbuf_size_ms;

	SoundIoFltSpeex		*m_sigproc;
	SoundIoSpeexProps	m_sigproc_props;

	enum {
		SC_SHUTDOWN,
		SC_NONE,
		SC_CALL,
		SC_FEEDBACK,
		SC_MEMBUF,
		SC_RINGTONE
	}			m_sound_user;

	PrefsDialog		*m_prefs;

	bool SoundCardInit(void) {
		assert(!m_sound);
		assert(!m_sound);
		assert(m_sound_user == SC_SHUTDOWN);

		m_sound = new SoundIoManager(&g_qt_ei);
		if (!m_sound)
			return false;

		if (!m_sound->SetDriver(m_sound_driver.latin1(),
					m_sound_driveropt.latin1()))
			return false;

		m_sound->cb_NotifyAsyncState.Register(this,
					     &RealUI::NotifyPumpState);

		m_sound->SetPacketIntervalHint(m_packet_size_ms);
		m_sound->SetMinBufferFillHint(m_outbuf_size_ms);
		/* hard-code for now */
		m_sound->SetJitterWindowHint(10);

		m_sigproc = SoundIoFltCreateSpeex(&g_qt_ei);
		m_sigproc->Configure(m_sigproc_props);

		m_sound->SetDsp(m_sigproc);

		m_sound_user = SC_NONE;
		return true;
	}

	void SoundCardShutdown(void) {
		assert(m_sound);
		assert(m_sound_user == SC_NONE);
		assert(!m_sound->IsStarted());
		delete m_sound;
		m_sound = 0;
		m_sound_user = SC_SHUTDOWN;
	}

	/*
	 * Previously the state of the sound card was poorly controlled.
	 * This routine is pedantic to make up for it.
	 */
	void SoundCardRelease(void) {
		switch (m_sound_user) {
		case SC_NONE:
			assert(!m_sound->IsStarted());
			break;
		case SC_CALL:
			m_sound->Stop();
			assert(m_sound->GetSecondary() ==
			       m_active_dev->m_sess);
			m_sound->SetSecondary(0);
			break;
		case SC_FEEDBACK:
			m_sound->Stop();
			assert(m_sound->GetSecondary() == 0);
			m_sound->SetSecondary(0);
			break;
		case SC_MEMBUF:
			assert(m_membuf);
			m_sound->Stop();
			assert(m_sound->GetSecondary() == m_membuf);
			m_sound->SetSecondary(0);
			m_membuf->SndClose();
			break;
		case SC_RINGTONE:
			assert(m_ringtone_src != 0);
			m_sound->Stop();
			assert(m_sound->GetSecondary() == m_ringtone_src);
			m_sound->SetSecondary(0);
			m_ringtone_src->SndClose();
			break;
		default:
			/* Unexpected state */
			abort();
		}
		m_sound_user = SC_NONE;
	}

	bool SoundCardCall(void) {
		assert(m_active_dev);
		assert(m_sound_user == SC_NONE);
		assert(!m_sound->IsStarted());
		assert(m_sound->GetSecondary() == 0);
		m_sound->SetSecondary(m_active_dev->m_sess);
		if (!m_sound->Start()) {
			m_sound->SetSecondary(0);
			return false;
		}
		m_sound_user = SC_CALL;
		return true;
	}

	bool SoundCardLoop(void) {
		assert(m_sound_user == SC_NONE);
		assert(!m_sound->IsStarted());
		assert(m_sound->GetSecondary() == 0);
		m_sound->Loopback();
		if (!m_sound->Start()) {
			m_sound->SetSecondary(0);
			return false;
		}
		m_sound_user = SC_FEEDBACK;
		return true;
	}

	bool SoundCardMembuf(bool in, bool out) {
		assert(m_membuf);
		assert(m_sound_user == SC_NONE);
		assert(!m_sound->IsStarted());
		assert(m_sound->GetSecondary() == 0);
		m_sound->SetSecondary(m_membuf);
		if (!m_membuf->SndOpen(in, out))
			abort();
		if (!m_sound->Start()) {
			m_sound->SetSecondary(0);
			m_membuf->SndClose();
			return false;
		}
		m_sound_user = SC_MEMBUF;
		return true;
	}

	bool SoundCardRingTone(void) {
		if (m_ringtone_src == 0)
			return false;

		assert(m_sound_user == SC_NONE);
		assert(!m_sound->IsStarted());
		assert(m_sound->GetSecondary() == 0);

		if (!m_ringtone_src->SndOpen(false, true))
			return false;

		m_sound->SetSecondary(m_ringtone_src);
		if (!m_sound->Start()) {
			m_sound->SetSecondary(0);
			m_ringtone_src->SndClose();
			return false;
		}

		m_sound_user = SC_RINGTONE;
		return true;
	}

	void OpenRingTone(void) {
		if (m_sound_user == SC_RINGTONE)
			/* Stop an existing ring tone playback */
			SoundCardRelease();

		if (m_ringtone_src) {
			delete m_ringtone_src;
			m_ringtone_src = 0;
		}

		m_ringtone_src = SoundIoCreateFileHandler(&g_qt_ei,
			m_ringtone_filename.latin1(), false);

		if (!m_ringtone_src) {
			printf("Could not create ring tone handler\n");
		}
	}

	void VoiceAttached(AgDevice *agp) {
		/*
		 * If the focused device doesn't have an active
		 * voice connection, shift focus to this device.
		 */

		if ((m_active_dev != agp) &&
		    ((m_active_dev == NULL) ||
		     !m_active_dev->m_sess->IsConnectedVoice())) {
			SetDeviceFocus(agp);
		}

		/*
		 * If this was already the focused device, attempt
		 * to open the sound card and start the audio pump.
		 */

		else if (m_active_dev == agp) {
			MaybeAttachVoice();
		}

		printf("Voice attached!\n");
		UpdateButtons(agp);
	}

	void VoiceDetached(AgDevice *agp) {
		if ((m_sound_user == SC_CALL) && (agp == m_active_dev)) {
			SoundCardRelease();
		}
		printf("Voice detached!\n");
		UpdateButtons(agp);
	}

	/* Support for inquiries and scanning */
	ScanDialog		*m_scanbox;

	virtual void OpenScanDialog(void) {
		ScanResult *resp;

		assert(m_scanbox == NULL);
		m_scanbox = new ScanDialog(this, NULL, true);

		if (!m_hub->IsScanning())
			m_hub->StartInquiry(5000); /* FIXME: hard-code 5sec */

		if (m_scanbox->exec() == QDialog::Accepted) {
			AgDevice *agp = 0;

			resp = (ScanResult*)
				m_scanbox->ScanResultsList->selectedItem();
			if (resp != NULL)
				agp = GetAgp(resp->Device());

			if (agp) {
				if (!agp->IsKnown()) {
					agp->SetKnown(true);
					SaveConfiguration();
				}
				agp->m_sess->SetAutoReconnect(m_autoreconnect);
				agp->m_sess->Connect();
			}
		}

		delete m_scanbox;
		m_scanbox = NULL;
	}

public slots:
	void ToggleFeedbackTest(bool enable) {
		assert(m_prefs);

		if (m_sound_user == SC_SHUTDOWN)
			return;

		if (enable && (m_sound_user == SC_NONE)) {
			QString label;

			if (!SoundCardLoop()) {
				fprintf(stderr, "Could not start "
					"feedback loop\n");
				SoundCardRelease();
				return;
			}

			label.sprintf("%dms", m_sound->GetPacketInterval());
			m_prefs->RealPacketSizeLabel->setText(label);
			return;
		}

		if (enable) {
			/* Sound card is busy */
			return;
		}

		m_prefs->RealPacketSizeLabel->setText("");
		if (m_sound_user == SC_FEEDBACK) {
			SoundCardRelease();
			if (m_active_dev)
				MaybeAttachVoice();
		}
	}

	void ToggleProcTest(bool enable) {
		assert(m_prefs);

		if (m_sound_user == SC_SHUTDOWN)
			return;

		if (enable && (m_sound_user == SC_NONE)) {
			bool in = (m_prefs->RecTestRadio->isOn() ||
				   m_prefs->DuplexTestRadio->isOn());
			bool out = (m_prefs->PlayTestRadio->isOn() ||
				    m_prefs->DuplexTestRadio->isOn());

			if (!SoundCardMembuf(in, out)) {
				fprintf(stderr, "Could not start "
					"memory buffer mode\n");
				return;
			}
		}

		if (enable) {
			/* Sound card is busy */
			return;
		}

		if (m_sound_user == SC_MEMBUF) {
			if (m_prefs->RecTestRadio->isOn()) {
				m_prefs->DuplexTestRadio->setChecked(true);
			} else if (m_prefs->DuplexTestRadio->isOn()) {
				m_prefs->PlayTestRadio->setChecked(true);
			}
			SoundCardRelease();
			if (m_active_dev)
				MaybeAttachVoice();
		}
	}

	void SoundCardReconfig(void) {
		int oldstate = m_sound_user;

		if (m_sound_user != SC_NONE)
			SoundCardRelease();

		if (!m_sigproc->Configure(m_sigproc_props)) {
			fprintf(stderr, "%s: sigproc failed\n", __FUNCTION__);
			return;
		}
		m_sound->SetPacketIntervalHint(m_packet_size_ms);
		m_sound->SetMinBufferFillHint(m_outbuf_size_ms);

		switch (oldstate) {
		case SC_CALL:
			MaybeAttachVoice();
			break;
		case SC_FEEDBACK:
			if (SoundCardLoop() && m_prefs) {
				QString label;
				label.sprintf("%dms",
					      m_sound->GetPacketInterval());
				m_prefs->RealPacketSizeLabel->setText(label);
			}
			break;
		case SC_RINGTONE:
			SoundCardRingTone();
			break;
		}
	}

	void UpdatePacketSize(int packet_ms) {
		m_packet_size_ms = packet_ms;
		SoundCardReconfig();
	}

	void UpdateOutbufSize(int outbuf_ms) {
		m_outbuf_size_ms = outbuf_ms;
		SoundCardReconfig();
	}

	void UpdateNoiseReduction(bool enable) {
		m_sigproc_props.noisereduce = enable;
		SoundCardReconfig();
	}

	void UpdateEchoCancel(int echocancel_ms) {
		m_sigproc_props.echocancel_ms = echocancel_ms;
		SoundCardReconfig();
	}

	void UpdateAutoGain(int agc_level) {
		m_sigproc_props.agc_level = agc_level * 1000;
		SoundCardReconfig();
	}

	void UpdateDereverb(float drlvl, float drdecay) {
		m_sigproc_props.dereverb_level = drlvl / 100;
		m_sigproc_props.dereverb_decay = drdecay / 100;
		SoundCardReconfig();
	}

public:

	virtual void OpenPrefsDialog(void) {
		SoundIoSpeexProps save_props(m_sigproc_props);
		int save_packet = m_packet_size_ms,
			save_outbuf = m_outbuf_size_ms;
		PrefsDialog *prefsp;
		bool res, old_autoreconnect, reopen_soundcard;
		rfcomm_secmode_t secmode;
		int i;
		SoundIoFormat mbfmt;

		/* Use this fixed format for membuf testing */
		memset(&mbfmt, 0, sizeof(mbfmt));
		mbfmt.samplerate = 8000;
		mbfmt.sampletype = SIO_PCM_S16_LE;
		mbfmt.nchannels = 1;
		mbfmt.bytes_per_record = 2;

		/* Store ten seconds */
		assert(!m_membuf);
		m_membuf = SoundIoCreateMembuf(&mbfmt, 10 * mbfmt.samplerate);
		assert(m_membuf);

		prefsp = new PrefsDialog(this, NULL, false);
		assert(prefsp);

		m_prefs = prefsp;

		/* Bluetooth page */
		switch (m_hfpsvc->GetSecMode()) {
		case RFCOMM_SEC_NONE:
			prefsp->AttachAny->setChecked(true);
			break;
		case RFCOMM_SEC_AUTH:
			prefsp->AttachAuth->setChecked(true);
			break;
		case RFCOMM_SEC_CRYPT:
			prefsp->AttachEncrypt->setChecked(true);
			break;
		}

		prefsp->DisableAttachUnknown->setChecked(m_known_devices_only);
		prefsp->AutoReconnect->setChecked(m_autoreconnect);

		static const char *drivers[] = { "ALSA", "OSS", 0 };
		prefsp->DriverSelect->insertStrList(drivers);
		for (i = 0; drivers[i]; i++) {
			if (m_sound_driver == drivers[i]) {
				prefsp->DriverSelect->setCurrentItem(i);
				break;
			}
		}

		prefsp->DriverOpt->setText(m_sound_driveropt);

		/* Signal Processing page */
		connect(prefsp, SIGNAL(ToggleFeedbackTest(bool)),
			this, SLOT(ToggleFeedbackTest(bool)));
		connect(prefsp, SIGNAL(ToggleProcTest(bool)),
			this, SLOT(ToggleProcTest(bool)));
		connect(prefsp, SIGNAL(UpdatePacketSize(int)),
			this, SLOT(UpdatePacketSize(int)));
		connect(prefsp, SIGNAL(UpdateOutbufSize(int)),
			this, SLOT(UpdateOutbufSize(int)));
		connect(prefsp, SIGNAL(UpdateNoiseReduction(bool)),
			this, SLOT(UpdateNoiseReduction(bool)));
		connect(prefsp, SIGNAL(UpdateEchoCancel(int)),
			this, SLOT(UpdateEchoCancel(int)));
		connect(prefsp, SIGNAL(UpdateAutoGain(int)),
			this, SLOT(UpdateAutoGain(int)));
		connect(prefsp, SIGNAL(UpdateDereverb(float,float)),
			this, SLOT(UpdateDereverb(float,float)));
		prefsp->SetSignalProcValues(m_packet_size_ms,
					    m_outbuf_size_ms,
					    m_sigproc_props.echocancel_ms,
					    m_sigproc_props.noisereduce,
					    m_sigproc_props.agc_level / 1000,
					    m_sigproc_props.dereverb_level,
					    m_sigproc_props.dereverb_decay);

		/* Alerting page */
		prefsp->UseInBandRingTone->setDisabled(true);	/* Not yet */
		prefsp->RingToneFile->setText(m_ringtone_filename);

		res = prefsp->exec();

		if (m_sound_user == SC_FEEDBACK) {
			ToggleFeedbackTest(false);
			assert(m_sound_user != SC_FEEDBACK);
		}

		if (!res) {
			/* Revert to the old settings if they're different */
			memcpy(&m_sigproc_props, &save_props,
			       sizeof(m_sigproc_props));
			m_packet_size_ms = save_packet;
			m_outbuf_size_ms = save_outbuf;
			SoundCardReconfig();
			goto done;
		}

		/* Bluetooth tab */
		if (prefsp->AttachEncrypt->isChecked()) {
			secmode = RFCOMM_SEC_CRYPT;
		} else if (prefsp->AttachAuth->isChecked()) {
			secmode = RFCOMM_SEC_AUTH;
		} else {
			secmode = RFCOMM_SEC_NONE;
		}
		if (!m_hfpsvc->SetSecMode(secmode)) {
			fprintf(stderr, "Failed to set sec mode\n");
		}
		m_known_devices_only =
			prefsp->DisableAttachUnknown->isChecked();
		old_autoreconnect = m_autoreconnect;
		m_autoreconnect = prefsp->AutoReconnect->isChecked();
		if (old_autoreconnect != m_autoreconnect) {
			BtDevice *devp;
			for (devp = m_hub->GetFirstDevice();
			     (devp != NULL);
			     devp = m_hub->GetNextDevice(devp)) {
				AgDevice *agp = 0;
				agp = GetAgp(devp);
				if (agp && agp->IsKnown())
					agp->m_sess->SetAutoReconnect(
						m_autoreconnect);
			}
		}

		/* Audio device tab */
		reopen_soundcard =
			(m_sound_driver !=
			 prefsp->DriverSelect->currentText()) ||
			(m_sound_driveropt != prefsp->DriverOpt->text());

		/* Alerting tab */
		m_ringtone_filename = prefsp->RingToneFile->text();
		OpenRingTone();

		if (reopen_soundcard) {
			int old_state = m_sound_user;
			m_sound_driver = prefsp->DriverSelect->currentText();
			m_sound_driveropt = prefsp->DriverOpt->text();
			if (old_state > SC_NONE)
				SoundCardRelease();

			/* Try opening the device in any case */
			if (!m_sound->SetDriver(m_sound_driver.latin1(),
						m_sound_driveropt.latin1()) ||
			    !m_sound->TestOpen()) {
				if (old_state == SC_CALL)
					m_active_dev->m_sess->SndClose();
				QMessageBox::warning(0, QString("NoHands"),
			     QString("Audio Device Could Not Be Opened"),
						     QMessageBox::Ok,
						     QMessageBox::NoButton);
			}

			else switch (old_state) {
			case SC_NONE:
				/* Leave it alone */
				break;
			case SC_CALL:
				/* Try to reattach the call */
				MaybeAttachVoice();
				break;
			case SC_RINGTONE:
				SoundCardRingTone();
				break;
			default:
				/* Bad state */
				abort();
			}
		}

		SaveConfiguration();

	done:
		assert(m_membuf);
		assert(m_sound_user != SC_MEMBUF);
		delete m_membuf;
		m_membuf = 0;

		assert(m_prefs == prefsp);
		delete prefsp;
		m_prefs = 0;
	}

	void NotifyInquiryResult(BtDevice *devp, int error) {

		if (!devp) {
			if (error) {
				QMessageBox::warning(0, QString("NoHands"),
				     QString("Inquiry Failed to Start"),
						     QMessageBox::Ok,
						     QMessageBox::NoButton);
				return;
			}

			/* Scan is complete */
			printf("HCI scan complete\n");
			return;
		}

		if (m_scanbox) {
			ScanResult *resp = new ScanResult(devp);
			m_scanbox->ScanResultsList->insertItem(resp);
			devp->ResolveName();
		}
		printf("Scan: %s\n", devp->GetName());
	}

	void NotifyBTState(void) {
		if (!m_hub->IsStarted()) {
			setStatus("Bluetooth Unavailable");
			printf("Bluetooth Failure, hub shut down\n");
		} else {
			uint32_t devclass;
			if (m_hub->GetDeviceClassLocal(devclass) &&
			    !m_hfpsvc->IsDeviceClassHf(devclass)) {
				m_hfpsvc->SetDeviceClassHf(devclass);
				printf("*** Your configured device class may "
				       "not be recognized as a hands-free\n");
				printf("*** Edit /etc/bluetooth/hcid.conf "
				       "and change:\n");
				printf("*** class 0x%06x;\n", devclass);
			}
		}

		UpdateButtons(0);
	}

	void NotifyPumpState(SoundIoManager *soundp) {
		int old_user = m_sound_user;
		fprintf(stderr, "Audio pump aborted\n");
		SoundCardRelease();
		switch (old_user) {
		case SC_CALL:
			m_active_dev->m_sess->SndClose();
			UpdateButtons(m_active_dev);
			break;
		case SC_FEEDBACK:
			if (m_prefs)
				m_prefs->FeedbackTest->setOn(false);
			break;
		case SC_MEMBUF:
			if (m_prefs)
				m_prefs->ProcTest->setOn(false);
			break;
		}
	}

	void DoRing(void) {
		/* Only start a ring tone if the device is unoccupied */
		if (m_sound_user == SC_NONE)
			SoundCardRingTone();
	}

	bool DelPend(HfpPendingCommand *pendp) {
		if (!pendp)
			return false;

		delete pendp;
		return true;
	}

	/*
	 * Bluetooth Command Handling for the user interface
	 */
	virtual void KeypadPress(char c) {
		if ((m_active_dev != NULL) &&
		    m_active_dev->m_sess->HasEstablishedCall()) {
			DelPend(m_active_dev->m_sess->CmdSendDtmf(c));
		}
	}
	virtual void KeypadClick(char c) {
		if ((m_active_dev == NULL) ||
		    (!m_active_dev->m_sess->HasEstablishedCall() &&
		     !m_active_dev->m_sess->HasConnectingCall())) {
			NoHands::KeypadClick(c);
		}
	}
	virtual void Dial(const QString &phoneNum) {
		if (!phoneNum.isEmpty() &&
		    (m_active_dev != NULL) &&
		    m_active_dev->m_sess->GetService()) {
			printf("Dial: %s\n", phoneNum.latin1());
			DelPend(m_active_dev->m_sess->
				CmdDial(phoneNum.latin1()));
		}
	}
	virtual void RedialClicked(void) {
		if ((m_active_dev != NULL) &&
		    m_active_dev->m_sess->GetService()) {
			DelPend(m_active_dev->m_sess->CmdRedial());
		}
	}
	virtual void HandsetClicked(void) {
		if (m_sound_user == SC_CALL) {
			SoundCardRelease();
			m_active_dev->m_sess->SndClose();
			UpdateButtons(m_active_dev);
		} else
			MaybeAttachVoice();
	}
	virtual void HoldClicked(AgDevice *devp) {
		DelPend(devp->m_sess->CmdCallSwapHoldActive());
	}
	virtual void HangupClicked(AgDevice *devp) {
		DelPend(devp->m_sess->CmdHangUp());
	}
	virtual void AcceptCallClicked(AgDevice *devp) {
		if (devp->m_sess->HasEstablishedCall()) {
			DelPend(devp->m_sess->CmdCallSwapDropActive());
		} else {
			DelPend(devp->m_sess->CmdAnswer());
		}
	}
	virtual void RejectCallClicked(AgDevice *devp) {
		DelPend(devp->m_sess->CmdCallDropHeldUdub());
	}

	/*
	 * Configuration Data
	 */

	void LoadConfiguration(void) {
		QSettings cs;
		int val;
		QStringList slist;
		AgDevice *agp;

		/* Load everything from QSettings */
		cs.setPath("NobodyWare", "NoHands");

		/* Bluetooth */
		val = cs.readNumEntry("/nh/bluetooth/secmode",
				      RFCOMM_SEC_CRYPT);
		if ((val != RFCOMM_SEC_CRYPT) &&
		    (val != RFCOMM_SEC_AUTH) &&
		    (val != RFCOMM_SEC_NONE)) {
			val = RFCOMM_SEC_CRYPT;
		}
		m_hfpsvc->SetSecMode((rfcomm_secmode_t)val);
		m_known_devices_only =
			cs.readBoolEntry("/nh/bluetooth/known_devices_only",
					 true);
		m_autoreconnect =
			cs.readBoolEntry("/nh/bluetooth/autoreconnect", false);

		/* Known devices */
		slist = cs.entryList("/nh/knowndevices");
		for (QStringList::Iterator it = slist.begin();
		     it != slist.end();
		     it++) {
			BtDevice *devp;
			agp = 0;
			devp = m_hub->GetDevice((*it).latin1());
			if (devp) {
				agp = GetAgp(devp);
				devp->Put();
			}
			if (agp) {
				agp->SetKnown(true);
				agp->m_sess->SetAutoReconnect(
					m_autoreconnect);
				UpdateButtons(agp);
			}
		}

		/* Driver related */
		m_sound_driver = cs.readEntry("/nh/driver/name");
		m_sound_driveropt = cs.readEntry("/nh/driver/opt");
		m_packet_size_ms = cs.readNumEntry("/nh/driver/packet_ms", 10);
		m_outbuf_size_ms = cs.readNumEntry("/nh/driver/outbuf_ms", 20);

		/* Signal processing */
		m_sigproc_props.echocancel_ms =
			cs.readNumEntry("/nh/signalproc/echocancel_ms", 100);
		m_sigproc_props.noisereduce =
			cs.readBoolEntry("/nh/signalproc/noisereduce", true);
		m_sigproc_props.agc_level =
			cs.readNumEntry("/nh/signalproc/agc_level", 0);
		m_sigproc_props.dereverb_level =
			cs.readDoubleEntry("/nh/signalproc/dereverb_level", 0);
		m_sigproc_props.dereverb_decay =
			cs.readDoubleEntry("/nh/signalproc/dereverb_decay", 0);

		/* Alerting */
		m_ringtone_filename =
			cs.readEntry("/nh/alerting/ringtonefile");
		OpenRingTone();
	}

	void SaveConfiguration(void) {
		QSettings cs;
		QStringList slist;
		BtDevice *devp;
		char namebuf[32];

		/* Load everything from QSettings */
		cs.setPath("NobodyWare", "NoHands");

		/* Bluetooth */
		cs.writeEntry("/nh/bluetooth/secmode", m_hfpsvc->GetSecMode());
		cs.writeEntry("/nh/bluetooth/known_devices_only",
			      m_known_devices_only);
		cs.writeEntry("/nh/bluetooth/autoreconnect", m_autoreconnect);

		/* Clear out the known devices list */
		slist = cs.entryList("/nh/knowndevices");
		for (QStringList::Iterator it = slist.begin();
		     it != slist.end();
		     it++) {
			cs.removeEntry(QString("/nh/knowndevices/") + *it);
		}

		/* Repopulate */
		for (devp = m_hub->GetFirstDevice();
		     devp != NULL;
		     devp = m_hub->GetNextDevice(devp)) {
			AgDevice *agp = 0;
			agp = GetAgp(devp);
			if (!agp || !agp->IsKnown()) {
				continue;
			}
			devp->GetAddr(namebuf);
			cs.writeEntry(QString("/nh/knowndevices/") +
				      QString(namebuf), true);
		}

		/* Driver related */
		cs.writeEntry("/nh/driver/name", m_sound_driver);
		cs.writeEntry("/nh/driver/opt", m_sound_driveropt);
		cs.writeEntry("/nh/driver/packet_ms", m_packet_size_ms);
		cs.writeEntry("/nh/driver/outbuf_ms", m_outbuf_size_ms);

		/* Signal processing */
		cs.writeEntry("/nh/signalproc/echocancel_ms",
			      m_sigproc_props.echocancel_ms);
		cs.writeEntry("/nh/signalproc/noisereduce",
			      m_sigproc_props.noisereduce);
		cs.writeEntry("/nh/signalproc/agc_level",
			      m_sigproc_props.agc_level);
		cs.writeEntry("/nh/signalproc/dereverb_level",
			      m_sigproc_props.dereverb_level);
		cs.writeEntry("/nh/signalproc/dereverb_decay",
			      m_sigproc_props.dereverb_decay);

		/* Alerting */
		cs.writeEntry("/nh/alerting/ringtonefile",
			      m_ringtone_filename);
	}

	void ReconnectNow(void) {
		BtDevice *devp;

		for (devp = m_hub->GetFirstDevice();
		     devp != NULL;
		     devp = m_hub->GetNextDevice(devp)) {
			AgDevice *agp = 0;
			agp = GetAgp(devp);
			if (agp->IsKnown()) {
				agp->ReconnectDeviceClicked();
			}
		}
	}

	bool Init(void) {
		LoadConfiguration();

		if (!SoundCardInit())
			return false;

		if (!m_sound->TestOpen()) {
			QMessageBox::warning(0, QString("NoHands"),
			     QString("Audio Device Could Not Be Opened"),
					     QMessageBox::Ok,
					     QMessageBox::NoButton);
		}

		m_hub->SetAutoRestart(true);
		m_hub->Start();
		NotifyBTState();

		if (m_autoreconnect) {
			ReconnectNow();
		}
		return true;
	}

	AgDevice *Connect(const char *rname) {
		HfpSession *sessp;
		AgDevice *agp = 0;
		sessp = m_hfpsvc->Connect(rname);
		if (sessp != NULL) {
			agp = (AgDevice *) sessp->GetPrivate();
			assert(agp && (agp->m_sess == sessp));

			if (!agp->IsKnown()) {
				agp->SetKnown(true);
				SaveConfiguration();
			}
			sessp->SetAutoReconnect(m_autoreconnect);
			UpdateButtons(agp);
		}
		return agp;
	}

	RealUI(QWidget *parent = 0, const char *name = 0,
	       bool modal = FALSE, WFlags fl = 0)
		: NoHands(parent, name, modal, fl),
		  m_known_devices_only(true), m_autoreconnect(false),
		  m_active_dev(NULL), m_ringtone_src(0),
		  m_membuf(0), m_sound(0), m_sound_user(SC_SHUTDOWN),
		  m_prefs(0), m_scanbox(NULL) {

		m_hub = new BtHub(&g_qt_ei);

		m_hub->cb_InquiryResult.Register(this,
						 &RealUI::NotifyInquiryResult);
		m_hub->cb_NotifySystemState.Register(this,
						&RealUI::NotifyBTState);
		m_hub->cb_BtDeviceFactory.Register(this,
						&RealUI::DeviceFactory);

		m_hfpsvc = new HfpService();
		m_hfpsvc->cb_HfpSessionFactory.Register(this,
						&RealUI::SessionFactory);
		m_hub->AddService(m_hfpsvc);

		UpdateButtons(NULL);
		QLabel *lp;

		StateDummy1->hide();
		StateDummy2->hide();

		m_device_scroll = new QScrollView(this, NULL);
		StateShell->addWidget(m_device_scroll);
		//m_device_scroll->resize(StateShell->size());
		m_device_scroll->setHScrollBarMode(QScrollView::AlwaysOn);
		m_device_scroll->setSizePolicy(QSizePolicy::Expanding,
					       QSizePolicy::Expanding);
		m_device_scroll->show();

		m_device_layout = new QVBoxLayout(m_device_scroll);
		//m_device_list->setMinimumSize(m_device_scroll->size());

		lp = new QLabel(m_device_scroll);
		m_device_layout->addWidget(lp);
		lp->show();
		m_device_filler = lp;
	}

	virtual ~RealUI() {
		SoundCardRelease();
		SoundCardShutdown();
		delete m_hub;
	}
};


/*
 * I'm not clever enough to slot these user interface callbacks to
 * RealUI, so they are slotted to AgDevice and we call RealUI from here.
 */

void AgDevice::
Selected(void)
{
	m_ui->SetDeviceFocus(this);
	m_ui->UpdateButtons(NULL);
}

void AgDevice::
DetachConnectionClicked(void)
{
	if (m_sess->IsConnected() || m_sess->IsConnecting()) {
		m_sess->Disconnect();
		/*
		 * Because we called Disconnect directly, it will be
		 * recorded as a voluntary disconnection, and
		 * NotifyDeviceConnection() will behave as such.
		 */
		m_ui->NotifyVoiceConnection(this, m_sess);
		m_ui->NotifyConnection(this, m_sess);
	} else {
		m_ui->VoluntaryDisconnect(this);
		m_ui->UpdateButtons(this);
	}
}

void AgDevice::
HoldClicked(void)
{
	m_ui->HoldClicked(this);
}

void AgDevice::
HangupClicked(void)
{
	m_ui->HangupClicked(this);
}

void AgDevice::
AcceptCallClicked(void)
{
	m_ui->AcceptCallClicked(this);
}

void AgDevice::
RejectCallClicked(void)
{
	m_ui->RejectCallClicked(this);
}

void AgDevice::
ReconnectDeviceClicked(void)
{
	if (!m_sess->IsConnecting() && !m_sess->IsConnected()) {
		m_sess->Connect();
		m_ui->UpdateButtons(this);
	}
}


int
main(int argc, char **argv)
{
	QApplication app(argc, argv);
	RealUI *rui;

	rui = new RealUI();

	if (!rui->Init()) {
		fprintf(stderr, "Init failed!\n");
		return 1;
	}

#if 0
	if (!rui->SoundCardOpen()) { abort(); }
	SoundIoPump *pumpp = new SoundIoPump(&g_qt_ei, rui->m_sio);
	pumpp->Loopback();
#if 0
	SoundIo *filep, *nullp;
	nullp = SoundIoCreateNull(&rui->m_soundio_format);
	filep = rui->OpenCallRecord(NULL);
	if (filep) {
		nullp = SoundIoAddDiverter(nullp, filep,
					   &rui->m_soundio_format);
	}
	pumpp->SetSecondary(nullp);
#endif
	return app.exec();
#endif

	if (argc >= 2) {
		if (!rui->Connect(argv[1])) {
			fprintf(stderr, "Connection setup failed!\n");
			return 1;
		}
	}

	app.setMainWidget(rui);
	rui->show();
	return app.exec();
}

#include "standalone.moc.cpp"
