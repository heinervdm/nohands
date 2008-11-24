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
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#if defined(USE_OSS_SOUNDIO)
#include <linux/soundcard.h>
#endif

#include <libhfp/soundio.h>
#include <libhfp/soundio-buf.h>

namespace libhfp {

/*
 * OSS (deprecated) backend SoundIo implemetation
 */

#if defined(USE_OSS_SOUNDIO)

static bool
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
 * Sound I/O routines for deprecated OSS.  Don't use this unless you have to.
 */
class OssSoundIo : public SoundIoBufferBase {
	int				m_play_fh;
	int				m_rec_fh;

	char				*m_play_path;
	char				*m_rec_path;

	bool				m_play_nonblock;
	bool				m_rec_nonblock;

	SoundIoFormat			m_format;
	sio_sampnum_t			m_obuf_size;

	DispatchInterface		*m_ei;
	SocketNotifier			*m_not;

public:
	OssSoundIo(DispatchInterface *eip,
		   const char *play_dev, const char *rec_dev)
		: m_play_fh(-1), m_rec_fh(-1),
		  m_play_path(NULL), m_rec_path(NULL), m_obuf_size(0),
		  m_ei(eip), m_not(NULL) {
		if (play_dev)
			m_play_path = strdup(play_dev);
		if (rec_dev)
			m_rec_path = strdup(rec_dev);

		/* Set a default format */
		memset(&m_format, 0, sizeof(m_format));
		m_format.sampletype = SIO_PCM_S16_LE;
		m_format.samplerate = 8000;
		m_format.packet_samps = 128;
		m_format.nchannels = 1;
		m_format.bytes_per_record = 2;
	}

	virtual ~OssSoundIo() {
		SndClose();
		if (m_play_path)
			free(m_play_path);
		if (m_rec_path)
			free(m_rec_path);
	}

	bool InitOss(int fh, SoundIoFormat &format, ErrorInfo *error) {
		long data;
		int p2 = 0, target;

		/*
		 * This is just passing a hint to the sound driver.
		 * There is no way to query the effective fragment size.
		 * The whole thing seems inadequate.  If I really cared,
		 * there would be a method of determining the fragment
		 * size by observation.  But nobody cares about OSS.
		 */

		target = format.packet_samps;
		if (!target)
			target = 128;	/* Pull numbers out of our asses! */
		else if (target < 16)
			target = 16;

		for (p2 = 0; (1 << p2) < target; p2++) {}

		m_ei->LogDebug("OSS: using fragment order %d (%d)",
			       p2, (1 << p2));

		data = (2048 << 16) | p2;

		if (ioctl(fh, SNDCTL_DSP_SETFRAGMENT, &data) < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "OSS set fragment params: %s",
				      strerror(errno));
			return false;
		}

		format.packet_samps = (1 << p2);
		return true;
	}

	bool SetupOss(int fh, SoundIoFormat &format, ErrorInfo *error) {
		long data;

		switch (format.sampletype) {
		case SIO_PCM_U8:
			data = AFMT_U8;
			break;
		case SIO_PCM_S16_LE:
			data = AFMT_S16_LE;
			break;
		case SIO_PCM_A_LAW:
			data = AFMT_A_LAW;
			break;
		case SIO_PCM_MU_LAW:
			data = AFMT_MU_LAW;
			break;
		default:
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_FORMAT_UNKNOWN,
				      "Unrecognized sample format %d",
				      format.sampletype);
			return false;
		}

