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
#include <errno.h>

#include <libhfp/soundio.h>

#include "oplatency.h"

/*
 * This file contains the implementation for SoundIoPump, the
 * asynchronous streaming audio pump class.
 */

namespace libhfp {

#define SOUND_IO_MAXSAMPS (~(sio_sampnum_t)0)

void SoundIoPump::
FillSilence(SoundIoFormat &fmt, uint8_t *dest)
{
	switch (fmt.sampletype) {
	case SIO_PCM_U8:
		memset(dest, 0x7f, fmt.nchannels);
		break;
	case SIO_PCM_S16_LE:
		memset(dest, 0, 2 * fmt.nchannels);
		break;
	default:
		abort();
	}
}

/*
 * The SoundIo interface does not set a minimum buffer size returned
 * from the SndGetIBuf() and SndGetOBuf() methods.  We may need to
 * call them in a loop in order to get the number of samples required.
 * There are five functions that use the buffer interfaces directly.
 * Four of them are largely redundant:
 *
 * CopyIn(), CopyOut(), CopyCross(), OutputSilence().
 *
 * CopyIn() returns the number of samples that were silence-padded,
 * either due to a failure of the source to produce samples, or due to
 * the in_xfer count reaching zero.  The others return the number of
 * samples that were not copied to the output buffer, either due to the
 * sink failing to produce an output buffer, or due to out_xfer
 * reaching zero.
 *
 * The fifth is ProcessOneWay(), which also calls into filter objects.
 */

#define SaveInSilence(SWSP, BUF, BPS) do {			\
		memcpy((SWSP)->in_silence,			\
		       (((uint8_t *) (BUF)) - (BPS)),		\
		       (BPS));					\
	} while (0)
		       
/*
 * CopyIn: Transfer samples from a SoundIo to a raw buffer
 */
sio_sampnum_t SoundIoPump::
CopyIn(uint8_t *dest, SoundIoWorkingState *swsp, sio_sampnum_t nsamps)
{
	sio_sampnum_t rem;
	uint8_t *end;
	uint8_t bps = swsp->bpr;

	assert(nsamps);
	if (swsp->in_buf.m_size) {
		assert(swsp->in_buf.m_size <= swsp->in_xfer);
		rem = swsp->in_buf.m_size;
		if (nsamps < rem) {
			memcpy(dest, swsp->in_buf.m_data, nsamps * bps);
			swsp->in_buf.m_data += (nsamps * bps);
			swsp->in_buf.m_size -= nsamps;
			swsp->in_xfer -= nsamps;
			SaveInSilence(swsp, swsp->in_buf.m_data, bps);
			swsp->siop->SndDequeueIBuf(nsamps);
			return 0;
		}

		memcpy(dest, swsp->in_buf.m_data, rem * bps);
		dest += (rem * bps);
		SaveInSilence(swsp, dest, bps);
		swsp->in_buf.m_size = 0;
		nsamps -= rem;
		swsp->in_xfer -= rem;
		swsp->siop->SndDequeueIBuf(rem);
	}

	while (nsamps) {
		if (!swsp->in_xfer)
			goto do_silencepad;
		swsp->in_buf.m_size = (nsamps > swsp->in_xfer)
			? swsp->in_xfer : nsamps;
		swsp->siop->SndGetIBuf(swsp->in_buf);
		rem = swsp->in_buf.m_size;
		if (!rem) {
			/*
			 * The input may provide less than expected,
			 * and if this happens, we silence-pad.
			 */
			goto do_silencepad;
		}

		if (nsamps <= rem) {
			memcpy(dest, swsp->in_buf.m_data, nsamps * bps);
			swsp->in_buf.m_size -= nsamps;
			swsp->in_buf.m_data += (nsamps * bps);
			SaveInSilence(swsp, swsp->in_buf.m_data, bps);
			swsp->in_xfer -= nsamps;
			swsp->siop->SndDequeueIBuf(nsamps);
			break;
		}

		memcpy(dest, swsp->in_buf.m_data, rem * bps);
		dest += (rem * bps);
		SaveInSilence(swsp, dest, bps);
		nsamps -= rem;
		swsp->in_xfer -= rem;
		swsp->siop->SndDequeueIBuf(rem);
		swsp->in_buf.m_size = 0;
	}

	return 0;

do_silencepad:
	end = &dest[nsamps * bps];
	while (dest < end) {
		memcpy(dest, swsp->in_silence, bps);
		dest += bps;
	}
	swsp->in_silencepad += nsamps;
	return nsamps;
}

#define SaveOutSilence(SWSP, BUF, BPS) do {			\
		memcpy((SWSP)->out_silence,			\
		       (((uint8_t *) (BUF)) - (BPS)),		\
		       (BPS));					\
	} while (0)

/*
 * CopyOut: Transfer samples from a raw buffer to a SoundIo
 */
sio_sampnum_t SoundIoPump::
CopyOut(SoundIoWorkingState *dwsp, const uint8_t *src, sio_sampnum_t nsamps)
{
	sio_sampnum_t rem;
	uint8_t bps = dwsp->bpr;

	assert(nsamps);

	if (dwsp->out_buf.m_size) {
		assert(dwsp->out_buf.m_size <= dwsp->out_xfer);
		rem = dwsp->out_buf.m_size;
		if (nsamps < rem) {
			memcpy(dwsp->out_buf.m_data, src, nsamps * bps);
			dwsp->out_buf.m_data += (nsamps * bps);
			SaveOutSilence(dwsp, dwsp->out_buf.m_data, bps);
			dwsp->out_buf.m_size -= nsamps;
			dwsp->out_buf_used += nsamps;
			dwsp->out_xfer -= nsamps;
			return 0;
		}

		memcpy(dwsp->out_buf.m_data, src, rem * bps);
		src += (rem * bps);
		SaveOutSilence(dwsp, src, bps);
		nsamps -= rem;
		dwsp->out_xfer -= rem;
		dwsp->siop->SndQueueOBuf(dwsp->out_buf_used + rem);
		dwsp->out_buf_used = 0;
		dwsp->out_buf.m_size = 0;
	}

	while (nsamps) {
		if (!dwsp->out_xfer)
			goto out_drop;
		dwsp->out_buf.m_size = (nsamps > dwsp->out_xfer)
			? dwsp->out_xfer : nsamps;
		dwsp->siop->SndGetOBuf(dwsp->out_buf);
		rem = dwsp->out_buf.m_size;
		assert(rem <= dwsp->out_xfer);
		if (!rem)
			goto out_drop;

		if (nsamps < rem) {
			memcpy(dwsp->out_buf.m_data, src, nsamps * bps);
			dwsp->out_buf.m_data += (nsamps * bps);
			SaveOutSilence(dwsp, dwsp->out_buf.m_data, bps);
			dwsp->out_buf.m_size -= nsamps;
			dwsp->out_buf_used = nsamps;
			dwsp->out_xfer -= nsamps;
			break;
		}

		memcpy(dwsp->out_buf.m_data, src, rem * bps);
		src += (rem * bps);
		SaveOutSilence(dwsp, src, bps);
		nsamps -= rem;
		dwsp->out_xfer -= rem;
		dwsp->siop->SndQueueOBuf(rem);
		dwsp->out_buf.m_size = 0;
	}

	return 0;

out_drop:
	dwsp->out_drop += nsamps;
	return nsamps;
}


sio_sampnum_t SoundIoPump::
CopyCross(SoundIoWorkingState *dwsp, SoundIoWorkingState *swsp,
	  sio_sampnum_t nsamps)
{
	sio_sampnum_t rem, in_pad = 0;
	uint8_t bps = dwsp->bpr;

	/* We maintain an in_pad count but do nothing with it */

	assert(nsamps);

	if (dwsp->out_buf.m_size) {
		assert(dwsp->out_buf.m_size <= dwsp->out_xfer);
		rem = dwsp->out_buf.m_size;
		if (nsamps < rem) {
			in_pad += CopyIn(dwsp->out_buf.m_data, swsp, nsamps);
			dwsp->out_buf.m_data += (nsamps * bps);
			SaveOutSilence(dwsp, dwsp->out_buf.m_data, bps);
			dwsp->out_buf.m_size -= nsamps;
			dwsp->out_buf_used += nsamps;
			dwsp->out_xfer -= nsamps;
			return 0;
		}

		in_pad += CopyIn(dwsp->out_buf.m_data, swsp, rem);
		SaveOutSilence(dwsp, &dwsp->out_buf.m_data[rem * bps], bps);
		nsamps -= rem;
		dwsp->out_xfer -= rem;
		dwsp->siop->SndQueueOBuf(dwsp->out_buf_used + rem);
		dwsp->out_buf_used = 0;
		dwsp->out_buf.m_size = 0;
	}

	while (nsamps) {
		if (!dwsp->out_xfer)
			goto out_drop;
		dwsp->out_buf.m_size = (nsamps > dwsp->out_xfer)
			? dwsp->out_xfer : nsamps;
		dwsp->siop->SndGetOBuf(dwsp->out_buf);
		rem = dwsp->out_buf.m_size;
		assert(rem <= dwsp->out_xfer);
		if (!rem)
			goto out_drop;

		if (nsamps < rem) {
			in_pad += CopyIn(dwsp->out_buf.m_data, swsp, nsamps);
			dwsp->out_buf.m_data += (nsamps * bps);
			SaveOutSilence(dwsp, dwsp->out_buf.m_data, bps);
			dwsp->out_buf.m_size -= nsamps;
			dwsp->out_buf_used = nsamps;
			dwsp->out_xfer -= nsamps;
			break;
		}

		in_pad += CopyIn(dwsp->out_buf.m_data, swsp, rem);
		SaveOutSilence(dwsp, &dwsp->out_buf.m_data[rem * bps], bps);
		nsamps -= rem;
		dwsp->out_xfer -= rem;
		dwsp->siop->SndQueueOBuf(rem);
		dwsp->out_buf.m_size = 0;
	}

	return 0;

out_drop:
	dwsp->out_drop += nsamps;

	/* We still need to try to remove nsamps from the input. */
	rem = nsamps;
	if (rem > swsp->in_xfer) {
		in_pad += rem - swsp->in_xfer;
		swsp->in_silencepad += (rem - swsp->in_xfer);
		rem = swsp->in_xfer;
	}
	if (rem) {
		swsp->in_buf.m_size = 0;
		swsp->siop->SndDequeueIBuf(rem);
		swsp->in_xfer -= rem;
	}

	return nsamps;
}

