#pragma once

#include "csvdata.h"
#include "theme.h"

#include <QModelIndexList>
#include <QWidget>

class GridModel;
class GridFilterProxy;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;
class QToolButton;

// The spreadsheet-style grid shown instead of the editor for CSV and TSV
// files. It parses the text into a table and layers filtering, search and a
// formula engine on top, all in-session: the file on disk is never changed.
//
// Cells can be edited and can hold "=" formulas (arithmetic, statistical,
// financial, logical and text functions, with A1-style references), but the
// edits and results live only in the grid; closing it or switching back to
// the editor leaves the original text untouched.
class GridView : public QWidget
{
    Q_OBJECT

public:
    explicit GridView(QWidget *parent = nullptr);
    ~GridView() override;

    // Loads the grid from the tab's text. The delimiter is guessed from the
    // name and the text unless the user has chosen one. Re-parsing only
    // happens when the text or delimiter actually changed, so toggling the
    // grid off and on keeps the session's formulas and filters.
    void setSource(const QString &text, const QString &fileName);

    void setTheme(const Theme &theme);
    void setGridFont(const QFont &font);

    // Puts the keyboard focus in the search box (for Find).
    void focusSearch();

signals:
    // A short description of the selection/size, for the status bar.
    void statusChanged(const QString &text);

private:
    void rebuild();
    void applyThemeToWidgets();
    void updateFormulaBar();
    void commitFormula();
    void setFilterRowVisible(bool visible);
    void applyColumnFilter();
    void clearFilters();
    void refreshMatches();
    void goToMatch(int delta);
    void updateMatchStatus();
    void populateColumnFilterCombo();

    GridModel *m_model;
    GridFilterProxy *m_proxy;
    QTableView *m_table;

    QComboBox *m_delimiterCombo;
    QCheckBox *m_headerCheck;
    QLineEdit *m_searchEdit;
    QToolButton *m_prevButton;
    QToolButton *m_nextButton;
    QLabel *m_matchLabel;
    QToolButton *m_filterButton;

    QWidget *m_filterRow;
    QComboBox *m_filterColumnCombo;
    QComboBox *m_filterOpCombo;
    QLineEdit *m_filterValueEdit;
    QLabel *m_filterSummary;

    QLabel *m_refLabel;
    QLineEdit *m_formulaEdit;

    QString m_text;
    QString m_fileName;
    QString m_parsedText;                 // what the model currently reflects
    CsvData::Delimiter m_delimiter = CsvData::Delimiter::Comma;
    CsvData::Delimiter m_parsedDelimiter = CsvData::Delimiter::Comma;
    bool m_delimiterUserSet = false;
    bool m_hasParsed = false;

    Theme m_theme = Theme::dracula();
    QModelIndexList m_matches;            // in proxy coordinates
    int m_matchPos = -1;
};
