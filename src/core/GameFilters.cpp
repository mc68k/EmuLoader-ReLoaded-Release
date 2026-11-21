#include "GameFilters.h"

#include "MultiSlot.h"
#include "TextFile.h"
#include "WinIni.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>

namespace eld {

namespace {
const char* const kCategoryKeys[] = {
    "CategoryCasino", "CategoryFruitMachines", "CategorySlotMachines", "CategoryRhythm", "CategoryMature",
    "CategoryMahjong", "CategoryTabletop", "CategoryPinMAME", "CategoryBoardGame", "CategoryQuiz",
    "CategoryUtilities", "CategoryCalculator", "CategoryEducational", "CategoryElectronic", "CategoryPrinters",
    "CategoryPhones", "CategoryMusic", "CategoryMAMEConsoleMachines", "CategoryMAMEComputerMachines",
    "CategoryMAMEHandheldMachines"};
// VerifyCategoryFilter: the words looked for in the game's category (lower case), per check box.
const QStringList kCategoryWords[] = {
    {"casino"}, {"reel", "fruit"}, {"slot machine", "video slot"}, {"rhythm"}, {"mature", "adult"}, {"mahjong"},
    {"tabletop"}, {"mechanic"}, {"board game"}, {"quiz"}, {"utilities"}, {"calculator"}, {"education"},
    {"electronic"}, {"printer"}, {"phone"}, {"music"}, {"console"}, {"computer"}, {"handheld"}};
const char* const kSection = "Miscellaneous";
} // namespace

MiscFilters::MiscFilters() {
    for (bool& b : show) b = true;
}

const char* MiscFilters::categoryKey(int c) { return kCategoryKeys[c]; }

bool MiscFilters::operator==(const MiscFilters& o) const {
    if (bios != o.bios || deviceRoms != o.deviceRoms || gameRoms != o.gameRoms || audio != o.audio ||
        interface != o.interface || orientation != o.orientation || saveState != o.saveState ||
        specialList != o.specialList || cocktail != o.cocktail || artwork != o.artwork || control != o.control ||
        hideBiosSets != o.hideBiosSets || hideDeviceSets != o.hideDeviceSets || hideChd != o.hideChd ||
        hideNoDump != o.hideNoDump || neoGeoMvs != o.neoGeoMvs || stvMultiSlot != o.stvMultiSlot ||
        mergedOnly != o.mergedOnly || crcCollisionOnly != o.crcCollisionOnly || hideVgmplay != o.hideVgmplay ||
        mainCpu != o.mainCpu)
        return false;
    for (int c = 0; c < CategoryCount; ++c)
        if (show[c] != o.show[c]) return false;
    return true;
}

bool MiscFilters::isDefault() const { return *this == MiscFilters{}; }

// ReadArcadeMiscFiltersSettings
MiscFilters MiscFilters::load(const QString& file) {
    MiscFilters f;
    WinIni ini;
    if (!ini.load(file)) return f;
    f.bios = ini.readInteger(kSection, "BiosNoBios", 0);
    f.hideBiosSets = ini.readInteger(kSection, "HideBiosSets", 0) == 1;
    f.interface = ini.readInteger(kSection, "InterfaceType", 0);
    f.audio = ini.readInteger(kSection, "AudioType", 0);
    f.deviceRoms = ini.readInteger(kSection, "DevicesNoDevices", 0);
    f.hideDeviceSets = ini.readInteger(kSection, "HideDeviceSets", 0) == 1;
    f.gameRoms = ini.readInteger(kSection, "GamesROMs", 0);
    for (int c = 0; c < CategoryCount; ++c) f.show[c] = ini.readInteger(kSection, kCategoryKeys[c], 1) == 1;
    f.hideChd = ini.readInteger(kSection, "HideGamesWithCHDFiles", 0) == 1;
    f.hideNoDump = ini.readInteger(kSection, "HideNoDumpROMsGames", 0) == 1;
    f.orientation = ini.readInteger(kSection, "ScreenOrientation", 0);
    f.saveState = ini.readInteger(kSection, "SaveState", 0);
    f.cocktail = ini.readInteger(kSection, "Cocktail", 0);
    f.artwork = ini.readInteger(kSection, "ArtworkRequired", 0);
    f.neoGeoMvs = ini.readInteger(kSection, "OnlyNeoGeoMVS", 0) == 1;
    f.stvMultiSlot = ini.readInteger(kSection, "OnlySTVMultiSlot", 0) == 1;
    f.mergedOnly = ini.readInteger(kSection, "ShowMergedSetsOnly", 0) == 1;
    f.crcCollisionOnly = ini.readInteger(kSection, "ShowOnlySetsCRC32Collision", 0) == 1;
    f.hideVgmplay = ini.readInteger(kSection, "HideMAMESoftlist_vgmplay", 0) == 1;
    f.specialList = ini.readInteger(kSection, "SpecialList", 0);
    f.mainCpu = ini.readInteger(kSection, "FilterMAMEGamesMainCPU", 0) == 1;
    f.control = ini.readString(kSection, "ControlType"); // this version's: the original keeps it until it closes
    return f;
}

// WriteArcadeMiscFiltersSettings
bool MiscFilters::save(const QString& file) const {
    WinIni ini;
    ini.load(file);
    ini.writeInteger(kSection, "BiosNoBios", bios);
    ini.writeInteger(kSection, "HideBiosSets", hideBiosSets);
    ini.writeInteger(kSection, "InterfaceType", interface);
    ini.writeInteger(kSection, "AudioType", audio);
    ini.writeInteger(kSection, "DevicesNoDevices", deviceRoms);
    ini.writeInteger(kSection, "HideDeviceSets", hideDeviceSets);
    ini.writeInteger(kSection, "GamesROMs", gameRoms);
    for (int c = 0; c < CategoryCount; ++c) ini.writeInteger(kSection, kCategoryKeys[c], show[c]);
    ini.writeInteger(kSection, "HideGamesWithCHDFiles", hideChd);
    ini.writeInteger(kSection, "HideNoDumpROMsGames", hideNoDump);
    ini.writeInteger(kSection, "ScreenOrientation", orientation);
    ini.writeInteger(kSection, "SaveState", saveState);
    ini.writeInteger(kSection, "Cocktail", cocktail);
    ini.writeInteger(kSection, "ArtworkRequired", artwork);
    ini.writeInteger(kSection, "OnlyNeoGeoMVS", neoGeoMvs);
    ini.writeInteger(kSection, "OnlySTVMultiSlot", stvMultiSlot);
    ini.writeInteger(kSection, "ShowMergedSetsOnly", mergedOnly);
    ini.writeInteger(kSection, "ShowOnlySetsCRC32Collision", crcCollisionOnly);
    ini.writeInteger(kSection, "HideMAMESoftlist_vgmplay", hideVgmplay);
    ini.writeInteger(kSection, "SpecialList", specialList);
    ini.writeInteger(kSection, "FilterMAMEGamesMainCPU", mainCpu);
    if (control.isEmpty()) ini.deleteKey(kSection, "ControlType");
    else ini.writeString(kSection, "ControlType", control);
    QDir().mkpath(QFileInfo(file).absolutePath());
    return ini.save(file);
}

MiscFilters::MultiSlot MiscFilters::loadMultiSlot(const QString& neoGeoFile, const QString& stvFile) {
    return {eld::MultiSlot::loadCartList(neoGeoFile), eld::MultiSlot::loadCartList(stvFile)};
}

// ApplyMiscFilters (and the control type of ApplyFilters, CheckGameControlType)
bool MiscFilters::accept(const MameGame& g, const MultiSlot& multiSlot, bool hbmame) const {
    Q_UNUSED(hbmame);
    switch (orientation) {
    case 1: if (g.orientation != 0) return false; break;
    case 2: if (g.orientation != 1) return false; break;
    case 3: if (g.orientation != -1) return false; break;
    }
    if (cocktail == 1 && g.cocktailStatus == -1) return false;
    if (cocktail == 2 && g.cocktailStatus != -1) return false;
    if (artwork == 1 && !g.requiresArtwork) return false;
    if (artwork == 2 && g.requiresArtwork) return false;
    if (hideChd && g.mediaType == 1) return false;
    if (hideVgmplay && g.softwareList == "vgmplay") return false; // the music player's software list
    // The ROM filters need the list's ROM flags (a list made by this version); without them they
    // keep every game.
    const bool roms = g.romFlag(MameGame::RomsKnown);
    if (hideNoDump && roms && g.romFlag(MameGame::AllNoDump)) return false;
    if (mergedOnly && !g.merged) return false;
    if (crcCollisionOnly && roms && !g.romFlag(MameGame::CrcCollision)) return false;
    if (hideDeviceSets && g.kind == SetKind::Device) return false;
    if (hideBiosSets && g.kind == SetKind::Bios) return false;
    if (bios == 1 && g.bios.isEmpty() && g.kind != SetKind::Bios) return false;
    if (bios == 2 && !g.bios.isEmpty()) return false;
    if (roms) {
        bool dev = g.romFlag(MameGame::DeviceRoms);
        if (deviceRoms == 1 && !dev && g.kind != SetKind::Device) return false;
        if (deviceRoms == 2 && dev) return false;
        bool any = g.romFlag(MameGame::AnyRoms);
        switch (gameRoms) {
        case 1: if (!any) return false; break;
        case 2: if (any) return false; break;
        case 3: // no main set: ROMs, but only the BIOS's or devices' (not BIOS or device sets)
            if (!any || g.romFlag(MameGame::GameRoms) || g.kind == SetKind::Bios || g.kind == SetKind::Device) return false;
            break;
        }
    }
    switch (saveState) {
    case 1: if (g.saveState != 1) return false; break;
    case 2: if (g.saveState != 0) return false; break;
    case 3: if (g.saveState != -1) return false; break;
    }
    const QString category = g.category.toLower();
    switch (interface) {
    case 1: if (g.mechanical || category.contains("mechanic")) return false; break;
    case 2: if (!g.mechanical) return false; break;
    }
    switch (audio) {
    case 1: if (g.audioType != 0) return false; break;
    case 2: if (g.audioType != 1) return false; break;
    case 3: if (g.audioType != 2) return false; break;
    }
    const QString drv = ArcadeFilter::driverBase(g.driver);
    if (neoGeoMvs && !multiSlot.neoGeo.isEmpty() && (g.bios.compare("neogeo", Qt::CaseInsensitive) == 0 || drv == "neopcb") &&
        !multiSlot.neoGeo.contains(g.name))
        return false;
    if (stvMultiSlot && !multiSlot.stv.isEmpty() && drv == "stv" && !multiSlot.stv.contains(g.name)) return false;
    if (!category.isEmpty() && !g.isSoftware()) // catver.ini's categories (a software list game's is its list)
        for (int c = 0; c < CategoryCount; ++c)
            if (!show[c])
                for (const QString& w : kCategoryWords[c])
                    if (category.contains(w)) return false;
    if (!control.isEmpty()) {
        if (control.compare("none", Qt::CaseInsensitive) == 0) {
            if (!g.controls.isEmpty() || g.buttons > 0) return false;
        } else if (!g.controls.contains(control)) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- Games Filters

namespace ArcadeFilter {

QString driverBase(const QString& driver) {
    QString d = driver;
    int slash = std::max(d.lastIndexOf('/'), d.lastIndexOf('\\'));
    if (slash >= 0) d = d.mid(slash + 1);
    int dot = d.lastIndexOf('.');
    return dot > 0 ? d.left(dot) : d;
}

namespace {
struct Rule { QStringList drivers, bioses; };
// FilterGamesList: the drivers (file names without folder and extension) and BIOS sets of each
// filter. The original compares the driver name with its folder ("capcom/cps1"), so these filters
// found nothing with MAME 0.2xx; here the folder is left out.
const QHash<QString, Rule>& rules() {
    static const QStringList capcom80s = {"gng", "gunsmoke", "higemaru", "sonson", "commando", "tigeroad"};
    static const QStringList zn1 = {"coh1000c", "cpzn1", "coh1000a", "acpsx", "coh1000w", "atpsx", "coh1001l", "atluspsx",
                                    "coh1002e", "psarc95", "coh1002m", "tps", "coh1002v", "vspsx"};
    static const QStringList konami80s = {"trackfld", "gyruss", "hyperspt", "yiear", "pingpong", "gberet", "hcastle", "contra",
                                          "thunderx", "88games", "nemesis", "twin16", "gradius3", "pooyan", "timeplt",
                                          "tp84", "circusc"};
    static const QStringList konami90s = {"tmnt", "simpsons", "moo", "parodius", "xexex", "asterix"};
    static const QStringList taitoMisc = {"taito_b", "taito_l", "taito_h", "taito_x", "taito_z", "taitojc", "taitosj"};
    static const QStringList taitoMix1 = {"halleys", "tsamurai", "fastfred", "superqix", "flstory", "slapfght", "tnzs",
                                          "arkanoid", "bublbobl", "rainbow", "lkage"};
    static const QStringList namco90s = {"namcona1", "namconb1", "namcond1", "namcofl"};
    static const QStringList namcoMix = {"galaga", "mappy", "gaplus", "baraduke", "rallyx", "pacland", "pacman", "polepos"};
    static const QStringList atari90s = {"atarig1", "atarig42", "atarigt", "atarigx2"};
    static const QStringList atariMix = {"centiped", "ccastles", "irobot", "missile", "arcadecl", "badlands", "blstroid",
                                         "gauntlet", "harddriv", "klax", "atetris", "toobin", "xybots"};
    static const QStringList nintendoOld = {"dkong", "popeye", "punchout", "mario"};
    static const QStringList system16 = {"system16", "segahang", "segaorun", "segas16a", "segas16b"};
    static const QHash<QString, Rule> r = {
        {"pgm", {{"pgm", "pgm2", "pgm3"}, {}}},
        {"arcadia", {{"arcadia", "arsystems"}, {}}},
        {"irem", {{"mpatrol", "m52", "m62", "m72", "m90", "m92", "m107", "vigilant"}, {}}},
        {"midway", {{"midtunit", "midvunit", "midwunit", "midxunit", "midyunit", "midzeus"}, {}}},
        {"capcom", {QStringList{"cps1", "cps1mis", "cps2", "cps3"} + capcom80s, zn1 + QStringList{"coh3002c", "cpzn2"}}},
        {"cps1", {{"cps1", "cps1mis"}, {}}},
        {"cps2", {{"cps2", "cps2mis"}, {}}},
        {"cps3", {{"cps3", "cps3mis"}, {}}},
        {"cpzn2", {{}, {"coh3002c", "cpzn2"}}},
        {"capcom_80s", {capcom80s, {}}},
        {"sega", {QStringList{"system1"} + system16 + QStringList{"system18", "segas18", "system24", "segas24", "system32",
                  "segas32", "stv", "segaxbd", "segaybd", "segac2", "megatech", "megaplay", "model1", "model2", "model3",
                  "naomi", "chihiro"}, {"naomigd", "naomi2", "naomi2gd", "awbios"}}},
        {"system16", {system16, {}}},
        {"system18", {{"system18", "segas18"}, {}}},
        {"system24", {{"system24", "segas24"}, {}}},
        {"system32", {{"system32", "segas32"}, {}}},
        {"segaxybd", {{"segaxbd", "segaybd"}, {}}},
        {"awbios", {{}, {"awbios"}}},
        {"konami", {QStringList{"konamigx", "konamigv", "konamigq", "ksys573", "viper", "hornet", "gticlub"} + konami90s + konami80s, {}}},
        {"hornet_gticlub", {{"hornet", "gticlub"}, {}}},
        {"konami_80s", {konami80s, {}}},
        {"konami_90s", {konami90s, {}}},
        {"taito", {QStringList{"taito_f2", "taito_f3", "coh3002t", "taitogn", "taitotx"} + taitoMisc + taitoMix1, {"coh1000t", "taitofx1"}}},
        {"taito_fx1", {{}, {"coh1000t", "taitofx1"}}},
        {"taito_misc", {taitoMisc, {}}},
        {"taito_mix1", {taitoMix1, {}}},
        {"taito_mix2", {{"8080bw", "mw8080bw", "phoenix", "galdrvr", "williams"}, {}}},
        {"namco", {QStringList{"namcos1", "namcos2", "namcos10", "namcos11", "namcos12", "namcos21", "namcos22", "namcos23",
                   "namcops2"} + namco90s + namcoMix, {}}},
        {"namcos11", {{"namcos10", "namcos11"}, {}}},
        {"namcos22", {{"namcos22", "namcos23"}, {}}},
        {"namco_90s", {namco90s, {}}},
        {"namco_mix1", {namcoMix, {}}},
        {"atari", {QStringList{"atarisy1", "atarisy2"} + atari90s + atariMix, {}}},
        {"atari_90s", {atari90s, {}}},
        {"atari_mix1", {atariMix, {}}},
        {"nintendo", {QStringList{"playch10", "nss", "vsnes"} + nintendoOld, {}}},
        {"nintendo_nostalgia", {nintendoOld, {}}},
        {"vsnes", {{"vsnes", "nvs"}, {}}},
    };
    return r;
}
// Filters that are just their own driver (CheckDriverFilter).
const QSet<QString>& ownDriver() {
    static const QSet<QString> s = {"decocass", "simpl156", "vegas", "ssv", "seattle", "triforce", "system1", "stv",
                                    "segac2", "megatech", "megaplay", "model1", "model2", "model3", "chihiro", "viper",
                                    "konamigx", "konamigv", "konamigq", "ksys573", "taito_f2", "taito_f3", "taitogn",
                                    "taitotx", "namcos1", "namcos2", "namcos12", "namcops2", "atarisy1", "atarisy2",
                                    "playch10", "nss", "globalvr"};
    return s;
}
} // namespace

bool accept(const QString& filter, const MameGame& g, bool hbmame) {
    if (filter.isEmpty() || filter == "allgames" || filter == "favorites") return true;
    const QString drv = driverBase(g.driver);
    const QString& bios = g.bios;
    if (filter == "biossets") return g.kind == SetKind::Bios;
    if (filter == "devicesets") return g.kind == SetKind::Device;
    if (filter == "neogeo") return bios.compare("neogeo", Qt::CaseInsensitive) == 0 || drv == "neopcb";
    if (filter == "vector") return g.screenType == 1 && !hbmame;
    if (filter == "chd") return g.mediaType == 1;
    if (filter == "cpzn1") // the ZN driver, but not the ZN-2 or Taito FX-1 games
        return drv == "zn" && !QStringList{"coh3002c", "cpzn2", "coh1000t", "taitofx1"}.contains(bios);
    if (filter == "naomi") return drv == "naomi" && g.mediaType == 0 && bios != "naomigd" && bios != "naomi2" && bios != "awbios";
    if (filter == "naomigd") return bios == "naomigd";
    if (filter == "naomi2") return bios == "naomi2" && g.mediaType == 0;
    if (filter == "naomi2gd") return bios == "naomi2" && g.mediaType == 1;
    if (filter == "namcos21") return drv.contains("namcos21");
    if (filter == "namco" && drv.contains("namcos21")) return true;
    if (ownDriver().contains(filter)) return drv == filter;
    auto it = rules().constFind(filter);
    if (it == rules().cend()) return true; // a filter this version doesn't know
    return it->drivers.contains(drv) || it->bioses.contains(bios);
}

} // namespace ArcadeFilter

} // namespace eld
