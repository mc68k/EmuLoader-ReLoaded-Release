#include "ImageDeleteRenameDialog.h"

#include "RecycleBin.h"

#include "ElWidgets.h"
#include "VclRuntime.h"
#include "ui_FormImageDeleteRename.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QMessageBox>
#include <QRegularExpressionValidator>

namespace eld {

namespace {
QString sizeText(qint64 bytes) {
    if (bytes < 1024) return ImageDeleteRenameDialog::tr("%1 Bytes").arg(bytes);
    return QLocale().formattedDataSize(bytes, 2, QLocale::DataSizeTraditionalFormat);
}
} // namespace

ImageDeleteRenameDialog::ImageDeleteRenameDialog(const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormImageDeleteRename), in_(in) {
    ui_->setupUi(this);
    vcl::apply(this);
    const MameGame& g = in_.game;
    setWindowTitle(in_.rename ? tr("Rename Image File") : tr("Delete Image File"));

    // Top bar, colored by the set's status (SetColorsGameTopBar)
    static const int topColors[] = {0xF0FAE5, 0xE5F0FA, 0xEEEEEE};
    ui_->TopBar->setProperty("vclColor1", ElColorBox::toQColor(topColors[std::clamp<int>(g.setStatus, 0, 2)]).name());
    ui_->TopBar->setProperty("vclColor2", QColor(0xF1F1F1).name());
    ui_->GameIcon->setPixmap(in_.gameIcon.pixmap(68, 68));
    ui_->GameIcon->setAlignment(Qt::AlignCenter);
    ui_->LabelGameTitle->setText(g.title);
    ui_->LabelGameName->setText(tr("name: %1").arg(in_.nameLine));
    const char* status[] = {QT_TR_NOOP("Available"), QT_TR_NOOP("Missing ROMs/CHDs"), QT_TR_NOOP("Not Available")};
    ui_->LabelGameStatus->setText(tr("Game Status") + "\n" +
                                  (g.setStatus >= 0 && g.setStatus <= 2 ? tr(status[g.setStatus]) : tr("Unknown")));
    ui_->LabelGameStatus->setToolTip(tr("Game Status"));
    ui_->LabelGameStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ui_->LabelSystemTitle->setText(in_.system);
    ui_->LabelSoftwareListTitle->setText(g.softwareTitle); // a software list game's list
    ui_->LabelSoftwareListTitle->setVisible(g.isSoftware());

    // The picture and its details (LoadPreviewImage: the type from the data, not the extension)
    QFileInfo fi(in_.file);
    // The path on two lines: the folder (shortened in the middle when it's long), then the file.
    {
        QLabel* l = ui_->LabelFilename;
        QString dir = QDir::toNativeSeparators(fi.absolutePath() + "/");
        dir = l->fontMetrics().elidedText(dir, Qt::ElideMiddle, l->width());
        l->setText(dir + "\n" + fi.fileName());
        l->setToolTip(QDir::toNativeSeparators(in_.file));
        l->setWordWrap(false);
        l->resize(l->width(), std::max(l->height(), l->fontMetrics().lineSpacing() * 2 + 4));
    }
    QImageReader reader(in_.file);
    reader.setDecideFormatFromContent(true);
    QByteArray format = reader.format().toLower();
    QImage img = reader.read();
    QSize box = ui_->ImagePreview->size();
    if (!img.isNull())
        ui_->ImagePreview->setPixmap(QPixmap::fromImage(
            img.width() > box.width() || img.height() > box.height()
                ? img.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation) : img));
    ui_->ImagePreview->setAlignment(Qt::AlignCenter);
    struct T { const char* fmt; const char* ext; const char* text; };
    static const T types[] = {{"png", "png", QT_TR_NOOP("Type: Portable Network Graphics (PNG)")},
                              {"jpeg", "jpg", QT_TR_NOOP("Type: Joint Photographic Experts Group (JPEG)")},
                              {"gif", "gif", QT_TR_NOOP("Type: Graphics Interchange Format (GIF)")},
                              {"bmp", "bmp", QT_TR_NOOP("Type: Bitmap Image (BMP)")}};
    ui_->LabelFileType->clear();
    ui_->LabelFileTypeMismatch->hide();
    for (const T& t : types)
        if (format == t.fmt) {
            ui_->LabelFileType->setText(tr(t.text));
            QString ext = fi.suffix().toLower();
            bool same = ext == t.ext || (format == "jpeg" && ext == "jpeg");
            ui_->LabelFileTypeMismatch->setVisible(!same);
        }
    ui_->LabelFileTypeMismatch->setText(tr("file type does not match file extension"));
    ui_->LabelFileTypeMismatch->setStyleSheet("color: #C00000;");
    ui_->LabelDimensions->setText(img.isNull() ? tr("Dimensions: ?") : tr("Dimensions: %1x%2").arg(img.width()).arg(img.height()));
    ui_->LabelFileSize->setText(tr("Size: %1").arg(sizeText(fi.size())));
    ui_->LabelDateTime->setText(tr("Date/Time: %1").arg(QLocale().toString(fi.lastModified(), QLocale::ShortFormat)));
    for (QLabel* l : {ui_->LabelDimensions, ui_->LabelFileSize, ui_->LabelDateTime, ui_->LabelFileType, ui_->LabelFileTypeMismatch})
        l->adjustSize();
    ui_->ImageCategoryIcon->setPixmap(in_.categoryIcon.pixmap(68, 68));
    ui_->ImageCategoryIcon->setAlignment(Qt::AlignCenter);

