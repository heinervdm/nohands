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

#if defined(USE_SPEEXDSP)
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#endif /* defined(USE_SPEEXDSP) */

#if defined(USE_AUDIOFILE)
#include <audiofile.h>
#endif

#include <libhfp/soundio.h>
#include <libhfp/soundio-buf.h>

namespace libhfp {


SoundIoDeviceList::
~SoundIoDeviceList()
{
	SoundIoDeviceList::InfoNode *nodep;

	while (m_first) {
		nodep = m_first;
		m_first = m_first->m_next;
		free(nodep);
	}
}


bool SoundIoDeviceList::
Add(const char *name, const char *desc)
{
	SoundIoDeviceList::InfoNode *nodep;
	char *str;
	size_t siz;

	siz = sizeof(*nodep);
	if (name)
		siz += (strlen(name) + 1);
	if (desc)
		siz += (strlen(desc) + 1);
	nodep = (SoundIoDeviceList::InfoNode *) malloc(siz);
	if (!nodep)
		return 0;

	memset(nodep, 0, sizeof(*nodep));

	str = (char *) (nodep + 1);
	if (name) {
		strcpy(str, name);
		nodep->m_name = str;
		str += (1 + strlen(str));
	} else {
		nodep->m_name = "";
	}

	if (desc) {
		strcpy(str, desc);
		nodep->m_desc = str;
		str += (1 + strlen(str));
	} else {
		nodep->m_desc = "";
	}

	if (m_last) {
		m_last->m_next = nodep;
	} else {
		assert(!m_first);
		m_first = nodep;
	}
	m_last = nodep;

	return nodep;
}


class SoundIoMembuf : public SoundIo {
public:
	SoundIoFormat		m_fmt;
	bool			m_do_sink, m_do_source;
	VarBuf			m_sink_buf;
	VarBuf			m_source_buf;
	sio_sampnum_t		m_nsamples;

	SoundIoMembuf(SoundIoFormat const &format, sio_sampnum_t nsamples)
		: m_fmt(format), m_do_sink(false), m_do_source(false),
		  m_nsamples(nsamples) {
	}
	virtual ~SoundIoMembuf() {
		SndClose();
		m_sink_buf.FreeBuffer();
		m_source_buf.FreeBuffer();
	}

	virtual bool SndOpen(bool sink, bool source, ErrorInfo *error) {
		if (!source && !sink) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
					   "Neither source nor sink mode set");
			return false;
		}

		m_do_sink = sink;
		m_do_source = source;

		if (m_do_source && m_sink_buf.m_buf) {
			/* Swap the buffers */
			m_source_buf.FreeBuffer();
			m_source_buf = m_sink_buf;
			m_source_buf.m_size = m_source_buf.m_end;
			m_sink_buf.m_buf = 0;
		}

		if (m_do_sink) {
			m_sink_buf.FreeBuffer();
			m_sink_buf.m_start = m_sink_buf.m_end = 0;
			if (!m_sink_buf.AllocateBuffer(m_nsamples *
					       m_fmt.bytes_per_record)) {
				if (error)
					error->SetNoMem();
				return false;
			}
		}

		if (m_do_source)
			m_source_buf.m_start = 0;

