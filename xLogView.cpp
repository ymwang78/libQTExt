// ***************************************************************
//  xLogView   version:  1.0   -  date:  2025/08/13
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************
#include "xLogView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QMessageBox>
#include <QFileDialog>
#include <QTextStream>
#include <QDebug>
#include <QFont>
#include <QScrollBar>
#include <zce/zce_log.h>
#include "xTheme.h"

// ==========================================
// LogModel 实现 (数据层)
// ==========================================

xLogModel::xLogModel(int maxLines, QObject* parent)
    : QAbstractListModel(parent), m_maxLines(maxLines) {}

int xLogModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_data.size();
}

QVariant xLogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_data.size()) return QVariant();

    const xLogItem& item = m_data.at(index.row());

    switch (role) {
        case Qt::DisplayRole: {
            // 格式化文本: [HH:mm:ss] [LEVEL] Content
            return QString("[%1] [%2] %3")
                .arg(item.time.toString("HH:mm:ss"))
                .arg(getLevelString(item.level))
                .arg(item.text);
        }
        case Qt::ForegroundRole: {
            // 设置文字颜色
            return getLevelColor(item.level);
        }
        case Qt::ToolTipRole: {
            return item.text;
        }
        case LevelRole:
            return item.level;
        case TimeRole:
            return item.time;
    }

    return QVariant();
}

void xLogModel::appendLogs(const QList<xLogItem>& newLogs) {
    if (newLogs.isEmpty()) return;

    // 1. 批量插入数据
    // beginInsertRows 通知 View 即将发生变化，非常重要！
    beginInsertRows(QModelIndex(), m_data.size(), m_data.size() + newLogs.size() - 1);
    m_data.append(newLogs);
    endInsertRows();

    // 2. 检查并删除超出限制的数据 (保留最新的 m_maxLines 条)
    if (m_data.size() > m_maxLines) {
        int removeCount = m_data.size() - m_maxLines;
        beginRemoveRows(QModelIndex(), 0, removeCount - 1);
        m_data.erase(m_data.begin(), m_data.begin() + removeCount);
        endRemoveRows();
    }
}

void xLogModel::clear() {
    beginResetModel();
    m_data.clear();
    endResetModel();
}

QString xLogModel::getLevelString(int level) const {
    // 简单映射，根据你的枚举调整
    switch (level) {
        case 0:
            return "TRACE";
        case 1:
            return "DEBUG";
        case 2:
            return "INFO ";
        case 3:
            return "WARN ";
        case 4:
            return "ERROR";
        case 5:
            return "FATAL";
        case 6:
            return "BIZDATA";
        case 7:
            return "SILENT";
        default:
            return "UNKNW";
    }
}

QColor xLogModel::getLevelColor(int level) const {
    switch (level) {
        case 0:
            return QColor(128, 128, 128);  // Gray
        case 1:
            return QColor(0, 128, 255);  // Blue
        case 2:
            return QColor(0, 0, 0);  // Black
        case 3:
            return QColor(255, 140, 0);  // Orange
        case 4:
            return QColor(255, 0, 0);  // Red
        case 5:
            return QColor(128, 0, 128);  // Purple
        case 6:
            return QColor(0, 128, 0);  // Green
        case 7:
            return QColor(192, 192, 192);  // Silver
        default:
            return QColor(0, 0, 0);
    }
}

// ==========================================
// LogFilterProxy 实现 (过滤层)
// ==========================================

xLogFilterProxy::xLogFilterProxy(QObject* parent) : QSortFilterProxyModel(parent) {
    // 优化：不需要动态排序，日志本身就是按时间顺序的
    setDynamicSortFilter(false);
}

void xLogFilterProxy::setMinLevel(int level) {
    m_minLevel = level;
    invalidateFilter();  // 触发重新过滤
}

void xLogFilterProxy::setSearchText(const QString& text) {
    m_searchText = text.toLower();
    invalidateFilter();  // 触发重新过滤
}

bool xLogFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    // 获取源模型的数据
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    // 1. 检查等级
    int level = sourceModel()->data(index, xLogModel::LevelRole).toInt();
    if (level < m_minLevel) return false;

    // 2. 检查搜索文本 (如果为空则通过)
    if (m_searchText.isEmpty()) return true;

    QString text = sourceModel()->data(index, Qt::DisplayRole).toString().toLower();
    return text.contains(m_searchText);
}

