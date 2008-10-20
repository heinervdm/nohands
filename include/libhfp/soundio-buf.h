/* -*- C++ -*- */
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

#if !defined(__LIBHFP_SOUNDIO_BUF_H__)
#define __LIBHFP_SOUNDIO_BUF_H__

/**
 * @file libhfp/soundio-buf.h
 */

/*
 * Buffer utility classes for SoundIo implementations.
 *
 * Includes:
 * VarBuf            A stupid contiguous buffer used in the loopback class.
 * PacketSeq         A fragment buffer queue used by SoundIoBufferMgr.
 * SoundIoBufferMgr  A general purpose utility class forming the
 *		     base of most non-trivial, non-mmap SoundIo
 *		     implementations.
 */

#include <libhfp/soundio.h>
#include <stdlib.h>
#include <string.h>


namespace libhfp {

struct VarBuf {
	uint8_t		*m_buf;
	size_t		m_size;
	size_t		m_start;
	size_t		m_end;

	void Defragment(void) {
		if (m_start && (m_end == m_start)) {
			m_start = m_end = 0;
		} else if (m_start && (m_end > m_start)) {
			memmove(m_buf, &m_buf[m_start], m_end - m_start);
			m_end -= m_start;
			m_start = 0;
		}
	}

	uint8_t *GetStart(void) { return &m_buf[m_start]; }

	uint8_t *GetSpace(size_t nbytes) {
		if (m_start == m_end)
			m_start = m_end = 0;
		if ((m_size - m_end) >= nbytes) {
			return &m_buf[m_end];
		}
		if ((m_size - m_end + m_start) >= nbytes) {
			Defragment();
			return m_buf;
		}
		return NULL;
	}

	size_t SpaceUsed(void) const { return m_end - m_start; }
	size_t SpaceFree(void) const { return m_size - m_end + m_start; }

	bool AllocateBuffer(size_t nbytes) {
		uint8_t *newbuf;
		if (m_buf) {
			if (SpaceUsed() > nbytes) { return false; }
			if (nbytes <= m_size) { return true; }
		}
		newbuf = (uint8_t *) malloc(nbytes);
		if (m_buf && (m_end > m_start)) {
			memcpy(newbuf, &m_buf[m_start], m_end - m_start);
		}
		free(m_buf);
		m_buf = newbuf;
		m_size = nbytes;
		return true;
	}

	void FreeBuffer(void) {
		if (m_buf) {
			free(m_buf);
			m_buf = NULL;
			m_start = m_end = m_size = 0;
		}
	}

	VarBuf(void) : m_buf(NULL), m_size(0), m_start(0), m_end(0) {}
};


struct SoundIoPacket {
	SoundIoPacket	*m_next;
	uint8_t		m_data[0];
};

struct PacketSeq {
	int		m_bpr;
	int		m_packetsize;
	SoundIoPacket	*m_free;
	int		m_nfree;
	SoundIoPacket	*m_head, *m_tail;
	int		m_npackets;
	int		m_head_start;
	int		m_tail_end;

	enum {
		c_free_buffer_retain = 10,
	};

	int PacketSize(void) const { return m_packetsize / m_bpr; }

	static SoundIoPacket *GetBuffer(int packetbytes) {
		SoundIoPacket *resp = (SoundIoPacket*)
			malloc(packetbytes + sizeof(*resp));
		resp->m_next = NULL;
		return resp;
	}

	SoundIoPacket *GetBuffer(void) {
		assert(m_packetsize);
		SoundIoPacket *resp;
		if (m_free) {
			resp = m_free;
			m_free = resp->m_next;
			m_nfree--;
			resp->m_next = NULL;
			return resp;
		}
		return GetBuffer(m_packetsize);
	}

	void PutBuffer(SoundIoPacket *bufp) {
		if (m_nfree > c_free_buffer_retain) {
			free(bufp);
			return;
		}
		bufp->m_next = m_free;
		m_free = bufp;
		m_nfree++;
	}

	void CollectBuffers(void) {
		while (m_free) {
			SoundIoPacket *bufp = GetBuffer();
			free(bufp);
		}
	}

	void Clear(void) {
		m_head_start = 0;
		m_tail_end = 0;
		while (m_head) {
			assert(m_npackets);
			m_npackets--;
			SoundIoPacket *bufp = m_head;
			m_head = bufp->m_next;
			free(bufp);
		}
		assert(!m_npackets);
		m_tail = NULL;
	}

