#include "Theme.h"

#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>
#include <QToolTip>

namespace {
constexpr char kSettingsKey[] = "appearance/theme";
constexpr char kHoverSizeKey[] = "appearance/hoverTextPoints";
}

QColor CanvasColors::displayWireColor(const QColor& c) const
{
    // Wokwi draws wire colors exactly as named, in both themes (measured from
    // a dark-mode Wokwi screenshot: green stays #008000, black stays black).
    return c;
}

Theme& Theme::instance()
{
    static Theme t;
    return t;
}

Theme::Theme()
{
    m_light.background = QColor(0xff, 0xff, 0xff);
    m_light.gridDot = QColor(0xd0, 0xd0, 0xd0);
    m_light.selection = QColor(0x21, 0x96, 0xf3);
    m_light.partStroke = QColor(0xb4, 0x00, 0xb4);
    m_light.partFill = QColor(0xf3, 0xe5, 0xf5);
    m_light.partText = QColor(0x21, 0x21, 0x21);
    m_light.lead = QColor(0x00, 0x00, 0x00);
    m_light.dark = false;

    // Wokwi's dark palette, measured from a dark-mode Wokwi screenshot.
    m_dark.background = QColor(0x33, 0x33, 0x33);
    m_dark.gridDot = QColor(0x4d, 0x4d, 0x4d);
    m_dark.selection = QColor(0x4f, 0xb3, 0xff);
    m_dark.partStroke = QColor(0xd4, 0x78, 0xe2);
    m_dark.partFill = QColor(0x3d, 0x33, 0x40);
    m_dark.partText = QColor(0xcc, 0xcc, 0xcc);
    m_dark.lead = QColor(0xaa, 0xaa, 0xaa);
    m_dark.dark = true;

    QSettings s;
    Mode m = Mode::System;
    parseMode(s.value(kSettingsKey, "system").toString(), &m);
    m_mode = m;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_mode == Mode::System)
            emit changed();
    });
#endif
    m_hoverTextSize = s.value(kHoverSizeKey, 18).toInt();
    applyToApplication();
    applyHoverTextSize();
}

void Theme::setHoverTextSize(int points)
{
    m_hoverTextSize = points;
    QSettings().setValue(kHoverSizeKey, points);
    applyHoverTextSize();
}

void Theme::applyHoverTextSize()
{
    QFont f = QToolTip::font();
    f.setPointSize(m_hoverTextSize);
    QToolTip::setFont(f);
}

void Theme::setMode(Mode m)
{
    if (m == m_mode)
        return;
    m_mode = m;
    QSettings().setValue(kSettingsKey, modeName(m).toLower());
    applyToApplication();
    emit changed();
}

void Theme::setModeForSession(Mode m)
{
    m_mode = m;
    applyToApplication();
    emit changed();
}

bool Theme::isDark() const
{
    switch (m_mode) {
    case Mode::Light: return false;
    case Mode::Dark: return true;
    case Mode::System: break;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
#endif
}

void Theme::applyToApplication()
{
    // Window chrome, menus, docks and dialogs follow the same choice
    // (needs Qt 6.8; older Qt keeps the OS appearance for the chrome).
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    Qt::ColorScheme scheme = Qt::ColorScheme::Unknown; // Unknown = follow the OS
    if (m_mode == Mode::Light)
        scheme = Qt::ColorScheme::Light;
    else if (m_mode == Mode::Dark)
        scheme = Qt::ColorScheme::Dark;
    QGuiApplication::styleHints()->setColorScheme(scheme);
#endif
}

QString Theme::modeName(Mode m)
{
    switch (m) {
    case Mode::Light: return QStringLiteral("Light");
    case Mode::Dark: return QStringLiteral("Dark");
    case Mode::System: break;
    }
    return QStringLiteral("System");
}

bool Theme::parseMode(const QString& s, Mode* out)
{
    const QString l = s.trimmed().toLower();
    if (l == "light") { *out = Mode::Light; return true; }
    if (l == "dark") { *out = Mode::Dark; return true; }
    if (l == "system" || l == "auto") { *out = Mode::System; return true; }
    return false;
}
