// The TeX preview's Quick mode converter: what it shows, and that nothing
// from the document can become markup.

#include "latexhtml.h"

#include <QElapsedTimer>
#include <QTest>

namespace {

QString html(const QString &latex)
{
    return LatexHtml::toHtml(latex);
}

} // namespace

class TestLatexHtml : public QObject
{
    Q_OBJECT

private slots:
    void titleAndSections()
    {
        const QString out = html(QStringLiteral(
            "\\documentclass{article}\\title{My Paper}\\author{Ann}\\date{2026}"
            "\\begin{document}\\maketitle\\section{Intro}\\subsection{Detail}\\section*{Extra}"
            "\\end{document}"));
        QVERIFY(out.contains(QLatin1String("My Paper</h1>")));
        QVERIFY(out.contains(QLatin1String(">Ann</p>")));
        QVERIFY(out.contains(QLatin1String("1&nbsp;&nbsp;Intro</h2>")));
        QVERIFY(out.contains(QLatin1String("1.1&nbsp;&nbsp;Detail</h3>")));
        QVERIFY(out.contains(QLatin1String("></a>Extra</h2>")));
        QVERIFY(!out.contains(QLatin1String("documentclass")));
    }

    void textStylesAndSymbols()
    {
        const QString out = html(QStringLiteral(
            "\\textbf{B} \\emph{E} \\texttt{T} ``q'' a--b a---b 50\\% \\& \\_ \\LaTeX{} caf\\'e"));
        QVERIFY(out.contains(QLatin1String("<b>B</b>")));
        QVERIFY(out.contains(QLatin1String("<i>E</i>")));
        QVERIFY(out.contains(QLatin1String("<tt>T</tt>")));
        QVERIFY(out.contains(QStringLiteral("“q”")));
        QVERIFY(out.contains(QStringLiteral("a–b")));
        QVERIFY(out.contains(QStringLiteral("a—b")));
        QVERIFY(out.contains(QLatin1String("50%")));
        QVERIFY(out.contains(QLatin1String("&amp;")));
        QVERIFY(out.contains(QLatin1String("LaTeX")));
        QVERIFY(out.contains(QStringLiteral("café")) || out.contains(QStringLiteral("cafe\u0301")));
    }

    void maths()
    {
        const QString out = html(QStringLiteral(
            "$x^2 + \\alpha_i \\leq \\frac{a}{b}$ and $\\mathbb{R}$"));
        QVERIFY(out.contains(QLatin1String("<i>x</i><sup>2</sup>")));
        QVERIFY(out.contains(QStringLiteral("α<sub><i>i</i></sub>")));
        QVERIFY(out.contains(QStringLiteral("≤")));
        QVERIFY(out.contains(QStringLiteral("ℝ")));
    }

    void numberedEquationsAndReferences()
    {
        const QString out = html(QStringLiteral(
            "See \\eqref{e2} and Section~\\ref{s}.\\section{S}\\label{s}"
            "\\begin{equation}a\\label{e1}\\end{equation}"
            "\\begin{align}b &= c \\label{e2}\\\\ d &= e \\nonumber\\end{align}"
            "Missing: \\ref{nowhere}."));
        QVERIFY(out.contains(QLatin1String("See (2)")));
        QVERIFY(out.contains(QLatin1String("Section&nbsp;1.")));
        QVERIFY(out.contains(QLatin1String("(1)</td>")));
        QVERIFY(out.contains(QLatin1String("(2)</td>")));
        QVERIFY(!out.contains(QLatin1String("(3)")));
        QVERIFY(out.contains(QLatin1String("Missing: ??")));
    }

    void listsTablesAndFloats()
    {
        const QString out = html(QStringLiteral(
            "\\begin{itemize}\\item One \\item Two\\end{itemize}"
            "\\begin{enumerate}\\item A\\end{enumerate}"
            "\\begin{table}\\centering\\begin{tabular}{|l|r|}\\hline x & y \\\\ \\multicolumn{2}{c}{z}\\\\\\hline"
            "\\end{tabular}\\caption{Data}\\label{t}\\end{table} Table \\ref{t}."));
        QVERIFY(out.contains(QLatin1String("<ul")));
        QVERIFY(out.contains(QLatin1String("<li> One </li>")));
        QVERIFY(out.contains(QLatin1String("<ol")));
        QVERIFY(out.contains(QLatin1String("border=\"1\"")));
        QVERIFY(out.contains(QLatin1String("<td align=\"right\">y</td>")));
        QVERIFY(out.contains(QLatin1String("colspan=\"2\"")));
        QVERIFY(out.contains(QLatin1String("<b>Table 1:</b> Data")));
        QVERIFY(out.contains(QLatin1String("Table 1.")));
    }

