# Accessibility

TextEditor+ is a simple text and code editor. Users who type text via a keyboard instead of a mouse or use other input device (e.g., screen readers, a magnifier, large text, high-contrast colors, or voice control apps, etc.,) should be able to use TextEditor+. Everyone is welcome to help improve it.

This document explains what we are working towards, what we ask of contributors, how to report a barrier, what you can expect when you do, and what we already know does not work well.

TextEditor+ is a small, independently maintained [hobby] project. We try to say only what we can stand behind, so this document separates what we aim for from what has actually been checked.

## Priorities

We have not yet had TextEditor+ evaluated for accessibility by an independent person or tool, so **we do not claim conformance with any accessibility standard.** Where we mention goals below, they are goals, not results.

What we prioritize:

- **Everything works from the keyboard.** Every command in the menus has a keyboard shortcut or can be reached with the keyboard, and you should never get stuck in a part of the window. The shortcuts are listed in the [README](README.md#keyboard-shortcuts).
- **Text you can read comfortably.** You can choose any installed monospace font, zoom the text and switch between a Dark and a Light theme.
- **Compatibility with screen readers and other assistive technology.** TextEditor+ is built with Qt, which connects to the accessibility features of macOS, Windows and Linux. Our goal is for the editor, tabs, menus, dialogs and find/replace bar to be announced correctly. We have not yet systematically tested this (see [Known limitations](#known-limitations)).
- **Not relying on color alone.** Meaning such as unsaved changes, misspelled words, and errors should also be available through text or shape, not only through colour.
- **Plain language.** Labels, messages and documentation should be understandable without technical knowledge.

## Contributor expectations

If you contribute a change that affects what people see or do, please:

- **Check it is accessible via the keyboard alone.** You should be able to reach and use any new control or feature without a mouse in a sensible order, with a visible focus indicator.
- **Give controls a name.** Buttons, icon-only toolbar items, and input fields need a text label or accessible name, so assistive technology can announce them. Don't make icon-only controls whose only explanation is a tooltip.
- **Don't use colour as the only signal.** If something changes colour, also change a label, icon or text.
- **Check both themes.** Text and controls should stay readable in the Dark and Light themes, and when the text is zoomed.
- **Respect system settings** (such as font size, contrast and motion preferences) wherever Qt allows it.
- **Describe what you checked.** In your pull request, say briefly how you tested the change for accessibility, for example "tabbed through the new dialog on macOS; checked it with VoiceOver". If you could not test with assistive technology, say so; that is fine and helps us follow up.
- **Keep the automated tests passing.** The project's Qt Test suite in `tests/` includes checks for keyboard focus, and new user-facing behaviour should come with tests where practical. We do not currently run automated accessibility scanners, so manual checks matter.
- **Update the documentation.** If your change adds or alters a shortcut, update the keyboard shortcuts table in the README.

## Reporting accessibility issues

If something in TextEditor+ is hard or impossible to use, please open an issue using the **Issues** tab in this repository and start the title with "Accessibility:". You need a GitHub account to do this. If you can't use GitHub, ask someone to report it for you, or contact the maintainer through the address on their GitHub profile.

It helps if you can tell us:

- **What you were trying to do**, such as "open a folder" or "find and replace text".
- **What happened**, and what you expected to happen instead.
- **Your computer**: operating system and version, and the TextEditor+ version (shown in the About window).
- **Any assistive technology** you use, such as VoiceOver, NVDA, JAWS, Orca, a screen magnifier or voice control, with its version if you know it.
- **A screenshot or recording**, if that is easy for you. This is optional.

You do not need to tell us about any disability or explain why you use the tools you do. A short report is welcome; you don't need to know the cause.

### Severity

When we triage an issue we may label it with one of these levels. You don't need to choose one when you report.

| Level | What it means | Examples |
|---|---|---|
| **Blocker** | You cannot complete a core task at all. | A dialog can't be closed with the keyboard; a screen reader announces nothing in the editor. |
| **Serious** | You can complete the task, but only with great difficulty or a workaround. | A toolbar button has no accessible name, so you have to guess what it does. |
| **Moderate** | The task is possible but slower or more confusing than it should be. | Focus moves in an unexpected order; a status message isn't announced. |
| **Minor** | An annoyance that doesn't stop you doing the task. | An unclear label; a low-contrast decorative element. |

### How we respond

TextEditor+ is maintained by one person in their own time, so we promise only what we can reliably do:

- **Acknowledgement:** we aim to reply to accessibility issues within about a week.
- **Updates:** we will comment on the issue when we have reproduced it, decided how to fix it, or if we can't fix it soon and why.
- **Workarounds:** if we know a way around the problem, we will share it in the issue.
- **Priority:** blockers and serious barriers are looked at before new features.
- **Checking the fix:** when a fix is ready, we will tell you which version includes it and may ask whether it works for you. You are welcome to say it doesn't.
- **Fixes appear in the [release notes](release_notes.txt).**

We can't promise a fix date for every issue, but we will be honest with you about where it stands.

## Ownership and maintenance

Accessibility is the responsibility of the project maintainer, Sanjeeva Wijeyesakere. The maintainer triages accessibility issues, reviews pull requests for the checks listed above, and keeps this document accurate.

This document is reviewed at least once for each major release, and whenever a reported barrier changes what we know. If the project changes hands or gains additional maintainers, the new maintainers take on these responsibilities, and this section will be updated to name them.

## Supported environments

TextEditor+ is a native desktop application built with Qt 6 for:

- **macOS**
- **Linux**
- **Windows**

Which assistive technology has been tested:

| Environment | Status |
|---|---|
| Keyboard only (all platforms) | Developed and used with the keyboard; automated tests cover keyboard focus at startup. Not formally audited. |
| macOS VoiceOver | **Not yet evaluated.** |
| Windows NVDA, JAWS or Narrator | **Not yet evaluated.** |
| Linux Orca | **Not yet evaluated.** |
| Screen magnifiers, voice control, switch access | **Not yet evaluated.** |

"Not yet evaluated" means we don't know how well it works, not that it works or that it doesn't. Please tell us what you find. Reports from people using these tools are among the most useful contributions anyone can make.

## Known limitations

We haven't yet done a systematic accessibility review, so this list is based on how the app is built and not on user testing. It will change as we learn more.

- **Screen reader support is unverified.** We don't know how well the editor, tabs, sidebar, find/replace bar and dialogs are announced.
- **The CSV/TSV grid view** is a custom spreadsheet-style view. We have not checked whether screen readers can read cells, headers, filters and formula results. The text view of the same file is available as an alternative: press the **Grid** button again (⇧⌘G / Ctrl+Shift+G) to go back to the text.
- **Live preview** (Markdown, HTML, LaTeX) shows the formatted result beside the text. We haven't checked how it is read by assistive technology. The source text in the editor is always available.
- **Themes.** There are two themes, Dark and Light. They do not follow the system's high-contrast or increased-contrast settings, and we have not measured their colour contrast. If neither theme is comfortable for you, please tell us.
- **Syntax highlighting and spell checking** rely on color and underlines. Both can be turned off, spell checking in Settings (⌘, / Ctrl+,).
- **Custom scroll bars.** The classic-style scroll bars are drawn by the app instead of the operating system, so they may not respond to system accessibility settings in the same way as standard ones. You can scroll with the keyboard instead.
- **Icon-only toolbar.** The toolbar can show icons alone. We haven't confirmed that every button has an accessible name. Choosing the setting that shows labels beside or under the icons helps.

Tracked barriers are listed in the repository's issues under the "Accessibility:" title prefix.

## Feedback and improvements

Suggestions for this statement or for how we handle accessibility are welcome. Open an issue titled "Accessibility statement:" or send a pull request that changes this file. If you have found something that doesn't work for you right now, please use [Reporting accessibility issues](#reporting-accessibility-issues) instead, so it reaches the right place.
