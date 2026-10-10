# dmvsi API Reference

`#include "dmvsi.h"`

## Units

Positions and sizes are `dmvsi_unit_t`: 1/64 pixel (`DMVSI_UNIT`,
`DMVSI_PX(n)`), as a browser's layout units. Percentages (gradient stops and
geometry) are 1/100 % (`DMVSI_PERCENT(n)`: 10000 is 100 %).

## Converting

| Function | |
|----------|-|
| `dmvsi_doc_t dmvsi_convert_file(path, options, status)` | Convert a file with the converter that recognizes it (or `dmvs_<extension>`, loaded); `status`: 0, `-ENOENT`, `-ENOTSUP` (no converter), or the converter's error |
| `const char* dmvsi_converter_name(doc)` | The module that converted it, e.g. `"dmvs_html"` |
| `void dmvsi_free(doc)` | Release a document and its fonts |

`dmvsi_options_t` (all optional, zero for the defaults):

| Field | |
|-------|-|
| `root` | The element the view is made of (an id); the view is its size. NULL: the whole page |
| `width`, `height` | The screen the page is laid out for (the converter's default) |
| `name` | The view's name (default: the file's) |
| `maps`, `map_count` | Where resources are: `{ from, to }` - a URL (or its beginning) and the file it is; `to` ending in `/` replaces the beginning (a directory), else the whole URL |

## Building a document (converters)

| Function | |
|----------|-|
| `dmvsi_new()` | An empty document |
| `dmvsi_set_view(doc, name, w, h)` | The view's name and size - once, first. The root group is the screen, clipped |
| `dmvsi_begin_group(doc, group)` / `dmvsi_end_group(doc)` | A group in the current one: `rect` (clip with `DMVSI_GROUP_CLIP`, else computed: what is in it), `opacity`, `scroll_w`/`scroll_h` (scrollable content), `name` |
| `dmvsi_add_fill(doc, fill)` | A rectangle `rect`, corners rounded by `radius`, filled with `paint` |
| `dmvsi_add_frame(doc, frame)` | An outline `width` thick inside `rect` |
| `dmvsi_add_shadow(doc, shadow)` | The shadow of a rounded rectangle `shape` (moved by its offset, grown by its spread) blurred with a Gaussian of `sigma`; shown outside `hole` - or inside it with `DMVSI_SHADOW_INSET`. A blurred element is a shadow with an empty hole |
| `dmvsi_add_text(doc, text)` | A line of UTF-8 text: its pen at `x`, on `baseline`, in `font`, painted with `paint` |
| `dmvsi_add_image(doc, image)` | An image file in `rect`; with `DMVSI_IMAGE_MASK` only its coverage, painted with `paint`. `width` x `height`: the size it is drawn at (scaled into it, its aspect kept; 0: its own), placed by `DMVSI_IMAGE_CENTER` / `_RIGHT` / `_MIDDLE` / `_BOTTOM` (else top left) and clipped to `rect`; `blur`: blurred with that standard deviation |

A `dmvsi_paint_t` is a color (`DMVSI_PAINT_COLOR`, 0xAARRGGBB, not
premultiplied) or a gradient placed on the shape as dmview places gradients:
`DMVSI_PAINT_LINEAR` (`angle`: 0 up, 90 right, 180 down) or
`DMVSI_PAINT_RADIAL` (`cx`, `cy`, `rx`, `ry` - also outside the shape), with
2 ... 16 `stops` (`color`, `position`), not decreasing. dmview interpolates
each channel on its own - a converter whose source interpolates otherwise
(CSS: premultiplied) adds stops in between.

## Behaviour (converters)

What a page does when it is used - switching screens, toggling things,
counting, showing text - is variables, groups bound to them, and handlers
of clicks, timers and the view being shown:

| Function | |
|----------|-|
| `dmvsi_var_t dmvsi_add_var(doc, name, initial)` | An integer variable (its name made an identifier, unique) |
| `dmvsi_var_t dmvsi_add_text_var(doc, name, size, initial)` | A text variable of up to `size` bytes (1 ... 1024) |
| `dmvsi_bind(doc, what, var)` | The innermost open group's `DMVSI_BIND_X` / `_Y` (a position on the screen, in units - the group's rectangle's), `_OPACITY` (0 ... 255) or `_W` / `_H` (its size in pixels, as a handler computes it) is the integer variable's value. A bound group is kept while it is off the screen |
| `dmvsi_handler_t dmvsi_add_handler(doc, actions, count)` | A handler: its actions, run in order (below) |
| `dmvsi_new_handler(doc)`, `dmvsi_set_handler(doc, h, actions, count)` | The same in two steps - for handlers that `CALL` it before it is made |
| `dmvsi_add_timer(doc, ms, handler)` | Run it every `ms` milliseconds (10 ...) while the view is shown |
| `dmvsi_set_init(doc, handler)` | Run it once when the view is shown, before it is drawn |
| `dmvsi_show_when(doc, var, value)` | Show the innermost open group only while `var == value` - also `DMVSI_VAR_PRESSED`, whether the box it is in is pressed; up to `DMVSI_MAX_SHOW`, all hold. An element's looks (one per state) are groups shown on their conditions |
| `dmvsi_on_click(doc, handler)` | Run it when the innermost open group is clicked (its rectangle: at least the one it was given) |
| `dmvsi_var_at(doc, i, &name, &initial)`, `dmvsi_var_info(doc, var, &info)`, `dmvsi_handler_actions(doc, h, &actions)`, `dmvsi_timer_at(doc, i, &ms, &h)`, `dmvsi_init_handler(doc)` | Reading them (writers); a group node's `bind[]` and `click` |

