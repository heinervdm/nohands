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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libhfp/soundio.h>
#include <libhfp/soundio-buf.h>

#if defined(USE_ALSA_SOUNDIO)
#include <alsa/asoundlib.h>
#endif

#include "oplatency.h"

/*
 * ALSA backend SoundIo implementation, including support for procedural
 * and mmap access.
 */

namespace libhfp {

#if defined(USE_ALSA_SOUNDIO)

/*
 * Linux ALSA sound I/O base class.
 * There are more specific ALSA classes for low-level procedural
 * and MMAP interfaces.
 *
 * Nomenclature clash alert!
 * One sample group -- N bytes per sample, M channels, NxM bytes
 *	ALSA: "frame"
 *	Us:   "record"
 *
 * Hardware sample batch size -- the interrupt interval
 *	ALSA: "period"
 *	Us:   "packet"
 */

class AlsaIoBase {
public:
	struct AlsaChannelProps {
		snd_pcm_uframes_t	packetsize;
		snd_pcm_uframes_t	bufsize;
		snd_pcm_uframes_t	min_avail;
	};

	SoundIoFormat			m_format;

	DispatchInterface		*m_ei;

	snd_pcm_t			*m_play_handle;
	char				*m_play_devspec;
	AlsaChannelProps		m_play_props;
	int				m_play_not_count;
	SocketNotifier			**m_play_not;
	bool				m_play_async;
	bool				m_play_xrun;

	snd_pcm_t			*m_rec_handle;
	char				*m_rec_devspec;
	AlsaChannelProps		m_rec_props;
	int				m_rec_not_count;
	SocketNotifier			**m_rec_not;
	bool				m_rec_async;
	bool				m_rec_xrun;

	AlsaIoBase(DispatchInterface *eip,
		   const char *output_devspec, const char *input_devspec)
		: m_ei(eip), 
		  m_play_handle(NULL), m_play_not_count(0), m_play_not(NULL),
		  m_play_async(false), m_rec_handle(NULL), m_rec_not_count(0),
		  m_rec_not(NULL), m_rec_async(false) {
		m_play_devspec = strdup(output_devspec);
		m_rec_devspec = m_play_devspec;
		if (input_devspec &&
		    strcmp(input_devspec, output_devspec)) {
			m_rec_devspec = strdup(input_devspec);
		}
		memset(&m_play_props, 0, sizeof(m_play_props));
		memset(&m_rec_props, 0, sizeof(m_rec_props));

		/* Set a default format */
		memset(&m_format, 0, sizeof(m_format));
		m_format.sampletype = SIO_PCM_S16_LE;
		m_format.samplerate = 8000;
		m_format.packet_samps = 128;
		m_format.nchannels = 1;
		m_format.bytes_per_record = 2;
	}

	~AlsaIoBase() {
		CloseDevice();
		if (m_play_devspec) {
			free(m_play_devspec);
		}
		if (m_rec_devspec && (m_rec_devspec != m_play_devspec)) {
			free(m_rec_devspec);
		}
	}

	snd_pcm_sframes_t GetPacketSize(void) const {
		if (m_rec_handle) return m_rec_props.packetsize;
		if (m_play_handle) return m_play_props.packetsize;
		return m_format.packet_samps;
	}

	snd_pcm_sframes_t GetSampleBytes(void) const
		{ return m_format.bytes_per_record; }

	bool OpenDevice(snd_pcm_access_t pcm_access, bool play, bool rec,
			ErrorInfo *error) {
		int err;
		if (m_play_handle || m_rec_handle) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
					   LIBHFP_ERROR_SOUNDIO_ALREADY_OPEN,
					   "Device already open");
			return false;
		}

		/*
		 * ALSA's snd_config_update() is broken WRT
		 * modifying /etc/asound.conf for now, so we
		 * just invalidate everything every time.
		 */
		snd_config_update_free_global();

		if (play) {
			err = snd_pcm_open(&m_play_handle, m_play_devspec,
					   SND_PCM_STREAM_PLAYBACK, 0);
			if (err < 0) {
				m_ei->LogWarn(error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
					      LIBHFP_ERROR_SOUNDIO_SYSCALL,
					      "Could not open playback "
					      "device \"%s\": %s",
					      m_play_devspec,
					      strerror(-err));
				return false;
			}

			if (!ConfigurePcm(m_play_handle, m_format, pcm_access,
					  m_play_props, error)) {
				m_ei->LogWarn("Error configuring playback "
					      "device \"%s\"",
					      m_play_devspec);
				CloseDevice();
				return false;
			}
		}

		if (rec) {
			err = snd_pcm_open(&m_rec_handle, m_rec_devspec,
					   SND_PCM_STREAM_CAPTURE, 0);
			if (err < 0) {
				m_ei->LogWarn(error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
					      LIBHFP_ERROR_SOUNDIO_SYSCALL,
					      "Could not open record "
					      "device \"%s\": %s",
					      m_rec_devspec,
					      strerror(-err));
				CloseDevice();
				return false;
			}

			if (!ConfigurePcm(m_rec_handle, m_format, pcm_access,
					  m_rec_props, error)) {
				m_ei->LogWarn("Error configuring record "
					      "device \"%s\"",
					      m_rec_devspec);
				CloseDevice();
				return false;
			}
		}

