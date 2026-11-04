#include "gridview.h"

#include "formula.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

// ===========================================================================
// GridModel: the parsed table plus an in-session overlay of edits/formulas.
// It is also a Formula::Sheet so cells can reference one another.
// ===========================================================================

namespace {
constexpr int kPad = 2; // spare empty rows/columns for scratch formulas

qint64 packKey(int row, int col)
{
    return (qint64(row) << 32) | quint32(col);
}

// A number parsed in the C locale (so "1.5" works regardless of system
// settings); returns false for text that is not purely a number.
bool asNumber(const QString &text, double *out)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return false;
    bool ok = false;
    const double d = t.toDouble(&ok);
    if (ok)
        *out = d;
    return ok;
}
} // namespace

// A Formula::Sheet facade over the model, kept separate so the model can
// keep QAbstractTableModel's rowCount(QModelIndex) without colliding with the
// Sheet's no-argument rowCount().
class GridModel;
class GridSheet : public Formula::Sheet
{
public:
    explicit GridSheet(const GridModel *model)
        : m_model(model)
    {
    }
    int rowCount() const override;
    int columnCount() const override;
    Formula::Value cellValue(int row, int column) const override;

private:
    const GridModel *m_model;
};

class GridModel : public QAbstractTableModel
{
public:
    explicit GridModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
    }

    // The grid's dimensions and a cell's evaluated value, for GridSheet and
    // for callers that need a value rather than display text.
    int gridRows() const { return m_rowCount; }
    int gridCols() const { return m_colCount; }
    Formula::Value evalCell(int row, int col) const;

    void setGrid(const QList<QStringList> &rows, bool clearOverlay)
    {
        beginResetModel();
        m_data = rows;
        m_dataCols = 0;
        for (const QStringList &r : rows)
            m_dataCols = qMax(m_dataCols, int(r.size()));
        if (clearOverlay) {
            m_overrides.clear();
            m_maxOverrideRow = 0;
            m_maxOverrideCol = 0;
        }
        recomputeCounts();
        endResetModel();
    }

    void setHeaderRow(bool header)
    {
        if (m_header == header)
            return;
        beginResetModel();
        m_header = header;
        recomputeCounts();
        endResetModel();
    }
    bool headerRow() const { return m_header; }
    bool hasHeaderAvailable() const { return !m_data.isEmpty(); }

    QString rawAt(int row, int col) const
    {
        const auto it = m_overrides.constFind(packKey(row, col));
        if (it != m_overrides.constEnd())
            return it.value();
        const int dataRow = row + (m_header ? 1 : 0);
        if (dataRow >= 0 && dataRow < m_data.size() && col >= 0 && col < m_data[dataRow].size())
            return m_data[dataRow][col];
        return QString();
    }

    bool isFormula(int row, int col) const { return rawAt(row, col).startsWith(QLatin1Char('=')); }

    // -- QAbstractTableModel -------------------------------------------------

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rowCount;
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_colCount;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid())
            return QVariant();
        const int row = index.row();
        const int col = index.column();
        const QString raw = rawAt(row, col);

        switch (role) {
        case Qt::DisplayRole:
            if (raw.startsWith(QLatin1Char('=')))
                return evalCell(row, col).toText();
            return raw;
        case Qt::EditRole:
            return raw;
        case Qt::TextAlignmentRole:
            return int(isNumericCell(row, col) ? (Qt::AlignRight | Qt::AlignVCenter)
                                               : (Qt::AlignLeft | Qt::AlignVCenter));
        case Qt::ForegroundRole:
            if (raw.startsWith(QLatin1Char('=')) && evalCell(row, col).isError())
                return QColor(m_errorColor);
            return QVariant();
        case Qt::ToolTipRole:
            if (raw.startsWith(QLatin1Char('='))) {
                const Formula::Value v = evalCell(row, col);
                return QStringLiteral("%1  →  %2").arg(raw, v.toText());
            }
            return raw.isEmpty() ? QVariant() : raw;
        default:
            return QVariant();
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (!index.isValid() || role != Qt::EditRole)
            return false;
        const int row = index.row();
        const int col = index.column();
        const QString newRaw = value.toString();
        if (newRaw == rawAt(row, col))
            return true;

        m_overrides.insert(packKey(row, col), newRaw);

        // Grow the grid if the edit landed in (or past) the spare area.
        int wantRows = qMax(bodyRowCount(), qMax(m_maxOverrideRow, row + 1)) + kPad;
        int wantCols = qMax(m_dataCols, qMax(m_maxOverrideCol, col + 1)) + kPad;
        m_maxOverrideRow = qMax(m_maxOverrideRow, row + 1);
        m_maxOverrideCol = qMax(m_maxOverrideCol, col + 1);
        if (wantRows > m_rowCount) {
            beginInsertRows(QModelIndex(), m_rowCount, wantRows - 1);
            m_rowCount = wantRows;
            endInsertRows();
        }
        if (wantCols > m_colCount) {
            beginInsertColumns(QModelIndex(), m_colCount, wantCols - 1);
            m_colCount = wantCols;
            endInsertColumns();
        }

        // A formula anywhere may depend on this cell, so refresh everything.
        emit dataChanged(this->index(0, 0), this->index(m_rowCount - 1, m_colCount - 1),
                         {Qt::DisplayRole, Qt::ToolTipRole, Qt::ForegroundRole,
                          Qt::TextAlignmentRole});
        return true;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::NoItemFlags;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Vertical) {
            if (role == Qt::DisplayRole)
                return section + 1;
            return QVariant();
        }
        const QString letter = Formula::columnToLetters(section);
        QString name;
        if (m_header && !m_data.isEmpty() && section < m_data.first().size())
            name = m_data.first().at(section);
        if (role == Qt::DisplayRole)
            return (m_header && !name.isEmpty()) ? name : letter;
        if (role == Qt::ToolTipRole)
            return name.isEmpty() ? tr("Column %1").arg(letter)
                                  : tr("Column %1 — %2").arg(letter, name);
        return QVariant();
    }

    void setErrorColor(const QColor &color) { m_errorColor = color; }

