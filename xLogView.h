#pragma once
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
#include <QWidget>
#include <QListView>
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QMutex>
#include <QTime>
#include <QList>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>

#ifndef ZCE_DEFINED_LOGLEVEL
typedef enum _zlog_level {
    ZLOG_TRACE = 0, /* trace */
    ZLOG_DEBUG = 1, /* debug */
    ZLOG_INFOR = 2, /* info */
    ZLOG_WARNI = 3, /* warn */
    ZLOG_ERROR = 4, /* error */
    ZLOG_FATAL = 5, /* fatal */
    ZLOG_BIZDT = 6, /* bizdata */
    ZLOG_NONEL = 7, /* none */
} ZLOG_LEVEL;
#    define ZCE_DEFINED_LOGLEVEL
#endif

typedef ZLOG_LEVEL xLogLevel;

// 定义日志结构
struct xLogItem {
    xLogLevel level;
    QTime time;
    QString text;
};

// --- 1. 自定义数据模型 ---
class xLogModel : public QAbstractListModel {
    Q_OBJECT
  public:
    enum LogRoles { LevelRole = Qt::UserRole + 1, TimeRole };

    explicit xLogModel(int maxLines, QObject* parent = nullptr);

    // 必须实现的虚函数
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    // 自定义接口
    void appendLogs(const QList<xLogItem>& newLogs);  // 批量添加
    void clear();
    void refreshThemeColors();  // 刷新主题颜色（当主题切换时调用）

  private:
    QList<xLogItem> m_data;
    const int m_maxLines;

    // 辅助函数：获取颜色
    QColor getLevelColor(int level) const;
    QString getLevelString(int level) const;
};

// --- 2. 自定义过滤代理模型 ---
class xLogFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
  public:
    explicit xLogFilterProxy(QObject* parent = nullptr);

    void setMinLevel(int level);
    void setSearchText(const QString& text);

  protected:
    // 决定某一行是否显示的核心函数
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

  private:
    int m_minLevel = 0;
    QString m_searchText;
};

class xLogView : public QWidget {
    Q_OBJECT

  public:

    explicit xLogView(int maxLines = 2000, QWidget* parent = nullptr);
    ~xLogView();
    void appendLog(ZLOG_LEVEL level, const QString& logText);
    void clear();

  private slots:
    void onUpdateTimer();  // 定时器槽函数
    void applyFilter();
    void onClearLog();
    void setAutoScroll(bool enabled);
    void onScrollBarValueChanged(int value);  // 滚动条位置变化

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void setupUI();
    void setupTitleBar();

    int m_maxLines;
    // UI 组件
    QFrame* m_titleBar;
    QComboBox* m_levelFilter;
    QLineEdit* m_searchBox;
    QCheckBox* m_autoScroll;
    QPushButton* m_clearButton;
    QListView* m_listView;
    xLogModel* m_model;
    xLogFilterProxy* m_proxyModel;

    // 缓冲机制
    QTimer* m_updateTimer;
    QList<xLogItem> m_pendingLogs;  // 待处理日志缓冲区
    QMutex m_logMutex;             // 保护缓冲区的锁
};
