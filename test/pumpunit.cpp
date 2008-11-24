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

/*
 * Unit test for SoundIoPump
 * Currently this test is very weak and doesn't catch all the
 * types of problems it should be catching.
 */

#include <stdio.h>
#include <assert.h>

#include <libhfp/soundio.h>
#include <libhfp/soundio-buf.h>
#include <libhfp/events-indep.h>

using namespace libhfp;


class SoundIoTestEp : public SoundIo {
public:
	SoundIoFormat	m_fmt;
	bool		m_do_sink, m_do_source;
	bool		m_async_sink, m_async_source;
	const char	*m_name;
	sio_sampnum_t	m_buf_size;
	bool		m_has_clock;
	VarBuf		m_source_buf;
	VarBuf		m_sink_buf;

	uint8_t		m_source_seq;
	uint8_t		m_sink_seq;

	bool		m_source_overflow;
	bool		m_sink_underflow;

	SoundIoTestEp(const char *name, sio_sampnum_t bufsize)
		: m_do_sink(false), m_do_source(false),
		  m_async_sink(false), m_async_source(false),
		  m_name(name), m_buf_size(bufsize), m_has_clock(true),
		  m_source_overflow(false), m_sink_underflow(false) {}

	~SoundIoTestEp() {
		SndClose();
	}

	virtual void SndGetFormat(SoundIoFormat &format) const {
		format = m_fmt;
	}

	virtual bool SndSetFormat(SoundIoFormat &format,
				  ErrorInfo *error = 0) {
		if ((m_do_sink || m_do_source) &&
		    ((format.samplerate != m_fmt.samplerate) ||
		     (format.sampletype != m_fmt.sampletype) ||
		     (format.nchannels != m_fmt.nchannels))) {
			error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_FORMAT_MISMATCH,
				   "Format mismatch");
			return false;
		}

