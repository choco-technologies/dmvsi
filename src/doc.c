#include "private.h"
#include <errno.h>
#include <string.h>

/*
 * Documents: a tree of nodes in the order they are painted. A node keeps
 * what it paints as a whole (bounds) - a group without a clip is as large
 * as what is in it, computed when it is closed.
 */

#define MAX_SIGMAS      3               /* A shadow's blur reaches this many standard deviations */

bool doc_valid(dmvsi_doc_t doc)
{
    return doc != NULL && doc->magic == DOC_MAGIC;
}

char* copy_string(const char* s, size_t length)
{
    char* copy = Dmod_Malloc(length + 1U);
    if (copy != NULL)
    {
        memcpy(copy, s, length);
        copy[length] = '\0';
    }
    return copy;
}

static bool rect_empty(const dmvsi_rect_t* r)
{
    return r->w <= 0 || r->h <= 0;
}

static void rect_union(dmvsi_rect_t* a, const dmvsi_rect_t* b)
{
    if (rect_empty(b))
        return;
    if (rect_empty(a))
    {
        *a = *b;
        return;
    }
    dmvsi_unit_t x1 = (a->x + a->w > b->x + b->w) ? a->x + a->w : b->x + b->w;
    dmvsi_unit_t y1 = (a->y + a->h > b->y + b->h) ? a->y + a->h : b->y + b->h;
    a->x = (a->x < b->x) ? a->x : b->x;
    a->y = (a->y < b->y) ? a->y : b->y;
    a->w = x1 - a->x;
    a->h = y1 - a->y;
}

static bool paint_valid(const dmvsi_paint_t* p)
{
    if (p->kind == DMVSI_PAINT_COLOR)
        return true;
    if (p->kind > DMVSI_PAINT_RADIAL || p->count < 2U || p->count > DMVSI_MAX_STOPS ||
        (p->kind == DMVSI_PAINT_RADIAL && (p->rx <= 0 || p->ry <= 0)))
        return false;
    for (uint32_t i = 1; i < p->count; i++)
    {
        if (p->stops[i].position < p->stops[i - 1U].position)
            return false;
    }
    return true;
}

dmvsi_doc_t doc_new(void)
{
    dmvsi_doc_t doc = Dmod_Malloc(sizeof(*doc));
    if (doc != NULL)
    {
        memset(doc, 0, sizeof(*doc));
        doc->magic = DOC_MAGIC;
    }
    return doc;
}

void doc_free(dmvsi_doc_t doc)
{
    while (doc->nodes != NULL)
    {
        node_t* n = doc->nodes;
        doc->nodes = n->all;
        Dmod_Free(n);
    }
    fonts_free(doc);
    for (uint32_t i = 0; i < doc->handler_count; i++)
    {
        for (uint32_t k = 0; k < doc->handlers[i].count; k++)
            Dmod_Free((char*)doc->handlers[i].actions[k].text);
        Dmod_Free(doc->handlers[i].actions);
    }
    Dmod_Free(doc->handlers);
    for (uint32_t i = 0; i < doc->var_count; i++)
        Dmod_Free(doc->vars[i].text);
    Dmod_Free(doc->vars);
    Dmod_Free(doc->timers);
    Dmod_Free(doc->name);
    doc->magic = 0;
    Dmod_Free(doc);
}

/* A node of `kind` with `extra` bytes after it (copies of strings), appended to the current group */
static node_t* new_node(dmvsi_doc_t doc, uint8_t kind, size_t extra)
{
    node_t* n = Dmod_Malloc(sizeof(*n) + extra);
    if (n == NULL)
        return NULL;
    memset(n, 0, sizeof(*n));
    n->pub.kind = kind;
    n->all = doc->nodes;
    doc->nodes = n;
    n->parent = doc->current;
    if (n->parent != NULL)
    {
        if (n->parent->last == NULL)
            n->parent->pub.first = &n->pub;
        else
            n->parent->last->pub.next = &n->pub;
        n->parent->last = n;
    }
    return n;
}

