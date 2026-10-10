#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "private.h"
#include <errno.h>
#include <string.h>

/*
 * The dmvsi API: finding the converter of a file and handing the file to
 * it, the document it describes the view in (doc.c), the fonts the text is
 * measured with (font.c). The converters are the enabled modules
 * implementing the dmvsi DIF; each recognizes its format by the file's
 * first bytes and its name (_probe).
 */

#define MODULE_PREFIX       "dmvs_"
#define MAX_EXTENSION       8u
#define NO_CONVERTER        1               /* Internal: no plugin recognized the file */

/* Extensions that name another module than dmvs_<extension> */
static const struct
{
    char    extension[MAX_EXTENSION];
    char    format[MAX_EXTENSION];
} g_aliases[] = {
    { "htm",   "html" },
    { "xhtml", "html" },
};

/* ---- Converters ---- */

static Dmod_Context_t* find_converter(const char* path, const uint8_t* head, size_t size, Dmod_Context_t* previous)
{
    Dmod_Context_t* m = previous;
    while ((m = Dmod_GetNextDifModule(dmod_dmvsi_probe_sig, m)) != NULL)
    {
        dmod_dmvsi_probe_t probe = (dmod_dmvsi_probe_t)Dmod_GetDifFunction(m, dmod_dmvsi_probe_sig);
        if (probe != NULL && probe(path, head, size))
            return m;
    }
    return NULL;
}

/* dmvs_<format> for the extension of `path`: the module's name, or false */
static bool module_for(const char* path, char* name, size_t size)
{
    const char* dot = strrchr(path, '.');
    const char* slash = strrchr(path, '/');
    char extension[MAX_EXTENSION];
    size_t n = 0;
    if (dot == NULL || (slash != NULL && dot < slash) || dot[1] == '\0')
        return false;
    for (const char* p = dot + 1; *p != '\0'; p++)
    {
        if (n + 1U >= sizeof(extension))
            return false;
        extension[n++] = (*p >= 'A' && *p <= 'Z') ? (char)(*p - 'A' + 'a') : *p;
    }
    extension[n] = '\0';

    const char* format = extension;
    for (size_t i = 0; i < sizeof(g_aliases) / sizeof(g_aliases[0]); i++)
    {
        if (strcmp(g_aliases[i].extension, extension) == 0)
            format = g_aliases[i].format;
    }
    if (sizeof(MODULE_PREFIX) + strlen(format) > size)
        return false;
    memcpy(name, MODULE_PREFIX, sizeof(MODULE_PREFIX) - 1U);
    strcpy(name + sizeof(MODULE_PREFIX) - 1U, format);
    return true;
}

/* Load and enable the converter named after the file's extension */
static bool load_converter(const char* path)
{
    char name[sizeof(MODULE_PREFIX) + MAX_EXTENSION];
    if (!module_for(path, name, sizeof(name)))
        return false;
    if (Dmod_GetModuleContext(name) == NULL && Dmod_LoadModuleByName(name) == NULL)
        return false;
    return Dmod_IsModuleEnabled(name) || Dmod_EnableModule(name, false, NULL);
}

/* The first converter that recognizes the file and converts it */
static int try_converters(dmvsi_doc_t* doc, const char* path, const uint8_t* head, size_t size, const dmvsi_options_t* options)
{
    int ret = NO_CONVERTER;
    for (Dmod_Context_t* m = find_converter(path, head, size, NULL); m != NULL; m = find_converter(path, head, size, m))
    {
        dmod_dmvsi_convert_t convert = (dmod_dmvsi_convert_t)Dmod_GetDifFunction(m, dmod_dmvsi_convert_sig);
        if (convert == NULL)
        {
            DMOD_LOG_ERROR("dmvsi: %s does not implement the whole converter interface\n", Dmod_GetName(m));
            ret = -ENOTSUP;
            continue;
        }
        if ((*doc = doc_new()) == NULL)
            return -ENOMEM;
        (*doc)->converter = m;
        if ((ret = convert(path, options, *doc)) == 0 && (*doc)->root == NULL)
            ret = -EBADMSG;            /* It described no view */
        if (ret == 0)
            return 0;
        doc_free(*doc);
        *doc = NULL;
        if (ret != -EBADMSG && ret != -ENOTSUP)
            return ret;                /* Not a matter of another converter */
    }
    return ret;
}

static size_t read_head(const char* path, uint8_t* head, int* status)
{
    void* f = Dmod_FileOpen(path, "rb");
    if (f == NULL)
    {
        *status = -ENOENT;
        return 0;
    }
    size_t size = Dmod_FileRead(head, 1, DMVSI_PROBE_SIZE, f);
    Dmod_FileClose(f);
    *status = 0;
    return size;
}

/* ---- API ---- */

