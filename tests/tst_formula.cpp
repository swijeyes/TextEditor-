// The spreadsheet formula engine: parsing, operators, references and the
// maths, statistical, financial, logical and text functions.

#include "formula.h"

#include <QHash>
#include <QSet>
#include <QTest>

#include <cmath>

using namespace Formula;

namespace {

// A grid of raw cell text that evaluates its own "=" formulas, with cycle
// detection, exactly as the grid model does.
class TestSheet : public Sheet
{
public:
    explicit TestSheet(const QList<QStringList> &rows)
        : m_rows(rows)
    {
    }

    int rowCount() const override { return int(m_rows.size()); }
    int columnCount() const override
    {
        int c = 0;
        for (const auto &r : m_rows)
            c = qMax(c, int(r.size()));
        return c;
    }

    Value cellValue(int row, int col) const override
    {
        if (row < 0 || row >= m_rows.size() || col < 0 || col >= m_rows[row].size())
            return Value::empty();
        const QString raw = m_rows[row][col];
        if (raw.isEmpty())
            return Value::empty();
        if (raw.startsWith(QLatin1Char('='))) {
            const QPair<int, int> key{row, col};
            if (m_visiting.contains(key))
                return Value::error(QStringLiteral("#CIRC!"));
            m_visiting.insert(key);
            const Value v = evaluate(raw.mid(1), *this);
            m_visiting.remove(key);
            return v;
        }
        bool ok = false;
        const double d = raw.toDouble(&ok);
        return ok ? Value::number(d) : Value::text(raw);
    }

private:
    QList<QStringList> m_rows;
    mutable QSet<QPair<int, int>> m_visiting;
};

double evalNum(const QString &expr, const Sheet &sheet)
{
    return evaluate(expr, sheet).toNumber();
}

} // namespace

class TestFormula : public QObject
{
    Q_OBJECT

private:
    TestSheet sheet{{
        {"10", "1", "x"},     // row 1: A1=10 B1=1 C1=x
        {"20", "2", "y"},     // row 2
        {"30", "3", "z"},     // row 3
        {"40", "4", ""},      // row 4
    }};

private slots:
    void columnLetters()
    {
        QCOMPARE(columnFromLetters("A"), 0);
        QCOMPARE(columnFromLetters("Z"), 25);
        QCOMPARE(columnFromLetters("AA"), 26);
        QCOMPARE(columnFromLetters("AB"), 27);
        QCOMPARE(columnToLetters(0), QStringLiteral("A"));
        QCOMPARE(columnToLetters(25), QStringLiteral("Z"));
        QCOMPARE(columnToLetters(26), QStringLiteral("AA"));
        QCOMPARE(columnToLetters(701), QStringLiteral("ZZ"));
    }

    void arithmetic()
    {
        QCOMPARE(evalNum("1+2*3", sheet), 7.0);
        QCOMPARE(evalNum("(1+2)*3", sheet), 9.0);
        QCOMPARE(evalNum("2^3^2", sheet), 512.0);   // right associative
        QCOMPARE(evalNum("-2^2", sheet), 4.0);       // unary binds tighter than ^
        QCOMPARE(evalNum("10/4", sheet), 2.5);
        QCOMPARE(evalNum("50%", sheet), 0.5);
        QCOMPARE(evalNum("10+20%", sheet), 10.2);
    }

    void errors()
    {
        QVERIFY(evaluate("1/0", sheet).isError());
        QCOMPARE(evaluate("1/0", sheet).toText(), QStringLiteral("#DIV/0!"));
        QVERIFY(evaluate("NOSUCH(1)", sheet).isError());
        QCOMPARE(evaluate("NOSUCH(1)", sheet).toText(), QStringLiteral("#NAME?"));
        QVERIFY(evaluate("1+", sheet).isError());
        QVERIFY(evaluate("(1+2", sheet).isError());
    }

    void references()
    {
        QCOMPARE(evalNum("A1", sheet), 10.0);
        QCOMPARE(evalNum("A1+A2", sheet), 30.0);
        QCOMPARE(evalNum("SUM(A1:A4)", sheet), 100.0);
        QCOMPARE(evalNum("SUM(A:A)", sheet), 100.0);       // whole column
        QCOMPARE(evalNum("SUM(A1:B2)", sheet), 33.0);      // block 10+1+20+2
        QCOMPARE(evalNum("AVERAGE(A1:A4)", sheet), 25.0);
        QCOMPARE(evalNum("COUNT(A1:C4)", sheet), 8.0);     // numbers only
        QCOMPARE(evalNum("COUNTA(A1:C4)", sheet), 11.0);   // C4 is empty
    }

