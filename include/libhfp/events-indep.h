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

/*
 * Independent event loop implementation for libhfp.
 * Supports file handles and timers only.
 */

#if !defined(__LIBHFP_EVENTS_INDEP_H__)
#define __LIBHFP_EVENTS_INDEP_H__

#include <sys/time.h>

#if defined(USE_PTHREADS)
#include <pthread.h>
#endif

#include <libhfp/events.h>
#include <libhfp/list.h>


namespace libhfp {

class IndepTimerNotifier;
class IndepSocketNotifier;

class IndepEventDispatcher : public DispatchInterface {
	friend class IndepTimerNotifier;
	friend class IndepSocketNotifier;

private:
	ListItem	m_timers;
	ListItem	m_sockets;
	struct timeval	m_last_run;

	bool		m_sleeping;

#if defined(USE_PTHREADS)
	pthread_mutex_t	m_lock;
	int		m_wake_pipe[2];
	SocketNotifier	*m_wake;
	bool		m_wake_pending;
	void Lock(void);
	void Unlock(bool wake = false);
	void WakeNotify(SocketNotifier *notp, int fh);
	bool WakeSetup(void);
	void WakeCleanup(void);
#else
	void Lock(void) {}
	void Unlock(bool wake = false) {}
	bool WakeSetup(void) { return true; }
	void WakeCleanup(void) {}
#endif

	static void PairTimers(ListItem &siblings, ListItem &dest,
			       unsigned int delta);
	void AddTimer(IndepTimerNotifier *);
	void RemoveTimer(IndepTimerNotifier *);
	void RunTimers(unsigned int ms_elapsed);

	void AddSocket(IndepSocketNotifier *);
	void RemoveSocket(IndepSocketNotifier *);

public:
	/*
	 * DispatchInterface methods
	 */
	virtual SocketNotifier *NewSocket(int fh, bool writable);
	virtual TimerNotifier *NewTimer(void);
	virtual void LogVa(DispatchInterface::logtype_t lt,
			   const char *fmt, va_list ap);

	/*
	 * Direct methods
	 */

	void RunOnce(int max_sleep_ms = -1);
	void Run(void);

	IndepEventDispatcher(void);
	virtual ~IndepEventDispatcher();
};


} /* namespace libhfp */
#endif /* !defined(__LIBHFP_EVENTS_INDEP_H__) */