		return true;
	}
	virtual void SndClose(void) {
		m_do_sink = false;
		m_do_source = false;
	}

	virtual void SndGetFormat(SoundIoFormat &format) const {
		format = m_fmt;
	}

	virtual bool SndSetFormat(SoundIoFormat &format, ErrorInfo *error) {
		if ((m_do_sink || m_do_source) &&
		    ((format.samplerate != m_fmt.samplerate) ||
		     (format.sampletype != m_fmt.sampletype) ||
		     (format.nchannels != m_fmt.nchannels))) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_FORMAT_MISMATCH,
					   "Format does not match "
					   "preconfigured format");
			return false;
		}
		m_fmt = format;
		return true;
	}

	virtual void SndGetProps(SoundIoProps &props) const {
		props.has_clock = false;
		props.does_source = m_do_source;
		props.does_sink = m_do_sink;
		props.does_loop = false;
		props.remove_on_exhaust = true;
		props.outbuf_size = m_source_buf.m_size;
	}

	virtual void SndGetIBuf(SoundIoBuffer &fillme) {
		if (!m_do_source || !m_source_buf.m_buf) {
			fillme.m_size = 0;
			return;
		}
		if (!fillme.m_size ||
		    (fillme.m_size > (m_source_buf.SpaceUsed() /
				      m_fmt.bytes_per_record))) {
			fillme.m_size = (m_source_buf.SpaceUsed() /
					 m_fmt.bytes_per_record);
		}
		fillme.m_data = m_source_buf.GetStart();
	}
	virtual void SndDequeueIBuf(sio_sampnum_t samps) {
		if (samps > (m_source_buf.SpaceUsed() /
			     m_fmt.bytes_per_record)) {
			assert(!m_source_buf.SpaceUsed());
			return;
		}
		m_source_buf.m_start += (samps * m_fmt.bytes_per_record);
		assert(m_source_buf.m_start <= m_source_buf.m_end);
	}
	virtual void SndGetOBuf(SoundIoBuffer &fillme) {
		int nbytes;
		if (!m_do_sink || !m_sink_buf.m_buf) {
			fillme.m_size = 0;
			return;
		}
		if (!fillme.m_size ||
		    (fillme.m_size > (m_sink_buf.SpaceFree() /
				      m_fmt.bytes_per_record))) {
			fillme.m_size = (m_sink_buf.SpaceFree() /
					 m_fmt.bytes_per_record);
		}
		nbytes = fillme.m_size * m_fmt.bytes_per_record;
		fillme.m_data = m_sink_buf.GetSpace(nbytes);
	}
	virtual void SndQueueOBuf(sio_sampnum_t samps) {
		m_sink_buf.m_end += (samps * m_fmt.bytes_per_record);
		assert(m_sink_buf.m_end <= m_sink_buf.m_size);
	}
	virtual void SndGetQueueState(SoundIoQueueState &qs) {
		qs.in_queued = m_do_source
			? (m_source_buf.SpaceUsed() / m_fmt.bytes_per_record)
			: 0;
		qs.out_queued = m_do_sink
			? (m_sink_buf.SpaceUsed() / m_fmt.bytes_per_record)
			: 0;
		qs.in_overflow = false;
		qs.out_underflow = false;
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


SoundIo *
SoundIoCreateMembuf(const SoundIoFormat *fmt, sio_sampnum_t nsamps)
{
	return new SoundIoMembuf(*fmt, nsamps);
}


#if defined(USE_AUDIOFILE)
class SoundIoAudioFile : public SoundIo {
	DispatchInterface		*m_ei;
	char				*m_filename;
	bool				m_create;
	SoundIoFormat			m_fmt;
	AFfilehandle			m_handle;
	bool				m_write;
	int				m_track;
	uint8_t				*m_buf;
	sio_sampnum_t			m_buf_size;
	sio_sampnum_t			m_offset;
	sio_sampnum_t			m_length;

	enum {
		c_bufsize = 4096,
	};

public:
	bool IsOpen(void) const { return (m_handle != AF_NULL_FILEHANDLE); }

	void SndGetProps(SoundIoProps &props) const {
		props.has_clock = false;
		props.does_source = IsOpen() && !m_write;
		props.does_sink = IsOpen() && m_write;
		props.does_loop = false;
		props.remove_on_exhaust = true;
		props.outbuf_size = 0;
	}

	void SndGetFormat(SoundIoFormat &fmt) const { fmt = m_fmt; }
	bool SndSetFormat(SoundIoFormat &fmt, ErrorInfo *error) {
		if (!IsOpen()) {
			m_fmt = fmt;
			return true;
		}
		if ((fmt.samplerate == m_fmt.samplerate) &&
		     (fmt.sampletype == m_fmt.sampletype) &&
		    (fmt.nchannels == m_fmt.nchannels))
			return true;

		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_FORMAT_MISMATCH,
				   "Requested format does not match format "
				   "of open file");
		return false;
	}

	bool SndOpen(bool sink, bool source, ErrorInfo *error) {
		AFfilesetup fs = NULL;
		int *tracks, ntracks;

		if ((sink && source) || (!sink && !source)) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
					   "AudioFile must be open for only "
					   "one direction");
			return false;
		}
		if (IsOpen()) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
					   LIBHFP_ERROR_SOUNDIO_ALREADY_OPEN,
					   "AudioFile already open");
			return false;
		}

		if (sink) {
			m_track = AF_DEFAULT_TRACK;
			fs = afNewFileSetup();
			afInitFileFormat(fs, AF_FILE_WAVE);
			afInitChannels(fs, m_track, m_fmt.nchannels);
			afInitRate(fs, m_track, m_fmt.samplerate);

			switch (m_fmt.sampletype) {
			case SIO_PCM_U8:
				afInitSampleFormat(fs, m_track,
						   AF_SAMPFMT_UNSIGNED, 8);
				break;
			case SIO_PCM_S16_LE:
				afInitSampleFormat(fs, m_track,
						   AF_SAMPFMT_TWOSCOMP, 16);
				afInitByteOrder(fs, m_track,
						AF_BYTEORDER_LITTLEENDIAN);
				break;
			default:
				afFreeFileSetup(fs);
				m_ei->LogWarn(error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_FORMAT_UNKNOWN,
					      "audiofile: output format "
					      "not supported");
				return false;
			}
		}

		m_handle = afOpenFile(m_filename,
				      sink ? "w" : "r",
				      fs);
		if (fs)
			afFreeFileSetup(fs);

		if (!IsOpen())
			return false;

		ntracks = afGetTrackIDs(m_handle, NULL);
		if (!ntracks) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_BAD_FILE,
				      "audiofile: no tracks?");
			SndClose();
			return false;
		}

		tracks = (int *) malloc(ntracks * sizeof(*tracks));
		if (!tracks) {
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_BAD_FILE,
				      "audiofile: could not allocate "
				      "track list of length %d", ntracks);
			SndClose();
			return false;
		}
		afGetTrackIDs(m_handle, tracks);
		m_track = tracks[0];
		free(tracks);

		if (!sink) {
			int v1, v2, v3;
			m_fmt.samplerate = (sio_sampnum_t)
				afGetRate(m_handle, m_track);
			m_fmt.nchannels = afGetChannels(m_handle, m_track);
			afGetSampleFormat(m_handle, m_track, &v1, &v2);
			v3 = afGetByteOrder(m_handle, m_track);
			if ((v1 == AF_SAMPFMT_UNSIGNED) && (v2 == 8)) {
				m_fmt.sampletype = SIO_PCM_U8;
				m_fmt.bytes_per_record = 1 * m_fmt.nchannels;
			} else if ((v1 == AF_SAMPFMT_TWOSCOMP) &&
				   (v2 == 16) &&
				   (v3 == AF_BYTEORDER_LITTLEENDIAN)) {
				m_fmt.sampletype = SIO_PCM_S16_LE;
				m_fmt.bytes_per_record = 2 * m_fmt.nchannels;
			} else {
				m_ei->LogWarn(error,
					      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_FORMAT_UNKNOWN,
					      "audiofile: format of opened "
					      "file not supported");
				SndClose();
				return false;
			}
			m_fmt.packet_samps = c_bufsize;
		}

		m_write = sink;

		m_buf = (uint8_t *) malloc(c_bufsize * m_fmt.bytes_per_record);
		if (!m_buf) {
			if (error)
				error->SetNoMem();
			SndClose();
			return false;
		}

		m_buf_size = c_bufsize;
		m_offset = 0;
		m_length = 0;
		return true;
	}

	void SndClose(void) {
		if (IsOpen()) {
			if (m_write && m_offset)
				(void) FlushPage();
			afCloseFile(m_handle);
			m_handle = AF_NULL_FILEHANDLE;
		}
		if (m_buf) {
			free(m_buf);
			m_buf = NULL;
		}
	}

	bool NextPage() {
		int res;
		m_length = 0;
		m_offset = 0;
		res = afReadFrames(m_handle, m_track, m_buf, m_buf_size);
		if (res <= 0)
			return false;
		assert(res <= (int) m_buf_size);
		m_length = res;
		return true;
	}

	void SndGetIBuf(SoundIoBuffer &map) {
		if (!IsOpen() || m_write) {
			map.m_size = 0;
			return;
		}
		assert(m_offset <= m_length);
		if ((m_offset == m_length) && !NextPage()) {
			map.m_size = 0;
			return;
		}

		if (!map.m_size || (map.m_size > (m_length - m_offset)))
			map.m_size = m_length - m_offset;
		map.m_data = m_buf + (m_offset * m_fmt.bytes_per_record);
	}

	void SndDequeueIBuf(sio_sampnum_t samps) {
		AFframecount fc;

		if (!IsOpen() || m_write)
			return;

		if (samps <= (m_length - m_offset)) {
			m_offset += samps;
			return;
		}

		samps -= (m_length - m_offset);
		m_offset = m_length;
		fc = afSeekFrame(m_handle, m_track, -1);
		if (fc >= 0)
			afSeekFrame(m_handle, m_track, fc + samps);
	}

	bool FlushPage() {
		int res;
		if (m_offset) {
			res = afWriteFrames(m_handle, m_track,
					    m_buf, m_offset);
			if (res <= 0)
				return false;
			assert(res <= (int) m_offset);
		}
		m_offset = 0;
		return true;
	}

	void SndGetOBuf(SoundIoBuffer &map) {
		if (!IsOpen() || !m_write) {
			map.m_size = 0;
			return;
		}

		if (!map.m_size || (map.m_size > (m_buf_size - m_offset)))
			map.m_size = m_buf_size - m_offset;
		map.m_data = m_buf + (m_offset * m_fmt.bytes_per_record);
	}

	void SndQueueOBuf(sio_sampnum_t samps) {
		assert(samps <= (m_buf_size - m_offset));
		m_offset += samps;
		if (m_offset == m_buf_size)
			(void) FlushPage();
	}

	void SndGetQueueState(SoundIoQueueState &qs) {
		qs.in_queued = 0;
		qs.out_queued = 0;
		qs.in_overflow = false;
		qs.out_underflow = false;

		if (IsOpen() && !m_write) {
			qs.in_queued = afGetFrameCount(m_handle, m_track)
				- (afSeekFrame(m_handle, m_track, -1) -
				   (m_length - m_offset));
		}
	}

	bool SndAsyncStart(bool sink, bool source, ErrorInfo *error) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_NO_CLOCK,
				   "Not a clocked endpoint");
		return false;
	}
	void SndAsyncStop(void) {}
	bool SndIsAsyncStarted(void) const { return false; }

	SoundIoAudioFile(DispatchInterface *ei, char *filename, bool create)
		: m_ei(ei), m_filename(filename), m_create(create),
		  m_handle(AF_NULL_FILEHANDLE), m_buf(0), m_buf_size(0) {
		memset(&m_fmt, 0, sizeof(m_fmt));
	}

	virtual ~SoundIoAudioFile() { 
		SndClose();
		free(m_filename);
	}

};
#endif  /* defined(USE_AUDIOFILE) */


