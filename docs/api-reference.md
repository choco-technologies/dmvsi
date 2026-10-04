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
| `dmvsi_add_image(doc, image)` | An image file in `rect`; with `DMVSI_IMAGE_MASK` only its coverage, painted with `paint` |

A `dmvsi_paint_t` is a color (`DMVSI_PAINT_COLOR`, 0xAARRGGBB, not
premultiplied) or a gradient placed on the shape as dmview places gradients:
`DMVSI_PAINT_LINEAR` (`angle`: 0 up, 90 right, 180 down) or
`DMVSI_PAINT_RADIAL` (`cx`, `cy`, `rx`, `ry` - also outside the shape), with
2 ... 16 `stops` (`color`, `position`), not decreasing. dmview interpolates
each channel on its own - a converter whose source interpolates otherwise
(CSS: premultiplied) adds stops in between.

## Behaviour (converters)

What a page does when it is used - switching screens, toggling things -
is variables, groups bound to them and handlers of clicks:

| Function | |
|----------|-|
| `dmvsi_var_t dmvsi_add_var(doc, name, initial)` | An integer variable (its name made an identifier, unique) |
| `dmvsi_bind(doc, what, var)` | The innermost open group's `DMVSI_BIND_X` / `_Y` (a position on the screen, in units - the group's rectangle's) or `_OPACITY` (0 ... 255) is the variable's value. A bound group is kept while it is off the screen |
| `dmvsi_handler_t dmvsi_add_handler(doc, actions, count)` | A handler: `DMVSI_ACT_SET`, `_ANIMATE` (to `value` in `duration` ms, eased by a CSS `cubic-bezier`, 1/1000), `_TOGGLE`, `_IF_EQ` / `_IF_NE` ... `_END` |
| `dmvsi_on_click(doc, handler)` | Run it when the innermost open group is clicked (its rectangle: at least the one it was given) |
| `dmvsi_var_at(doc, i, &name, &initial)`, `dmvsi_handler_actions(doc, h, &actions)` | Reading them (writers); a group node's `bind[]` and `click` |

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
