/* -*- C++ -*- */
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

#if !defined(__HFPD_OBJECTS_H__)
#define __HFPD_OBJECTS_H__

#include <libhfp/list.h>
#include <libhfp/events.h>
#include <libhfp/hfp.h>
#include <libhfp/soundio.h>
#include "dbus.h"
#include "configfile.h"
#include "proto.h"


class HandsFree;
class SoundIoObj;
class ConfigHandler;


class HfpdExportObject : public DbusExportObject {
public:
	HfpdExportObject(const char *name, const DbusInterface *iface_tbl)
		: DbusExportObject(name, iface_tbl) {}

	const char *DbusErrorName(libhfp::ErrorInfo &error) const;
	bool SendReplyErrorInfo(DBusMessage *msgp, libhfp::ErrorInfo &error);
};

/*
 * AudioGateway is the underlying class to /net/sf/nohands/hfpd/<bdaddr>
 */

class AudioGateway : public HfpdExportObject {
	friend class HandsFree;
	friend class SoundIoObj;

	libhfp::ListItem		m_links;
	libhfp::HfpSession		*m_sess;
	char				*m_name_free;
	bool				m_known;
	bool				m_unbind_on_audio_close;

	AudioGatewayState		m_state;
	AudioGatewayCallState		m_call_state;
	AudioGatewayAudioState		m_audio_state;

	HandsFree			*m_hf;
	DbusPeerDisconnectNotifier	*m_owner;
	SoundIoObj			*m_audio_bind;

	void OwnerDisconnectNotify(DbusPeerDisconnectNotifier *notp);

	static const DbusInterface	s_ifaces[];

	bool UpdateState(AudioGatewayState st);
	bool UpdateCallState(AudioGatewayCallState st);
	bool UpdateAudioState(AudioGatewayAudioState st);

	void DoDisconnect(void);

	bool CreatePendingCommand(DBusMessage *msgp,
				  class AgPendingCommand *&agpendp);
	bool DoPendingCommand(class AgPendingCommand *agpendp,
			      libhfp::ErrorInfo &error,
			      libhfp::HfpPendingCommand *cmdp);

public:
	AudioGateway(HandsFree *hfp, libhfp::HfpSession *sessp, char *name);
	virtual ~AudioGateway();

	AudioGatewayState State(void);
	AudioGatewayCallState CallState(void);
	AudioGatewayAudioState AudioState(void);

	libhfp::SoundIo *GetSoundIo(void) const { return m_sess; }
	void Get(void) { m_sess->Get(); }
	void Put(void) { m_sess->Put(); }

	libhfp::DispatchInterface *GetDi(void) const
		{ return m_sess->GetDi(); }

	void DoSetKnown(bool known);

	/*
	 * NotifyXxx are callbacks invoked by HfpSession
	 * in response to Bluetooth events.
	 */
	void NotifyConnection(libhfp::HfpSession *sessp,
			      libhfp::ErrorInfo *reason);
	void NotifyCall(libhfp::HfpSession *sessp, bool act, bool waiting,
			bool ring);
	void NotifyIndicator(libhfp::HfpSession *sessp, const char *indname,
			     int val);
	void NotifyAudioConnection(libhfp::HfpSession *sessp,
				   libhfp::ErrorInfo *reason);
	void NotifyDestroy(libhfp::BtManaged *sessp);
	void NameResolved(void);

	/* D-Bus method handler methods */
	bool Connect(DBusMessage *msgp);
	bool Disconnect(DBusMessage *msgp);
	bool CloseAudio(DBusMessage *msgp);
	bool Dial(DBusMessage *msgp);
	bool Redial(DBusMessage *msgp);
	bool HangUp(DBusMessage *msgp);
	bool SendDtmf(DBusMessage *msgp);
	bool Answer(DBusMessage *msgp);
	bool CallDropHeldUdub(DBusMessage *msgp);
	bool CallSwapDropActive(DBusMessage *msgp);
	bool CallSwapHoldActive(DBusMessage *msgp);
	bool CallLink(DBusMessage *msgp);
	bool CallTransfer(DBusMessage *msgp);

