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
 * nGhost Hands-Free Profile (nhfp)
 *
 * This module implements a LinuxICE nGhost2 UI for libhfp.
 */

#include <stdio.h>
#include <stdlib.h>

#include <libhfp/events.h>
#include <libhfp/events-indep.h>


class TestObj {
public:
	void Timeout(libhfp::TimerNotifier *timerp) {
		printf("Timer %p fired\n", timerp);
		delete timerp;
	}

	void PrimeTest(libhfp::DispatchInterface *dip) {
		libhfp::TimerNotifier *tp;
		unsigned int ms;
		int i;

		for (i = 0; i < 10; i++) {
			tp = dip->NewTimer();
			ms = random() % 10000;
			printf("tp %p created for %dms\n", tp, ms);
			tp->Register(this, &TestObj::Timeout);
			tp->Set(ms);

			if (random() % 2) {
				ms = random() % 10000;
				printf("tp %p re-registered for %dms\n",
				       tp, ms);
				tp->Set(ms);
			}
		}
	}
};


libhfp::IndepEventDispatcher g_dispatcher;

int
main(int argc, char **argv)
{
	TestObj tester;
	tester.PrimeTest(&g_dispatcher);
	g_dispatcher.Run();

	return 0;
}
