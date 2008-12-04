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

typedef SoundIo *(*sound_driver_factory_t)(DispatchInterface *, const char *,
					   ErrorInfo *error);
typedef SoundIoDeviceList *(*sound_driver_device_enum_t)(ErrorInfo *error);

struct SoundIoDriver {
	const char *name;
	const char *descr;
	sound_driver_factory_t factory;
	sound_driver_device_enum_t deviceenum;
};

static SoundIoDriver sound_drivers[] = {
#if defined(USE_ALSA_SOUNDIO)
	{ "ALSA",
	  "Advanced Linux Sound Architecture back-end",
	  SoundIoCreateAlsa,
	  SoundIoGetDeviceListAlsa },
#endif
#if defined(USE_OSS_SOUNDIO)
	{ "OSS",
	  "Open Sound System back-end (deprecated)",
	  SoundIoCreateOss,
	  SoundIoGetDeviceListOss },
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

	virtual bool SndOpen(bool play, bool capture, ErrorInfo *error) {
		return true;
	}
	virtual void SndClose(void) {
		m_buf.FreeBuffer();
	}

	virtual void SndGetFormat(SoundIoFormat &format) const {
		format = m_fmt;
	}

	virtual bool SndSetFormat(SoundIoFormat &format, ErrorInfo *error) {
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
	virtual bool SndAsyncStart(bool, bool, ErrorInfo *error) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_NO_CLOCK,
				   "Not a clocked endpoint");
		return false;
	}
	virtual void SndAsyncStop(void) {}
	virtual bool SndIsAsyncStarted(void) const { return false; }
};

class SoundIoNull : public SoundIo {
public:
	SoundIoFormat		m_fmt;

	SoundIoNull() {}
	virtual ~SoundIoNull() { SndClose(); }

	virtual bool SndOpen(bool play, bool capture, ErrorInfo *error) {
		return true;
	}
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

	virtual bool SndSetFormat(SoundIoFormat &format, ErrorInfo *error) {
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
	virtual bool SndAsyncStart(bool, bool, ErrorInfo *error) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_NO_CLOCK,
				   "Not a clocked endpoint");
		return false;
	}
	virtual void SndAsyncStop(void) {}
	virtual bool SndIsAsyncStarted(void) const { return false; }
};