	/* D-Bus Property related methods */
	bool GetState(DBusMessage *msgp, uint8_t &val);
	bool GetCallState(DBusMessage *msgp, uint8_t &val);
	bool GetAudioState(DBusMessage *msgp, uint8_t &val);
	bool GetClaimed(DBusMessage *msgp, bool &val);
	bool GetVoluntaryDisconnect(DBusMessage *msgp, bool &val);
	bool GetAddress(DBusMessage *msgp, const DbusProperty *propp,
			DBusMessageIter &mi);
	bool GetName(DBusMessage *msgp, const char * &val);
	bool GetKnown(DBusMessage *msgp, bool &val);
	bool SetKnown(DBusMessage *msgp, const bool &val, bool &doreply);
	bool GetAutoReconnect(DBusMessage *msgp, bool &val);
	bool SetAutoReconnect(DBusMessage *msgp, const bool &val,
			      bool &doreply);
	bool GetFeatures(DBusMessage *msgp, const DbusProperty *propp,
			 DBusMessageIter &mi);
	bool GetRawFeatures(DBusMessage *msgp, dbus_uint32_t &val);
};

#if defined(HFPD_AUDIOGATEWAY_DEFINE_INTERFACES)
static const DbusMethod g_AudioGateway_methods[] = {
	DbusMethodEntry(AudioGateway, Connect, "", ""),
	DbusMethodEntry(AudioGateway, Disconnect, "", ""),
	DbusMethodEntry(AudioGateway, CloseAudio, "", ""),
	DbusMethodEntry(AudioGateway, Dial, "s", ""),
	DbusMethodEntry(AudioGateway, Redial, "", ""),
	DbusMethodEntry(AudioGateway, HangUp, "", ""),
	DbusMethodEntry(AudioGateway, SendDtmf, "y", ""),
	DbusMethodEntry(AudioGateway, Answer, "", ""),
	DbusMethodEntry(AudioGateway, CallDropHeldUdub, "", ""),
	DbusMethodEntry(AudioGateway, CallSwapDropActive, "", ""),
	DbusMethodEntry(AudioGateway, CallSwapHoldActive, "", ""),
	DbusMethodEntry(AudioGateway, CallLink, "", ""),
	DbusMethodEntry(AudioGateway, CallTransfer, "", ""),
	{ 0, }
};

static const DbusMethod g_AudioGateway_signals[] = {
	DbusSignalEntry(StateChanged, "yb"),
	DbusSignalEntry(CallStateChanged, "y"),
	DbusSignalEntry(AudioStateChanged, "y"),
	DbusSignalEntry(ClaimStateChanged, "b"),
	DbusSignalEntry(Ring, "ss"),
	DbusSignalEntry(IndicatorChanged, "si"),
	DbusSignalEntry(NameResolved, "s"),
	{ 0, }
};

/*
 * We don't have the bdaddr stashed conveniently in a string
 * representation, so we use a raw get method.
 */
static const DbusProperty g_AudioGateway_properties[] = {
	DbusPropertyMarshallImmutable(uint8_t, State, AudioGateway,
				      GetState),
	DbusPropertyMarshallImmutable(uint8_t, CallState, AudioGateway,
				      GetCallState),
	DbusPropertyMarshallImmutable(uint8_t, AudioState, AudioGateway,
				      GetAudioState),
	DbusPropertyMarshallImmutable(bool, Claimed, AudioGateway,
				      GetClaimed),
	DbusPropertyMarshallImmutable(bool, VoluntaryDisconnect, AudioGateway,
				      GetVoluntaryDisconnect),
	DbusPropertyRawImmutable("s", Address, AudioGateway, GetAddress),
	DbusPropertyMarshallImmutable(const char *, Name, AudioGateway,
				      GetName),
	DbusPropertyMarshall(bool, Known, AudioGateway, GetKnown, SetKnown),
	DbusPropertyMarshall(bool, AutoReconnect, AudioGateway,
			     GetAutoReconnect, SetAutoReconnect),
	DbusPropertyRawImmutable("a{sb}", Features, AudioGateway, GetFeatures),
	DbusPropertyMarshallImmutable(dbus_uint32_t, RawFeatures, AudioGateway,
				      GetRawFeatures),
	{ 0, }
};