/*
 * OutputSilence: Copy silence samples to a SoundIo
 * This function does not update out_xfer or out_drop.
 */
sio_sampnum_t SoundIoPump::
OutputSilence(SoundIoWorkingState *dwsp, sio_sampnum_t nsamps)
{
	sio_sampnum_t rem;
	uint8_t bps = dwsp->bpr;
	uint8_t *buf, *end;

	if (dwsp->out_buf.m_size) {
		rem = dwsp->out_buf.m_size;
		if (nsamps < rem)
			rem = nsamps;

		buf = dwsp->out_buf.m_data;
		end = &buf[rem * bps];
		while (buf < end) {
			memcpy(buf, dwsp->out_silence, bps);
			buf += bps;
		}
		nsamps -= rem;
		dwsp->siop->SndQueueOBuf(dwsp->out_buf_used + rem);
		dwsp->out_buf.m_size -= rem;
		dwsp->out_buf.m_data = end;
		if (dwsp->out_buf.m_size) {
			assert(!nsamps);
			dwsp->out_buf_used += rem;
		} else {
			dwsp->out_buf_used = 0;
		}
	}

	while (nsamps) {
		dwsp->out_buf.m_size = nsamps;
		dwsp->siop->SndGetOBuf(dwsp->out_buf);
		rem = dwsp->out_buf.m_size;
		if (!rem)
			return nsamps;
		if (nsamps < rem)
			rem = nsamps;

		buf = dwsp->out_buf.m_data;
		end = &buf[rem * bps];
		while (buf < end) {
			memcpy(buf, dwsp->out_silence, bps);
			buf += bps;
		}
		nsamps -= rem;
		dwsp->siop->SndQueueOBuf(rem);
		dwsp->out_buf.m_size = 0;
	}

	return 0;
}

void SoundIoPump::
ProcessOneWay(SoundIoWorkingState *swsp, SoundIoWorkingState *dwsp,
	      bool up, SoundIoBuffer &buf1, SoundIoBuffer &buf2)
{
	SoundIoBuffer bufs, bufd, *bufp;
	SoundIoFilter *fltp;
	uint8_t *dibuf = NULL;
	uint8_t bps = dwsp->bpr;

	/* Acquire a buffer from the source */
	bufs.m_size = 0;
	if (swsp->in_xfer >= buf1.m_size) {
		bufs.m_size = buf1.m_size;
		swsp->siop->SndGetIBuf(bufs);
		assert(bufs.m_size <= buf1.m_size);
		if (bufs.m_size == buf1.m_size) {
			dibuf = bufs.m_data;
			SaveInSilence(swsp, &dibuf[buf1.m_size * bps], bps);
		}
	}
	if (bufs.m_size < buf1.m_size) {
		bufs = buf1;
		CopyIn(bufs.m_data, swsp, bufs.m_size);
	}

	bufd = buf2;

	for (fltp = (up ? m_bottom_flt : m_top_flt);
	     fltp != (up ? m_top_flt : m_bottom_flt);
	     fltp = (up ? fltp->m_up : fltp->m_down)) {
		bufp = const_cast<SoundIoBuffer*>
			(fltp->FltProcess(up, bufs, bufd));

		if (dibuf && (bufp->m_data != dibuf)) {
			/* Mark it consumed when filters stop returning it */
			swsp->siop->SndDequeueIBuf(buf1.m_size);
			assert(swsp->in_xfer >= buf1.m_size);
			swsp->in_xfer -= buf1.m_size;
			dibuf = NULL;
		}

		bufs = *bufp;
		bufd = (bufs.m_data == buf1.m_data) ? buf2 : buf1;
	}

	bufd.m_size = 0;
	if (dwsp->out_xfer >= buf1.m_size) {
		if (dwsp->out_buf.m_size &&
		    (dwsp->out_buf.m_size >= bufd.m_size)) {
			bufd = dwsp->out_buf;
			bufd.m_size = buf1.m_size;
		} else {
			if (dwsp->out_buf.m_size)
				dwsp->siop->SndQueueOBuf(dwsp->out_buf_used);
			dwsp->out_buf.m_size = buf1.m_size;
			dwsp->siop->SndGetOBuf(dwsp->out_buf);
			assert(dwsp->out_buf.m_size <= dwsp->out_xfer);
			bufd = dwsp->out_buf;
			dwsp->out_buf_used = 0;
		}
	}
	if (bufd.m_size == buf1.m_size) {
		bufp = const_cast<SoundIoBuffer*>
			(fltp->FltProcess(up, bufs, bufd));
		if (bufp->m_data == bufd.m_data) {
			assert(dwsp->out_xfer >= bufp->m_size);
			dwsp->out_xfer -= bufp->m_size;
			dwsp->out_buf_used += bufp->m_size;
			dwsp->out_buf.m_data +=
				(bufp->m_size * dwsp->bpr);
			dwsp->out_buf.m_size -= bufp->m_size;
			SaveOutSilence(dwsp, dwsp->out_buf.m_data, bps);
			if (!dwsp->out_buf.m_size) {
				dwsp->siop->SndQueueOBuf(dwsp->out_buf_used);
				dwsp->out_buf_used = 0;
			}
		} else {
			(void) CopyOut(dwsp, bufp->m_data, bufp->m_size);
		}
	} else {
		bufd = (bufs.m_data == buf1.m_data) ? buf2 : buf1;
		bufp = const_cast<SoundIoBuffer*>
			(fltp->FltProcess(up, bufs, bufd));
		(void) CopyOut(dwsp, bufp->m_data, bufp->m_size);
	}

	if (dibuf) {
		swsp->siop->SndDequeueIBuf(buf1.m_size);
		assert(swsp->in_xfer >= buf1.m_size);
		swsp->in_xfer -= buf1.m_size;
	}
}

void SoundIoPump::
ProcessorLoop(SoundIoWorkingState &bws, SoundIoWorkingState &tws,
	      unsigned int npackets)
{
	SoundIoBuffer buf1, buf2;

	assert(m_top_flt && m_bottom_flt);

	buf1.m_size = m_config.filter_packet_samps;
	buf1.m_data = (uint8_t *) malloc(m_config.filter_packet_samps *
					 bws.bpr * 2);
	buf2.m_size = m_config.filter_packet_samps;
	buf2.m_data = buf1.m_data + (m_config.filter_packet_samps * bws.bpr);

	while (npackets--) {
		/*
		 * Filter plugins expect an I/O progression loop:
		 * top input -> bottom output, THEN
		 * bottom input -> top output
		 *
		 * We also push data through one packet at a time.
		 */
		if (m_config.pump_down)
			ProcessOneWay(&tws, &bws, false, buf1, buf2);
		if (m_config.pump_up)
			ProcessOneWay(&bws, &tws, true, buf1, buf2);
	}

	free(buf1.m_data);
}

struct xfer_bound {
	sio_sampnum_t lower;
	sio_sampnum_t upper;
	uint8_t prio;
	uint8_t under_cost;
	uint8_t over_cost;
};

