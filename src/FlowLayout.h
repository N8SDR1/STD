#pragma once

#include <QLayout>
#include <QList>
#include <QRect>
#include <QSize>

#include <algorithm>

// One horizontal strip that starts another line when the panel is too narrow.
class FlowLayout : public QLayout {
 public:
  explicit FlowLayout(QWidget* parent = nullptr) : QLayout(parent) {}
  ~FlowLayout() override {
    while (QLayoutItem* item = takeAt(0))
      delete item;
  }
  void addItem(QLayoutItem* item) override { items_.append(item); }
  int count() const override { return items_.size(); }
  QLayoutItem* itemAt(int index) const override { return items_.value(index); }
  QLayoutItem* takeAt(int index) override {
    return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
  }
  Qt::Orientations expandingDirections() const override { return {}; }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
  QSize sizeHint() const override { return minimumSize(); }
  QSize minimumSize() const override {
    QSize size;
    for (const QLayoutItem* item : items_)
      size = size.expandedTo(item->minimumSize());
    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
  }
  void setGeometry(const QRect& rect) override {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
  }

 private:
  int doLayout(const QRect& rect, bool test) const {
    const QMargins margins = contentsMargins();
    const QRect area = rect.marginsRemoved(margins);
    int x = area.x();
    int y = area.y();
    int lineHeight = 0;
    const int spaceX = spacing();
    const int spaceY = spacing();
    for (const QLayoutItem* item : items_) {
      const QSize hint = item->sizeHint().boundedTo(item->maximumSize());
      const int itemWidth =
          std::min(std::max(item->minimumSize().width(), hint.width()), std::max(1, area.width()));
      const int itemHeight = std::max(item->minimumSize().height(), hint.height());
      if (x > area.x() && x + itemWidth > area.right() + 1) {
        x = area.x();
        y += lineHeight + spaceY;
        lineHeight = 0;
      }
      if (!test)
        const_cast<QLayoutItem*>(item)->setGeometry(QRect(x, y, itemWidth, itemHeight));
      x += itemWidth + spaceX;
      lineHeight = std::max(lineHeight, itemHeight);
    }
    return y + lineHeight - rect.y() + margins.bottom();
  }
  QList<QLayoutItem*> items_;
};

