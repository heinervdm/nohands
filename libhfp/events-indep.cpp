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

/*
 * Standalone select()-based event loop for libhfp.
 * Useful for environments lacking a native event loop, e.g. SDL.
 */

#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <libhfp/events-indep.h>
#include <libhfp/list.h>

using namespace libhfp;

/*
 * Timers are stored in a Pairing Heap structure with the values
 * represented as a delta hierarchy.  All children of a particular
 * node have that node's effective timeout value PLUS their own
 * m_msec_delta.
 */

namespace libhfp {
class IndepTimerNotifier : public TimerNotifier {
public:
	ListItem		m_links;
	ListItem		m_children;
	unsigned int		m_msec_delta;
	IndepEventDispatcher	*m_dispatcher;

	virtual void Set(int msec) {
		if (!m_links.Empty())
			m_dispatcher->RemoveTimer(this);
		m_msec_delta = msec;
		m_dispatcher->AddTimer(this);
	}
	virtual void Cancel(void) {
		if (!m_links.Empty())
			m_dispatcher->RemoveTimer(this);
	}
	IndepTimerNotifier(IndepEventDispatcher *disp)
		: m_dispatcher(disp) {}
	virtual ~IndepTimerNotifier() {
		Cancel();
	}
};
} /* namespace libhfp */

void IndepEventDispatcher::
PairTimers(ListItem &siblings, ListItem &dest, unsigned int delta)
{
	ListItem newsublist;
	IndepTimerNotifier *a;

	assert(!siblings.Empty());

	/* Step 1: Pair items left-to-right */
	do {
		a = GetContainer(siblings.next,
				 IndepTimerNotifier, m_links);
		a->m_links.UnlinkOnly();
		a->m_msec_delta += delta;

		if (!siblings.Empty()) {
			IndepTimerNotifier *b;
			b = GetContainer(siblings.next, IndepTimerNotifier,
				      m_links);
			b->m_links.UnlinkOnly();
			b->m_msec_delta += delta;

			if (b->m_msec_delta < a->m_msec_delta) {
				IndepTimerNotifier *t = a; a = b; b = t;
			}

			a->m_children.AppendItem(b->m_links);
			b->m_msec_delta -= a->m_msec_delta;
		}

		newsublist.AppendItem(a->m_links);

	} while (!siblings.Empty());

	/* Step 2: Collect items right-to-left */
	a = GetContainer(newsublist.prev, IndepTimerNotifier, m_links);
	a->m_links.UnlinkOnly();

	while (!newsublist.Empty()) {
		IndepTimerNotifier *b;
		b = GetContainer(newsublist.prev, IndepTimerNotifier,
				 m_links);
		b->m_links.UnlinkOnly();

		if (b->m_msec_delta < a->m_msec_delta) {
			b->m_children.PrependItem(a->m_links);
			a->m_msec_delta -= b->m_msec_delta;
			a = b;
		} else {
			a->m_children.PrependItem(b->m_links);
			b->m_msec_delta -= a->m_msec_delta;
		}
	}

	dest.AppendItem(a->m_links);
}

void IndepEventDispatcher::
AddTimer(IndepTimerNotifier *timerp)
{
	assert(timerp->m_links.Empty());
	assert(timerp->m_children.Empty());

	if (!m_timers.Empty()) {
		IndepTimerNotifier *root_to =
			GetContainer(m_timers.next, IndepTimerNotifier,
				     m_links);
		if (root_to->m_msec_delta < timerp->m_msec_delta) {
			root_to->m_children.PrependItem(timerp->m_links);
			timerp->m_msec_delta -= root_to->m_msec_delta;
			return;
		} else {
			root_to->m_links.UnlinkOnly();
			timerp->m_children.AppendItem(root_to->m_links);
			root_to->m_msec_delta -= timerp->m_msec_delta;
		}
	}

	m_timers.AppendItem(timerp->m_links);
}

