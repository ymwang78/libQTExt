#include "xLogView.h"
#include <gtest/gtest.h>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QListView>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTest>

namespace {

class LogViewTest : public ::testing::Test {
  protected:
    void SetUp() override {
        if (QApplication::instance()) return;
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
        static int argc = 1;
        static char name[] = "test_log_view";
        static char* argv[] = {name, nullptr};
        static QApplication app(argc, argv);
    }
};

TEST_F(LogViewTest, SupportsExtendedMultiRowSelectionAndCopy) {
    xLogView logView(100);
    ASSERT_NE(logView.listView(), nullptr);
    EXPECT_EQ(logView.listView()->selectionMode(), QAbstractItemView::ExtendedSelection);
    EXPECT_EQ(logView.listView()->contextMenuPolicy(), Qt::CustomContextMenu);

    logView.appendLog(ZLOG_INFOR, QStringLiteral("First line\nSecond line\nThird line"));
    QTest::qWait(150);

    QAbstractItemModel* model = logView.listView()->model();
    ASSERT_NE(model, nullptr);
    ASSERT_EQ(model->rowCount(), 3);

    QItemSelectionModel* sel = logView.listView()->selectionModel();
    ASSERT_NE(sel, nullptr);

    // Select row 2 then row 0 out of order; selectedLogsText() should return sorted order (0, 2).
    sel->select(model->index(2, 0), QItemSelectionModel::Select);
    sel->select(model->index(0, 0), QItemSelectionModel::Select);

    const QString selectedText = logView.selectedLogsText();
    const QStringList selectedLines = selectedText.split(QLatin1Char('\n'));
    ASSERT_EQ(selectedLines.size(), 2);
    EXPECT_TRUE(selectedLines[0].endsWith(QStringLiteral("First line")));
    EXPECT_TRUE(selectedLines[1].endsWith(QStringLiteral("Third line")));

    if (QClipboard* clipboard = QApplication::clipboard()) {
        clipboard->clear();
        logView.copySelectedToClipboard();
        EXPECT_EQ(clipboard->text(), selectedText);

        logView.copyAllToClipboard();
        const QStringList allLines = clipboard->text().split(QLatin1Char('\n'));
        ASSERT_EQ(allLines.size(), 3);
        EXPECT_TRUE(allLines[0].endsWith(QStringLiteral("First line")));
        EXPECT_TRUE(allLines[1].endsWith(QStringLiteral("Second line")));
        EXPECT_TRUE(allLines[2].endsWith(QStringLiteral("Third line")));
    }

    logView.selectAllLogs();
    EXPECT_EQ(sel->selectedIndexes().size(), 3);
}

/// 可见行的正文（去掉「[时间] [级别] 」那段头），按显示顺序
QStringList visibleTexts(const xLogView& logView) {
    QStringList texts;
    const QAbstractItemModel* model = logView.listView()->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QString line = model->index(row, 0).data(Qt::DisplayRole).toString();
        // DisplayRole 形如 "[HH:mm:ss] [LEVEL] text"，第二个 "] " 之后是正文
        const int levelEnd =
            line.indexOf(QStringLiteral("] "), line.indexOf(QStringLiteral("] ")) + 2);
        texts.append(line.mid(levelEnd + 2));
    }
    return texts;
}

QComboBox* projectCombo(xLogView& logView) {
    for (QComboBox* combo : logView.findChildren<QComboBox*>()) {
        if (combo->itemData(0).typeId() == QMetaType::QString) return combo;
    }
    return nullptr;
}

QComboBox* levelCombo(xLogView& logView) {
    for (QComboBox* combo : logView.findChildren<QComboBox*>()) {
        if (combo->itemData(0).typeId() == QMetaType::Int) return combo;
    }
    return nullptr;
}

TEST_F(LogViewTest, LevelFilterFitsAllItemTextsWithoutClippingAndHasThresholdTooltip) {
    xLogView logView(100);
    logView.resize(480, 200);
    logView.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&logView));

    QComboBox* combo = levelCombo(logView);
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->sizeAdjustPolicy(), QComboBox::AdjustToContents);
    EXPECT_EQ(combo->view()->textElideMode(), Qt::ElideNone);
    EXPECT_FALSE(combo->toolTip().isEmpty());
    EXPECT_GE(combo->width(), combo->sizeHint().width());

    QStyleOptionComboBox opt;
    opt.initFrom(combo);
    const QRect editField =
        combo->style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, combo);
    ASSERT_TRUE(editField.isValid());

    const QFontMetrics fm = combo->fontMetrics();
    for (int i = 0; i < combo->count(); ++i) {
        const QString text = combo->itemText(i);
        EXPECT_LE(fm.horizontalAdvance(text), editField.width())
            << "Level item clipped at index " << i << ": " << text.toStdString();
    }
}

