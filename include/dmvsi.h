#ifndef DMVSI_H
#define DMVSI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod.h"
#include "dmvsi_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * dmvsi - the interface of the converters into dmview views. Every source
 * format (HTML, QML, ...) is a converter plugin: a separate dmf module
 * implementing the dmvsi DIF below, in any repository - dmvs_html for HTML
 * and CSS. A program that makes views (todmvs) uses the dmvsi API:
 * dmvsi_convert_file() finds the plugin that knows the file, and the plugin
 * describes what the page looks like in a document (dmvsi_doc_t):
 *
 *  - groups, which clip, fade or scroll what is in them (dmview boxes),
 *  - shapes in the order they are painted: rectangles (rounded, filled with
 *    a color or a gradient), outlines, shadows, text, images.
 *
 * The plugin lays the page out; the document has only absolute positions
 * and sizes, in 1/64 pixel (DMVSI_UNIT). How the document becomes
 * instructions of a view - which shapes need a box, how a shadow is drawn -
 * is the writer's (libtodmvs): every plugin gets it the same.
 *
 * Text is measured with the fonts of the document (dmvsi_font()) - the same
 * way todmvf makes them, so the view's text is as wide as the layout made
 * room for.
 *
 * What a page does when it is used - a converter that understands it (a
 * script that switches screens) - is variables, groups bound to them (their
 * position, their opacity) and handlers of clicks: actions that set the
 * variables or animate them. The writer makes them the view's variables,
 * BOX operands and handlers.
 */

/* ---- Units ---- */

/** Positions and sizes: 1/64 pixel. */
typedef int32_t dmvsi_unit_t;

#define DMVSI_UNIT              64
#define DMVSI_PX(n)             ((dmvsi_unit_t)(n) * DMVSI_UNIT)

/** Bytes of the start of a file a plugin recognizes it by (fewer when the file is shorter). */
#define DMVSI_PROBE_SIZE        256u

/* ---- Paints ---- */

#define DMVSI_PAINT_COLOR       0u      /**< One color */
#define DMVSI_PAINT_LINEAR      1u      /**< A linear gradient */
#define DMVSI_PAINT_RADIAL      2u      /**< A radial gradient */

#define DMVSI_MAX_STOPS         16u

/** Position of a gradient stop or of a radial gradient's geometry: 1/100 % (10000: 100 %). */
#define DMVSI_PERCENT(n)        ((n) * 100)

typedef struct
{
    uint32_t    color;          /**< 0xAARRGGBB, not premultiplied */
    uint16_t    position;       /**< 0 ... 10000, along the gradient */
} dmvsi_stop_t;

/**
 * What a shape is filled with: a color, or a gradient placed on the shape
 * (its x, y, w, h) as dmview places gradients - see assembly.md, Gradients.
 */
typedef struct
{
    uint8_t         kind;           /**< DMVSI_PAINT_* */
    uint8_t         count;          /**< Gradient: number of stops, 2 ... DMVSI_MAX_STOPS */
    int16_t         angle;          /**< Linear: degrees, 0 up, 90 right, 180 down (CSS) */
    int32_t         cx, cy;         /**< Radial: center, 1/100 % of the shape's width / height (also outside it) */
    int32_t         rx, ry;         /**< Radial: radii, 1/100 % of the shape's width / height, > 0 */
    uint32_t        color;          /**< Color */
    dmvsi_stop_t    stops[DMVSI_MAX_STOPS];
} dmvsi_paint_t;

/** A rectangle: x, y of its top-left corner, w, h. */
typedef struct
{
    dmvsi_unit_t    x, y, w, h;
} dmvsi_rect_t;

/* ---- Fonts ---- */

/** A font of a document: a font file at one size. */
typedef struct dmvsi_font* dmvsi_font_t;