    void macrosAndTheorems()
    {
        const QString out = html(QStringLiteral(
            "\\newcommand{\\R}{\\mathbb{R}}\\newcommand{\\pair}[2]{(#1, #2)}"
            "\\newcommand{\\greet}[1][World]{Hello #1}\\newtheorem{thm}{Theorem}"
            "$\\R$ \\pair{a}{b} \\greet \\greet[You]"
            "\\begin{thm}[Big]True.\\end{thm}\\begin{proof}Easy.\\end{proof}"));
        QVERIFY(out.contains(QStringLiteral("ℝ")));
        QVERIFY(out.contains(QLatin1String("(a, b)")));
        QVERIFY(out.contains(QLatin1String("Hello World")));
        QVERIFY(out.contains(QLatin1String("Hello You")));
        QVERIFY(out.contains(QLatin1String("<b>Theorem 1 (Big).</b>")));
        QVERIFY(out.contains(QLatin1String("<i>Proof.</i>")));
    }

    void footnotesAndCitations()
    {
        const QString out = html(QStringLiteral(
            "Text\\footnote{A note.} \\cite{k1,k2}"
            "\\begin{thebibliography}{9}\\bibitem{k1} One.\\bibitem{k2} Two.\\end{thebibliography}"));
        QVERIFY(out.contains(QLatin1String("<sup><a href=\"#fn1\">1</a></sup>")));
        QVERIFY(out.contains(QLatin1String("A note.")));
        QVERIFY(out.contains(QLatin1String("[1, 2]")));
    }

    void verbatimIsLiteral()
    {
        const QString out = html(QStringLiteral(
            "\\begin{verbatim}\n\\textbf{x} <b> % not a comment\n\\end{verbatim} \\verb|<i>|"));
        QVERIFY(out.contains(QLatin1String("<pre>\\textbf{x} &lt;b&gt; % not a comment\n</pre>")));
        QVERIFY(out.contains(QLatin1String("<tt>&lt;i&gt;</tt>")));
    }

    // Nothing in the document can become HTML: tags, attributes and
    // javascript: links all come out as text.
    void documentCantInjectMarkup()
    {
        const QString out = html(QStringLiteral(
            "<script>x</script> \\textbf{<img src=x onerror=y>} \\href{javascript:alert(1)}{click}"
            " \\url{\"><b>} \\textcolor{red;background:url(x)}{c} \\includegraphics{a\"onload=\"x}"
            " \\label{\"><b>} \\ref{\"><b>} $<b>$ \\begin{verbatim}</pre><b>\\end{verbatim}"));
        QVERIFY(!out.contains(QLatin1String("<script")));
        QVERIFY(!out.contains(QLatin1String("<img src=x")));
        QVERIFY(!out.contains(QLatin1String("javascript:")));
        QVERIFY(!out.contains(QLatin1String("\"><b>")));
        QVERIFY(!out.contains(QLatin1String("background:url")));
        QVERIFY(!out.contains(QLatin1String("\"onload")));
        QVERIFY(!out.contains(QLatin1String("<b>$")));
        QVERIFY(out.contains(QLatin1String("&lt;script&gt;")));
        QVERIFY(out.contains(QLatin1String("click")));
    }

    void safeLinksKept()
    {
        const QString out = html(QStringLiteral("\\href{https://example.com/a?b=1}{site}"));
        QVERIFY(out.contains(QLatin1String("<a href=\"https://example.com/a?b=1\">site</a>")));
    }

    // Recursive macros and deep nesting stop instead of hanging or
    // overflowing the stack.
    void hostileInputFinishes()
    {
        QElapsedTimer timer;
        timer.start();
        html(QStringLiteral("\\newcommand{\\a}{\\a\\a}\\a"));
        html(QStringLiteral("\\def\\b{\\b}\\b"));
        html(QString(200000, u'{'));
        html(QString(50000, u'$') + QString(50000, u'\\'));
        html(QStringLiteral("\\begin{itemize}").repeated(5000));
        html(QStringLiteral("$\\frac{").repeated(5000));
        html(QStringLiteral("\\newcommand{\\x}[1]{#1#1#1#1}\\x{\\x{\\x{\\x{\\x{\\x{\\x{\\x{\\x{\\x{y}}}}}}}}}}"));
        QVERIFY2(timer.elapsed() < 10000, qPrintable(QString::number(timer.elapsed())));
    }
};

QTEST_MAIN(TestLatexHtml)
#include "tst_latexhtml.moc"
