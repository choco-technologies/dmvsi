# Writing a Converter

A converter is a dmf library module implementing the dmvsi DIF. It can live
in its own repository - public or private - and needs only dmvsi's headers.

## Name

Name the module **`dmvs_<format>`** after the format's file extension:
`dmvs_html` (`.htm`, `.xhtml` are aliases of `html`), `dmvs_qml`, ...
`dmvsi_convert_file()` then loads it on demand from the extension of a file
no enabled converter recognizes.

## Build

```cmake
set(DMOD_MODULE_NAME        dmvs_qml)
set(DMOD_DIF_IMPLS          dmvsi)

dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/qml.c
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmvsi@>=0.1
)
```

## Implementation

```c
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmvsi.h"
#include <errno.h>
#include <string.h>

dmod_dmvsi_dif_api_declaration(1.0, dmvs_qml, bool, _probe, ( const char* path, const uint8_t* head, size_t size ))
{
    const char* dot = strrchr(path, '.');
    return dot != NULL && strcmp(dot, ".qml") == 0;
}

dmod_dmvsi_dif_api_declaration(1.0, dmvs_qml, int, _convert, ( const char* path, const dmvsi_options_t* options, dmvsi_doc_t doc ))
{
    int status = dmvsi_set_view(doc, "settings", 480, 272);

    /* A rounded button with its label */
    dmvsi_fill_t button = { .rect = { DMVSI_PX(16), DMVSI_PX(60), DMVSI_PX(160), DMVSI_PX(48) },
                            .radius = DMVSI_PX(8), .paint = { .kind = DMVSI_PAINT_COLOR, .color = 0xFF3D85F5 } };
    if (status == 0)
        status = dmvsi_add_fill(doc, &button);

    dmvsi_font_t font = dmvsi_font(doc, "/fonts/Inter-Medium.otf", 16, 0, &status);
    if (font != NULL)
    {
        const char* label = "Save";
        int32_t width = dmvsi_text_width(font, label, strlen(label));     /* To center it */
        dmvsi_font_info_t info;
        dmvsi_font_info(font, &info);
        dmvsi_text_t text = { .x = DMVSI_PX(16 + (160 - width) / 2), .baseline = DMVSI_PX(60 + 24 + info.ascent / 2),
                              .text = label, .length = strlen(label), .font = font,
                              .paint = { .kind = DMVSI_PAINT_COLOR, .color = 0xFFFFFFFF } };
        status = dmvsi_add_text(doc, &text);
    }
    return status;
}

int dmod_init(const Dmod_Config_t* Config) { (void)Config; return 0; }
int dmod_deinit(void) { return 0; }
```

## Rules

- **Lay the page out yourself.** The document has absolute positions only;
  measure text with `dmvsi_text_width()` and the font's metrics - they are
  the ones the view draws with.
- **Paint in order.** Shapes are painted in the order they are added; a
  group clips (`DMVSI_GROUP_CLIP`), fades (`opacity`) or scrolls what is in
  it. A group without a clip is as large as what is in it.
- **Describe, don't emulate.** A shadow is `dmvsi_add_shadow()`, not
  rectangles of your own: how it is painted is the writer's, and the same
  for every converter.
- **Resources** (style sheets, fonts, images) are files; the options' `maps`
  say where a URL is.
- **No pointers in static data.** A module's data is not relocated when it is
  loaded (only its GOT is): a table of `const char*` - also a local one the
  compiler keeps as a copy - points where the module was linked. Keep tables
  as text (`char name[16]`, `"|a|b|c|"`), or fill them in at run time.
