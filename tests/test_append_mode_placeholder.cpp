// ***************************************************************
//  GTest coverage: xAbstractTableModel 追加模式下往占位行提交空值
// ***************************************************************
//
// 追加模式在表尾放一行 "* Click to add a new item..." 占位行，往它提交值就新增一行。
// 以前不看提交的是什么：在占位行打开编辑器后什么都不填就回车、点到别的格子
// （QAbstractItemView::currentChanged 会先提交编辑器），都会插进一行空行。
//
// 现在"空"值——无效 QVariant、空或全空白的 QString、空或各项都是空白的 QStringList——
// 提交到占位行什么都不做；有内容的提交在任意列照旧新增一行，普通行照旧可以写成空值。
// 模型层直接调 setData()；界面层走真实的 xTableView / xItemDelegate / xTableStringListEditor。
#include "xTableEditor.h"
#include "xTableView.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QTest>

#include <memory>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

// 断言失败时按 qDebug 的格式打印 QVariant（GTest 经 ADL 找到它，必须在全局命名空间）。
void PrintTo(const QVariant& value, std::ostream* os) {
    QString text;
    QDebug(&text).nospace() << value;
    *os << text.toStdString();
}

namespace {

void ensureApplication() {
    if (QApplication::instance() != nullptr) {
        return;
    }
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    static int argc = 1;
    static char arg0[] = "test_append_mode_placeholder";
    static char* argv[] = {arg0, nullptr};
    static QApplication app(argc, argv);
}

// 两列的最小追加模式模型，记下每次 insertNewBaseRow 收到的值。
class AppendTableModel : public xAbstractTableModel {
  public:
    explicit AppendTableModel(int row_count, bool string_list_first_column = false)
        : string_list_first_column_(string_list_first_column) {
        for (int row = 0; row < row_count; ++row) {
            rows_.push_back({QStringLiteral("r%1").arg(row), QVariant()});
        }
        setAppendMode(true, 0);
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : kColumnCount;
    }

    int dataRowCount() const { return baseRowCount(); }

    QVariant cell(int row, int column) const { return rows_.at(row).at(column); }

    const QVariantList& insertedValues() const { return inserted_values_; }

  protected:
    int baseRowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : static_cast<int>(rows_.size());
    }

    QVariant baseData(const QModelIndex& index, int role) const override {
        // 同 xRto 同伦法页的名称列：第 0 列用带"…"按钮的字符串列表编辑器。
        // 这两个角色在占位行上也会问到 baseData。
        if (string_list_first_column_ && index.column() == 0) {
            if (role == xTableView::StringListEditRole) {
                return true;
            }
            if (role == xTableView::StringListDialogFactoryRole) {
                xTableView::StringListDialogFactory factory =
                    [](QWidget*, const QStringList&) -> std::optional<QStringList> {
                    return std::nullopt;
                };
                return QVariant::fromValue(factory);
            }
        }
        if (index.row() >= rows_.size()) {
            return {};
        }
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            return rows_.at(index.row()).at(index.column());
        }
        return {};
    }

    Qt::ItemFlags baseFlags(const QModelIndex&) const override {
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    }

    bool baseSetData(const QModelIndex& index, const QVariant& value, int role) override {
        if (role != Qt::EditRole || index.row() >= rows_.size()) {
            return false;
        }
        rows_[index.row()][index.column()] = value;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }

    bool insertNewBaseRow(int row, const QVariant& value) override {
        inserted_values_.push_back(value);
        rows_.insert(row, QVariantList(kColumnCount));
        return true;
    }

  private:
    static constexpr int kColumnCount = 2;

    QList<QVariantList> rows_;
    QVariantList inserted_values_;
    bool string_list_first_column_ = false;
};

struct NamedValue {
    const char* name;
    QVariant value;
};

std::vector<NamedValue> emptyValues() {
    return {
        {"invalid QVariant", QVariant()},
        {"null QString", QVariant(QString())},
        {"empty QString", QVariant(QStringLiteral(""))},
        {"spaces", QVariant(QStringLiteral("   "))},
        {"tab and line breaks", QVariant(QStringLiteral("\t\r\n"))},
        {"empty QStringList", QVariant(QStringList())},
        {"QStringList of blank items", QVariant(QStringList{QString(), QStringLiteral("  ")})},
    };
}

std::vector<NamedValue> nonEmptyValues() {
    return {
        {"QString", QVariant(QStringLiteral("x"))},
        {"QString with padding", QVariant(QStringLiteral("  x  "))},
        {"QStringList", QVariant(QStringList{QStringLiteral("a")})},
        {"QStringList with a blank item", QVariant(QStringList{QString(), QStringLiteral("a")})},
        {"int 0", QVariant(0)},
        {"bool false", QVariant(false)},
        {"double 0.0", QVariant(0.0)},
    };
}

