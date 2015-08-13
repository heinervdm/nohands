#if !defined(__NOHANDSWIDGET_H__)
#define __NOHANDSWIDGET_H__

#include <QDialog>
#include <nohands.h>

class NoHandsWidget : public QDialog, public Ui::NoHands
{
	Q_OBJECT

public:
	NoHandsWidget(QWidget *parent = 0, Qt::WindowFlags fl = 0) : QDialog(parent, fl) {
		setupUi(this);
	}

public slots:
	virtual void KeypadPress( char c ) {
	}

	virtual void KeypadClick( char c ) {
		char ss[2] = {c,0};
		PhoneNumberEdit->insert(QString(ss));
	}

	virtual void NumButton1_clicked() { KeypadClick('1'); }
	virtual void NumButton2_clicked() { KeypadClick('2'); }
	virtual void NumButton3_clicked() { KeypadClick('3'); }
	virtual void NumButton4_clicked() { KeypadClick('4'); }
	virtual void NumButton5_clicked() { KeypadClick('5'); }
	virtual void NumButton6_clicked() { KeypadClick('6'); }
	virtual void NumButton7_clicked() { KeypadClick('7'); }
	virtual void NumButton8_clicked() { KeypadClick('8'); }
	virtual void NumButton9_clicked() { KeypadClick('9'); }
	virtual void NumButtonH_clicked() { KeypadClick('#'); }
	virtual void NumButton0_clicked() { KeypadClick('0'); }
	virtual void NumButtonS_clicked() { KeypadClick('*'); }

	virtual void NumButton1_pressed() { KeypadPress('1'); }
	virtual void NumButton2_pressed() { KeypadPress('2'); }
	virtual void NumButton3_pressed() { KeypadPress('3'); }
	virtual void NumButton4_pressed() { KeypadPress('4'); }
	virtual void NumButton5_pressed() { KeypadPress('5'); }
	virtual void NumButton6_pressed() { KeypadPress('6'); }
	virtual void NumButton7_pressed() { KeypadPress('7'); }
	virtual void NumButton8_pressed() { KeypadPress('8'); }
	virtual void NumButton9_pressed() { KeypadPress('9'); }
	virtual void NumButtonH_pressed() { KeypadPress('#'); }
	virtual void NumButton0_pressed() { KeypadPress('0'); }
	virtual void NumButtonS_pressed() { KeypadPress('*'); }

	virtual void NumDelButton_clicked() {
		PhoneNumberEdit->backspace();
	}

	virtual void DialClicked() {
		/* Collect the phone number, clear the input field, invoke the real routine */
		QString phoneNum(PhoneNumberEdit->text());
		PhoneNumberEdit->clear();
		Dial(phoneNum);
	}

	virtual void setStatus( const QString &status ) {
		CallStatus->setText(status);
	}

	virtual void Dial( const QString &phoneNumber ) {
	}

	virtual void setDialButtonsEnabled( bool enabled ) {
		PhoneNumberEdit->setEnabled(enabled);
		PhoneNumberEdit->clear();
		NumDelButton->setEnabled(enabled);
		DialButton->setEnabled(enabled);
		RedialButton->setEnabled(enabled);
	}

	virtual void setDigitButtonsEnabled( bool enabled ) {
		NumButton1->setEnabled(enabled);
		NumButton2->setEnabled(enabled);
		NumButton3->setEnabled(enabled);
		NumButton4->setEnabled(enabled);
		NumButton5->setEnabled(enabled);
		NumButton6->setEnabled(enabled);
		NumButton7->setEnabled(enabled);
		NumButton8->setEnabled(enabled);
		NumButton9->setEnabled(enabled);
		NumButtonH->setEnabled(enabled);
		NumButton0->setEnabled(enabled);
		NumButtonS->setEnabled(enabled);
	}

	virtual void setHandsetButtonEnabled( bool enabled, bool muted ) {
		if (!enabled) { muted = false; }
		HandsetButton->setEnabled(enabled);
		HandsetButton->setText(QString(muted ? "Speaker" : "Handset"));
	}

	virtual void disableAllButtons() {
		setDialButtonsEnabled(false);
		setDigitButtonsEnabled(false);
		setHandsetButtonEnabled(false, false);
	}

	virtual void DeviceSelectionChanged() {
	}

	virtual void RedialClicked() {
	}

	virtual void HandsetClicked() {
	}

	virtual void OpenScanDialog() {
	}

	virtual void OpenPrefsDialog() {
	}

}; 

#endif