		return true;
	}

	void CloseDevice(void) {
		if (m_play_handle) {
			snd_pcm_close(m_play_handle);
			m_play_handle = NULL;
		}
		if (m_rec_handle) {
			snd_pcm_close(m_rec_handle);
			m_rec_handle = NULL;
		}
	}

	void GetProps(SoundIoProps &props) const {
		props.has_clock = true;
		props.does_source = (m_rec_handle != NULL);
		props.does_sink = (m_play_handle != NULL);
		props.does_loop = false;
		props.remove_on_exhaust = false;
		props.outbuf_size = m_play_props.bufsize;
	}

	/*
	 * Register file handle notification callbacks.
	 * Start recording if requested.
	 */
	bool CreatePcmNotifiers(bool playback, bool capture,
				Callback<void, SocketNotifier*, int> &tmpl,
				ErrorInfo *error) {
		int err;
		bool do_playback = playback;

		if (!playback && !capture) {
			return true;
		}
		if (playback && (m_play_handle == NULL)) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
					   "Device not open for playback");
			return false;
		}
		if (capture && (m_rec_handle == NULL)) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
					   "Device not open for capture");
			return false;
		}

		if (playback && capture) {
			/*
			 * We could save some resources and register
			 * a single notifier here, but what the heck.
			 */
			/* do_playback = false; */
		}

		m_play_xrun = false;
		m_rec_xrun = false;

		if (do_playback &&
		    (m_play_not == NULL) &&
		    !CreateNotifiers(m_play_handle, m_ei, tmpl, m_play_not,
				     m_play_not_count, error)) {
			return false;
		}

		if (capture && (m_rec_not == NULL)) {
			if (!CreateNotifiers(m_rec_handle, m_ei, tmpl,
					     m_rec_not, m_rec_not_count,
					     error)) {
				CleanupPcmNotifiers();
				return false;
			}

			err = snd_pcm_start(m_rec_handle);
			if (err == -EBADFD) {
				/* ???  Try again! */
				snd_pcm_drop(m_rec_handle);
				snd_pcm_prepare(m_rec_handle);
				err = snd_pcm_start(m_rec_handle);
			}

			if (err < 0) {
				m_ei->LogDebug(error,
					       LIBHFP_ERROR_SUBSYS_SOUNDIO,
					       LIBHFP_ERROR_SOUNDIO_SYSCALL,
					       "ALSA pcm start: %s",
					       strerror(-err));
				CleanupPcmNotifiers();
				return false;
			}
		}

		if (!do_playback && (m_play_not != NULL)) {
			CleanupNotifiers(m_play_not, m_play_not_count);
		}

		m_play_async = playback;
		m_rec_async = capture;
		return true;
	}

	void CleanupPcmNotifiers(void) {
		m_play_async = m_rec_async = false;
		CleanupNotifiers(m_play_not, m_play_not_count);
		CleanupNotifiers(m_rec_not, m_rec_not_count);
	}

	bool Reconfigure(SoundIoFormat *formatp,
			 snd_pcm_access_t pcm_access, ErrorInfo *error) {
		SoundIoFormat format;

		if (formatp) { format = *formatp; } else { format = m_format; }

		if (m_play_handle) {
			m_ei->LogDebug("ALSA play state: %d",
				       snd_pcm_state(m_play_handle));
			snd_pcm_drop(m_play_handle);
			if (!ConfigurePcm(m_play_handle, format, pcm_access,
					  m_play_props, error)) {
				return false;
			}
			m_ei->LogDebug("ALSA play state: %d",
				       snd_pcm_state(m_play_handle));
		}

		if (m_rec_handle) {
			m_ei->LogDebug("ALSA rec state: %d",
				       snd_pcm_state(m_rec_handle));
			snd_pcm_drop(m_rec_handle);
			if (!ConfigurePcm(m_rec_handle, format, pcm_access,
					  m_rec_props, error)) {
				return false;
			}
			m_ei->LogDebug("ALSA rec state: %d",
				       snd_pcm_state(m_rec_handle));
		}

		if (!m_play_handle && !m_rec_handle) {
			m_rec_props.packetsize = m_play_props.packetsize =
				format.packet_samps;
		}

		m_format = format;
		return true;
	}

	static snd_pcm_format_t AlsaPcmFormat(sio_sampletype_t st) {
		switch (st) {
		case SIO_PCM_U8:
			return SND_PCM_FORMAT_U8;
		case SIO_PCM_S16_LE:
			return SND_PCM_FORMAT_S16_LE;
		case SIO_PCM_A_LAW:
			return SND_PCM_FORMAT_A_LAW;
		case SIO_PCM_MU_LAW:
			return SND_PCM_FORMAT_MU_LAW;
		default:
			return SND_PCM_FORMAT_UNKNOWN;
		}
	}

	bool SetAvailMin(snd_pcm_t *pcmp, snd_pcm_uframes_t amin) {
		int err;
		snd_pcm_sw_params_t *swp;
		OpLatencyMonitor lat(m_ei, "ALSA SetAvailMin");

		snd_pcm_sw_params_alloca(&swp);

		err = snd_pcm_sw_params_current(pcmp, swp);
		if (err < 0) {
			m_ei->LogWarn("ALSA sw_params_current failed: %s",
				      strerror(-err));
			return false;
		}

		err = snd_pcm_sw_params_set_avail_min(pcmp, swp, amin);
		if (err < 0) {
			m_ei->LogWarn("ALSA sw_params_set_avail_min "
				      "failed: %s", strerror(-err));
			return false;
		}

		err = snd_pcm_sw_params(pcmp, swp);
		if (err < 0) {
			m_ei->LogWarn("ALSA sw_params failed: %s",
				      strerror(-err));
			return false;
		}

		return true;
	}

	bool ConfigurePcm(snd_pcm_t *pcmp, SoundIoFormat &format,
			  snd_pcm_access_t pcm_access,
			  AlsaChannelProps &props, ErrorInfo *error = 0) {
		snd_pcm_hw_params_t *hwp;
		snd_pcm_sw_params_t *swp;
		snd_pcm_uframes_t buffersize, packetsize, amin;
		snd_pcm_format_t sampfmt;
		int err;

		sampfmt = AlsaPcmFormat(format.sampletype);
		if (sampfmt == SND_PCM_FORMAT_UNKNOWN) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_FORMAT_UNKNOWN,
				      "Unrecognized sample type %d",
				      format.sampletype);
			return false;
		}

		snd_pcm_hw_params_alloca(&hwp);
		snd_pcm_sw_params_alloca(&swp);

		err = snd_pcm_hw_params_any(pcmp, hwp);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA hw_params_any failed: %s",
				      strerror(-err));
			goto failed;
		}

		err = snd_pcm_hw_params_set_access(pcmp, hwp, pcm_access);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA set_access failed: %s",
				      strerror(-err));
			goto failed;
		}

		err = snd_pcm_hw_params_set_format(pcmp, hwp, sampfmt);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA set_format failed: %s",
				      strerror(-err));
			goto failed;
		}

		err = snd_pcm_hw_params_set_rate(pcmp, hwp,
						 format.samplerate, 0);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA set_rate failed: %s",
				      strerror(-err));
			goto failed;
		}

		err = snd_pcm_hw_params_set_channels(pcmp, hwp,
						     format.nchannels);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA set_channels failed: %s",
				      strerror(-err));
			goto failed;
		}

		packetsize = format.packet_samps;

		err = snd_pcm_hw_params_set_period_size_near(pcmp, hwp,
							     &packetsize, 0);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA set_period failed: %s",
				      strerror(-err));
			goto failed;
		}

		buffersize = props.bufsize ? props.bufsize : 0;
		if (buffersize < packetsize)
			/* Default create a buffer sixteen packets deep */
			buffersize = packetsize * 16;

		err = snd_pcm_hw_params_set_buffer_size_near(pcmp, hwp,
							     &buffersize);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA set_buffer_size failed: %s",
				      strerror(-err));
			goto failed;
		}

		err = snd_pcm_hw_params(pcmp, hwp);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA hw_params failed: %s",
				      strerror(-err));
			goto failed;
		}

		err = snd_pcm_hw_params_current(pcmp, hwp);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA hw_params_current failed: %s",
				      strerror(-err));
			goto failed;
		}

		err = snd_pcm_hw_params_get_buffer_size(hwp, &buffersize);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA get_buffer_size failed: %s",
				      strerror(-err));
			goto failed;
		}
		err = snd_pcm_hw_params_get_period_size(hwp, &packetsize, 0);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA get_period_size failed: %s",
				      strerror(-err));
			goto failed;
		}

		err = snd_pcm_sw_params_current(pcmp, swp);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA sw_params failed: %s",
				      strerror(-err));
			goto failed;
		}

		err = snd_pcm_sw_params_get_avail_min(swp, &amin);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA sw_params_get_avail_min "
				      "failed: %s", strerror(-err));
			goto failed;
		}

		err = snd_pcm_prepare(pcmp);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA prepare failed: %s",
				      strerror(-err));
			goto failed;
		}

		props.packetsize = packetsize;
		props.bufsize = buffersize;
		props.min_avail = amin;
		return true;

	failed:
		return false;
	}

	bool Prepare(bool playback, bool capture, ErrorInfo *error) {
		if (playback) {
			assert(m_play_handle);
			m_ei->LogDebug("ALSA play state: %d",
				       snd_pcm_state(m_play_handle));
		}
		if (capture) {
			assert(m_rec_handle);
			m_ei->LogDebug("ALSA rec state: %d",
				       snd_pcm_state(m_rec_handle));
		}
		return true;
	}

	bool CreateNotifiers(snd_pcm_t *pcm_handle,
			     DispatchInterface *eip,
			     Callback<void, SocketNotifier*, int> &cbtemplate,
			     SocketNotifier **&table, int &count,
			     ErrorInfo *error) {
		struct pollfd *polldesc;
		int i, j, nfds, nnot;
		int err;

		nfds = snd_pcm_poll_descriptors_count(pcm_handle);
		polldesc = (struct pollfd*) alloca(nfds * sizeof(*polldesc));
		err = snd_pcm_poll_descriptors(pcm_handle, polldesc, nfds);
		if (err < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA get poll descriptors: %s",
				      strerror(-err));
			return false;
		}

		/* Determine how many notifiers we need */
		nnot = 0;
		for (i = 0; i < nfds; i++) {
			if (polldesc[i].events & POLLIN)
				nnot++;
			if (polldesc[i].events & POLLOUT)
				nnot++;
		}

		table = (SocketNotifier**)
			malloc(nnot * sizeof(SocketNotifier*));

		for (i = 0, j = 0; i < nfds; i++) {
			if (polldesc[i].events & POLLIN) {
				assert(j < nnot);
				table[j] = eip->NewSocket(polldesc[i].fd,
							  false);
				table[j]->Register(cbtemplate);
				j++;
			}
			if (polldesc[i].events & POLLOUT) {
				assert(j < nnot);
				table[j] = eip->NewSocket(polldesc[i].fd,
							  true);
				table[j]->Register(cbtemplate);
				j++;
			}
		}
		assert(j == nnot);
		count = nnot;
		return true;
	}

	bool HasNotifiers(void) const {
		return (m_play_not != 0) || (m_rec_not != 0);
	}

	static void CleanupNotifiers(SocketNotifier **&table, int &count) {
		int i;
		if (count) {
			for (i = 0; i < count; i++) {
				delete table[i];
			}
			free(table);
			table = NULL;
			count = 0;
		}
	}

	/*
	 * This is quite ugly.  We don't control the real event loop,
	 * so we call poll() ourselves and let ALSA process the result.
	 * Depending on how the alsa-lib PCM device stack is built, this
	 * function can do some really interesting things.
	 */
	bool CheckNotifications(void) {
		struct pollfd *pollfds;
		unsigned short revents;
		int ndesc = 0, nused = 0, play_first;
		OpLatencyMonitor lat(m_ei, "ALSA CheckNotifications");

		if (m_rec_handle)
			ndesc = snd_pcm_poll_descriptors_count(m_rec_handle);
		if (m_play_handle)
			ndesc += snd_pcm_poll_descriptors_count(m_play_handle);
		if (!ndesc)
			return false;
		pollfds = (struct pollfd*) alloca(ndesc * sizeof(*pollfds));
		if (m_rec_handle) {
			nused = snd_pcm_poll_descriptors(m_rec_handle,
							 pollfds, ndesc);
			assert(nused <= ndesc);
		}
		play_first = nused;
		if (m_play_handle) {
			nused += snd_pcm_poll_descriptors(m_play_handle,
							  &pollfds[nused],
							  ndesc - nused);
			assert(nused <= ndesc);
		}
		if (!nused)
			return false;
		poll(pollfds, nused, 0);
		if (m_rec_handle && play_first) {
			if (snd_pcm_poll_descriptors_revents(m_rec_handle,
							     pollfds,
							     play_first,
							     &revents) < 0) {
			m_ei->LogWarn("ALSA pcm_poll_descriptors_revents "
				      "for rec: %s", strerror(errno));
			}
			else if (revents & POLLIN)
				return true;
		}
		if (m_play_handle && (nused - play_first)) {
			if (snd_pcm_poll_descriptors_revents(m_play_handle,
						     &pollfds[play_first],
						     nused - play_first,
						     &revents) < 0) {
			m_ei->LogWarn("ALSA pcm_poll_descriptors_revents "
				      "for play: %s", strerror(errno));
			}
			else if (revents & POLLOUT)
				return true;
		}

		return false;
	}

	bool HandleInterruption(snd_pcm_t *pcmp, snd_pcm_sframes_t res,
				ErrorInfo *error) {
		int err;
		const char *streamtype;
		bool *xrun;

		if (snd_pcm_stream(pcmp) == SND_PCM_STREAM_PLAYBACK) {
			streamtype = "playback";
			xrun = &m_play_xrun;
		} else {
			streamtype = "capture";
			xrun = &m_rec_xrun;
		}

	restart:
		switch (snd_pcm_state(pcmp)) {
		case SND_PCM_STATE_XRUN:
			/* buffer overrun/underrun, treat like setup */
			m_ei->LogDebug("**** ALSA %s xrun ****", streamtype);
			*xrun = true;

		case SND_PCM_STATE_SETUP:
			err = snd_pcm_prepare(pcmp);
			if (err < 0) {
				m_ei->LogWarn(error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
					      LIBHFP_ERROR_SOUNDIO_SYSCALL,
					      "ALSA %s prepare: %s",
					      streamtype, strerror(-err));
				return false;
			}

			assert(snd_pcm_state(pcmp) == SND_PCM_STATE_PREPARED);
			/* fall-thru */

		case SND_PCM_STATE_PREPARED:
			if (snd_pcm_stream(pcmp) == SND_PCM_STREAM_CAPTURE) {
				err = snd_pcm_start(pcmp);
				if (err < 0) {
					m_ei->LogWarn(error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
					      LIBHFP_ERROR_SOUNDIO_SYSCALL,
						      "ALSA %s start: %s",
						      streamtype,
						      strerror(-err));
					return false;
				}
			}
			return true;

		case SND_PCM_STATE_SUSPENDED:
			/* hardware was suspended? huh? */
			m_ei->LogWarn("ALSA %s suspended", streamtype);

			err = snd_pcm_resume(pcmp);
			if (err < 0) {
				m_ei->LogWarn(error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
					      LIBHFP_ERROR_SOUNDIO_SYSCALL,
					      "ALSA %s resume: %s",
					      streamtype, strerror(-err));
				return false;
			}
			if (snd_pcm_state(pcmp) == SND_PCM_STATE_SUSPENDED) {
				m_ei->LogWarn(error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
					      LIBHFP_ERROR_SOUNDIO_SYSCALL,
					      "ALSA %s resume succeeded, but "
					      "still in suspended state",
					      streamtype);
				return false;
			}
			goto restart;

		case SND_PCM_STATE_RUNNING:
			return true;

		case SND_PCM_STATE_DISCONNECTED:
			m_ei->LogDebug(error,
				       LIBHFP_ERROR_SUBSYS_SOUNDIO,
				       LIBHFP_ERROR_SOUNDIO_SYSCALL,
				       "ALSA %s is disconnected, "
				       "shutting down", streamtype);
			return false;

		default:
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "ALSA %s in strange state %d",
				      streamtype, snd_pcm_state(pcmp));
			return false;
		}
	}
};