		m_fmt = format;
		return true;
	}

	virtual void SndGetProps(SoundIoProps &props) const {
		props.has_clock = m_has_clock;
		props.does_source = m_do_source;
		props.does_sink = m_do_sink;
		props.does_loop = false;
		props.remove_on_exhaust = true;
		props.outbuf_size = m_source_buf.m_size;
	}

	virtual bool SndOpen(bool sink, bool source, ErrorInfo *error = 0) {
		assert(!m_do_sink && !m_do_source);
		if (sink) {
			if (!m_sink_buf.AllocateBuffer(m_fmt.bytes_per_record *
						       m_buf_size)) {
				if (error)
					error->SetNoMem();
				return false;
			}
			m_sink_buf.m_start = 0;
			m_sink_buf.m_end = 0;
			m_sink_seq = 0;
			m_do_sink = true;
		}
		if (source) {
			if (!m_source_buf.AllocateBuffer(
				    m_fmt.bytes_per_record * m_buf_size)) {
				SndClose();
				if (error)
					error->SetNoMem();
				return false;
			}
			m_source_buf.m_start = 0;
			m_source_buf.m_end = 0;
			m_source_seq = 0;
			m_do_source = true;
		}

		m_source_overflow = false;
		m_sink_underflow = false;
		return true;
	}

	virtual void SndClose(void) {
		SndAsyncStop();
		if (m_do_sink) {
			m_sink_buf.FreeBuffer();
			m_do_sink = false;
		}
		if (m_do_source) {
			m_source_buf.FreeBuffer();
			m_do_source = false;
		}

		m_source_overflow = false;
		m_sink_underflow = false;
	}

	void FillOutput(void) {
		uint8_t *bufp;
		sio_sampnum_t i;

		bufp = m_source_buf.GetSpace(m_fmt.packet_samps *
					     m_fmt.bytes_per_record);
		assert(bufp);

		for (i = 0; i < m_fmt.packet_samps; i++) {
			memset(bufp, m_source_seq, m_fmt.bytes_per_record);
			bufp += m_fmt.bytes_per_record;
			m_source_seq++;
		}

		m_source_buf.m_end += (m_fmt.packet_samps *
				       m_fmt.bytes_per_record);
	}

	void ConsumeInput(void) {
		size_t nbytes = m_fmt.packet_samps * m_fmt.bytes_per_record;
		if (m_sink_buf.SpaceUsed() < nbytes) {
			m_sink_buf.m_start = 0;
			m_sink_buf.m_end = 0;
		} else {
			m_sink_buf.m_start += nbytes;
		}
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
		m_source_overflow = false;
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

	void CheckOBuf(const uint8_t *buf, size_t len) {
		int subsamp, bpr, count = 0;
		bool last_mismatch = false;

		bpr = m_fmt.bytes_per_record;
		assert(!(len % bpr));

		while (len) {
			if (buf[0] != m_sink_seq) {
				if (!last_mismatch) {
					fprintf(stderr,
						"[%s] Sample %d has "
						"mismatching sequence number: "
						"expect: 0x%02x got: 0x%02x\n",
						m_name, count, m_sink_seq,
						buf[0]);
				}
				m_sink_seq = buf[0];
				last_mismatch = true;
			} else {
				last_mismatch = false;
			}

			for (subsamp = 1; subsamp < bpr; subsamp++) {
				if (buf[subsamp] != m_sink_seq) {
					fprintf(stderr,
						"[%s] Mismatched subsample at "
						"position %d: "
						"expect: 0x%02x got: 0x%02x\n",
						m_name, subsamp, m_sink_seq,
						buf[subsamp]);
				}
			}

			buf += bpr;
			len -= bpr;
			count++;
			m_sink_seq++;
		}
	}

	virtual void SndQueueOBuf(sio_sampnum_t samps) {
		size_t xend = m_sink_buf.m_end;

		m_sink_buf.m_end += (samps * m_fmt.bytes_per_record);
		assert(m_sink_buf.m_end <= m_sink_buf.m_size);

		/*
		 * Analyze what was submitted
		 */
		if (samps) {
			assert(m_sink_buf.SpaceUsed());
			CheckOBuf(&m_sink_buf.m_buf[xend],
				  m_sink_buf.m_end - xend);
		}

		m_sink_underflow = false;
	}

	virtual void SndGetQueueState(SoundIoQueueState &qs) {
		qs.in_queued = m_do_source
			? (m_source_buf.SpaceUsed() / m_fmt.bytes_per_record)
			: 0;
		qs.out_queued = m_do_sink
			? (m_sink_buf.SpaceUsed() / m_fmt.bytes_per_record)
			: 0;
		qs.in_overflow = m_source_overflow;
		qs.out_underflow = m_sink_underflow;
	}

	virtual bool SndAsyncStart(bool sink, bool source, ErrorInfo *error) {
		assert(!m_async_sink && !m_async_source);
		assert(sink || source);
		if (!m_has_clock) {
			if (error)
				error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
					   LIBHFP_ERROR_SOUNDIO_NO_CLOCK,
					   "Not a clocked endpoint");
			return false;
		}
		m_async_sink = sink;
		m_async_source = source;
		return true;
	}

	virtual void SndAsyncStop(void) {
		m_async_sink = false;
		m_async_source = false;
	}

	virtual bool SndIsAsyncStarted(void) const {
		return m_async_sink || m_async_source;
	}

	void DoAsync(void) {
		SoundIoQueueState qs;
		SndGetQueueState(qs);
		cb_NotifyPacket(this, qs);
	}
};


int
main(int argc, char **argv)
{
	IndepEventDispatcher disp;
	SoundIoTestEp top("Top", 10000), bot("Bot", 10000);
	SoundIoPump pump(&disp, &bot);
	SoundIoFormat xfmt;
	bool res;
	int i;

	xfmt.samplerate = 10000;
	xfmt.sampletype = SIO_PCM_U8;
	xfmt.nchannels = 3;
	xfmt.bytes_per_record = 3;
	xfmt.packet_samps = 32;

	bot.SndSetFormat(xfmt);
	top.SndSetFormat(xfmt);
	res = pump.SetTop(&top);
	assert(res);
	res = bot.SndOpen(true, true);
	assert(res);
	res = top.SndOpen(true, true);
	assert(res);
	res = pump.Start();
	assert(res);
	assert(bot.SndIsAsyncStarted());

	for (i = 0; i < 10000; i++) {

		if (i == 309)
			printf("Hi\n");

		bot.FillOutput();
		bot.ConsumeInput();
		bot.DoAsync();
	
		top.FillOutput();
		top.ConsumeInput();
		top.DoAsync();

		assert(pump.IsStarted());
	}

	pump.Stop();

	return 0;
}
