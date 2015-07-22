#ifndef pb_h
#define pb_h


#include <stddef.h>
#include <setjmp.h>


#ifndef PB_NS_BEGIN
# ifdef __cplusplus
#   define PB_NS_BEGIN extern "C" {
#   define PB_NS_END   }
# else
#   define PB_NS_BEGIN
#   define PB_NS_END
# endif
#endif /* PB_NS_BEGIN */

#if !defined(PB_API) && defined(_WIN32)
# ifdef PB_IMPLEMENTATION
#  define PB_API __declspec(dllexport)
# else
#  define PB_API __declspec(dllimport)
# endif
#endif

#ifndef PB_API
# define PB_API extern
#endif

#if defined(_MSC_VER) || defined(__UNIXOS2__) || defined(__SOL64__)
typedef unsigned char      uint8_t;
typedef signed   char       int8_t;
typedef unsigned short     uint16_t;
typedef signed   short      int16_t;
typedef unsigned int       uint32_t;
typedef signed   int        int32_t;
typedef unsigned long long uint64_t;
typedef signed   long long  int64_t;

#elif defined(__SCO__) || defined(__USLC__) || defined(__MINGW32__)
# include <stdint.h>
#else
# include <inttypes.h>
# if (defined(__sun__) || defined(__digital__))
#   if defined(__STDC__) && (defined(__arch64__) || defined(_LP64))
typedef unsigned long int  uint64_t;
typedef signed   long int   int64_t;
#   else
typedef unsigned long long uint64_t;
typedef signed   long long  int64_t;
#   endif /* LP64 */
# endif /* __sun__ || __digital__ */
#endif

/* limits */
#define PB_BUFFERSIZE   4096
#define PB_POOLSIZE     512
#define PB_HASHLIMIT    5
#define PB_MIN_TYPES    64
#define PB_MIN_HASHSIZE 8

/* wired types */
#define PB_WIRETYPES(X) \
    X(VARINT,  "varint",  0) \
    X(64BIT,   "64bit",   1) \
    X(DATA,    "bytes",   2) \
    X(GSTART,  "gstart",  3) \
    X(GEND,    "gend",    4) \
    X(32BIT,   "32bit",   5) \

/* declared types */
#define PB_TYPES(X) \
    X(double,   1 ) \
    X(float,    2 ) \
    X(int64,    3 ) \
    X(uint64,   4 ) \
    X(int32,    5 ) \
    X(fixed64,  6 ) \
    X(fixed32,  7 ) \
    X(bool,     8 ) \
    X(string,   9 ) \
    X(group,    10) \
    X(message,  11) \
    X(bytes,    12) \
    X(uint32,   13) \
    X(enum,     14) \
    X(sfixed32, 15) \
    X(sfixed64, 16) \
    X(sint32,   17) \
    X(sint64,   18) \

typedef enum pb_WireType {
#define X(name,str,num) PB_T##name = num,
    PB_WIRETYPES(X)
#undef  X
    PB_TWCOUNT
} pb_WireType;

typedef enum pb_ProtoType {
#define X(name,num) PB_T##name = num,
    PB_TYPES(X)
#undef  X
    PB_TCOUNT
} pb_ProtoType;

/* decode */

typedef struct pb_Slice {
    const char *p, *end;
} pb_Slice;

typedef struct pb_Value {
    union {
        uint32_t fixed32;
        uint64_t fixed64;
        pb_Slice data;
    } u;
    unsigned tag      : 29;
    unsigned wiretype : 3;
} pb_Value;

#define pb_slicelen(s) ((size_t)((s)->end - (s)->p))
#define pb_gettag(v)  ((unsigned)((v) >> 3))
#define pb_gettype(v) ((unsigned)((v) &  7))

PB_API pb_Slice pb_slice  (const char *s);
PB_API pb_Slice pb_lslice (const char *s, size_t len);

PB_API int pb_readvalue   (pb_Slice *s, pb_Value *value);
PB_API int pb_readvar32   (pb_Slice *s, uint32_t *pv);
PB_API int pb_readvarint  (pb_Slice *s, uint64_t *pv);
PB_API int pb_readfixed32 (pb_Slice *s, uint32_t *pv);
PB_API int pb_readfixed64 (pb_Slice *s, uint64_t *pv);
PB_API int pb_readslice   (pb_Slice *s, pb_Slice *pv);

PB_API int pb_skipvalue   (pb_Slice *s, int wiretype);
PB_API int pb_skipvarint  (pb_Slice *s);
PB_API int pb_skipslice   (pb_Slice *s);
PB_API int pb_skipsize    (pb_Slice *s, size_t len);

/* encode */

typedef struct pb_Buffer {
    unsigned used;
    unsigned size;
    char *buff;
    char init_buff[PB_BUFFERSIZE];
} pb_Buffer;