sio_sampnum_t
BestXfer(xfer_bound *bounds, int nbounds, sio_sampnum_t interval)
{
	int i, prio, bestprio, cost, bestcost;
	sio_sampnum_t minu, maxl, tryme, best;

	minu = maxl = 0;

	/* Find min(bounds[x].upper) */
	for (i = 0; i < nbounds; i++) {
		assert(bounds[i].lower <= bounds[i].upper);
		if (!i || (bounds[i].upper < minu))
			minu = bounds[i].upper;
		if (!i || (bounds[i].lower > maxl))
			maxl = bounds[i].lower;
	}

	/* Round to an interval boundary */
	minu = (minu / interval) * interval;
	maxl = ((maxl + interval - 1) / interval) * interval;

	if (minu >= maxl) {
		/* Ideal solution */
		return minu;
	}

	/*
	 * We have four piecewise linear equations and want to
	 * minimize the sum.
	 */
	best = 0;
	bestcost = INT_MAX;
	bestprio = INT_MAX;
	tryme = minu;
	while (tryme <= maxl) {
		cost = 0;
		prio = INT_MAX;
		for (i = 0; i < nbounds; i++) {
			if (tryme < bounds[i].lower) {
				if (prio > bounds[i].prio)
					prio = bounds[i].prio;
				cost += ((bounds[i].lower - tryme) *
					 bounds[i].under_cost);
			} else if (tryme > bounds[i].upper) {
				if (prio > bounds[i].prio)
					prio = bounds[i].prio;
				cost += ((tryme - bounds[i].upper) *
					 bounds[i].over_cost);
			}
		}
		assert(cost);
		assert(prio != INT_MAX);

		/*
		 * 'prio' is set to the priority value of the highest
		 * priority constraint (lowest numerical value) that is
		 * being violated.
		 *
		 * Priorities being equal, compare cost <= bestcost to
		 * maximize the transfer size
		 */
		if ((tryme == minu) ||
		    (prio > bestprio) ||
		    ((prio == bestprio) && (cost <= bestcost))) {
			best = tryme;
			bestprio = prio;
			bestcost = cost;
		}

		tryme += interval;
	}

	return best;
}

void SoundIoPump::
DumpQueueState(bool start, bool top) const
{
	assert(((int) m_bottom_qs.out_queued) >= 0);
	GetDi()->LogDebug("%s%sBot: In %d Out %d",
			  start ? "->" : "<-",
			  top ? "[-]" : "[*]",
			  m_bottom_qs.in_queued,
			  m_bottom_qs.out_queued);
	GetDi()->LogDebug("%s%sTop: In %d Out %d",
			  start ? "->" : "<-",
			  top ? "[*]" : "[-]",
			  m_top_qs.in_queued, m_top_qs.out_queued);
}