SoundIo *
SoundIoCreateFileHandler(DispatchInterface *ei,
			 const char *filename, bool create, ErrorInfo *error)
{
	SoundIo *siop = 0;

	if (!filename || !filename[0]) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_EVENTS,
				   LIBHFP_ERROR_EVENTS_BAD_PARAMETER,
				   "Empty filename specified for audiofile");
		return 0;
	}

#if defined(USE_AUDIOFILE)
	{
		char *xfn;
		xfn = strdup(filename);
		if (!xfn) {
			if (error)
				error->SetNoMem();
			return 0;
		}
		siop = new SoundIoAudioFile(ei, xfn, create);
		if (!siop) {
			free(xfn);
			if (error)
				error->SetNoMem();
		}
	}
	return siop;
#endif

	if (error)
		error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
			   LIBHFP_ERROR_SOUNDIO_NOT_SUPPORTED,
			   "Support for libaudiofile omitted");
	return 0;
}


class SoundIoSnooper : public SoundIoFilter {
	SoundIo		*m_output;
	uint8_t		*m_buf;
	bool		m_half;
	SoundIoFormat	m_fmt;
	bool		m_open;
	bool		m_no_up, m_no_dn;

public:
	SoundIoSnooper(SoundIo *target, bool up, bool dn)
		: m_output(target), m_buf(0), m_half(false),
		  m_open(false), m_no_up(!up), m_no_dn(!dn) {}
	~SoundIoSnooper() {}