#define pb_buffer(b)      ((b)->buff)
#define pb_bufflen(b)     ((b)->used)
#define pb_addsize(b, sz) ((b)->used += (sz))
#define pb_addchar(b, ch) \
    ((void)((b)->used < (b)->size || pb_prepbuffsize((b), 1)), \
     ((b)->buff[(b)->used++] = (ch)))

PB_API void   pb_initbuffer   (pb_Buffer *b);
PB_API void   pb_resetbuffer  (pb_Buffer *b);
PB_API size_t pb_resizebuffer (pb_Buffer *b, size_t len);
PB_API void  *pb_prepbuffsize (pb_Buffer *b, size_t len);

PB_API pb_Slice pb_result (pb_Buffer *b);
PB_API void pb_addfile  (pb_Buffer *b, const char *filename);
PB_API void pb_addslice (pb_Buffer *b, pb_Slice *s);

PB_API void pb_adddata     (pb_Buffer *b, pb_Slice *s);
PB_API void pb_addvarint   (pb_Buffer *b, uint64_t n);
PB_API void pb_addvarint32 (pb_Buffer *b, uint32_t n);
PB_API void pb_addfixed64  (pb_Buffer *b, uint64_t n);
PB_API void pb_addfixed32  (pb_Buffer *b, uint32_t n);
PB_API void pb_addtag      (pb_Buffer *b, uint32_t tag, uint32_t type);

/* conversions */
PB_API uint64_t pb_expandsig     (uint32_t value);
PB_API uint32_t pb_encode_sint32 (uint32_t value);
PB_API uint32_t pb_decode_sint32 (uint32_t value);
PB_API uint64_t pb_encode_sint64 (uint64_t value);
PB_API uint64_t pb_decode_sint64 (uint64_t value);
PB_API uint32_t pb_encode_float  (float    value);
PB_API float    pb_decode_float  (uint32_t value);
PB_API uint64_t pb_encode_double (double   value);
PB_API double   pb_decode_double (uint64_t value);

/* type info */

typedef struct pb_State  pb_State;
typedef struct pb_Type   pb_Type;
typedef struct pb_Field  pb_Field;

PB_API void pb_init (pb_State *S);
PB_API void pb_free (pb_State *S);

PB_API const char *pb_newstring (pb_State *S, const char *s);
PB_API pb_Slice    pb_newslice  (pb_State *S, pb_Slice *s);
PB_API pb_Type    *pb_newtype   (pb_State *S, pb_Slice *qname);
PB_API pb_Field   *pb_newfield  (pb_State *S, pb_Type *t, pb_Slice *s, int tag);

PB_API int pb_load (pb_State *S, pb_Slice *b);
PB_API int pb_loadfile (pb_State *S, const char *filename);

PB_API pb_Type  *pb_type       (pb_State *S, pb_Slice *qname);
PB_API pb_Field *pb_field      (pb_Type *t, pb_Slice *field);
PB_API pb_Field *pb_fieldbytag (pb_Type *t, int field_tag);

PB_API const char *pb_wirename (pb_WireType t);
PB_API const char *pb_typename (pb_ProtoType t);

/* hash table */

typedef struct pb_Entry {
    int      next;
    unsigned hash;
    uintptr_t key;
    uintptr_t value;
} pb_Entry;

typedef struct pb_Map {
    unsigned size;
    unsigned lastfree;
    pb_Entry *hash;
} pb_Map;

PB_API void   pbM_init   (pb_Map *m);
PB_API void   pbM_free   (pb_Map *m);
PB_API size_t pbM_resize (pb_Map *m, size_t len);

PB_API unsigned pbM_calchash (pb_Slice *s);

PB_API pb_Entry *pbM_seti (pb_Map *m, uint32_t key);
PB_API pb_Entry *pbM_geti (pb_Map *m, uint32_t key);

PB_API pb_Entry *pbM_sets (pb_Map *m, pb_Slice *key);
PB_API pb_Entry *pbM_gets (pb_Map *m, pb_Slice *key);

/* memory pool */

typedef struct pb_MemPool {
    char *buffer, *end;
    char *p;
    struct pb_MemPool *next;
} pb_MemPool;

PB_API pb_MemPool *pbP_new    (size_t size);
PB_API void        pbP_delete (pb_MemPool *pool);

PB_API void    *pbP_newsize  (pb_MemPool *pool, size_t len);
PB_API pb_Slice pbP_newslice (pb_MemPool *pool, pb_Slice *s);

/* structures */

struct pb_Field {
    const char *name;
    const char *type_name;
    union {
        const char *default_value;
        unsigned enum_value;
    } u;
    unsigned tag        : 29;
    unsigned repeated   : 1;
    unsigned scalar     : 1;
    unsigned packed     : 1;
    unsigned type;
};

struct pb_Type {
    int      is_enum;
    pb_Map field_tags;
    pb_Map field_names;
    const char *name;
    const char *basename;
};

struct pb_State {
    pb_Map types;
    pb_MemPool *strpool;
    pb_MemPool *typepool;
    pb_MemPool *fieldpool;
};