void SoundIoPump::
AsyncProcess(SoundIo *subp, SoundIoQueueState &state)
{
	/* Compile-time debugging flags for this method */
	const bool fill_debug = false;
	const bool loss_debug = false;

	/* Policy flag for querying the queue state of the other EP */
	const bool query_other_ep = false;

	OpLatencyMonitor lat(GetDi(), "async process overall");
	sio_sampnum_t ncopy, nadj, todo;
	xfer_bound bounds[4];
	SoundIoWorkingState bws, tws;
	bool did_loss = false, did_state_dump = false;

	/* This function is not reenterant, catch attempts to do so */
	assert(!m_async_entered);
	m_async_entered = true;

	if (!IsStarted()) {
		GetDi()->LogWarn("Received cb_NotifyPacket from %s??",
				 (subp == m_bottom) ? "Bottom" :
				 ((subp == m_top) ? "Top" : "Unknown"));
		goto done;
	}

	if (subp == m_bottom) {
		assert(m_config.bottom_async);

		ncopy = (state.in_queued - m_bottom_qs.in_queued);
		nadj = (m_bottom_qs.out_queued - state.out_queued);
		m_bottom_in_count += ncopy;
		m_bottom_out_count += nadj;

		if (m_stat) {
			m_stat->bottom.in.process += ncopy;
			m_stat->bottom.out.process += nadj;
		}

		m_bottom_strikes = 0;
		m_bottom_qs = state;

		if (query_other_ep || !m_config.top_async) {
			ncopy = m_top_qs.in_queued;
			nadj = m_top_qs.out_queued;

			m_top->SndGetQueueState(m_top_qs);
		}

		if (m_config.top_loop) {
			m_top_qs.in_queued += m_bottom_qs.in_queued;
		}

		if (query_other_ep || !m_config.top_async) {
			ncopy = (m_top_qs.in_queued - ncopy);
			nadj -= m_top_qs.out_queued;
			m_top_in_count += ncopy;
			m_top_out_count += nadj;

			if (m_stat) {
				m_stat->top.in.process += ncopy;
				m_stat->top.out.process += nadj;
			}
		}
	}
	else if (subp == m_top) {
		assert(m_config.top_async);

		ncopy = (state.in_queued - m_top_qs.in_queued);
		nadj = (m_top_qs.out_queued - state.out_queued);
		m_top_in_count += ncopy;
		m_top_out_count += nadj;

		if (m_stat) {
			m_stat->top.in.process += ncopy;
			m_stat->top.out.process += nadj;
		}

		m_top_strikes = 0;
		m_top_qs = state;

		if (query_other_ep || !m_config.bottom_async) {
			ncopy = m_bottom_qs.in_queued;
			nadj = m_bottom_qs.out_queued;

			m_bottom->SndGetQueueState(m_bottom_qs);
		}

		if (m_config.bottom_loop) {
			m_bottom_qs.in_queued += m_top_qs.in_queued;
		}

		if (query_other_ep || !m_config.bottom_async) {
			ncopy = (m_bottom_qs.in_queued - ncopy);
			nadj -= m_bottom_qs.out_queued;
			m_bottom_in_count += ncopy;
			m_bottom_out_count += nadj;

			if (m_stat) {
				m_stat->bottom.in.process += ncopy;
				m_stat->bottom.out.process += nadj;
			}
		}
	} else
		abort();

	if (fill_debug) {
		DumpQueueState(true, (subp == m_top));
		did_state_dump = true;
	}

	ncopy = 0;
	if (m_config.pump_up) {
		bounds[ncopy].lower = (m_config.bottom_async &&
				       (m_bottom_qs.in_queued >
					m_config.bottom_in_max))
			? (m_bottom_qs.in_queued - m_config.bottom_in_max) : 0;
		bounds[ncopy].upper = (!m_config.bottom_async &&
				       m_bottom_qs.in_queued &&
				       (m_bottom_qs.in_queued <
					m_config.filter_packet_samps))
			? m_config.filter_packet_samps : m_bottom_qs.in_queued;

		/* Make it more expensive to drop than to pad with silence. */
		bounds[ncopy].prio = m_bottom_loss_tolerate ? 2 : 1;
		bounds[ncopy].under_cost = 1;
		bounds[ncopy++].over_cost = 2;

		bounds[ncopy].lower = (m_config.top_async &&
				       (m_config.bottom_async ||
					m_bottom_qs.in_queued) &&
				       (m_top_qs.out_queued <
					m_config.top_out_min))
			? (m_config.top_out_min - m_top_qs.out_queued) : 0;
		bounds[ncopy].upper = (m_top_qs.out_queued <
				       m_config.top_out_max)
			? (m_config.top_out_max - m_top_qs.out_queued) : 0;
		bounds[ncopy].prio = m_top_loss_tolerate ? 2 : 1;
		bounds[ncopy].under_cost = 2;
		bounds[ncopy++].over_cost = 1;
	}
	if (m_config.pump_down) {
		bounds[ncopy].lower = (m_config.top_async &&
				       (m_top_qs.in_queued >
					m_config.top_in_max))
			? (m_top_qs.in_queued - m_config.top_in_max) : 0;
		bounds[ncopy].upper = (!m_config.top_async &&
				       m_top_qs.in_queued &&
				       (m_top_qs.in_queued <
					m_config.filter_packet_samps))
			? m_config.filter_packet_samps : m_top_qs.in_queued;
		bounds[ncopy].prio = m_top_loss_tolerate ? 2 : 1;
		bounds[ncopy].under_cost = 1;
		bounds[ncopy++].over_cost = 2;
		bounds[ncopy].lower = (m_config.bottom_async &&
				       (m_config.top_async ||
					m_top_qs.in_queued) &&
				       (m_bottom_qs.out_queued <
					m_config.bottom_out_min))
			? (m_config.bottom_out_min -
			   m_bottom_qs.out_queued) : 0;
		bounds[ncopy].upper = (m_bottom_qs.out_queued <
				       m_config.bottom_out_max)
			? (m_config.bottom_out_max -
			   m_bottom_qs.out_queued) : 0;
		bounds[ncopy].prio = m_bottom_loss_tolerate ? 2 : 1;
		bounds[ncopy].under_cost = 2;
		bounds[ncopy++].over_cost = 1;
	}

	assert(ncopy);
	ncopy = BestXfer(bounds, ncopy, m_config.filter_packet_samps);
	assert(!(ncopy % m_config.filter_packet_samps));

	if (m_config.top_loop) {
		/* Hack, hack */
		m_top_qs.in_queued -= m_bottom_qs.in_queued;
		m_top_qs.in_queued += ncopy;
	}
	else if (m_config.bottom_loop) {
		/* Hack, hack */
		m_bottom_qs.in_queued -= m_top_qs.in_queued;
		m_bottom_qs.in_queued += ncopy;
	}

	/* Initialize the working states */
	memset(&bws, 0, sizeof(bws));
	memset(&tws, 0, sizeof(tws));
	bws.siop = m_bottom;
	bws.bpr = m_config.fmt.bytes_per_record;
	memcpy(bws.in_silence, m_bi_last, sizeof(bws.in_silence));
	memcpy(bws.out_silence, m_bo_last, sizeof(bws.out_silence));
	tws.siop = m_top;
	tws.bpr = m_config.fmt.bytes_per_record;
	memcpy(tws.in_silence, m_ti_last, sizeof(tws.in_silence));
	memcpy(tws.out_silence, m_to_last, sizeof(tws.out_silence));

	if (m_stat) {
		m_stat->process_count += ncopy;

		if (m_bottom_qs.out_underflow) {
			did_loss = true;
			m_stat->bottom.out.xrun++;
		}
		if (m_bottom_qs.in_overflow) {
			did_loss = true;
			m_stat->bottom.in.xrun++;
		}
		if (m_top_qs.out_underflow) {
			did_loss = true;
			m_stat->top.out.xrun++;
		}
		if (m_top_qs.in_overflow) {
			did_loss = true;
			m_stat->top.in.xrun++;
		}
	}

	/* Some imbalances need to be corrected immediately */
	if (m_config.pump_up) {
		bws.in_xfer = ncopy;
		nadj = m_bottom_qs.in_queued - ncopy;
		if (ncopy > m_bottom_qs.in_queued) {
			bws.in_xfer = m_bottom_qs.in_queued;
		} else if (m_config.bottom_async &&
			   (nadj > m_config.bottom_in_max)) {
			/* Input overflow, toss the excess now */
			if (loss_debug && m_config.warn_loss) {
				if (!did_state_dump) {
					DumpQueueState(true, (subp == m_top));
					did_state_dump = true;
				}
				GetDi()->LogDebug("Bot: discarding %u input",
						  nadj -
						  m_config.bottom_in_max);
			}
			if (m_stat)
				m_stat->bottom.in.drop +=
					(nadj - m_config.bottom_in_max);
			m_bottom->SndDequeueIBuf(nadj -
						 m_config.bottom_in_max);
			m_bottom_qs.in_queued -= (nadj -
						  m_config.bottom_in_max);
			did_loss = true;
		}
		tws.out_xfer = ncopy;
		nadj = m_top_qs.out_queued + ncopy;
		if (nadj > m_config.top_out_max) {
			if (loss_debug && m_config.warn_loss) {
				if (!did_state_dump) {
					DumpQueueState(true, (subp == m_top));
					did_state_dump = true;
				}
				GetDi()->LogDebug("Top: discarding %u "
						  "output",
						  nadj - m_config.top_out_max);
			}
			if (m_stat)
				m_stat->top.out.drop +=
					(nadj - m_config.top_out_max);
			nadj -= m_config.top_out_max;
			tws.out_xfer = (nadj > tws.out_xfer)
				? 0 : (tws.out_xfer - nadj);
			did_loss = true;
		}
	}
	if (m_config.pump_down) {
		tws.in_xfer = ncopy;
		nadj = m_top_qs.in_queued - ncopy;
		if (ncopy > m_top_qs.in_queued) {
			tws.in_xfer = m_top_qs.in_queued;
		} else if (m_config.top_async &&
			   (nadj > m_config.top_in_max)) {
			/* Input overflow, toss the excess now */
			if (loss_debug && m_config.warn_loss) {
				if (!did_state_dump) {
					DumpQueueState(true, (subp == m_top));
					did_state_dump = true;
				}
				GetDi()->LogDebug("Top: discarding %u input",
						  nadj - m_config.top_in_max);
			}
			if (m_stat)
				m_stat->top.in.drop +=
					(nadj - m_config.top_in_max);
			m_top->SndDequeueIBuf(nadj - m_config.top_in_max);
			m_top_qs.in_queued -= (nadj - m_config.top_in_max);
			did_loss = true;
		}
		bws.out_xfer = ncopy;
		nadj = m_bottom_qs.out_queued + ncopy;
		if (nadj > m_config.bottom_out_max) {
			if (loss_debug && m_config.warn_loss) {
				if (!did_state_dump) {
					DumpQueueState(true, (subp == m_top));
					did_state_dump = true;
				}
				GetDi()->LogDebug("Bot: discarding %u "
						  "output",
						  nadj -
						  m_config.bottom_out_max);
			}
			if (m_stat)
				m_stat->bottom.out.drop +=
					(nadj - m_config.bottom_out_max);
			nadj -= m_config.bottom_out_max;
			bws.out_xfer = (nadj > bws.out_xfer)
				? 0 : (bws.out_xfer - nadj);
			did_loss = true;
		}
	}

	bws.in_xfer_expect = bws.in_xfer;
	bws.out_xfer_expect = bws.out_xfer;
	tws.in_xfer_expect = tws.in_xfer;
	tws.out_xfer_expect = tws.out_xfer;

	if ((fill_debug || (loss_debug && did_state_dump)) && ncopy) {
		GetDi()->LogDebug("Copy %d", ncopy);
	}

	if (!ncopy)
		goto done_copyback;

	if (!m_top_flt) {
		if (!m_config.bottom_loop && !m_config.top_loop) {
			/* No filters, just send it all through */
			if (m_config.pump_down)
				(void) CopyCross(&bws, &tws, ncopy);
			if (m_config.pump_up)
				(void) CopyCross(&tws, &bws, ncopy);
		} else {
			/* Keep the packet size down for loops */
			todo = ncopy;
			nadj = m_config.filter_packet_samps;
			while (todo) {
				if (m_config.pump_down)
					(void) CopyCross(&bws, &tws, nadj);
				if (m_config.pump_up)
					(void) CopyCross(&tws, &bws, nadj);
				todo -= nadj;
			}
		}

	} else {
		/* Deal with filters */
		ProcessorLoop(bws, tws, ncopy / m_config.filter_packet_samps);
	}

done_copyback:
	m_bottom_qs.in_queued -= (bws.in_xfer_expect - bws.in_xfer);
	m_bottom_qs.out_queued += (bws.out_xfer_expect - bws.out_xfer);
	m_top_qs.in_queued -= (tws.in_xfer_expect - tws.in_xfer);
	m_top_qs.out_queued += (tws.out_xfer_expect - tws.out_xfer);

	memcpy(m_bi_last, bws.in_silence, sizeof(m_bi_last));
	memcpy(m_bo_last, bws.out_silence, sizeof(m_bo_last));
	memcpy(m_ti_last, tws.in_silence, sizeof(m_ti_last));
	memcpy(m_to_last, tws.out_silence, sizeof(m_to_last));

	/*
	 * If silence padding is required, do it
	 * Don't silence-pad if a remove-on-exhaust source is empty.
	 */
	if (m_config.pump_up &&
	    (!m_config.bottom_roe || m_bottom_qs.in_queued)) {
		nadj = m_top_qs.out_queued;
		if (nadj < m_config.top_out_min) {
			if (loss_debug && m_config.warn_loss) {
				if (!did_state_dump) {
					DumpQueueState(true, (subp == m_top));
					did_state_dump = true;
				}
				GetDi()->LogDebug("Top: silence padding "
						  "output %u",
						  m_config.top_out_min - nadj);
			}
			if (m_stat)
				m_stat->top.out.pad +=
					(m_config.top_out_min - nadj);
			OutputSilence(&tws, m_config.top_out_min - nadj);
			m_top_qs.out_queued += (m_config.top_out_min - nadj);
			did_loss = true;
		}
	}
	if (m_config.pump_down &&
	    (!m_config.top_roe || m_top_qs.in_queued)) {
		nadj = m_bottom_qs.out_queued;
		if (nadj < m_config.bottom_out_min) {
			if (loss_debug && m_config.warn_loss) {
				if (!did_state_dump) {
					DumpQueueState(true, (subp == m_top));
					did_state_dump = true;
				}
				GetDi()->LogDebug("Bot: silence padding "
						  "output %u",
						  m_config.bottom_out_min -
						  nadj);
			}
			if (m_stat)
				m_stat->bottom.out.pad +=
					(m_config.bottom_out_min - nadj);
			OutputSilence(&bws, m_config.bottom_out_min - nadj);
			m_bottom_qs.out_queued +=
				(m_config.bottom_out_min - nadj);
			did_loss = true;
		}
	}

	/* Flush pending output buffers */
	if (bws.out_buf.m_size) {
		m_bottom->SndQueueOBuf(bws.out_buf_used);
		bws.out_buf.m_size = 0;
	}
	if (tws.out_buf.m_size) {
		m_top->SndQueueOBuf(tws.out_buf_used);
		tws.out_buf.m_size = 0;
	}

	/*
	 * If we have a static endpoint that has become exhausted
	 * in all of its relevant directions, halt.
	 */
	if ((m_config.top_roe &&
	     (!m_config.pump_up || tws.out_drop) &&
	     (!m_config.pump_down || (!ncopy && m_bottom_qs.out_underflow))) ||
	    (m_config.bottom_roe &&
	     (!m_config.pump_up || (!ncopy && m_top_qs.out_underflow)) &&
	     (!m_config.pump_down || bws.out_drop))) {
		ErrorInfo error;
		error.Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
			  LIBHFP_ERROR_SOUNDIO_DATA_EXHAUSTED,
			  "Static Endpoint Exhausted");
		assert(m_async_entered);
		m_async_entered = false;
		__Stop(&error, m_config.top_async ? m_bottom : m_top);
		return;
	}

	/*
	 * Losses are uncommon, so only do this messy processing
	 * section when we have to.
	 */
	if (bws.in_silencepad || bws.in_xfer || bws.out_xfer ||
	    tws.in_silencepad || tws.in_xfer || tws.out_xfer) {
		did_loss = true;

		if (m_stat) {
			m_stat->bottom.in.pad += bws.in_silencepad;
			m_stat->bottom.in.fail += bws.in_xfer;
			m_stat->bottom.out.fail += bws.out_xfer;
			m_stat->top.in.pad += tws.in_silencepad;
			m_stat->top.in.fail += tws.in_xfer;
			m_stat->top.out.fail += tws.out_xfer;
		}

		if (loss_debug && m_config.warn_loss) {
			if (bws.in_xfer)
				GetDi()->LogDebug("Bot: failed to process %u "
						  "input",
						  bws.in_xfer);
			if (bws.out_xfer)
				GetDi()->LogDebug("Bot: failed to process %u "
						  "output",
						  bws.out_xfer);
			if (tws.in_xfer)
				GetDi()->LogDebug("Top: failed to process %u "
						  "input",
						  tws.in_xfer);
			if (tws.out_xfer)
				GetDi()->LogDebug("Top: failed to process %u "
						  "output",
						  tws.out_xfer);
		}
	}

	if (fill_debug || (loss_debug && did_state_dump)) {
		SoundIoQueueState qs;
		m_bottom->SndGetQueueState(qs);
		assert(qs.in_queued >= m_bottom_qs.in_queued);
		assert(qs.out_queued <= m_bottom_qs.out_queued);
		GetDi()->LogDebug("<-[%c]Bot: In %d Out %d",
				  (subp == m_bottom) ? '*' : '-',
				  qs.in_queued, qs.out_queued);
		if ((subp == m_bottom) &&
		    ((m_bottom_qs.out_queued - qs.out_queued) > 20))
			GetDi()->LogDebug("*** Bot out: expect %u real %u",
					  m_bottom_qs.out_queued,
					  qs.out_queued);
		m_top->SndGetQueueState(qs);
		assert(qs.in_queued >= m_top_qs.in_queued);
		assert(qs.out_queued <= m_top_qs.out_queued);
		GetDi()->LogDebug("<-[%c]Top: In %d Out %d",
				  (subp == m_top) ? '*' : '-',
			       qs.in_queued, qs.out_queued);
		if ((subp == m_top) &&
		    ((m_top_qs.out_queued - qs.out_queued) > 20))
			GetDi()->LogDebug("*** Top out: expect %u real %u",
					  m_top_qs.out_queued, qs.out_queued);
	}

	if (m_config.top_loop && m_config.pump_up && m_config.pump_down) {
		/*
		 * Make sure our loop isn't turning into a
		 * latency trap.
		 */
		SoundIoQueueState qs;
		m_top->SndGetQueueState(qs);
		assert(qs.in_queued <= m_config.filter_packet_samps);
	}

	if (m_stat && (ncopy || did_loss) &&
	    cb_NotifyStatistics.Registered()) {
		m_stat->bottom.out.level = m_bottom_qs.out_queued;
		m_stat->bottom.in.level = m_bottom_qs.in_queued;
		m_stat->top.out.level = m_top_qs.out_queued;
		m_stat->top.in.level = m_top_qs.in_queued;
		cb_NotifyStatistics(this, *m_stat, did_loss);
	}

