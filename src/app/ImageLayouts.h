// ImageLayouts - the image panel's 27 layouts (TFormMain.SetImageLayout, ReadScreenshotLayout,
// UpdateScreenshotLayout and the GetScrLayout* tables in uCommon.pas).
//
// Layout 0 shows one image. The others show 2, 3 or 4 images (Images = view 1, ImageScr[2..4] =
// views 2..4), arranged with VCL Align (top / bottom / left / right / client) in the original's
// order, with a splitter between neighbors. Sizes and categories are kept in
// ini_files\screenshot_layouts.ini, in the original's sections and keys.
#pragma once

#include <QList>
#include <QString>
#include <array>
#include <functional>

class QWidget;

namespace eld {

class ElSplitter;
class ImageView;
class WinIni;

class ImageLayouts {
public:
    static constexpr int Count = 27;          // MaxImageLayouts + 1
    static QString section(int layout);       // GetScrLayoutSection: "Single", "Dual [Layout 1]", ...
    static QString title(int layout);         // section() in the user's language, for display
    static QString iconFile(int layout);      // GetScrLayoutImageFile without extension: "img1_layout1", ...
    static int imageCount(int layout);        // 1..4
    static int defaultCategory(int layout, int image);  // GetScrLayoutDefaultType (image 1..4)
    // The Video "category" (EmuLoader's own; the category pickers' choice): a panel that plays the
    // game's video over its snapshot. Saved as the snapshot (imageN_category=1, what the original
    // shows) and imageN_video=1 (imageNconscomp_video), which the original doesn't read.
    static constexpr int Video = 1000;

    // Settings of one layout (ReadScreenshotLayout).
    struct Settings {
        std::array<int, 4> category{1, 0, 0, 0};
        std::array<int, 4> consComp{1, 0, 0, 0}; // for console/computer machines (image1conscomp_category ...)
        std::array<bool, 4> video{}, videoCC{};  // Video: the snapshot, with the video playing over it
        std::array<bool, 2> enabled{true, true};  // images 2 and 3 (dual and triple layouts only)
        std::array<int, 3> size{};                // ImgSize[0..2]
        bool visible = true;                      // shown by the prev/next layout buttons
    };
    static Settings read(const WinIni& ini, int layout, int panelW, int panelH);
    static void writeCategories(WinIni& ini, int layout, const Settings& s);
    // [settings] UseCustomCategoryConsoleComputer
    static bool useConsComp(const WinIni& ini);

    // host: PanelImage; view1: the main image (Images).
    ImageLayouts(QWidget* host, ImageView* view1);
    ~ImageLayouts();

    // Builds a layout. Views 2..4 are created here (newView makes and connects one).
    void build(int layout, const Settings& s, const std::function<ImageView*()>& newView);
    int layout() const { return layout_; }
    // View for image 1..4 of the current layout, or nullptr when it isn't shown.
    ImageView* view(int image) const { return image >= 1 && image <= 4 ? views_[image - 1] : nullptr; }
    QList<ElSplitter*> splitters() const { return splitters_; }
    // UpdateScreenshotLayout: writes the current sizes of the built layout.
    void writeSizes(WinIni& ini) const;
    // Center All Splitters: back to the default sizes.
    void centerSplitters();
    // Center This Splitter (the splitter's own menu): the picture it sizes back to its default
    // size, the others as they are now.
    void centerSplitter(const QWidget* splitter);

private:
    void keepCurrentSizes(); // what the splitters were dragged to, into settings_
    void clear();
    void add(QWidget* w, QWidget* parent, const char* align, int size = -1);
    ImageView* image(int n, QWidget* parent, const char* align, int sizeIndex = -1);
    void splitter(QWidget* parent, const char* align);
    QWidget* panel(QWidget* parent, const char* align, int sizeIndex = -1);

    QWidget* host_;
    std::array<ImageView*, 4> views_{};
    QList<ElSplitter*> splitters_;
    QList<QWidget*> panels_;
    int layout_ = 0, order_ = 0;
    Settings settings_;
    std::function<ImageView*()> newView_;
    std::array<QWidget*, 3> sized_{};  // the widget each ImgSize[] was applied to
    std::array<char, 3> sizedDim_{};   // 'w' or 'h'
};

} // namespace eld
