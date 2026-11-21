#include "ToolBarIcons.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <algorithm>
#include <cmath>

namespace eld::ToolBarIcons {

namespace {

// The picture part of an icon frame: the caption is drawn below row 40 of the 48 pixel frames,
// row 58 of the 68 pixel and row 112 of the 128 pixel ones (the drop-down overlays end there too).
QSize framePicture(QSize frame, bool cropCaption) {
    if (!cropCaption) return frame;
    if (frame.width() >= 128) return {frame.width(), 112};
    if (frame.width() >= 68) return {frame.width(), 58};
    if (frame.width() >= 48) return {frame.width(), 40};
    return frame;
}

QList<QImage> frames(const QString& file) {
    QList<QImage> out;
    QImageReader r(file);
    do {
        QImage i = r.read();
        if (!i.isNull()) out << i.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    } while (r.jumpToNextImage());
    std::sort(out.begin(), out.end(), [](const QImage& a, const QImage& b) { return a.width() < b.width(); });
    return out;
}

// The display scalings in use: 1 and every screen's.
QList<qreal> scalings() {
    QList<qreal> out{1.0};
    for (QScreen* s : QGuiApplication::screens())
        if (!out.contains(s->devicePixelRatio())) out << s->devicePixelRatio();
    if (qApp && !out.contains(qApp->devicePixelRatio())) out << qApp->devicePixelRatio();
    return out;
}

} // namespace

QSize pictureSize(QSize iconSize) {
    return framePicture(iconSize, true);
}

QIcon make(const QString& file, const QString& overlayFile, QSize picture, bool cropCaption) {
    const QList<QImage> pics = frames(file);
    if (pics.isEmpty()) return {};
    const QList<QImage> overlays = overlayFile.isEmpty() ? QList<QImage>() : frames(overlayFile);
    QIcon icon;
    for (qreal dpr : scalings()) {
        QSize phys(qRound(picture.width() * dpr), qRound(picture.height() * dpr));
        // The smallest frame whose picture is at least as big; else the biggest.
        QImage src = pics.last();
        for (const QImage& f : pics)
            if (framePicture(f.size(), cropCaption).width() >= phys.width()) { src = f; break; }
        QImage img = src.copy(QRect(QPoint(0, 0), framePicture(src.size(), cropCaption)));
        for (const QImage& o : overlays) // the drop-down frame, from the same frame size
            if (o.size() == src.size()) {
                QPainter(&img).drawImage(0, 0, o.copy(img.rect()));
                break;
            }
        QImage out(phys, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QImage s = img.size() == phys ? img : img.scaled(phys, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter(&out).drawImage((phys.width() - s.width()) / 2, (phys.height() - s.height()) / 2, s);
        QPixmap px = QPixmap::fromImage(out);
        px.setDevicePixelRatio(dpr);
        icon.addPixmap(px, QIcon::Normal);
        icon.addPixmap(px, QIcon::Disabled); // buttons not ported yet keep their look
    }
    return icon;
}

QString caption(const QString& fileName) {
    static const QHash<QString, const char*> captions = {
        {"EmuLoader_Orb.ico", QT_TRANSLATE_NOOP("ToolBar", "Menu")},
        {"information.ico", QT_TRANSLATE_NOOP("ToolBar", "Guide")},
        {"error.ico", QT_TRANSLATE_NOOP("ToolBar", "MAME Problems")}, // (msgbox_icons: the MAME Problems button)
        {"viewmode_01_details.ico", QT_TRANSLATE_NOOP("ToolBar", "Details")},
        {"viewmode_02_grouped.ico", QT_TRANSLATE_NOOP("ToolBar", "Grouped")},
        {"viewmode_03_small.ico", QT_TRANSLATE_NOOP("ToolBar", "Small Icons")},
        {"viewmode_04_large.ico", QT_TRANSLATE_NOOP("ToolBar", "Large Icons")},
        {"viewmode_05_tile.ico", QT_TRANSLATE_NOOP("ToolBar", "Tiles")},
        {"viewmode_06_thumbnail.ico", QT_TRANSLATE_NOOP("ToolBar", "Thumbnails")},
        {"filter5_machines.ico", QT_TRANSLATE_NOOP("ToolBar", "Machines")},
        {"filter_systems_quick_disabled.ico", QT_TRANSLATE_NOOP("ToolBar", "Systems")},
        {"filter_systems_quick_enabled.ico", QT_TRANSLATE_NOOP("ToolBar", "Systems")},
        {"favorites.ico", QT_TRANSLATE_NOOP("ToolBar", "Favorites")},
        {"allgames.ico", QT_TRANSLATE_NOOP("ToolBar", "All Games")},
        {"filter2_01_both.ico", QT_TRANSLATE_NOOP("ToolBar", "Parents + Clones")},
        {"filter2_02_parent.ico", QT_TRANSLATE_NOOP("ToolBar", "Parents")},
        {"filter2_03_clone.ico", QT_TRANSLATE_NOOP("ToolBar", "Clones")},
        {"filter3_01_listall.ico", QT_TRANSLATE_NOOP("ToolBar", "Have + Missing")},
        {"filter3_02_have.ico", QT_TRANSLATE_NOOP("ToolBar", "Available")},
        {"filter3_03_miss.ico", QT_TRANSLATE_NOOP("ToolBar", "Missing")},
        {"filter3_04_missroms.ico", QT_TRANSLATE_NOOP("ToolBar", "Missing ROMs")},
        {"filter3_05_have_and_missroms.ico", QT_TRANSLATE_NOOP("ToolBar", "Available + Missing ROMs")},
        {"filter3_06_miss_and_missroms.ico", QT_TRANSLATE_NOOP("ToolBar", "Missing + Missing ROMs")},
        {"filter4_01_all.ico", QT_TRANSLATE_NOOP("ToolBar", "All Drivers")},
        {"filter4_02_good-imperfect.ico", QT_TRANSLATE_NOOP("ToolBar", "Good + Imperfect")},
        {"filter4_03_good.ico", QT_TRANSLATE_NOOP("ToolBar", "Good")},
        {"filter4_04_imperfect.ico", QT_TRANSLATE_NOOP("ToolBar", "Imperfect")},
        {"filter4_05_preliminary.ico", QT_TRANSLATE_NOOP("ToolBar", "Preliminary")},
        {"mamu_.ico", QT_TRANSLATE_NOOP("ToolBar", "MAMu_")},
        {"filter_extra.ico", QT_TRANSLATE_NOOP("ToolBar", "Misc")},
        {"cdimage_cue_enabled.ico", QT_TRANSLATE_NOOP("ToolBar", "Use .cue")},
        {"cdimage_cue_disabled.ico", QT_TRANSLATE_NOOP("ToolBar", "Use .cue")},
        {"emu_useparameter.ico", QT_TRANSLATE_NOOP("ToolBar", "Parameters")},
        {"games_mru.ico", QT_TRANSLATE_NOOP("ToolBar", "Last Played")},
        {"emulators.ico", QT_TRANSLATE_NOOP("ToolBar", "Emulators")},
        {"filter_search_text.ico", QT_TRANSLATE_NOOP("ToolBar", "Search")},
        {"hide_games.ico", QT_TRANSLATE_NOOP("ToolBar", "Hide Games")},
        // Games Filters - MAME and Arcade (resources\main_icons\arcade_filters\): the original's short captions
        {"arcadia.ico", QT_TRANSLATE_NOOP("ToolBar", "Arcadia")},
        {"atari.ico", QT_TRANSLATE_NOOP("ToolBar", "Atari")},
        {"atari_90s.ico", QT_TRANSLATE_NOOP("ToolBar", "Atari 90s")},
        {"atari_mix1.ico", QT_TRANSLATE_NOOP("ToolBar", "Atari Mix")},
        {"atarisy1.ico", QT_TRANSLATE_NOOP("ToolBar", "Atari Sys 1")},
        {"atarisy2.ico", QT_TRANSLATE_NOOP("ToolBar", "Atari Sys 2")},
        {"awbios.ico", QT_TRANSLATE_NOOP("ToolBar", "Atomiswave")},
        {"biossets.ico", QT_TRANSLATE_NOOP("ToolBar", "BIOS Sets")},
        {"capcom.ico", QT_TRANSLATE_NOOP("ToolBar", "Capcom")},
        {"capcom_80s.ico", QT_TRANSLATE_NOOP("ToolBar", "Capcom 80s")},
        {"chd.ico", QT_TRANSLATE_NOOP("ToolBar", "CHD")},
        {"chihiro.ico", QT_TRANSLATE_NOOP("ToolBar", "Chihiro")},
        {"cps1.ico", QT_TRANSLATE_NOOP("ToolBar", "CPS-1")},
        {"cps2.ico", QT_TRANSLATE_NOOP("ToolBar", "CPS-2")},
        {"cps3.ico", QT_TRANSLATE_NOOP("ToolBar", "CPS-3")},
        {"cpzn1.ico", QT_TRANSLATE_NOOP("ToolBar", "ZN-1")},
        {"cpzn2.ico", QT_TRANSLATE_NOOP("ToolBar", "ZN-2")},
        {"decocass.ico", QT_TRANSLATE_NOOP("ToolBar", "DECO Tape")},
        {"devicesets.ico", QT_TRANSLATE_NOOP("ToolBar", "Devices")},
        {"globalvr.ico", QT_TRANSLATE_NOOP("ToolBar", "Global VR")},
        {"hornet_gticlub.ico", QT_TRANSLATE_NOOP("ToolBar", "Hornet/GTI")},
        {"irem.ico", QT_TRANSLATE_NOOP("ToolBar", "Irem Misc")},
        {"konami.ico", QT_TRANSLATE_NOOP("ToolBar", "Konami")},
        {"konami_80s.ico", QT_TRANSLATE_NOOP("ToolBar", "Konami 80s")},
        {"konami_90s.ico", QT_TRANSLATE_NOOP("ToolBar", "Konami 90s")},
        {"konamigq.ico", QT_TRANSLATE_NOOP("ToolBar", "Konami GQ")},
        {"konamigv.ico", QT_TRANSLATE_NOOP("ToolBar", "Konami GV")},
        {"konamigx.ico", QT_TRANSLATE_NOOP("ToolBar", "Konami GX")},
        {"ksys573.ico", QT_TRANSLATE_NOOP("ToolBar", "Konami 573")},
        {"megaplay.ico", QT_TRANSLATE_NOOP("ToolBar", "Mega Play")},
        {"megatech.ico", QT_TRANSLATE_NOOP("ToolBar", "Mega-Tech")},
        {"midway.ico", QT_TRANSLATE_NOOP("ToolBar", "Midway")},
        {"model1.ico", QT_TRANSLATE_NOOP("ToolBar", "Model 1")},
        {"model2.ico", QT_TRANSLATE_NOOP("ToolBar", "Model 2")},
        {"model3.ico", QT_TRANSLATE_NOOP("ToolBar", "Model 3")},
        {"namco.ico", QT_TRANSLATE_NOOP("ToolBar", "Namco")},
        {"namco_90s.ico", QT_TRANSLATE_NOOP("ToolBar", "Namco 90s")},
        {"namco_mix1.ico", QT_TRANSLATE_NOOP("ToolBar", "Namco Mix")},
        {"namcops2.ico", QT_TRANSLATE_NOOP("ToolBar", "Sys 246/256")},
        {"namcos1.ico", QT_TRANSLATE_NOOP("ToolBar", "System 1")},
        {"namcos2.ico", QT_TRANSLATE_NOOP("ToolBar", "System 2")},
        {"namcos11.ico", QT_TRANSLATE_NOOP("ToolBar", "Sys 10/11")},
        {"namcos12.ico", QT_TRANSLATE_NOOP("ToolBar", "System 12")},
        {"namcos21.ico", QT_TRANSLATE_NOOP("ToolBar", "System 21")},
        {"namcos22.ico", QT_TRANSLATE_NOOP("ToolBar", "Sys 22/23")},
        {"naomi.ico", QT_TRANSLATE_NOOP("ToolBar", "Naomi")},
        {"naomi2.ico", QT_TRANSLATE_NOOP("ToolBar", "Naomi 2")},
        {"naomi2gd.ico", QT_TRANSLATE_NOOP("ToolBar", "Naomi 2 GD")},
        {"naomigd.ico", QT_TRANSLATE_NOOP("ToolBar", "Naomi GD")},
        {"neogeo.ico", QT_TRANSLATE_NOOP("ToolBar", "Neo-Geo")},
        {"nintendo.ico", QT_TRANSLATE_NOOP("ToolBar", "Nintendo")},
        {"nintendo_nostalgia.ico", QT_TRANSLATE_NOOP("ToolBar", "Nostalgia")},
        {"nss.ico", QT_TRANSLATE_NOOP("ToolBar", "Super System")},
        {"pgm.ico", QT_TRANSLATE_NOOP("ToolBar", "PGM")},
        {"playch10.ico", QT_TRANSLATE_NOOP("ToolBar", "PlayChoice-10")},
        {"seattle.ico", QT_TRANSLATE_NOOP("ToolBar", "Seattle")},
        {"sega.ico", QT_TRANSLATE_NOOP("ToolBar", "Sega")},
        {"segac2.ico", QT_TRANSLATE_NOOP("ToolBar", "Sega C/C2")},
        {"segaxybd.ico", QT_TRANSLATE_NOOP("ToolBar", "Sega X/Y")},
        {"simpl156.ico", QT_TRANSLATE_NOOP("ToolBar", "Simple 156")},
        {"ssv.ico", QT_TRANSLATE_NOOP("ToolBar", "SSV")},
        {"stv.ico", QT_TRANSLATE_NOOP("ToolBar", "ST-V")},
        {"system1.ico", QT_TRANSLATE_NOOP("ToolBar", "Sega 1/2")},
        {"system16.ico", QT_TRANSLATE_NOOP("ToolBar", "Sega 16")},
        {"system18.ico", QT_TRANSLATE_NOOP("ToolBar", "Sega 18")},
        {"system24.ico", QT_TRANSLATE_NOOP("ToolBar", "Sega 24")},
        {"system32.ico", QT_TRANSLATE_NOOP("ToolBar", "Sega 32")},
        {"taito.ico", QT_TRANSLATE_NOOP("ToolBar", "Taito")},
        {"taito_f2.ico", QT_TRANSLATE_NOOP("ToolBar", "Taito F2")},
        {"taito_f3.ico", QT_TRANSLATE_NOOP("ToolBar", "Taito F3")},
        {"taito_fx1.ico", QT_TRANSLATE_NOOP("ToolBar", "Taito FX-1")},
        {"taito_misc.ico", QT_TRANSLATE_NOOP("ToolBar", "Taito Misc")},
        {"taito_mix1.ico", QT_TRANSLATE_NOOP("ToolBar", "Taito Mix 1")},
        {"taito_mix2.ico", QT_TRANSLATE_NOOP("ToolBar", "Taito Mix 2")},
        {"taitogn.ico", QT_TRANSLATE_NOOP("ToolBar", "G-NET")},
        {"taitotx.ico", QT_TRANSLATE_NOOP("ToolBar", "Type X")},
        {"triforce.ico", QT_TRANSLATE_NOOP("ToolBar", "Triforce")},
        {"vector.ico", QT_TRANSLATE_NOOP("ToolBar", "Vector")},
        {"vegas.ico", QT_TRANSLATE_NOOP("ToolBar", "Vegas")},
        {"viper.ico", QT_TRANSLATE_NOOP("ToolBar", "Viper")},
        {"vsnes.ico", QT_TRANSLATE_NOOP("ToolBar", "VS System")},
        {"no_icon.ico", ""},
    };
    QString name = QFileInfo(fileName).fileName();
    auto it = captions.constFind(name);
    if (it == captions.cend()) return QFileInfo(fileName).completeBaseName(); // a new icon: its file name
    return **it ? QCoreApplication::translate("ToolBar", *it) : QStringLiteral("");
}

int captionPixelSize(QSize iconSize) {
    return iconSize.width() >= 68 ? 12 : iconSize.width() >= 48 ? 11 : 10;
}

} // namespace eld::ToolBarIcons
