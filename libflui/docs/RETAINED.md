# Retained document API: C consumer quick reference

This reference is shipped in the binary SDK. No Dart source or Flutter knowledge
is required. Include `<libflui/desktop.h>` and follow the lifecycle in
[Integration](INTEGRATION.md). The C++ convenience layer generates these documents
for applications that prefer controls and XML fragments over raw JSON.

## Documents and patches

After READY, submit a UTF-8 JSON document with `flui_window_set_tree`. The root tag
must be `Window`. Every node has four fields: `id`, `tag`, `attrs`, `children`.
IDs are unique positive decimal strings of 1–20 digits, with no leading zero.
Attributes are string-to-string maps, including numeric and boolean values.
Children are arrays, empty for leaf controls. Limits are 16,384 nodes, depth 64
(root at depth zero), and the ABI's 1 MiB message limit.

A minimal form:

```json
{
  "id": "1", "tag": "Window", "attrs": {"theme": "neutral"},
  "children": [{
    "id": "2", "tag": "VerticalLayout",
    "attrs": {"padding": "16", "childpadding": "8"},
    "children": [
      {"id": "3", "tag": "Label", "attrs": {"height": "24", "text": "Ready"}, "children": []},
      {"id": "4", "tag": "Edit", "attrs": {"height": "28", "text": "1"}, "children": []},
      {"id": "5", "tag": "Button", "attrs": {"height": "28", "text": "Submit", "enabled": "true"}, "children": []}
    ]
  }]
}
```

Wait for COMPLETE to check acceptance. To change only the label, call
`flui_window_patch` with:

```json
[{"id":"3","attrs":{"height":"24","text":"Accepted"}}]
```

Each entry replaces that control's **entire attribute map**. Preserve height,
event flags and styling you still need. Unknown/duplicate IDs or invalid attributes
reject the whole patch; the existing document remains. Structural changes use a
new full tree. Stable IDs and unchanged tags preserve control/editor identity.
Do not change IDs on every quote tick.

## Common controls and attributes

| Control | Purpose / attributes |
| --- | --- |
| Window | Root; `theme`, `theme_tokens`, `theme_dark_tokens` (JSON objects encoded as strings), `font`, `fontsize`, `bkcolor`, `defaultfontcolor`, `dialog` |
| VerticalLayout / HorizontalLayout | Weighted children; `padding`, `childpadding`; children use `height`/`width` or `weight` |
| Label / DecimalLabel | Exact `text`, `fontsize`, `bold`, `textcolor`, `align` (`left`, `center`, `right`), `wordwrap` |
| Button / DecimalButton | Same text styling plus `enabled`; click emits an action |
| Edit | `text`, `hint`, `maxchar`, `readonly`, `password`, `multiline`, `event_enter`, `event_step`, `event_navigate` |
| Combo | `items_json` (JSON string array encoded as a string), `selected` (zero-based index; -1 for none) |
| Option | Controlled checkbox with `text`, `selected`; application updates state after click |

Common attributes include `visible`, `enabled`, `width`, `height`, `weight`,
`bkcolor`, `bordercolor`, `bordersize`, `padding` and `tooltip`. Boolean strings are
`"true"`/`"false"`. Dimensions are logical pixels; zero/omitted main-axis extent in
a row/column fills the available space by weight. Padding accepts one value or
`"left,top,right,bottom"`. Colors use `#RRGGBB` or `#AARRGGBB`, or a semantic theme
reference such as `$accent`. Attribute names outside the supported vocabulary are
rejected. Retained layouts use `childpadding`; the binding XML sample uses `gap`.
These are separate document formats.

Lists, panes, plots and other advanced desktop controls are exposed by `flui.hpp`;
prefer that header for their virtualization, geometry and event management in this
preview. The minimal C document surface above is sufficient for labels, forms and
business-result presentation without inspecting the renderer implementation.

## Actions and editing

Retained actions arrive as `FLUI_EVENT_ACTION`, with `name` equal to
`<control-id>:<event>`. For the form above:

| Name | Value |
| --- | --- |
| `5:click` | Empty string; dispatch to the application's submit handler |
| `4:valuechanged` | Current editor text; validate/store as application state |
| `4:enter` | Empty; emitted only with `event_enter="true"` |
| `4:step` | `up` or `down`; opt in with `event_step="true"` |
| `4:navigate` | `forward` or `backward`; opt in with `event_navigate="true"` |
| `0:close` | Esc on a root with `dialog="true"`; host decides whether to close |

An Enter event never automatically invokes a button. Checkbox and selector state
is controlled by the application; publish the accepted value after handling the
event. Keep exact quantities/IDs as strings. Ignore unhandled event names; SDK
geometry/profiling events may also be delivered. Never replay user commands because
a rendering queue was busy.

Focused editors retain in-progress text during model echoes. A changed
`edit_revision` string explicitly forces the current `text` into the editor; use
that only for deliberate application resets. Changing `focus` or `select_all` to
a fresh string triggers the corresponding operation. Ordinary quote updates should
not change these attributes. Disabled ancestors block interaction; applications
must still validate business actions against their current model.

This retained schema is a preview API versioned with the SDK. It is not a network
protocol or a script environment. Use complete matched headers/library/runtime and
revalidate raw-document consumers when upgrading a 0.x minor version.

## Adaptive appearance (unreleased)

`theme="system"` observes the engine platform appearance without polling. Supply
light/base colors in `theme_tokens` and dark overrides in `theme_dark_tokens`.
`theme="light"` / `"dark"` select a fixed appearance; dark overrides apply only
for dark or a dark system appearance. A theme change preserves control identity,
editor text, focus and composing state. Explicit color literals remain fixed.
`borderround` also accepts a numeric token such as `$corner`.

`classic-2000` provides square, gray controls, a navy accent and Tahoma fallback.
`role="button"` receives raised edges, `role="inset"` sunken edges. This is a
client-area theme; the native OS window title bar is retained.

## Localization and narrow layouts (unreleased)

The application supplies its own language catalogs. Set root attributes locale
(system, zh-Hans, zh-Hant or en), fallback_locale (default en) and translations
(a JSON object encoded as an attribute string). Each catalog maps message keys
to plain strings or plural forms with an other entry. This release targets these
three locales; it does not promise arbitrary locale, RTL, date or currency support.

Use textkey/textargs, hintkey/hintargs and tooltipkey/tooltipargs. Argument
attributes contain JSON objects with string values. Example catalogs:
{"en":{"send":"Send","files":{"one":"{count} file","other":"{count} files"}},
"zh-Hans":{"send":"发送"},"zh-Hant":{"send":"傳送"}}.
A missing key falls back to the fallback catalog, then [key]; plural messages
require a finite count argument. Parameters are plain text, not XML or code.
Plural counts are UI counts; identifiers and exact business quantities must not
be parsed through this numeric path.

C++ consumers use DesktopWindow::SetTranslations, SetLanguage and
Control::SetTextKey. SetText clears textkey for literal/user content. Never
translate received messages, device names, user drafts or filenames through the
catalog. Stable control IDs retain the editor, focus and composing state while
the locale or appearance changes.

Root safe_area=true respects platform insets. ScrollLayout scrolls a vertical
column of bounded-height children (default 40); use it for settings, not unbounded
history. WrapLayout wraps bounded-size children (default 100 by 34) and uses
childpadding between items and runs. Existing List remains the virtualized list.