class SoundIoFltMute : public SoundIoFilter {
	bool		m_mute_dn, m_mute_up;
	bool		m_init_dn, m_init_up;
	int		m_bpr, m_pktsize;
	uint8_t		*m_silence_dn, *m_silence_up;

public:
	virtual bool FltPrepare(SoundIoFormat const &fmt, bool up, bool dn,
				ErrorInfo *error) {
		m_bpr = fmt.bytes_per_record;
		m_pktsize = fmt.packet_samps;

		m_init_dn = false;
		m_init_up = false;

		if (dn && m_mute_dn) {
			m_silence_dn = (uint8_t *) malloc(m_pktsize * m_bpr);
			if (!m_silence_dn) {
				if (error)
					error->SetNoMem();
				return false;
			}
		}
		if (up && m_mute_up) {
			m_silence_up = (uint8_t *) malloc(m_pktsize * m_bpr);
			if (!m_silence_up) {
				if (error)
					error->SetNoMem();
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
	  m_dsp(0), m_dsp_enabled(true), m_dsp_installed(false),
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
PumpStopped(SoundIoPump *pumpp, SoundIo *offender, ErrorInfo &error)
{
	assert(pumpp == &m_pump);
	assert(m_primary);

	StopStats();

	if (offender == m_primary) {
		ErrorInfo xerr(error);
		ClosePrimary();
		error.Clear();
		error.Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
			  LIBHFP_ERROR_SOUNDIO_SOUNDCARD_FAILED,
			  "Sound card failed: %s",
			  xerr.Desc());

		/* This is quite severe, make sure everyone can see it! */
		GetDi()->LogError("%s", error.Desc());
	}

	if (cb_NotifyAsyncState.Registered())
		cb_NotifyAsyncState(this, error);

	/*
	 * If our client didn't manage to restart the pump, and
	 * the primary wasn't closed above, close it.
	 */
	if (!IsStarted() && m_primary_open)
		ClosePrimary();
}


template <typename T> static T abs_x(T val) { return (val < 0) ? -val : val; }

bool SoundIoManager::
StartStats(SoundIoFormat &fmt, SoundIoProps &secprops, ErrorInfo *error)
{
	/* Zero out the statistics */
	memset(&m_pump_stat, 0, sizeof(m_pump_stat));

	m_stat_cur_count = 0;
	m_stat_interval = fmt.samplerate;
	m_pri_skew_strikes = 0;
	m_sec_skew_strikes = 0;
	m_endpoint_skew_strikes = 0;

	/*
	 * We keep a revolving buffer of statistics over a time
	 * period, and construct our result from the aggregate.
	 */
	m_history_count = 5;
	m_history = new Stats[m_history_count];
	if (!m_history) {
		if (error)
			error->SetNoMem();
		return false;
	}

	memset(m_history, 0, m_history_count * sizeof(*m_history));
	m_history_pos = 0;


	/*
	 * Be very intolerant of primary endpoint duplex skew.
	 * Complain if it's over 0.01%
	 */
	m_stat_min_pri_duplex_skew = 1;

	/*
	 * Complain if the skew on the secondary endpoint,
	 * and between the primary/secondary endpoints is >=2%
	 */
	m_stat_min_sec_duplex_skew = 200;
	m_stat_min_endpoint_skew = 200;

	m_use_process_values = m_top_loop;

	if (!secprops.has_clock) {
		m_stat_min_sec_duplex_skew = 0;
		m_stat_min_endpoint_skew = 0;
	}
	m_pump.SetStatistics(&m_pump_stat);
	m_pump.cb_NotifyStatistics.Register(this,
					    &SoundIoManager::DoStatistics);
	return true;
}

void SoundIoManager::
StopStats(void)
{
	if (m_history) {
		delete[] m_history;
		m_history = 0;
	}
	m_pump.SetStatistics(0);
	m_pump.cb_NotifyStatistics.Unregister();
}

void SoundIoManager::
DoStatistics(SoundIoPump *pumpp, SoundIoPumpStatistics &stat, bool loss)
{
	/* Compile-time debugging flags for this method */
	const bool skew_debug = false;

	sio_sampnum_t tmp;
	int i, tmpt, tmpb, max;
	double skew;
	bool did_duplex = false;
	Stats *histp, totals;
	ErrorInfo error;

	assert(IsStarted());
	assert(pumpp == &m_pump);
	assert(&stat == &m_pump_stat);

	assert(cb_NotifySkew.Registered());

	if (stat.process_count < m_stat_interval)
		return;

	/*
	 * There are four causes of loss that interest us:
	 * - Asymmetry of overall rates between the primary and secondary
	 *   endpoints.  This is to be expected and ignored, but we will
	 *   complain if it is over a threshold.
	 * - Overruns/underruns caused by scheduling, which we will
	 *   complain about if it occurs too frequently.
	 * - Asymmetry of input and output rates of the secondary endpoint.
	 * - Asymmetry of input and output rates of the primary endpoint,
	 *   which make the echo canceler ineffective.  We complain most
	 *   bitterly about this class of problems.
	 */

	tmp = (stat.bottom.out.xrun + stat.bottom.in.xrun);
	if (tmp) {
		GetDi()->LogDebug("SoundIoDrop: xrun count %d", (int) tmp);
		cb_NotifySkew(this, SIO_STREAM_SKEW_XRUN, (int) tmp);

		/*
		 * If buffers over/underran, we don't evaluate for
		 * other forms of droppage.
		 */
		m_stat_cur_count = 0;
		m_history_pos = 0;
		memset(m_history, 0, m_history_count * sizeof(*m_history));
		goto done_zap;
	}

	m_stat_cur_count++;

	/* Don't evaluate skew for the first two periods */
	if (m_stat_cur_count <= 1)
		goto done_zap;

	histp = &m_history[m_history_pos];

	/*
	 * The process counters are useful for determining skew within
	 * an endpoint.  However, there is a practical problem that
	 * prevents it from being terribly useful for cross-endpoint
	 * skew estimation.  Frequently data will arrive as below:
	 *
	 * [0ms] Interrupt from primary: pri +100 samples, sec +0
	 * [10ms] Interrupt from secondary: pri +0 samples, sec +100
	 * [20ms] Interrupt from primary: pri +100 samples, sec +0
	 *
	 * Ideally, the primary and secondary endpoints would report
	 * the incremental number of samples that had been processed at
	 * the midpoint of their interrupt periods, but alas it is common
	 * not to get this service.  alsa-lib's dmix and resampler like
	 * to operate in batches, and deliver a period at a time.
	 *
	 * So we don't use production counters to estimate cross-endpoint
	 * skew, and rely instead on the pad/drop counters.
	 *
	 * In/drop, out/pad imply a faster clock,
	 * in/pad, out/drop imply a slower clock
	 */
	tmpb = (int) (stat.bottom.in.drop + stat.bottom.out.pad);
	tmpb -= (int) (stat.bottom.in.pad + stat.bottom.out.pad);
	tmpt = (int) (stat.top.in.drop + stat.top.out.pad);
	tmpt -= (int) (stat.top.in.pad + stat.top.out.drop);

	histp->endpoint_skew = (tmpt - tmpb) / 2;

	if (m_use_process_values) {
		histp->pri_max_nsamples = (int)
			((stat.bottom.in.process > stat.bottom.out.process) ?
			 stat.bottom.in.process : stat.bottom.out.process);
		histp->sec_max_nsamples = (int)
			((stat.top.in.process > stat.top.out.process) ?
			 stat.top.in.process : stat.top.out.process);

		histp->pri_duplex_skew = stat.bottom.out.process;
		histp->pri_duplex_skew -= (int) stat.bottom.in.process;
		histp->sec_duplex_skew = stat.top.out.process;
		histp->sec_duplex_skew -= (int) stat.top.in.process;

	} else {
		tmpb = (int) (stat.process_count + stat.bottom.in.drop);
		tmpb -= (int) stat.bottom.in.pad;
		tmpt = (int) (stat.process_count + stat.bottom.out.pad);
		tmpt -= (int) stat.bottom.out.drop;
		histp->pri_max_nsamples = (tmpb > tmpt) ? tmpb : tmpt;
		tmpb = (int) (stat.process_count + stat.top.in.drop);
		tmpb -= (int) stat.top.in.pad;
		tmpt = (int) (stat.process_count + stat.top.out.pad);
		tmpt -= (int) stat.top.out.drop;
		histp->sec_max_nsamples = (tmpb > tmpt) ? tmpb : tmpt;

		/*
		 * Padding implies a faster output clock,
		 * dropping implies a faster input clock 
		 */
		histp->pri_duplex_skew =
			(int) (stat.bottom.in.pad + stat.bottom.out.pad);
		histp->pri_duplex_skew -=
			(int) (stat.bottom.in.drop + stat.bottom.out.drop);
		histp->sec_duplex_skew =
			(int) (stat.top.in.pad + stat.top.out.pad);
		histp->sec_duplex_skew -=
			(int) (stat.top.in.drop + stat.top.out.drop);
	}

	m_history_pos = (m_history_pos + 1) % m_history_count;

	memset(&totals, 0, sizeof(totals));
	for (i = 0; i < m_history_count; i++) {
		totals.pri_max_nsamples += m_history[i].pri_max_nsamples;
		totals.sec_max_nsamples += m_history[i].sec_max_nsamples;
		totals.pri_duplex_skew += m_history[i].pri_duplex_skew;
		totals.sec_duplex_skew += m_history[i].sec_duplex_skew;
		totals.endpoint_skew += m_history[i].endpoint_skew;
	}

	max = (totals.pri_max_nsamples > totals.sec_max_nsamples)
		? totals.pri_max_nsamples : totals.sec_max_nsamples;

	if (skew_debug) {
		GetDi()->LogDebug("Stat: nsamples:%d priskew:%d "
				  "secskew:%d epskew:%d",
				  max, totals.pri_duplex_skew,
				  totals.sec_duplex_skew,
				  totals.endpoint_skew);
	}

	if (m_stat_min_pri_duplex_skew &&
	    (((abs_x(totals.pri_duplex_skew) * 10000) /
	      totals.pri_max_nsamples) >
	     m_stat_min_pri_duplex_skew)) {
		did_duplex = true;
		if ((m_pri_skew_strikes > 1) || (++m_pri_skew_strikes > 1)) {
			skew = totals.pri_duplex_skew;
			skew /= totals.pri_max_nsamples;
			skew *= 100;
			GetDi()->LogDebug("SoundIoDrop: pri duplex skew %f%% "
					  "to %s", abs_x(skew),
					  (skew < 0) ? "input" : "output");
			cb_NotifySkew(this, SIO_STREAM_SKEW_PRI_DUPLEX, skew);
		}
	} else {
		m_pri_skew_strikes = 0;
	}

	if (m_stat_min_sec_duplex_skew &&
	    (((abs_x(totals.sec_duplex_skew) * 10000) /
	      totals.sec_max_nsamples) >
	     m_stat_min_sec_duplex_skew)) {
		did_duplex = true;
		if ((m_sec_skew_strikes > 1) || (++m_sec_skew_strikes > 1)) {
			skew = totals.sec_duplex_skew;
			skew /= totals.sec_max_nsamples;
			skew *= 100;
			GetDi()->LogDebug("SoundIoDrop: sec duplex skew %f%% "
					  "to %s", abs_x(skew),
					  (skew < 0) ? "input" : "output");
			cb_NotifySkew(this, SIO_STREAM_SKEW_SEC_DUPLEX, skew);
		}
	} else {
		m_sec_skew_strikes = 0;
	}

	if (!did_duplex &&
	    m_stat_min_endpoint_skew &&
	    (((abs_x(totals.endpoint_skew) * 10000) / max) >
	     m_stat_min_endpoint_skew)) {
		did_duplex = true;
		if ((m_endpoint_skew_strikes > 1) ||
		    (++m_endpoint_skew_strikes > 1)) {
			skew = totals.endpoint_skew;
			skew /= max;
			skew *= 100;
			GetDi()->LogDebug("SoundIoDrop: endpoint skew %f%% "
					  "to %s", abs_x(skew),
				  (skew < 0) ? "primary" : "secondary");
			cb_NotifySkew(this, SIO_STREAM_SKEW_ENDPOINT, skew);
		}
	} else {
		m_endpoint_skew_strikes = 0;
	}

done_zap:
	memset(&stat, 0, sizeof(stat));
}


/* This is only non-static for GetDi() */
SoundIo *SoundIoManager::
CreatePrimary(const char *name, const char *opts, ErrorInfo *error)
{
	sound_driver_factory_t factory = 0;
	const char *use_opts;
	int i;

	assert(name || !opts);

	if (!sound_drivers[0].name) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_SOUNDIO,
				 LIBHFP_ERROR_SOUNDIO_NO_DRIVER,
				 "SoundIo: No drivers registered");
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
					 "using default \"%s\"",
					 name, sound_drivers[0].name);

		factory = sound_drivers[0].factory;
		assert(factory);
	}

