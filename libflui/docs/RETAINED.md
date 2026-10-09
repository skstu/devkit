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
| Edit | `text`, `hint`, `maxchar`, `readonly`, `password`, `multiline`, `valign` (`top`, `center`, `bottom`; default center), `event_enter`, `event_step`, `event_navigate` |
| Combo | `items_json` (JSON string array encoded as a string), `selected` (zero-based index; -1 for none) |
| Svg | Inline static `svg` string; optional `textcolor` tint; scales to its layout bounds |
| TabLayout | `selectedid` chooses a child by name; `keepalive="true"` retains inactive pages while excluding their focus, semantics and animation tickers |
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

## Local SVG assets and retained navigation

`flui::Svg::SetSource()` embeds the complete SVG string in the document; it never
loads a path or URL. Supported static vector elements include paths, shapes, groups,
gradients, clip paths, masks and local `use` references. Scripts, CSS, external
references, raster images, fonts and animation are rejected. Use presentation
attributes and `textcolor` for a single-color theme tint. Include the SVG through
your resource build; its content remains the consumer's asset. The renderer owns
the pinned flutter_svg implementation and its license in bundled NOTICES.

Keep-alive tabs are opt-in because all child layouts remain mounted. They preserve
editor selection and list position; switching tabs removes focus from the hidden
page and does not restore keyboard focus automatically. They do not pause business
controllers, connections or subscriptions. The consumer owns those lifecycles.


## Resize handles

`ResizeHandle` / `flui::ResizeHandle` is an invisible splitter hit area. Set an
explicit height (vertical motion, default) or width (`direction="horizontal"`).
A tooltip supplies its accessible label. The pointer shows the appropriate resize
cursor; keyboard arrows move by 8 logical pixels and focus highlights the handle.

`resizestart` emits `0`; `resize` and `resizeend` emit a signed integer string:
cumulative displacement from this gesture's start in window logical pixels,
positive down/right. C++ `NotifyEvent::param1` contains that integer. Values are
not incremental deltas; save the initial size on start and derive each requested
size from it. A cancelled drag ends at its last reported displacement. The
consumer clamps sizes, remembers preferences, and reserves space for adjacent
content. The control itself has no application-specific sizing policy.

Resizing does not replace adjacent editors or submit their contents. Multiline
`Edit valign="top"` keeps text at the top of a tall editing region. As before,
Enter inserts a line break unless the consumer explicitly opts into handling it.


`List scrollanchor="bottom"` preserves the content at the visible bottom edge
when its viewport height changes (for example, a resizable pane or keyboard).
This also keeps the last item visible if the reader was at the end. A reader of
history stays at the same content boundary instead of jumping to the newest item.
Corrections happen during layout, before painting, and clamp at content limits.
Content-only updates retain the existing policy. Omit the attribute for ordinary
scroll behaviour. The retained scroll offset is updated after corrections.

`Icon strokewidth="1.4"` opts `chat`, `discover`, `settings`, `emoji`, `folder`, `bluetooth`, `lan`, and `globe` into thin stroke
outlines. Width is in a 24-unit view box, scaled with `iconsize` (clamped to 0.5–3).
Other glyphs, or omitted width, retain their original icons. Colors, semantics and
click target dimensions are unchanged.


`clip="true"` clips a control and its descendants to its `borderround` rectangle,
including hit testing outside the rounded corners. This lets adjacent child
panels share one outer silhouette; child background colors cannot cover the
parent's rounded corners. Clipping is opt-in; the default remains unclipped.


## Emoji picker

`EmojiPicker` / `flui::EmojiPicker` owns an anchored popover with a scrollable
Unicode emoji grid and session-only recent selections (at most ten per window).
It shares `Icon` glyph, size, stroke, color and tooltip styling; the popup follows
the window theme and stays inside the viewport. No external images, file access,
network calls or persistent history are involved. Fonts may render emoji
differently on each platform.

Set `target` to an Edit's ID or unique name (`SetTarget(edit)` in C++). `text` /
`textkey` labels the all-emoji section, `hint` / `hintkey` the recent section,
and `tooltip` / `tooltipkey` the trigger. Omit `items_json` for the SDK catalog;
supply a JSON string array to choose a custom catalog. `borderround` controls
the popup corner radius. For example:

```xml
<EmojiPicker target="draft" glyph="emoji" width="24" iconsize="21"
 strokewidth="1.4" textkey="emoji.all" hintkey="emoji.recent"
 tooltipkey="emoji.choose" />
```

Selecting inserts at the editor's caret or replaces its selection, then restores
input focus and emits the Edit's normal `valuechanged` with the complete draft.
It also emits the picker's `itemselect` with the Unicode choice. It never emits
Enter, clicks Send, or appends an automatic space. An active IME composing span
is committed as its displayed text before insertion; `maxchar` counts grapheme
clusters. A choice exceeding the limit leaves the draft and menu unchanged.

Click outside or press Escape to dismiss; arrow keys navigate and Enter selects.
Keep-alive page switches, disabled/hidden ancestors and deliberate editor resets
(`edit_revision`) dismiss the menu. Missing, ambiguous, read-only or disabled
edit targets cannot open it. Consumers continue to own draft, conversation and
submission lifecycles. This preview adds retained controls without changing C ABI.

### Optional large-emoji page

Set `largepage="true"` to add Normal / Large tabs. `normaltext` /
`normaltextkey` and `largetext` / `largetextkey` localize the two tabs.
Every opening starts on Normal. Tab switches pages; arrows, Enter and Escape
keep their existing behavior. The large grid uses 64 logical pixel glyphs in
88 pixel cells; glyphs scale down to fit when a platform font needs more room.
Both pages share the catalog and each owns up to ten recent choices in memory.

Normal selection still inserts into the target Edit. Large selection instead
emits `largeitemselect` with one Unicode emoji cluster, then dismisses the
popover and restores editor focus. It does not mutate the editor value,
selection or composing range, and does not emit `valuechanged` or `itemselect`.
The consumer owns the preview, cancel action, explicit Send, message type,
transport and durable history. Hidden/disabled targets and draft resets dismiss
both pages. This attribute is opt-in and changes no C ABI symbols.

## Emoji text size and wrapped metrics

`Edit emojiscale="1.4"` and `Label emojiscale="1.4"` enlarge Unicode emoji
without changing surrounding text. The scale defaults to 1 and must be finite
in the range 1–3. Classification uses complete grapheme clusters and Unicode
emoji presentation properties, including flags, skin tones, keycaps and ZWJ
families. Text-presentation symbols and ordinary digits keep their normal size.
Changing the scale preserves the editor value, selection and composing range.

A wrapping Label can opt into `event_textmeasure="true"`. After layout it emits
`textmeasure` with `"width,height"` in logical pixels (available text width and
rounded-up actual paragraph height). The event refreshes when text or metrics
change, even when the resulting height stays equal. In the C++ convenience
header, `Bind("textmeasure", ...)` enables this event and `MeasureText(width)`
returns the cached exact height for that width; before the first matching
measurement it returns a conservative estimate. Set the control height from
this result to avoid clipping large emoji or multiline content. This is a UI
measurement event, not a message send or persistence action.
