// The Classic scroll bars: both arrows together at the end, the thumb in the
// track before them, and clicks reaching the right parts.

#include "classicstyle.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QTest>

class TestClassicStyle : public QObject
{
    Q_OBJECT

private:
    QStyleOptionSlider option(QScrollBar *bar) const
    {
        QStyleOptionSlider opt;
        opt.initFrom(bar);
        opt.subControls = QStyle::SC_All;
        opt.orientation = bar->orientation();
        opt.minimum = bar->minimum();
        opt.maximum = bar->maximum();
        opt.sliderPosition = bar->sliderPosition();
        opt.sliderValue = bar->value();
        opt.singleStep = bar->singleStep();
        opt.pageStep = bar->pageStep();
        opt.upsideDown = bar->invertedAppearance();
        if (bar->orientation() == Qt::Horizontal)
            opt.state |= QStyle::State_Horizontal;
        return opt;
    }

    QRect rect(QScrollBar *bar, QStyle::SubControl sub) const
    {
        const QStyleOptionSlider opt = option(bar);
        return bar->style()->subControlRect(QStyle::CC_ScrollBar, &opt, sub, bar);
    }

    std::unique_ptr<ClassicStyle> m_style = std::make_unique<ClassicStyle>(QStringLiteral("Fusion"));

private slots:
    void layout_data()
    {
        QTest::addColumn<Qt::Orientation>("orientation");
        QTest::newRow("vertical") << Qt::Vertical;
        QTest::newRow("horizontal") << Qt::Horizontal;
    }

    void layout()
    {
        QFETCH(Qt::Orientation, orientation);
        QScrollBar bar(orientation);
        bar.setStyle(m_style.get());
        bar.resize(orientation == Qt::Vertical ? QSize(ClassicStyle::Extent, 300) : QSize(300, ClassicStyle::Extent));
        bar.setRange(0, 1000);
        bar.setPageStep(100);

        const auto start = [orientation](const QRect &r) { return orientation == Qt::Vertical ? r.top() : r.left(); };
        const auto end = [orientation](const QRect &r) { return orientation == Qt::Vertical ? r.bottom() : r.right(); };
        const auto length = [orientation](const QRect &r) { return orientation == Qt::Vertical ? r.height() : r.width(); };

        const QRect groove = rect(&bar, QStyle::SC_ScrollBarGroove);
        const QRect up = rect(&bar, QStyle::SC_ScrollBarSubLine);
        const QRect down = rect(&bar, QStyle::SC_ScrollBarAddLine);
        // Track first, then the two arrows together at the end.
        QCOMPARE(start(groove), 0);
        QCOMPARE(start(up), end(groove) + 1);
        QCOMPARE(start(down), end(up) + 1);
        QCOMPARE(end(down), 299);
        QCOMPARE(length(up), ClassicStyle::ButtonLength);
        QCOMPARE(length(down), ClassicStyle::ButtonLength);

        // The thumb moves along the track, in proportion.
        QRect thumb = rect(&bar, QStyle::SC_ScrollBarSlider);
        QCOMPARE(start(thumb), 0);
        QVERIFY(qAbs(length(thumb) - length(groove) * 100 / 1100) <= 1);
        bar.setValue(1000);
        thumb = rect(&bar, QStyle::SC_ScrollBarSlider);
        QCOMPARE(end(thumb), end(groove));
        bar.setValue(500);
        thumb = rect(&bar, QStyle::SC_ScrollBarSlider);
        QCOMPARE(end(rect(&bar, QStyle::SC_ScrollBarSubPage)) + 1, start(thumb));
        QCOMPARE(start(rect(&bar, QStyle::SC_ScrollBarAddPage)), end(thumb) + 1);

        // Hit testing.
        const QStyleOptionSlider opt = option(&bar);
        QCOMPARE(m_style->hitTestComplexControl(QStyle::CC_ScrollBar, &opt, up.center(), &bar),
                 QStyle::SC_ScrollBarSubLine);
        QCOMPARE(m_style->hitTestComplexControl(QStyle::CC_ScrollBar, &opt, down.center(), &bar),
                 QStyle::SC_ScrollBarAddLine);
        QCOMPARE(m_style->hitTestComplexControl(QStyle::CC_ScrollBar, &opt, thumb.center(), &bar),
                 QStyle::SC_ScrollBarSlider);
    }

    // Clicking the arrows and the track scrolls; dragging the thumb too.
    void clicks()
    {
        QScrollBar bar(Qt::Vertical);
        bar.setStyle(m_style.get());
        bar.resize(ClassicStyle::Extent, 300);
        bar.setRange(0, 1000);
        bar.setPageStep(100);
        bar.setSingleStep(10);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));

        QTest::mouseClick(&bar, Qt::LeftButton, {}, rect(&bar, QStyle::SC_ScrollBarAddLine).center());
        QCOMPARE(bar.value(), 10);
        QTest::mouseClick(&bar, Qt::LeftButton, {}, rect(&bar, QStyle::SC_ScrollBarSubLine).center());
        QCOMPARE(bar.value(), 0);

        const QPoint from = rect(&bar, QStyle::SC_ScrollBarSlider).center();
        const QRect groove = rect(&bar, QStyle::SC_ScrollBarGroove);
        QTest::mousePress(&bar, Qt::LeftButton, {}, from);
        QMouseEvent move(QEvent::MouseMove, QPointF(from.x(), groove.bottom()), bar.mapToGlobal(QPointF(from.x(), groove.bottom())),
                         Qt::NoButton, Qt::LeftButton, {});
        QApplication::sendEvent(&bar, &move);
        QTest::mouseRelease(&bar, Qt::LeftButton, {}, QPoint(from.x(), groove.bottom()));
        QCOMPARE(bar.value(), 1000);
    }

    // A tiny bar has just the two arrows; a bar with nothing to scroll has
    // no thumb. Both draw without trouble, in both themes.
    void edgeCases()
    {
        QScrollBar bar(Qt::Horizontal);
        bar.setStyle(m_style.get());
        bar.resize(20, ClassicStyle::Extent);
        bar.setRange(0, 100);
        QCOMPARE(rect(&bar, QStyle::SC_ScrollBarSubLine).width() + rect(&bar, QStyle::SC_ScrollBarAddLine).width(), 20);
        QVERIFY(rect(&bar, QStyle::SC_ScrollBarGroove).width() <= 0);

        for (bool dark : {false, true}) {
            for (int width : {0, 1, 20, 300}) {
                bar.setProperty("classicDark", dark);
                bar.resize(width, ClassicStyle::Extent);
                bar.setRange(0, width == 300 ? 0 : 100);
                QImage image(qMax(1, width), ClassicStyle::Extent, QImage::Format_ARGB32);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                const QStyleOptionSlider opt = option(&bar);
                m_style->drawComplexControl(QStyle::CC_ScrollBar, &opt, &painter, &bar);
            }
        }
    }

    void metrics()
    {
        QCOMPARE(m_style->pixelMetric(QStyle::PM_ScrollBarExtent), ClassicStyle::Extent);
        QCOMPARE(m_style->styleHint(QStyle::SH_ScrollBar_Transient), 0);
    }
};

QTEST_MAIN(TestClassicStyle)
#include "tst_classicstyle.moc"