TEST_F(LogViewTest, ProjectTagIsTheLeadingBracketedNameFollowedBySpaceOrEnd) {
    EXPECT_EQ(xLogView::projectTagOf(QStringLiteral("[Host1-MPC] step done")),
              QStringLiteral("Host1-MPC"));
    EXPECT_EQ(xLogView::projectTagOf(QStringLiteral("[Host1-MPC]")), QStringLiteral("Host1-MPC"));
    EXPECT_EQ(xLogView::projectTagOf(QStringLiteral("[ 反应器 A ] 已启动")),
              QStringLiteral("反应器 A"));

    EXPECT_TRUE(xLogView::projectTagOf(QStringLiteral("step done [Host1-MPC]")).isEmpty());
    EXPECT_TRUE(xLogView::projectTagOf(QStringLiteral("[Host1-MPC]step done")).isEmpty());
    EXPECT_TRUE(xLogView::projectTagOf(QStringLiteral("[] empty")).isEmpty());
    EXPECT_TRUE(xLogView::projectTagOf(QStringLiteral("[   ] blank")).isEmpty());
    EXPECT_TRUE(xLogView::projectTagOf(QStringLiteral("[unterminated text")).isEmpty());
    EXPECT_TRUE(xLogView::projectTagOf(QStringLiteral("[%1] x").arg(QString(129, QLatin1Char('a'))))
                    .isEmpty());
    EXPECT_EQ(xLogView::projectTagOf(QStringLiteral("[%1] x").arg(QString(128, QLatin1Char('a')))),
              QString(128, QLatin1Char('a')));
}

TEST_F(LogViewTest, ListsWorkspaceProjectsBeforeTheirFirstLogAndAfterClear) {
    xLogView logView(100);
    logView.setProjectFilterEnabled(true);
    logView.setKnownProjects({QStringLiteral("MySim"), QStringLiteral("ktrace"),
                              QStringLiteral("MySim")});
    EXPECT_EQ(logView.projects(), (QStringList{QStringLiteral("ktrace"),
                                              QStringLiteral("MySim")}));
    logView.setCurrentProject(QStringLiteral("MySim"));
    logView.appendLog(ZLOG_WARNI, QStringLiteral("KeelSim[MySim]: Delete source"));
    logView.appendLog(ZLOG_INFOR, QStringLiteral("KeelTrace[ktrace]: Ready"));
    logView.appendLog(ZLOG_INFOR, QStringLiteral("Framework ready"));
    QTest::qWait(150);
    EXPECT_EQ(visibleTexts(logView), QStringList{QStringLiteral("KeelSim[MySim]: Delete source")});

    logView.setKnownProjects({QStringLiteral("MySim"), QStringLiteral("ksim")});
    EXPECT_EQ(logView.currentProject(), QStringLiteral("MySim"));
    EXPECT_TRUE(logView.projects().contains(QStringLiteral("ktrace")));
    logView.clear();
    EXPECT_EQ(logView.projects(), (QStringList{QStringLiteral("ksim"),
                                              QStringLiteral("MySim")}));
    EXPECT_EQ(logView.currentProject(), QStringLiteral("MySim"));
    logView.setKnownProjects({QStringLiteral("ksim")});
    EXPECT_TRUE(logView.currentProject().isEmpty());
    EXPECT_EQ(logView.projects(), QStringList{QStringLiteral("ksim")});
}

TEST_F(LogViewTest, ProjectFilterIsHiddenAndInactiveUntilEnabled) {
    xLogView logView(100);
    QComboBox* combo = projectCombo(logView);
    ASSERT_NE(combo, nullptr);
    EXPECT_FALSE(logView.isProjectFilterEnabled());
    EXPECT_TRUE(combo->isHidden());

    logView.appendLog(ZLOG_INFOR, QStringLiteral("[P1] one"));
    logView.appendLog(ZLOG_INFOR, QStringLiteral("framework"));
    QTest::qWait(150);
    // 关闭时也收集项目，但选了项目也不过滤
    EXPECT_EQ(logView.projects(), QStringList{QStringLiteral("P1")});
    logView.setCurrentProject(QStringLiteral("P1"));
    EXPECT_EQ(visibleTexts(logView).size(), 2);

    logView.setProjectFilterEnabled(true);
    EXPECT_FALSE(combo->isHidden());
    EXPECT_EQ(visibleTexts(logView), QStringList{QStringLiteral("[P1] one")});
}