const DbusInterface AudioGateway::s_ifaces[] = {
	{ HFPD_AUDIOGATEWAY_INTERFACE_NAME,
	  g_AudioGateway_methods,
	  g_AudioGateway_signals,
	  g_AudioGateway_properties },
	{ 0, }
};
#endif /* defined(HFPD_AUDIOGATEWAY_DEFINE_INTERFACES) */



/*
 * HandsFree is the underlying class to /net/sf/nohands/hfpd
 */

class HandsFree : public HfpdExportObject {
public:
	static const DbusInterface	s_ifaces[];

	libhfp::ListItem		m_gateways;

	libhfp::DispatchInterface	*m_di;
	DbusSession			*m_dbus;
	libhfp::BtHub			*m_hub;
	libhfp::HfpService		*m_hfp;

	SoundIoObj			*m_sound;
	bool				m_inquiry_state;
	bool				m_accept_unknown;
	bool				m_voice_persist;
	bool				m_voice_autoconnect;

	ConfigHandler			*m_config;

	HandsFree(libhfp::DispatchInterface *dip, DbusSession *dbusp);
	~HandsFree();

	libhfp::DispatchInterface *GetDi(void) const { return m_di; }

	bool Init(const char *cfgfile);
	void Cleanup(void);
	bool SaveConfig(libhfp::ErrorInfo *error = 0, bool force = false);
	void LoadDeviceConfig(void);

	void LogMessage(libhfp::DispatchInterface::logtype_t lt,
			const char *msg);

	AudioGateway *GetAudioGateway(libhfp::HfpSession *sessp) {
		return !sessp ? 0 :
			(sessp->GetPrivate()
			 ? (AudioGateway *) sessp->GetPrivate()
			 : 0);
	}

	AudioGateway *FindAudioGateway(const char *agpath);

	libhfp::BtDevice *DeviceFactory(bdaddr_t const &addr);
	libhfp::HfpSession *SessionFactory(libhfp::BtDevice *devp);

	void DoStarted(void);
	void DoStopped(void);

	void NotifySystemState(libhfp::ErrorInfo *reason);
	void NotifyInquiryResult(libhfp::BtDevice *devp,
				 libhfp::ErrorInfo *error);
	void NotifyNameResolved(libhfp::BtDevice *devp, const char *name,
				libhfp::ErrorInfo *reason);

	/* D-Bus method handler methods */
	bool SaveSettings(DBusMessage *msgp);
	bool Start(DBusMessage *msgp);
	bool Stop(DBusMessage *msgp);
	bool StartInquiry(DBusMessage *msgp);
	bool StopInquiry(DBusMessage *msgp);
	bool GetName(DBusMessage *msgp);
	bool AddDevice(DBusMessage *msgp);
	bool RemoveDevice(DBusMessage *msgp);