dmod_dmvsi_api_declaration(1.0, dmvsi_doc_t, _convert_file, ( const char* path, const dmvsi_options_t* options, int* status ))
{
    static const dmvsi_options_t defaults = { 0 };
    uint8_t head[DMVSI_PROBE_SIZE];
    int ret = -EINVAL;
    dmvsi_doc_t doc = NULL;
    if (path != NULL)
    {
        size_t size = read_head(path, head, &ret);
        if (options == NULL)
            options = &defaults;
        if (ret == 0 && (ret = try_converters(&doc, path, head, size, options)) == NO_CONVERTER && load_converter(path))
            ret = try_converters(&doc, path, head, size, options);
        if (ret == NO_CONVERTER)
        {
            DMOD_LOG_WARN("dmvsi: no converter for %s\n", path);
            ret = -ENOTSUP;
        }
    }
    if (status != NULL)
        *status = ret;
    return doc;
}

dmod_dmvsi_api_declaration(1.0, const char*, _converter_name, ( dmvsi_doc_t doc ))
{
    return (doc_valid(doc) && doc->converter != NULL) ? Dmod_GetName(doc->converter) : NULL;
}

dmod_dmvsi_api_declaration(1.0, dmvsi_doc_t, _new, ( void ))
{
    return doc_new();
}

dmod_dmvsi_api_declaration(1.0, void, _free, ( dmvsi_doc_t doc ))
{
    if (doc_valid(doc))
        doc_free(doc);
}

dmod_dmvsi_api_declaration(1.0, int, _set_view, ( dmvsi_doc_t doc, const char* name, uint16_t width, uint16_t height ))
{
    return doc_valid(doc) ? doc_set_view(doc, name, width, height) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, int, _begin_group, ( dmvsi_doc_t doc, const dmvsi_group_t* group ))
{
    return doc_valid(doc) ? doc_begin_group(doc, group) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, int, _end_group, ( dmvsi_doc_t doc ))
{
    return doc_valid(doc) ? doc_end_group(doc) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, int, _add_fill, ( dmvsi_doc_t doc, const dmvsi_fill_t* fill ))
{
    return doc_valid(doc) ? doc_add(doc, DMVSI_NODE_RECT, fill) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, int, _add_frame, ( dmvsi_doc_t doc, const dmvsi_frame_t* frame ))
{
    return doc_valid(doc) ? doc_add(doc, DMVSI_NODE_FRAME, frame) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, int, _add_shadow, ( dmvsi_doc_t doc, const dmvsi_shadow_t* shadow ))
{
    return doc_valid(doc) ? doc_add(doc, DMVSI_NODE_SHADOW, shadow) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, int, _add_text, ( dmvsi_doc_t doc, const dmvsi_text_t* text ))
{
    return doc_valid(doc) ? doc_add(doc, DMVSI_NODE_TEXT, text) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, int, _add_image, ( dmvsi_doc_t doc, const dmvsi_image_t* image ))
{
    return doc_valid(doc) ? doc_add(doc, DMVSI_NODE_IMAGE, image) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, dmvsi_font_t, _font, ( dmvsi_doc_t doc, const char* file, uint16_t size, int32_t tracking, int* status ))
{
    int ignored;
    if (status == NULL)
        status = &ignored;
    if (!doc_valid(doc) || size == 0)
    {
        *status = -EINVAL;
        return NULL;
    }
    return font_get(doc, file, size, tracking, status);
}

dmod_dmvsi_api_declaration(1.0, int, _font_info, ( dmvsi_font_t font, dmvsi_font_info_t* info ))
{
    return (font != NULL && info != NULL) ? font_info(font, info) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, bool, _font_has, ( dmvsi_font_t font, uint32_t codepoint ))
{
    return font != NULL && font_has(font, codepoint);
}

dmod_dmvsi_api_declaration(1.0, int32_t, _text_width, ( dmvsi_font_t font, const char* text, size_t length ))
{
    return (font != NULL && text != NULL) ? text_width(font, text, length) : 0;
}

dmod_dmvsi_api_declaration(1.0, const char*, _view_name, ( dmvsi_doc_t doc ))
{
    return doc_valid(doc) ? doc->name : NULL;
}

dmod_dmvsi_api_declaration(1.0, int, _view_size, ( dmvsi_doc_t doc, uint16_t* width, uint16_t* height ))
{
    if (!doc_valid(doc) || doc->root == NULL)
        return -EINVAL;
    if (width != NULL)
        *width = doc->width;
    if (height != NULL)
        *height = doc->height;
    return 0;
}

dmod_dmvsi_api_declaration(1.0, const dmvsi_node_t*, _root, ( dmvsi_doc_t doc ))
{
    return (doc_valid(doc) && doc->root != NULL) ? &doc->root->pub : NULL;
}

dmod_dmvsi_api_declaration(1.0, dmvsi_font_t, _font_at, ( dmvsi_doc_t doc, uint32_t index ))
{
    if (!doc_valid(doc))
        return NULL;
    dmvsi_font_t f = doc->fonts;
    for (; f != NULL && index > 0; index--)
        f = f->next;
    return f;
}