/** Metrics of a font, in whole pixels - those of the .dmvf todmvf makes of it. */
typedef struct
{
    int32_t         ascent;         /**< Baseline below the top of a line */
    int32_t         descent;        /**< Below the baseline */
    uint16_t        size;           /**< Pixel size (em) */
    int32_t         tracking;       /**< Letter spacing in every advance, 1/100 pixel */
    const char*     file;           /**< The font file, NULL for the built-in font */
} dmvsi_font_info_t;

/* ---- Documents ---- */

typedef struct dmvsi_doc* dmvsi_doc_t;

/** A variable of the document: 1 ...; 0 is none. */
typedef uint16_t dmvsi_var_t;

#define DMVSI_VAR_INT           0u      /**< A 32-bit integer (dmvsi_add_var()) */
#define DMVSI_VAR_TEXT          1u      /**< Text of up to `size` bytes (dmvsi_add_text_var()) */

/** What a variable is (dmvsi_var_info()) */
typedef struct
{
    const char*     name;           /**< As the view names it */
    uint8_t         kind;           /**< DMVSI_VAR_* */
    int32_t         initial;        /**< DMVSI_VAR_INT */
    const char*     text;           /**< DMVSI_VAR_TEXT: its initial text */
    uint16_t        size;           /**< DMVSI_VAR_TEXT: bytes it holds at most */
} dmvsi_var_info_t;

/** What of a group a variable is (dmvsi_bind()) */
#define DMVSI_BIND_X            0u      /**< Its rectangle's x: the variable is a position on the screen, in units */
#define DMVSI_BIND_Y            1u      /**< Its rectangle's y */
#define DMVSI_BIND_OPACITY      2u      /**< Its opacity, 0 ... 255 */
#define DMVSI_BIND_W            3u      /**< Its rectangle's width, in pixels (of the view) - what a handler computes */
#define DMVSI_BIND_H            4u      /**< Its rectangle's height, in pixels */
#define DMVSI_BIND_COUNT        5u

/** The variable of whether the box a group is in is pressed (dmview's $box.pressed): 1 while it is */
#define DMVSI_VAR_PRESSED       0xFFFFu

/** Conditions of a group being shown (dmvsi_show_when()), at most */
#define DMVSI_MAX_SHOW          2u

/** A handler of the document: 1 ...; 0 is none. */
typedef uint16_t dmvsi_handler_t;

/*
 * Actions. "The operand" is `operand`'s value when it is set, else `value`
 * (a text variable's: `operand`'s text, else `text`). Blocks - IF ... [ELSE]
 * END, LOOP ... END - nest.
 */
#define DMVSI_ACT_SET           0u      /**< var = the operand (a text var: its text) */
#define DMVSI_ACT_ANIMATE       1u      /**< var goes to value in `duration` ms, eased by `easing` */
#define DMVSI_ACT_TOGGLE        2u      /**< var = (var == 0) ? 1 : 0 */
#define DMVSI_ACT_IF_EQ         3u      /**< The actions up to the matching DMVSI_ACT_ELSE / _END only when var == the operand */
#define DMVSI_ACT_IF_NE         4u      /**< ... only when var != the operand */
#define DMVSI_ACT_END           5u      /**< The end of an IF or a LOOP */
#define DMVSI_ACT_ADD           6u      /**< var += the operand (integers: 32-bit, wrapping) */
#define DMVSI_ACT_SUB           7u      /**< var -= the operand */
#define DMVSI_ACT_MUL           8u      /**< var *= the operand */
#define DMVSI_ACT_DIV           9u      /**< var /= the operand, toward zero; 0 when it is 0 */
#define DMVSI_ACT_MOD           10u     /**< var %= the operand; 0 when it is 0 */
#define DMVSI_ACT_MIN           11u     /**< var = min(var, the operand) */
#define DMVSI_ACT_MAX           12u     /**< var = max(var, the operand) */
#define DMVSI_ACT_IF_LT         13u     /**< IF var < the operand */
#define DMVSI_ACT_IF_LE         14u     /**< IF var <= the operand */
#define DMVSI_ACT_IF_GT         15u     /**< IF var > the operand */
#define DMVSI_ACT_IF_GE         16u     /**< IF var >= the operand */
#define DMVSI_ACT_ELSE          17u     /**< What an IF does when it does not hold, up to its END */
#define DMVSI_ACT_LOOP          18u     /**< The actions up to its END, again and again (until a BREAK) */
#define DMVSI_ACT_BREAK         19u     /**< Out of the innermost LOOP */
#define DMVSI_ACT_CONTINUE      20u     /**< To the start of the innermost LOOP */
#define DMVSI_ACT_CALL          21u     /**< Run handler `handler`, then go on */
#define DMVSI_ACT_RETURN        22u     /**< Out of the handler */
#define DMVSI_ACT_APPEND        23u     /**< Text var += the operand's text */
#define DMVSI_ACT_FORMAT        24u     /**< Text var = `text` (one %d / %x, a width: %02d) with the operand (an integer) */