TEST_F(LogViewTest, SelectingAProjectShowsOnlyItsLinesIncludingContinuations) {
    xLogView logView(100);
    logView.setLevel(ZLOG_TRACE);
    logView.setProjectFilterEnabled(true);

    logView.appendLog(ZLOG_INFOR, QStringLiteral("[Beta] started"));
    logView.appendLog(ZLOG_WARNI, QStringLiteral("[Alpha] header\n  detail 1\n  detail 2"));
    logView.appendLog(ZLOG_INFOR, QStringLiteral("framework message"));
    logView.appendLog(ZLOG_ERROR, QStringLiteral("[beta2] failed"));
    QTest::qWait(150);

    // 字母序、不分大小写；首项「全部项目」不在 projects() 里
    EXPECT_EQ(logView.projects(), (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta"),
                                               QStringLiteral("beta2")}));
    EXPECT_TRUE(logView.currentProject().isEmpty());
    EXPECT_EQ(visibleTexts(logView).size(), 6);

    logView.setCurrentProject(QStringLiteral("Alpha"));
    EXPECT_EQ(visibleTexts(logView),
              (QStringList{QStringLiteral("[Alpha] header"), QStringLiteral("  detail 1"),
                           QStringLiteral("  detail 2")}));
    EXPECT_EQ(logView.allVisibleLogsText().split(QLatin1Char('\n')).size(), 3);

    // 项目与等级、搜索叠加生效
    logView.setCurrentProject(QStringLiteral("Beta"));
    EXPECT_EQ(visibleTexts(logView), QStringList{QStringLiteral("[Beta] started")});
    logView.setLevel(ZLOG_WARNI);
    EXPECT_TRUE(visibleTexts(logView).isEmpty());

    logView.setLevel(ZLOG_TRACE);
    logView.setCurrentProject(QString());
    EXPECT_EQ(visibleTexts(logView).size(), 6);
}

TEST_F(LogViewTest, UserPickingAProjectInTheComboFiltersAndNewProjectsKeepTheSelection) {
    xLogView logView(100);
    logView.setProjectFilterEnabled(true);
    QComboBox* combo = projectCombo(logView);
    ASSERT_NE(combo, nullptr);

    logView.appendLog(ZLOG_INFOR, QStringLiteral("[M] m1"));
    QTest::qWait(150);
    ASSERT_EQ(combo->count(), 2);
    combo->setCurrentIndex(1);
    EXPECT_EQ(logView.currentProject(), QStringLiteral("M"));
    EXPECT_EQ(visibleTexts(logView), QStringList{QStringLiteral("[M] m1")});

    // 新项目插在当前项之前，当前选中的项目不变，过滤也不变
    logView.appendLog(ZLOG_INFOR, QStringLiteral("[A] a1"));
    logView.appendLog(ZLOG_INFOR, QStringLiteral("[M] m2"));
    QTest::qWait(150);
    EXPECT_EQ(logView.projects(), (QStringList{QStringLiteral("A"), QStringLiteral("M")}));
    EXPECT_EQ(logView.currentProject(), QStringLiteral("M"));
    EXPECT_EQ(visibleTexts(logView),
              (QStringList{QStringLiteral("[M] m1"), QStringLiteral("[M] m2")}));
}

TEST_F(LogViewTest, ClearDropsStaleProjectsButKeepsTheSelectedOne) {
    xLogView logView(100);
    logView.setProjectFilterEnabled(true);
    logView.appendLog(ZLOG_INFOR, QStringLiteral("[A] a1"));
    logView.appendLog(ZLOG_INFOR, QStringLiteral("[B] b1"));
    QTest::qWait(150);
    logView.setCurrentProject(QStringLiteral("B"));

    logView.clear();
    EXPECT_EQ(logView.projects(), QStringList{QStringLiteral("B")});
    EXPECT_EQ(logView.currentProject(), QStringLiteral("B"));

    logView.appendLog(ZLOG_INFOR, QStringLiteral("[A] a2"));
    logView.appendLog(ZLOG_INFOR, QStringLiteral("[B] b2"));
    QTest::qWait(150);
    EXPECT_EQ(visibleTexts(logView), QStringList{QStringLiteral("[B] b2")});

    logView.setCurrentProject(QString());
    logView.clear();
    EXPECT_TRUE(logView.projects().isEmpty());
    EXPECT_TRUE(logView.currentProject().isEmpty());
}

}  // namespace