#endif /* pb_h */


#if defined(PB_IMPLEMENTATION) && !defined(pb_implemented)
#define pb_implemented


#ifdef _MSC_VER
# pragma warning(disable:4996)
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* decode */

PB_API pb_Slice pb_slice(const char *s)
{ return pb_lslice(s, strlen(s)); }

PB_API pb_Slice pb_lslice(const char *s, size_t len)
{ pb_Slice slice = { s, s + len }; return slice; }

PB_API int pb_readvalue(pb_Slice *s, pb_Value *value) {
    const char *p = s->p;
    int res;
    uint32_t tag;
    if (!pb_readvar32(s, &tag)) return 0;
    value->tag = pb_gettag(tag);
    value->wiretype = pb_gettype(tag);
    switch (value->wiretype) {
    case PB_TVARINT:
        res = pb_readvarint(s, &value->u.fixed64); break;
    case PB_T64BIT:
        res = pb_readfixed64(s, &value->u.fixed64); break;
    case PB_TDATA:
        res = pb_readslice(s, &value->u.data); break;
        break;
    case PB_T32BIT:
        res = pb_readfixed32(s, &value->u.fixed32); break;
        break;
    default:
    case PB_TGSTART: /* start group */
    case PB_TGEND: /* end group */
        /* TODO need implemenbt */
        return 0;
    }
    if (!res) s->p = p;
    return res;
}

PB_API int pb_readvar32(pb_Slice *s, uint32_t *pv) {
    uint32_t n = 0;
    const char *p = s->p, *end;
    while (p < s->end && (*p & 0x80) != 0)
        ++p;
    if (p >= s->end)
        return 0;
    end = p + 1;
    while (p >= s->p) {
        n <<= 7;
        n |= *p-- & 0x7F;
    }
    s->p = end;
    *pv = n;
    return end-p-1;
}

PB_API int pb_readvarint(pb_Slice *s, uint64_t *pv) {
    uint64_t n = 0;
    const char *p = s->p, *end;
    while (p < s->end && (*p & 0x80) != 0)
        ++p;
    if (p >= s->end)
        return 0;
    end = p + 1;
    while (p >= s->p) {
        n <<= 7;
        n |= *p-- & 0x7F;
    }
    s->p = end;
    *pv = n;
    return end-p-1;
}

PB_API int pb_readfixed32(pb_Slice *s, uint32_t *pv) {
    int i;
    uint32_t n = 0;
    if (s->p + 4 > s->end)
        return 0;
    for (i = 3; i >= 0; --i) {
        n <<= 8;
        n |= s->p[i] & 0xFF;
    }
    s->p += 4;
    *pv = n;
    return 4;
}

PB_API int pb_readfixed64(pb_Slice *s, uint64_t *pv) {
    int i;
    uint64_t n = 0;
    if (s->p + 8 < s->end)
        return 0;
    for (i = 7; i >= 0; --i) {
        n <<= 8;
        n |= s->p[i] & 0xFF;
    }
    s->p += 8;
    *pv = n;
    return 8;
}

PB_API int pb_readslice(pb_Slice *s, pb_Slice *pv) {
    const char *p = s->p;
    uint32_t var;
    if (!pb_readvar32(s, &var)) return 0;
    if (pb_slicelen(s) < var) {
        s->p = p;
        return 0;
    }
    pv->p = s->p;
    pv->end = s->p = pv->p + var;
    return s->p - p;
}

PB_API int pb_skipvalue(pb_Slice *s, int wiretype) {
    const char *p = s->p;
    int res;
    uint64_t n;
    switch (wiretype) {
    case PB_TVARINT:
        res = pb_skipvarint(s); break;
    case PB_T64BIT:
        res = pb_skipsize(s, 8); break;
    case PB_TDATA:
        res = pb_readvarint(s, &n)
            && pb_skipsize(s, (size_t)n);
        if (!res) s->p = p;
        break;
    case PB_T32BIT:
        res = pb_skipsize(s, 4);
        break;
    default:
    case PB_TGSTART: /* start group */
    case PB_TGEND: /* end group */
        /* TODO need implemenbt */
        return 0;
    }
    return res;
}

PB_API int pb_skipvarint(pb_Slice *s) {
    const char *p = s->p, *op = p;
    while (p < s->end && (*p & 0x80) != 0)
        ++p;
    if (p >= s->end)
        return 0;
    s->p = p + 1;
    return p - op + 1;
}

PB_API int pb_skipslice(pb_Slice *s) {
    const char *p = s->p;
    uint64_t var;
    if (!pb_readvarint(s, &var)) return 0;
    if (pb_slicelen(s) < var) {
        s->p = p;
        return 0;
    }
    s->p += var;
    return s->p - p;
}

PB_API int pb_skipsize(pb_Slice *s, size_t len) {
    if (s->p + len > s->end)
        return 0;
    s->p += len;
    return len;
}

/* encode */

