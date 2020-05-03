#ifndef SCANDIALOGWIDGET_H
#define SCANDIALOGWIDGET_H

#include <QDialog>
#include <QListWidgetItem>
#include <scandialog.h>

class ScanDialogWidget : public QDialog, public Ui::ScanDialog
{
	Q_OBJECT

public:
	ScanDialogWidget(QWidget *parent = 0, Qt::WindowFlags fl = 0) : QDialog(parent, fl) {
		setupUi(this);
	}

public slots:
	virtual void SelectDevice( QListWidgetItem *item ) {
		if (item == NULL) {
			buttonOk->setEnabled(false);
		} else {
			buttonOk->setEnabled(true);
		}
	}
};

#endif
