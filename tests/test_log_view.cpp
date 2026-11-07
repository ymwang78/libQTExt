#include "xLogView.h"
#include <gtest/gtest.h>
#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QListView>
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

}  // namespace
