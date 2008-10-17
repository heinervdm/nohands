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

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#include <libhfp/soundio.h>
#include <libhfp/soundio-buf.h>

#include "oplatency.h"

namespace libhfp {

/*
 * Below is the list of drivers recognized by SoundIoManager, and
 * the factory function for each.
 *
 * Drivers are identified by a case-insensitive string.  The factory
 * function is provided an options string.  The factory function must
 * choose reasonable defaults if no options string is provided.
 */

typedef SoundIo *(*sound_driver_factory_t)(DispatchInterface *, const char *);

struct SoundIoDriver {
	const char *name;
	const char *descr;
	sound_driver_factory_t factory;
};

static SoundIoDriver sound_drivers[] = {
#if defined(USE_ALSA_SOUNDIO)
	{ "ALSA",
	  "Advanced Linux Sound Architecture back-end",
	  SoundIoCreateAlsa },
#endif
#if defined(USE_OSS_SOUNDIO)
	{ "OSS",
	  "Open Sound System back-end (deprecated)",
	  SoundIoCreateOss },
#endif
	{ 0 }
};


/*
 * SoundIoLoop and SoundIoNull are used by the SoundIoManager
 * class to support loopback and mute-while-streaming.
 */

class SoundIoLoop : public SoundIo {
public:
	SoundIoFormat		m_fmt;
	VarBuf			m_buf;

	SoundIoLoop(void) {}
	virtual ~SoundIoLoop() { SndClose(); }

	virtual bool SndOpen(bool play, bool capture) { return true; }
	virtual void SndClose(void) {
		m_buf.FreeBuffer();
	}

	virtual void SndGetFormat(SoundIoFormat &format) const {
		format = m_fmt;
	}

	virtual bool SndSetFormat(SoundIoFormat &format) {
		m_fmt = format;
		return true;
	}

	virtual void SndGetProps(SoundIoProps &props) const {
		props.has_clock = false;
		props.does_source = true;
		props.does_sink = true;
		props.does_loop = true;
		props.remove_on_exhaust = false;
		props.outbuf_size = m_buf.m_size;
	}

	virtual void SndGetIBuf(SoundIoBuffer &fillme) {
		if (!fillme.m_size ||
		    (fillme.m_size > (m_buf.SpaceUsed() /
				      m_fmt.bytes_per_record))) {
			if (!m_buf.SpaceUsed()) {
				/* Let the pump silence-pad it */
				fillme.m_size = 0;
				return;
			}

			fillme.m_size = m_buf.SpaceUsed() /
				m_fmt.bytes_per_record;
		}
		fillme.m_data = m_buf.GetStart();
	}
	virtual void SndDequeueIBuf(sio_sampnum_t samps) {
		if (samps > (m_buf.SpaceUsed() / m_fmt.bytes_per_record)) {
			assert(!m_buf.SpaceUsed());
			return;
		}
		m_buf.m_start += (samps * m_fmt.bytes_per_record);
		assert(m_buf.m_start <= m_buf.m_end);
	}
	virtual void SndGetOBuf(SoundIoBuffer &fillme) {
		int nbytes;
		if (!m_buf.m_size) {
			(void) m_buf.AllocateBuffer(m_fmt.packet_samps *
						    m_fmt.bytes_per_record *
						    16);
		}
		if (!fillme.m_size ||
		    (fillme.m_size > (m_buf.SpaceFree() /
				      m_fmt.bytes_per_record))) {
			fillme.m_size = (m_buf.SpaceFree() /
					 m_fmt.bytes_per_record);
		}
		nbytes = fillme.m_size * m_fmt.bytes_per_record;
		fillme.m_data = m_buf.GetSpace(nbytes);
	}
	virtual void SndQueueOBuf(sio_sampnum_t samps) {
		m_buf.m_end += (samps * m_fmt.bytes_per_record);
		assert(m_buf.m_end <= m_buf.m_size);
	}
	virtual void SndGetQueueState(SoundIoQueueState &qs) {
		qs.in_queued = m_buf.SpaceUsed() / m_fmt.bytes_per_record;
		qs.out_queued = 0;
	}
	virtual bool SndAsyncStart(bool, bool) { return false; }
	virtual void SndAsyncStop(void) {}
	virtual bool SndIsAsyncStarted(void) const { return false; }
};

class SoundIoNull : public SoundIo {
public:
	SoundIoFormat		m_fmt;

	SoundIoNull() {}
	virtual ~SoundIoNull() { SndClose(); }

	virtual bool SndOpen(bool play, bool capture) { return true; }
	virtual void SndClose(void) {}

