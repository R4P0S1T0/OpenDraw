#include "PageBar.h"

#include "Icons.h"
#include "core/Document.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QTabBar>
#include <QToolButton>

namespace opendraw {

namespace {
QToolButton *button(Icon id, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icon(id));
    b->setIconSize(QSize(16, 16));
    b->setAutoRaise(true);
    b->setToolTip(tip);
    return b;
}
} // namespace

PageBar::PageBar(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
{
    auto *insertBefore = button(Icon::AddPage, tr("Insert page before the current one"), this);
    m_first = button(Icon::FirstPage, tr("First page"), this);
    m_previous = button(Icon::PreviousPage, tr("Previous page (PgUp)"), this);
    m_position = new QLabel(this);
    m_position->setContentsMargins(6, 0, 6, 0);
    m_next = button(Icon::NextPage, tr("Next page (PgDn)"), this);
    m_last = button(Icon::LastPage, tr("Last page"), this);
    auto *insertAfter = button(Icon::AddPage, tr("Insert page after the current one"), this);

    m_tabs = new QTabBar(this);
    m_tabs->setObjectName(QStringLiteral("pageTabs"));
    m_tabs->setShape(QTabBar::RoundedSouth);
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setDocumentMode(true);
    m_tabs->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(0);
    for (QWidget *w : std::initializer_list<QWidget *>{insertBefore, m_first, m_previous, m_position, m_next, m_last,
                                                       insertAfter})
        layout->addWidget(w);
    layout->addSpacing(6);
    layout->addWidget(m_tabs);
    layout->addStretch();

    connect(insertBefore, &QToolButton::clicked, this,
            [this] { emit insertPageRequested(m_document->currentPageIndex()); });
    connect(insertAfter, &QToolButton::clicked, this,
            [this] { emit insertPageRequested(m_document->currentPageIndex() + 1); });
    connect(m_first, &QToolButton::clicked, this, [this] { m_document->setCurrentPage(0); });
    connect(m_previous, &QToolButton::clicked, this,
            [this] { m_document->setCurrentPage(m_document->currentPageIndex() - 1); });
    connect(m_next, &QToolButton::clicked, this,
            [this] { m_document->setCurrentPage(m_document->currentPageIndex() + 1); });
    connect(m_last, &QToolButton::clicked, this, [this] { m_document->setCurrentPage(m_document->pageCount() - 1); });

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_updating && index >= 0)
            m_document->setCurrentPage(index);
    });
    connect(m_tabs, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (index >= 0)
            emit renamePageRequested();
    });
    connect(m_tabs, &QTabBar::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int index = m_tabs->tabAt(pos);
        if (index < 0)
            return;
        m_document->setCurrentPage(index);
        QMenu menu(this);
        menu.addAction(tr("Rename Page…"), this, &PageBar::renamePageRequested);
        menu.addAction(tr("Insert Page Before"), this,
                       [this] { emit insertPageRequested(m_document->currentPageIndex()); });
        menu.addAction(tr("Insert Page After"), this,
                       [this] { emit insertPageRequested(m_document->currentPageIndex() + 1); });
        QAction *remove = menu.addAction(tr("Delete Page"), this, &PageBar::deletePageRequested);
        remove->setEnabled(m_document->pageCount() > 1);
        menu.exec(m_tabs->mapToGlobal(pos));
    });
    connectDocument();
    sync();
}

void PageBar::setDocument(Document *document)
{
    if (document == m_document)
        return;
    m_document->disconnect(this);
    m_document = document;
    connectDocument();
    sync();
}

void PageBar::connectDocument()
{
    connect(m_document, &Document::structureChanged, this, &PageBar::sync);
    connect(m_document, &Document::currentPageChanged, this, &PageBar::sync);
    connect(m_document, &Document::cleared, this, &PageBar::sync);
}

void PageBar::sync()
{
    m_updating = true;
    while (m_tabs->count() > m_document->pageCount())
        m_tabs->removeTab(m_tabs->count() - 1);
    for (int i = 0; i < m_document->pageCount(); ++i) {
        const QString name = m_document->pages()[i]->name;
        if (i < m_tabs->count())
            m_tabs->setTabText(i, name);
        else
            m_tabs->addTab(name);
    }
    const int current = m_document->currentPageIndex();
    m_tabs->setCurrentIndex(current);
    m_position->setText(tr("%1 of %2").arg(current + 1).arg(m_document->pageCount()));
    m_first->setEnabled(current > 0);
    m_previous->setEnabled(current > 0);
    m_next->setEnabled(current + 1 < m_document->pageCount());
    m_last->setEnabled(current + 1 < m_document->pageCount());
    m_updating = false;
}

} // namespace opendraw
