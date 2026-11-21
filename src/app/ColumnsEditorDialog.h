// ColumnsEditorDialog - Customize Columns (TFormColumnsEditor): the games list's columns in
// their order, with a check box to show or hide each (Title always shows) and its width.
//   Ctrl+Up / Ctrl+Down   move the column        F2 / double-click   edit the width
//   Ctrl+Left / Right     width -1 / +1          F3 default width    F4 the list's width
//   F5 Reset All (as the list is)                F6 Default All
// Customize Header Font / Default Font change the column header's font at once.
// Ok applies it to the list (which saves ini_files\columns_profile.ini as before).
#pragma once

#include <QDialog>
#include <functional>
#include <memory>

class QHeaderView;
class QTreeWidget;
class QTreeWidgetItem;
namespace Ui { class FormColumnsEditor; }

namespace eld {

class ColumnsEditorDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        QHeaderView* header = nullptr;      // the games list's header
        std::function<void()> customizeFont; // PopupCustomizeColumnsHeaderFont
        std::function<void()> defaultFont;   // PopupDefaultHeaderFont
    };
    explicit ColumnsEditorDialog(const Input& in, QWidget* parent = nullptr);
    ~ColumnsEditorDialog() override;

    // The columns as listed ("key:width:shown").
    QStringList rows() const;
    void selectColumn(int column);
    void moveSelected(int by);
    void setSelectedWidth(int w);
    void setShown(int column, bool on);
    void defaultAll();
    void apply() const; // what Ok does

protected:
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void layoutWindow();
    void load(bool defaults);
    QTreeWidgetItem* selected() const;
    bool editing() const;
    void editWidth();
    void addWidth(int by);

    std::unique_ptr<Ui::FormColumnsEditor> ui_;
    Input in_;
    QTreeWidget* list_;
};

} // namespace eld