	virtual void SndGetProps(SoundIoProps &props) const {
		props.has_clock = false;
		props.does_source = true;
		props.does_sink = true;
		props.does_loop = false;
		props.remove_on_exhaust = false;
		props.outbuf_size = 0;
	}

	virtual void SndGetFormat(SoundIoFormat &format) const {
		format = m_fmt;
	}

	virtual bool SndSetFormat(SoundIoFormat &format) {
		m_fmt = format;
		return true;
	}

	virtual void SndGetIBuf(SoundIoBuffer &fillme) {
		fillme.m_size = 0;
	}
	virtual void SndDequeueIBuf(sio_sampnum_t) { abort(); }
	virtual void SndGetOBuf(SoundIoBuffer &fillme) {
		fillme.m_size = 0;
	}
	virtual void SndQueueOBuf(sio_sampnum_t) { abort(); }
	virtual void SndGetQueueState(SoundIoQueueState &qs) {
		qs.in_queued = 0;
		qs.out_queued = 0;
	}
	virtual bool SndAsyncStart(bool, bool) { return false; }
	virtual void SndAsyncStop(void) {}
	virtual bool SndIsAsyncStarted(void) const { return false; }
};


class SoundIoFltMute : public SoundIoFilter {
	bool		m_mute_dn, m_mute_up;
	bool		m_init_dn, m_init_up;
	int		m_bpr, m_pktsize;
	uint8_t		*m_silence_dn, *m_silence_up;

public:
	virtual bool FltPrepare(SoundIoFormat const &fmt, bool up, bool dn) {
		m_bpr = fmt.bytes_per_record;
		m_pktsize = fmt.packet_samps;

		m_init_dn = false;
		m_init_up = false;

		if (dn && m_mute_dn) {
			m_silence_dn = (uint8_t *) malloc(m_pktsize * m_bpr);
			if (!m_silence_dn)
				return false;
		}
		if (up && m_mute_up) {
			m_silence_up = (uint8_t *) malloc(m_pktsize * m_bpr);
			if (!m_silence_up) {
				FltCleanup();
				return false;
			}
		}

		return true;
	}

	virtual void FltCleanup(void) {
		if (m_silence_dn) {
			free(m_silence_dn);
			m_silence_dn = 0;
		}
		if (m_silence_up) {
			free(m_silence_up);
			m_silence_up = 0;
		}
	}

	virtual SoundIoBuffer const *FltProcess(bool up,
						SoundIoBuffer const &src,
						SoundIoBuffer &dest) {
		uint8_t *buf, *end;
		uint8_t *silence = 0;

		if (up) {
			if (!m_mute_up)
				return &src;
			silence = m_silence_up;
			if (!m_init_up) {
				end = &silence[m_bpr * m_pktsize];
				for (buf = silence; buf < end; buf += m_bpr)
					memcpy(buf, src.m_data, m_bpr);
				m_init_up = true;
			}
		} else {
			if (!m_mute_dn)
				return &src;
			silence = m_silence_dn;
			if (!m_init_dn) {
				end = &silence[m_bpr * m_pktsize];
				for (buf = silence; buf < end; buf += m_bpr)
					memcpy(buf, src.m_data, m_bpr);
				m_init_dn = true;
			}
		}

		memcpy(dest.m_data, silence, m_bpr * m_pktsize);
		return &dest;
	}