	/* Property related methods */
	bool GetVersion(DBusMessage *msgp, dbus_uint32_t &val);
	bool GetAutoSave(DBusMessage *msgp, bool &val);
	bool SetAutoSave(DBusMessage *msgp, const bool &val, bool &doreply);
	bool GetSaveFile(DBusMessage *msgp, const char * &val);
	bool SetSaveFile(DBusMessage *msgp, const char * const &val,
			 bool &accept);
	bool GetSystemState(DBusMessage *msgp, bool &val);
	bool GetAutoRestart(DBusMessage *msgp, bool &val);
	bool SetAutoRestart(DBusMessage *msgp, const bool &val, bool &doreply);
	bool GetSecMode(DBusMessage *msgp, uint8_t &val);
	bool SetSecMode(DBusMessage *msgp, const uint8_t &val,
			bool &doreply);
	bool GetAcceptUnknown(DBusMessage *msgp, bool &val);
	bool SetAcceptUnknown(DBusMessage *msgp, const bool &val,
			      bool &doreply);
	bool GetScoEnabled(DBusMessage *msgp, bool &val);
	bool SetScoEnabled(DBusMessage *msgp, const bool &val, bool &doreply);
	bool GetVoicePersist(DBusMessage *msgp, bool &val);
	bool SetVoicePersist(DBusMessage *msgp, const bool &val,
			     bool &doreply);
	bool GetVoiceAutoConnect(DBusMessage *msgp, bool &val);
	bool SetVoiceAutoConnect(DBusMessage *msgp, const bool &val,
			     bool &doreply);
	bool GetAudioGateways(DBusMessage *msgp, const DbusProperty *propp,
			      DBusMessageIter &mi);
	bool GetReportCapabilities(DBusMessage *msgp, dbus_uint32_t &val);
	bool SetReportCapabilities(DBusMessage *msgp, const dbus_uint32_t &val,
				   bool &doreply);
	bool GetServiceName(DBusMessage *msgp, const char * &val);
	bool SetServiceName(DBusMessage *msgp, const char * const &val,
			    bool &accept);
	bool GetServiceDesc(DBusMessage *msgp, const char * &val);
	bool SetServiceDesc(DBusMessage *msgp, const char * const &val,
			    bool &accept);
};

#if defined(HFPD_HANDSFREE_DEFINE_INTERFACES)
static const DbusMethod g_HandsFree_methods[] = {
	DbusMethodEntry(HandsFree, Start, "", ""),
	DbusMethodEntry(HandsFree, Stop, "", ""),
	DbusMethodEntry(HandsFree, StartInquiry, "", ""),
	DbusMethodEntry(HandsFree, StopInquiry, "", ""),
	DbusMethodEntry(HandsFree, GetName, "s", "s"),
	DbusMethodEntry(HandsFree, AddDevice, "sb", "o"),
	DbusMethodEntry(HandsFree, RemoveDevice, "s", ""),
	DbusMethodEntry(HandsFree, SaveSettings, "", ""),
	{ 0, }
};

static const DbusMethod g_HandsFree_signals[] = {
	DbusSignalEntry(SystemStateChanged, "b"),
	DbusSignalEntry(InquiryStateChanged, "b"),
	DbusSignalEntry(InquiryResult, "su"),
	DbusSignalEntry(AudioGatewayAdded, "o"),
	DbusSignalEntry(AudioGatewayRemoved, "o"),
	DbusSignalEntry(LogMessage, "us"),
	{ 0, }
};