#define PB_MAX_SIZET (~(size_t)0 - 100)

PB_API pb_Slice pb_result(pb_Buffer *b)
{ pb_Slice slice = { b->buff, b->buff+b->used }; return slice; }

PB_API void pb_adddata(pb_Buffer *b, pb_Slice *s)
{ pb_addvarint32(b, pb_slicelen(s)); pb_addslice(b, s); }

PB_API void pb_initbuffer(pb_Buffer *b) {
    b->buff = b->init_buff;
    b->size = PB_BUFFERSIZE;
    b->used = 0;
}

PB_API void pb_resetbuffer(pb_Buffer *b) {
    if (b->buff != b->init_buff)
        free(b->buff);
    pb_initbuffer(b);
}

PB_API size_t pb_resizebuffer(pb_Buffer *b, size_t len) {
    char *newbuff;
    size_t newsize = b->size ? b->size : PB_BUFFERSIZE;
    if (len < b->used) len = b->used;
    while (newsize < PB_MAX_SIZET/2 && newsize < len)
        newsize <<= 1;
    newbuff = b->buff == b->init_buff ? NULL : b->buff;
    if (newsize >= len) {
        if (b->buff == b->init_buff) {
            newbuff = (char*)malloc(newsize);
            if (newbuff == NULL) return b->size;
            memcpy(newbuff, b->buff, b->used);
        }
        else {
            newbuff = (char*)realloc(b->buff, newsize);
            if (newbuff == NULL) return b->size;
        }
    }
    b->buff = newbuff;
    b->size = newsize;
    return b->size;
}

PB_API void* pb_prepbuffsize(pb_Buffer *b, size_t len) {
    if (b->used + len > b->size) {
        size_t oldsize = b->size;
        if (pb_resizebuffer(b, oldsize + len) == oldsize)
            return NULL;
    }
    return &b->buff[b->used];
}

PB_API void pb_addfile(pb_Buffer *b, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) return;
    do {
        char *buff = pb_prepbuffsize(b, BUFSIZ);
        size_t ret = fread(buff, 1, BUFSIZ, fp);
        pb_addsize(b, ret);
        if (ret == 0 || ret < BUFSIZ) break;
    } while (1);
    fclose(fp);
}

PB_API void pb_addslice(pb_Buffer *b, pb_Slice *s) {
    size_t len = pb_slicelen(s);
    void *p = pb_prepbuffsize(b, len);
    memcpy(p, s->p, len);
    pb_addsize(b, len);
}

PB_API void pb_addvarint(pb_Buffer *b, uint64_t n) {
    pb_prepbuffsize(b, 10);
    do {
        int cur = n & 0x7F;
        n >>= 7;
        pb_addchar(b, n != 0 ? cur | 0x80 : cur);
    } while (n != 0);
}

PB_API void pb_addvarint32(pb_Buffer *b, uint32_t n) {
    pb_prepbuffsize(b, 5);
    do {
        int cur = n & 0x7F;
        n >>= 7;
        pb_addchar(b, n != 0 ? cur | 0x80 : cur);
    } while (n != 0);
}

PB_API void pb_addfixed64(pb_Buffer *b, uint64_t n) {
    int i;
    pb_prepbuffsize(b, 4);
    for (i = 0; i < 4; ++i) {
        pb_addchar(b, n & 0xFF);
        n >>= 8;
    }
}

PB_API void pb_addfixed32(pb_Buffer *b, uint32_t n) {
    int i;
    pb_prepbuffsize(b, 8);
    for (i = 0; i < 8; ++i) {
        pb_addchar(b, n & 0xFF);
        n >>= 8;
    }
}

PB_API void pb_addtag(pb_Buffer *b, uint32_t tag, uint32_t type) {
    pb_addvarint32(b, (uint32_t)((tag << 3) | (type & 7)));
}

/* conversions */

PB_API uint64_t pb_expandsig(uint32_t value) {
    uint64_t ret = value & (((uint64_t)1 << 32) - 1);
    return (ret ^ (1 << 31)) - (1 << 31);
}

PB_API uint32_t pb_encode_sint32(uint32_t value)
{ return (value << 1) ^ (value >> 31); }

PB_API uint32_t pb_decode_sint32(uint32_t value)
{ return (value >> 1) ^ -(int32_t)(value & 1); }

PB_API uint64_t pb_encode_sint64(uint64_t value)
{ return (value << 1) ^ (value >> 63); }

PB_API uint64_t pb_decode_sint64(uint64_t value)
{ return (value >> 1) ^ -(int64_t)(value & 1); }

PB_API uint32_t pb_encode_float(float value) {
    union { uint32_t u32; float f; } u;
    u.f = value;
    return u.u32;
}

PB_API float pb_decode_float(uint32_t value) {
    union { uint32_t u32; float f; } u;
    u.u32 = value;
    return u.f;
}

PB_API uint64_t pb_encode_double(double value) {
    union { uint64_t u64; double d; } u;
    u.d = value;
    return u.u64;
}