int doc_set_view(dmvsi_doc_t doc, const char* name, uint16_t width, uint16_t height)
{
    if (doc->root != NULL || name == NULL || width == 0 || height == 0)
        return -EINVAL;
    if ((doc->name = copy_string(name, strlen(name))) == NULL)
        return -ENOMEM;
    node_t* root = new_node(doc, DMVSI_NODE_GROUP, 0);
    if (root == NULL)
        return -ENOMEM;
    doc->width = width;
    doc->height = height;
    root->pub.u.group.rect.w = DMVSI_PX(width);
    root->pub.u.group.rect.h = DMVSI_PX(height);
    root->pub.u.group.flags = DMVSI_GROUP_CLIP;
    root->pub.u.group.opacity = 255;
    root->pub.bounds = root->pub.u.group.rect;
    doc->root = root;
    doc->current = root;
    return 0;
}

int doc_begin_group(dmvsi_doc_t doc, const dmvsi_group_t* group)
{
    if (doc->current == NULL || group == NULL)
        return -EINVAL;
    size_t name = (group->name != NULL) ? strlen(group->name) + 1U : 0;
    node_t* n = new_node(doc, DMVSI_NODE_GROUP, name);
    if (n == NULL)
        return -ENOMEM;
    n->pub.u.group = *group;
    if (group->name != NULL)
    {
        char* copy = (char*)(n + 1);
        memcpy(copy, group->name, name);
        n->pub.u.group.name = copy;
    }
    if ((group->flags & DMVSI_GROUP_CLIP) != 0)
        n->pub.bounds = group->rect;
    doc->current = n;
    return 0;
}

int doc_end_group(dmvsi_doc_t doc)
{
    node_t* n = doc->current;
    if (n == NULL || n == doc->root)
        return -EINVAL;
    if ((n->pub.u.group.flags & DMVSI_GROUP_CLIP) == 0)
    {
        dmvsi_rect_t bounds = n->pub.u.group.rect;      /* At least the rectangle it was given */
        for (const dmvsi_node_t* c = n->pub.first; c != NULL; c = c->next)
            rect_union(&bounds, &c->bounds);
        n->pub.u.group.rect = bounds;
        n->pub.bounds = bounds;
    }
    doc->current = n->parent;
    return 0;
}

static dmvsi_rect_t grown(const dmvsi_rect_t* r, dmvsi_unit_t by)
{
    dmvsi_rect_t g = { r->x - by, r->y - by, r->w + 2 * by, r->h + 2 * by };
    return g;
}

