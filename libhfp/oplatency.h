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

#if !defined(__PRIVATE_OPLATENCY_H__)
#define __PRIVATE_OPLATENCY_H__

#if !defined(NDEBUG)
#define DO_OPLATENCY
#endif

#if defined(DO_OPLATENCY)
#include <sys/time.h>
#include <libhfp/events.h>
#endif /* defined(DO_OPLATENCY) */

/*
 * Problems can be caused by a function incurring higher than expected
 * CPU usage, or more commonly, unexpectedly blocking in the kernel.
 *
 * To diagnose operations incurring more latency than expected, we
 * use the OpLatencyMonitor class.  We instantiate one on the stack
 * frame of a function or block to be monitored.
 */

class OpLatencyMonitor {
public:
#if defined(DO_OPLATENCY)
	libhfp::DispatchInterface	*m_di;
	const char			*m_label;
	int				m_trigger;
	struct timeval			m_start;

	OpLatencyMonitor(libhfp::DispatchInterface *di, const char *label,
			 int trigger_ms = 10)
		: m_di(di), m_label(label), m_trigger(trigger_ms) {
		gettimeofday(&m_start, NULL);
	}

	~OpLatencyMonitor() {
		struct timeval rtv;
		gettimeofday(&rtv, NULL);
		/* Make sure time doesn't go backwards */
		if (timercmp(&m_start, &rtv, <)) {
			timersub(&rtv, &m_start, &rtv);
			if ((rtv.tv_sec > (m_trigger / 1000)) ||
			    ((rtv.tv_sec == (m_trigger / 1000)) &&
			     ((rtv.tv_usec / 1000) > (m_trigger % 1000)))) {
				m_di->LogDebug("** OpLatency: %s took %ldms",
					       m_label,
					       (rtv.tv_sec * 1000) +
					       (rtv.tv_usec / 1000));
			}
		}
	}
#else /* defined(DO_OPLATENCY) */
	OpLatencyMonitor(libhfp::DispatchInterface *, const char *,
			 int trig = 0) {}
#endif /* defined(DO_OPLATENCY) */
};


#endif /* !defined(__PRIVATE_OPLATENCY_H__) */
