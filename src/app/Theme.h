#pragma once
// Canvas color theme (Light / Dark / follow the OS) and the window chrome's
// color scheme. One global instance; views and sessions re-read it when it
// emits changed().
#include <QColor>
#include <QObject>

struct CanvasColors {
    QColor background;
    QColor gridDot;
    QColor selection;      // marquee and selection outline
    QColor partStroke;     // logic symbol outline (Wokwi magenta in light mode)
    QColor partFill;
    QColor partText;
    QColor lead;           // pin leads, black in light mode

    // Wire colors are drawn exactly as stored in the file, as Wokwi does.
    QColor displayWireColor(const QColor& fileColor) const;
    bool dark = false;
};

class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Light, Dark };
    Q_ENUM(Mode)

    static Theme& instance();

    Mode mode() const { return m_mode; }
    void setMode(Mode m);              // persists to QSettings
    void setModeForSession(Mode m);    // this run only (command line)
    bool isDark() const;               // resolved: System follows the OS
    const CanvasColors& canvas() const { return isDark() ? m_dark : m_light; }

    static QString modeName(Mode m);
    static bool parseMode(const QString& s, Mode* out);

signals:
    void changed();

private:
    Theme();
    void applyToApplication();

    Mode m_mode = Mode::System;
    CanvasColors m_light;
    CanvasColors m_dark;
};
