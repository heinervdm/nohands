#ifndef SCANDIALOGWIDGET_H
#define SCANDIALOGWIDGET_H

#include <QDialog>
#include <Q3ListBoxItem>
#include <scandialog.h>

class ScanDialogWidget : public QDialog, public Ui::ScanDialog
{
	Q_OBJECT

public:
	ScanDialogWidget(QWidget *parent = 0, Qt::WindowFlags fl = 0) : QDialog(parent, fl) {
		setupUi(this);
	}

	virtual void SelectDevice( Q3ListBoxItem *item ) {
		if (item == NULL) {
			buttonOk->setEnabled(false);
		} else {
			buttonOk->setEnabled(true);
		}
	}
};

#endif
