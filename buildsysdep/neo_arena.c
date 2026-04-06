#include "neo_internal.h"

static neo_arena_page_t *neo__arena_new_page(size_t min_size)
{
    size_t cap = min_size > NEO_ARENA_DEFAULT_PAGE ? min_size : NEO_ARENA_DEFAULT_PAGE;
    neo_arena_page_t *p = (neo_arena_page_t *)malloc(sizeof(neo_arena_page_t) + cap);
    if (!p) return NULL;
    p->next = NULL;
    p->used = 0;
    p->capacity = cap;
    return p;
}

neo_arena_t *neo_arena_create(size_t page_size)
{
    neo_arena_t *a = (neo_arena_t *)calloc(1, sizeof(neo_arena_t));
    if (!a) return NULL;
    a->default_page_size = page_size > 0 ? page_size : NEO_ARENA_DEFAULT_PAGE;
    a->current = neo__arena_new_page(a->default_page_size);
    if (!a->current) { free(a); return NULL; }
    a->pages = a->current;
    return a;
}

void *neo_arena_alloc(neo_arena_t *a, size_t size)
{
    if (!a || size == 0) return NULL;
    size_t aligned = (size + NEO_ARENA_ALIGN - 1) & ~(size_t)(NEO_ARENA_ALIGN - 1);

    if (a->current && a->current->used + aligned <= a->current->capacity) {
        void *ptr = a->current->data + a->current->used;
        a->current->used += aligned;
        return ptr;
    }

    /* need new page */
    size_t psize = aligned > a->default_page_size ? aligned : a->default_page_size;
    neo_arena_page_t *p = neo__arena_new_page(psize);
    if (!p) return NULL;
    p->next = a->pages;
    a->pages = p;
    a->current = p;
    void *ptr = p->data;
    p->used = aligned;
    return ptr;
}

char *neo_arena_strdup(neo_arena_t *a, const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = (char *)neo_arena_alloc(a, len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

char *neo_arena_sprintf(neo_arena_t *a, const char *fmt, ...)
{
    va_list args, copy;
    va_start(args, fmt);
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) { va_end(args); return NULL; }
    char *buf = (char *)neo_arena_alloc(a, (size_t)needed + 1);
    if (buf) vsnprintf(buf, (size_t)needed + 1, fmt, args);
    va_end(args);
    return buf;
}

void neo_arena_destroy(neo_arena_t *a)
{
    if (!a) return;
    neo_arena_page_t *p = a->pages;
    while (p) {
        neo_arena_page_t *next = p->next;
        free(p);
        p = next;
    }
    free(a);
}