int doc_add(dmvsi_doc_t doc, uint8_t kind, const void* shape)
{
    if (doc->current == NULL || shape == NULL)
        return -EINVAL;
    node_t* n = NULL;
    switch (kind)
    {
        case DMVSI_NODE_RECT:
        {
            const dmvsi_fill_t* f = shape;
            if (!paint_valid(&f->paint) || f->radius < 0)
                return -EINVAL;
            if ((n = new_node(doc, kind, 0)) == NULL)
                return -ENOMEM;
            n->pub.u.fill = *f;
            n->pub.bounds = f->rect;
            break;
        }
        case DMVSI_NODE_FRAME:
        {
            const dmvsi_frame_t* f = shape;
            if (!paint_valid(&f->paint) || f->radius < 0 || f->width <= 0)
                return -EINVAL;
            if ((n = new_node(doc, kind, 0)) == NULL)
                return -ENOMEM;
            n->pub.u.frame = *f;
            n->pub.bounds = f->rect;
            break;
        }
        case DMVSI_NODE_SHADOW:
        {
            const dmvsi_shadow_t* s = shape;
            if (s->sigma < 0 || s->radius < 0 || s->hole_radius < 0)
                return -EINVAL;
            if ((n = new_node(doc, kind, 0)) == NULL)
                return -ENOMEM;
            n->pub.u.shadow = *s;
            n->pub.bounds = ((s->flags & DMVSI_SHADOW_INSET) != 0) ? s->hole : grown(&s->shape, MAX_SIGMAS * s->sigma);
            break;
        }
        case DMVSI_NODE_TEXT:
        {
            const dmvsi_text_t* t = shape;
            if (t->font == NULL || t->text == NULL || !paint_valid(&t->paint) || t->align > DMVSI_TEXT_RIGHT ||
                (t->var != 0 && (t->var > doc->var_count || doc->vars[t->var - 1U].kind != DMVSI_VAR_TEXT)))
                return -EINVAL;
            int status = font_add_chars(t->font, t->text, t->length);
            if (status == 0 && t->var != 0 && t->chars != NULL)
                status = font_add_chars(t->font, t->chars, strlen(t->chars));     /* What it may show */
            if (status != 0)
                return status;
            if ((n = new_node(doc, kind, t->length + 1U)) == NULL)
                return -ENOMEM;
            char* copy = (char*)(n + 1);
            memcpy(copy, t->text, t->length);
            copy[t->length] = '\0';
            n->pub.u.text = *t;
            n->pub.u.text.text = copy;
            n->pub.u.text.chars = NULL;                 /* In the font now */
            dmvsi_unit_t w = (t->var != 0 && t->width > 0) ? t->width : DMVSI_PX(text_width(t->font, t->text, t->length));
            dmvsi_rect_t b = { t->x, t->baseline - DMVSI_PX(t->font->ascent), w, DMVSI_PX(t->font->ascent + t->font->descent) };
            n->pub.bounds = b;
            break;
        }
        case DMVSI_NODE_IMAGE:
        {
            const dmvsi_image_t* im = shape;
            if (im->path == NULL || !paint_valid(&im->paint))
                return -EINVAL;
            size_t length = strlen(im->path);
            if ((n = new_node(doc, kind, length + 1U)) == NULL)
                return -ENOMEM;
            char* copy = (char*)(n + 1);
            memcpy(copy, im->path, length + 1U);
            n->pub.u.image = *im;
            n->pub.u.image.path = copy;
            n->pub.bounds = im->rect;
            break;
        }
        default:
            return -EINVAL;
    }
    return 0;
}

/* ---- Behaviour ---- */

/* Grown to hold one more of `size` bytes each */
static bool grow(void** items, uint32_t count, uint32_t* capacity, size_t size)
{
    if (count < *capacity)
        return true;
    uint32_t n = (*capacity == 0) ? 8U : *capacity * 2U;
    void* p = Dmod_Malloc(n * size);
    if (p == NULL)
        return false;
    if (count != 0)
        memcpy(p, *items, count * size);
    Dmod_Free(*items);
    *items = p;
    *capacity = n;
    return true;
}

static bool var_named(const dmvsi_doc_t doc, const char* name)
{
    for (uint32_t i = 0; i < doc->var_count; i++)
    {
        if (strcmp(doc->vars[i].name, name) == 0)
            return true;
    }
    return false;
}

/* A new variable named after `name` (made unique), NULL on failure */
static var_t* new_var(dmvsi_doc_t doc, const char* name)
{
    char base[MAX_VAR_NAME - 6U];
    size_t n = 0;
    if (doc->var_count >= 0xFFFEu || !grow((void**)&doc->vars, doc->var_count, &doc->var_capacity, sizeof(var_t)))
        return NULL;
    for (const char* s = (name != NULL) ? name : "v"; *s != '\0' && n + 1U < sizeof(base); s++)
    {
        char c = *s;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (n == 0 && c >= '0' && c <= '9')
            base[n++] = 'v';
        base[n++] = ok ? c : '_';
    }
    base[n] = '\0';
    if (n == 0)
        strcpy(base, "v");
    var_t* v = &doc->vars[doc->var_count];
    memset(v, 0, sizeof(*v));
    strcpy(v->name, base);
    for (uint32_t k = 2; var_named(doc, v->name); k++)
        Dmod_SnPrintf(v->name, sizeof(v->name), "%s_%u", base, (unsigned)k);
    return v;
}

dmvsi_var_t doc_add_var(dmvsi_doc_t doc, const char* name, int32_t initial)
{
    var_t* v = new_var(doc, name);
    if (v == NULL)
        return 0;
    v->kind = DMVSI_VAR_INT;
    v->initial = initial;
    doc->var_count++;
    return (dmvsi_var_t)doc->var_count;
}

