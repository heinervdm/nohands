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
 * The purpose of this module is to glue together the Qt library and
 * the user interface components with the framework-agnostic libhfp.
 */

#if !defined(__EVENTS_QT_H__)
#define __EVENTS_QT_H__

#include <qobject.h>
#include <qsocketnotifier.h>
#include <qtimer.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

#include <libhfp/events.h>

using namespace libhfp;

class QtEiTimerNotifier : public QObject, public TimerNotifier {
	Q_OBJECT;
	bool timer_set;
public:
	void timerEvent(QTimerEvent *e) {
		killTimer(e->timerId());
		timer_set = false;
		(*this)(this);
	}
	virtual void Set(int msec) {
		assert(Registered());
		if (timer_set)
			Cancel();
		startTimer(msec);
		timer_set = true;
	}
	virtual void Cancel(void) {
		if (timer_set) {
			killTimers();
			timer_set = false;
		}
	}
	QtEiTimerNotifier(void) : QObject(0, 0), timer_set(false) {}
	virtual ~QtEiTimerNotifier() {
		Cancel();
	}
};
class QtEiSocketNotifier : public QObject, public SocketNotifier {
	Q_OBJECT;

public slots:
	void SocketNotify(int fh) { (*this)(this, fh); }
public:
	QSocketNotifier			*m_sn;
	QtEiSocketNotifier(int fh, bool nwrite)
		: m_sn(new QSocketNotifier(fh, (nwrite
						? QSocketNotifier::Write
						: QSocketNotifier::Read))) {
		connect(m_sn, SIGNAL(activated(int)),
			this, SLOT(SocketNotify(int)));
	}
	virtual void SetEnabled(bool enable) {
		m_sn->setEnabled(enable);
	}
	virtual ~QtEiSocketNotifier() {
		if (m_sn) {
			delete m_sn;
			m_sn = NULL;
		}
	}
};

class QtEventDispatchInterface : public DispatchInterface {
public:

public:
	/*
	 * Socket event reporting
	 */
	virtual SocketNotifier *NewSocket(int fh, bool nwrite)
		{ return new QtEiSocketNotifier(fh, nwrite); }
	virtual TimerNotifier *NewTimer(void)
		{ return new QtEiTimerNotifier; }
	virtual void LogVa(DispatchInterface::logtype_t lt, const char *fmt,
			   va_list ap) {
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	}
	QtEventDispatchInterface() {}
};

#endif /* !defined(__EVENTS_QT_H__) */