PB_API double pb_decode_double(uint64_t value) {
    union { uint64_t u64; double d; } u;
    u.u64 = value;
    return u.d;
}

/* hash table */

static size_t pbM_hashsize(size_t len) {
    size_t newsize = PB_MIN_HASHSIZE;
    while (newsize < PB_MAX_SIZET/2 && newsize < len)
        newsize <<= 1;
    return newsize;
}

static pb_Entry *pbM_mainposition(pb_Map *m, pb_Entry *entry) {
    assert((m->size & (m->size - 1)) == 0);
    return &m->hash[(entry->hash == 0 ? 
            entry->key : entry->hash) & (m->size - 1)];
}

static int key_compare(pb_Entry *entry, pb_Slice *s) {
    const char *s1 = (char*)entry->key;
    const char *s2 = s->p;
    while (*s1 != '\0' && s2 < s->end && *s1 == *s2)
        ++s1, ++s2;
    return *s1 == '\0' && s2 == s->end;
}

static pb_Entry *pbM_newkey(pb_Map *m, pb_Entry *entry) {
    pb_Entry *mp;
    if (m->size == 0 && pbM_resize(m, PB_MIN_HASHSIZE) == 0) 
        return NULL;
redo:
    mp = pbM_mainposition(m, entry);
    if (mp->key != 0) {
        pb_Entry *f = NULL, *othern;
        while (m->lastfree > 0) {
            pb_Entry *e = &m->hash[--m->lastfree];
            if (e->key == 0)  { f = e; break; }
        }
        if (f == NULL) {
            if (pbM_resize(m, m->size*2) == 0)
                return NULL;
            goto redo; /* return pbM_newkey(m, entry); */
        }
        assert(f->hash == 0);
        othern = pbM_mainposition(m, mp);
        if (othern != mp) {
            while (othern + othern->next != mp)
                othern += othern->next;
            othern->next = f - othern;
            *f = *mp;
            if (mp->next != 0) {
                f->next += mp - f;
                mp->next = 0;
            }
        }
        else {
            if (mp->next != 0)
                f->next = (mp + mp->next) - f;
            else assert(f->next == 0);
            mp->next = f - mp;
            mp = f;
        }
    }
    mp->key = entry->key;
    mp->hash = entry->hash;
    mp->value = entry->value;
    return mp;
}

PB_API unsigned pbM_calchash(pb_Slice *s) {
    size_t l1, len = pb_slicelen(s);
    size_t step = (len >> PB_HASHLIMIT) + 1;
    unsigned h = (unsigned)len;
    for (l1 = len; l1 >= step; l1 -= step)
        h ^= (h<<5) + (h>>2) + (unsigned char)s->p[l1 - 1];
    return h;
}

PB_API void pbM_init(pb_Map *m) {
    m->size = 0;
    m->lastfree = 0;
    m->hash = NULL;
}

PB_API void pbM_free(pb_Map *m) {
    if (m->hash != NULL)
        free(m->hash);
    pbM_init(m);
}

PB_API size_t pbM_resize(pb_Map *m, size_t len) {
    size_t i;
    pb_Map new_map;
    new_map.size = pbM_hashsize(len);
    new_map.hash = (pb_Entry*)malloc(new_map.size*sizeof(pb_Entry));
    if (new_map.hash == NULL) return 0;
    new_map.lastfree = new_map.size;
    memset(new_map.hash, 0, sizeof(pb_Entry)*new_map.size);
    for (i = 0; i < m->size; ++i) {
        if (m->hash[i].key != 0)
            pbM_newkey(&new_map, &m->hash[i]);
    }
    *m = new_map;
    return m->size;
}

PB_API pb_Entry *pbM_geti(pb_Map *m, uint32_t key) {
    pb_Entry *e;
    if (m->size == 0) return NULL;
    assert((m->size & (m->size - 1)) == 0);
    e = &m->hash[key & (m->size - 1)];
    while (e->key != key) {
        int next = e->next;
        if (next == 0) return NULL;
        e += next;
    }
    return e;
}

PB_API pb_Entry *pbM_seti(pb_Map *m, uint32_t key) {
    pb_Entry e, *ret;
    e.key = key;
    e.hash = 0;
    e.value = 0;
    if ((ret = pbM_geti(m, key)) != NULL)
        return ret;
    return pbM_newkey(m, &e);
}

PB_API pb_Entry *pbM_gets(pb_Map *m, pb_Slice *key) {
    uint32_t hash;
    pb_Entry *e;
    if (m->size == 0) return NULL;
    hash = pbM_calchash(key);
    assert((m->size & (m->size - 1)) == 0);
    e = &m->hash[hash & (m->size - 1)];
    while (1) {
        int next = e->next;
        if (e->hash == hash && key_compare(e, key))
            return e;
        if (next == 0) return NULL;
        e += next;
    }
    return NULL;
}