A text node shows a text variable when its `var` is set: `text` is the
variable's initial text (what the line is measured by), `chars` every
character it may show (added to the font), `width` / `align`
(`DMVSI_TEXT_LEFT` / `_CENTER` / `_RIGHT`) where in the line it is put as
its length changes.

### Actions

`dmvsi_action_t`: `kind`, `var`, and the operand - `operand` (a variable of
the same kind as `var`, or `DMVSI_VAR_TIME`: the milliseconds since the view
was shown) when it is set, else `value` (an integer) or `text` (copied). Blocks nest: `IF ... [ELSE] ... END`, `LOOP ... END`.

| Kind | |
|------|-|
| `SET` | `var` = the operand (a text variable: its text) |
| `ANIMATE` | `var` goes to `value` in `duration` ms, eased by a CSS `cubic-bezier` (`easing`, 1/1000) |
| `TOGGLE` | `var` = `var` == 0 ? 1 : 0 |
| `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `MIN`, `MAX` | `var` = `var` op the operand (32-bit integers, wrapping; `DIV` toward zero; by 0: 0) |
| `IF_EQ`, `IF_NE`, `IF_LT`, `IF_LE`, `IF_GT`, `IF_GE` | The actions up to its `ELSE` / `END` only when `var` op the operand holds (integers) |
| `ELSE` | What an `IF` does when it does not hold, up to its `END` |
| `LOOP`, `BREAK`, `CONTINUE` | The actions up to its `END` again and again; out of / to the start of the innermost `LOOP` |
| `CALL` | Run `handler` (not itself), then go on |
| `RETURN` | Out of the handler |
| `APPEND` | Text `var` += the operand's text |
| `FORMAT` | Text `var` = `text` (one `%d` / `%x`, a width: `%02d`) with the operand (an integer) |

## Fonts

| Function | |
|----------|-|
| `dmvsi_font(doc, file, size, tracking, status)` | The document's font of a TrueType / OpenType file at a pixel size, letter spacing in 1/100 pixel - the same for the same arguments. NULL file: the built-in font (its metrics estimated) |
| `dmvsi_font_info(font, info)` | Its ascent, descent (whole pixels, as in the `.dmvf`), size, tracking, file |
| `dmvsi_font_has(font, codepoint)` | Whether it has a glyph for it |
| `int32_t dmvsi_text_width(font, text, length)` | Width of UTF-8 text in pixels: its advances as dmview adds them up |
| `dmvsi_font_at(doc, i)`, `dmvsi_font_char(font, i, &c)` | The fonts, the characters text added with them uses |

## Reading a document (writers)

| Function | |
|----------|-|
| `dmvsi_view_name(doc)`, `dmvsi_view_size(doc, &w, &h)` | The view |
| `const dmvsi_node_t* dmvsi_root(doc)` | The root group; a node's `first` (a group's), `next`, `kind` (`DMVSI_NODE_*`), `u` (what it is), `bounds` (what it paints, a shadow all of its blur) |
| `dmvsi_utf8_next(&p, end)` | The next character of UTF-8 text |

## DIF

Implemented by every converter (see [writing-a-converter.md](writing-a-converter.md)):

```c
dmod_dmvsi_dif(1.0, bool, _probe,   ( const char* path, const uint8_t* head, size_t size ));
dmod_dmvsi_dif(1.0, int,  _convert, ( const char* path, const dmvsi_options_t* options, dmvsi_doc_t doc ));
```

- `_probe`: whether it knows the file, by its path and its first
  `DMVSI_PROBE_SIZE` bytes.
- `_convert`: describe the file in `doc` - `dmvsi_set_view()`, then groups
  and shapes in the order they are painted. 0, or `-EBADMSG` / `-ENOTSUP`
  (dmvsi asks the next converter), `-ENOENT`, `-EIO`, `-ENOMEM`.