/** One action of a handler */
typedef struct
{
    uint8_t         kind;           /**< DMVSI_ACT_* */
    dmvsi_var_t     var;
    int32_t         value;          /**< A position (of a variable bound to X / Y) in units, an opacity, a number */
    uint16_t        duration;       /**< ANIMATE: milliseconds */
    int16_t         easing[4];      /**< ANIMATE: cubic-bezier(x1, y1, x2, y2), 1/1000 (CSS's) */
    dmvsi_var_t     operand;        /**< The operand is this variable (0: `value` / `text`) - of the same kind as var */
    const char*     text;           /**< A text var's operand (copied); FORMAT's format */
    dmvsi_handler_t handler;        /**< CALL: what it runs */
} dmvsi_action_t;

#define DMVSI_NODE_GROUP        0u      /**< Holds other nodes */
#define DMVSI_NODE_RECT         1u      /**< A filled, optionally rounded rectangle */
#define DMVSI_NODE_FRAME        2u      /**< A rectangle's outline */
#define DMVSI_NODE_SHADOW       3u      /**< A blurred shadow of a rectangle */
#define DMVSI_NODE_TEXT         4u      /**< A line of text */
#define DMVSI_NODE_IMAGE        5u      /**< An image file */

#define DMVSI_GROUP_CLIP        0x01u   /**< What is in the group is clipped to its rectangle */

#define DMVSI_SHADOW_INSET      0x01u   /**< Inside `hole` (box-shadow inset) instead of outside it */

#define DMVSI_IMAGE_MASK        0x01u   /**< Only the image's coverage counts, painted with `paint` (dmview ICON) */
#define DMVSI_IMAGE_CENTER      0x02u   /**< Placed in the middle of `rect` across (else at its left) ... */
#define DMVSI_IMAGE_RIGHT       0x04u   /**< ... or at its right */
#define DMVSI_IMAGE_MIDDLE      0x08u   /**< In the middle of `rect` down (else at its top) ... */
#define DMVSI_IMAGE_BOTTOM      0x10u   /**< ... or at its bottom */

/** A group: what is in it is clipped to `rect`, faded, scrolled. */
typedef struct
{
    dmvsi_rect_t    rect;           /**< Clip; without DMVSI_GROUP_CLIP computed by dmvsi_end_group(): what is in it (and rect) */
    uint8_t         flags;          /**< DMVSI_GROUP_* */
    uint8_t         opacity;        /**< 255: opaque */
    dmvsi_unit_t    scroll_w;       /**< Scrollable content of this size (> rect), 0: none */
    dmvsi_unit_t    scroll_h;
    const char*     name;           /**< Optional: the element's name (id) */
} dmvsi_group_t;

/** A filled rectangle, its corners rounded by `radius`. */
typedef struct
{
    dmvsi_rect_t    rect;
    dmvsi_unit_t    radius;
    dmvsi_paint_t   paint;
} dmvsi_fill_t;

/** An outline `width` thick drawn inside `rect`. */
typedef struct
{
    dmvsi_rect_t    rect;
    dmvsi_unit_t    radius;
    dmvsi_unit_t    width;
    dmvsi_paint_t   paint;
} dmvsi_frame_t;

