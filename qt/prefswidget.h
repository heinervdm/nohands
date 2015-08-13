#ifndef PREFSWIDGET_H
#define PREFSWIDGET_H

#include <QFileDialog>
#include <QDialog>
#include <prefs.h>
#include <math.h>

class PrefsWidget : public QDialog, public Ui::PrefsDialog
{
    Q_OBJECT

protected:
	bool Configuring;
    
signals:
	void UpdatePacketSize(int packet_ms);
	void UpdateOutbufSize(int outbuf_ms);
	void UpdateNoiseReduction(bool enable);
	void UpdateEchoCancel(int echocancel_ms);
	void UpdateAutoGain(int agc_level);
	void UpdateDereverb(float drlvl, float drdecay);
	void ToggleProcTest(bool enable);
	void ToggleFeedbackTest(bool enable);

public:
    PrefsWidget(QWidget *parent = 0, Qt::WindowFlags fl = 0) : QDialog(parent, fl) {
		setupUi(this);
		Configuring = false;
	}

	static inline int _ExpToVal(int val) {
		return (int) round(exp2(((double)val) / 82));
	}
	static inline int _ValToExp(int val) {
		return (int) round((log2(val) * 82));
	}

public slots:
	virtual void PacketSize_valueChanged( int val) {
		QString label;
		val = _ExpToVal(val);
		label.sprintf("%dms", val);
		PacketSizeLabel->setText(label);
		if (PacketSize->value() > OutbufSize->value()) {
			OutbufSize->setValue(PacketSize->value());
		}
		if (!Configuring) {
			emit UpdatePacketSize(val);
		}
	}

	virtual void OutbufSize_valueChanged( int val) {
		QString label;
		val = _ExpToVal(val);
		label.sprintf("%dms", val);
		OutbufSizeLabel->setText(label);
		if (OutbufSize->value() < PacketSize->value()) {
			PacketSize->setValue(OutbufSize->value());
		}
		if (!Configuring) {
			emit UpdateOutbufSize(val);
		}
	}

	virtual void NoiseReduction_toggled( bool val) {
		if (!Configuring) {
			emit UpdateNoiseReduction(val);
		}
	}

	virtual void EchoCancelation_toggled( bool enable) {
		EchoCancelTail->setEnabled(enable);
		if (enable) {
			EchoCancelTail_valueChanged(EchoCancelTail->value());
		} else if (!Configuring) {
			emit UpdateEchoCancel(0);
		}
	}

	virtual void EchoCancelTail_valueChanged( int val) {
		QString label;
		// Exponentiate for finer grain control
		val = _ExpToVal(val);
		label.sprintf("%dms", val);
		EchoCancelTailLabel->setText(label);
		if (!Configuring) {
			UpdateEchoCancel(val);
		}
	}

	virtual void AutoGainLevel_valueChanged( int val) {
		QString label;
		label.sprintf("%d", val);
		AutoGainLevelLabel->setText(label);
		if (!Configuring) {
			emit UpdateAutoGain(val);
		}
	}

	virtual void Dereverb_toggled( bool state) {
		DereverbLevel->setEnabled(state);
		DereverbDecay->setEnabled(state);
		if (!state) {
			DereverbLevelLabel->setText("");
			DereverbDecayLabel->setText("");
			if (!Configuring) {
				emit UpdateDereverb(0, 0);
			}
		} else {
			DereverbLevel_valueChanged(DereverbLevel->value());
			DereverbDecay_valueChanged(DereverbDecay->value());
			if (!Configuring) {
				emit UpdateDereverb(((float)DereverbLevel->value()) / 100, ((float)DereverbDecay->value() / 100));
			}
		}
	}

	virtual void DereverbLevel_valueChanged( int val) {
		QString label;
		label.sprintf("%.2f", ((float)val) / 100);
		DereverbLevelLabel->setText(label);
		if (!Configuring) {
			emit UpdateDereverb(val, ((float)DereverbDecay->value()) / 100);
		}
	}

	virtual void DereverbDecay_valueChanged( int val) {
		QString label;
		label.sprintf("%.2f", ((float)val) / 100);
		DereverbDecayLabel->setText(label);
		if (!Configuring) {
			emit UpdateDereverb(((float)DereverbLevel->value()) / 100, val);
		}
	}

	virtual void SetSignalProcValues( int psms, int bsms, int ecms, bool noisereduce, int aglevel, float rvrblvl, float rvrbdecay ) {
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

	virtual void FeedbackTest_toggled( bool val) {
		emit ToggleFeedbackTest(val);
	}

	virtual void RingToneListFiles_clicked()
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

	virtual void ProcTest_toggled( bool val)
	{
		RecTestRadio->setDisabled(val);
		DuplexTestRadio->setDisabled(val);
		PlayTestRadio->setDisabled(val);
		emit ToggleProcTest(val);
	}
};

#endif