    // Rename: the new name, without the path or the characters a file name can't have
    ui_->LabelRenameImage->setText(tr("New Name (file extension not required)"));
    ui_->LabelRenameImage->adjustSize();
    ui_->RenameImageEditBoxButtonReset->setText(tr("Reset"));
    ui_->RenameImageEditBoxButtonReset->setToolTip(tr("Click here to reset name to the image's file name"));
    ui_->RenameImageEditBox->setValidator(new QRegularExpressionValidator(QRegularExpression(R"([^/\\*?<>|:;"]*)"), this));
    auto reset = [this] { ui_->RenameImageEditBox->setText(QFileInfo(in_.file).completeBaseName()); };
    connect(ui_->RenameImageEditBoxButtonReset, &QPushButton::clicked, this, reset);
    for (QWidget* w : {(QWidget*)ui_->RenameImageEditBox, (QWidget*)ui_->LabelRenameImage, (QWidget*)ui_->RenameImageEditBoxButtonReset})
        w->setVisible(in_.rename);

    ui_->ButtonOk->setText(in_.rename ? tr("Rename File") : tr("Delete File"));
    ui_->ButtonOk->setToolTip(tr("Click here to confirm operation"));
    ui_->ButtonCancel->setText(tr("Cancel"));
    ui_->ButtonCancel->setToolTip(tr("Click here to abort"));
    for (QPushButton* b : {ui_->ButtonOk, ui_->ButtonCancel}) b->setAutoDefault(false);
    ui_->ButtonOk->setDefault(true);
    connect(ui_->ButtonOk, &QPushButton::clicked, this, &ImageDeleteRenameDialog::accept);
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    vcl::setDataLabel(ui_->LabelFilename); // (a path: its design width, shortened)
    vcl::layoutForm(this); // every text whole in any language and size
    if (in_.rename) {
        reset();
        ui_->RenameImageEditBox->selectAll();
        ui_->RenameImageEditBox->setFocus();
    } else {
        ui_->ButtonCancel->setFocus();
    }
}

ImageDeleteRenameDialog::~ImageDeleteRenameDialog() = default;

QString ImageDeleteRenameDialog::newName() const {
    QString n = ui_->RenameImageEditBox->text().trimmed();
    if (n.isEmpty()) return {};
    QString ext = QFileInfo(in_.file).suffix();
    // ChangeFileExtW: what's after the last dot is replaced by the image's extension
    int dot = n.lastIndexOf('.');
    if (dot > 0) n = n.left(dot);
    return n + "." + ext;
}

// FormCloseQuery: a rename needs a name.
void ImageDeleteRenameDialog::accept() {
    if (in_.rename && ui_->RenameImageEditBox->text().trimmed().isEmpty()) return;
    QDialog::accept();
}

bool ImageDeleteRenameDialog::run(const Input& in, QWidget* parent) {
    ImageDeleteRenameDialog dlg(in, parent);
    if (dlg.exec() != QDialog::Accepted) return false;
    if (!in.rename) {
        if (!moveToRecycleBin({in.file}, parent).isEmpty()) return true;
        QMessageBox::warning(parent, dlg.windowTitle(), tr("Could not delete %1").arg(QDir::toNativeSeparators(in.file)));
        return false;
    }
    QString name = dlg.newName();
    QFileInfo fi(in.file);
    if (name.isEmpty() || name == fi.fileName()) return false;
    QString to = fi.dir().filePath(name);
    if (QFileInfo::exists(to) && name.compare(fi.fileName(), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(parent, dlg.windowTitle(), tr("%1 already exists.").arg(QDir::toNativeSeparators(to)));
        return false;
    }
    bool ok = name.compare(fi.fileName(), Qt::CaseInsensitive) == 0
                  ? QFile::rename(in.file, in.file + ".tmp") && QFile::rename(in.file + ".tmp", to)
                  : QFile::rename(in.file, to);
    if (!ok) QMessageBox::warning(parent, dlg.windowTitle(), tr("Could not rename %1").arg(QDir::toNativeSeparators(in.file)));
    return ok;
}

} // namespace eld