void IndepEventDispatcher::
RemoveTimer(IndepTimerNotifier *timerp)
{
	assert(!timerp->m_links.Empty());
	if (!timerp->m_children.Empty())
		PairTimers(timerp->m_children, timerp->m_links,
			   timerp->m_msec_delta);
	assert(timerp->m_children.Empty());
	timerp->m_links.Unlink();
}

void IndepEventDispatcher::
RunTimers(unsigned int ms_elapsed)
{
	ListItem runlist;
	ListItem *listp;

	assert(m_timers.Length() <= 1);

	ListForEach(listp, &m_timers) {
		IndepTimerNotifier *to, *to2;
		to = GetContainer(listp, IndepTimerNotifier, m_links);

		/*
		 * If the timer has expired, append its children
		 * to the m_timers list, and move it to the run list.
		 */
		if (to->m_msec_delta <= ms_elapsed) {
			listp = listp->prev;
			while (!to->m_children.Empty()) {
				to2 = GetContainer(to->m_children.next,
						   IndepTimerNotifier,
						   m_links);
				to2->m_links.UnlinkOnly();
				to2->m_msec_delta += to->m_msec_delta;
				to2->m_links.UnlinkOnly();
				m_timers.AppendItem(to2->m_links);
			}
			to->m_links.UnlinkOnly();
			runlist.AppendItem(to->m_links);
		} else {
			to->m_msec_delta -= ms_elapsed;
		}
	}

	if (m_timers.next != m_timers.prev) {
		ListItem tlist;
		tlist.PrependItemsFrom(m_timers);
		PairTimers(tlist, m_timers, 0);
		assert(tlist.Empty());
		assert(m_timers.Length() == 1);
	}

	while (!runlist.Empty()) {
		IndepTimerNotifier *to;
		to = GetContainer(runlist.next, IndepTimerNotifier, m_links);
		to->m_links.Unlink();
		(*to)(to);
	}
}

TimerNotifier *IndepEventDispatcher::
NewTimer(void)
{
	return new IndepTimerNotifier(this);
}


/*
 * Sockets are stored in a simple singly linked list that is traversed
 * every time we wait for events.
 */

namespace libhfp {
class IndepSocketNotifier : public SocketNotifier {
public:
	ListItem		m_links;
	IndepEventDispatcher	*m_dispatcher;
	int			m_fh;
	bool			m_writable;
	IndepSocketNotifier(IndepEventDispatcher *disp, int fh, bool writable)
		: m_dispatcher(disp), m_fh(fh), m_writable(writable) {}
	virtual void SetEnabled(bool enable) {
		if (!enable == m_links.Empty())
			return;
		if (enable)
			m_dispatcher->AddSocket(this);
		else
			m_dispatcher->RemoveSocket(this);
	}
	virtual ~IndepSocketNotifier() {
		if (!m_links.Empty())
			m_dispatcher->RemoveSocket(this);
	}
};
} /* namespace libhfp */


void IndepEventDispatcher::
AddSocket(IndepSocketNotifier *sockp)
{
	assert(sockp->m_links.Empty());
	m_sockets.AppendItem(sockp->m_links);
}

void IndepEventDispatcher::
RemoveSocket(IndepSocketNotifier *sockp)
{
	/*
	 * NOTE: This might remove the socket from the iorun list
	 * inside of RunOnce(), which is exactly what we want.
	 */
	assert(!sockp->m_links.Empty());
	sockp->m_links.Unlink();
}

SocketNotifier *IndepEventDispatcher::
NewSocket(int fh, bool writable)
{
	IndepSocketNotifier *sockp;
	sockp = new IndepSocketNotifier(this, fh, writable);

	AddSocket(sockp);
	return sockp;
}


void IndepEventDispatcher::
LogVa(DispatchInterface::logtype_t lt, const char *fmt, va_list ap)
{
	vfprintf(stderr, fmt, ap);
}