dmvsi_var_t doc_add_text_var(dmvsi_doc_t doc, const char* name, uint16_t size, const char* initial)
{
    if (size == 0 || size > MAX_TEXT_VAR)
        return 0;
    var_t* v = new_var(doc, name);
    if (v == NULL)
        return 0;
    size_t n = (initial != NULL) ? strlen(initial) : 0;
    if (n > size)
    {
        n = size;
        while (n > 0 && ((uint8_t)initial[n] & 0xC0u) == 0x80u)
            n--;                                    /* Not in the middle of a character */
    }
    if ((v->text = copy_string((initial != NULL) ? initial : "", n)) == NULL)
        return 0;
    v->kind = DMVSI_VAR_TEXT;
    v->size = size;
    doc->var_count++;
    return (dmvsi_var_t)doc->var_count;
}

int doc_bind(dmvsi_doc_t doc, uint8_t what, dmvsi_var_t var)
{
    node_t* g = doc->current;
    if (g == NULL || g == doc->root || what >= DMVSI_BIND_COUNT || var == 0 || var > doc->var_count ||
        doc->vars[var - 1U].bound || doc->vars[var - 1U].kind != DMVSI_VAR_INT)
        return -EINVAL;
    doc->vars[var - 1U].bound = true;
    g->pub.bind[what] = var;
    return 0;
}

static bool is_if(uint8_t kind)
{
    return kind == DMVSI_ACT_IF_EQ || kind == DMVSI_ACT_IF_NE || (kind >= DMVSI_ACT_IF_LT && kind <= DMVSI_ACT_IF_GE);
}

/* An action fits the document: its variables (of the kinds it takes), its handler */
static bool action_valid(const dmvsi_doc_t doc, dmvsi_handler_t self, const dmvsi_action_t* a)
{
    switch (a->kind)
    {
        case DMVSI_ACT_END: case DMVSI_ACT_ELSE: case DMVSI_ACT_LOOP: case DMVSI_ACT_BREAK: case DMVSI_ACT_CONTINUE:
        case DMVSI_ACT_RETURN:
            return true;
        case DMVSI_ACT_CALL:
            return a->handler != 0 && a->handler <= doc->handler_count && a->handler != self;   /* Not itself */
        default:
            break;
    }
    if (a->kind > DMVSI_ACT_FORMAT || a->var == 0 || a->var > doc->var_count ||
        (a->operand > doc->var_count && a->operand != DMVSI_VAR_TIME))
        return false;
    bool text = doc->vars[a->var - 1U].kind == DMVSI_VAR_TEXT;
    uint8_t operand = (a->operand == DMVSI_VAR_TIME) ? DMVSI_VAR_INT :
                      (a->operand != 0) ? doc->vars[a->operand - 1U].kind : (text ? DMVSI_VAR_TEXT : DMVSI_VAR_INT);
    switch (a->kind)
    {
        case DMVSI_ACT_SET:
        case DMVSI_ACT_APPEND:
            if (text)
                return operand == DMVSI_VAR_TEXT && (a->operand != 0 || a->text != NULL);
            return a->kind == DMVSI_ACT_SET && operand == DMVSI_VAR_INT;
        case DMVSI_ACT_FORMAT:
            return text && a->text != NULL && operand == DMVSI_VAR_INT;
        case DMVSI_ACT_ANIMATE:
            return !text && a->operand == 0;
        default:
            return !text && operand == DMVSI_VAR_INT;   /* TOGGLE, the IFs, arithmetic: integers */
    }
}

/* Its blocks nest: IF [ELSE] END, LOOP END; BREAK / CONTINUE inside a LOOP */
static bool blocks_valid(const dmvsi_action_t* actions, uint32_t count)
{
    char stack[MAX_NESTING];
    uint32_t depth = 0, loops = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t k = actions[i].kind;
        if (is_if(k) || k == DMVSI_ACT_LOOP)
        {
            if (depth >= MAX_NESTING)
                return false;
            stack[depth++] = (k == DMVSI_ACT_LOOP) ? 'L' : 'I';
            loops += (k == DMVSI_ACT_LOOP) ? 1U : 0U;
        }
        else if (k == DMVSI_ACT_ELSE)
        {
            if (depth == 0 || stack[depth - 1U] != 'I')
                return false;
            stack[depth - 1U] = 'E';                /* One ELSE per IF */
        }
        else if (k == DMVSI_ACT_END)
        {
            if (depth == 0)
                return false;
            loops -= (stack[--depth] == 'L') ? 1U : 0U;
        }
        else if ((k == DMVSI_ACT_BREAK || k == DMVSI_ACT_CONTINUE) && loops == 0)
            return false;
    }
    return depth == 0;
}