	virtual bool FltPrepare(SoundIoFormat const &fmt,
				bool up, bool dn, ErrorInfo *error) {
		SoundIoFormat fmt_copy(fmt);

		if (up && !m_no_up && dn && !m_no_dn) {
			switch (fmt.sampletype) {
			case SIO_PCM_U8:
			case SIO_PCM_S16_LE:
				break;
			default:
				if (error)
					error->Set(
						LIBHFP_ERROR_SUBSYS_SOUNDIO,
					LIBHFP_ERROR_SOUNDIO_FORMAT_UNKNOWN,
						"Format not recognized by "
						"snooper");
				return false;
			}
			m_buf = (uint8_t *) malloc(fmt.packet_samps *
						   fmt.bytes_per_record);
			if (!m_buf) {
				if (error)
					error->SetNoMem();
				return false;
			}
		}

		if ((up && !m_no_up) || (dn && !m_no_dn)) {
			if (!m_output->SndSetFormat(fmt_copy, error) ||
			    !m_output->SndOpen(true, false, error)) {
				if (m_buf) {
					free(m_buf);
					m_buf = 0;
				}
				return false;
			}
			m_open = true;
		}

		m_fmt = fmt;
		return true;
	}

	virtual void FltCleanup(void) {
		if (m_open) {
			m_output->SndClose();
			m_open = false;
		}
		if (m_buf) {
			free(m_buf);
			m_buf = 0;
		}
	}

