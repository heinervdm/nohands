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

void NoHands::KeypadPress( char c ) {
}
void NoHands::KeypadClick( char c ) {
    char ss[2] = {c,0};
    PhoneNumberEdit->insert(QString(ss));
}

void NoHands::NumButton1_clicked() { KeypadClick('1'); }
void NoHands::NumButton2_clicked() { KeypadClick('2'); }
void NoHands::NumButton3_clicked() { KeypadClick('3'); }
void NoHands::NumButton4_clicked() { KeypadClick('4'); }
void NoHands::NumButton5_clicked() { KeypadClick('5'); }
void NoHands::NumButton6_clicked() { KeypadClick('6'); }
void NoHands::NumButton7_clicked() { KeypadClick('7'); }
void NoHands::NumButton8_clicked() { KeypadClick('8'); }
void NoHands::NumButton9_clicked() { KeypadClick('9'); }
void NoHands::NumButtonH_clicked() { KeypadClick('#'); }
void NoHands::NumButton0_clicked() { KeypadClick('0'); }
void NoHands::NumButtonS_clicked() { KeypadClick('*'); }

void NoHands::NumButton1_pressed() { KeypadPress('1'); }
void NoHands::NumButton2_pressed() { KeypadPress('2'); }
void NoHands::NumButton3_pressed() { KeypadPress('3'); }
void NoHands::NumButton4_pressed() { KeypadPress('4'); }
void NoHands::NumButton5_pressed() { KeypadPress('5'); }
void NoHands::NumButton6_pressed() { KeypadPress('6'); }
void NoHands::NumButton7_pressed() { KeypadPress('7'); }
void NoHands::NumButton8_pressed() { KeypadPress('8'); }
void NoHands::NumButton9_pressed() { KeypadPress('9'); }
void NoHands::NumButtonH_pressed() { KeypadPress('#'); }
void NoHands::NumButton0_pressed() { KeypadPress('0'); }
void NoHands::NumButtonS_pressed() { KeypadPress('*'); }

void NoHands::NumDelButton_clicked() {
    PhoneNumberEdit->backspace();
}

void NoHands::DialClicked() {
    /* Collect the phone number, clear the input field, invoke the real routine */
    QString phoneNum(PhoneNumberEdit->text());
    PhoneNumberEdit->clear();
    Dial(phoneNum);
}
void NoHands::setStatus( const QString &status ) {
    CallStatus->setText(status);
}

void NoHands::Dial( const QString &phoneNumber ) {
}

void NoHands::setDialButtonsEnabled( bool enabled ) {
    PhoneNumberEdit->setEnabled(enabled);
    PhoneNumberEdit->clear();
    NumDelButton->setEnabled(enabled);
    DialButton->setEnabled(enabled);
    RedialButton->setEnabled(enabled);
}
void NoHands::setDigitButtonsEnabled( bool enabled ) {
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
void NoHands::setHandsetButtonEnabled( bool enabled, bool muted ) {
    if (!enabled) { muted = false; }
    HandsetButton->setEnabled(enabled);
    HandsetButton->setText(QString(muted ? "Speaker" : "Handset"));
}
void NoHands::disableAllButtons() {
    setDialButtonsEnabled(false);
    setDigitButtonsEnabled(false);
    setHandsetButtonEnabled(false, false);
}

void NoHands::DeviceSelectionChanged() {
}

void NoHands::RedialClicked() {
}
void NoHands::HandsetClicked() {
}
void NoHands::OpenScanDialog() {
}
void NoHands::OpenPrefsDialog() {
}