class SoundIoAlsaProc : public SoundIoBufferBase {
	AlsaIoBase		m_alsa;

	bool			m_play_nonblock;
	bool			m_rec_nonblock;

	void OpenBuf(void) {
		BufOpen(m_alsa.GetPacketSize(),
			m_alsa.m_format.bytes_per_record);
	}

public:
	SoundIoAlsaProc(DispatchInterface *eip,
			const char *output_devspec, const char *input_devspec)
		: m_alsa(eip, output_devspec, input_devspec) {}
	virtual ~SoundIoAlsaProc() {}

	virtual bool SndOpen(bool play, bool capture, ErrorInfo *error) {
		if (play)
			m_alsa.m_play_props.bufsize = 0;
		if (capture)
			m_alsa.m_rec_props.bufsize = 0;
		if (m_alsa.OpenDevice(SND_PCM_ACCESS_RW_INTERLEAVED,
				      play, capture, error)) {
			m_play_nonblock = false;
			m_rec_nonblock = false;
			OpenBuf();
			return true;
		}
		return false;
	}
	virtual void SndClose(void) {
		SndAsyncStop();
		BufClose();
		m_alsa.CloseDevice();
	}

	virtual void SndGetFormat(SoundIoFormat &format) const {
		format = m_alsa.m_format;
		format.packet_samps = m_alsa.GetPacketSize();
	}