private:
    int bodyRowCount() const { return qMax(0, int(m_data.size()) - (m_header ? 1 : 0)); }

    void recomputeCounts()
    {
        m_rowCount = qMax(bodyRowCount(), m_maxOverrideRow) + kPad;
        m_colCount = qMax(m_dataCols, m_maxOverrideCol) + kPad;
    }

    bool isNumericCell(int row, int col) const
    {
        const QString raw = rawAt(row, col);
        if (raw.isEmpty())
            return false;
        if (raw.startsWith(QLatin1Char('=')))
            return evalCell(row, col).type() == Formula::ValueType::Number;
        double d = 0.0;
        return asNumber(raw, &d);
    }

    GridSheet m_sheet{this};
    QList<QStringList> m_data;
    int m_dataCols = 0;
    bool m_header = false;
    QHash<qint64, QString> m_overrides;
    int m_maxOverrideRow = 0;
    int m_maxOverrideCol = 0;
    int m_rowCount = kPad;
    int m_colCount = kPad;
    mutable QSet<qint64> m_evaluating;
    QColor m_errorColor = QColor(QStringLiteral("#ff5555"));
};

Formula::Value GridModel::evalCell(int row, int col) const
{
    if (row < 0 || col < 0)
        return Formula::Value::empty();
    const QString raw = rawAt(row, col);
    if (raw.isEmpty())
        return Formula::Value::empty();
    if (raw.startsWith(QLatin1Char('='))) {
        const qint64 key = packKey(row, col);
        if (m_evaluating.contains(key))
            return Formula::Value::error(QStringLiteral("#CIRC!"));
        m_evaluating.insert(key);
        const Formula::Value v = Formula::evaluate(raw.mid(1), m_sheet);
        m_evaluating.remove(key);
        return v;
    }
    double d = 0.0;
    if (asNumber(raw, &d))
        return Formula::Value::number(d);
    return Formula::Value::text(raw);
}

int GridSheet::rowCount() const
{
    return m_model->gridRows();
}
int GridSheet::columnCount() const
{
    return m_model->gridCols();
}
Formula::Value GridSheet::cellValue(int row, int column) const
{
    return m_model->evalCell(row, column);
}

// ===========================================================================
// GridFilterProxy: per-column filters (AND-ed) and numeric-aware sorting.
// ===========================================================================

class GridFilterProxy : public QSortFilterProxyModel
{
public:
    enum Op { Contains, StartsWith, Equals, NotEquals, Greater, GreaterEqual, Less, LessEqual };