	void MixBuffer(const uint8_t *buf, sio_sampnum_t size) {
		size *= m_fmt.nchannels;

		switch (m_fmt.sampletype) {
		case SIO_PCM_U8: {
			uint8_t *src = (uint8_t *) buf;
			uint8_t *dest = (uint8_t *) m_buf;
			int32_t tmp;
			while (size) {
				tmp = (((int) (*(src++)) - 128) +
				       ((int) (*dest) - 128));
				if (tmp < -128)
					tmp = -128;
				if (tmp > 127)
					tmp = 127;
				*(dest++) = tmp + 128;
				size--;
			}
			break;
		}
		case SIO_PCM_S16_LE: {
			int16_t *src = (int16_t *) buf;
			int16_t *dest = (int16_t *) m_buf;
			int32_t tmp;
			while (size) {
				tmp = *(src++) + *dest;
				if (tmp < -32768)
					tmp = -32768;
				if (tmp > 32767)
					tmp = -32767;
				*(dest++) = tmp;
				size--;
			}
			break;
		}
		default:
			abort();
		}
	}

	void OutputBuffer(const uint8_t *buf, sio_sampnum_t size) {
		SoundIoBuffer xbuf;
		while (size) {
			xbuf.m_size = size;
			m_output->SndGetOBuf(xbuf);
			if (!xbuf.m_size)
				/* Uh-oh! */
				return;

			memcpy(xbuf.m_data,
			       buf,
			       xbuf.m_size * m_fmt.bytes_per_record);
			m_output->SndQueueOBuf(xbuf.m_size);
			size -= xbuf.m_size;
			buf += (xbuf.m_size * m_fmt.bytes_per_record);
		}
	}

	virtual SoundIoBuffer const *FltProcess(bool up,
						SoundIoBuffer const &src,
						SoundIoBuffer &dest) {
		if (!m_buf) {
			if ((up && !m_no_up) || (!up && !m_no_dn)) {
				assert(m_open);
				OutputBuffer(src.m_data, src.m_size);
			}
			return &src;
		}

		assert(!m_no_up && !m_no_dn);

		if (!up) {
			assert(!m_half);
			memcpy(m_buf,
			       src.m_data,
			       src.m_size * m_fmt.bytes_per_record);
			m_half = true;
			return &src;
		}

		assert(m_half);
		m_half = 0;
		MixBuffer(src.m_data, src.m_size);
		OutputBuffer(m_buf, src.m_size);
		return &src;
	}
};

SoundIoFilter *
SoundIoCreateSnooper(SoundIo *target, bool up, bool dn)
{
	assert(target);
	assert(up || dn);
	return new SoundIoSnooper(target, up, dn);
}



#if defined(USE_SPEEXDSP)
class SoundIoFltSpeexImpl : public SoundIoFltSpeex {
	DispatchInterface		*m_ei;
	SpeexPreprocessState		*m_spsp;
	SpeexEchoState			*m_sesp;
	uint8_t				*m_downpkt;
	bool				m_downpkt_ready;