	virtual bool SndSetFormat(SoundIoFormat &format, ErrorInfo *error) {
		SndAsyncStop();
		if (m_alsa.m_play_handle)
			m_alsa.m_play_props.bufsize = 0;
		if (m_alsa.m_rec_handle)
			m_alsa.m_rec_props.bufsize = 0;
		if (m_alsa.Reconfigure(&format,
				       SND_PCM_ACCESS_RW_INTERLEAVED, error)) {
			OpenBuf();
			return true;
		}
		return false;
	}

	virtual void SndGetProps(SoundIoProps &props) const {
		m_alsa.GetProps(props);
	}

	virtual void SndGetQueueState(SoundIoQueueState &qs) {
		snd_pcm_sframes_t err, exp;

		if (m_alsa.m_play_handle) {
			OpLatencyMonitor lat(m_alsa.m_ei,
					     "ALSA get playback queue state");
			(void) snd_pcm_avail_update(m_alsa.m_play_handle);
			err = snd_pcm_delay(m_alsa.m_play_handle, &exp);
			if (err < 0) {
				exp = 0;
			}
			if (exp < 0) {
				/* WTF?  Die broken alsa-lib plugins! */
				exp = 0;
			}
			m_hw_outq = exp;
		}
		SoundIoBufferBase::SndGetQueueState(qs);
	}

