# Interface localization

AOWIS Mobile provides five live interface languages:

| Code | Display name | Direction |
| --- | --- | --- |
| `en` | English | LTR |
| `fr` | Français | LTR |
| `ar` | العربية | RTL |
| `de` | Deutsch | LTR |
| `es` | Español | LTR |

The **Settings → Language** selector updates the currently visible UI **without
restarting the application or recreating the QML component tree**. Map camera,
GPS, instrument state, active page, unfinished map-server input and live
performance capture remain in place. Selection is stored under
`interface/language` in the same `QSettings` application scope as Appearance.
The default is English. There is no extra Android permission or Docker module.

## Implementation

- `LanguageSettings` installs a Qt `QTranslator` and owns its lifetime. The
  production translations are embedded UTF-8 strings from
  `i18n/translations.json`. This is a small application catalog backed by Qt's
  standard translation lookup API, rather than an external gettext runtime.
- QML uses `qsTr("English source text")`. The language-change signal invokes
  `QQmlApplicationEngine::retranslate()` to reevaluate the existing bindings.
  UI strings stored as state (e.g. benchmark/recovery messages) are represented
  by stable status types and formatted again on demand.
- `LocationService`, `CompassService`, `GradienterService`, `PlumbBobService`,
  `MapServerSettings`, and `RenderLab` expose translated descriptions through
  `tr()` or `QCoreApplication::translate()` and notify their QML properties
  when the active language changes. Status logic is based on explicit states,
  **never on matching translated English sentences**.
- Arabic switches Qt's application layout direction and applies
  `LayoutMirroring` to the interface pages and menu. The compass, bubble level,
  and plumb-line graphics explicitly opt out of mirroring; cardinal points and
  numeric measurements retain their physical meanings. Addresses, ports and
  other technical input fields retain LTR input order.
- Language names in the dropdown remain in their own native scripts, so the
  user can always identify and restore a language.

## Adding or changing text

1. Wrap new QML text with `qsTr("English source text")`; use `tr()` for status
   getters in C++ and notify the relevant properties on language change.
2. Add the **same English source key** and four translations to
   `i18n/translations.json`. Preserve `%1`, `%2`, etc. format markers; their
   order may change in a translation.
3. Run `python tests/verify_localization.py`. This fails on missing keys,
   empty translations, lost format parameters and changed newline counts.
4. Compile with `./compile_linux.sh --tests`, and check all five languages on
   Linux and Android, especially Arabic RTL layout, long labels, fields with
   keyboard focus, GPS readouts, all instruments and Android Back.

Machine-readable benchmark CSV headers, event IDs, package names and URLs are
intentionally **not localized**: automation and API consumers rely on stable
field identifiers. The UI *presenting* their measurements is localized.

The catalogs are editable JSON resources, not compiled `.qm` files, so the
current Android Docker build needs **no Qt LinguistTools module**. The Qt
translation bindings and live-switch behavior are otherwise conventional.
