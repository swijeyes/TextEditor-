#include "messagebox.h"

QString plainToolTip(const QString &text)
{
    if (text.isEmpty())
        return text;
    return QStringLiteral("<p style='white-space:pre'>") + text.toHtmlEscaped() + QStringLiteral("</p>");
}

QMessageBox::StandardButton showMessage(
    QWidget *parent, QMessageBox::Icon icon, const QString &title, const QString &text,
    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    QMessageBox box(icon, title, text, buttons, parent);
    box.setTextFormat(Qt::PlainText);
    if (defaultButton != QMessageBox::NoButton)
        box.setDefaultButton(defaultButton);
    return QMessageBox::StandardButton(box.exec());
}
