# dmvsi

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmvsi/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmvsi/actions/workflows/ci.yml)

The interface of the converters into dmview views.

## Description

Every source format a view can be made of - HTML and CSS, and later others
(QML, a design tool's export, ...) - is a **converter plugin**: a separate
dmf module that implements the dmvsi DIF - `dmvs_html`, ... A plugin can
live in any repository; a program that makes views does not depend on any
of them.

A converter lays its page out and describes what it looks like in a
**document**:

- **groups**, which clip, fade (opacity) or scroll what is in them,
- **shapes** in the order they are painted: rectangles (rounded, filled
  with a color or a linear / radial gradient), outlines, shadows (blurred,
  outer or inset - also a blurred element), lines of text, images.

Everything is at absolute positions, in 1/64 pixel. How a document becomes
dmview assembly - which group needs a box, how a shadow is painted - is the
writer's, [libtodmvs](https://github.com/choco-technologies/todmvs): every
converter gets the same.

Text is measured with the document's **fonts** - a TrueType / OpenType file
at a pixel size, with letter spacing - exactly as todmvf makes the `.dmvf`
the view will draw it in (the same rounding of the ascent, the descent and
every advance), so the text fits the room the layout made for it.

```c
#include "dmvsi.h"

dmvsi_options_t options = { .root = "screen", .width = 480, .height = 272 };
int status;
dmvsi_doc_t doc = dmvsi_convert_file("/sd/ui/home.html", &options, &status);
if (doc != NULL)
{
    const dmvsi_node_t* root = dmvsi_root(doc);     /* The screen: its groups and shapes */
    /* ... */
    dmvsi_free(doc);
}
```

`dmvsi_convert_file()` reads the first bytes of the file and asks the
enabled converters which of them recognizes it; when none does, it loads the
module named after the file's extension - `dmvs_html` for `.html`, `.htm`.

## Documentation

- [docs/api-reference.md](docs/api-reference.md) - the API and the DIF
- [docs/writing-a-converter.md](docs/writing-a-converter.md) - a converter plugin, step by step

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Testing

The tests use a converter of their own, `dmvs_tsrc` (tests/tsrc/), and a
font, Inter (tests/fonts/, SIL OFL). Once built, run them with `ctest`:

```bash
cd build
ctest --output-on-failure
```

## License

MIT - see [LICENSE](LICENSE).
