/*
 * Software Hands-Free with Crappy UI
 */

#include <stdio.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libhfp/soundio.h>
#include <libhfp/events-indep.h>

using namespace libhfp;

/* The global notifier factory */
static IndepEventDispatcher g_dispatcher;


class TimerCrap {
public:
	TimerNotifier *timer;

	void Tick(TimerNotifier *tnp, SoundIoManager *siop) {
		bool res;

		assert(tnp == timer);
		res = siop->GetMute();
		if (res) {
			printf("Disabling mute\n");
		} else {
			printf("Enabling mute\n");
		}
		res = siop->SetMute(!res);
		assert(res);
		tnp->Set(1000);
	}
};


int
main(int argc, char **argv)
{
	SoundIoFormat soundio_format;
	SoundIoManager *soundp;
	SoundIo *sndp;
	SoundIoFilter *fltp;
	SoundIoFltSpeex *fltsp;
	SoundIoSpeexProps sprops;
	bool res;
	bool do_file = false;
	TimerCrap tc;

	soundp = new SoundIoManager(&g_dispatcher);
	assert(soundp);

	res = soundp->SetDriver("ALSA", "plughw:0");
	assert(res);

	if (!do_file) {

		soundio_format.samplerate = 8000;
		soundio_format.sampletype = SIO_PCM_S16_LE;
		soundio_format.nchannels = 1;
		soundio_format.bytes_per_record = 2;
		soundio_format.packet_samps = 64;

		sndp = SoundIoCreateAlsa(&g_dispatcher,
					 "plughw:1", 0);

	} else {
		sndp = SoundIoCreateFileHandler(&g_dispatcher,
						"/vhome/samr7/R2D2.wav",
						 false);
	}
	assert(sndp);

	soundp->SetSecondary(sndp);
	soundp->SetPacketIntervalHint(5);
	soundp->SetMinBufferFillHint(100);

	sprops.noisereduce = true;
	sprops.echocancel_ms = 200;
	sprops.agc_level = 0;
	sprops.dereverb_level = 0.0;
	sprops.dereverb_decay = 0.0;

	fltsp = SoundIoFltCreateSpeex(&g_dispatcher);
	fltsp->Configure(sprops);
	//soundp->AddBottom(fltsp);

	fltp = SoundIoFltCreateDummy();
	//soundp->AddTop(fltp);

	res = sndp->SndOpen(!do_file, true);
	if (!res) {
		printf("Could not open secondary\n");
		return 1;
	}

	soundp->Start();

	tc.timer = g_dispatcher.NewTimer();
	tc.timer->Bind(&tc, &TimerCrap::Tick, Arg1, soundp);
	tc.timer->Set(1000);

	assert(soundp->IsStarted());

	g_dispatcher.Run();
	return 0;
}