    explicit GridFilterProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
    }

    void setColumnFilter(int column, Op op, const QString &value)
    {
        if (value.isEmpty())
            m_filters.remove(column);
        else
            m_filters.insert(column, {op, value});
        invalidate(); // refilter (invalidate is stable across Qt 6.x)
    }
    void clearColumnFilters()
    {
        m_filters.clear();
        invalidate(); // refilter (invalidate is stable across Qt 6.x)
    }
    bool hasFilters() const { return !m_filters.isEmpty(); }
    QList<int> filteredColumns() const { return m_filters.keys(); }
    QString filterValue(int column) const { return m_filters.value(column).value; }
    Op filterOp(int column) const { return m_filters.value(column).op; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &parent) const override
    {
        for (auto it = m_filters.constBegin(); it != m_filters.constEnd(); ++it) {
            const QModelIndex idx = sourceModel()->index(sourceRow, it.key(), parent);
            const QString text = sourceModel()->data(idx, Qt::DisplayRole).toString();
            if (!matches(text, it.value()))
                return false;
        }
        return true;
    }

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        const QString a = sourceModel()->data(left, Qt::DisplayRole).toString();
        const QString b = sourceModel()->data(right, Qt::DisplayRole).toString();
        double da = 0.0, db = 0.0;
        if (asNumber(a, &da) && asNumber(b, &db))
            return da < db;
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    }

private:
    struct Filter
    {
        Op op = Contains;
        QString value;
    };

    static bool matches(const QString &text, const Filter &f)
    {
        double tn = 0.0, fn = 0.0;
        const bool numeric = asNumber(text, &tn) && asNumber(f.value, &fn);
        switch (f.op) {
        case Contains:
            return text.contains(f.value, Qt::CaseInsensitive);
        case StartsWith:
            return text.startsWith(f.value, Qt::CaseInsensitive);
        case Equals:
            return numeric ? tn == fn : QString::compare(text, f.value, Qt::CaseInsensitive) == 0;
        case NotEquals:
            return numeric ? tn != fn : QString::compare(text, f.value, Qt::CaseInsensitive) != 0;
        case Greater:
            return numeric ? tn > fn : QString::compare(text, f.value, Qt::CaseInsensitive) > 0;
        case GreaterEqual:
            return numeric ? tn >= fn : QString::compare(text, f.value, Qt::CaseInsensitive) >= 0;
        case Less:
            return numeric ? tn < fn : QString::compare(text, f.value, Qt::CaseInsensitive) < 0;
        case LessEqual:
            return numeric ? tn <= fn : QString::compare(text, f.value, Qt::CaseInsensitive) <= 0;
        }
        return true;
    }

    QHash<int, Filter> m_filters;
};

// ===========================================================================
// GridView
// ===========================================================================