static const DbusProperty g_HandsFree_properties[] = {
	DbusPropertyMarshallImmutable(dbus_uint32_t, Version, HandsFree,
				      GetVersion),
	DbusPropertyMarshall(bool, AutoSave, HandsFree,
			     GetAutoSave, SetAutoSave),
	DbusPropertyMarshall(const char *, SaveFile, HandsFree,
			     GetSaveFile, SetSaveFile),
	DbusPropertyMarshallImmutable(bool, SystemState, HandsFree,
				      GetSystemState),
	DbusPropertyMarshall(bool, AutoRestart, HandsFree,
			     GetAutoRestart, SetAutoRestart),
	DbusPropertyMarshall(uint8_t, SecMode, HandsFree,
			     GetSecMode, SetSecMode),
	DbusPropertyMarshall(bool, AcceptUnknown, HandsFree,
			     GetAcceptUnknown, SetAcceptUnknown),
	DbusPropertyMarshall(bool, ScoEnabled, HandsFree,
			     GetScoEnabled, SetScoEnabled),
	DbusPropertyMarshall(bool, VoicePersist, HandsFree,
			     GetVoicePersist, SetVoicePersist),
	DbusPropertyMarshall(bool, VoiceAutoConnect, HandsFree,
			     GetVoiceAutoConnect, SetVoiceAutoConnect),
	DbusPropertyRawImmutable("ao", AudioGateways, HandsFree,
				 GetAudioGateways),
	DbusPropertyMarshall(dbus_uint32_t, ReportCapabilities, HandsFree,
			     GetReportCapabilities, SetReportCapabilities),
	DbusPropertyMarshall(const char *, ServiceName, HandsFree,
			     GetServiceName, SetServiceName),
	DbusPropertyMarshall(const char *, ServiceDesc, HandsFree,
			     GetServiceDesc, SetServiceDesc),
	{ 0, }
};

const DbusInterface HandsFree::s_ifaces[] = {
	{ HFPD_HANDSFREE_INTERFACE_NAME,
	  g_HandsFree_methods,
	  g_HandsFree_signals,
	  g_HandsFree_properties },
	{ 0, }
};
#endif /* defined(HFPD_HANDSFREE_DEFINE_INTERFACES) */


/*
 * SoundIoObj is the underlying class to /net/sf/nohands/hfpd/soundio
 */

class SoundIoObj : public HfpdExportObject {
public:
	static const DbusInterface	s_ifaces[];

	HandsFree			*m_hf;
	libhfp::SoundIoManager		*m_sound;
	SoundIoState			m_state;
	SoundIoState			m_state_sent;
	libhfp::SoundIo			*m_ringtone;
	libhfp::SoundIoFltSpeex		*m_sigproc;
	libhfp::SoundIoSpeexProps	m_procprops;

	libhfp::SoundIo			*m_membuf;
	libhfp::sio_sampnum_t		m_membuf_size;

	ConfigHandler			*m_config;

	AudioGateway			*m_bound_ag;

	libhfp::SoundIoFilter		*m_snoop;
	libhfp::SoundIo			*m_snoop_ep;
	char				*m_snoop_filename;

	DbusPeerDisconnectNotifier	*m_state_owner;

	libhfp::DispatchInterface *GetDi(void) const { return m_hf->GetDi(); }


	SoundIoObj(HandsFree *hfp);
	~SoundIoObj();

	bool SaveConfig(libhfp::ErrorInfo *error = 0, bool force = false)
		{ return m_hf->SaveConfig(error, force); }

	bool Init(DbusSession *dbusp);
	void Cleanup(void);
	void CleanupSnoop(void);
	bool UpdateState(SoundIoState st, libhfp::ErrorInfo *reason = 0);

	bool SetupStateOwner(DBusMessage *msgp);
	void StateOwnerDisconnectNotify(DbusPeerDisconnectNotifier *notp);

	/*
	 * These internal methods connect the SoundIoManager to a
	 * secondary endpoint and start it.
	 */
	void EpRelease(SoundIoState st = HFPD_SIO_INVALID,
		       libhfp::ErrorInfo *reason = 0);
	bool EpAudioGateway(AudioGateway *agp, bool can_connect,
			    libhfp::ErrorInfo *error);
	bool EpAudioGatewayComplete(AudioGateway *agp,
				    libhfp::ErrorInfo *error);
	bool EpFile(const char *filename, bool writing,
		    libhfp::ErrorInfo *error);
	bool EpLoopback(libhfp::ErrorInfo *error);
	bool EpMembuf(bool in, bool out, libhfp::SoundIoFilter *fltp,
		      libhfp::ErrorInfo *error);

