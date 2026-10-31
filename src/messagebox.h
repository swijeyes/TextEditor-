#pragma once

#include <QMessageBox>

// Message text often contains file names, paths or text from a server, so
// it is always shown as plain text: QMessageBox would otherwise render a
// name such as "<img src=...>.txt" as HTML.
// Tooltips guess whether their text is HTML, so names and paths go through
// this, which always shows them as plain text.
QString plainToolTip(const QString &text);

QMessageBox::StandardButton showMessage(
    QWidget *parent, QMessageBox::Icon icon, const QString &title, const QString &text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