done:
	assert(m_async_entered);
	m_async_entered = false;
}

void SoundIoPump::
AsyncStopped(SoundIo *subp, ErrorInfo &error)
{
	assert(!m_async_entered);
	m_async_entered = true;

	if (!IsStarted()) {
		GetDi()->LogWarn("Received cb_NotifyAsyncStop from %s??",
				 (subp == m_bottom) ? "Bottom" :
				 ((subp == m_top) ? "Top" : "Unknown"));
		m_async_entered = false;
		return;
	}

	if (subp == m_bottom) {
		assert(m_config.bottom_async);

		GetDi()->LogDebug("Bottom endpoint caused pump halt: %s",
				  error.Desc());
	}
	else if (subp == m_top) {
		assert(m_config.top_async);

		GetDi()->LogDebug("Top endpoint caused pump halt: %s",
				  error.Desc());
	} else
		abort();

	assert(m_async_entered);
	m_async_entered = false;

	__Stop(&error, subp);
}

bool SoundIoPump::
WatchdogThreshold(sio_sampnum_t &count, int8_t &strikes, const char *name,
		  ErrorInfo &error)
{
	int8_t res = 0;

	if (count < m_config.watchdog_min_progress) {
		GetDi()->LogDebug("SoundIoPump: %s underprocessed (%d < %d)",
				  name, count, m_config.watchdog_min_progress);
		res = -1;
	}
	else if (count > m_config.watchdog_max_progress) {
		GetDi()->LogDebug("SoundIoPump: %s overprocessed (%d > %d)",
				  name, count, m_config.watchdog_max_progress);
		res = 1;
	}
	count = 0;
	if (!res) {
		strikes = 0;
		return true;
	}

	strikes += res;
	if (strikes < -m_config.watchdog_strikes) {
		GetDi()->LogDebug("SoundIoPump: %d strikes, you're out",
				  strikes);
		error.Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
			  LIBHFP_ERROR_SOUNDIO_WATCHDOG_TIMEOUT,
			  "SoundIoPump: %s underprocessing", name);
		return false;
	}
	if (strikes > m_config.watchdog_strikes) {
		GetDi()->LogDebug("SoundIoPump: %d strikes, you're out",
				  strikes);
		error.Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
			  LIBHFP_ERROR_SOUNDIO_WATCHDOG_TIMEOUT,
			  "SoundIoPump: %s overprocessing", name);
		return false;
	}
	return true;
}

void SoundIoPump::
Watchdog(TimerNotifier *notp)
{
	SoundIo *ep = 0;
	ErrorInfo error;

	assert(notp == m_watchdog);
	assert(m_running);

	/*
	 * If we are stuck with a device that has a flaky clock or
	 * a broken driver, we want to fail gracefully.
	 *
	 * We allow a maximum number of "strikes" for any sort of violation.
	 */
	if (m_bottom_async_started) {
		ep = m_bottom;
		if (++m_bottom_strikes > m_config.watchdog_strikes) {
			error.Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				  LIBHFP_ERROR_SOUNDIO_WATCHDOG_TIMEOUT,
				  "SoundIoPump: Bottom endpoint timeout");
			goto failed;
		}

		if (m_config.pump_up &&
		    !WatchdogThreshold(m_bottom_in_count, m_bottom_in_strikes,
				       "bottom input", error))
			goto failed;
		if (m_config.pump_down &&
		    !WatchdogThreshold(m_bottom_out_count,
				       m_bottom_out_strikes,
				       "bottom output", error))
			goto failed;
	}

	if (m_top_async_started) {
		if (++m_top_strikes > m_config.watchdog_strikes) {
			error.Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				  LIBHFP_ERROR_SOUNDIO_WATCHDOG_TIMEOUT,
				  "SoundIoPump: Top endpoint timeout");
			goto failed;
		}

		if (m_config.pump_down &&
		    !WatchdogThreshold(m_top_in_count, m_top_in_strikes,
				       "top input", error))
			goto failed;
		if (m_config.pump_up &&
		    !WatchdogThreshold(m_top_out_count, m_top_out_strikes,
				       "top output", error))
			goto failed;
	}

	m_watchdog->Set(m_config.watchdog_to);
	return;

failed:
	GetDi()->LogWarn("%s", error.Desc());
	__Stop(&error, ep);
	return;
}

/*
 * This function fills out a SoundIoPumpConfig:
 * - If the pump_down and pump_up fields are both false, they will be set.
 * - The filter_packet_samps may be nonzero, in which case its value is
 *   enforced against others, otherwise a reasonable value is determined.
 * - All other fields are filled in by this function.
 */