	sio_sampnum_t			m_packetsize;
	sio_sampnum_t			m_echotail;
	int				m_rate;
	int				m_bps;
	bool				m_running;
	SoundIoSpeexProps		m_props;

public:
	SoundIoFltSpeexImpl(DispatchInterface *ei)
		: m_ei(ei), m_spsp(NULL), m_sesp(NULL), m_downpkt(NULL),
		  m_downpkt_ready(false), m_running(false) {

		m_props.noisereduce = false;
		m_props.echocancel_ms = 0;
		m_props.agc_level = 0;
		m_props.dereverb_level = 0.0;
		m_props.dereverb_decay = 0.0;
	}

	virtual ~SoundIoFltSpeexImpl() {
		assert(!m_running);
	}

	bool InitSpeex(bool do_dn) {
		int val;
		float fval;

		assert(!m_spsp && !m_sesp);

		/*
		 * Intelligent default Values:
		 * framesize = 20ms
		 * echotail = 100ms
		 * agc_level = 8000 (when enabled)
		 * dereverb_level = 0.4 (when enabled)
		 * dereverb_decay = 0.3 (when enabled)
		 */

		if (do_dn && m_props.echocancel_ms) {
			m_echotail = (m_rate * m_props.echocancel_ms) / 1000;
			if (m_echotail < m_packetsize)
				m_echotail = m_packetsize;
		}

		if (m_props.noisereduce ||
		    m_props.agc_level ||
		    m_props.dereverb_level ||
		    m_echotail) {
			m_spsp = speex_preprocess_state_init(m_packetsize,
							     m_rate);
			if (!m_spsp) {
				m_ei->LogWarn("Speex: could not allocate "
					      "preprocess state");
				CleanupSpeex();
				return false;
			}

			val = m_props.noisereduce ? 1 : 0;
			speex_preprocess_ctl(m_spsp,
					     SPEEX_PREPROCESS_SET_DENOISE,
					     &val);
			val = m_props.agc_level ? 1 : 0;
			speex_preprocess_ctl(m_spsp,
					     SPEEX_PREPROCESS_SET_AGC,
					     &val);
			fval = m_props.agc_level ?
				m_props.agc_level : 8000;
			speex_preprocess_ctl(m_spsp,
					     SPEEX_PREPROCESS_SET_AGC_LEVEL,
					     &fval);
			val = m_props.dereverb_level ? 1 : 0;
			speex_preprocess_ctl(m_spsp,
					     SPEEX_PREPROCESS_SET_DEREVERB,
					     &val);
			fval = m_props.dereverb_level;
			speex_preprocess_ctl(m_spsp,
				     SPEEX_PREPROCESS_SET_DEREVERB_DECAY,
					     &fval);
			fval = m_props.dereverb_decay;
			speex_preprocess_ctl(m_spsp,
				     SPEEX_PREPROCESS_SET_DEREVERB_LEVEL,
					     &fval);
		}

		if (m_echotail) {
			m_sesp = speex_echo_state_init(m_packetsize,
						       m_echotail);
			if (!m_sesp) {
				m_ei->LogWarn("Speex: could not allocate "
					      "echo cancel state");
				CleanupSpeex();
				return false;
			}
			speex_echo_ctl(m_sesp, SPEEX_ECHO_SET_SAMPLING_RATE,
				       &m_rate);

			m_downpkt = (uint8_t *) malloc(m_packetsize * m_bps);
			if (!m_downpkt) {
				m_ei->LogWarn("Speex: Could not allocate "
					      "saved packet buffer");
				CleanupSpeex();
				return false;
			}
		}

		m_ei->LogDebug("Echo tail: %i", m_echotail);
		return true;
	}

	void CleanupSpeex(void) {
		if (m_sesp) {
			speex_echo_state_destroy(m_sesp);
			m_sesp = NULL;
		}
		if (m_spsp) {
			speex_preprocess_state_destroy(m_spsp);
			m_spsp = NULL;
		}
		if (m_downpkt) {
			free(m_downpkt);
			m_downpkt = NULL;
		}
	}

