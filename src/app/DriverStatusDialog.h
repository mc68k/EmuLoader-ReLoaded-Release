// DriverStatusDialog - Driver Status Filter (TFormArcadeFiltersDriverStatus,
// uFiltersDriverStatus.pas; the Driver Status tool bar button): which status to look at
// (driver, emulation, color, sound, graphics) and which games to list (all, working good +
// imperfect, good, imperfect, non-working preliminary). Saved as [ListFilter] DriverStatus and
// DriverStatusCategory in EmuLoader.ini, as the original does.
#pragma once

#include <QDialog>
#include <QIcon>
#include <memory>

namespace Ui { class FormArcadeFiltersDriverStatus; }
class QRadioButton;

namespace eld {

class DriverStatusDialog : public QDialog {
    Q_OBJECT
public:
    // icons: the five status pictures of the tool bar button (List All ... Preliminary)
    DriverStatusDialog(int status, int category, const QList<QIcon>& icons, QWidget* parent = nullptr);
    ~DriverStatusDialog() override;
    int status() const;
    int category() const;

private:
    std::unique_ptr<Ui::FormArcadeFiltersDriverStatus> ui_;
    QList<QRadioButton*> rows_;
};

} // namespace eld