GridView::GridView(QWidget *parent)
    : QWidget(parent)
    , m_model(new GridModel(this))
    , m_proxy(new GridFilterProxy(this))
    , m_table(new QTableView(this))
{
    m_proxy->setSourceModel(m_model);
    m_proxy->setDynamicSortFilter(true);
    m_table->setModel(m_proxy);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setCornerButtonEnabled(false);
    m_table->horizontalHeader()->setSectionsMovable(true);
    m_table->horizontalHeader()->setDefaultSectionSize(110);
    m_table->verticalHeader()->setDefaultSectionSize(22);

    // --- top toolbar: delimiter, header, search ---
    auto *toolbar = new QWidget(this);
    auto *toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(6, 4, 6, 4);
    toolLayout->setSpacing(6);

    m_delimiterCombo = new QComboBox(toolbar);
    m_delimiterCombo->addItem(tr("Comma"), int(CsvData::Delimiter::Comma));
    m_delimiterCombo->addItem(tr("Tab"), int(CsvData::Delimiter::Tab));
    m_delimiterCombo->addItem(tr("Semicolon"), int(CsvData::Delimiter::Semicolon));
    m_delimiterCombo->setToolTip(tr("Field separator"));
    toolLayout->addWidget(new QLabel(tr("Separator:"), toolbar));
    toolLayout->addWidget(m_delimiterCombo);

    m_headerCheck = new QCheckBox(tr("First row is header"), toolbar);
    toolLayout->addWidget(m_headerCheck);

    m_filterButton = new QToolButton(toolbar);
    m_filterButton->setText(tr("Filter"));
    m_filterButton->setCheckable(true);
    m_filterButton->setToolTip(tr("Show the column filter bar"));
    toolLayout->addWidget(m_filterButton);

    toolLayout->addStretch();

    toolLayout->addWidget(new QLabel(tr("Search:"), toolbar));
    m_searchEdit = new QLineEdit(toolbar);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setPlaceholderText(tr("Find in grid"));
    m_searchEdit->setMaximumWidth(220);
    toolLayout->addWidget(m_searchEdit);
    m_prevButton = new QToolButton(toolbar);
    m_prevButton->setText(QStringLiteral("‹"));
    m_prevButton->setToolTip(tr("Previous match"));
    m_nextButton = new QToolButton(toolbar);
    m_nextButton->setText(QStringLiteral("›"));
    m_nextButton->setToolTip(tr("Next match"));
    toolLayout->addWidget(m_prevButton);
    toolLayout->addWidget(m_nextButton);
    m_matchLabel = new QLabel(toolbar);
    m_matchLabel->setMinimumWidth(70);
    toolLayout->addWidget(m_matchLabel);

    // --- column filter bar (hidden until toggled) ---
    m_filterRow = new QWidget(this);
    auto *filterLayout = new QHBoxLayout(m_filterRow);
    filterLayout->setContentsMargins(6, 2, 6, 4);
    filterLayout->setSpacing(6);
    filterLayout->addWidget(new QLabel(tr("Column:"), m_filterRow));
    m_filterColumnCombo = new QComboBox(m_filterRow);
    filterLayout->addWidget(m_filterColumnCombo);
    m_filterOpCombo = new QComboBox(m_filterRow);
    m_filterOpCombo->addItem(tr("contains"), GridFilterProxy::Contains);
    m_filterOpCombo->addItem(tr("starts with"), GridFilterProxy::StartsWith);
    m_filterOpCombo->addItem(tr("="), GridFilterProxy::Equals);
    m_filterOpCombo->addItem(tr("≠"), GridFilterProxy::NotEquals);
    m_filterOpCombo->addItem(tr(">"), GridFilterProxy::Greater);
    m_filterOpCombo->addItem(tr("≥"), GridFilterProxy::GreaterEqual);
    m_filterOpCombo->addItem(tr("<"), GridFilterProxy::Less);
    m_filterOpCombo->addItem(tr("≤"), GridFilterProxy::LessEqual);
    filterLayout->addWidget(m_filterOpCombo);
    m_filterValueEdit = new QLineEdit(m_filterRow);
    m_filterValueEdit->setPlaceholderText(tr("value"));
    m_filterValueEdit->setMaximumWidth(180);
    filterLayout->addWidget(m_filterValueEdit);
    auto *applyFilter = new QToolButton(m_filterRow);
    applyFilter->setText(tr("Apply"));
    filterLayout->addWidget(applyFilter);
    auto *clearFilter = new QToolButton(m_filterRow);
    clearFilter->setText(tr("Clear all"));
    filterLayout->addWidget(clearFilter);
    m_filterSummary = new QLabel(m_filterRow);
    filterLayout->addWidget(m_filterSummary, 1);
    m_filterRow->hide();

    // --- formula bar ---
    auto *formulaBar = new QWidget(this);
    auto *formulaLayout = new QHBoxLayout(formulaBar);
    formulaLayout->setContentsMargins(6, 2, 6, 4);
    formulaLayout->setSpacing(6);
    m_refLabel = new QLabel(formulaBar);
    m_refLabel->setMinimumWidth(44);
    m_refLabel->setAlignment(Qt::AlignCenter);
    QFont refFont = m_refLabel->font();
    refFont.setBold(true);
    m_refLabel->setFont(refFont);
    formulaLayout->addWidget(m_refLabel);
    auto *fx = new QLabel(QStringLiteral("ƒx"), formulaBar);
    formulaLayout->addWidget(fx);
    m_formulaEdit = new QLineEdit(formulaBar);
    m_formulaEdit->setPlaceholderText(tr("Value, or a formula starting with ="));
    formulaLayout->addWidget(m_formulaEdit, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(m_filterRow);
    layout->addWidget(formulaBar);
    layout->addWidget(m_table, 1);

    // --- wiring ---
    connect(m_delimiterCombo, &QComboBox::activated, this, [this](int) {
        m_delimiter = CsvData::Delimiter(m_delimiterCombo->currentData().toInt());
        m_delimiterUserSet = true;
        rebuild();
    });
    connect(m_headerCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_model->setHeaderRow(on);
        populateColumnFilterCombo();
        refreshMatches();
        updateFormulaBar();
    });
    connect(m_filterButton, &QToolButton::toggled, this, &GridView::setFilterRowVisible);
    connect(applyFilter, &QToolButton::clicked, this, &GridView::applyColumnFilter);
    connect(m_filterValueEdit, &QLineEdit::returnPressed, this, &GridView::applyColumnFilter);
    connect(clearFilter, &QToolButton::clicked, this, &GridView::clearFilters);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &GridView::refreshMatches);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] { goToMatch(1); });
    connect(m_nextButton, &QToolButton::clicked, this, [this] { goToMatch(1); });
    connect(m_prevButton, &QToolButton::clicked, this, [this] { goToMatch(-1); });

    connect(m_formulaEdit, &QLineEdit::returnPressed, this, &GridView::commitFormula);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this] { updateFormulaBar(); });

    applyThemeToWidgets();
    updateFormulaBar();
}