		if (ioctl(fh, SNDCTL_DSP_RESET, 0) < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "OSS reset device: %s",
				      strerror(errno));
			return false;
		}

		if (ioctl(fh, SNDCTL_DSP_SETFMT, &data) < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "OSS set sample format: %s",
				      strerror(errno));
			return false;
		}

		data = format.nchannels;
		if (ioctl(fh, SNDCTL_DSP_CHANNELS, &data) < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "OSS set channels: %s",
				      strerror(errno));
			return false;
		}

		data = format.samplerate;
		if (ioctl(fh, SNDCTL_DSP_SPEED, &data) < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "OSS set sample rate: %s",
				      strerror(errno));
			return false;
		}

		return true;
	}

	bool SetupOssPlayback(int fh, SoundIoFormat &format,
			      ErrorInfo *error) {
		audio_buf_info bi;
		if (ioctl(fh, SNDCTL_DSP_GETOSPACE, &bi) < 0) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_SYSCALL,
				      "OSS get output space: %s",
				      strerror(errno));
			return false;
		}
		m_obuf_size = (bi.fragstotal * bi.fragsize) /
			format.bytes_per_record;
		return true;
	}

	virtual bool SndOpen(bool play, bool capture, ErrorInfo *error) {
		bool same_fh = false;;

		if ((m_play_fh >= 0) || (m_rec_fh >= 0)) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
					   LIBHFP_ERROR_SOUNDIO_ALREADY_OPEN,
					   "Device already open");
			return false;
		}
		if (play && !m_play_path) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
					   "No playback device configured");
			return false;
		}
		if (capture && !m_rec_path) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
					   "No capture device configured");
			return false;
		}

		if (play && capture && !strcmp(m_play_path, m_rec_path))
			same_fh = true;

		if (play) {
			m_play_fh = open(m_play_path,
					 same_fh ? O_RDWR : O_WRONLY);
			if (m_play_fh < 0) {
				m_ei->LogWarn(error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
					      LIBHFP_ERROR_SOUNDIO_SYSCALL,
					      "Open playback device: %s",
					      strerror(errno));
				return false;
			}

			if (!InitOss(m_play_fh, m_format, error) ||
			    !SetupOss(m_play_fh, m_format, error) ||
			    !SetupOssPlayback(m_play_fh, m_format, error)) {
				m_ei->LogWarn("OSS: could not configure "
					      "playback device");
				SndClose();
				return false;
			}
		}

		if (capture && same_fh) {
			m_rec_fh = m_play_fh;
		}

		else if (capture) {
			m_rec_fh = open(m_rec_path, O_RDONLY);
			if (m_rec_fh < 0) {
				m_ei->LogWarn(error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
					      LIBHFP_ERROR_SOUNDIO_SYSCALL,
					      "Open capture device: %s",
					      strerror(errno));
				SndClose();
				return false;
			}

			if (!InitOss(m_rec_fh, m_format, error) ||
			    !SetupOss(m_rec_fh, m_format, error)) {
				m_ei->LogWarn("OSS: could not configure "
					      "capture device");
				SndClose();
				return false;
			}
		}

		m_play_nonblock = false;
		m_rec_nonblock = false;
		BufOpen(m_format.packet_samps,
			m_format.bytes_per_record);
		return true;
	}

	virtual void SndClose(void) {
		SndAsyncStop();
		BufClose();
		if (m_play_fh >= 0) {
			close(m_play_fh);
			if (m_rec_fh == m_play_fh) {
				m_rec_fh = -1;
			}
			m_play_fh = -1;
		}
		if (m_rec_fh >= 0) {
			close(m_rec_fh);
			m_rec_fh = -1;
		}
		m_obuf_size = 0;
	}

	virtual void SndGetProps(SoundIoProps &props) const {
		props.has_clock = true;
		props.does_source = (m_rec_fh >= 0);
		props.does_sink = (m_play_fh >= 0);
		props.does_loop = false;
		props.remove_on_exhaust = false;
		props.outbuf_size = m_obuf_size;
	}

	virtual void SndGetFormat(SoundIoFormat &format) const {
		format = m_format;
	}

	virtual bool SndSetFormat(SoundIoFormat &format, ErrorInfo *error) {
		if (m_play_fh >= 0) {
			SndAsyncStop();
			if (!SetupOss(m_play_fh, format, error))
				return false;
			if (!SetupOssPlayback(m_play_fh, format, error)) {
				(void) SetupOss(m_play_fh, m_format, 0);
				return false;
			}
			if ((m_rec_fh != m_play_fh) &&
			    !SetupOss(m_rec_fh, format, error)) {
				(void) SetupOss(m_play_fh, m_format, 0);
				(void) SetupOssPlayback(m_play_fh, m_format,0);
				return false;
			}

			BufOpen(format.packet_samps,
				format.bytes_per_record);
		}
		m_format = format;
		return true;
	}

	virtual void SndPushInput(bool nonblock) {
		unsigned int nsamples;
		uint8_t *buf;
		ssize_t err;
		int res;
		ErrorInfo error;

		if (m_rec_nonblock != nonblock) {
			if (!SetNonBlock(m_rec_fh, nonblock)) {
				m_ei->LogWarn("OSS set rec nonblock: %s",
					      strerror(errno));
			}
			m_rec_nonblock = nonblock;
			if (m_play_fh == m_rec_fh)
				m_play_nonblock = nonblock;
		}

		while (1) {
			nsamples = 0;
			m_input.GetUnfilled(buf, nsamples);
			err = read(m_rec_fh, buf,
				   nsamples * m_format.bytes_per_record);
			if (err < 0) {
				if (errno == EAGAIN) { break; }
				res = errno;
				m_ei->LogWarn(&error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
					      LIBHFP_ERROR_SOUNDIO_SYSCALL,
					      "OSS capture failed: %s",
					      strerror(res));
				BufAbort(m_ei, error);
				break;
			}
			if (!err) { break; }
			m_input.PutUnfilled(err / m_format.bytes_per_record);
		}
	}

	virtual void SndPushOutput(bool nonblock) {
		unsigned int nsamples;
		uint8_t *buf;
		ssize_t err;
		int res;
		ErrorInfo error;

		if (m_play_nonblock != nonblock) {
			if (!SetNonBlock(m_play_fh, nonblock)) {
				m_ei->LogWarn("OSS set play nonblock "
					      "failed");
			}
			m_play_nonblock = nonblock;
			if (m_play_fh == m_rec_fh)
				m_rec_nonblock = nonblock;
		}

		while (1) {
			nsamples = 0;
			m_output.Peek(buf, nsamples);
			if (!nsamples) { break; }

			err = write(m_play_fh, buf,
				    nsamples * m_format.bytes_per_record);
			if (err < 0) {
				if (errno == EAGAIN) {
					m_ei->LogWarn("OSS: playback buffer "
						      "full");
					break;
				}
				res = errno;
				m_ei->LogWarn(&error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
					      LIBHFP_ERROR_SOUNDIO_SYSCALL,
					      "OSS playback failed: %s",
					      strerror(res));
				BufAbort(m_ei, error);
				break;
			}
			if (!err) { break; }
			m_output.Dequeue(err / m_format.bytes_per_record);
		}
	}

	void AsyncProcess(SocketNotifier *notp, int fh) {
		int delay = 0;

		if (m_play_fh >= 0) {
			if (ioctl(m_play_fh, SNDCTL_DSP_GETODELAY,
				  &delay) < 0) {
				m_ei->LogWarn("OSS GETOSPACE: %s",
					      strerror(errno));
				delay = m_hw_outq;
			} else {
				delay /= m_format.bytes_per_record;
			}
		}

		SndPushInput(true);
		BufProcess(delay, false, false);
	}

	virtual bool SndAsyncStart(bool playback, bool capture,
				   ErrorInfo *error) {
		if (m_not) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
					   LIBHFP_ERROR_SOUNDIO_ALREADY_OPEN,
					   "Streaming already in progress");
			return false;
		}
		if (!playback && !capture) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
					   "Neither source nor sink mode "
					   "requested");
			return false;
		}
		if (playback && (m_play_fh < 0)) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
					   "Device not open for playback");
			return false;
		}
		if (capture && (m_rec_fh < 0)) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
					   "Device not open for capture");
			return false;
		}
		if (playback && capture) { playback = false; }

		if (!m_not) {
			m_not = m_ei->NewSocket(capture
						    ? m_rec_fh
						    : m_play_fh, false);
			if (!m_not) {
				if (error)
					error->SetNoMem();
				return false;
			}
			m_not->Register(this, &OssSoundIo::AsyncProcess);
		}
		return true;
	}

	virtual void SndAsyncStop(void) {
		BufStop();
		BufClose();
		if (m_not) {
			delete m_not;
			m_not = 0;
		}
	}

	virtual bool SndIsAsyncStarted(void) const {
		return (m_not != 0);
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
SoundIoCreateOss(DispatchInterface *dip, const char *driveropts,
		 ErrorInfo *error)
{
	char *opts = 0, *tok, *save = 0, *tmp;
	const char *ind = "/dev/dsp", *outd = "/dev/dsp";
	SoundIo *ossp = 0;

	if (driveropts && driveropts[0]) {
		opts = strdup(driveropts);
		if (!opts) {
			if (error)
				error->SetNoMem();
			return 0;
		}
	}

	tok = strtok_r(opts, "&", &save);
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
		else if (strchr(tok, '=')) {
			dip->LogWarn("OSS: unrecognized option \"%s\"",
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

	ossp = new OssSoundIo(dip, outd, ind);

	if (!ossp) {
		if (error)
			error->SetNoMem();
		return 0;
	}

	if (opts)
		free(opts);
	return ossp;
}

SoundIoDeviceList *
SoundIoGetDeviceListOss(ErrorInfo *error)
{
	SoundIoDeviceList *infop;
	mixer_info mi;
	int i, fh, res;
	char buf[16];

	infop = new SoundIoDeviceList;
	if (!infop) {
		if (error)
			error->SetNoMem();
		return 0;
	}

	/* Probe for devices the hard way */
	for (i = 0; i < 16; i++) {
		if (i > 0) {
			snprintf(buf, sizeof(buf), "/dev/dsp%d", i);
		} else {
			strcpy(buf, "/dev/dsp");
		}

		fh = open(buf, O_RDONLY);
		if (fh < 0)
			continue;

		res = ioctl(fh, SOUND_MIXER_INFO, &mi);
		close(fh);

		mi.name[sizeof(mi.name) - 1] = '\0';
		if ((res >= 0) && !infop->Add(buf, mi.name)) {
			delete infop;
			if (error)
				error->SetNoMem();
			return 0;
		}
	}

	return infop;
}

#else  /* defined(USE_OSS_SOUNDIO) */
SoundIo *
SoundIoCreateOss(DispatchInterface *dip, const char *driveropts)
{
	return 0;
}
SoundIoDeviceList *
SoundIoGetDeviceInfoOss(void)
{
	return 0;
}
#endif  /* defined(USE_OSS_SOUNDIO) */

} /* namespace libhfp */