PB_API pb_Entry *pbM_sets(pb_Map *m, pb_Slice *key) {
    pb_Entry e, *ret;
    if ((ret = pbM_gets(m, key)) != NULL)
        return ret;
    e.key = (uintptr_t)key->p;
    e.hash = pbM_calchash(key);
    e.value = 0;
    return pbM_newkey(m, &e);
}

/* memory pool */

PB_API pb_MemPool *pbP_new(size_t size) {
    pb_MemPool *pool = (pb_MemPool*)malloc(sizeof(pb_MemPool) + size);
    if (pool == NULL) return NULL;
    pool->buffer = (char*)(pool + 1);
    pool->end = pool->buffer + size;
    pool->p = pool->buffer;
    pool->next = NULL;
    return pool;
}

PB_API void pbP_delete(pb_MemPool *pool) {
    while (pool) {
        pb_MemPool *next = pool->next;
        free(pool);
        pool = next;
    }
}

PB_API void* pbP_newsize(pb_MemPool *pool, size_t len) {
    pb_MemPool *next;
    if (len <= (size_t)(pool->end - pool->p)) {
        char *ret = pool->p;
        pool->p += len;
        return ret;
    }
    if (len > PB_POOLSIZE) {
        next = pbP_new(len);
        if (next == NULL) return NULL;
        next->p = next->end;
        next->next = pool->next;
        pool->next = next;
        return next->buffer;
    }
    next = (pb_MemPool*)malloc(sizeof(pb_MemPool) + PB_POOLSIZE);
    if (next == NULL) return NULL;
    next->buffer = pool->buffer;
    next->end = pool->end;
    next->p = pool->p;
    next->next = pool->next;
    pool->buffer = (char*)(next + 1);
    pool->end = pool->buffer + PB_POOLSIZE;
    pool->p = pool->buffer + len;
    pool->next = next;
    return pool->buffer;
}

PB_API pb_Slice pbP_newslice(pb_MemPool *pool, pb_Slice *s) {
    pb_Slice ret = { NULL, NULL };
    size_t len = pb_slicelen(s);
    char *p = (char*)pbP_newsize(pool, len + 1);
    if (p == NULL) return ret;
    memcpy(p, s->p, len);
    ((char*)p)[len] = '\0';
    ret.p = p;
    ret.end = p + len;
    return ret;
}

/* type info */

PB_API pb_Slice pb_newslice(pb_State *S, pb_Slice *s) {
    pb_Slice slice = pbP_newslice(S->strpool, s);
    return slice;
}

static const char *pbT_getbasename(pb_Slice *s) {
    const char *end = s->end;
    while (s->p <= end && *end != '.')
        --end;
    return end + 1;
}

PB_API void pb_init(pb_State *S) {
    pbM_init(&S->types);
    S->strpool = pbP_new(PB_POOLSIZE);
    S->typepool = pbP_new(PB_POOLSIZE);
    S->fieldpool = pbP_new(PB_POOLSIZE);
}

PB_API void pb_free(pb_State *S) {
    size_t i;
    for (i = 0; i < S->types.size; ++i) {
        pb_Entry *entry = &S->types.hash[i];
        pb_Type *t = (pb_Type*)entry->value;
        if (t != NULL) {
            pbM_free(&t->field_tags);
            pbM_free(&t->field_names);
        }
    }
    pbP_delete(S->strpool);
    pbP_delete(S->fieldpool);
    pbP_delete(S->typepool);
    pbM_free(&S->types);
}

PB_API pb_Type *pb_newtype(pb_State *S, pb_Slice *qname) {
    pb_Entry *entry = pbM_sets(&S->types, qname);
    pb_Type *t = (pb_Type*)pbP_newsize(S->typepool, sizeof(pb_Type));
    entry->value = (uintptr_t)t;
    memset(t, 0, sizeof(*t));
    t->name = qname->p;
    t->basename = pbT_getbasename(qname);
    pbM_init(&t->field_tags);
    pbM_init(&t->field_names);
    return t;
}

PB_API pb_Field *pb_newfield(pb_State *S, pb_Type *t, pb_Slice *name, int tag) {
    pb_Entry *et = pbM_seti(&t->field_tags, tag);
    pb_Entry *en = pbM_sets(&t->field_names, name);
    pb_Field *f = (pb_Field*)pbP_newsize(S->fieldpool, sizeof(pb_Field));
    et->value = (uintptr_t)f;
    en->value = (uintptr_t)f;
    memset(f, 0, sizeof(*f));
    f->name = (const char*)en->key;
    f->tag = tag;
    return f;
}

PB_API pb_Type *pb_type(pb_State *S, pb_Slice *qname) {
    pb_Entry *e = pbM_gets(&S->types, qname);
    return e ? (pb_Type*)e->value : NULL;
}

PB_API pb_Field *pb_field(pb_Type *t, pb_Slice *field) {
    pb_Entry *e = pbM_gets(&t->field_names, field);
    return e ? (pb_Field*)e->value : NULL;
}