    void statistics()
    {
        QCOMPARE(evalNum("MIN(A1:A4)", sheet), 10.0);
        QCOMPARE(evalNum("MAX(A1:A4)", sheet), 40.0);
        QCOMPARE(evalNum("MEDIAN(A1:A4)", sheet), 25.0);
        QCOMPARE(evalNum("MEDIAN(A1:A3)", sheet), 20.0);
        QVERIFY(qFuzzyCompare(evalNum("STDEV(A1:A4)", sheet), 12.9099444873581));
        QVERIFY(qFuzzyCompare(evalNum("STDEVP(A1:A4)", sheet), 11.1803398874989));
        QVERIFY(qFuzzyCompare(evalNum("VAR(A1:A4)", sheet), 166.6666666666667));
        QVERIFY(qFuzzyCompare(evalNum("VARP(A1:A4)", sheet), 125.0));
        QVERIFY(qFuzzyCompare(evalNum("PERCENTILE(A1:A4,0.5)", sheet), 25.0));
        QVERIFY(qFuzzyCompare(evalNum("QUARTILE(A1:A4,1)", sheet), 17.5));
        QCOMPARE(evalNum("LARGE(A1:A4,1)", sheet), 40.0);
        QCOMPARE(evalNum("SMALL(A1:A4,2)", sheet), 20.0);
        // CORREL of a perfectly linear pair is 1.
        QVERIFY(qFuzzyCompare(evalNum("CORREL(A1:A4,B1:B4)", sheet), 1.0));
        QVERIFY(qFuzzyCompare(evalNum("SLOPE(A1:A4,B1:B4)", sheet), 10.0));
        QVERIFY(qFuzzyCompare(evalNum("INTERCEPT(A1:A4,B1:B4)", sheet), 0.0));
        QCOMPARE(evalNum("RANK(30,A1:A4)", sheet), 2.0);
    }

    void maths()
    {
        QCOMPARE(evalNum("ABS(-5)", sheet), 5.0);
        QCOMPARE(evalNum("ROUND(3.14159,2)", sheet), 3.14);
        QCOMPARE(evalNum("ROUNDUP(3.1,0)", sheet), 4.0);
        QCOMPARE(evalNum("ROUNDDOWN(3.9,0)", sheet), 3.0);
        QCOMPARE(evalNum("MOD(7,3)", sheet), 1.0);
        QCOMPARE(evalNum("MOD(-7,3)", sheet), 2.0);     // divisor's sign
        QCOMPARE(evalNum("POWER(2,10)", sheet), 1024.0);
        QCOMPARE(evalNum("SQRT(144)", sheet), 12.0);
        QVERIFY(evaluate("SQRT(-1)", sheet).isError());
        QCOMPARE(evalNum("INT(3.9)", sheet), 3.0);
        QCOMPARE(evalNum("CEILING(2.1,1)", sheet), 3.0);
        QCOMPARE(evalNum("FLOOR(2.9,1)", sheet), 2.0);
        QCOMPARE(evalNum("LOG(1000,10)", sheet), 3.0);
        QVERIFY(qFuzzyCompare(evalNum("SUMPRODUCT(A1:A3,B1:B3)", sheet), 140.0)); // 10+40+90
    }

    void logical()
    {
        QCOMPARE(evaluate("IF(1>0,\"yes\",\"no\")", sheet).toText(), QStringLiteral("yes"));
        QCOMPARE(evaluate("IF(A1>100,\"big\",\"small\")", sheet).toText(),
                 QStringLiteral("small"));
        QCOMPARE(evaluate("AND(TRUE,1>0)", sheet).toText(), QStringLiteral("TRUE"));
        QCOMPARE(evaluate("OR(FALSE,1>2)", sheet).toText(), QStringLiteral("FALSE"));
        QCOMPARE(evaluate("NOT(TRUE)", sheet).toText(), QStringLiteral("FALSE"));
        QCOMPARE(evaluate("IFERROR(1/0,\"oops\")", sheet).toText(), QStringLiteral("oops"));
        QCOMPARE(evalNum("IFERROR(10,99)", sheet), 10.0);
    }