/**
 * The shadow of a rounded rectangle `shape` (already moved by its offset and
 * grown by its spread) blurred with a Gaussian of standard deviation
 * `sigma` (CSS: half the blur radius of box-shadow, the radius of blur()).
 * An outer shadow shows only outside `hole` (the element casting it), an
 * inset one only inside. A blurred element (filter: blur()) is a shadow
 * with an empty hole.
 */
typedef struct
{
    dmvsi_rect_t    shape;
    dmvsi_unit_t    radius;
    dmvsi_unit_t    sigma;
    uint32_t        color;
    dmvsi_rect_t    hole;
    dmvsi_unit_t    hole_radius;
    uint8_t         flags;          /**< DMVSI_SHADOW_* */
} dmvsi_shadow_t;

/** A line of text: its pen starts at x, on the baseline y. */
typedef struct
{
    dmvsi_unit_t    x;
    dmvsi_unit_t    baseline;
    const char*     text;           /**< UTF-8 - with `var`: the variable's initial text (what it is measured by) */
    size_t          length;         /**< Bytes of `text` */
    dmvsi_font_t    font;
    dmvsi_paint_t   paint;
    dmvsi_var_t     var;            /**< Its text is this text variable's, as the handlers change it (0: `text`) */
    const char*     chars;          /**< With `var`: every character it may show (UTF-8, added to the font) */
    dmvsi_unit_t    width;          /**< With `var`: the room it is placed in from x (0: as wide as `text`) ... */
    uint8_t         align;          /**< ... by DMVSI_TEXT_* */
} dmvsi_text_t;

#define DMVSI_TEXT_LEFT         0u
#define DMVSI_TEXT_CENTER       1u
#define DMVSI_TEXT_RIGHT        2u

/** An image file shown in `rect`. */
typedef struct
{
    dmvsi_rect_t    rect;
    const char*     path;           /**< The image file (as the plugin found it) */
    uint8_t         flags;          /**< DMVSI_IMAGE_* */
    dmvsi_paint_t   paint;          /**< DMVSI_IMAGE_MASK: what the coverage is painted with */
    dmvsi_unit_t    width;          /**< The size it is drawn at - the file scaled into it, its aspect kept; */
    dmvsi_unit_t    height;         /**< 0: its own. Placed in `rect` by the flags and clipped to it */
    dmvsi_unit_t    blur;           /**< Blurred: the standard deviation of the Gaussian blur, 0: sharp */
} dmvsi_image_t;

/** A node of a document, as the writer reads it (dmvsi_root()). */
typedef struct dmvsi_node dmvsi_node_t;
struct dmvsi_node
{
    uint8_t             kind;       /**< DMVSI_NODE_* */
    const dmvsi_node_t* next;       /**< The next node of the same group, painted after this one */
    const dmvsi_node_t* first;      /**< DMVSI_NODE_GROUP: its first node */
    union
    {
        dmvsi_group_t   group;
        dmvsi_fill_t    fill;
        dmvsi_frame_t   frame;
        dmvsi_shadow_t  shadow;
        dmvsi_text_t    text;       /**< `text` is the document's copy */
        dmvsi_image_t   image;      /**< `path` is the document's copy */
    } u;
    dmvsi_rect_t        bounds;     /**< What the node paints (a shadow: all of its blur), as a whole */
    dmvsi_var_t         bind[DMVSI_BIND_COUNT];     /**< A group: the variables it is bound to (0: none) */
    dmvsi_handler_t     click;      /**< A group: run when it is clicked (0: none) */
    dmvsi_var_t         show_var[DMVSI_MAX_SHOW];   /**< A group: shown only while each var == show_value (0: no condition) */
    int32_t             show_value[DMVSI_MAX_SHOW];
};

/* ---- Options ---- */

/** A resource that is somewhere else than its URL says: `from` (a URL or a path prefix) is `to`. */
typedef struct
{
    const char*     from;
    const char*     to;             /**< Replaces `from` when it ends with '/' (a directory), else the whole URL */
} dmvsi_map_t;