dmvsi_handler_t doc_new_handler(dmvsi_doc_t doc)
{
    if (doc->handler_count >= 0xFFFEu ||
        !grow((void**)&doc->handlers, doc->handler_count, &doc->handler_capacity, sizeof(handler_t)))
        return 0;
    handler_t* h = &doc->handlers[doc->handler_count];
    memset(h, 0, sizeof(*h));
    doc->handler_count++;
    return (dmvsi_handler_t)doc->handler_count;
}

int doc_set_handler(dmvsi_doc_t doc, dmvsi_handler_t handler, const dmvsi_action_t* actions, uint32_t count)
{
    if (handler == 0 || handler > doc->handler_count || doc->handlers[handler - 1U].made ||
        (actions == NULL && count != 0) || !blocks_valid(actions, count))
        return -EINVAL;
    for (uint32_t i = 0; i < count; i++)
    {
        if (!action_valid(doc, handler, &actions[i]))
            return -EINVAL;
    }
    dmvsi_action_t* copy = (count > 0) ? Dmod_Malloc(count * sizeof(dmvsi_action_t)) : NULL;
    if (count > 0 && copy == NULL)
        return -ENOMEM;
    for (uint32_t i = 0; i < count; i++)
    {
        copy[i] = actions[i];
        copy[i].text = NULL;
        if (actions[i].text != NULL && (copy[i].text = copy_string(actions[i].text, strlen(actions[i].text))) == NULL)
        {
            for (uint32_t k = 0; k < i; k++)
                Dmod_Free((char*)copy[k].text);
            Dmod_Free(copy);
            return -ENOMEM;
        }
    }
    handler_t* h = &doc->handlers[handler - 1U];
    h->actions = copy;
    h->count = count;
    h->made = true;
    return 0;
}

dmvsi_handler_t doc_add_handler(dmvsi_doc_t doc, const dmvsi_action_t* actions, uint32_t count)
{
    dmvsi_handler_t h = doc_new_handler(doc);
    if (h != 0 && doc_set_handler(doc, h, actions, count) != 0)
    {
        doc->handler_count--;                       /* Not made: forget it */
        return 0;
    }
    return h;
}

int doc_add_timer(dmvsi_doc_t doc, uint16_t ms, dmvsi_handler_t handler)
{
    if (ms < 10u || handler == 0 || handler > doc->handler_count)
        return -EINVAL;
    if (!grow((void**)&doc->timers, doc->timer_count, &doc->timer_capacity, sizeof(timer_entry_t)))
        return -ENOMEM;
    doc->timers[doc->timer_count].ms = ms;
    doc->timers[doc->timer_count].handler = handler;
    doc->timer_count++;
    return 0;
}

int doc_on_click(dmvsi_doc_t doc, dmvsi_handler_t handler)
{
    node_t* g = doc->current;
    if (g == NULL || g == doc->root || handler == 0 || handler > doc->handler_count)
        return -EINVAL;
    g->pub.click = handler;
    return 0;
}

int doc_show_when(dmvsi_doc_t doc, dmvsi_var_t var, int32_t value)
{
    node_t* g = doc->current;
    if (g == NULL || g == doc->root || var == 0 || (var != DMVSI_VAR_PRESSED && var > doc->var_count))
        return -EINVAL;
    for (uint32_t i = 0; i < DMVSI_MAX_SHOW; i++)
    {
        if (g->pub.show_var[i] == 0)
        {
            g->pub.show_var[i] = var;
            g->pub.show_value[i] = value;
            return 0;
        }
    }
    return -EINVAL;
}