	int TotalFill(void) const {
		assert(m_packetsize);
		if (!m_npackets) { return 0; }
		if (m_head == m_tail)
			return (m_tail_end - m_head_start) / m_bpr;
		return ((((m_npackets - 2) * m_packetsize) + m_tail_end +
			 (m_packetsize - m_head_start)) / m_bpr);
	}

	bool GetUnfilled(uint8_t *&ptr, unsigned int &nsamples) {
		sio_sampnum_t real_nsamples;
		if (!m_tail) {
			assert(!m_tail_end);
			assert(!m_head_start);
			m_head = m_tail = GetBuffer();
			m_npackets++;
		} else if (m_tail_end == m_packetsize) {
			m_tail->m_next = GetBuffer();
			m_tail = m_tail->m_next;
			m_tail_end = 0;
			m_npackets++;
		}
		real_nsamples = (m_packetsize - m_tail_end) / m_bpr;
		ptr = &m_tail->m_data[m_tail_end];
		if (!nsamples || (nsamples > real_nsamples))
			nsamples = real_nsamples;
		return true;
	}

	void PutUnfilled(int nsamples_added) {
		int nbytes = nsamples_added * m_bpr;
		assert(nbytes >= 0);
		assert((nbytes + m_tail_end) <= m_packetsize);
		m_tail_end += nbytes;
	}

	void Peek(uint8_t *&buf, sio_sampnum_t &nsamples) {
		sio_sampnum_t real_nsamples;
		if (m_head) {
			if (m_head == m_tail)
				real_nsamples = m_tail_end - m_head_start;
			else 
				real_nsamples = m_packetsize - m_head_start;
			real_nsamples /= m_bpr;
			buf = &m_head->m_data[m_head_start];
			if (!nsamples || (nsamples > real_nsamples))
				nsamples = real_nsamples;
		} else {
			nsamples = 0;
		}

	}

	SoundIoPacket *DequeueFirst(void) {
		SoundIoPacket *bufp = m_head;
		assert(!m_head_start);
		if (bufp) {
			m_head = bufp->m_next;
			if (!--m_npackets) {
				m_tail = NULL;
			}
		}
		return bufp;
	}

	void Dequeue(int nsamples) {
		int nbytes = nsamples * m_bpr;
		if (!m_head) {
			assert(!nsamples);
			return;
		}

		assert(nbytes >= 0);
		if (m_head != m_tail) {
			if (m_head_start) {
				if ((m_packetsize - m_head_start) > nbytes) {
					m_head_start += nbytes;
					return;
				}

				nbytes -= (m_packetsize - m_head_start);
				m_head_start = 0;
				PutBuffer(DequeueFirst());
			}

			while (nbytes >= m_packetsize) {
				assert((m_head != m_tail) ||
				       (m_tail_end == m_packetsize));
				PutBuffer(DequeueFirst());
				nbytes -= m_packetsize;
			}

			if (!nbytes)
				return;
		}

		assert(nbytes >= 0);
		if (m_head) {
			assert(nbytes <= (((m_head == m_tail)
					   ? m_tail_end : m_packetsize) -
					  m_head_start));
			m_head_start += nbytes;
			nbytes = 0;
			if ((m_head == m_tail) &&
			    (m_head_start == m_tail_end)) {
				m_head_start = m_tail_end = 0;
				PutBuffer(DequeueFirst());
			}
		}

		assert(!nbytes);
	}

	void SetPacketSize(int packetsize, int bps) {
		Clear();
		CollectBuffers();
		m_packetsize = packetsize * bps;
		m_bpr = bps;
	}

	PacketSeq(int packetsize = 0, int bpr = 2)
		: m_bpr(bpr), m_packetsize(packetsize),
		  m_free(NULL), m_nfree(0),
		  m_head(NULL), m_tail(NULL), m_npackets(0),
		  m_head_start(0), m_tail_end(0) {}

	~PacketSeq() { Clear(); CollectBuffers(); }
};

/*
 * SoundIoBufferMgr
 *
 * This class provides a simple queued buffer implementation of the SoundIo
 * interface.  It serves as the base class of most SoundIo implementations.
 */

/** @brief SoundIo skeleton with integrated buffer management */
class SoundIoBufferBase : public SoundIo {
public:

