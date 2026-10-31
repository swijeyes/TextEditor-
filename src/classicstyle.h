#pragma once

#include <QProxyStyle>

// "Classic" scroll bars: a glossy blue capsule thumb in a shaded track,
// with both arrow buttons together at the bottom (or right) end. Everything else is left to the platform style.
//
// The track and buttons follow the scroll bar's palette (light or dark),
// or the "classicDark" property when a widget sets it on its scroll bars. The
// thumb turns grey while the window is in the background.
class ClassicStyle : public QProxyStyle
{
    Q_OBJECT

public:
    // Wraps the platform's default style (or the named one).
    explicit ClassicStyle(const QString &baseKey = QString());

    static constexpr int Extent = 15;       // scroll bar thickness
    static constexpr int ButtonLength = 16; // each arrow button
    static constexpr int MinThumb = 24;

    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr, const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sub,
                         const QWidget *widget = nullptr) const override;
    SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                     const QPoint &pos, const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                            const QWidget *widget = nullptr) const override;
    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};