// ==========================================
// LogView 实现 (UI与控制层)
// ==========================================

xLogView::xLogView(int maxLines, QWidget* parent)
    : QWidget(parent), m_maxLines(maxLines), m_model(nullptr), m_proxyModel(nullptr) {
    setupUI();

    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(100);  // 100ms 刷新一次
    connect(m_updateTimer, &QTimer::timeout, this, &xLogView::onUpdateTimer);
    m_updateTimer->start();
}

xLogView::~xLogView() {}

void xLogView::setupUI() {
    Q_INIT_RESOURCE(qtext);
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupTitleBar();
    mainLayout->addWidget(m_titleBar);

    // --- 初始化 Model/View ---
    m_listView = new QListView(this);
    m_model = new xLogModel(m_maxLines, this);
    m_proxyModel = new xLogFilterProxy(this);

    // 设置代理链：View -> Proxy -> Model
    m_proxyModel->setSourceModel(m_model);
    m_listView->setModel(m_proxyModel);

    // --- UI 性能设置 ---
    m_listView->setUniformItemSizes(true);          // 固定行高
    m_listView->setLayoutMode(QListView::Batched);  // 批处理布局
    m_listView->setBatchSize(100);

    // 设置字体
    QFont monoFont("Consolas");
    monoFont.setStyleHint(QFont::Monospace);
#ifdef Q_OS_WIN
    monoFont.setFamily("Consolas, Microsoft YaHei");
#endif
    m_listView->setFont(monoFont);

    mainLayout->addWidget(m_listView);

    // 连接信号
    connect(m_levelFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &xLogView::applyFilter);
    connect(m_searchBox, &QLineEdit::textChanged, this, &xLogView::applyFilter);
    connect(m_clearButton, &QPushButton::clicked, this, &xLogView::onClearLog);
    connect(m_autoScroll, &QCheckBox::toggled, this, &xLogView::setAutoScroll);
    
    // 连接滚动条信号，检测用户手动滚动
    QScrollBar* scrollBar = m_listView->verticalScrollBar();
    connect(scrollBar, &QScrollBar::valueChanged, this, &xLogView::onScrollBarValueChanged);
}

void xLogView::setupTitleBar() {
    m_titleBar = new QFrame(this);
    m_titleBar->setFrameShape(QFrame::NoFrame);
    m_titleBar->setFixedHeight(28);

    QHBoxLayout* titleBarLayout = new QHBoxLayout(m_titleBar);
    titleBarLayout->setContentsMargins(8, 2, 8, 2);
    titleBarLayout->setSpacing(6);

    titleBarLayout->addStretch();

    auto* vline = new QFrame(this);
    vline->setFrameShape(QFrame::VLine);
    vline->setFrameShadow(QFrame::Sunken);
    titleBarLayout->addWidget(vline);

    m_levelFilter = new QComboBox(m_titleBar);
    m_levelFilter->addItem("ALL", static_cast<int>(ZLOG_TRACE));
    m_levelFilter->addItem("DEBUG+", static_cast<int>(ZLOG_DEBUG));
    m_levelFilter->addItem("INFO+", static_cast<int>(ZLOG_INFOR));
    m_levelFilter->addItem("WARN+", static_cast<int>(ZLOG_WARNI));
    m_levelFilter->addItem("ERROR+", static_cast<int>(ZLOG_ERROR));
    m_levelFilter->addItem("FATAL ONLY", static_cast<int>(ZLOG_FATAL));
    m_levelFilter->addItem("SILENT", static_cast<int>(ZLOG_NONEL));
    m_levelFilter->setMaximumWidth(80);
    m_levelFilter->view()->setMinimumWidth(100);
    m_levelFilter->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    titleBarLayout->addWidget(m_levelFilter);

    auto* vline2 = new QFrame(this);
    vline2->setFrameShape(QFrame::VLine);
    vline2->setFrameShadow(QFrame::Sunken);
    titleBarLayout->addWidget(vline2);

    QLabel* searchIcon = new QLabel(this);
    QColor iconColor = searchIcon->palette().color(QPalette::WindowText);
    QIcon icon = xTheme::createColorizedIcon(":/qtext/resource/search.svg", iconColor);
    searchIcon->setPixmap(icon.pixmap(16, 16));
    searchIcon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    titleBarLayout->addWidget(searchIcon);
    m_searchBox = new QLineEdit(m_titleBar);
    m_searchBox->setPlaceholderText(tr("Search..."));
    m_searchBox->setMaximumWidth(120);
    titleBarLayout->addWidget(m_searchBox);

    m_autoScroll = new QCheckBox(tr("Auto Scroll"), m_titleBar);
    m_autoScroll->setChecked(true);
    titleBarLayout->addWidget(m_autoScroll);

    m_clearButton = new QPushButton(tr("Clear"), m_titleBar);
    QPalette pal = m_clearButton->palette();
    QColor textColor = pal.color(QPalette::ButtonText);
    QIcon clearIcon(xTheme::createColorizedIcon(":/qtext/resource/clear.svg", textColor));
    m_clearButton->setIcon(clearIcon);
    titleBarLayout->addWidget(m_clearButton);
}