/** How a file is converted. */
typedef struct
{
    const char*         root;       /**< The element the view is made of (an id), NULL: the whole page */
    uint16_t            width;      /**< Size of the screen the page is laid out for, 0: the plugin's default */
    uint16_t            height;
    const char*         name;       /**< Name of the view, NULL: the file's name */
    const dmvsi_map_t*  maps;       /**< Where resources (style sheets, fonts, images) are */
    uint32_t            map_count;
} dmvsi_options_t;

/* ---- DIF - implemented by every converter plugin ---- */

/**
 * @brief Whether the plugin recognizes a file by its first bytes and its path.
 * @param head First DMVSI_PROBE_SIZE bytes (fewer when the file is shorter)
 */
dmod_dmvsi_dif(1.0, bool, _probe, ( const char* path, const uint8_t* head, size_t size ));

/**
 * @brief Convert a file: lay it out and describe it in @p doc (dmvsi_set_view(),
 *        then groups and shapes in the order they are painted).
 * @return 0, -EBADMSG (not a file of this format, damaged), -ENOTSUP, -ENOENT, -EIO, -ENOMEM
 */
dmod_dmvsi_dif(1.0, int, _convert, ( const char* path, const dmvsi_options_t* options, dmvsi_doc_t doc ));

/* ---- API - for programs that make views ---- */

/**
 * @brief Convert a file into a document: find the plugin that recognizes it
 *        among the enabled modules implementing the dmvsi DIF. When none
 *        does, the module named after the file's extension - dmvs_<extension>,
 *        e.g. dmvs_html, also for .htm - is loaded and asked too.
 * @param options May be NULL
 * @param status Receives 0, -ENOENT, -ENOTSUP (no plugin knows it), or the plugin's error (may be NULL)
 * @return The document (dmvsi_free()), NULL on failure
 */
dmod_dmvsi_api(1.0, dmvsi_doc_t, _convert_file, ( const char* path, const dmvsi_options_t* options, int* status ));

/** @brief Name of the module that converted the document, e.g. "dmvs_html"; NULL for one made with dmvsi_new(). */
dmod_dmvsi_api(1.0, const char*, _converter_name, ( dmvsi_doc_t doc ));

/** @brief An empty document (for plugins' tests, documents made in code). */
dmod_dmvsi_api(1.0, dmvsi_doc_t, _new, ( void ));

/** @brief Release a document and its fonts. Safe on NULL. */
dmod_dmvsi_api(1.0, void, _free, ( dmvsi_doc_t doc ));

/* ---- API - building a document (plugins) ---- */

/**
 * @brief Name and size of the view; once, before anything is added. The
 *        root group is the screen: 0, 0, width, height, clipped.
 * @return 0, -EINVAL
 */
dmod_dmvsi_api(1.0, int, _set_view, ( dmvsi_doc_t doc, const char* name, uint16_t width, uint16_t height ));

/**
 * @brief Open a group in the current one; what is added until
 *        dmvsi_end_group() is in it.
 * @return 0, -ENOMEM, -EINVAL
 */
dmod_dmvsi_api(1.0, int, _begin_group, ( dmvsi_doc_t doc, const dmvsi_group_t* group ));

/** @brief Close the innermost group. @return 0, -EINVAL (none is open) */
dmod_dmvsi_api(1.0, int, _end_group, ( dmvsi_doc_t doc ));

/** @brief Add a filled rectangle. @return 0, -ENOMEM, -EINVAL */
dmod_dmvsi_api(1.0, int, _add_fill, ( dmvsi_doc_t doc, const dmvsi_fill_t* fill ));

/** @brief Add an outline. @return 0, -ENOMEM, -EINVAL */
dmod_dmvsi_api(1.0, int, _add_frame, ( dmvsi_doc_t doc, const dmvsi_frame_t* frame ));

