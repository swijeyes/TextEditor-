#pragma once

#include <QString>

// Turns a LaTeX document into HTML for Qt's rich-text engine (QTextBrowser):
// the TeX preview's quick mode, which needs no TeX installation and is fast
// enough to run on every keystroke.
//
// It understands the common parts of LaTeX: the title block, sections and a
// table of contents, lists, tables, figures and captions, footnotes, labels
// and references, citations with thebibliography, theorems and proofs,
// verbatim text, text styles and colours, links, images, and simple
// \newcommand / \newenvironment macros. Maths is shown with Unicode symbols,
// superscripts and subscripts, and numbered, aligned display equations.
//
// Everything taken from the source is HTML-escaped, so a document can't
// inject markup of its own. Images are left as relative <img> paths for the
// viewer to resolve; links are kept only for http, https and mailto.
//
// The HTML uses these classes, for the viewer's style sheet: title, author,
// date, caption, note (things the preview can't show), eqno, toc, footnotes.
namespace LatexHtml {

QString toHtml(const QString &latex);

} // namespace LatexHtml
