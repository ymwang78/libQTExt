#include "xItemDelegate.h"
#include "xTableView.h"
#include <gtest/gtest.h>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>
#include <memory>

namespace {
class ComboCommit : public ::testing::Test {
  protected:
    void SetUp() override {
        if (QApplication::instance()) return;
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
        static int argc = 1;
        static char name[] = "test_combo_commit";
        static char* argv[] = {name, nullptr};
        static QApplication app(argc, argv);
    }
};
TEST_F(ComboCommit, InitializationDoesNotCommit) {
    QStandardItemModel model(1, 1);
    auto index = model.index(0, 0);
    model.setData(index, QStringList{"OFF", "ON"}, xTableView::ComboBoxItemsRole);
    model.setData(index, QStringLiteral("ON"));
    xItemDelegate delegate;
    QWidget parent;
    std::unique_ptr<QWidget> editor(delegate.createEditor(&parent, {}, index));
    auto* combo = qobject_cast<QComboBox*>(editor.get());
    ASSERT_NE(combo, nullptr);
    QSignalSpy committed(&delegate, &QAbstractItemDelegate::commitData);
    delegate.setEditorData(editor.get(), index);
    combo->setCurrentIndex(0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    EXPECT_EQ(committed.count(), 0);
}
TEST_F(ComboCommit, KeyboardSelectionCommitsWithoutFocusLoss) {
    QStandardItemModel model(1, 1);
    auto index = model.index(0, 0);
    model.setData(index, QStringList{"OFF", "ON"}, xTableView::ComboBoxItemsRole);
    model.setData(index, QStringLiteral("OFF"));
    xItemDelegate delegate;
    QWidget parent;
    std::unique_ptr<QWidget> editor(delegate.createEditor(&parent, {}, index));
    auto* combo = qobject_cast<QComboBox*>(editor.get());
    ASSERT_NE(combo, nullptr);
    delegate.setEditorData(editor.get(), index);
    QSignalSpy closed(&delegate, &QAbstractItemDelegate::closeEditor);
    int submissions = 0;
    QObject::connect(&delegate, &QAbstractItemDelegate::commitData, &parent, [&](QWidget* widget) {
        ++submissions;
        delegate.setModelData(widget, &model, index);
    });
    QTest::keyClick(combo, Qt::Key_Down);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    EXPECT_EQ(submissions, 1);
    EXPECT_EQ(index.data().toString(), QStringLiteral("ON"));
    EXPECT_EQ(closed.count(), 1);
}
TEST_F(ComboCommit, DeletionDuringConfirmationIsSafe) {
    QStandardItemModel model(1, 1);
    auto index = model.index(0, 0);
    model.setData(index, QStringList{"OFF", "ON"}, xTableView::ComboBoxItemsRole);
    model.setData(index, QStringLiteral("OFF"));
    xItemDelegate delegate;
    QWidget parent;
    QWidget* editor = delegate.createEditor(&parent, {}, index);
    auto* combo = qobject_cast<QComboBox*>(editor);
    ASSERT_NE(combo, nullptr);
    delegate.setEditorData(editor, index);
    QSignalSpy closed(&delegate, &QAbstractItemDelegate::closeEditor);
    QObject::connect(&delegate, &QAbstractItemDelegate::commitData, &parent,
                     [](QWidget* widget) { delete widget; });
    QTest::keyClick(combo, Qt::Key_Down);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    EXPECT_EQ(closed.count(), 0);
    EXPECT_EQ(index.data().toString(), QStringLiteral("OFF"));
}
}  // namespace
