#pragma once

#include <QWidget>

class QLabel;
class QTabBar;
class QToolButton;

namespace opendraw {

class Document;

// Page navigator under the drawing: insert before, first, previous, "1 of 3", next, last,
// insert after, then one tab per page.
class PageBar : public QWidget {
    Q_OBJECT

public:
    explicit PageBar(Document *document, QWidget *parent = nullptr);

signals:
    // Insert a new page at this index.
    void insertPageRequested(int index);
    void renamePageRequested();
    void deletePageRequested();

private:
    void sync();

    Document *m_document;
    QToolButton *m_first;
    QToolButton *m_previous;
    QLabel *m_position;
    QToolButton *m_next;
    QToolButton *m_last;
    QTabBar *m_tabs;
    bool m_updating = false;
};

} // namespace opendraw