/** @brief Add a shadow. @return 0, -ENOMEM, -EINVAL */
dmod_dmvsi_api(1.0, int, _add_shadow, ( dmvsi_doc_t doc, const dmvsi_shadow_t* shadow ));

/** @brief Add a line of text (copied); its characters are added to its font. @return 0, -ENOMEM, -EINVAL */
dmod_dmvsi_api(1.0, int, _add_text, ( dmvsi_doc_t doc, const dmvsi_text_t* text ));

/** @brief Add an image (its path copied). @return 0, -ENOMEM, -EINVAL */
dmod_dmvsi_api(1.0, int, _add_image, ( dmvsi_doc_t doc, const dmvsi_image_t* image ));

/* ---- API - behaviour ---- */

/**
 * @brief A variable of the document (an integer), its name for the view (letters,
 *        digits, '_'; unique - made so with a number when it is not).
 * @return The variable, 0 on failure
 */
dmod_dmvsi_api(1.0, dmvsi_var_t, _add_var, ( dmvsi_doc_t doc, const char* name, int32_t initial ));

/**
 * @brief A text variable of up to `size` bytes (1 ... 1024), its initial text
 *        (copied, cut to `size`) - named as dmvsi_add_var() names them.
 * @return The variable, 0 on failure
 */
dmod_dmvsi_api(1.0, dmvsi_var_t, _add_text_var, ( dmvsi_doc_t doc, const char* name, uint16_t size, const char* initial ));

/** @brief What a variable (1 ...) is. @return 0, -EINVAL (no such variable) */
dmod_dmvsi_api(1.0, int, _var_info, ( dmvsi_doc_t doc, dmvsi_var_t var, dmvsi_var_info_t* info ));

/**
 * @brief Bind the innermost open group's position or opacity to a variable
 *        (DMVSI_BIND_*): the group is where (or as opaque as) the variable
 *        says. A variable is bound to one group at most. A bound group is
 *        kept even while it is off the screen.
 * @return 0, -EINVAL
 */
dmod_dmvsi_api(1.0, int, _bind, ( dmvsi_doc_t doc, uint8_t what, dmvsi_var_t var ));

/**
 * @brief A handler: actions run in order (DMVSI_ACT_*; blocks nest).
 * @return The handler, 0 on failure (-EINVAL: a block without its END, an
 *         action that does not fit its variables)
 */
dmod_dmvsi_api(1.0, dmvsi_handler_t, _add_handler, ( dmvsi_doc_t doc, const dmvsi_action_t* actions, uint32_t count ));

/**
 * @brief A handler without its actions yet - for the handlers that CALL it
 *        before it is made (dmvsi_set_handler()).
 * @return The handler, 0 on failure
 */
dmod_dmvsi_api(1.0, dmvsi_handler_t, _new_handler, ( dmvsi_doc_t doc ));

/** @brief The actions of a handler of dmvsi_new_handler() - once. @return 0, -EINVAL, -ENOMEM */
dmod_dmvsi_api(1.0, int, _set_handler, ( dmvsi_doc_t doc, dmvsi_handler_t handler, const dmvsi_action_t* actions, uint32_t count ));

/** @brief Run a handler every `ms` milliseconds (10 ...) while the view is shown. @return 0, -EINVAL, -ENOMEM */
dmod_dmvsi_api(1.0, int, _add_timer, ( dmvsi_doc_t doc, uint16_t ms, dmvsi_handler_t handler ));

/** @brief Run a handler once when the view is shown, before it is drawn. @return 0, -EINVAL */
dmod_dmvsi_api(1.0, int, _set_init, ( dmvsi_doc_t doc, dmvsi_handler_t handler ));

/**
 * @brief Show the innermost open group only while a variable has a value -
 *        also DMVSI_VAR_PRESSED, whether the box it is in is pressed. Up to
 *        DMVSI_MAX_SHOW conditions, all of them hold. Its variants of an
 *        element (one per state) are groups shown on their conditions.
 * @return 0, -EINVAL
 */
dmod_dmvsi_api(1.0, int, _show_when, ( dmvsi_doc_t doc, dmvsi_var_t var, int32_t value ));

