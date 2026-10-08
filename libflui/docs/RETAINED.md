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
| Window | Root; `theme`, `theme_tokens` (JSON object encoded as a string), `font`, `fontsize`, `bkcolor`, `defaultfontcolor`, `dialog` |
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
