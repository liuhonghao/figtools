// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Category editor for annotation projects. Add / rename / recolour / remove
// label classes. No Q_OBJECT: all wiring is done with lambdas, so no moc step
// is needed. The dialog returns the edited classes through `classes()`.

#ifndef FIGTOOLS_GUI_CLASS_MANAGER_HPP_
#define FIGTOOLS_GUI_CLASS_MANAGER_HPP_

#include <QColorDialog>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <vector>

#include "figtools/annotate/annotation.hpp"
#include "theme.hpp"

namespace figtools::gui {

class ClassManager : public QDialog {
 public:
  explicit ClassManager(const std::vector<LabelClass>& initial, QWidget* parent = nullptr)
      : QDialog(parent) {
    setWindowTitle(tr("管理类别"));
    resize(460, 420);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);
    layout->addWidget(new QLabel(
        tr("定义这个数据集的部件类别。序号即 YOLO 训练时的类别 ID，请保持名称唯一。"), this));

    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("ID"), tr("类别名称"), tr("颜色")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table_->setColumnWidth(0, 50);
    table_->setColumnWidth(2, 80);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);

    for (const LabelClass& label : initial) {
      add_row(label.id, QString::fromStdString(label.name), QString::fromStdString(label.color));
    }
    renumber();

    auto* actions = new QHBoxLayout();
    auto* add = new QPushButton(tr("+ 添加类别"), this);
    auto* remove = new QPushButton(tr("删除选中"), this);
    connect(add, &QPushButton::clicked, this, [this]() {
      const int id = table_->rowCount() == 0 ? 0 : table_->rowCount();
      const QColor palette[] = {"#3D9BE9", "#4F7A72", "#D9A441", "#D9615A", "#7F77DD",
                                "#1D9E75", "#D85A30", "#534AB7", "#A9781A", "#42A5C4"};
      add_row(id, tr("新类别"), palette[id % 10].name());
      renumber();
      table_->setCurrentCell(table_->rowCount() - 1, 1);
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
      const int row = table_->currentRow();
      if (row < 0) return;
      table_->removeRow(row);
      renumber();
    });
    actions->addWidget(add);
    actions->addWidget(remove);
    actions->addStretch(1);
    layout->addLayout(actions);

    auto* buttons = new QHBoxLayout();
    auto* ok = new QPushButton(tr("保存"), this);
    ok->setObjectName(QStringLiteral("Primary"));
    auto* cancel = new QPushButton(tr("取消"), this);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);
  }

  [[nodiscard]] std::vector<LabelClass> classes() const {
    std::vector<LabelClass> result;
    for (int row = 0; row < table_->rowCount(); ++row) {
      LabelClass label;
      label.id = row;
      auto* name = qobject_cast<QLineEdit*>(table_->cellWidget(row, 1));
      label.name = name != nullptr && !name->text().trimmed().isEmpty()
                       ? name->text().trimmed().toStdString()
                       : ("class_" + std::to_string(row));
      auto* colour = qobject_cast<QPushButton*>(table_->cellWidget(row, 2));
      label.color = colour != nullptr ? colour->property("hex").toString().toStdString()
                                      : "#3D9BE9";
      result.push_back(std::move(label));
    }
    return result;
  }

 private:
  void add_row(int id, const QString& name, const QString& colour) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto* id_item = new QTableWidgetItem(QString::number(id));
    id_item->setFlags(Qt::ItemIsEnabled);
    table_->setItem(row, 0, id_item);
    auto* name_edit = new QLineEdit(name, table_);
    name_edit->setFont(theme::ui_font(11));
    table_->setCellWidget(row, 1, name_edit);
    auto* colour_button = new QPushButton(table_);
    colour_button->setProperty("hex", colour);
    colour_button->setStyleSheet(
        QStringLiteral("background:%1; border:1px solid #6B7280; border-radius:3px;").arg(colour));
    colour_button->setToolTip(tr("点击选择颜色"));
    table_->setCellWidget(row, 2, colour_button);
    const int captured_row = row;
    connect(colour_button, &QPushButton::clicked, this, [this, captured_row]() {
      auto* button = qobject_cast<QPushButton*>(table_->cellWidget(captured_row, 2));
      if (button == nullptr) return;
      const QColor chosen = QColorDialog::getColor(QColor(button->property("hex").toString()),
                                                   this, tr("选择类别颜色"));
      if (!chosen.isValid()) return;
      button->setProperty("hex", chosen.name());
      button->setStyleSheet(
          QStringLiteral("background:%1; border:1px solid #6B7280; border-radius:3px;")
              .arg(chosen.name()));
    });
  }

  void renumber() {
    for (int row = 0; row < table_->rowCount(); ++row) {
      auto* item = table_->item(row, 0);
      if (item != nullptr) item->setText(QString::number(row));
    }
  }

  QTableWidget* table_ = nullptr;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_CLASS_MANAGER_HPP_