	PacketSeq		m_input;
	PacketSeq		m_output;
	sio_sampnum_t		m_hw_outq;
	bool			m_abort;
	TimerNotifier		*m_abort_to;

	struct AsyncState {
		bool m_stopped;
	}			*m_async_state;

	SoundIoQueueState	m_qs;

	SoundIoBufferBase(void)
		: m_input(), m_output(), m_hw_outq(0), m_abort(false),
		  m_abort_to(0), m_async_state(0) {}
	virtual ~SoundIoBufferBase() {
		BufCancelAbort();
	}

	/* Override these with methods that fill the FIFOs */
	virtual void SndPushInput(bool nonblock) = 0;
	virtual void SndPushOutput(bool nonblock) = 0;

	void BufCancelAbort(void) {
		m_abort = false;
		if (m_abort_to) {
			m_abort_to->Cancel();
			m_abort_to->Unregister();
			delete m_abort_to;
			m_abort_to = 0;
		}
	}

	virtual void SndHandleAbort(void) {
		BufCancelAbort();
		this->SndAsyncStop();
		if (cb_NotifyPacket.Registered())
			cb_NotifyPacket(this, NULL);
	}

	virtual void SndGetIBuf(SoundIoBuffer &fillme) {
		if (!m_input.m_npackets) {
			SndPushInput((m_async_state != 0) ||
				     SndIsAsyncStarted());
		}
		m_input.Peek(fillme.m_data, fillme.m_size);
	}

	virtual void SndDequeueIBuf(sio_sampnum_t samps) {
		m_input.Dequeue(samps);
	}

	virtual void SndGetOBuf(SoundIoBuffer &fillme) {
		m_output.GetUnfilled(fillme.m_data, fillme.m_size);
	}

	virtual void SndQueueOBuf(sio_sampnum_t samps) {
		m_output.PutUnfilled(samps);
		if (!m_async_state) {
			SndPushOutput(SndIsAsyncStarted());
		}
	}

	virtual void SndGetQueueState(SoundIoQueueState &qs) {
		m_qs.in_queued = m_input.TotalFill();
		m_qs.out_queued = m_hw_outq + m_output.TotalFill();
		qs = m_qs;
	}

	void BufOpen(int packetsize, int bps) {
		BufCancelAbort();
		m_input.SetPacketSize(packetsize, bps);
		m_output.SetPacketSize(packetsize, bps);
		m_hw_outq = 0;
		m_abort = false;
	}

	void BufClose(void) {
		BufCancelAbort();
		BufStop();
		m_input.Clear();
		m_output.Clear();
	}

	bool BufProcess(sio_sampnum_t out_queued,
			bool in_overrun, bool out_underrun) {
		SoundIoQueueState ss;
		AsyncState as;
		as.m_stopped = false;
		m_hw_outq = out_queued;
		SndGetQueueState(ss);
		m_async_state = &as;
		if (cb_NotifyPacket.Registered())
			cb_NotifyPacket(this, &ss);
		if (as.m_stopped) { return false; }
		if (!m_abort) {
			SndPushOutput(true);
			if (as.m_stopped) { return false; }
		}
		assert(m_async_state == &as);
		m_async_state = 0;
		if (m_abort) {
			this->SndHandleAbort();
			return false;
		}
		return true;
	}
private:
	void BufAsyncAbort(TimerNotifier *notp) {
		assert(notp == m_abort_to);
		m_abort_to->Unregister();
		delete m_abort_to;
		m_abort_to = 0;
		if (m_abort)
			SndHandleAbort();
	}
public:
	void BufAbort(DispatchInterface *eip) {
		if (!m_abort) {
			m_abort = true;
			assert(!m_abort_to);
			m_abort_to = eip->NewTimer();
			m_abort_to->Register(this,
				     &SoundIoBufferBase::BufAsyncAbort);
			m_abort_to->Set(1);
		}
	}

	void BufStop(void) {
		if (m_async_state) {
			m_async_state->m_stopped = true;
			m_async_state = 0;
		}
	}
};


} /* namespace libhfp */
#endif  /* !defined(__LIBHFP_SOUNDIO_BUF_H__) */