	virtual void SndPushInput(bool nonblock) {
		unsigned int nsamples;
		uint8_t *buf;
		snd_pcm_sframes_t exp, err;
		ErrorInfo error;
		OpLatencyMonitor lat(m_alsa.m_ei, "ALSA SndPushInput");

		if (m_abort)
			return;		/* Don't bother */

		if (m_rec_nonblock != nonblock) {
			err = snd_pcm_nonblock(m_alsa.m_rec_handle, nonblock);
			if (err < 0) {
				m_alsa.m_ei->LogWarn("ALSA set nonblock: "
						     "%ld", err);
			}
			m_rec_nonblock = nonblock;
		}

		/*
		 * For some reason, if we're dealing directly with a
		 * hardware device, nonblocking mode is busted here.
		 * We need to deal with hw:X devices in order to
		 * achieve low latency.
		 *
		 * We won't even bother to set it, and will emulate it by
		 * only calling pcm_readi if we're sure the number of
		 * samples we want is available.
		 */
	restart_me:
		exp = snd_pcm_avail_update(m_alsa.m_rec_handle);
		if (exp < 0) {
			err = exp;
			goto do_interruption;
		}
		while (1) {
			nsamples = 0;
			m_input.GetUnfilled(buf, nsamples);
			assert(nsamples);
			if (exp < (snd_pcm_sframes_t) nsamples)
				break;

			err = snd_pcm_readi(m_alsa.m_rec_handle, buf,nsamples);
			if (err < 0) {
				if (err == -EAGAIN) { break; }
			do_interruption:
				if (m_alsa.HandleInterruption(
					    m_alsa.m_rec_handle, err,
					    &error)) {
					goto restart_me;
				}
				m_alsa.m_ei->LogWarn("ALSA capture failed: "
						     "%ld", err);
				BufAbort(m_alsa.m_ei, error);
				break;
			}
			if (!err) { break; }

			m_input.PutUnfilled(err);
			exp -= err;
		}
	}

	virtual void SndPushOutput(bool nonblock) {
		unsigned int nsamples;
		uint8_t *buf;
		ssize_t err;
		ErrorInfo error;
		OpLatencyMonitor lat(m_alsa.m_ei, "ALSA SndPushOutput");

		if (m_abort)
			return;		/* Don't bother */

		if (!m_alsa.m_play_handle)
			return;

		if (m_play_nonblock != nonblock) {
			err = snd_pcm_nonblock(m_alsa.m_play_handle, nonblock);
			if (err < 0) {
				m_alsa.m_ei->LogWarn("ALSA set nonblock: "
						     "%zd", err);
			}
			m_play_nonblock = nonblock;
		}

		(void) snd_pcm_avail_update(m_alsa.m_play_handle);
		while (1) {
			nsamples = 0;
			m_output.Peek(buf, nsamples);
			if (!nsamples) { break; }

		restart_me:
			err = snd_pcm_writei(m_alsa.m_play_handle,
					     buf, nsamples);
			if (err < 0) {
				if (err == -EAGAIN) {
					m_alsa.m_ei->LogWarn("ALSA: playback "
							     "buffer full");
					break;
				}
				if (m_alsa.HandleInterruption(
					    m_alsa.m_play_handle, err,
					    &error)) {
					goto restart_me;
				}
				m_alsa.m_ei->LogWarn("ALSA playback failed: "
						     "%zd", err);
				BufAbort(m_alsa.m_ei, error);
				break;
			}
			if (!err) {
				m_alsa.m_ei->LogWarn("ALSA pcm_writei "
						     "result is 0?");
				break;
			}

			m_output.Dequeue(err);
			m_hw_outq += err;
		}
	}

	void AsyncProcess(SocketNotifier *notp, int fh) {
		bool overrun = false, underrun = false;
		int err;
		snd_pcm_sframes_t exp = 0;
		OpLatencyMonitor olat(m_alsa.m_ei, "ALSA async overall");

		/*
		 * We will explicitly test for xruns here.
		 */

		if (m_abort)
			goto do_abort;

		if (!m_alsa.CheckNotifications())
			return;

		if (m_alsa.m_rec_async) {
			if (!m_alsa.m_rec_xrun) {
				/*
				 * We will read as much input as we can
				 * into our buffer
				 */
				SndPushInput(true);
				if (m_abort)
					goto do_abort;
			}

			overrun = m_alsa.m_rec_xrun;
			m_alsa.m_rec_xrun = false;
			if (!overrun) {
				overrun = (snd_pcm_state(m_alsa.
							 m_rec_handle) ==
					   SND_PCM_STATE_XRUN);
				if (overrun &&
				    !m_alsa.HandleInterruption(m_alsa.
							       m_rec_handle,
							       -EPIPE,
							       &m_abort)) {
					goto do_abort;
				}
			}
		}

		if (m_alsa.m_play_async) {
			OpLatencyMonitor lat(m_alsa.m_ei,
					     "ALSA check playback");
			(void) snd_pcm_avail_update(m_alsa.m_play_handle);
			underrun = m_alsa.m_play_xrun;
			m_alsa.m_play_xrun = false;
			if (!underrun) {
				underrun = (snd_pcm_state(m_alsa.
							  m_play_handle) ==
					    SND_PCM_STATE_XRUN);
				if (underrun &&
				    !m_alsa.HandleInterruption(m_alsa.
							       m_play_handle,
							       -EPIPE,
							       &m_abort)) {
					goto do_abort;
				}
			}

			err = snd_pcm_delay(m_alsa.m_play_handle, &exp);
			if (err < 0) {
				if (!underrun || (err != -EPIPE))
					m_alsa.m_ei->LogWarn(
						"ALSA playback: "
						"snd_pcm_delay: %s",
						strerror(-err));
				exp = 0;
			}
			if (exp < 0) {
				/* WTF?  Die broken alsa-lib plugins! */
				exp = 0;
			}
		}

		if (!BufProcess(exp, overrun, underrun))
			return;

		if (m_alsa.m_play_not &&
		    (m_hw_outq < m_alsa.m_play_props.bufsize)) {
			/*
			 * ALSA wants to alert us whenever our output
			 * buffer free space increases above some level.
			 * We just want to know whenever pktsize samples
			 * have been processed.
			 */
			exp = ((m_alsa.m_play_props.bufsize - m_hw_outq) +
			       m_alsa.m_play_props.packetsize);

			m_alsa.SetAvailMin(m_alsa.m_play_handle, exp);
		}
		return;

	do_abort:
		SndHandleAbort(m_abort);
	}