	return factory(GetDi(), use_opts, error);
}

bool SoundIoManager::
DspInstall(ErrorInfo *error)
{
	assert(m_dsp);
	assert(!m_dsp_installed);

	if (!m_pump.AddBottom(m_dsp, error))
		return false;
	m_dsp_installed = true;

	/*
	 * Assume the DSP filter implements an NLMS echo canceler.
	 * Configure the pump to avoid loss at the primary endpoint,
	 * before invoking the filters, when the DSP filter is installed.
	 */
	m_pump.SetLossMode(false, true);
	return true;
}

void SoundIoManager::
DspRemove(void)
{
	SoundIoFilter *fltp;

	if (m_dsp_installed) {
		assert(m_dsp_enabled);
		fltp = m_pump.RemoveBottom();
		assert(fltp == m_dsp);
		m_dsp_installed = false;
		m_pump.SetLossMode(true, true);
	}
}


bool SoundIoManager::
GetDriverInfo(int index, const char **name, const char **desc,
	      SoundIoDeviceList **devlist, ErrorInfo *error)
{
	sound_driver_device_enum_t enumfun;

	if ((index < 0) ||
	    (index >= (int) (sizeof(sound_drivers) /
			     sizeof(sound_drivers[0])))) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_NO_DRIVER,
				   "No more drivers");
		return false;
	}

	if (!sound_drivers[index].name) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_NO_DRIVER,
				   "No more drivers");
		return false;
	}

	enumfun = sound_drivers[index].deviceenum;

	if (name)
		*name = sound_drivers[index].name;
	if (desc)
		*desc = sound_drivers[index].descr;
	if (devlist) {
		*devlist = enumfun(error);
		if (!*devlist)
			return false;
	}
	return true;
}