bool SoundIoPump::
ConfigureEndpoints(SoundIo *bottom, SoundIo *top, SoundIoPumpConfig &cfg,
		   ErrorInfo *error)
{
	enum { watchdog_packets = 15 };

	SoundIoProps bottom_props, top_props;
	SoundIoFormat bottom_fmt, top_fmt;
	sio_sampnum_t config_out_min, config_window, max_packet, nsamps;
	unsigned int msecs;
	bool fixed_fps;

	if (!bottom || !top) {
		GetDi()->LogDebug(error,
				  LIBHFP_ERROR_SUBSYS_SOUNDIO,
				  LIBHFP_ERROR_SOUNDIO_BAD_PUMP_CONFIG,
				  "Config fail: Endpoints not set");
		return false;
	}

	bottom->SndGetProps(bottom_props);
	top->SndGetProps(top_props);

	/* Which directions are we going to be moving data? */
	if (!cfg.pump_up && !cfg.pump_down) {
		cfg.pump_down = (top_props.does_source &&
				 bottom_props.does_sink);
		cfg.pump_up = (top_props.does_sink &&
			       bottom_props.does_source);
		if (!cfg.pump_down && !cfg.pump_up) {
			GetDi()->LogWarn(error,
					 LIBHFP_ERROR_SUBSYS_SOUNDIO,
					 LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
					 "Config fail: "
					 "Can't pump up or down");
			return false;
		}
	}
	else if (cfg.pump_down && (!bottom_props.does_sink ||
				   !top_props.does_source)) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_SOUNDIO,
				 LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
				 "Config fail: One or both endpoints does "
				 "not support downward streaming");
		return false;
	}
	else if (cfg.pump_up && (!bottom_props.does_source ||
				 !top_props.does_sink)) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_SOUNDIO,
				 LIBHFP_ERROR_SOUNDIO_DUPLEX_MISMATCH,
				 "Config fail: One or both endpoints does "
				 "not support upward streaming");
		return false;
	}

	/* Do we have independent clocks? */
	cfg.bottom_async = bottom_props.has_clock;
	cfg.top_async = top_props.has_clock;

	/* How many are loops? */
	cfg.bottom_loop = bottom_props.does_loop;
	cfg.top_loop = top_props.does_loop;

	cfg.bottom_roe = bottom_props.remove_on_exhaust;
	cfg.top_roe = top_props.remove_on_exhaust;

	/*
	 * If there is a non-async, non-remove-on-exhaust EP, we don't
	 * warn about dropping/padding at all.
	 */
	cfg.warn_loss = ((cfg.bottom_async || cfg.bottom_roe) &&
			 (cfg.top_async || cfg.top_roe));

	if (!cfg.bottom_async && !cfg.top_async) {
		/* Offline processing mode isn't supported yet */
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_SOUNDIO,
				 LIBHFP_ERROR_SOUNDIO_BAD_PUMP_CONFIG,
				 "Config fail: Offline mode not supported");
		return false;
	}

	if (cfg.bottom_loop && cfg.top_loop) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_SOUNDIO,
				 LIBHFP_ERROR_SOUNDIO_BAD_PUMP_CONFIG,
				 "Config fail: Both bottom and top "
				 "are loops");
		return false;
	}

	/*
	 * We don't negotiate format.  We require our client to have
	 * already set that up.
	 */
	if (cfg.bottom_loop) {
		top->SndGetFormat(top_fmt);
		bottom->SndSetFormat(top_fmt);
	}
	bottom->SndGetFormat(bottom_fmt);
	if (cfg.top_loop) {
		top->SndSetFormat(bottom_fmt);
	}
	top->SndGetFormat(top_fmt);
	if ((top_fmt.sampletype != bottom_fmt.sampletype) ||
	    (top_fmt.samplerate != bottom_fmt.samplerate) ||
	    (top_fmt.nchannels != bottom_fmt.nchannels)) {
		GetDi()->LogWarn(error,
				 LIBHFP_ERROR_SUBSYS_SOUNDIO,
				 LIBHFP_ERROR_SOUNDIO_FORMAT_MISMATCH,
				 "Config fail: Top/bottom formats disagree");
		return false;
	}

	cfg.fmt = bottom_fmt;
	fixed_fps = (cfg.filter_packet_samps != 0);

	max_packet = cfg.filter_packet_samps;
	if (cfg.bottom_async && (bottom_fmt.packet_samps > max_packet))
		max_packet = bottom_fmt.packet_samps;
	if (cfg.top_async && (top_fmt.packet_samps > max_packet))
		max_packet = top_fmt.packet_samps;

	/* We don't allow the endpoints to constrain us beyond this: */
	if (cfg.bottom_async && bottom_props.outbuf_size) {
		if ((4 * max_packet) > bottom_props.outbuf_size) {
			GetDi()->LogWarn(error,
					 LIBHFP_ERROR_SUBSYS_SOUNDIO,
					 LIBHFP_ERROR_SOUNDIO_BAD_PUMP_CONFIG,
					 "Config fail: Bottom output "
					 "buffer (%d) "
					 "is less than four times the "
					 "maximum packet size (%d)",
					 bottom_props.outbuf_size,
					 max_packet);
			return false;
		}
	}
	if (cfg.top_async && top_props.outbuf_size) {
		if ((4 * max_packet) > top_props.outbuf_size) {
			GetDi()->LogWarn(error,
					 LIBHFP_ERROR_SUBSYS_SOUNDIO,
					 LIBHFP_ERROR_SOUNDIO_BAD_PUMP_CONFIG,
					 "Config fail: Top output buffer (%d) "
					 "is less than four times the "
					 "maximum packet size (%d)",
					 top_props.outbuf_size, max_packet);
			return false;
		}
	}

	/*
	 * Compute sample counts for configured values, if specified.
	 */

	config_out_min = 0;
	if (m_config_out_min_ms) {
		config_out_min = (m_config_out_min_ms *
				  cfg.fmt.samplerate) / 1000;
		if (!config_out_min)
			config_out_min = 1;
	}

	config_window = 0;
	if (m_config_out_window_ms) {
		config_window = (m_config_out_window_ms *
				 cfg.fmt.samplerate) / 1000;
		if (!config_window)
			config_window = 1;
	}

	if (config_out_min &&
	    (config_out_min < (2 * max_packet))) {
		GetDi()->LogDebug("Config warn: Configured output "
				  "minimum buffer (%d) is less than "
				  "twice the maximum packet size (%d)",
				  config_out_min, (2 * max_packet));
		config_out_min = (2 * max_packet);
	}

	if (config_window &&
	    (config_window < (2 * max_packet))) {
		GetDi()->LogDebug("Config warn: Configured output "
				  "window size (%d) is less than "
				  "twice the maximum packet size (%d)",
				  config_window, (2 * max_packet));
		config_window = (2 * max_packet);
	}

	/*
	 * Determine appropriate minimum and maximum output buffer
	 * fill levels for the top and bottom endpoints.
	 */

	cfg.bottom_out_min = 0;
	if (cfg.bottom_async) {
		if (config_out_min)
			cfg.bottom_out_min = config_out_min;
		else
			/* A hopefully safe default */
			cfg.bottom_out_min = max_packet * 2;

		if (cfg.bottom_out_min < bottom_fmt.packet_samps) {
			GetDi()->LogDebug("Config warn: Configured output "
					  "minimum buffer (%d) is less than "
					  "the bottom packet size (%d)",
					  config_out_min,
					  bottom_fmt.packet_samps);
			cfg.bottom_out_min = bottom_fmt.packet_samps;
		}
		if (bottom_props.outbuf_size &&
		    (cfg.bottom_out_min >
		     (bottom_props.outbuf_size - bottom_fmt.packet_samps))) {
			if (config_out_min) {
				GetDi()->LogDebug("Config warn: "
						  "Configured output minimum "
						  "buffer (%d) is within one "
						  "packet size of the bottom "
						  "buffer size (%d)",
						  config_out_min,
						  bottom_props.outbuf_size);
			}
			cfg.bottom_out_min = bottom_props.outbuf_size -
				bottom_fmt.packet_samps;
		}

		/* Two packets acceptable fill window */
		nsamps = config_window ? config_window : cfg.bottom_out_min;
		if (nsamps < (max_packet * 3))
			nsamps = (max_packet * 3);
		cfg.bottom_out_max = cfg.bottom_out_min + nsamps;

		if (bottom_props.outbuf_size &&
		    (cfg.bottom_out_max > bottom_props.outbuf_size)) {
			cfg.bottom_out_max = bottom_props.outbuf_size;
			GetDi()->LogDebug("Config warn: Configured output "
					  "window (%d) would exceed bottom "
					  "output buffer (%d)",
					  nsamps, bottom_props.outbuf_size);
			assert((cfg.bottom_out_max - cfg.bottom_out_min) >=
			       bottom_fmt.packet_samps);
		}

	} else {
		/* Non-asynchronous case */
		cfg.bottom_out_max = bottom_props.outbuf_size ?
			bottom_props.outbuf_size : SOUND_IO_MAXSAMPS;
	}

	cfg.top_out_min = 0;
	if (cfg.top_async) {
		if (config_out_min)
			cfg.top_out_min = config_out_min;
		else
			/* A hopefully safe default */
			cfg.top_out_min = max_packet * 2;

		if (cfg.top_out_min < top_fmt.packet_samps) {
			GetDi()->LogDebug("Config warn: Configured output "
					  "minimum buffer (%d) is less than "
					  "the top packet size (%d)",
					  config_out_min,
					  top_fmt.packet_samps);
			cfg.top_out_min = top_fmt.packet_samps;
		}
		if (top_props.outbuf_size &&
		    (cfg.top_out_min >
		     (top_props.outbuf_size - top_fmt.packet_samps))) {
			if (config_out_min) {
				GetDi()->LogDebug("Config warn: "
						  "Configured output minimum "
						  "buffer (%d) is within one "
						  "packet size of the top "
						  "buffer size (%d)",
						  config_out_min,
						  top_props.outbuf_size);
			}
			cfg.top_out_min = top_props.outbuf_size -
				top_fmt.packet_samps;
		}

		/* Two packets acceptable fill window */
		nsamps = config_window ? config_window : cfg.top_out_min;
		if (nsamps < (max_packet * 3))
			nsamps = (max_packet * 3);
		cfg.top_out_max = cfg.top_out_min + nsamps;

		if (top_props.outbuf_size &&
		    (cfg.top_out_max > top_props.outbuf_size)) {
			cfg.top_out_max = top_props.outbuf_size;
			GetDi()->LogDebug("Config warn: Configured output "
					  "window (%d) would exceed top "
					  "output buffer (%d)",
					  nsamps, top_props.outbuf_size);
			assert((cfg.top_out_max - cfg.top_out_min) >=
			       top_fmt.packet_samps);
		}

	} else {
		/* Non-asynchronous case */
		cfg.top_out_max = top_props.outbuf_size ?
			top_props.outbuf_size : SOUND_IO_MAXSAMPS;
	}

	/*
	 * Use the chosen output maximum as the input max fill level
	 */
	cfg.bottom_in_max = cfg.bottom_out_max - cfg.bottom_out_min;
	cfg.top_in_max = cfg.top_out_max - cfg.bottom_out_min;

	if (!fixed_fps) {
		/* Find a good filter packet size */
		cfg.filter_packet_samps = cfg.fmt.packet_samps;
		if (!cfg.bottom_async ||
		    (cfg.top_async && (top_fmt.packet_samps <
				       cfg.filter_packet_samps)))
			cfg.filter_packet_samps = top_fmt.packet_samps;
	}

	while (1) {
		/* Ensure that the packet size is < 1/2 min fill
		   of all outputs */
		if ((!cfg.bottom_async ||
		     (cfg.filter_packet_samps <= (cfg.bottom_out_min / 2))) &&
		    (!cfg.top_async ||
		     (cfg.filter_packet_samps <= (cfg.top_out_min / 2))) &&
		    (!cfg.bottom_async ||
		     (cfg.filter_packet_samps <=
		      ((cfg.bottom_out_max - cfg.bottom_out_min) / 2))) &&
		    (!cfg.top_async ||
		     (cfg.filter_packet_samps <= ((cfg.top_out_max -
						   cfg.top_out_min) / 2))))
			break;

		assert(!fixed_fps);
		cfg.filter_packet_samps /= 2;
	}

	/*
	 * Pick a timeout value for the watchdog timer that is
	 * watchdog_packets number of milliseconds for the largest
	 * of the bottom and top packet sizes.
	 *
	 * Don't pick a timeout smaller than 1/2 second.
	 *
	 * The watchdog will abort the pump if the timeout elapses
	 * twice with at least one of the clocked sources not
	 * delivering any events.
	 */

	cfg.watchdog_to = 500;
	cfg.watchdog_strikes = 2;

	if (cfg.bottom_async) {
		msecs = ((watchdog_packets * bottom_fmt.packet_samps * 1000) /
			 cfg.fmt.samplerate);
		if (msecs > cfg.watchdog_to)
			cfg.watchdog_to = msecs;
	}

	if (cfg.top_async) {
		msecs = ((watchdog_packets * top_fmt.packet_samps * 1000) /
			 cfg.fmt.samplerate);
		if (msecs > cfg.watchdog_to)
			cfg.watchdog_to = msecs;
	}

	/*
	 * If any duplex of any clocked endpoint is processing samples
	 * at less than 25% of the system clock, or above 200% the
	 * system clock, we count it as a watchdog failure.
	 */
	nsamps = (cfg.fmt.samplerate * cfg.watchdog_to) / 1000;
	cfg.watchdog_min_progress = nsamps / 4;
	cfg.watchdog_max_progress = nsamps * 2;

	/*
	 * The working configuration of the pump is delicate,
	 * and useful for debugging.
	 */
	GetDi()->LogDebug("Pump: packet size = %u", cfg.filter_packet_samps);
	GetDi()->LogDebug("Pump: bot packet size = %u",
			  bottom_fmt.packet_samps);
	GetDi()->LogDebug("Pump: bot input max fill = %u",
			  cfg.bottom_in_max);
	GetDi()->LogDebug("Pump: bot min fill = %u", cfg.bottom_out_min);
	GetDi()->LogDebug("Pump: bot max fill = %u", cfg.bottom_out_max);
	GetDi()->LogDebug("Pump: top packet size = %u",
			  top_fmt.packet_samps);
	GetDi()->LogDebug("Pump: top input max fill = %u", cfg.top_in_max);
	GetDi()->LogDebug("Pump: top min fill = %u", cfg.top_out_min);
	GetDi()->LogDebug("Pump: top max fill = %u", cfg.top_out_max);
	GetDi()->LogDebug("Pump: watchdog timeout = %u", cfg.watchdog_to);

	return true;
}