    void text()
    {
        QCOMPARE(evaluate("CONCAT(\"a\",\"b\",C1)", sheet).toText(), QStringLiteral("abx"));
        QCOMPARE(evaluate("\"a\"&\"b\"&\"c\"", sheet).toText(), QStringLiteral("abc"));
        QCOMPARE(evalNum("LEN(\"hello\")", sheet), 5.0);
        QCOMPARE(evaluate("UPPER(\"abc\")", sheet).toText(), QStringLiteral("ABC"));
        QCOMPARE(evaluate("LOWER(\"ABC\")", sheet).toText(), QStringLiteral("abc"));
        QCOMPARE(evaluate("LEFT(\"hello\",2)", sheet).toText(), QStringLiteral("he"));
        QCOMPARE(evaluate("RIGHT(\"hello\",3)", sheet).toText(), QStringLiteral("llo"));
        QCOMPARE(evaluate("MID(\"hello\",2,3)", sheet).toText(), QStringLiteral("ell"));
        QCOMPARE(evaluate("TRIM(\"  a  b  \")", sheet).toText(), QStringLiteral("a b"));
        QCOMPARE(evaluate("SUBSTITUTE(\"a-b-c\",\"-\",\"+\")", sheet).toText(),
                 QStringLiteral("a+b+c"));
    }

    void finance()
    {
        // Loan: 5%/12 monthly, 60 months, borrow 20000 -> payment ~ -377.42.
        QVERIFY(std::abs(evalNum("PMT(0.05/12,60,20000)", sheet) + 377.424255) < 1e-3);
        QVERIFY(std::abs(evalNum("PMT(0.08/12,120,0,100000)", sheet) + 546.6088) < 1e-2);
        // FV of saving 100/month at 6%/12 for 120 months with 0 pv.
        QVERIFY(std::abs(evalNum("FV(0.06/12,120,-100,0)", sheet) - 16387.93) < 1e-1);
        // PV of 1000/yr for 10 yrs at 8%.
        QVERIFY(std::abs(evalNum("PV(0.08,10,-1000)", sheet) - 6710.08) < 1e-1);
        // NPER: how many 200 payments at 6%/12 to pay off 10000.
        QVERIFY(std::abs(evalNum("NPER(0.06/12,-200,10000)", sheet) - 57.68) < 1e-1);
        // NPV at 10% of -10000 then five 3000 inflows.
        QVERIFY(std::abs(evalNum("NPV(0.1,3000,3000,3000,3000,3000)", sheet) - 11372.36) < 1e-1);
        // IRR of a classic project.
        QVERIFY(std::abs(evalNum("IRR(A1:A4)", sheet)) >= 0.0); // just exercises the solver
        QVERIFY(std::abs(evalNum("RATE(60,-377.42,20000)", sheet) - 0.05 / 12) < 1e-4);
        // Straight-line and double-declining depreciation.
        QCOMPARE(evalNum("SLN(10000,1000,5)", sheet), 1800.0);
        QVERIFY(std::abs(evalNum("DDB(10000,1000,5,1)", sheet) - 4000.0) < 1e-6);
        // IPMT + PPMT should equal the whole payment.
        const double pmt = evalNum("PMT(0.1/12,36,8000)", sheet);
        const double ipmt = evalNum("IPMT(0.1/12,1,36,8000)", sheet);
        const double ppmt = evalNum("PPMT(0.1/12,1,36,8000)", sheet);
        QVERIFY(std::abs((ipmt + ppmt) - pmt) < 1e-6);
        QVERIFY(std::abs(ipmt + 66.6667) < 1e-3); // first-period interest
    }

    void irrAccuracy()
    {
        TestSheet cash{{{"-10000"}, {"3000"}, {"4200"}, {"6800"}}};
        // IRR of {-10000, 3000, 4200, 6800} is about 0.16341.
        QVERIFY(std::abs(evalNum("IRR(A1:A4)", cash) - 0.1634056) < 1e-4);
    }

    void circularReference()
    {
        TestSheet loop{{{"=B1+1", "=A1+1"}}};
        QVERIFY(loop.cellValue(0, 0).isError());
    }
};

QTEST_APPLESS_MAIN(TestFormula)
#include "tst_formula.moc"
