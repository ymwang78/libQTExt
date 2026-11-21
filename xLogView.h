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
    QString project;  // 来源项目（日志首行的「[项目名] 」前缀），空串 = 不属于任何项目
};

// --- 1. 自定义数据模型 ---
class xLogModel : public QAbstractListModel {
    Q_OBJECT
  public:
    enum LogRoles { LevelRole = Qt::UserRole + 1, TimeRole, ProjectRole };

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
    // 只显示该项目的日志；空串 = 不按项目过滤
    void setProject(const QString& project);

  protected:
    // 决定某一行是否显示的核心函数
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

  private:
    int m_minLevel = 0;
    QString m_searchText;
    QString m_project;
};

class xLogView : public QWidget {
    Q_OBJECT

  public:

    explicit xLogView(int maxLines = 2000, QWidget* parent = nullptr);
    ~xLogView();
    void appendLog(ZLOG_LEVEL level, const QString& logText);
    void clear();

    // 当前等级过滤门限（ZLOG_LEVEL 数值，低于该值的日志被隐藏）
    int currentLevel() const;
    // 以编程方式设置等级过滤门限，不会触发 logLevelChanged（用于从工程配置同步初值）
    void setLevel(int level);
    QString selectedLogsText() const;
    QString allVisibleLogsText() const;
    void copySelectedToClipboard();
    void copyAllToClipboard();
    void selectAllLogs();
    QListView* listView() const { return m_listView; }

    // 按项目过滤：开启后标题栏出现项目下拉框（默认关闭，不开启时界面与行为都不变）。
    // 日志首行以「[项目名] 」开头即归到该项目，同一次 appendLog 的后续行跟随首行；
    // 下拉框在日志里出现新项目时自动增补，首项「全部项目」不过滤。
    void setProjectFilterEnabled(bool enabled);
    bool isProjectFilterEnabled() const { return m_projectFilterEnabled; }
    // 下拉框里的项目名（不含「全部项目」），按字母序、不分大小写
    QStringList projects() const;
    // 当前选中的项目；空串 = 全部项目
    QString currentProject() const;
    // 以编程方式选中项目（不在列表里就先加进去）；空串 = 全部项目
    void setCurrentProject(const QString& project);
    // 取一行日志的项目前缀「[项目名] 」里的项目名，没有前缀时返回空串
    static QString projectTagOf(const QString& line);

  signals:
    // 用户在界面上修改等级过滤门限时发出（编程调用 setLevel 不触发）
    void logLevelChanged(int level);

  private slots:
    void onUpdateTimer();  // 定时器槽函数
    void onLevelFilterChanged();  // 用户切换等级下拉框
    void onProjectFilterChanged();  // 用户切换项目下拉框
    void applyFilter();
    void onClearLog();
    void setAutoScroll(bool enabled);
    void onContextMenuRequested(const QPoint& pos);
    void onScrollBarActionTriggered(int action);  // 用户操作滚动条（不响应程序调用）

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

  private:
    void setupUI();
    void setupTitleBar();
    void retranslateUi();
    // 把项目加进下拉框（已有则不动），保持字母序和当前选中项
    void addProject(const QString& project);
    // 清空日志时把下拉框收回到「全部项目」+ 当前选中的项目
    void resetProjectList();

    int m_maxLines;
    // UI 组件
    QFrame* m_titleBar;
    QFrame* m_projectSeparator;
    QComboBox* m_projectFilter;
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

    // 自动滚动状态：true = 当前应自动滚动（滚动条在底部）
    bool m_autoScrollEnabled = true;

    // 为 true 时抑制 logLevelChanged（区分 setLevel 的编程调用与用户操作）
    bool m_suppressLevelSignal = false;

    bool m_projectFilterEnabled = false;
};