PB_API pb_Field *pb_fieldbytag(pb_Type *t, int field_tag) {
    pb_Entry *e = pbM_geti(&t->field_tags, field_tag);
    return e ? (pb_Field*)e->value : NULL;
}

PB_API const char *pb_wirename(pb_WireType t) {
    const char *s = "unknown";
    switch (t) {
#define X(name, str, num) case PB_T##name: s = str; break;
        PB_WIRETYPES(X)
#undef  X
    default: break;
    }
    return s;
}

PB_API const char *pb_typename(pb_ProtoType t) {
    const char *s = "unknown";
    switch (t) {
#define X(name, num) case PB_T##name: s = #name; break;
        PB_TYPES(X)
#undef  X
    default: break;
    }
    return s;
}

/* type info loader */

#define CHECK(cond) do { if (!(cond)) return 0; } while (0)

static int pbL_getqname(pb_State *S, pb_Slice *prefix, pb_Slice *name) {
    size_t plen = pb_slicelen(prefix);
    size_t nlen = pb_slicelen(name);
    char *p = (char*)pbP_newsize(S->strpool, plen + nlen + 2);
    CHECK(p);
    memcpy(p, prefix->p, plen);
    p[plen] = '.';
    memcpy(p+plen+1, name->p, nlen);
    p[plen+nlen+1] = '\0';
    name->p = p;
    name->end = p+plen+nlen+1;
    return 1;
}

static int pbL_rawfield(pb_State *S, pb_Type *t, pb_Field *f, pb_Slice *name) {
    pb_Entry *en = pbM_sets(&t->field_names, name);
    pb_Entry *et = pbM_seti(&t->field_tags, f->tag);
    pb_Field *nf = (pb_Field*)pbP_newsize(S->fieldpool, sizeof(pb_Field));
    CHECK(nf);
    *nf = *f;
    en->value = (uintptr_t)nf;
    et->value = (uintptr_t)nf;
    return 1;
}

static int pbL_rawtype(pb_State *S, pb_Type *t, pb_Slice *name) {
    pb_Entry *e = pbM_sets(&S->types, name);
    pb_Type *nt = (pb_Type*)pbP_newsize(S->typepool, sizeof(pb_Type));
    CHECK(nt);
    *nt = *t;
    e->value = (uintptr_t)nt;
    return 1;
}

static int pbL_EnumValueDescriptorProto(pb_State *S, pb_Slice *b, pb_Type *t) {
    uint32_t tag, number;
    pb_Slice name;
    pb_Field f;
    memset(&f, 0, sizeof(f));
    f.scalar = 1;
    while (pb_readvar32(b, &tag)) {
        switch (pb_gettag(tag)) {
        case 1: /* name */
            CHECK(pb_readslice(b, &name));
            CHECK(f.name = pb_newslice(S, &name).p);
            break;
        case 2: /* number */
            CHECK(pb_readvar32(b, &number));
            f.u.enum_value = number;
            break;
        default:
            CHECK(pb_skipvalue(b, pb_gettype(tag)));
            break;
        }
    }
    CHECK(b->p == b->end);
    return pbL_rawfield(S, t, &f, &name);
}

static int pbL_EnumDescriptorProto(pb_State *S, pb_Slice *b, pb_Slice *prefix) {
    uint32_t tag;
    pb_Slice name, slice;
    pb_Type t;
    memset(&t, 0, sizeof(t));
    t.is_enum = 1;
    while (pb_readvar32(b, &tag)) {
        switch (pb_gettag(tag)) {
        case 1: /* name */
            CHECK(pb_readslice(b, &name));
            CHECK(pbL_getqname(S, prefix, &name));
            break;
        case 2: /* value */
            CHECK(pb_readslice(b, &slice));
            CHECK(pbL_EnumValueDescriptorProto(S, &slice, &t));
            break;
        default:
            CHECK(pb_skipvalue(b, pb_gettype(tag)));
            break;
        }
    }
    CHECK(b->p == b->end);
    return pbL_rawtype(S, &t, &name);
}

static int pbL_FieldOptions(pb_State *S, pb_Slice *b, pb_Field *f) {
    uint32_t tag;
    while (pb_readvar32(b, &tag)) {
        if (pb_gettag(tag) == 2) {
            uint32_t v;
            CHECK(pb_readvar32(b, &v));
            f->packed = v;
            continue;
        }
        CHECK(pb_skipvalue(b, pb_gettype(tag)));
    }
    return b->p == b->end;
}

