#ifndef DMVSI_PRIVATE_H
#define DMVSI_PRIVATE_H

#include "dmvsi.h"

/* A font file's tables that measuring needs - shared by its sizes */
typedef struct face face_t;
struct face
{
    face_t*         next;
    char*           path;
    uint16_t        units_per_em;
    int16_t         ascender;           /* hhea */
    int16_t         descender;
    uint16_t        hmetrics;           /* Glyphs with their own advance */
    uint8_t*        hmtx;
    uint32_t        hmtx_size;
    uint8_t*        cmap;               /* The Unicode subtable */
    uint32_t        cmap_size;
    uint16_t        cmap_format;        /* 4 or 12 */
};

struct dmvsi_font
{
    dmvsi_font_t    next;
    face_t*         face;               /* NULL: the built-in font */
    uint16_t        size;
    int32_t         tracking;
    int32_t         ascent;
    int32_t         descent;
    float           scale;              /* Pixels per font unit */
    uint32_t*       chars;              /* Used, increasing */
    uint32_t        char_count;
    uint32_t        char_capacity;
};

typedef struct node node_t;
struct node
{
    dmvsi_node_t    pub;                /* First: what the API hands out */
    node_t*         all;                /* Every node of the document, for freeing */
    node_t*         parent;
    node_t*         last;               /* A group's last node */
};

#define MAX_VAR_NAME    32u

typedef struct
{
    char            name[MAX_VAR_NAME];
    int32_t         initial;
    bool            bound;
} var_t;

typedef struct
{
    dmvsi_action_t* actions;
    uint32_t        count;
} handler_t;

struct dmvsi_doc
{
    uint32_t        magic;
    char*           name;
    uint16_t        width;
    uint16_t        height;
    node_t*         root;
    node_t*         current;            /* The innermost open group */
    node_t*         nodes;
    dmvsi_font_t    fonts;
    face_t*         faces;
    Dmod_Context_t* converter;
    var_t*          vars;
    uint32_t        var_count;
    uint32_t        var_capacity;
    handler_t*      handlers;
    uint32_t        handler_count;
    uint32_t        handler_capacity;
};

#define DOC_MAGIC       0x44535649u     /* 'IVSD' */

bool            doc_valid(dmvsi_doc_t doc);
char*           copy_string(const char* s, size_t length);

/* font.c */
dmvsi_font_t    font_get(dmvsi_doc_t doc, const char* file, uint16_t size, int32_t tracking, int* status);
int             font_info(dmvsi_font_t font, dmvsi_font_info_t* info);
bool            font_has(dmvsi_font_t font, uint32_t c);
bool            font_char(dmvsi_font_t font, uint32_t index, uint32_t* c);
int32_t         text_width(dmvsi_font_t font, const char* text, size_t length);
uint32_t        utf8_next(const char** p, const char* end);
int             font_add_chars(dmvsi_font_t font, const char* text, size_t length);
void            fonts_free(dmvsi_doc_t doc);

/* doc.c */
dmvsi_doc_t     doc_new(void);
void            doc_free(dmvsi_doc_t doc);
int             doc_set_view(dmvsi_doc_t doc, const char* name, uint16_t width, uint16_t height);
int             doc_begin_group(dmvsi_doc_t doc, const dmvsi_group_t* group);
int             doc_end_group(dmvsi_doc_t doc);
int             doc_add(dmvsi_doc_t doc, uint8_t kind, const void* shape);
dmvsi_var_t     doc_add_var(dmvsi_doc_t doc, const char* name, int32_t initial);
int             doc_bind(dmvsi_doc_t doc, uint8_t what, dmvsi_var_t var);
dmvsi_handler_t doc_add_handler(dmvsi_doc_t doc, const dmvsi_action_t* actions, uint32_t count);
int             doc_on_click(dmvsi_doc_t doc, dmvsi_handler_t handler);
int             doc_show_when(dmvsi_doc_t doc, dmvsi_var_t var, int32_t value);

#endif /* DMVSI_PRIVATE_H */