	virtual bool SndAsyncStart(bool playback, bool capture,
				   ErrorInfo *error) {
		Callback<void, SocketNotifier*, int> tmpl;
		if (playback) {
			m_alsa.SetAvailMin(m_alsa.m_play_handle,
					   m_alsa.m_play_props.bufsize -
					   m_alsa.m_play_props.packetsize);
		}
		tmpl.Register(this, &SoundIoAlsaProc::AsyncProcess);
		if (!m_alsa.Prepare(playback, capture, error))
			return false;
		return m_alsa.CreatePcmNotifiers(playback, capture, tmpl,
						 error);
	}

	virtual void SndAsyncStop(void) {
		BufClose();
		m_alsa.CleanupPcmNotifiers();
	}

	virtual bool SndIsAsyncStarted(void) const {
		return m_alsa.HasNotifiers();
	}
};

class SoundIoAlsaMmap : public SoundIo {
	AlsaIoBase		m_alsa;

	const snd_pcm_channel_area_t	*m_play_areas;
	snd_pcm_uframes_t		m_play_off, m_play_size;
	const snd_pcm_channel_area_t	*m_rec_areas;
	snd_pcm_uframes_t		m_rec_off, m_rec_size;
	int				m_packetbytes;
	SoundIoQueueState		m_qs;
	ErrorInfo			m_abort;

public:
	SoundIoAlsaMmap(DispatchInterface *eip,
			const char *output_devspec, const char *input_devspec)
		: m_alsa(eip, output_devspec, input_devspec),
		  m_play_areas(NULL), m_rec_areas(NULL) {}
	virtual ~SoundIoAlsaMmap() {}

	bool SetPacketSize(ErrorInfo *error) {
		if (m_alsa.m_rec_props.packetsize !=
		    m_alsa.m_play_props.packetsize) {
			m_alsa.m_ei->LogWarn(error,
					     LIBHFP_ERROR_SUBSYS_SOUNDIO,
					     LIBHFP_ERROR_SOUNDIO_INTERNAL,
					     "ALSA packet size mismatch: "
					     "%ld capture, %ld playback",
					     m_alsa.m_rec_props.packetsize,
					     m_alsa.m_play_props.packetsize);
			return false;
		}
		m_packetbytes = m_alsa.GetPacketSize() *
			m_alsa.GetSampleBytes();

		return true;
	}

	void CommitMappings(void) {
		if (m_play_areas) {
			snd_pcm_mmap_commit(m_alsa.m_play_handle,
					    m_play_off, 0);
			m_play_areas = NULL;
		}
		if (m_rec_areas) {
			snd_pcm_mmap_commit(m_alsa.m_rec_handle,
					    m_rec_off, 0);
			m_rec_areas = NULL;
		}
	}

	virtual bool SndOpen(bool play, bool capture, ErrorInfo *error) {
		return m_alsa.OpenDevice(SND_PCM_ACCESS_MMAP_INTERLEAVED,
					 play, capture, error) &&
			SetPacketSize(error);
	}
	virtual void SndClose(void) {
		SndAsyncStop();
		m_alsa.CloseDevice();
	}

	virtual void SndGetProps(SoundIoProps &props) const {
		m_alsa.GetProps(props);
	}

	virtual void SndGetFormat(SoundIoFormat &format) const {
		format = m_alsa.m_format;
		format.packet_samps = m_alsa.GetPacketSize();
	}

	virtual bool SndSetFormat(SoundIoFormat &format, ErrorInfo *error) {
		SndAsyncStop();
		if (!m_alsa.Reconfigure(&format,
					SND_PCM_ACCESS_MMAP_INTERLEAVED,
					error)) {
			return false;
		}
		if (!SetPacketSize(error)) {
			return false;
		}
		return true;
	}

	virtual void SndGetIBuf(SoundIoBuffer &fillme) {
		int err;

		if (m_rec_areas &&
		    (!fillme.m_size || (fillme.m_size < m_rec_size))) {
			/* Use the existing buffer */
			fillme.m_data = (((uint8_t *) m_rec_areas[0].addr) +
				 (m_rec_off * m_alsa.GetSampleBytes()));
			return;
		}

		if (m_rec_areas) {
			(void) snd_pcm_mmap_commit(m_alsa.m_rec_handle,
						   m_rec_off, 0);
			m_rec_areas = NULL;
		}

		m_rec_size = fillme.m_size;
		if (!m_rec_size) { m_rec_size = m_alsa.GetPacketSize(); }
	retry:
		(void) snd_pcm_avail_update(m_alsa.m_rec_handle);
		err = snd_pcm_mmap_begin(m_alsa.m_rec_handle,
					 &m_rec_areas,
					 &m_rec_off,
					 &m_rec_size);
		if (err < 0) {
			if (m_alsa.HandleInterruption(m_alsa.m_rec_handle,
						      err, &m_abort))
				goto retry;

			m_alsa.m_ei->LogWarn("ALSA rec mmap_begin: %s",
					     strerror(-err));
			fillme.m_size = 0;
			return;
		}
		if (!m_rec_size) {
			(void) snd_pcm_mmap_commit(m_alsa.m_rec_handle,
						   m_rec_off, 0);
			m_rec_areas = NULL;
			fillme.m_size = 0;
			return;
		}

		fillme.m_data = (((uint8_t *) m_rec_areas[0].addr) +
				 (m_rec_off * m_alsa.GetSampleBytes()));
		fillme.m_size = m_rec_size;
	}