	void NotifySoundStop(libhfp::SoundIoManager *mgrp,
			     libhfp::ErrorInfo &error);
	void NotifySkew(libhfp::SoundIoManager *mgrp,
			libhfp::sio_stream_skewinfo_t reason, double value);

	/* D-Bus SoundIo interface related methods */
	bool SetDriver(DBusMessage *msgp);
	bool ProbeDevices(DBusMessage *msgp);
	bool Stop(DBusMessage *msgp);
	bool AudioGatewayStart(DBusMessage *msgp);
	bool FileStart(DBusMessage *msgp);
	bool LoopbackStart(DBusMessage *msgp);
	bool MembufClear(DBusMessage *msgp);
	bool MembufStart(DBusMessage *msgp);
	bool SetSnoopFile(DBusMessage *msgp);

	/* D-Bus SoundIo property related methods */
	bool GetState(DBusMessage *msgp, uint8_t &val);
	bool GetAudioGateway(DBusMessage *msgp, const DbusProperty *propp,
			     DBusMessageIter &mi);
	bool GetMute(DBusMessage *msgp, bool &val);
	bool SetMute(DBusMessage *msgp, const bool &val, bool &doreply);
	bool GetSnoopFileName(DBusMessage *msgp, const char * &val);
	bool GetDrivers(DBusMessage *msgp, const DbusProperty *propp,
			     DBusMessageIter &mi);
	bool GetDriverName(DBusMessage *msgp, const char * &val);
	bool GetDriverOpts(DBusMessage *msgp, const char * &val);

	bool GetPacketInterval(DBusMessage *msgp, dbus_uint32_t &val);
	bool GetMinBufferFill(DBusMessage *msgp, dbus_uint32_t &val);
	bool GetJitterWindow(DBusMessage *msgp, dbus_uint32_t &val);

	bool GetPacketIntervalHint(DBusMessage *msgp, dbus_uint32_t &val);
	bool SetPacketIntervalHint(DBusMessage *msgp,
					const dbus_uint32_t &val,
					bool &doreply);
	bool GetMinBufferFillHint(DBusMessage *msgp, dbus_uint32_t &val);
	bool SetMinBufferFillHint(DBusMessage *msgp,
				       const dbus_uint32_t &val,
				       bool &doreply);
	bool GetJitterWindowHint(DBusMessage *msgp, dbus_uint32_t &val);
	bool SetJitterWindowHint(DBusMessage *msgp,
				      const dbus_uint32_t &val, bool &doreply);
	bool GetDenoise(DBusMessage *msgp, bool &val);
	bool SetDenoise(DBusMessage *msgp, const bool &val, bool &doreply);
	bool GetAutoGain(DBusMessage *msgp, dbus_uint32_t &val);
	bool SetAutoGain(DBusMessage *msgp, const dbus_uint32_t &val,
			 bool &doreply);
	bool GetEchoCancelTail(DBusMessage *msgp, dbus_uint32_t &val);
	bool SetEchoCancelTail(DBusMessage *msgp, const dbus_uint32_t &val,
			       bool &doreply);
	bool GetDereverbLevel(DBusMessage *msgp, float &val);
	bool SetDereverbLevel(DBusMessage *msgp, const float &val,
			       bool &doreply);
	bool GetDereverbDecay(DBusMessage *msgp, float &val);
	bool SetDereverbDecay(DBusMessage *msgp, const float &val,
			       bool &doreply);
};


#if defined(HFPD_SOUNDIO_DEFINE_INTERFACES)
static const DbusMethod g_SoundIo_methods[] = {
	DbusMethodEntry(SoundIoObj, SetDriver, "ss", ""),
	DbusMethodEntry(SoundIoObj, ProbeDevices, "s", "a(ss)"),
	DbusMethodEntry(SoundIoObj, Stop, "", ""),
	DbusMethodEntry(SoundIoObj, AudioGatewayStart, "ob", ""),
	DbusMethodEntry(SoundIoObj, FileStart, "sb", ""),
	DbusMethodEntry(SoundIoObj, LoopbackStart, "", ""),
	DbusMethodEntry(SoundIoObj, MembufStart, "bbuu", ""),
	DbusMethodEntry(SoundIoObj, MembufClear, "", ""),
	DbusMethodEntry(SoundIoObj, SetSnoopFile, "sbb", ""),
	{ 0, }
};

