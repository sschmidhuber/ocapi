#include "ui/theme.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyle>
#include <QStyleHints>
#include <QSvgRenderer>

namespace okapi {

namespace {

// Dark palette roughly following Breeze Dark.
[[nodiscard]] QPalette darkPalette() {
    QPalette p;
    p.setColor(QPalette::Window, QColor(37, 38, 41));
    p.setColor(QPalette::WindowText, QColor(240, 240, 240));
    p.setColor(QPalette::Base, QColor(30, 30, 30));
    p.setColor(QPalette::AlternateBase, QColor(42, 42, 45));
    p.setColor(QPalette::ToolTipBase, QColor(30, 30, 30));
    p.setColor(QPalette::ToolTipText, QColor(240, 240, 240));
    p.setColor(QPalette::Text, QColor(240, 240, 240));
    p.setColor(QPalette::Button, QColor(45, 45, 49));
    p.setColor(QPalette::ButtonText, QColor(240, 240, 240));
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(106, 169, 255));
    p.setColor(QPalette::Highlight, QColor(42, 130, 218));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Mid, QColor(77, 77, 77));
    // Frame/separator roles: left at their defaults these stay (near-)white
    // and show up as light lines around toolbars etc. in dark mode.
    p.setColor(QPalette::Light, QColor(72, 72, 78));
    p.setColor(QPalette::Midlight, QColor(58, 58, 63));
    p.setColor(QPalette::Dark, QColor(18, 18, 20));
    p.setColor(QPalette::Shadow, Qt::black);
    const QColor disabled(128, 128, 128);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Link, QColor(90, 130, 180));
    return p;
}

} // namespace

bool systemPrefersDark() {
    return QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QIcon themedIcon(const QString &resourcePath) {
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return QIcon{resourcePath};
    QByteArray svg = file.readAll();
    file.close();
    // The bundled symbolic icons use a fixed dark color (#232629); recolor
    // to the palette text color so they remain visible in dark mode.
    if (svg.contains("#232629"))
        svg.replace("#232629", qApp->palette().color(QPalette::Text).name().toUtf8());
    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
        return QIcon{resourcePath};
    QPixmap pix(renderer.defaultSize());
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    renderer.render(&painter);
    return QIcon{pix};
}

bool applyTheme(UiSettings::Theme theme) {
    const bool dark = theme == UiSettings::Theme::Dark ||
                      (theme == UiSettings::Theme::Auto && systemPrefersDark());
    if (dark)
        qApp->setPalette(darkPalette());
    else
        qApp->setPalette(qApp->style()->standardPalette());
    // Consistent slim borders app-wide: no toolbar border lines at all,
    // splitter handles and separators thin and in the theme's mid color.
    // palette() references re-evaluate automatically on palette changes.
    qApp->setStyleSheet(QStringLiteral(
        "QToolBar { border: none; }"
        "QMainWindow::separator { width: 1px; height: 1px; background: palette(mid); }"
        "QSplitter::handle { background: palette(mid); }"
        "QSplitter::handle:horizontal { width: 3px; }"
        "QSplitter::handle:vertical { height: 3px; }"));
    return dark;
}

} // namespace okapi