class AppendModePlaceholder : public ::testing::Test {
  protected:
    static void SetUpTestSuite() { ensureApplication(); }
};

TEST_F(AppendModePlaceholder, EmptyCommitAddsNoRowInAnyColumn) {
    for (const NamedValue& empty : emptyValues()) {
        for (int column = 0; column < 2; ++column) {
            SCOPED_TRACE(::testing::Message() << empty.name << ", column " << column);
            AppendTableModel model(1);
            int inserted_signals = 0;
            QObject::connect(&model, &QAbstractItemModel::rowsInserted,
                             [&inserted_signals] { ++inserted_signals; });

            EXPECT_FALSE(model.setData(model.index(1, column), empty.value, Qt::EditRole));

            EXPECT_EQ(model.dataRowCount(), 1);
            EXPECT_EQ(model.rowCount(), 2);  // 1 行数据 + 占位行
            EXPECT_TRUE(model.insertedValues().isEmpty());
            EXPECT_EQ(inserted_signals, 0);
        }
    }
}

TEST_F(AppendModePlaceholder, NonEmptyCommitAddsOneRowInAnyColumn) {
    for (const NamedValue& non_empty : nonEmptyValues()) {
        for (int column = 0; column < 2; ++column) {
            SCOPED_TRACE(::testing::Message() << non_empty.name << ", column " << column);
            AppendTableModel model(1);
            std::vector<std::pair<int, int>> inserted_ranges;
            QObject::connect(&model, &QAbstractItemModel::rowsInserted,
                             [&inserted_ranges](const QModelIndex&, int first, int last) {
                                 inserted_ranges.emplace_back(first, last);
                             });

            EXPECT_TRUE(model.setData(model.index(1, column), non_empty.value, Qt::EditRole));

            ASSERT_EQ(model.dataRowCount(), 2);
            EXPECT_EQ(model.rowCount(), 3);  // 占位行挪到了第 2 行
            EXPECT_EQ(model.insertedValues(), QVariantList{non_empty.value});
            EXPECT_EQ(model.cell(1, column), non_empty.value);
            EXPECT_EQ(inserted_ranges, (std::vector<std::pair<int, int>>{{1, 1}}));
        }
    }
}

TEST_F(AppendModePlaceholder, RegularRowStillAcceptsEmptyValues) {
    for (const NamedValue& empty : emptyValues()) {
        SCOPED_TRACE(empty.name);
        AppendTableModel model(1);
        ASSERT_TRUE(model.setData(model.index(0, 1), QStringLiteral("old"), Qt::EditRole));

        EXPECT_TRUE(model.setData(model.index(0, 1), empty.value, Qt::EditRole));

        EXPECT_EQ(model.cell(0, 1), empty.value);
        EXPECT_EQ(model.dataRowCount(), 1);
        EXPECT_TRUE(model.insertedValues().isEmpty());
    }
}

// 界面层：真实的 xTableView（带排序过滤代理）+ xItemDelegate。
class AppendModePlaceholderView : public ::testing::Test {
  protected:
    static void SetUpTestSuite() { ensureApplication(); }

    void setUpView(int row_count, bool string_list_first_column = false) {
        model_ = std::make_unique<AppendTableModel>(row_count, string_list_first_column);
        view_ = std::make_unique<xTableView>();
        view_->setSourceModel(model_.get());
        view_->resize(480, 320);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
    }

    // 视图挂的是代理模型，按视图的行列号取索引。
    QModelIndex viewIndex(int row, int column) const {
        return view_->model()->index(row, column);
    }

    template <typename Editor>
    Editor* openEditor(const QModelIndex& index) {
        view_->setCurrentIndex(index);
        view_->edit(index);
        return view_->viewport()->findChild<Editor*>();
    }

    // 回车提交走的是排队调用，处理一下事件队列。
    static void pressReturn(QWidget* widget) {
        QTest::keyClick(widget, Qt::Key_Return);
        QCoreApplication::processEvents();
    }

    // 成员按声明逆序析构：先拆视图，再拆模型。
    std::unique_ptr<AppendTableModel> model_;
    std::unique_ptr<xTableView> view_;
};

TEST_F(AppendModePlaceholderView, ReturnOnUntouchedEditorAddsNoRow) {
    setUpView(1);
    auto* editor = openEditor<QLineEdit>(viewIndex(1, 0));
    ASSERT_NE(editor, nullptr);

    pressReturn(editor);

    EXPECT_FALSE(view_->isEditing());
    EXPECT_EQ(model_->dataRowCount(), 1);
    EXPECT_TRUE(model_->insertedValues().isEmpty());
}