GridView::~GridView() = default;

void GridView::setSource(const QString &text, const QString &fileName)
{
    m_text = text;
    m_fileName = fileName;
    if (!m_delimiterUserSet) {
        m_delimiter = CsvData::detectDelimiter(text, fileName);
        const QSignalBlocker blocker(m_delimiterCombo);
        m_delimiterCombo->setCurrentIndex(m_delimiterCombo->findData(int(m_delimiter)));
    }
    // Only re-parse (losing the session's formulas) when something changed.
    if (!m_hasParsed || text != m_parsedText || m_delimiter != m_parsedDelimiter)
        rebuild();
}

void GridView::rebuild()
{
    const QList<QStringList> rows = CsvData::parse(m_text, CsvData::delimiterChar(m_delimiter));
    m_model->setGrid(rows, /*clearOverlay=*/true);
    m_parsedText = m_text;
    m_parsedDelimiter = m_delimiter;
    m_hasParsed = true;
    m_headerCheck->setEnabled(m_model->hasHeaderAvailable());
    populateColumnFilterCombo();
    m_proxy->clearColumnFilters();
    m_filterSummary->clear();
    m_table->sortByColumn(-1, Qt::AscendingOrder);
    refreshMatches();
    updateFormulaBar();
    if (m_proxy->rowCount() > 0 && m_proxy->columnCount() > 0)
        m_table->setCurrentIndex(m_proxy->index(0, 0));
}

void GridView::populateColumnFilterCombo()
{
    const QSignalBlocker blocker(m_filterColumnCombo);
    m_filterColumnCombo->clear();
    for (int c = 0; c < m_model->columnCount(); ++c) {
        const QString name = m_model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
        const QString letter = Formula::columnToLetters(c);
        m_filterColumnCombo->addItem(name == letter ? letter : QStringLiteral("%1 (%2)").arg(name, letter),
                                     c);
    }
}

void GridView::setFilterRowVisible(bool visible)
{
    m_filterRow->setVisible(visible);
    if (visible)
        m_filterValueEdit->setFocus();
}

void GridView::applyColumnFilter()
{
    if (m_filterColumnCombo->count() == 0)
        return;
    const int column = m_filterColumnCombo->currentData().toInt();
    const auto op = GridFilterProxy::Op(m_filterOpCombo->currentData().toInt());
    m_proxy->setColumnFilter(column, op, m_filterValueEdit->text());

    QStringList parts;
    const QList<int> cols = m_proxy->filteredColumns();
    for (int c : cols) {
        const QString letter = Formula::columnToLetters(c);
        parts << tr("%1 \"%2\"").arg(letter, m_proxy->filterValue(c));
    }
    m_filterSummary->setText(parts.isEmpty() ? QString()
                                             : tr("Showing %1 of %2 rows — %3")
                                                   .arg(m_proxy->rowCount())
                                                   .arg(m_model->rowCount())
                                                   .arg(parts.join(QStringLiteral(", "))));
    refreshMatches();
}

void GridView::clearFilters()
{
    m_proxy->clearColumnFilters();
    m_filterValueEdit->clear();
    m_filterSummary->clear();
    refreshMatches();
}