	SoundIoFltMute(bool mute_up = false, bool mute_dn = true)
		: m_mute_dn(mute_dn), m_mute_up(mute_up),
		  m_silence_dn(0), m_silence_up(0) {
		assert(mute_up || mute_dn);
	}
	virtual ~SoundIoFltMute() {}
};


SoundIoManager::
SoundIoManager(DispatchInterface *di)
	: m_pump(di, 0), m_config_packet_ms(0), m_primary(0),
	  m_mute_swap(false), m_mute_soft_up(false), m_mute_soft_dn(false),
	  m_mute_soft(0), m_top_loop(false), m_primary_open(false),
	  m_dsp(0), m_dsp_installed(false),
	  m_driver_name(0), m_driver_opts(0)
{
	m_pump.cb_NotifyAsyncState.Register(this,
					    &SoundIoManager::PumpStopped);
}

SoundIoManager::
~SoundIoManager()
{
	if (IsStarted())
		Stop();
	if (m_primary)
		delete m_primary;
	SetSecondary(0);
	if (m_driver_name)
		free(m_driver_name);
	if (m_driver_opts)
		free(m_driver_opts);
}

void SoundIoManager::
PumpStopped(SoundIoPump *pumpp, SoundIo *offender)
{
	assert(m_primary);

	if (offender == m_primary)
		ClosePrimary();

	if (cb_NotifyAsyncState.Registered())
		cb_NotifyAsyncState(this);

	/*
	 * If our client didn't manage to restart the pump, and
	 * the primary wasn't closed above, close it.
	 */
	if (!IsStarted() && m_primary_open)
		ClosePrimary();
}


/* This is only non-static for GetDi() */
SoundIo *SoundIoManager::
CreatePrimary(const char *name, const char *opts)
{
	sound_driver_factory_t factory = 0;
	const char *use_opts;
	int i;

	assert(name || !opts);

	if (!sound_drivers[0].name) {
		GetDi()->LogWarn("SoundIo: No drivers registered\n");
		return 0;
	}

	use_opts = 0;
	if (name) {
		for (i = 0; sound_drivers[i].name; i++) {
			if (!strcasecmp(name, sound_drivers[i].name)) {
				factory = sound_drivers[i].factory;
				use_opts = opts;
				assert(factory);
				break;
			}
		}
	}

	if (!factory) {
		if (name)
			GetDi()->LogWarn("SoundIo: unknown driver \"%s\","
					 "using default \"%s\"\n",
					 name, sound_drivers[0].name);

		factory = sound_drivers[0].factory;
		assert(factory);
	}

	return factory(GetDi(), use_opts);
}


bool SoundIoManager::
GetDriverInfo(int index, const char **name, const char **desc)
{
	if ((index < 0) ||
	    (index >= (int) (sizeof(sound_drivers)/sizeof(sound_drivers[0]))))
		return false;

	if (!sound_drivers[index].name)
		return false;

	if (name)
		*name = sound_drivers[index].name;
	if (desc)
		*desc = sound_drivers[index].descr;
	return true;
}

bool SoundIoManager::
SetDriver(const char *drivername, const char *driveropts)
{
	SoundIo *driverp = 0;
	char *nd = 0, *od = 0;
	bool res;

	assert(!IsStarted());

	if (drivername && !drivername[0])
		drivername = 0;
	if (driveropts && !driveropts[0])
		driveropts = 0;
	assert(drivername || !driveropts);

	if (drivername) {
		nd = strdup(drivername);
		if (!nd)
			return false;
	}
	if (driveropts) {
		od = strdup(driveropts);
		if (!od)
			goto failed;
	}

	driverp = CreatePrimary(nd, od);
	if (!driverp)
		goto failed;

	res = m_pump.SetBottom(driverp);
	assert(res);

	if (m_primary)
		ClosePrimary();
	m_primary = driverp;
	m_primary_open = false;
	m_stream_up = false;
	m_stream_dn = false;

	if (m_driver_name)
		free(m_driver_name);
	m_driver_name = nd;
	if (m_driver_opts)
		free(m_driver_opts);
	m_driver_opts = od;
	return true;

failed:
	if (driverp)
		delete driverp;
	if (nd)
		free(nd);
	if (od)
		free(od);
	return false;
}

bool SoundIoManager::
TestOpen(bool up, bool down)
{
	SoundIoProps secprops;

	if (!m_primary) {
		GetDi()->LogDebug("SoundIo: no driver set, using default\n");
		if (!SetDriver(NULL, NULL))
			return false;
	}

	if (m_primary_open)
		return true;

	if (!up && !down) {
		if (m_pump.GetTop()) {
			m_pump.GetTop()->SndGetProps(secprops);
			up = secprops.does_sink;
			down = secprops.does_source;
		} else {
			up = true;
			down = true;
		}
	}

	if (!OpenPrimary(down, up))
		return false;

	m_primary->SndClose();
	return true;
}

bool SoundIoManager::
SetSecondary(SoundIo *secp)
{
	SoundIoFormat fmt;
	SoundIo *oldtop;

	oldtop = m_pump.GetTop();

	if (IsStarted()) {
		assert(m_primary);
		m_primary->SndGetFormat(fmt);
		secp->SndSetFormat(fmt);
	}

	if (!m_pump.SetTop(secp))
		return false;

	if (m_top_loop) {
		delete oldtop;
		m_top_loop = false;
	}

	return true;
}

bool SoundIoManager::
Loopback(void)
{
	SoundIoFormat fmt;
	SoundIo *siop;

	if (m_top_loop)
		return true;

	siop = new SoundIoLoop;
	if (!siop)
		return false;

	if (IsStarted()) {
		if (m_mute_swap) {
			GetDi()->LogWarn("SoundIo: loopback mute mode "
					 "is pointless\n");
			return false;
		}
		m_primary->SndGetFormat(fmt);
		siop->SndSetFormat(fmt);
	}
	if (!m_pump.SetTop(siop)) {
		delete siop;
		return false;
	}

	m_top_loop = true;
	return true;
}

bool SoundIoManager::
SetHardMute(bool state, bool closepri)
{
	SoundIoProps props;
	SoundIoFormat fmt;
	SoundIo *siop;
	bool res;

	if (m_mute_swap == state)
		return true;

	if (!state) {
		siop = m_pump.GetBottom();
		assert(siop);

		/* m_primary_open should always be false */
		if (IsStarted() && !m_primary_open) {
			assert(m_stream_dn || m_stream_up);
			if (!OpenPrimary(m_stream_dn, m_stream_up)) {
				GetDi()->LogWarn("SoundIo: could not open "
						 "primary for unmute\n");
				return false;
			}
			m_primary_open = true;
		}

		if (!m_pump.SetBottom(m_primary)) {
			GetDi()->LogWarn("Could not reinstall primary "
					 "as bottom endpoint\n");
			return false;
		}

		delete siop;
		m_mute_swap = false;
		return true;
	}

	if (IsStarted() && m_top_loop) {
		GetDi()->LogWarn("SoundIo: loopback mute mode is pointless\n");
		return false;
	}

	assert(m_pump.GetBottom() == m_primary);
	siop = new SoundIoNull();
	if (!siop)
		return false;

	if (IsStarted()) {
		m_pump.GetTop()->SndGetFormat(fmt);
		res = siop->SndSetFormat(fmt);
		assert(res);
	}

	if (!m_pump.SetBottom(siop)) {
		/* This can only mean the secondary is unclocked */
		assert(m_pump.IsStarted());
		m_pump.GetTop()->SndGetProps(props);
		assert(!props.has_clock);
		Stop();
		return true;
	}

	/* Close the primary without clearing m_stream_up, m_stream_dn */
	if (closepri && m_primary_open) {
		OpLatencyMonitor lm(GetDi(), "primary close");
		m_primary->SndClose();
		m_primary_open = false;
	}

	m_mute_swap = true;
	return true;
}

bool SoundIoManager::
SetMute(bool up, bool dn)
{
	SoundIoFilter *fltp;

	if ((up == m_mute_soft_up) && (dn == m_mute_soft_dn))
		return true;

	if (m_mute_soft) {
		fltp = m_pump.RemoveTop();
		assert(fltp == m_mute_soft);
		delete m_mute_soft;
		m_mute_soft = 0;
	}

	if (up || dn) {
		fltp = new SoundIoFltMute(up, dn);
		if (!fltp)
			return false;

		if (!m_pump.AddTop(fltp)) {
			delete fltp;
			return false;
		}
		m_mute_soft = fltp;
	}

	m_mute_soft_up = up;
	m_mute_soft_dn = dn;
	return true;
}

bool SoundIoManager::
SetDsp(SoundIoFilter *dspp)
{
	bool do_install = false;
	SoundIoFilter *fltp;
	if (m_dsp) {
		if (m_dsp_installed) {
			do_install = true;
			fltp = m_pump.RemoveBottom();
			assert(fltp == m_dsp);
			m_dsp_installed = false;
		}
		m_dsp = 0;
	}

	if (do_install) {
		if (!m_pump.AddBottom(dspp))
			return false;
		m_dsp_installed = true;
	}
	m_dsp = dspp;
	return true;
}

bool SoundIoManager::
OpenPrimary(bool sink, bool source)
{
	OpLatencyMonitor lm(GetDi(), "primary open");
	return m_primary->SndOpen(sink, source);
}

void SoundIoManager::
ClosePrimary(void)
{
	if (m_primary_open) {
		OpLatencyMonitor lm(GetDi(), "primary close");
		assert(m_primary);
		m_primary->SndClose();
		m_primary_open = false;
		m_stream_up = false;
		m_stream_dn = false;
	}

	if (m_dsp_installed) {
		SoundIoFilter *fltp;
		fltp = m_pump.RemoveBottom();
		assert(fltp == m_dsp);
		m_dsp_installed = false;
	}
}

bool SoundIoManager::
Start(bool up, bool down)
{
	SoundIoProps secprops;
	SoundIoFormat fmt;
	assert(!IsStarted());
	sio_sampnum_t pkt;
	bool was_open, res;

	if (IsStarted())
		return false;

	if (!m_primary) {
		GetDi()->LogDebug("SoundIo: no driver set, using default\n");
		if (!SetDriver(NULL, NULL))
			return false;
	}

	if (!m_pump.GetTop() && !Loopback()) {
		GetDi()->LogWarn("SoundIo: could not create loopback\n");
		return false;
	}

	if (m_top_loop && m_mute_swap) {
		GetDi()->LogWarn("SoundIo: loopback mute mode is pointless\n");
		return false;
	}

	if (!up && !down) {
		m_pump.GetTop()->SndGetProps(secprops);
		up = secprops.does_sink;
		down = secprops.does_source;
	}

	/* Make sure that the primary is open in a compatible mode */
	if (m_primary_open &&
	    ((m_stream_up != up) || (m_stream_dn != down)))
		ClosePrimary();

	was_open = m_primary_open;
	if (!m_mute_swap && !m_primary_open) {
		if (!OpenPrimary(down, up)) {
			GetDi()->LogWarn("SoundIo: could not open primary\n");
			return false;
		}
		m_primary_open = true;
	}

	if (m_dsp) {
		if (!m_top_loop && !m_mute_swap && !m_dsp_installed) {
			res = m_pump.AddBottom(m_dsp);
			assert(res);
			m_dsp_installed = true;
		}
		else if ((m_top_loop || m_mute_swap) && m_dsp_installed) {
			SoundIoFilter *fltp;
			fltp = m_pump.RemoveBottom();
			assert(fltp == m_dsp);
			m_dsp_installed = false;
		}
	}

	if (m_top_loop) {
		m_primary->SndGetFormat(fmt);
		m_pump.GetTop()->SndSetFormat(fmt);

		/* HACK: whack the loopback device buffer */
		m_pump.GetTop()->SndClose();

	} else {
		m_pump.GetTop()->SndGetFormat(fmt);
	}

	if (m_config_packet_ms) {
		pkt = (m_config_packet_ms * fmt.samplerate) / 1000;
		if (!pkt) {
			GetDi()->LogWarn("Configured packet size (%d) is "
					 "too small\n", m_config_packet_ms);
		} else {
			fmt.packet_samps = pkt;
		}
	}
	if (!m_primary->SndSetFormat(fmt)) {
		GetDi()->LogWarn("SoundIo: primary rejected format\n");
		goto failed;
	}

	/* Also set the null endpoint format if one is in. */
	if (m_mute_swap) {
		assert(m_pump.GetBottom());
		assert(m_pump.GetBottom() != m_primary);
		m_pump.GetBottom()->SndSetFormat(fmt);
	}

	if (!m_pump.Start()) {
		GetDi()->LogWarn("SoundIo: could not start pump\n");
		goto failed;
	}

	m_stream_up = up;
	m_stream_dn = down;
	return true;

failed:
	/* Leave the primary open if it was open when we got here */
	if (!was_open && m_primary_open) {
		ClosePrimary();
	}
	return false;
}

void SoundIoManager::
Stop(void)
{
	if (!IsStarted())
		return;

	m_pump.Stop();

	m_stream_up = false;
	m_stream_dn = false;

	ClosePrimary();
}

SoundIoFilter *SoundIoManager::
GetTopFilter(void) const
{
	SoundIoFilter *fltp = m_pump.GetTopFilter();
	if (m_mute_soft) {
		assert(fltp == m_mute_soft);
		fltp = m_pump.GetBelowFilter(fltp);
	}
	return fltp;
}

SoundIoFilter *SoundIoManager::
GetBottomFilter(void) const
{
	SoundIoFilter *fltp = m_pump.GetBottomFilter();
	if (m_dsp_installed) {
		assert(fltp == m_dsp);
		fltp = m_pump.GetAboveFilter(fltp);
	}
	return fltp;
}

bool SoundIoManager::
AddBelow(SoundIoFilter *fltp, SoundIoFilter *targp)
{
	if (!targp && m_mute_soft)
		targp = m_mute_soft;
	return m_pump.AddBelow(fltp, targp);
}

bool SoundIoManager::
AddAbove(SoundIoFilter *fltp, SoundIoFilter *targp)
{
	if (!targp && m_dsp_installed) {
		assert(m_dsp);
		targp = m_dsp;
	}
	return m_pump.AddAbove(fltp, targp);
}

unsigned int SoundIoManager::
GetPacketInterval(void)
{
	SoundIoFormat fmt;

	if (!m_primary_open)
		return m_config_packet_ms;

	m_primary->SndGetFormat(fmt);
	return (fmt.packet_samps * 1000) / fmt.samplerate;
}

} /* namespace libhfp */