TEST_F(AppendModePlaceholderView, MovingToAnotherCellFromUntouchedEditorAddsNoRow) {
    setUpView(1);
    auto* editor = openEditor<QLineEdit>(viewIndex(1, 1));
    ASSERT_NE(editor, nullptr);

    view_->setCurrentIndex(viewIndex(0, 0));  // currentChanged 会先提交再关闭编辑器

    EXPECT_FALSE(view_->isEditing());
    EXPECT_EQ(model_->dataRowCount(), 1);
    EXPECT_TRUE(model_->insertedValues().isEmpty());
}

TEST_F(AppendModePlaceholderView, TypedTextStillAddsOneRow) {
    setUpView(1);
    auto* editor = openEditor<QLineEdit>(viewIndex(1, 0));
    ASSERT_NE(editor, nullptr);

    QTest::keyClicks(editor, QStringLiteral("abc"));
    pressReturn(editor);

    ASSERT_EQ(model_->dataRowCount(), 2);
    EXPECT_EQ(model_->cell(1, 0), QVariant(QStringLiteral("abc")));
    EXPECT_EQ(model_->insertedValues(), QVariantList{QStringLiteral("abc")});
}

TEST_F(AppendModePlaceholderView, ReturnOnUntouchedStringListEditorAddsNoRow) {
    setUpView(1, /*string_list_first_column=*/true);
    auto* editor = openEditor<xTableStringListEditor>(viewIndex(1, 0));
    ASSERT_NE(editor, nullptr);
    auto* line_edit = editor->findChild<QLineEdit*>();
    ASSERT_NE(line_edit, nullptr);

    pressReturn(line_edit);

    EXPECT_FALSE(view_->isEditing());
    EXPECT_EQ(model_->dataRowCount(), 1);
    EXPECT_TRUE(model_->insertedValues().isEmpty());
}

TEST_F(AppendModePlaceholderView, TypedStringListStillAddsOneRow) {
    setUpView(1, /*string_list_first_column=*/true);
    auto* editor = openEditor<xTableStringListEditor>(viewIndex(1, 0));
    ASSERT_NE(editor, nullptr);
    auto* line_edit = editor->findChild<QLineEdit*>();
    ASSERT_NE(line_edit, nullptr);

    QTest::keyClicks(line_edit, QStringLiteral("a, b"));
    pressReturn(line_edit);

    const QStringList expected{QStringLiteral("a"), QStringLiteral("b")};
    ASSERT_EQ(model_->dataRowCount(), 2);
    EXPECT_EQ(model_->cell(1, 0), QVariant(expected));
    EXPECT_EQ(model_->insertedValues(), QVariantList{QVariant(expected)});
}

TEST_F(AppendModePlaceholderView, DeleteKeyOnPlaceholderCellsAddsNoRow) {
    setUpView(1);
    view_->setCurrentIndex(viewIndex(1, 0));
    view_->selectionModel()->select(QItemSelection(viewIndex(1, 0), viewIndex(1, 1)),
                                    QItemSelectionModel::ClearAndSelect);

    QTest::keyClick(view_.get(), Qt::Key_Delete);  // 对选中的格子逐个 setData(QVariant())

    EXPECT_EQ(model_->dataRowCount(), 1);
    EXPECT_TRUE(model_->insertedValues().isEmpty());
}

TEST_F(AppendModePlaceholderView, PasteSkipsBlankLineOnPlaceholderAndKeepsLaterLines) {
    setUpView(1);
    QApplication::clipboard()->setText(QStringLiteral("a\n\nb"));
    view_->setCurrentIndex(viewIndex(1, 0));

    QTest::keyClick(view_.get(), Qt::Key_V, Qt::ControlModifier);

    ASSERT_EQ(model_->dataRowCount(), 3);
    EXPECT_EQ(model_->cell(1, 0), QVariant(QStringLiteral("a")));
    EXPECT_EQ(model_->cell(2, 0), QVariant(QStringLiteral("b")));
    EXPECT_EQ(model_->insertedValues(),
              (QVariantList{QStringLiteral("a"), QStringLiteral("b")}));
}

TEST_F(AppendModePlaceholderView, PasteOverRegularRowsStillWritesBlankLine) {
    setUpView(3);
    QApplication::clipboard()->setText(QStringLiteral("p\n\nq"));
    view_->setCurrentIndex(viewIndex(0, 0));

    QTest::keyClick(view_.get(), Qt::Key_V, Qt::ControlModifier);

    EXPECT_EQ(model_->dataRowCount(), 3);
    EXPECT_EQ(model_->cell(0, 0), QVariant(QStringLiteral("p")));
    EXPECT_TRUE(model_->cell(1, 0).isValid());
    EXPECT_EQ(model_->cell(1, 0).toString(), QString());
    EXPECT_EQ(model_->cell(2, 0), QVariant(QStringLiteral("q")));
    EXPECT_TRUE(model_->insertedValues().isEmpty());
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