static int pbL_FieldDescriptorProto(pb_State *S, pb_Slice *b, pb_Type *t) {
    uint32_t tag, number;
    pb_Slice name, slice;
    pb_Field f;
    memset(&f, 0, sizeof(f));
    while (pb_readvar32(b, &tag)) {
        switch (pb_gettag(tag)) {
        case 1: /* name */
            CHECK(pb_readslice(b, &name));
            CHECK((name = pb_newslice(S, &name)).p);
            f.name = name.p;
            break;
        case 3: /* number */
            CHECK(pb_readvar32(b, &number));
            f.tag = number;
            break;
        case 4: /* label */
            CHECK(pb_readvar32(b, &number));
            if (number == 3) /* LABEL_OPTIONAL */
                f.repeated = 1;
            break;
        case 5: /* type */
            CHECK(pb_readvar32(b, &number));
            CHECK(number != PB_Tgroup);
            f.type = number;
            if (f.type != PB_Tmessage && f.type != PB_Tenum)
                f.scalar = 1;
            break;
        case 6: /* type_name */
            CHECK(pb_readslice(b, &slice));
            if (*slice.p == '.') ++slice.p;
            CHECK(f.type_name = pb_newslice(S, &slice).p);
            break;
        case 2: /* extendee */
            CHECK(t == NULL);
            CHECK(pb_readslice(b, &slice));
            t = pb_type(S, &slice);
            if (!t) t = pb_newtype(S, &slice);
            break;
        case 7: /* default_value */
            CHECK(pb_readslice(b, &slice));
            CHECK(f.u.default_value = pb_newslice(S, &slice).p);
            break;
        case 8: /* options */
            CHECK(pb_readslice(b, &slice));
            CHECK(pbL_FieldOptions(S, b, &f));
            break;
        default:
            CHECK(pb_skipvalue(b, pb_gettype(tag)));
            break;
        }
    }
    CHECK(b->p == b->end);
    CHECK(t != NULL);
    return pbL_rawfield(S, t, &f, &name);
}

static int pbL_DescriptorProto(pb_State *S, pb_Slice *b, pb_Slice *prefix) {
    uint32_t tag;
    pb_Type t;
    pb_Slice name, slice;
    memset(&t, 0, sizeof(t));
    while (pb_readvar32(b, &tag)) {
        switch (pb_gettag(tag)) {
        case 1: /* name */
            CHECK(pb_readslice(b, &name));
            CHECK(pbL_getqname(S, prefix, &name));
            t.name = name.p;
            break;
        case 2: /* field */
            CHECK(pb_readslice(b, &slice));
            CHECK(pbL_FieldDescriptorProto(S, &slice, &t));
            break;
        case 6: /* extension */
            CHECK(pb_readslice(b, &slice));
            CHECK(pbL_FieldDescriptorProto(S, &slice, NULL));
            break;
        case 3: /* nested_type */
            CHECK(pb_readslice(b, &slice));
            CHECK(pbL_DescriptorProto(S, &slice, &name));
            break;
        case 4: /* enum_type */
            CHECK(pb_readslice(b, &slice));
            CHECK(pbL_EnumDescriptorProto(S, &slice, &name));
            break;
        default:
            CHECK(pb_skipvalue(b, pb_gettype(tag)));
            break;
        }
    }
    CHECK(b->p != b->end);
    return pbL_rawtype(S, &t, &name);
}

static int pbL_FileDescriptorProto(pb_State *S, pb_Slice *b) {
    uint32_t tag;
    pb_Slice package, slice;
    while (pb_readvar32(b, &tag)) {
        switch (pb_gettag(tag)) {
        case 2: /* package */
            CHECK(pb_readslice(b, &package));
            break;
        case 4: /* message_type */
            CHECK(pb_readslice(b, &slice));
            CHECK(pbL_DescriptorProto(S, &slice, &package));
            break;
        case 5: /* enum_type */
            CHECK(pb_readslice(b, &slice));
            CHECK(pbL_EnumDescriptorProto(S, &slice, &package));
            break;
        case 7: /* extension */
            CHECK(pb_readslice(b, &slice));
            CHECK(pbL_FieldDescriptorProto(S, &slice, NULL));
            break;
        default:
            CHECK(pb_skipvalue(b, pb_gettype(tag)));
            break;
        }
    }
    return b->p == b->end;
}

PB_API int pb_load(pb_State *S, pb_Slice *b) {
    uint32_t tag;
    while (pb_readvar32(b, &tag)) {
        if (pb_gettag(tag) == 1) {
            pb_Slice slice;
            CHECK(pb_readslice(b, &slice));
            CHECK(pbL_FileDescriptorProto(S, &slice));
            continue;
        }
        CHECK(pb_skipvalue(b, pb_gettype(tag)));
    }
    return b->p == b->end;
}

#undef CHECK

PB_API int pb_loadfile(pb_State *S, const char *filename) {
    pb_Buffer b;
    pb_Slice s;
    int ret;
    pb_initbuffer(&b);
    pb_addfile(&b, filename);
    s = pb_result(&b);
    ret = pb_load(S, &s);
    pb_resetbuffer(&b);
    return ret;
}

#endif /* PB_IMPLEMENTATION */
/* cc: flags+='-DPB_IMPLEMENTATION -s -O3 -mdll -xc'
 * cc: output='pb_c.dll' */
