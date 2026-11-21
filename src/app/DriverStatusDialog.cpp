#include "DriverStatusDialog.h"

#include "VclRuntime.h"
#include "ui_FormArcadeFiltersDriverStatus.h"

#include <QFontMetrics>
#include <QPushButton>
#include <QRadioButton>

namespace eld {

DriverStatusDialog::DriverStatusDialog(int status, int category, const QList<QIcon>& icons, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeFiltersDriverStatus) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Driver Status Filter"));
    // The rows in the order of the tool bar button's states (ButtonGameFilterDriverStatus.Tag).
    rows_ = {ui_->ListAll, ui_->GoodImperfect, ui_->Good, ui_->Imperfect, ui_->Preliminary};
    for (int i = 0; i < 5; ++i) { // each row's picture between its circle and its caption
        if (i < icons.size()) rows_[i]->setIcon(icons[i]);
        rows_[i]->setIconSize(QSize(30, 24));
        rows_[i]->setChecked(i == status);
    }
    if (rows_.value(status) == nullptr) ui_->ListAll->setChecked(true);
    ui_->DriverStatusCategory->setCurrentIndex(std::clamp(category, 0, ui_->DriverStatusCategory->count() - 1));
    ui_->DriverStatusCategory->setToolTip(tr("Which status the filter looks at. MAME lists color, sound and graphics\n"
                                             "only when they aren't good, so machines that don't list them count as good."));
    connect(ui_->ButtonOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    ui_->ButtonOk->setDefault(true);
    vcl::layoutForm(this); // every text whole in any language and size
}

DriverStatusDialog::~DriverStatusDialog() = default;

int DriverStatusDialog::status() const {
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i]->isChecked()) return i;
    return 0;
}

int DriverStatusDialog::category() const { return std::max(0, ui_->DriverStatusCategory->currentIndex()); }

} // namespace eld