/** @brief Run a handler when the innermost open group is clicked. @return 0, -EINVAL */
dmod_dmvsi_api(1.0, int, _on_click, ( dmvsi_doc_t doc, dmvsi_handler_t handler ));

/** @brief The variables: the @p index -th (0 ...), false past the last. */
dmod_dmvsi_api(1.0, bool, _var_at, ( dmvsi_doc_t doc, uint32_t index, const char** name, int32_t* initial ));

/** @brief A handler's actions. @return Their number; *actions stays NULL for no such handler */
dmod_dmvsi_api(1.0, uint32_t, _handler_actions, ( dmvsi_doc_t doc, dmvsi_handler_t handler, const dmvsi_action_t** actions ));

/** @brief The timers: the @p index -th (0 ...), false past the last. */
dmod_dmvsi_api(1.0, bool, _timer_at, ( dmvsi_doc_t doc, uint32_t index, uint16_t* ms, dmvsi_handler_t* handler ));

/** @brief The handler run when the view is shown, 0: none. */
dmod_dmvsi_api(1.0, dmvsi_handler_t, _init_handler, ( dmvsi_doc_t doc ));

/* ---- API - fonts ---- */

/**
 * @brief The font of a document made of a font file (TrueType / OpenType)
 *        at a pixel size, with letter spacing - the same for the same
 *        arguments. NULL file: the built-in font (its metrics are estimated).
 * @param tracking Letter spacing added to every advance, 1/100 pixel (CSS letter-spacing)
 * @param status Receives 0, -ENOENT, -EBADMSG (not a font, or no Unicode map), -ENOMEM, -EINVAL (may be NULL)
 * @return The font (owned by the document), NULL on failure
 */
dmod_dmvsi_api(1.0, dmvsi_font_t, _font, ( dmvsi_doc_t doc, const char* file, uint16_t size, int32_t tracking, int* status ));

/** @brief Metrics of a font. @return 0, -EINVAL */
dmod_dmvsi_api(1.0, int, _font_info, ( dmvsi_font_t font, dmvsi_font_info_t* info ));

/** @brief Whether the font has a glyph for the codepoint (the built-in font: printable ASCII). */
dmod_dmvsi_api(1.0, bool, _font_has, ( dmvsi_font_t font, uint32_t codepoint ));

/** @brief Width of UTF-8 text in the font, in pixels: its advances, as dmview adds them up. */
dmod_dmvsi_api(1.0, int32_t, _text_width, ( dmvsi_font_t font, const char* text, size_t length ));

/* ---- API - reading a document (writers) ---- */

/** @brief The view's name, NULL before dmvsi_set_view(). */
dmod_dmvsi_api(1.0, const char*, _view_name, ( dmvsi_doc_t doc ));

/** @brief The view's size. @return 0, -EINVAL (no view yet) */
dmod_dmvsi_api(1.0, int, _view_size, ( dmvsi_doc_t doc, uint16_t* width, uint16_t* height ));

/** @brief The root group (the screen), NULL before dmvsi_set_view(). */
dmod_dmvsi_api(1.0, const dmvsi_node_t*, _root, ( dmvsi_doc_t doc ));

/** @brief The document's fonts, by index 0 ... (in the order they were made); NULL past the last. */
dmod_dmvsi_api(1.0, dmvsi_font_t, _font_at, ( dmvsi_doc_t doc, uint32_t index ));

/**
 * @brief The characters of the font that text uses, in increasing order:
 *        the @p index -th one, false past the last.
 */
dmod_dmvsi_api(1.0, bool, _font_char, ( dmvsi_font_t font, uint32_t index, uint32_t* codepoint ));

/** @brief Next character of UTF-8 text at *p (advanced past it); invalid bytes are U+FFFD. */
dmod_dmvsi_api(1.0, uint32_t, _utf8_next, ( const char** p, const char* end ));

#ifdef __cplusplus
}
#endif

#endif /* DMVSI_H */
