#include "GameFontsDialog.h"
#include "VclRuntime.h"
#include "ui_FormGamesListFontSettings.h"

#include <QColorDialog>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLayout>
#include <QMenu>
#include <QStandardItemModel>

namespace eld {

GameFontsDialog::GameFontsDialog(const GameFonts& fonts, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormGamesListFontSettings), fonts_(fonts) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Customize Game Fonts"));
    model_ = new QStandardItemModel(this);
    ui_->GamesFont->setModel(model_);
    ui_->GamesFont->setRootIsDecorated(false);
    ui_->GamesFont->setUniformRowHeights(false);
    ui_->GamesFont->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui_->GamesFont->setContextMenuPolicy(Qt::CustomContextMenu);
    refresh();
    ui_->GamesFont->setCurrentIndex(model_->index(0, 0));

    connect(ui_->GamesFont, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex& i) { changeFont(i.row()); });
    connect(ui_->GamesFont, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        int row = ui_->GamesFont->indexAt(p).row();
        if (row < 0) row = ui_->GamesFont->currentIndex().row();
        QMenu m(this);
        if (row >= 0) {
            m.addAction(tr("Change Font..."), this, [this, row] { changeFont(row); });
            m.addAction(tr("Change Color..."), this, [this, row] { changeColor(row); });
            m.addSeparator();
            m.addAction(tr("Set Selected to Default Font"), this, [this, row] { fonts_.setDefault(GameFonts::Kind(row)); refresh(); });
        }
        m.addAction(tr("Set All to Default Fonts"), this, [this] {
            for (int k = 0; k < GameFonts::Count; ++k) fonts_.setDefault(GameFonts::Kind(k));
            refresh();
        });
        m.exec(ui_->GamesFont->viewport()->mapToGlobal(p));
    });
    connect(ui_->ButtonOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);

    // (the games list's background color and picture are in Settings > Colors)
    ui_->LabelHint->setWordWrap(true);
    vcl::layoutForm(this); // every text whole in any language and size
    ui_->GamesFont->setMinimumSize(300, 130); // (the design's size was far more than five sets need)
    layout()->activate();
    setMinimumSize(layout()->minimumSize());
    resize(QSize(760, 300).expandedTo(minimumSize())); // (room for the five sets; it can be made bigger)
}

GameFontsDialog::~GameFontsDialog() = default;

void GameFontsDialog::refresh() {
    int cur = ui_->GamesFont->currentIndex().row();
    model_->clear();
    model_->setHorizontalHeaderLabels({tr("Set"), tr("Font"), tr("Size")});
    for (int k = 0; k < GameFonts::Count; ++k) {
        const QFont& f = fonts_.font[k];
        // "size: 9 - height: 15 pixels", as the original shows it.
        QString size = tr("size: %1 - height: %2 pixels").arg(f.pointSize()).arg(QFontMetrics(f).height());
        QList<QStandardItem*> row{new QStandardItem(GameFonts::title(GameFonts::Kind(k))), new QStandardItem(f.family()),
                                  new QStandardItem(size)};
        row[0]->setFont(f);
        row[0]->setForeground(fonts_.color[k]);
        model_->appendRow(row);
    }
    ui_->GamesFont->header()->resizeSection(0, 320);
    ui_->GamesFont->header()->resizeSection(1, 220);
    if (cur >= 0) ui_->GamesFont->setCurrentIndex(model_->index(cur, 0));
}

void GameFontsDialog::changeFont(int row) {
    if (row < 0 || row >= GameFonts::Count) return;
    bool ok = false;
    // Give the dialog the font's real style name ("Regular", "Book", "Bold Italic"...) so it
    // preselects the current style instead of the first one in its list.
    QFont cur = fonts_.font[row];
    for (const QString& st : QFontDatabase::styles(cur.family()))
        if (QFontDatabase::bold(cur.family(), st) == cur.bold() && QFontDatabase::italic(cur.family(), st) == cur.italic()) {
            cur.setStyleName(st);
            break;
        }
    QFont f = QFontDialog::getFont(&ok, cur, this, tr("Font: %1").arg(GameFonts::title(GameFonts::Kind(row))));
    if (!ok) return;
    f.setStyleName({}); // keep only bold/italic/underline/strikeout, as Delphi's TFont does
    fonts_.font[row] = f;
    refresh();
}

void GameFontsDialog::changeColor(int row) {
    if (row < 0 || row >= GameFonts::Count) return;
    QColor c = QColorDialog::getColor(fonts_.color[row], this, tr("Color: %1").arg(GameFonts::title(GameFonts::Kind(row))));
    if (!c.isValid()) return;
    fonts_.color[row] = c;
    refresh();
}

} // namespace eld