bool SoundIoPump::
SetBottom(SoundIo *newep, ErrorInfo *error)
{
	SoundIo *oldep;

	assert(!newep || (newep != m_top));

	if (newep == m_bottom)
		return true;

	if (newep) {
		assert(!newep->cb_NotifyPacket.Registered());
		newep->cb_NotifyPacket.Register(this,
						 &SoundIoPump::AsyncProcess);
		assert(!newep->cb_NotifyAsyncStop.Registered());
		newep->cb_NotifyAsyncStop.Register(this,
						   &SoundIoPump::AsyncStopped);
	}

	oldep = m_bottom;
	m_bottom = newep;

	if (IsStarted() && newep) {
		SoundIoPumpConfig newcfg;

		assert(oldep);

		memset(&newcfg, 0, sizeof(newcfg));
		newcfg.pump_up = m_config.pump_up;
		newcfg.pump_down = m_config.pump_down;

		/*
		 * We could choose enforce this only when a filter is
		 * attached, but that would encourage obscure bugs.
		 */
		newcfg.filter_packet_samps = m_config.filter_packet_samps;


		if (!ConfigureEndpoints(newep, m_top, newcfg, error))
			goto failed;

		if (newcfg.bottom_async) {
			OpLatencyMonitor lat(GetDi(), "new bottom EP start");
			if (!newep->SndAsyncStart(newcfg.pump_down,
						  newcfg.pump_up,
						  error)) {
				GetDi()->LogWarn("SoundIo: Could not start "
						 "new bottom EP");
				goto failed;
			}
		}

		if (m_bottom_async_started) {
			OpLatencyMonitor lat(GetDi(), "old bottom EP stop");
			oldep->SndAsyncStop();
		}

		m_bottom_async_started = newcfg.bottom_async;
		m_config = newcfg;
		m_bottom_strikes = 0;

		/*
		 * NOTE TO READER:
		 * We intentionally avoid resetting the m_xx_last
		 * sample buffers!
		 */
	}
	else if (IsStarted()) {
		__Stop();
	}

	if (oldep) {
		oldep->cb_NotifyPacket.Unregister();
		oldep->cb_NotifyAsyncStop.Unregister();
	}

	return true;

failed:
	newep->cb_NotifyPacket.Unregister();
	newep->cb_NotifyAsyncStop.Unregister();
	m_bottom = oldep;
	return false;
}

bool SoundIoPump::
SetTop(SoundIo *newep, ErrorInfo *error)
{
	SoundIo *oldep;

	assert(!newep || (newep != m_top));

	if (newep == m_top)
		return true;

	if (newep) {
		assert(!newep->cb_NotifyPacket.Registered());
		newep->cb_NotifyPacket.Register(this,
						 &SoundIoPump::AsyncProcess);
		assert(!newep->cb_NotifyAsyncStop.Registered());
		newep->cb_NotifyAsyncStop.Register(this,
						   &SoundIoPump::AsyncStopped);
	}

	oldep = m_top;
	m_top = newep;

	if (IsStarted() && newep) {
		SoundIoPumpConfig newcfg;
		assert(oldep);
		memset(&newcfg, 0, sizeof(newcfg));
		newcfg.pump_up = m_config.pump_up;
		newcfg.pump_down = m_config.pump_down;
		newcfg.filter_packet_samps = m_config.filter_packet_samps;
		if (!ConfigureEndpoints(newep, m_top, newcfg, error))
			goto failed;

		if (newcfg.top_async) {
			OpLatencyMonitor lat(GetDi(), "new top EP start");
			if (!newep->SndAsyncStart(newcfg.pump_up,
						  newcfg.pump_down,
						  error)) {
				GetDi()->LogWarn("SoundIo: Could not start "
						 "new top EP");
				goto failed;
			}
		}

		if (m_top_async_started) {
			OpLatencyMonitor lat(GetDi(), "old top EP stop");
			oldep->SndAsyncStop();
		}

		m_top_async_started = newcfg.top_async;
		m_config = newcfg;
		m_top_strikes = 0;
	}

	else if (IsStarted()) {
		__Stop();
	}

	if (oldep) {
		oldep->cb_NotifyPacket.Unregister();
		oldep->cb_NotifyAsyncStop.Unregister();
	}

	return true;

failed:
	newep->cb_NotifyPacket.Unregister();
	newep->cb_NotifyAsyncStop.Unregister();
	m_top = oldep;
	return false;
}

