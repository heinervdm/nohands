/****************************************************************************
** ui.h extension file, included from the uic-generated form implementation.
**
** If you want to add, delete, or rename functions or slots, use
** Qt Designer to update this file, preserving your code.
**
** You should not define a constructor or destructor in this file.
** Instead, write your code in functions called init() and destroy().
** These will automatically be called by the form's constructor and
** destructor.
*****************************************************************************/

#include <qfiledialog.h>
#include <math.h>

static inline int _ExpToVal(int val) {
    return (int) round(exp2(((double)val) / 82));
}
static inline int _ValToExp(int val) {
    return (int) round((log2(val) * 82));
}

void PrefsDialog::PacketSize_valueChanged( int val) {
    QString label;
    val = _ExpToVal(val);
    label.sprintf("%dms", val);
    PacketSizeLabel->setText(label);
    if (PacketSize->value() > OutbufSize->value()) {
	OutbufSize->setValue(PacketSize->value());
    }
    if (!Configuring) {
	UpdatePacketSize(val);
    }
}

void PrefsDialog::OutbufSize_valueChanged( int val) {
    QString label;
    val = _ExpToVal(val);
    label.sprintf("%dms", val);
    OutbufSizeLabel->setText(label);
    if (OutbufSize->value() < PacketSize->value()) {
	PacketSize->setValue(OutbufSize->value());
    }
    if (!Configuring) {
	UpdateOutbufSize(val);
    }
}

void PrefsDialog::NoiseReduction_toggled( bool val) {
    if (!Configuring) {
	UpdateNoiseReduction(val);
    }
}

void PrefsDialog::EchoCancelation_toggled( bool enable) {
    EchoCancelTail->setEnabled(enable);
    if (enable) {
	EchoCancelTail_valueChanged(EchoCancelTail->value());
    } else if (!Configuring) {
	UpdateEchoCancel(0);
    }
}

void PrefsDialog::EchoCancelTail_valueChanged( int val) {
    QString label;
    // Exponentiate for finer grain control
    val = _ExpToVal(val);
    label.sprintf("%dms", val);
    EchoCancelTailLabel->setText(label);
    if (!Configuring) {
	UpdateEchoCancel(val);
    }
}

void PrefsDialog::AutoGainLevel_valueChanged( int val) {
    QString label;
    label.sprintf("%d", val);
    AutoGainLevelLabel->setText(label);
    if (!Configuring) {
	UpdateAutoGain(val);
    }
}

void PrefsDialog::Dereverb_toggled( bool state) {
    DereverbLevel->setEnabled(state);
    DereverbDecay->setEnabled(state);
    if (!state) {
	DereverbLevelLabel->setText("");
	DereverbDecayLabel->setText("");
	if (!Configuring) {
	    UpdateDereverb(0, 0);
	}
    } else {
	DereverbLevel_valueChanged(DereverbLevel->value());
	DereverbDecay_valueChanged(DereverbDecay->value());
	if (!Configuring) {
	    UpdateDereverb(((float)DereverbLevel->value()) / 100, ((float)DereverbDecay->value() / 100));
	}
    }
}

void PrefsDialog::DereverbLevel_valueChanged( int val) {
    QString label;
    label.sprintf("%.2f", ((float)val) / 100);
    DereverbLevelLabel->setText(label);
    if (!Configuring) {
	UpdateDereverb(val, ((float)DereverbDecay->value()) / 100);
    }
}

void PrefsDialog::DereverbDecay_valueChanged( int val) {
    QString label;
    label.sprintf("%.2f", ((float)val) / 100);
    DereverbDecayLabel->setText(label);
    if (!Configuring) {
	UpdateDereverb(((float)DereverbLevel->value()) / 100, val);
    }
}

void PrefsDialog::SetSignalProcValues( int psms, int bsms, int ecms, bool noisereduce, int aglevel, float rvrblvl, float rvrbdecay ) {
    Configuring = true;
    psms = _ValToExp(psms);
    PacketSize->setValue(psms);
    PacketSize_valueChanged(psms);
    bsms = _ValToExp(bsms);
    OutbufSize->setValue(bsms);
    OutbufSize_valueChanged(bsms);
    NoiseReduction->setChecked(noisereduce);
    NoiseReduction_toggled(noisereduce);
    ecms = _ValToExp(ecms);
    EchoCancelTail->setValue(ecms);
    EchoCancelation->setChecked(ecms ? true : false);
    EchoCancelation_toggled(ecms ? true : false);
    AutoGainLevel->setValue(aglevel);
    AutoGainLevel_valueChanged(aglevel);
    if (rvrblvl == 0) {
	Dereverb_toggled(false);
    } else {
	int i_rl = (int) (rvrblvl * 100), i_rd = (int) (rvrbdecay * 100);
	Dereverb_toggled(true);
	DereverbLevel->setValue(i_rl);
	DereverbLevel_valueChanged(i_rl);
	DereverbDecay->setValue(i_rd);
	DereverbDecay_valueChanged(i_rd);
    }
    Configuring = false;
}

void PrefsDialog::FeedbackTest_toggled( bool val) {
    ToggleFeedbackTest(val);
}


void PrefsDialog::RingToneListFiles_clicked()
{
    QString s = QFileDialog::getOpenFileName(
	    RingToneFile->text(),
	    "Sound Files (*.wav)",
	    this,
	    "choose ring tone",
	    "Select a Ring Tone Sound");
    if (!s.isNull()) {
	RingToneFile->setText(s);
    }
}


void PrefsDialog::ProcTest_toggled( bool val)
{
    RecTestRadio->setDisabled(val);
    DuplexTestRadio->setDisabled(val);
    PlayTestRadio->setDisabled(val);
    ToggleProcTest(val);
}