	virtual void SndDequeueIBuf(sio_sampnum_t deqme) {
		int err;

		if (m_rec_areas) {
			if (deqme < m_rec_size) {
				m_rec_size -= deqme;
				m_rec_off += deqme;
				return;
			}

			m_rec_areas = NULL;
			err = snd_pcm_mmap_commit(m_alsa.m_rec_handle,
						  m_rec_off, m_rec_size);
			if (err != (int) m_rec_size) {
				if (!m_alsa.HandleInterruption(m_alsa.
							       m_rec_handle,
							       err,
							       &m_abort)) {
					m_alsa.m_ei->LogWarn(
						"ALSA rec mmap_commit: %s",
						strerror(-err));
					return;
				}
			}

			deqme -= m_rec_size;
		}

		if (deqme) {
			/*
			 * The caller wants us to dequeue more frames than
			 * were ever mapped.  Fine.
			 */
			err = snd_pcm_forward(m_alsa.m_rec_handle, deqme);
		}
	}

	virtual void SndGetOBuf(SoundIoBuffer &fillme) {
		int err;

		if (m_play_areas &&
		    (!fillme.m_size || (fillme.m_size < m_play_size))) {
			/* Use the existing buffer */
			fillme.m_data = (((uint8_t *) m_play_areas[0].addr) +
				 (m_play_off * m_alsa.GetSampleBytes()));
			return;
		}

		if (m_play_areas) {
			(void) snd_pcm_mmap_commit(m_alsa.m_play_handle,
						   m_play_off, 0);
			m_play_areas = NULL;
		}

		m_play_size = fillme.m_size;
		if (!m_play_size) { m_play_size = m_alsa.GetPacketSize(); }
	retry:
		(void) snd_pcm_avail_update(m_alsa.m_play_handle);
		err = snd_pcm_mmap_begin(m_alsa.m_play_handle,
					 &m_play_areas,
					 &m_play_off,
					 &m_play_size);
		if (err < 0) {
			if (m_alsa.HandleInterruption(m_alsa.m_play_handle,
						      err, &m_abort))
				goto retry;
			m_alsa.m_ei->LogWarn("ALSA play mmap_begin: %s",
					     strerror(-err));
			fillme.m_size = 0;
			return;
		}
		if (!m_play_size) {
			(void) snd_pcm_mmap_commit(m_alsa.m_play_handle,
						   m_play_off, 0);
			m_play_areas = NULL;
			fillme.m_size = 0;
			return;
		}

		fillme.m_data = (((uint8_t *) m_play_areas[0].addr) +
				 (m_play_off * m_alsa.GetSampleBytes()));
		fillme.m_size = m_play_size;
	}

	virtual void SndQueueOBuf(sio_sampnum_t qcount) {
		int err;
		ErrorInfo error;

		/*
		 * We permit callers to remove record buffer data
		 * without ever examining it, but we don't do the
		 * same for playback buffers.
		 */

		assert(m_play_areas);
		assert(qcount <= m_play_size);

		m_play_areas = NULL;
		err = snd_pcm_mmap_commit(m_alsa.m_play_handle,
					  m_play_off, qcount);
		if (err != (int) qcount) {
			m_alsa.m_ei->LogWarn("ALSA play mmap_commit: %s",
					     strerror(-err));
			m_alsa.HandleInterruption(m_alsa.m_play_handle, err,
						  &error);
		}

		else if (snd_pcm_state(m_alsa.m_play_handle) ==
			 SND_PCM_STATE_PREPARED) {
			err = snd_pcm_start(m_alsa.m_play_handle);
			if (err) {
				m_alsa.m_ei->LogWarn("ALSA play start: %s",
						     strerror(-err));
			}
		}

	}

	virtual void SndGetQueueState(SoundIoQueueState &qs) {
		snd_pcm_sframes_t val;

		m_qs.in_queued = 0;
		m_qs.out_queued = 0;

		if (m_alsa.m_rec_handle) {
			val = snd_pcm_avail_update(m_alsa.m_rec_handle);
			if (val > 0)
				m_qs.in_queued = val;
		}
		if (m_alsa.m_play_handle) {
			snd_pcm_avail_update(m_alsa.m_play_handle);
			if (!snd_pcm_delay(m_alsa.m_play_handle, &val)) {
				if (val < 0) {
					/* WTF? */
					val = 0;
				}
				m_qs.out_queued = val;
			}
		}

		qs = m_qs;
	}

	void SndHandleAbort(ErrorInfo error) {
		m_abort.Clear();
		CommitMappings();
		m_alsa.CleanupPcmNotifiers();
		if (cb_NotifyAsyncStop.Registered())
			cb_NotifyAsyncStop(this, error);
	}