// 线程安全的添加日志接口
void xLogView::appendLog(xLogLevel level, const QString& logText) {
    xLogItem item;
    item.level = level;
    item.text = logText;
    item.time = QTime::currentTime();

    QMutexLocker locker(&m_logMutex);
    m_pendingLogs.append(std::move(item));

    // 保护：防止主线程太卡导致 pendingLogs 无限增长
    if (m_pendingLogs.size() > m_maxLines) {
        m_pendingLogs.removeFirst();
    }
}

// 定时器触发 UI 更新
void xLogView::onUpdateTimer() {
    QList<xLogItem> batch;
    {
        QMutexLocker locker(&m_logMutex);
        if (m_pendingLogs.isEmpty()) return;
        batch.swap(m_pendingLogs);
        m_pendingLogs.clear();
    }

    // 批量添加到 Model
    // 此时运行在主线程，可以安全操作 Model
    m_model->appendLogs(batch);

    // --- 智能滚动 ---
    if (m_autoScroll && m_autoScroll->isChecked()) {
        // 检查滚动条是否在底部（允许小误差，避免浮点数精度问题）
        QScrollBar* scrollBar = m_listView->verticalScrollBar();
        bool isAtBottom = (scrollBar->value() >= scrollBar->maximum() - 1);
        
        if (isAtBottom) {
            // 在底部时才自动滚动
            m_listView->scrollToBottom();
        } else {
            // 不在底部，说明用户手动滚动了，取消自动滚动
            // 使用 blockSignals 避免触发 setAutoScroll 槽函数
            m_autoScroll->blockSignals(true);
            m_autoScroll->setChecked(false);
            m_autoScroll->blockSignals(false);
        }
    }
}

void xLogView::applyFilter() {
    if (!m_proxyModel) return;
    int levelIdx = m_levelFilter->currentData().toInt();
    m_proxyModel->setMinLevel(levelIdx);
    m_proxyModel->setSearchText(m_searchBox->text());
}

void xLogView::onClearLog() {
    if (m_model) m_model->clear();
}

void xLogView::setAutoScroll(bool enabled) {
    if (enabled) m_listView->scrollToBottom();
}

void xLogView::onScrollBarValueChanged(int value) {
    Q_UNUSED(value);
    
    // 当用户手动拖动滚动条时，检查是否在底部
    QScrollBar* scrollBar = m_listView->verticalScrollBar();
    bool isAtBottom = (scrollBar->value() >= scrollBar->maximum() - 1);
    
    if (!isAtBottom) {
        // 如果不在底部且自动滚动是开启的，则取消自动滚动
        if (m_autoScroll && m_autoScroll->isChecked()) {
            // 使用 blockSignals 避免触发 setAutoScroll 槽函数
            m_autoScroll->blockSignals(true);
            m_autoScroll->setChecked(false);
            m_autoScroll->blockSignals(false);
        }
    } else {
        // 如果滚动到底部且自动滚动是关闭的，自动恢复自动滚动
        if (m_autoScroll && !m_autoScroll->isChecked()) {
            // 使用 blockSignals 避免触发 setAutoScroll 槽函数
            m_autoScroll->blockSignals(true);
            m_autoScroll->setChecked(true);
            m_autoScroll->blockSignals(false);
            // 立即滚动到底部
            m_listView->scrollToBottom();
        }
    }
}
 