	bool Configure(SoundIoSpeexProps const &props, ErrorInfo *error) {
		if (m_running) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
			LIBHFP_ERROR_SOUNDIO_CANNOT_CHANGE_WHILE_STREAMING,
					   "Cannot change DSP parameters "
					   "while streaming");
			return false;
		}

		m_props = props;
		return true;
	}

	bool FltPrepare(SoundIoFormat const &fmt, bool up, bool dn,
			ErrorInfo *error) {
		m_rate = fmt.samplerate;
		m_bps = fmt.bytes_per_record;
		m_packetsize = fmt.packet_samps;

		if (fmt.sampletype != SIO_PCM_S16_LE) {
			/* Speex expects 16-bit little-endian samples */
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_FORMAT_MISMATCH,
				      "Speex requires S16_LE format");
			return false;
		}

		if (fmt.nchannels != 1) {
			/* Speex expects single-channel sample records */
			m_ei->LogWarn(error,
				      LIBHFP_ERROR_SUBSYS_SOUNDIO,
				      LIBHFP_ERROR_SOUNDIO_FORMAT_MISMATCH,
				      "Speex requires single channel "
				      "sample records");
			return false;
		}

		/* There is no sample rate requirement, so we won't check */

		m_downpkt_ready = false;
		m_echotail = 0;

		if (up && !InitSpeex(dn)) {
			error->SetNoMem();
			return false;
		}

		m_running = true;
		return true;
	}

	void FltCleanup(void) {
		assert(m_running);
		assert(!m_downpkt_ready);
		CleanupSpeex();

		m_running = false;
	}

	SoundIoBuffer const *FltProcess(bool up, SoundIoBuffer const &src,
					SoundIoBuffer &dest) {
		assert(src.m_size == m_packetsize);

		if (!up) {
			/* Stash the output packet */
			assert(!m_downpkt_ready);
			if (m_echotail) {
				memcpy(m_downpkt, src.m_data,
				       src.m_size * m_bps);
				m_downpkt_ready = true;
			}

			return &src;
		}

		/* Run the input packet through Speex */
		if (m_echotail) {
			assert(m_downpkt_ready);
			speex_echo_cancellation(m_sesp,
						(spx_int16_t*) src.m_data,
						(spx_int16_t*) m_downpkt,
						(spx_int16_t*) dest.m_data);
			m_downpkt_ready = false;
				
		} else if (m_spsp) {
			memcpy(dest.m_data, src.m_data, dest.m_size * m_bps);
		}

		if (m_spsp) {
			(void) speex_preprocess_run(m_spsp,
						    (spx_int16_t*)dest.m_data);
		}

		return (!m_echotail && !m_spsp) ? &src : &dest;
	}
};
#endif /* defined(USE_SPEEXDSP) */

SoundIoFltSpeex *SoundIoFltCreateSpeex(DispatchInterface *ei)
{
#if defined(USE_SPEEXDSP)
	return new SoundIoFltSpeexImpl(ei);
#else
	return 0;
#endif
}


/*
 * TODO: The rest of this file is unrefined junk left over from the last
 * major overhaul.  It needs to be adapted or removed.
 */

class SoundIoFltDummy : public SoundIoFilter {
public:
	bool m_started;
	bool m_up, m_dn;
	bool m_half;
	sio_sampnum_t m_pktsize;

	bool FltPrepare(SoundIoFormat const &fmt, bool up, bool dn,
			ErrorInfo *error) {
		assert(!m_started);
		m_up = up;
		m_dn = dn;
		m_pktsize = fmt.packet_samps;
		m_started = true;
		assert(up || dn);
		return true;
	}

	void FltCleanup(void) {
		assert(m_started);
		assert(!m_half);
		m_started = false;
	}

	SoundIoBuffer const *FltProcess(bool up, SoundIoBuffer const &src,
					SoundIoBuffer &dest) {
		assert(m_started);
		assert(src.m_size == m_pktsize);
		assert(src.m_size == dest.m_size);
		if (!m_half) {
			if (m_up && m_dn) {
				assert(!up);
				m_half = true;
			} else {
				assert(up == m_up);
			}

		} else {
			assert(m_up && m_dn);
			assert(up);
			m_half = false;
		}
		return &src;
	}

	SoundIoFltDummy(void) : m_started(false), m_up(false), m_dn(false),
				m_half(false) {}
};

SoundIoFilter *
SoundIoFltCreateDummy(void)
{
	return new SoundIoFltDummy();
}

} /* namespace libhfp */