void GridView::updateFormulaBar()
{
    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid()) {
        m_refLabel->clear();
        m_formulaEdit->clear();
        emit statusChanged(tr("Grid: %1 × %2").arg(m_model->rowCount()).arg(m_model->columnCount()));
        return;
    }
    const QModelIndex source = m_proxy->mapToSource(current);
    const QString ref = Formula::columnToLetters(source.column())
        + QString::number(source.row() + 1);
    m_refLabel->setText(ref);
    const QString raw = m_model->rawAt(source.row(), source.column());
    if (!m_formulaEdit->hasFocus())
        m_formulaEdit->setText(raw);

    QString status = tr("Cell %1").arg(ref);
    if (raw.startsWith(QLatin1Char('=')))
        status += tr("  =  %1").arg(m_model->evalCell(source.row(), source.column()).toText());
    emit statusChanged(status);
}

void GridView::commitFormula()
{
    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid())
        return;
    const QModelIndex source = m_proxy->mapToSource(current);
    m_model->setData(source, m_formulaEdit->text(), Qt::EditRole);
    m_table->setFocus();
    updateFormulaBar();
}

void GridView::refreshMatches()
{
    m_matches.clear();
    m_matchPos = -1;
    const QString needle = m_searchEdit->text();
    if (!needle.isEmpty()) {
        const int rows = m_proxy->rowCount();
        const int cols = m_proxy->columnCount();
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const QModelIndex idx = m_proxy->index(r, c);
                if (m_proxy->data(idx, Qt::DisplayRole).toString().contains(needle,
                                                                            Qt::CaseInsensitive))
                    m_matches << idx;
            }
        }
    }
    updateMatchStatus();
    if (!m_matches.isEmpty())
        goToMatch(1);
}

void GridView::goToMatch(int delta)
{
    if (m_matches.isEmpty()) {
        updateMatchStatus();
        return;
    }
    m_matchPos = (m_matchPos + delta + m_matches.size()) % m_matches.size();
    const QModelIndex idx = m_matches.at(m_matchPos);
    m_table->setCurrentIndex(idx);
    m_table->scrollTo(idx, QAbstractItemView::EnsureVisible);
    updateMatchStatus();
}

void GridView::updateMatchStatus()
{
    if (m_searchEdit->text().isEmpty())
        m_matchLabel->clear();
    else if (m_matches.isEmpty())
        m_matchLabel->setText(tr("No matches"));
    else
        m_matchLabel->setText(tr("%1 of %2").arg(m_matchPos + 1).arg(m_matches.size()));
}

void GridView::focusSearch()
{
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void GridView::setGridFont(const QFont &font)
{
    m_table->setFont(font);
    m_formulaEdit->setFont(font);
}

void GridView::setTheme(const Theme &theme)
{
    m_theme = theme;
    m_model->setErrorColor(QColor(theme.dark ? QStringLiteral("#ff5555") : QStringLiteral("#cc0000")));
    applyThemeToWidgets();
}

void GridView::applyThemeToWidgets()
{
    const Theme &t = m_theme;
    const QColor gridLine = t.dark ? t.selection : QColor(QStringLiteral("#d0d0d0"));

    QPalette pal = palette();
    pal.setColor(QPalette::Base, t.background);
    pal.setColor(QPalette::AlternateBase, t.currentLine);
    pal.setColor(QPalette::Text, t.foreground);
    pal.setColor(QPalette::Window, t.gutterBackground);
    pal.setColor(QPalette::WindowText, t.foreground);
    pal.setColor(QPalette::Button, t.gutterBackground);
    pal.setColor(QPalette::ButtonText, t.foreground);
    pal.setColor(QPalette::Highlight, t.selection);
    pal.setColor(QPalette::HighlightedText, t.foreground);
    setPalette(pal);
    setAutoFillBackground(true);

    m_table->setPalette(pal);
    m_table->setStyleSheet(QStringLiteral("QTableView{ gridline-color: %1; }"
                                           "QHeaderView::section{ background:%2; color:%3;"
                                           " border:0px; border-right:1px solid %1;"
                                           " border-bottom:1px solid %1; padding:2px 6px; }")
                               .arg(gridLine.name(), t.gutterBackground.name(),
                                    t.foreground.name()));
}