dmod_dmvsi_api_declaration(1.0, bool, _font_char, ( dmvsi_font_t font, uint32_t index, uint32_t* codepoint ))
{
    return font != NULL && codepoint != NULL && font_char(font, index, codepoint);
}

dmod_dmvsi_api_declaration(1.0, uint32_t, _utf8_next, ( const char** p, const char* end ))
{
    return (p != NULL && *p != NULL && *p < end) ? utf8_next(p, end) : 0;
}

dmod_dmvsi_api_declaration(1.0, dmvsi_var_t, _add_var, ( dmvsi_doc_t doc, const char* name, int32_t initial ))
{
    return doc_valid(doc) ? doc_add_var(doc, name, initial) : 0;
}

dmod_dmvsi_api_declaration(1.0, dmvsi_var_t, _add_text_var, ( dmvsi_doc_t doc, const char* name, uint16_t size, const char* initial ))
{
    return doc_valid(doc) ? doc_add_text_var(doc, name, size, initial) : 0;
}

dmod_dmvsi_api_declaration(1.0, int, _var_info, ( dmvsi_doc_t doc, dmvsi_var_t var, dmvsi_var_info_t* info ))
{
    if (!doc_valid(doc) || var == 0 || var > doc->var_count || info == NULL)
        return -EINVAL;
    const var_t* v = &doc->vars[var - 1U];
    memset(info, 0, sizeof(*info));
    info->name = v->name;
    info->kind = v->kind;
    info->initial = v->initial;
    info->text = v->text;
    info->size = v->size;
    return 0;
}

dmod_dmvsi_api_declaration(1.0, dmvsi_handler_t, _new_handler, ( dmvsi_doc_t doc ))
{
    return doc_valid(doc) ? doc_new_handler(doc) : 0;
}

dmod_dmvsi_api_declaration(1.0, int, _set_handler, ( dmvsi_doc_t doc, dmvsi_handler_t handler, const dmvsi_action_t* actions, uint32_t count ))
{
    return doc_valid(doc) ? doc_set_handler(doc, handler, actions, count) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, int, _add_timer, ( dmvsi_doc_t doc, uint16_t ms, dmvsi_handler_t handler ))
{
    return doc_valid(doc) ? doc_add_timer(doc, ms, handler) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, int, _set_init, ( dmvsi_doc_t doc, dmvsi_handler_t handler ))
{
    if (!doc_valid(doc) || handler == 0 || handler > doc->handler_count)
        return -EINVAL;
    doc->init = handler;
    return 0;
}

dmod_dmvsi_api_declaration(1.0, bool, _timer_at, ( dmvsi_doc_t doc, uint32_t index, uint16_t* ms, dmvsi_handler_t* handler ))
{
    if (!doc_valid(doc) || index >= doc->timer_count)
        return false;
    if (ms != NULL)
        *ms = doc->timers[index].ms;
    if (handler != NULL)
        *handler = doc->timers[index].handler;
    return true;
}

dmod_dmvsi_api_declaration(1.0, dmvsi_handler_t, _init_handler, ( dmvsi_doc_t doc ))
{
    return doc_valid(doc) ? doc->init : 0;
}

dmod_dmvsi_api_declaration(1.0, int, _bind, ( dmvsi_doc_t doc, uint8_t what, dmvsi_var_t var ))
{
    return doc_valid(doc) ? doc_bind(doc, what, var) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, dmvsi_handler_t, _add_handler, ( dmvsi_doc_t doc, const dmvsi_action_t* actions, uint32_t count ))
{
    return (doc_valid(doc) && (actions != NULL || count == 0)) ? doc_add_handler(doc, actions, count) : 0;
}

dmod_dmvsi_api_declaration(1.0, int, _on_click, ( dmvsi_doc_t doc, dmvsi_handler_t handler ))
{
    return doc_valid(doc) ? doc_on_click(doc, handler) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, int, _show_when, ( dmvsi_doc_t doc, dmvsi_var_t var, int32_t value ))
{
    return doc_valid(doc) ? doc_show_when(doc, var, value) : -EINVAL;
}

dmod_dmvsi_api_declaration(1.0, bool, _var_at, ( dmvsi_doc_t doc, uint32_t index, const char** name, int32_t* initial ))
{
    if (!doc_valid(doc) || index >= doc->var_count)
        return false;
    if (name != NULL)
        *name = doc->vars[index].name;
    if (initial != NULL)
        *initial = doc->vars[index].initial;
    return true;
}

dmod_dmvsi_api_declaration(1.0, uint32_t, _handler_actions, ( dmvsi_doc_t doc, dmvsi_handler_t handler, const dmvsi_action_t** actions ))
{
    static const dmvsi_action_t none[1];
    if (!doc_valid(doc) || handler == 0 || handler > doc->handler_count || actions == NULL)
        return 0;
    /* A handler without actions: not NULL, so it tells from none */
    *actions = (doc->handlers[handler - 1U].count > 0) ? doc->handlers[handler - 1U].actions : none;
    return doc->handlers[handler - 1U].count;
}

/* ---- Module ---- */

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}
