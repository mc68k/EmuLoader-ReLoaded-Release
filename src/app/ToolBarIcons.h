// ToolBarIcons - the main tool bar's pictures and captions.
//
// The original's tool bar icons (resources\main_icons\toolbar_filters\*.ico and others) have
// their caption drawn into the picture, in English. Here the caption strip is cut off and the
// caption is shown as text under the picture instead (ElButton::setCaption), so it can be
// translated and stays sharp at any display scaling. Each picture is taken from the icon file's
// best frame for the screen's scaling (the 128x128 frame at 200%), with the drop-down overlay
// drawn on it as before.
#pragma once

#include <QIcon>
#include <QSize>
#include <QString>

namespace eld::ToolBarIcons {

// The picture size for a tool bar icon size: 68x68 -> 68x58, 48x48 -> 48x40 (without the
// caption strip), 30x24 -> 30x24 (no caption in that size).
QSize pictureSize(QSize iconSize);

// The picture of an icon file (without its caption strip when cropCaption), with the overlay file
// (or none) drawn on top, for every display scaling in use.
QIcon make(const QString& file, const QString& overlayFile, QSize pictureSize, bool cropCaption);

// The caption for an icon file name ("favorites.ico" -> "Favorites"), translated; an empty
// string for icons that have none (the orb).
QString caption(const QString& fileName);

// The caption's font size in pixels for a tool bar icon size.
int captionPixelSize(QSize iconSize);

} // namespace eld::ToolBarIcons
