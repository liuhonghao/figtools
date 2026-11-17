// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Ctrl+K command palette. Type to filter, Enter to run. No Q_OBJECT needed:
// the dialog returns the chosen command id through selectedId().

#ifndef FIGTOOLS_GUI_COMMAND_PALETTE_HPP_
#define FIGTOOLS_GUI_COMMAND_PALETTE_HPP_

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <utility>
#include <vector>

#include "theme.hpp"

namespace figtools::gui {

class CommandPalette : public QDialog {
 public:
  struct Command {
    QString label;
    QString category;
    QString id;
  };

  explicit CommandPalette(const std::vector<Command>& commands, QWidget* parent = nullptr)
      : QDialog(parent) {
    setWindowTitle(tr("命令搜索"));
    resize(560, 420);
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("输入命令或算子名称…"));
    search_->setFont(theme::ui_font(13));
    layout->addWidget(search_);

    list_ = new QListWidget(this);
    list_->setFont(theme::ui_font(11));
    layout->addWidget(list_, 1);

    for (const Command& command : commands) {
      auto* item = new QListWidgetItem(
          QStringLiteral("%1   —   %2").arg(command.label, command.category), list_);
      item->setData(Qt::UserRole, command.id);
    }

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
      for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
      }
      if (list_->count() > 0) {
        for (int i = 0; i < list_->count(); ++i) {
          if (!list_->item(i)->isHidden()) {
            list_->setCurrentRow(i);
            break;
          }
        }
      }
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
      accept();
    });
    connect(search_, &QLineEdit::returnPressed, this, [this]() { accept(); });
    if (list_->count() > 0) {
      list_->setCurrentRow(0);
    }
    search_->setFocus();
  }

  [[nodiscard]] QString selectedId() const {
    QListWidgetItem* item = list_->currentItem();
    return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
  }

 private:
  QLineEdit* search_ = nullptr;
  QListWidget* list_ = nullptr;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_COMMAND_PALETTE_HPP_