	void AsyncProcess(SocketNotifier *notp, int fh) {
		SoundIoQueueState qs;
		bool overrun, underrun;
		ErrorInfo error;

		/*
		 * We will explicitly test for xruns here.
		 */

		if (m_abort) {
			SndHandleAbort(m_abort);
			return;
		}

		if (!m_alsa.CheckNotifications())
			return;

		SndGetQueueState(qs);

		underrun = m_alsa.m_play_xrun;
		m_alsa.m_play_xrun = false;
		if (!underrun && m_alsa.m_play_async) {
			underrun = (snd_pcm_state(m_alsa.m_play_handle) ==
				    SND_PCM_STATE_XRUN);
			if (underrun &&
			    !m_alsa.HandleInterruption(m_alsa.m_play_handle,
						       -EPIPE, &error)) {
				SndHandleAbort(error);
				return;
			}
		}

		overrun = m_alsa.m_rec_xrun;
		m_alsa.m_rec_xrun = false;
		if (!overrun && m_alsa.m_rec_async) {
			overrun = (snd_pcm_state(m_alsa.m_rec_handle) ==
				   SND_PCM_STATE_XRUN);
			if (overrun &&
			    !m_alsa.HandleInterruption(m_alsa.m_rec_handle,
						       -EPIPE, &error)) {
				SndHandleAbort(error);
				return;
			}
		}

		if (cb_NotifyPacket.Registered())
			cb_NotifyPacket(this, qs);

		if (m_abort)
			SndHandleAbort(m_abort);
	}

	virtual bool SndAsyncStart(bool playback, bool capture,
				   ErrorInfo *error) {
		Callback<void, SocketNotifier*, int> tmpl;
		tmpl.Register(this, &SoundIoAlsaMmap::AsyncProcess);
		return m_alsa.CreatePcmNotifiers(playback, capture, tmpl,
						 error);
	}

	virtual void SndAsyncStop(void) {
		CommitMappings();
		m_alsa.CleanupPcmNotifiers();
		m_abort.Clear();
	}

	virtual bool SndIsAsyncStarted(void) const {
		return m_alsa.HasNotifiers();
	}
};

#define trim_leading_ws(X) do { 					\
	while (*(X) && ((*(X) == ' ') || (*(X) == '\t'))) { (X)++; }	\
	} while (0)

#define trim_trailing_ws(X) do {					\
		size_t siz = strlen(X);					\
		while (siz && (((X)[siz - 1] == ' ') ||			\
			       ((X)[siz - 1] == '\t'))) {		\
			(X)[--siz] = '\0';				\
		} } while(0)

SoundIo *
SoundIoCreateAlsa(DispatchInterface *dip, const char *driveropts,
		  ErrorInfo *error)
{
	char *opts = 0, *tok = 0, *save = 0, *tmp;
	const char *ind, *outd;
	bool do_mmap = false;
	SoundIo *alsap = 0;

	ind = "default";
	outd = "default";

	if (driveropts) {
		opts = strdup(driveropts);
		if (!opts) {
			if (error)
				error->SetNoMem();
			return 0;
		}
		tok = strtok_r(opts, "&", &save);
	}

	while (tok) {
		trim_leading_ws(tok);
		if (!strncmp(tok, "in=", 3)) {
			tmp = &tok[3];
			trim_leading_ws(tmp);
			trim_trailing_ws(tmp);
			ind = tmp;
		}
		else if (!strncmp(tok, "out=", 4)) {
			tmp = &tok[4];
			trim_leading_ws(tmp);
			trim_trailing_ws(tmp);
			outd = tmp;
		}
		else if (!strncmp(tok, "dev=", 4)) {
			tmp = &tok[4];
			trim_leading_ws(tmp);
			trim_trailing_ws(tmp);
			ind = outd = tmp;
		}
		else if (!strncmp(tok, "mmap=", 5)) {
			tmp = &tok[5];
			trim_leading_ws(tmp);
			trim_trailing_ws(tmp);
			if (!strcmp(tmp, "on")) {
				do_mmap = true;
			}
			else if (!strcmp(tmp, "off")) {
				do_mmap = false;
			}
			else {
				dip->LogWarn("ALSA: unrecognized mmap "
					     "value \"%s\"", tmp);
			}
		}
		/*
		 * The ALSA device specifiers can themselves be complex
		 * and contain =.  Their initial separator character is
		 * the :, so if we find a : before an =, it's just a
		 * legal ALSA device specifier and we let it through.
		 */
		else if (strchr(tok, '=') &&
			 (!strchr(tok, ':') ||
			  (strchr(tok, '=') < strchr(tok, ':')))) {
			dip->LogWarn("ALSA: unrecognized option \"%s\"",
				     tok);
		}
		else {
			tmp = tok;
			trim_leading_ws(tmp);
			trim_trailing_ws(tmp);
			ind = outd = tmp;
		}

		tok = strtok_r(NULL, "&", &save);
	}

	if (do_mmap)
		alsap = new SoundIoAlsaMmap(dip, outd, ind);
	else
		alsap = new SoundIoAlsaProc(dip, outd, ind);

	if (!alsap) {
		if (error)
			error->SetNoMem();
		return 0;
	}

	if (opts)
		free(opts);
	return alsap;
}

SoundIoDeviceList *
SoundIoGetDeviceListAlsa(ErrorInfo *error)
{
	SoundIoDeviceList *infop;
	void **hints;
	int i;

	infop = new SoundIoDeviceList;
	if (!infop) {
		if (error)
			error->SetNoMem();
		return 0;
	}

	i = snd_device_name_hint(-1, "pcm", &hints);
	if (i < 0) {
		delete infop;
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_SYSCALL,
				   "ALSA snd_device_name_hint: %s",
				   strerror(-i));
		return 0;
	}

	for (i = 0; hints[i]; i++) {
		if (!infop->Add(snd_device_name_get_hint(hints[i], "NAME"),
				snd_device_name_get_hint(hints[i], "DESC"))) {
			delete infop;
			infop = 0;
			if (error)
				error->SetNoMem();
			break;
		}
			
	}

	snd_device_name_free_hint(hints);
	return infop;
}

#else  /* defined(USE_ALSA_SOUNDIO) */
SoundIo *
SoundIoCreateAlsa(DispatchInterface *dip, const char *devspec)
{
	return 0;
}
SoundIoDeviceList *
SoundIoGetDeviceListAlsa(void)
{
	return 0;
}
#endif  /* defined(USE_ALSA_SOUNDIO) */

} /* namespace libhfp */