void IndepEventDispatcher::
RunOnce(int max_sleep_ms)
{
	fd_set readi, writei;
	struct timeval timeout, *top, etime;
	ListItem iorun;
	unsigned int ms_elapsed;
	int maxfh, res;

	FD_ZERO(&readi);
	FD_ZERO(&writei);

	/* Run nonwaiting timers */
	RunTimers(0);

	if (m_timers.Empty() && m_sockets.Empty() && (max_sleep_ms < 0))
		/* Nothing to wait for, we'll wait forever! */
		return;

	/* Move sockets to the I/O run list */
	maxfh = 0;
	while (!m_sockets.Empty()) {
		IndepSocketNotifier *sp;
		sp = GetContainer(m_sockets.next, IndepSocketNotifier,
				  m_links);
		if (sp->m_writable)
			FD_SET(sp->m_fh, &writei);
		else
			FD_SET(sp->m_fh, &readi);

		if (maxfh < sp->m_fh)
			maxfh = sp->m_fh;

		sp->m_links.UnlinkOnly();
		iorun.AppendItem(sp->m_links);
	}

	if (m_timers.Empty()) {
		top = NULL;
		if (max_sleep_ms >= 0) {
			ms_elapsed = max_sleep_ms;
			top = &timeout;
		}
	} else {
		IndepTimerNotifier *to;
		to = GetContainer(m_timers.next, IndepTimerNotifier, m_links);

		/*
		 * We may have spent some time running tasks since
		 * our last gettimeofday() call.  Figure out how
		 * long that has been, and don't spend more time
		 * than that in select().
		 */
		gettimeofday(&etime, NULL);
		if (timercmp(&m_last_run, &etime, >)) {
			/* Time went backwards */
			m_last_run = etime;
			ms_elapsed = 0;
		} else {
			timersub(&etime, &m_last_run, &timeout);
			ms_elapsed = ((timeout.tv_sec * 1000) +
				      (timeout.tv_usec / 1000));
		}

		ms_elapsed = (to->m_msec_delta > ms_elapsed)
			? to->m_msec_delta - ms_elapsed : 0;

		if ((max_sleep_ms >= 0) &&
		    (ms_elapsed > (unsigned int) max_sleep_ms))
			ms_elapsed = max_sleep_ms;

		top = &timeout;
	}

	if (top) {
		timeout.tv_sec = ms_elapsed / 1000;
		timeout.tv_usec = (ms_elapsed % 1000) * 1000;
	}

	res = select(maxfh + 1, &readi, &writei, NULL, top);

	/* Compute elapsed time */
	gettimeofday(&etime, NULL);
	if (timercmp(&m_last_run, &etime, >)) {
		/* Time went backwards */
		ms_elapsed = 0;
	} else {
		timersub(&etime, &m_last_run, &timeout);
		ms_elapsed = ((timeout.tv_sec * 1000) +
			      (timeout.tv_usec / 1000));
	}

	if (ms_elapsed) {
		/*
		 * Only store the last run time if the ms_elapsed
		 * value rounds up to >0.
		 */
		m_last_run = etime;
	}

	/* Run expired timers */
	RunTimers(ms_elapsed);

	/* Move sockets from the I/O run list to the socket list */
	while (!iorun.Empty()) {
		IndepSocketNotifier *sp;
		sp = GetContainer(iorun.next, IndepSocketNotifier, m_links);
		sp->m_links.UnlinkOnly();
		m_sockets.AppendItem(sp->m_links);

		if ((sp->m_writable && FD_ISSET(sp->m_fh, &writei)) ||
		    (!sp->m_writable && FD_ISSET(sp->m_fh, &readi)))
			(*sp)(sp, sp->m_fh);
	}
}

void IndepEventDispatcher::
Run(void)
{
	while (1) {
		if (m_timers.Empty() && m_sockets.Empty())
			return;

		RunOnce(-1);
	}
}

IndepEventDispatcher::
IndepEventDispatcher(void)
{
	gettimeofday(&m_last_run, NULL);
}

IndepEventDispatcher::
~IndepEventDispatcher()
{
}