bool SoundIoManager::
SetDriver(const char *drivername, const char *driveropts, ErrorInfo *error)
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
		if (!nd) {
			if (error)
				error->SetNoMem();
			return false;
		}
	}
	if (driveropts) {
		od = strdup(driveropts);
		if (!od) {
			if (error)
				error->SetNoMem();
			goto failed;
		}
	}

	driverp = CreatePrimary(nd, od, error);
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
TestOpen(bool up, bool down, ErrorInfo *error)
{
	SoundIoProps secprops;

	if (!m_primary) {
		GetDi()->LogDebug("SoundIo: no driver set, using default");
		if (!SetDriver(NULL, NULL, error))
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

	if (!OpenPrimary(down, up, error))
		return false;

	m_primary->SndClose();
	return true;
}

bool SoundIoManager::
SetSecondary(SoundIo *secp, ErrorInfo *error)
{
	SoundIoFormat fmt;
	SoundIo *oldtop;

	oldtop = m_pump.GetTop();

	if (IsStarted()) {
		assert(m_primary);
		m_primary->SndGetFormat(fmt);
		secp->SndSetFormat(fmt);
	}

	if (!m_pump.SetTop(secp, error))
		return false;

	if (m_top_loop) {
		delete oldtop;
		m_top_loop = false;
	}

	return true;
}

bool SoundIoManager::
Loopback(ErrorInfo *error)
{
	SoundIoFormat fmt;
	SoundIo *siop;

	if (m_top_loop)
		return true;

	siop = new SoundIoLoop;
	if (!siop) {
		if (error)
			error->SetNoMem();
		return false;
	}

	if (IsStarted()) {
		if (m_mute_swap) {
			GetDi()->LogWarn(error,
					 LIBHFP_ERROR_SUBSYS_SOUNDIO,
					 LIBHFP_ERROR_SOUNDIO_BAD_PUMP_CONFIG,
					 "SoundIo: loopback mute mode "
					 "is pointless");
			return false;
		}
		m_primary->SndGetFormat(fmt);
		siop->SndSetFormat(fmt);
	}
	if (!m_pump.SetTop(siop, error)) {
		delete siop;
		return false;
	}

	m_top_loop = true;
	return true;
}

bool SoundIoManager::
SetHardMute(bool state, bool closepri, ErrorInfo *error)
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
			if (!OpenPrimary(m_stream_dn, m_stream_up, error)) {
				GetDi()->LogWarn("SoundIo: could not open "
						 "primary for unmute");
				return false;
			}
			m_primary_open = true;
		}

		if (!m_pump.SetBottom(m_primary, error)) {
			GetDi()->LogWarn("Could not reinstall primary "
					 "as bottom endpoint");
			return false;
		}

		delete siop;
		m_mute_swap = false;
		return true;
	}

	if (IsStarted() && m_top_loop) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_SOUNDIO,
				 LIBHFP_ERROR_SOUNDIO_BAD_PUMP_CONFIG,
				 "SoundIo: loopback mute mode is pointless");
		return false;
	}

	assert(m_pump.GetBottom() == m_primary);
	siop = new SoundIoNull();
	if (!siop) {
		if (error)
			error->SetNoMem();
		return false;
	}

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
SetMute(bool up, bool dn, ErrorInfo *error)
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
		if (!fltp) {
			if (error)
				error->SetNoMem();
			return false;
		}

		if (!m_pump.AddTop(fltp, error)) {
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
SetDsp(SoundIoFilter *dspp, ErrorInfo *error)
{
	bool do_install = false;
	if (m_dsp) {
		if (m_dsp_installed) {
			DspRemove();
			do_install = true;
		}
		m_dsp = 0;
	} else {
		do_install = IsStarted() && !m_top_loop && !m_mute_swap;
	}

	m_dsp = dspp;

	if (do_install && m_dsp_enabled && !DspInstall(error)) {
		m_dsp = 0;
		return false;
	}

	return true;
}

bool SoundIoManager::
SetDspEnabled(bool enabled, ErrorInfo *error)
{
	if (m_dsp_enabled == enabled)
		return true;

	if (!enabled) {
		DspRemove();
		m_dsp_enabled = false;
		return true;
	}

	if (m_dsp && IsStarted() && !m_top_loop && !m_mute_swap &&
	    !DspInstall(error))
		return false;

	m_dsp_enabled = true;
	return true;
}

bool SoundIoManager::
OpenPrimary(bool sink, bool source, ErrorInfo *error)
{
	OpLatencyMonitor lm(GetDi(), "primary open");
	bool res;
	res = m_primary->SndOpen(sink, source, error);
	assert(res || !error || error->IsSet());
	return res;
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

	DspRemove();
}

bool SoundIoManager::
Start(bool up, bool down, ErrorInfo *error)
{
	SoundIoProps secprops;
	SoundIoFormat fmt;
	assert(!IsStarted());
	sio_sampnum_t pkt;
	bool was_open, res;

	if (IsStarted())
		return false;

	if (!m_primary) {
		GetDi()->LogDebug("SoundIo: no driver set, using default");
		if (!SetDriver(NULL, NULL, error))
			return false;
	}

	if (!m_pump.GetTop() && !Loopback(error)) {
		GetDi()->LogWarn("SoundIo: could not create loopback");
		return false;
	}

	if (m_top_loop && m_mute_swap) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_SOUNDIO,
				 LIBHFP_ERROR_SOUNDIO_BAD_PUMP_CONFIG,
				 "SoundIo: loopback mute mode is pointless");
		return false;
	}

	m_pump.GetTop()->SndGetProps(secprops);
	if (!up && !down) {
		up = secprops.does_sink;
		down = secprops.does_source;
	}

	/* Make sure that the primary is open in a compatible mode */
	if (m_primary_open &&
	    ((m_stream_up != up) || (m_stream_dn != down)))
		ClosePrimary();

	was_open = m_primary_open;
	if (!m_mute_swap && !m_primary_open) {
		if (!OpenPrimary(down, up, error)) {
			GetDi()->LogWarn("SoundIo: could not open primary");
			return false;
		}
		m_primary_open = true;
	}

	if (m_dsp) {
		if (!m_top_loop && !m_mute_swap &&
		    m_dsp_enabled && !m_dsp_installed) {
			res = DspInstall(0);
			assert(res);
		}
		else if (m_top_loop || m_mute_swap)
			DspRemove();
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
					 "too small", m_config_packet_ms);
		} else {
			fmt.packet_samps = pkt;
		}
	}
	if (!m_primary->SndSetFormat(fmt, error)) {
		GetDi()->LogWarn("SoundIo: primary rejected format");
		goto failed;
	}

	/* Also set the null endpoint format if one is in. */
	if (m_mute_swap) {
		assert(m_pump.GetBottom());
		assert(m_pump.GetBottom() != m_primary);
		m_pump.GetBottom()->SndSetFormat(fmt);
	}

	if (up && down && cb_NotifySkew.Registered() &&
	    !StartStats(fmt, secprops, error))
		goto failed;

	if (!m_pump.Start(error)) {
		GetDi()->LogWarn("SoundIo: could not start pump");
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

	StopStats();

	return false;
}

void SoundIoManager::
Stop(void)
{
	if (!IsStarted())
		return;

	m_pump.Stop();

	StopStats();

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
AddBelow(SoundIoFilter *fltp, SoundIoFilter *targp, ErrorInfo *error)
{
	if (!targp && m_mute_soft)
		targp = m_mute_soft;
	return m_pump.AddBelow(fltp, targp, error);
}

bool SoundIoManager::
AddAbove(SoundIoFilter *fltp, SoundIoFilter *targp, ErrorInfo *error)
{
	if (!targp && m_dsp_installed) {
		assert(m_dsp);
		targp = m_dsp;
	}
	return m_pump.AddAbove(fltp, targp, error);
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
