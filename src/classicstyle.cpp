#include "classicstyle.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyleFactory>
#include <QStyleOptionSlider>

namespace {

// The parts of a scroll bar, as offsets along its length.
struct Layout
{
    int length;  // the whole bar
    int groove;  // the track, from the start
    int button;  // each arrow button, after the track
    int thumbPos;
    int thumbLength;
};

Layout layoutFor(const QStyleOptionSlider *bar)
{
    const bool vertical = bar->orientation == Qt::Vertical;
    Layout l{};
    l.length = vertical ? bar->rect.height() : bar->rect.width();
    l.button = qMin(ClassicStyle::ButtonLength, l.length / 2);
    l.groove = l.length - 2 * l.button;
    // Too short for a track: just the two arrows.
    if (l.groove < ClassicStyle::MinThumb / 2) {
        l.button = l.length / 2;
        l.groove = l.length - 2 * l.button;
    }

    const qint64 range = qint64(bar->maximum) - bar->minimum;
    if (range <= 0 || l.groove <= 0) {
        l.thumbPos = 0;
        l.thumbLength = l.groove;
        return l;
    }
    const qint64 page = qMax(0, bar->pageStep);
    l.thumbLength = int(page * l.groove / (range + page));
    l.thumbLength = qBound(qMin(ClassicStyle::MinThumb, l.groove), l.thumbLength, l.groove);
    l.thumbPos = QStyle::sliderPositionFromValue(bar->minimum, bar->maximum, bar->sliderPosition,
                                                 l.groove - l.thumbLength, bar->upsideDown);
    return l;
}

// A piece of the bar from `start`, `length` long, across its full width.
QRect along(const QRect &bar, Qt::Orientation orientation, int start, int length)
{
    return orientation == Qt::Vertical ? QRect(bar.x(), bar.y() + start, bar.width(), length)
                                       : QRect(bar.x() + start, bar.y(), length, bar.height());
}

// A gradient across the bar's width (left to right on a vertical bar, top
// to bottom on a horizontal one), for a glossy, lit-from-the-side look.
QLinearGradient across(const QRectF &r, Qt::Orientation orientation,
                       std::initializer_list<QPair<qreal, const char *>> stops)
{
    QLinearGradient g = orientation == Qt::Vertical ? QLinearGradient(r.topLeft(), r.topRight())
                                                    : QLinearGradient(r.topLeft(), r.bottomLeft());
    for (const auto &stop : stops)
        g.setColorAt(stop.first, QColor(QLatin1String(stop.second)));
    return g;
}

bool isDark(const QStyleOption *option, const QWidget *widget)
{
    for (const QWidget *w = widget; w; w = w->parentWidget()) {
        const QVariant dark = w->property("classicDark");
        if (dark.isValid())
            return dark.toBool();
        if (w->isWindow())
            break;
    }
    return option->palette.color(QPalette::Window).lightness() < 128;
}

// A capsule (a rectangle with fully rounded ends) inside `r`.
QPainterPath capsule(const QRectF &r)
{
    const qreal radius = qMin(r.width(), r.height()) / 2;
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    return path;
}

void drawArrow(QPainter *p, const QRect &r, Qt::ArrowType type, const QColor &color)
{
    const QPointF c = QRectF(r).center();
    const qreal half = 3.5; // half the base
    const qreal height = 4.5;
    QPolygonF triangle;
    switch (type) {
    case Qt::UpArrow:
        triangle << QPointF(c.x() - half, c.y() + height / 2) << QPointF(c.x() + half, c.y() + height / 2)
                 << QPointF(c.x(), c.y() - height / 2);
        break;
    case Qt::DownArrow:
        triangle << QPointF(c.x() - half, c.y() - height / 2) << QPointF(c.x() + half, c.y() - height / 2)
                 << QPointF(c.x(), c.y() + height / 2);
        break;
    case Qt::LeftArrow:
        triangle << QPointF(c.x() + height / 2, c.y() - half) << QPointF(c.x() + height / 2, c.y() + half)
                 << QPointF(c.x() - height / 2, c.y());
        break;
    default:
        triangle << QPointF(c.x() - height / 2, c.y() - half) << QPointF(c.x() - height / 2, c.y() + half)
                 << QPointF(c.x() + height / 2, c.y());
        break;
    }
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawPolygon(triangle);
}

} // namespace

ClassicStyle::ClassicStyle(const QString &baseKey)
    : QProxyStyle(baseKey.isEmpty() ? nullptr : QStyleFactory::create(baseKey))
{
}

int ClassicStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    switch (metric) {
    case PM_ScrollBarExtent:
        return Extent;
    case PM_ScrollBarSliderMin:
        return MinThumb;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int ClassicStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                         QStyleHintReturn *returnData) const
{
    switch (hint) {
    // Always there, beside the text, rather than macOS's overlay scroll bars.
    case SH_ScrollBar_Transient:
        return false;
    default:
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

QSize ClassicStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                                  const QWidget *widget) const
{
    if (type == CT_ScrollBar)
        return size;
    return QProxyStyle::sizeFromContents(type, option, size, widget);
}

QRect ClassicStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sub,
                                const QWidget *widget) const
{
    const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (control != CC_ScrollBar || !bar)
        return QProxyStyle::subControlRect(control, option, sub, widget);

    const Layout l = layoutFor(bar);
    const Qt::Orientation o = bar->orientation;
    QRect rect;
    switch (sub) {
    case SC_ScrollBarGroove:
        rect = along(bar->rect, o, 0, l.groove);
        break;
    case SC_ScrollBarSlider:
        rect = along(bar->rect, o, l.thumbPos, l.thumbLength);
        break;
    case SC_ScrollBarSubPage:
        rect = along(bar->rect, o, 0, l.thumbPos);
        break;
    case SC_ScrollBarAddPage:
        rect = along(bar->rect, o, l.thumbPos + l.thumbLength, l.groove - l.thumbPos - l.thumbLength);
        break;
    case SC_ScrollBarSubLine:
        rect = along(bar->rect, o, l.groove, l.button);
        break;
    case SC_ScrollBarAddLine:
        rect = along(bar->rect, o, l.groove + l.button, l.length - l.groove - l.button);
        break;
    default:
        break;
    }
    return visualRect(bar->direction, bar->rect, rect);
}

QStyle::SubControl ClassicStyle::hitTestComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                                    const QPoint &pos, const QWidget *widget) const
{
    const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (control != CC_ScrollBar || !bar)
        return QProxyStyle::hitTestComplexControl(control, option, pos, widget);
    for (SubControl sub : {SC_ScrollBarSlider, SC_ScrollBarSubLine, SC_ScrollBarAddLine,
                           SC_ScrollBarSubPage, SC_ScrollBarAddPage}) {
        if (subControlRect(control, option, sub, widget).contains(pos))
            return sub;
    }
    return SC_None;
}

void ClassicStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *p,
                                   const QWidget *widget) const
{
    const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (control != CC_ScrollBar || !bar) {
        QProxyStyle::drawComplexControl(control, option, p, widget);
        return;
    }

    const bool dark = isDark(option, widget);
    const bool vertical = bar->orientation == Qt::Vertical;
    const Qt::Orientation o = bar->orientation;
    const bool enabled = (bar->state & State_Enabled) && bar->maximum > bar->minimum;
    const bool active = bar->state & State_Active;
    const auto pressed = [bar](SubControl sub) {
        return (bar->state & State_Sunken) && (bar->activeSubControls & sub);
    };
    const QRect r = bar->rect;
    const QColor line = dark ? QColor(0x15, 0x16, 0x1b) : QColor(0x8e, 0x8e, 0x8e);

    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    // The track: an inset channel with a rounded far end.
    p->fillRect(r, dark ? QColor(0x2b, 0x2c, 0x34) : QColor(0xe4, 0xe4, 0xe4));
    const QRect groove = subControlRect(control, option, SC_ScrollBarGroove, widget);
    if (!groove.isEmpty()) {
        const QRectF g = QRectF(groove).adjusted(0.5, 0.5, -0.5, -0.5);
        // Round only the start: extend the capsule past the buttons and clip.
        const qreal radius = (vertical ? g.width() : g.height()) / 2;
        QRectF channel = vertical ? g.adjusted(0, 0, 0, radius) : g.adjusted(0, 0, radius, 0);
        if (bar->direction == Qt::RightToLeft && !vertical)
            channel = g.adjusted(-radius, 0, 0, 0);
        p->save();
        p->setClipRect(groove);
        p->setBrush(dark ? across(g, o, {{0, "#18191f"}, {0.2, "#24252c"}, {0.6, "#2f3038"}, {1, "#2a2b33"}})
                         : across(g, o, {{0, "#b4b4b4"}, {0.18, "#d6d6d6"}, {0.55, "#efefef"},
                                         {0.85, "#f8f8f8"}, {1, "#e8e8e8"}}));
        p->setPen(QPen(line, 1));
        p->drawPath(capsule(channel));
        p->restore();
    }

    // The thumb: a glossy blue (or, in a background window, grey) capsule.
    const QRect slider = subControlRect(control, option, SC_ScrollBarSlider, widget);
    if (enabled && !slider.isEmpty()) {
        QRectF t = QRectF(slider).adjusted(1, 1, -1, -1);
        if (vertical)
            t.adjust(0.5, 0, -0.5, 0);
        else
            t.adjust(0, 0.5, 0, -0.5);
        QLinearGradient fill = active
            ? across(t, o, {{0, "#3f75c9"}, {0.1, "#b4d4f8"}, {0.46, "#74adf0"}, {0.5, "#3c86e6"},
                            {0.8, "#57a6f5"}, {1, "#8ccbfc"}})
            : across(t, o, {{0, "#7c828b"}, {0.1, "#e2e5e9"}, {0.46, "#b9bec5"}, {0.5, "#99a0a9"},
                            {0.8, "#adb3bb"}, {1, "#cfd3d8"}});
        if (pressed(SC_ScrollBarSlider)) {
            QGradientStops stops = fill.stops();
            for (QGradientStop &stop : stops)
                stop.second = stop.second.darker(118);
            fill.setStops(stops);
        }
        const QPainterPath thumb = capsule(t);
        p->setPen(QPen(active ? QColor(0x23, 0x4f, 0x9c) : QColor(0x60, 0x66, 0x6f), 1));
        p->setBrush(fill);
        p->drawPath(thumb);
        // The highlight along the lit edge.
        const QRectF shine = vertical ? QRectF(t.left() + 2, t.top() + 2, t.width() * 0.38, t.height() - 4)
                                      : QRectF(t.left() + 2, t.top() + 2, t.width() - 4, t.height() * 0.38);
        QLinearGradient gloss = across(shine, o, {});
        gloss.setColorAt(0, QColor(255, 255, 255, 170));
        gloss.setColorAt(1, QColor(255, 255, 255, 30));
        p->setPen(Qt::NoPen);
        p->setBrush(gloss);
        p->drawPath(capsule(shine));
    }

    // The two arrow buttons, together at the end.
    const struct
    {
        SubControl sub;
        Qt::ArrowType arrow;
    } buttons[] = {
        {SC_ScrollBarSubLine, vertical ? Qt::UpArrow : (bar->direction == Qt::RightToLeft ? Qt::RightArrow : Qt::LeftArrow)},
        {SC_ScrollBarAddLine, vertical ? Qt::DownArrow : (bar->direction == Qt::RightToLeft ? Qt::LeftArrow : Qt::RightArrow)},
    };
    p->setRenderHint(QPainter::Antialiasing, false);
    for (const auto &button : buttons) {
        const QRect b = subControlRect(control, option, button.sub, widget);
        if (b.isEmpty())
            continue;
        const bool down = enabled && pressed(button.sub);
        if (down)
            p->fillRect(b, across(b, o, {{0, "#2f63b8"}, {0.1, "#9cc6f6"}, {0.48, "#5d9ded"}, {0.52, "#2f7be0"},
                                         {1, "#7dbdf9"}}));
        else if (dark)
            p->fillRect(b, across(b, o, {{0, "#202128"}, {0.12, "#4b4c56"}, {0.5, "#3b3c45"}, {0.52, "#32333b"},
                                         {1, "#41424c"}}));
        else
            p->fillRect(b, across(b, o, {{0, "#bdbdbd"}, {0.12, "#fdfdfd"}, {0.5, "#eeeeee"}, {0.52, "#e0e0e0"},
                                         {1, "#f7f7f7"}}));
        // A line before each button divides it from the track or the other one.
        p->setPen(line);
        const bool before = bar->direction != Qt::RightToLeft || vertical;
        if (vertical)
            p->drawLine(b.left(), b.top(), b.right(), b.top());
        else if (before)
            p->drawLine(b.left(), b.top(), b.left(), b.bottom());
        else
            p->drawLine(b.right(), b.top(), b.right(), b.bottom());
        QColor arrow = down ? QColor(Qt::white) : dark ? QColor(0xe6, 0xe6, 0xea) : QColor(0x22, 0x22, 0x22);
        if (!enabled)
            arrow.setAlphaF(0.35);
        p->setRenderHint(QPainter::Antialiasing);
        drawArrow(p, b, button.arrow, arrow);
        p->setRenderHint(QPainter::Antialiasing, false);
    }

    // The edge next to the content.
    p->setPen(dark ? QColor(0x12, 0x13, 0x17) : QColor(0xa8, 0xa8, 0xa8));
    if (!vertical)
        p->drawLine(r.topLeft(), r.topRight());
    else if (bar->direction == Qt::RightToLeft)
        p->drawLine(r.topRight(), r.bottomRight());
    else
        p->drawLine(r.topLeft(), r.bottomLeft());
    p->restore();
}

void ClassicStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    // Repaint when the window comes to the front or goes to the back, for
    // the blue/grey thumb.
    if (qobject_cast<QScrollBar *>(widget))
        widget->installEventFilter(this);
}

void ClassicStyle::unpolish(QWidget *widget)
{
    if (qobject_cast<QScrollBar *>(widget))
        widget->removeEventFilter(this);
    QProxyStyle::unpolish(widget);
}

bool ClassicStyle::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ActivationChange)
        static_cast<QWidget *>(watched)->update();
    return QProxyStyle::eventFilter(watched, event);
}