static const DbusMethod g_SoundIo_signals[] = {
	DbusSignalEntry(AudioGatewaySet, "o"),
	DbusSignalEntry(StateChanged, "y"),
	DbusSignalEntry(StreamAborted, "ss"),
	DbusSignalEntry(MuteChanged, "b"),
	DbusSignalEntry(SkewNotify, "yd"),
	DbusSignalEntry(MonitorNotify, "uq"),
	{ 0, }
};

static const DbusProperty g_SoundIo_properties[] = {
	DbusPropertyMarshallImmutable(uint8_t, State, SoundIoObj,
				      GetState),
	DbusPropertyRawImmutable("v", AudioGateway, SoundIoObj,
				 GetAudioGateway),
	DbusPropertyMarshall(bool, Mute, SoundIoObj, GetMute, SetMute),
	DbusPropertyMarshallImmutable(const char *, SnoopFileName, SoundIoObj,
				      GetSnoopFileName),
	DbusPropertyRawImmutable("a(ss)", Drivers, SoundIoObj,
				 GetDrivers),
	DbusPropertyMarshallImmutable(const char *, DriverName, SoundIoObj,
				      GetDriverName),
	DbusPropertyMarshallImmutable(const char *, DriverOpts, SoundIoObj,
				      GetDriverOpts),
	DbusPropertyMarshallImmutable(dbus_uint32_t, PacketInterval,
				      SoundIoObj, GetPacketInterval),
	DbusPropertyMarshallImmutable(dbus_uint32_t, MinBufferFill,
				      SoundIoObj, GetMinBufferFill),
	DbusPropertyMarshallImmutable(dbus_uint32_t, JitterWindow,
				      SoundIoObj, GetJitterWindow),
	DbusPropertyMarshall(dbus_uint32_t, PacketIntervalHint, SoundIoObj,
			     GetPacketIntervalHint, SetPacketIntervalHint),
	DbusPropertyMarshall(dbus_uint32_t, MinBufferFillHint, SoundIoObj,
			     GetMinBufferFillHint, SetMinBufferFillHint),
	DbusPropertyMarshall(dbus_uint32_t, JitterWindowHint, SoundIoObj,
			     GetJitterWindowHint, SetJitterWindowHint),
#if defined(USE_SPEEXDSP)
	DbusPropertyMarshall(bool, Denoise, SoundIoObj,
			     GetDenoise, SetDenoise),
	DbusPropertyMarshall(dbus_uint32_t, AutoGain, SoundIoObj,
			     GetAutoGain, SetAutoGain),
	DbusPropertyMarshall(dbus_uint32_t, EchoCancelTail, SoundIoObj,
			     GetEchoCancelTail, SetEchoCancelTail),
	DbusPropertyMarshall(float, DereverbLevel, SoundIoObj,
			     GetDereverbLevel, SetDereverbLevel),
	DbusPropertyMarshall(float, DereverbDecay, SoundIoObj,
			     GetDereverbDecay, SetDereverbDecay),
#endif /* defined(USE_SPEEXDSP) */
	{ 0, }
};

const DbusInterface SoundIoObj::s_ifaces[] = {
	{ HFPD_SOUNDIO_INTERFACE_NAME,
	  g_SoundIo_methods,
	  g_SoundIo_signals,
	  g_SoundIo_properties },
	{ 0, }
};
#endif /* defined(HFPD_SOUNDIO_DEFINE_INTERFACES) */



#endif /* !defined(__HFPD_OBJECTS_H__) */