bool SoundIoPump::
Start(ErrorInfo *error)
{
	SoundIoFilter *fltp;
	SoundIoFormat fltfmt;
	SoundIoPumpConfig cfg;

	if (IsStarted()) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_ALREADY_OPEN,
				   "Pump already started");
		return false;
	}

	if (!m_bottom || !m_top) {
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_SOUNDIO,
				   LIBHFP_ERROR_SOUNDIO_BAD_PUMP_CONFIG,
				   (!m_bottom && !m_top) ?
				   "Neither endpoint set" :
				   (!m_bottom ? "Bottom endpoint not set"
				    : "Top endpoint not set"));
		return false;
	}

	/* Blank slate, let ConfigureEndpoints figure everything out */
	memset(&cfg, 0, sizeof(cfg));

	/*
	 * Run the configuration function
	 */
	if (!ConfigureEndpoints(m_bottom, m_top, cfg, error))
		return false;

	/*
	 * Reset the silence buffers
	 */
	FillSilence(cfg.fmt, m_bi_last);
	FillSilence(cfg.fmt, m_bo_last);
	FillSilence(cfg.fmt, m_ti_last);
	FillSilence(cfg.fmt, m_to_last);

	m_watchdog = GetDi()->NewTimer();
	if (!m_watchdog) {
		if (error)
			error->SetNoMem();
		GetDi()->LogWarn("Could not create watchdog");
		return false;
	}
	m_watchdog->Register(this, &SoundIoPump::Watchdog);

	m_config = cfg;

	fltfmt = cfg.fmt;
	fltfmt.packet_samps = cfg.filter_packet_samps;

	/*
	 * Configure the filters
	 */

	for (fltp = m_bottom_flt; fltp != NULL; fltp = fltp->m_up) {
		if (!fltp->FltPrepare(fltfmt, cfg.pump_up, cfg.pump_down,
				      error)) {
			GetDi()->LogDebug("Filter prepare failed, "
					  "not starting");
			fltp = fltp->m_down;
			while (fltp != NULL) {
				fltp->FltCleanup();
				fltp = fltp->m_down;
			}
			if (m_watchdog) {
				m_watchdog->Unregister();
				delete m_watchdog;
				m_watchdog = 0;
			}
			return false;
		}
	}

	/*
	 * Start the various devices.
	 * Hereafter we use __Stop() to clean up
	 */
	m_running = true;

	if (cfg.bottom_async) {
		OpLatencyMonitor lat(GetDi(), "bottom EP start");
		if (!m_bottom->SndAsyncStart(cfg.pump_down, cfg.pump_up,
					     error))
			goto failed;
		m_bottom_async_started = true;
	}
	m_bottom->SndGetQueueState(m_bottom_qs);

	if (cfg.top_async) {
		OpLatencyMonitor lat(GetDi(), "top EP start");
		if (!m_top->SndAsyncStart(cfg.pump_up, cfg.pump_down,
					  error))
			goto failed;
		m_top_async_started = true;
	}
	m_top->SndGetQueueState(m_top_qs);

	/*
	 * Start the watchdog timer
	 */
	m_bottom_strikes = 0;
	m_top_strikes = 0;
	m_bottom_in_count = 0;
	m_top_in_count = 0;
	m_bottom_out_count = 0;
	m_top_out_count = 0;
	m_bottom_in_strikes = 0;
	m_top_in_strikes = 0;
	m_bottom_out_strikes = 0;
	m_top_out_strikes = 0;
	if (cfg.watchdog_to)
		m_watchdog->Set(cfg.watchdog_to);

	return true;

failed:
	__Stop();
	return false;
}

void SoundIoPump::
__Stop(ErrorInfo *reason, SoundIo *offender)
{
	SoundIoFilter *fltp;

	if (IsStarted()) {
		assert(m_watchdog);
		if (m_watchdog) {
			m_watchdog->Cancel();
			delete m_watchdog;
			m_watchdog = 0;
		}
		if (m_bottom_async_started) {
			OpLatencyMonitor lat(GetDi(), "bottom EP stop");
			m_bottom->SndAsyncStop();
			m_bottom_async_started = false;
		}
		if (m_top_async_started) {
			OpLatencyMonitor lat(GetDi(), "top EP stop");
			m_top->SndAsyncStop();
			m_top_async_started = false;
		}

		for (fltp = m_top_flt; fltp != NULL; fltp = fltp->m_down) {
			fltp->FltCleanup();
		}

		/* Clear the remembered queue sizes */
		m_bottom_qs.in_queued = 0;
		m_bottom_qs.out_queued = 0;
		m_top_qs.in_queued = 0;
		m_top_qs.out_queued = 0;

		m_running = false;

		GetDi()->LogDebug("SoundIoPump Stopped");

		if (reason && cb_NotifyAsyncState.Registered())
			cb_NotifyAsyncState(this, offender, *reason);
	}
}

bool SoundIoPump::
PrepareFilter(SoundIoFilter *fltp, SoundIoPumpConfig &cfg, ErrorInfo *error)
{
	SoundIoFormat fmt;
	bool res;
	fmt = cfg.fmt;
	fmt.packet_samps = cfg.filter_packet_samps;
	res = fltp->FltPrepare(fmt, cfg.pump_up, cfg.pump_down, error);
	assert(res || !error || error->IsSet());
	return res;
}

bool SoundIoPump::
AddBelow(SoundIoFilter *fltp, SoundIoFilter *targp, ErrorInfo *error)
{
	if (IsStarted() && !PrepareFilter(fltp, m_config, error))
		return false;

	fltp->m_up = targp;
	if (targp) {
		fltp->m_down = targp->m_down;
		if (targp->m_down) {
			assert(m_bottom_flt != targp);
			targp->m_down->m_up = fltp;
		} else {
			assert(m_bottom_flt == targp);
			m_bottom_flt = fltp;
		}
		targp->m_down = fltp;
	} else {
		fltp->m_down = m_top_flt;
		m_top_flt = fltp;
		if (fltp->m_down) {
			fltp->m_down->m_up = fltp;
		} else {
			assert(!m_bottom_flt);
			m_bottom_flt = fltp;
		}
	}
	return true;
}

void SoundIoPump::
RemoveFilter(SoundIoFilter *fltp)
{
	assert(fltp);

	if (fltp->m_up) {
		fltp->m_up->m_down = fltp->m_down;
	} else {
		assert(fltp == m_top_flt);
		m_top_flt = fltp->m_down;
	}

	if (fltp->m_down) {
		fltp->m_down->m_up = fltp->m_up;
	} else {
		assert(fltp == m_bottom_flt);
		m_bottom_flt = fltp->m_up;
	}

	fltp->m_up = 0;
	fltp->m_down = 0;

	if (IsStarted())
		fltp->FltCleanup();
}

void SoundIoPump::
SetLossMode(bool loss_at_bottom, bool loss_at_top)
{
	if (!loss_at_bottom && !loss_at_top)
		abort();

	m_bottom_loss_tolerate = loss_at_bottom;
	m_top_loss_tolerate = loss_at_top;
}

unsigned int SoundIoPump::
GetMinBufferFill(bool top)
{
	int val;
	if (!IsStarted() ||
	    (top && !m_config.top_async) ||
	    (!top && !m_config.bottom_async))
		return 0;
	val = top ? m_config.top_out_min : m_config.bottom_out_min;
	return (val * 1000) / m_config.fmt.samplerate;
}

unsigned int SoundIoPump::
GetJitterWindow(bool top)
{
	int val;
	if (!IsStarted() ||
	    (top && !m_config.top_async) ||
	    (!top && !m_config.bottom_async))
		return 0;
	val = top ? (m_config.top_out_max - m_config.top_out_min)
		: (m_config.bottom_out_max - m_config.bottom_out_min);
	return (val * 1000) / m_config.fmt.samplerate;
}

SoundIoPump::
SoundIoPump(DispatchInterface *eip, SoundIo *bottom)
	: m_ei(eip), m_bottom(0), m_top(0), m_running(false),
	  m_bottom_flt(0), m_top_flt(0),
	  m_bottom_async_started(false), m_top_async_started(false),
	  m_bottom_loss_tolerate(true), m_top_loss_tolerate(true),
	  m_async_entered(false), m_watchdog(0),
	  m_config_out_min_ms(0), m_config_out_window_ms(0), m_stat(0)
{
	SetBottom(bottom);
}

SoundIoPump::
~SoundIoPump()
{
	Stop();
	SetTop(0);
	SetBottom(0);
}

} /* namespace libhfp */
