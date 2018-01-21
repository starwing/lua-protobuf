#define PB_STATIC_API
#include "pb.h"

#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>


#include <stdio.h>
#include <errno.h>


/* Lua utils */

#define PB_STATE     "pb.State"
#define PB_BUFFER    "pb.Buffer"
#define PB_SLICE     "pb.Slice"

#define check_buffer(L,idx) ((pb_Buffer*)checkudata(L,idx,PB_BUFFER))
#define test_buffer(L,idx)  ((pb_Buffer*)testudata(L,idx,PB_BUFFER))
#define check_slice(L,idx)  ((pb_Slice*)checkudata(L,idx,PB_SLICE))
#define test_slice(L,idx)   ((pb_Slice*)testudata(L,idx,PB_SLICE))
#define return_self(L) { lua_settop(L, 1); return 1; }

#if LUA_VERSION_NUM < 502
#include <assert.h>

# define LUA_OK        0
# define lua_rawlen    lua_objlen
# define luaL_newlib(L,l) (lua_newtable(L), luaL_register(L,NULL,l))
# define luaL_setfuncs(L,l,n) (assert(n==0), luaL_register(L,NULL,l))
# define luaL_setmetatable(L, name) \
    (luaL_getmetatable((L), (name)), lua_setmetatable(L, -2))

static int relindex(int idx, int offset) {
    if (idx < 0 && idx > LUA_REGISTRYINDEX)
        return idx + offset;
    return idx;
}

void lua_rawgetp(lua_State *L, int idx, const void *p) {
    lua_pushlightuserdata(L, (void*)p);
    lua_rawget(L, relindex(idx, 1));
}

void lua_rawsetp(lua_State *L, int idx, const void *p) {
    lua_pushlightuserdata(L, (void*)p);
    lua_insert(L, -2);
    lua_rawset(L, relindex(idx, 1));
}

static lua_Integer lua_tointegerx(lua_State *L, int idx, int *isint) {
    lua_Integer i = lua_tointeger(L, idx);
    if (isint) *isint = (i != 0 || lua_type(L, idx) == LUA_TNUMBER);
    return i;
}

#ifdef LUAI_BITSINT /* not LuaJIT */
#include <errno.h>
static int luaL_fileresult(lua_State *L, int stat, const char *fname) {
    int en = errno;
    if (stat) { lua_pushboolean(L, 1); return 1; }
    lua_pushnil(L);
    if (fname)
        lua_pushfstring(L, "%s: %s", fname, strerror(en));
    else
        lua_pushstring(L, strerror(en));
    lua_pushinteger(L, en);
    return 3;
}
#endif /* not LuaJIT */

#endif

#if LUA_VERSION_NUM >= 503
# define lua53_getfield lua_getfield
# define lua53_rawgeti  lua_rawgeti
#else
static int lua53_getfield(lua_State *L, int idx, const char *field)
{ lua_getfield(L, idx, field); return lua_type(L, -1); }
static int lua53_rawgeti(lua_State *L, int idx, lua_Integer i)
{ lua_rawgeti(L, idx, i); return lua_type(L, -1); }
#endif

static int lpb_pos(pb_Slice *s) { return (int)(s[0].p - s[1].p) + 1; }

static int typeerror(lua_State *L, int idx, const char *type) {
    lua_pushfstring(L, "%s expected, got %s", type, luaL_typename(L, idx));
    return luaL_argerror(L, idx, lua_tostring(L, -1));
}

static void *testudata(lua_State *L, int idx, const char *type) {
    void *p = lua_touserdata(L, idx);
    if (p != NULL && lua_getmetatable(L, idx)) {
        lua_getfield(L, LUA_REGISTRYINDEX, type);
        if (!lua_rawequal(L, -2, -1))
            p = NULL;
        lua_pop(L, 2);
        return p;
    }
    return NULL;
}

static void *checkudata(lua_State *L, int idx, const char *type) {
    void *p = testudata(L, idx, type);
    if (p == NULL) typeerror(L, idx, type);
    return p;
}

static lua_Integer posrelat(lua_Integer pos, size_t len) {
    if (pos >= 0) return pos;
    else if (0u - (size_t)pos > len) return 0;
    else return (lua_Integer)len + pos;
}

static int argerror(lua_State *L, int idx, const char *fmt, ...) {
    va_list l;
    va_start(l, fmt);
    lua_pushvfstring(L, fmt, l);
    va_end(l);
    return luaL_argerror(L, idx, lua_tostring(L, -1));
}

static pb_Slice lpb_toslice(lua_State *L, int idx) {
    int type = lua_type(L, idx);
    pb_Slice ret = { NULL, NULL };
    if (type == LUA_TSTRING) {
        size_t len;
        const char *s = lua_tolstring(L, idx, &len);
        return pb_lslice(s, len);
    }
    else if (type == LUA_TUSERDATA) {
        pb_Buffer *buffer;
        pb_Slice *slice;
        if ((buffer = test_buffer(L, idx)) != NULL)
            ret = pb_result(buffer);
        else if ((slice = test_slice(L, idx)) != NULL)
            ret = *slice;
    }
    return ret;
}

static pb_Slice lpb_checkslice(lua_State *L, int idx) {
    pb_Slice ret = lpb_toslice(L, idx);
    if (ret.p == NULL) typeerror(L, idx, "string/buffer/slice");
    return ret;
}

static void lpb_addlength(lua_State *L, pb_Buffer *b, size_t len) {
    if (pb_addlength(b, len) == 0)
        luaL_error(L, "out of memory when encode packed fields");
}

static void lpb_readvarint64(lua_State *L, pb_Slice *s, uint64_t *pv) {
    if (pb_readvarint64(s, pv) == 0)
        luaL_error(L, "invalid varint64 value at pos %s", lpb_pos(s));
}

static void lpb_readbytes(lua_State *L, pb_Slice *s, pb_Slice *pv) {
    uint64_t len;
    if (pb_readvarint64(s, &len) == 0 || len > PB_MAX_SIZET)
        luaL_error(L, "invalid bytes length: %d (at %d)",
                (int)len, lpb_pos(s));
    if (pb_readslice(s, (size_t)len, pv) == 0 && len != 0)
        luaL_error(L, "un-finished bytes (%d len at %d)",
                (int)len, lpb_pos(s));
}

typedef union lpb_Value {
    pb_Slice s[1];
    uint32_t u32;
    uint64_t u64;
    lua_Integer lint;
    lua_Number lnum;
} lpb_Value;

static int lpb_addtype(lua_State *L, pb_Buffer *b, int idx, int type) {
    int ret = 0, expected = LUA_TNUMBER;
    lpb_Value v;
    switch (type) {
    case PB_Tbool:
        pb_addvarint32(b, lua_toboolean(L, idx));
        ret = 1;
        break;
    case PB_Tdouble:
        v.lnum = lua_tonumberx(L, idx, &ret);
        if (ret) pb_addfixed64(b, pb_encode_double((double)v.lnum));
        break;
    case PB_Tfloat:
        v.lnum = lua_tonumberx(L, idx, &ret);
        if (ret) pb_addfixed32(b, pb_encode_float((float)v.lnum));
        break;
    case PB_Tfixed32:
        v.lint = lua_tointegerx(L, idx, &ret);
        if (ret) pb_addfixed32(b, (uint32_t)v.lint);
        break;
    case PB_Tsfixed32:
        v.lint = lua_tointegerx(L, idx, &ret);
        if (ret) pb_addfixed32(b, (int32_t)v.lint);
        break;
    case PB_Tint32: case PB_Tuint32:
        v.lint = lua_tointegerx(L, idx, &ret);
        if (ret) pb_addvarint32(b, (int32_t)lua_tointeger(L, idx));
        break;
    case PB_Tsint32:
        v.lint = lua_tointegerx(L, idx, &ret);
        if (ret) pb_addvarint32(b, pb_encode_sint32((int32_t)v.lint));
        break;
    case PB_Tfixed64:
        v.lint = lua_tointegerx(L, idx, &ret);
        if (ret) pb_addfixed64(b, (uint64_t)v.lint);
        break;
    case PB_Tsfixed64:
        v.lint = lua_tointegerx(L, idx, &ret);
        if (ret) pb_addfixed64(b, (int64_t)v.lint);
        break;
    case PB_Tint64: case PB_Tuint64:
        v.lint = lua_tointegerx(L, idx, &ret);
        if (ret) pb_addvarint64(b, (int64_t)v.lint);
        break;
    case PB_Tsint64:
        v.lint = lua_tointegerx(L, idx, &ret);
        if (ret) pb_addvarint64(b, pb_encode_sint64((int64_t)v.lint));
        break;
    case PB_Tbytes: case PB_Tstring:
        *v.s = lpb_toslice(L, idx);
        if ((ret = v.s->p != NULL)) pb_addbytes(b, *v.s);
        expected = LUA_TSTRING;
        break;
    default:
        return luaL_error(L, "unexpected %s for type '%s'",
                luaL_typename(L, idx), pb_typename(type, "<unknown>"));
    }
    return ret ? 0 : expected;
}

static void lpb_readtype(lua_State *L, int type, pb_Slice *s, const char *wt) {
    lpb_Value v;
    switch (type) {
    case PB_Tbool:  case PB_Tenum:
    case PB_Tint32: case PB_Tuint32: case PB_Tsint32:
    case PB_Tint64: case PB_Tuint64: case PB_Tsint64:
        lpb_readvarint64(L, s, &v.u64);
        switch (type) {
        case PB_Tbool:   lua_pushboolean(L, v.u64 != 0); return;
        case PB_Tenum:   lua_pushinteger(L, v.u64); return;
        case PB_Tint32:  lua_pushinteger(L, (int32_t)v.u64); return;
        case PB_Tuint32: lua_pushinteger(L, (uint32_t)v.u64); return;
        case PB_Tsint32: lua_pushinteger(L, pb_decode_sint32((uint32_t)v.u64)); return;
        case PB_Tint64:  lua_pushinteger(L, (int64_t)v.u64); return;
        case PB_Tuint64: lua_pushinteger(L, (uint64_t)v.u64); return;
        case PB_Tsint64: lua_pushinteger(L, pb_decode_sint64(v.u64)); return;
        }
        break;
    case PB_Tfloat:
    case PB_Tfixed32:
    case PB_Tsfixed32:
        if (pb_readfixed32(s, &v.u32) == 0)
            luaL_error(L, "invalid fixed32 value at pos %s", lpb_pos(s));
        switch (type) {
        case PB_Tfloat:    lua_pushnumber(L, pb_decode_float(v.u32)); return;
        case PB_Tfixed32:  lua_pushinteger(L, v.u32); return;
        case PB_Tsfixed32: lua_pushinteger(L, (int32_t)v.u32); return;
        }
        break;
    case PB_Tdouble:
    case PB_Tfixed64:
    case PB_Tsfixed64:
        if (pb_readfixed64(s, &v.u64) == 0)
            luaL_error(L, "invalid fixed64 value at pos %s", lpb_pos(s));
        switch (type) {
        case PB_Tdouble:   lua_pushnumber(L, pb_decode_double(v.u64)); return;
        case PB_Tfixed64:  lua_pushinteger(L, v.u64); return;
        case PB_Tsfixed64: lua_pushinteger(L, (int64_t)v.u64); return;
        }
        break;
    case PB_Tbytes:
    case PB_Tstring:
    case PB_Tmessage:
        lpb_readbytes(L, s, v.s);
        lua_pushlstring(L, v.s->p, pb_len(*v.s));
        return;
    }
    lua_pushfstring(L, "type mismatch, %s expected for type %s",
            pb_wtypename(pb_wtypebytype(type), "nothing"),
            pb_typename(type, "<unknown>"));
    if (wt) {
        lua_pushfstring(L, ", got %s", wt);
        lua_concat(L, 2);
    }
    lua_error(L);
}


/* io routines */

#ifdef _WIN32
# include <io.h>
# include <fcntl.h>
#else
# define setmode(a,b)  ((void)0)
#endif

static int io_write(lua_State *L, FILE *f, int idx) {
    int nargs = lua_gettop(L) - idx + 1;
    int status = 1;
    for (; nargs--; idx++) {
        pb_Slice s = lpb_checkslice(L, idx);
        size_t l = pb_len(s);
        status = status && (fwrite(s.p, sizeof(char), l, f) == l);
    }
    if (status) return 1;  /* file handle already on stack top */
    else return luaL_fileresult(L, status, NULL);
}

static int Lio_read(lua_State *L) {
    const char *fname = luaL_optstring(L, 1, NULL);
    luaL_Buffer b;
    FILE *fp = stdin;
    size_t nr;
    if (fname == NULL)
        (void)setmode(fileno(stdin), O_BINARY);
    else if ((fp = fopen(fname, "rb")) == NULL)
        return luaL_fileresult(L, 0, fname);
    luaL_buffinit(L, &b);
    do {  /* read file in chunks of LUAL_BUFFERSIZE bytes */
        char *p = luaL_prepbuffer(&b);
        nr = fread(p, sizeof(char), LUAL_BUFFERSIZE, fp);
        luaL_addsize(&b, nr);
    } while (nr == LUAL_BUFFERSIZE);
    if (fp != stdin) fclose(fp);
    else (void)setmode(fileno(stdin), O_TEXT);
    luaL_pushresult(&b);  /* close buffer */
    return 1;
}

static int Lio_write(lua_State *L) {
    int res;
    (void)setmode(fileno(stdout), O_BINARY);
    res = io_write(L, stdout, 1);
    fflush(stdout);
    (void)setmode(fileno(stdout), O_TEXT);
    return res;
}

static int Lio_dump(lua_State *L) {
    int res;
    const char *fname = luaL_checkstring(L, 1);
    FILE *fp = fopen(fname, "wb");
    if (fp == NULL) return luaL_fileresult(L, 0, fname);
    res = io_write(L, fp, 2);
    fclose(fp);
    return res;
}

LUALIB_API int luaopen_pb_io(lua_State *L) {
    luaL_Reg libs[] = {
#define ENTRY(name) { #name, Lio_##name }
        ENTRY(read),
        ENTRY(write),
        ENTRY(dump),
#undef  ENTRY
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
    return 1;
}


/* protobuf integer conversion */

static int Lconv_encode_int32(lua_State *L) {
    lua_pushinteger(L, pb_expandsig((int32_t)luaL_checkinteger(L, 1)));
    return 1;
}

static int Lconv_encode_uint32(lua_State *L) {
    lua_pushinteger(L, (uint32_t)luaL_checkinteger(L, 1));
    return 1;
}

static int Lconv_encode_sint32(lua_State *L) {
    lua_pushinteger(L, pb_encode_sint32((int32_t)luaL_checkinteger(L, 1)));
    return 1;
}

static int Lconv_decode_sint32(lua_State *L) {
    lua_pushinteger(L, pb_decode_sint32((uint32_t)luaL_checkinteger(L, 1)));
    return 1;
}

static int Lconv_encode_sint64(lua_State *L) {
    lua_pushinteger(L, pb_encode_sint64(luaL_checkinteger(L, 1)));
    return 1;
}

static int Lconv_decode_sint64(lua_State *L) {
    lua_pushinteger(L, pb_decode_sint64(luaL_checkinteger(L, 1)));
    return 1;
}

static int Lconv_encode_float(lua_State *L) {
    lua_pushinteger(L, pb_encode_float((float)luaL_checknumber(L, 1)));
    return 1;
}

static int Lconv_decode_float(lua_State *L) {
    lua_pushnumber(L, pb_decode_float((uint32_t)luaL_checkinteger(L, 1)));
    return 1;
}

static int Lconv_encode_double(lua_State *L) {
    lua_pushinteger(L, pb_encode_double(luaL_checknumber(L, 1)));
    return 1;
}

static int Lconv_decode_double(lua_State *L) {
    lua_pushnumber(L, pb_decode_double(luaL_checkinteger(L, 1)));
    return 1;
}

LUALIB_API int luaopen_pb_conv(lua_State *L) {
    luaL_Reg libs[] = {
        { "decode_uint32", Lconv_encode_uint32 },
        { "decode_int32", Lconv_encode_int32 },
#define ENTRY(name) { #name, Lconv_##name }
        ENTRY(encode_int32),
        ENTRY(encode_uint32),
        ENTRY(encode_sint32),
        ENTRY(encode_sint64),
        ENTRY(decode_sint32),
        ENTRY(decode_sint64),
        ENTRY(decode_float),
        ENTRY(decode_double),
        ENTRY(encode_float),
        ENTRY(encode_double),
#undef  ENTRY
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
    return 1;
}


/* protobuf encode routine */

static const char *lpb_typefmt(const char *fmt, int *ptype) {
    int type = -1;
    switch (*fmt) {
    case 'b': ++fmt, type = PB_Tbool; break;
    case 'f': ++fmt, type = PB_Tfloat; break;
    case 'F': ++fmt, type = PB_Tdouble; break;
    case 'i': ++fmt, type = PB_Tint32; break;
    case 's': ++fmt, type = PB_Tsint32; break;
    case 'u': ++fmt, type = PB_Tuint32; break;
    case 'x': ++fmt, type = PB_Tfixed32; break;
    case 'y': ++fmt, type = PB_Tsfixed32; break;
    case 'I': ++fmt, type = PB_Tint64; break;
    case 'S': ++fmt, type = PB_Tsint64; break;
    case 'U': ++fmt, type = PB_Tuint64; break;
    case 'X': ++fmt, type = PB_Tfixed64; break;
    case 'Y': ++fmt, type = PB_Tsfixed64; break;
    }
    *ptype = type;
    return fmt;
}

static int lpb_packfmt(lua_State *L, int idx, pb_Buffer *b, const char *fmt, int level) {
    int type, ltype;
    size_t len;
    luaL_argcheck(L, 1, level <= 100, "format level overflow");
    for (;;) {
        switch (*fmt) {
        case 'v': pb_addvarint64(b, (uint64_t)luaL_checkinteger(L, idx++)); break;
        case 'd': pb_addfixed32(b, (uint32_t)luaL_checkinteger(L, idx++)); break;
        case 'q': pb_addfixed64(b, (uint64_t)luaL_checkinteger(L, idx++)); break;
        case 'c': pb_addslice(b, lpb_checkslice(L, idx++)); break;
        case 's': pb_addbytes(b, lpb_checkslice(L, idx++)); break;
        case '(':
            len = pb_bufflen(b);
            idx = lpb_packfmt(L, idx, b, fmt, ++level);
            lpb_addlength(L, b, len);
            break;
        case ')':
            if (level == 0)
                luaL_argerror(L, 1, "unexpected ')' in format");
            return idx;
        case '\0':
            if (level != 0)
                luaL_argerror(L, 1, "unmatch '(' in format");
            return idx;
        default:
            if (fmt = lpb_typefmt(fmt, &type), type < 0)
                argerror(L, 1, "invalid formater: '%c'", *fmt);
            if ((ltype = lpb_addtype(L, b, idx, type)) != 0)
                argerror(L, idx, "%s expected for type '%s', got %s",
                        lua_typename(L, ltype), pb_typename(type, "<unknown>"),
                        luaL_typename(L, idx));
        }
    }
}

static int Lbuf_new(lua_State *L) {
    int i, top = lua_gettop(L);
    pb_Buffer *buf = (pb_Buffer*)lua_newuserdata(L, sizeof(pb_Buffer));
    pb_initbuffer(buf);
    luaL_setmetatable(L, PB_BUFFER);
    for (i = 1; i <= top; ++i)
        pb_addslice(buf, lpb_checkslice(L, i));
    return 1;
}

static int Lbuf_libcall(lua_State *L) {
    int i, top = lua_gettop(L);
    pb_Buffer *buf = (pb_Buffer*)lua_newuserdata(L, sizeof(pb_Buffer));
    pb_initbuffer(buf);
    lua_setmetatable(L, 1);
    for (i = 2; i <= top; ++i)
        pb_addslice(buf, lpb_checkslice(L, i));
    return 1;
}

static int Lbuf_tostring(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    lua_pushfstring(L, "pb.Buffer: %p", buf);
    return 1;
}

static int Lbuf_reset(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    int i, top = lua_gettop(L);
    pb_resetbuffer(buf);
    for (i = 2; i <= top; ++i)
        pb_addslice(buf, lpb_checkslice(L, i));
    return_self(L);
}

static int Lbuf_len(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    lua_pushinteger(L, (lua_Integer)buf->size);
    return 1;
}

static int Lbuf_pack(lua_State *L) {
    pb_Buffer b, *pb = test_buffer(L, 1);
    int idx = 1 + (pb != NULL);
    const char *fmt = luaL_checkstring(L, idx++);
    if (pb == NULL) {
        pb = &b;
        pb_initbuffer(pb);
    }
    lpb_packfmt(L, idx, pb, fmt, 0);
    if (pb != &b)
        lua_settop(L, 1);
    else {
        pb_Slice ret = pb_result(pb);
        lua_pushlstring(L, ret.p, pb_len(ret));
        pb_resetbuffer(pb);
    }
    return 1;
}

static int Lbuf_pop(lua_State *L) {
    pb_Buffer *b = check_buffer(L, 1);
    size_t sz = (size_t)luaL_optinteger(L, 2, b->size);
    if (sz > b->size) sz = b->size;
    b->size -= sz;
    if (lua_toboolean(L, 3)) {
        lua_pushlstring(L, &b->buff[b->size], sz);
        return 1;
    }
    return_self(L);
}

static int Lbuf_result(lua_State *L) {
    pb_Buffer *b = check_buffer(L, 1);
    const char *s = luaL_optstring(L, 2, NULL);
    if (s == NULL)
        lua_pushlstring(L, b->buff, b->size);
    else if (strcmp(s, "hex") == 0) {
        const char *hexa = "0123456789ABCDEF";
        luaL_Buffer lb;
        char hex[4] = "XX ";
        size_t i;
        luaL_buffinit(L, &lb);
        for (i = 0; i < b->size; ++i) {
            hex[0] = hexa[(b->buff[i]>>4)&0xF];
            hex[1] = hexa[(b->buff[i]   )&0xF];
            if (i == b->size-1) hex[2] = '\0';
            luaL_addstring(&lb, hex);
        }
        luaL_pushresult(&lb);
    }
    else {
        luaL_argerror(L, 2, "invalid options");
    }
    return 1;
}

LUALIB_API int luaopen_pb_buffer(lua_State *L) {
    luaL_Reg libs[] = {
        { "__tostring", Lbuf_tostring },
        { "__len",      Lbuf_len },
        { "__gc",       Lbuf_reset },
        { "delete",     Lbuf_reset },
#define ENTRY(name) { #name, Lbuf_##name }
        ENTRY(new),
        ENTRY(reset),
        ENTRY(pack),
        ENTRY(pop),
        ENTRY(result),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, PB_BUFFER)) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        lua_createtable(L, 0, 1);
        lua_pushcfunction(L, Lbuf_libcall);
        lua_setfield(L, -2, "__call");
        lua_setmetatable(L, -2);
    }
    return 1;
}


/* protobuf decode routine */

#define LPB_INITSTACKLEN 16

typedef struct lpb_Slice {
    pb_Slice  view;
    pb_Slice  src;
    pb_Slice *stack;
    size_t    stack_used;
    size_t    stack_size;
    pb_Slice  init_stack[LPB_INITSTACKLEN];
} lpb_Slice;

static void lpb_resetslice(lua_State *L, lpb_Slice *s) {
    if (s->stack != s->init_stack)
        free(s->stack);
    s->stack      = s->init_stack;
    s->stack_used = 0;
    s->stack_size = LPB_INITSTACKLEN;
    lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, s);
}

static void lpb_pushslice(lua_State *L, lpb_Slice *s, pb_Slice view) {
    if (s->stack_used == s->stack_size) {
        size_t newsize = s->stack_size * 2;
        pb_Slice *oldstack = s->stack == s->init_stack ? NULL : s->stack;
        pb_Slice *stack = (pb_Slice*)realloc(oldstack, newsize);
        if (stack == NULL) { luaL_error(L, "out of memory"); return; }
        if (oldstack == NULL)
            memcpy(stack, s->stack, s->stack_size*sizeof(lpb_Slice));
        s->stack = stack;
        s->stack_size = newsize;
    }
    s->view = s->stack[s->stack_used++] = view;
}

static pb_Slice lpb_checkview(lua_State *L, int idx, pb_Slice *ps) {
    pb_Slice src = lpb_checkslice(L, idx), view;
    size_t len = pb_len(src);
    lua_Integer i = posrelat(luaL_optinteger(L, idx+1, 1), len);
    lua_Integer j = posrelat(luaL_optinteger(L, idx+2, len), len);
    if (i < 1) i = 1;
    if (j > (lua_Integer)len) j = len;
    view.p   = src.p + i - 1;
    view.end = src.p + j;
    if (ps) *ps = src;
    return view;
}

static void lpb_initslice(lua_State *L, int idx, lpb_Slice *s) {
    memset(s, 0, sizeof(lpb_Slice));
    s->stack      = s->init_stack;
    s->stack_used = 0;
    s->stack_size = LPB_INITSTACKLEN;
    if (!lua_isnoneornil(L, idx)) {
        pb_Slice src, view = lpb_checkview(L, idx, &src);
        s->src = src;
        lpb_pushslice(L, s, view);
        lua_pushvalue(L, idx);
        lua_rawsetp(L, LUA_REGISTRYINDEX, s);
    }
}

static int lpb_fmterror(lua_State *L, int wt, pb_Slice *s) {
    return argerror(L, 1, "invalid wireformat '%s' at pos %d",
            pb_wtypename(wt, NULL), lpb_pos(s));
}

static int lpb_unpackfmt(lua_State *L, int idx, int top, const char *fmt, pb_Slice *s) {
    lpb_Value v;
    pb_Slice os = *s;
    int type, rets = 0;
    size_t oslen;
    for (; *fmt != '\0'; ++fmt) {
        switch (*fmt) {
        case 'v':
            if (pb_readvarint64(s, &v.u64) == 0) lpb_fmterror(L, PB_TVARINT, s);
            luaL_checkstack(L, 1, "too many values");
            lua_pushinteger(L, v.u64), ++rets;
            break;
        case 'd':
            if (pb_readfixed32(s, &v.u32) == 0) lpb_fmterror(L, PB_T32BIT, s);
            luaL_checkstack(L, 1, "too many values");
            lua_pushinteger(L, v.u32), ++rets;
            break;
        case 'q':
            if (pb_readfixed64(s, &v.u64) == 0) lpb_fmterror(L, PB_T64BIT, s);
            luaL_checkstack(L, 1, "too many values");
            lua_pushinteger(L, v.u64), ++rets;
            break;
        case 'c':
            luaL_argcheck(L, 1, idx >= top, "format argument exceed");
            v.lint = luaL_checkinteger(L, idx++);
            if (pb_readslice(s, (size_t)v.lint, v.s) == 0) lpb_fmterror(L, PB_TBYTES, s);
            luaL_checkstack(L, 1, "too many values");
            lua_pushlstring(L, v.s->p, pb_len(*v.s)), ++rets;
            break;
        case 's':
            if (pb_readbytes(s, v.s) == 0) lpb_fmterror(L, PB_TBYTES, s);
            luaL_checkstack(L, 1, "too many values");
            lua_pushlstring(L, v.s->p, pb_len(*v.s)), ++rets;
            break;
        case '*':
            luaL_checkstack(L, 1, "too many values");
            lua_pushinteger(L, s->p - os.p), ++rets;
            break;

        case '@':
        case '+':
        case '-':
            luaL_argcheck(L, 1, idx >= top, "format argument exceed");
            v.lint = posrelat(luaL_checkinteger(L, idx++), oslen = pb_len(os));
            switch (*fmt) {
            case '+': v.lint = (s->p - os.p) + v.lint; break;
            case '-': v.lint = (s->p - os.p) - v.lint; break;
            }
            if (v.lint == 0) v.lint = 1;
            if (v.lint > (lua_Integer)oslen) v.lint = oslen + 1;
            s->p = os.p + v.lint - 1;
            break;

        default:
            if (fmt = lpb_typefmt(fmt, &type), type < 0)
                argerror(L, 1, "invalid formater: '%c'", *fmt);
            luaL_checkstack(L, 1, "too many values");
            lpb_readtype(L, type, s, NULL);
            ++rets;
        }
    }
    return rets;
}

static int Lslice_new(lua_State *L) {
    lpb_Slice *s;
    lua_settop(L, 3);
    s = (lpb_Slice*)lua_newuserdata(L, sizeof(lpb_Slice));
    lpb_initslice(L, 1, s);
    luaL_setmetatable(L, PB_SLICE);
    return 1;
}

static int Lslice_libcall(lua_State *L) {
    lpb_Slice *s;
    lua_settop(L, 4);
    s = (lpb_Slice*)lua_newuserdata(L, sizeof(lpb_Slice));
    lpb_initslice(L, 2, s);
    lua_setmetatable(L, 1);
    return 1;
}

static int Lslice_reset(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    lpb_resetslice(L, s);
    return_self(L);
}

static int Lslice_tostring(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    lua_pushfstring(L, "pb.Slice: %p", s);
    return 1;
}

static int Lslice_len(lua_State *L) {
    pb_Slice *s = (pb_Slice*)check_slice(L, 1);
    lua_pushinteger(L, (lua_Integer)pb_len(*s));
    return 2;
}

static int Lslice_view(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    lua_Integer level = posrelat(luaL_optinteger(L, 2, s->stack_used), s->stack_used);
    if (level == 0) level = 1;
    if (level > (lua_Integer)s->stack_used)
        return 0;
    lua_pushinteger(L, s->stack[level-1].p   - s->stack[0].p + 1);
    lua_pushinteger(L, s->stack[level-1].end - s->stack[0].p);
    return 2;
}

static int Lslice_level(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    lua_pushinteger(L, s->stack_used);
    return 1;
}

static int Lslice_slice(lua_State *L) {
    pb_Slice s = lpb_checkslice(L, 1);
    size_t len = pb_len(s);
    lua_Integer i = posrelat(luaL_optinteger(L, 2, 1), len);
    lua_Integer j = posrelat(luaL_optinteger(L, 3, len), len);
    if (i < 1) i = 1;
    if (j > (lua_Integer)len) j = len;
    lua_pushlstring(L, s.p + i - 1, (size_t)(j-i+1));
    return 1;
}

static int Lslice_enter(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    pb_Slice view;
    if (pb_readbytes(&s->view, &view) == 0)
        return argerror(L, 1, "bytes wireformat expected at pos %d",
                lpb_pos(&s->src));
    lpb_pushslice(L, s, view);
    return_self(L);
}

static int Lslice_leave(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    lua_Integer level = luaL_optinteger(L, 2, 1);
    if ((lua_Integer)s->stack_used <= level)
        argerror(L, 2, "invalid level (%d), should less %d",
                level, s->stack_used);
    s->stack_used -= (size_t)level;
    s->view = s->stack[s->stack_used-1];
    lua_settop(L, 1);
    lua_pushinteger(L, s->stack_used);
    return 2;
}

static int Lslice_unpack(lua_State *L) {
    pb_Slice view[2], *s = test_slice(L, 1);
    const char *fmt = luaL_checkstring(L, 2);
    int top = lua_gettop(L);
    if (s != NULL)
        view[0] = view[1] = lpb_checkslice(L, 1), s = view;
    return lpb_unpackfmt(L, 3, top, fmt, s);
}

LUALIB_API int luaopen_pb_slice(lua_State *L) {
    luaL_Reg libs[] = {
        { "__tostring", Lslice_tostring },
        { "__len",      Lslice_len },
        { "__gc",       Lslice_reset },
        { "delete",     Lslice_reset },
#define ENTRY(name) { #name, Lslice_##name }
        ENTRY(new),
        ENTRY(reset),
        ENTRY(view),
        ENTRY(level),
        ENTRY(slice),
        ENTRY(enter),
        ENTRY(leave),
        ENTRY(unpack),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, PB_SLICE)) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        lua_createtable(L, 0, 1);
        lua_pushcfunction(L, Lslice_libcall);
        lua_setfield(L, -2, "__call");
        lua_setmetatable(L, -2);
    }
    return 1;
}


/* high level encode/decode routines */

static int Lpb_delete(lua_State *L) {
    pb_State *S;
    if (lua53_getfield(L, LUA_REGISTRYINDEX, PB_STATE) != LUA_TUSERDATA)
        return 0;
    S = (pb_State*)lua_touserdata(L, -1);
    if (S != NULL) {
        pb_free(S);
        lua_pushnil(L);
        lua_setfield(L, LUA_REGISTRYINDEX, PB_STATE);
    }
    return 0;
}

static pb_State *default_state(lua_State *L) {
    pb_State *S;
    if (lua53_getfield(L, LUA_REGISTRYINDEX, PB_STATE) == LUA_TUSERDATA) {
        S = (pb_State*)lua_touserdata(L, -1);
        lua_pop(L, 1);
    }
    else {
        S = lua_newuserdata(L, sizeof(pb_State));
        pb_init(S);
        lua_createtable(L, 0, 1);
        lua_pushcfunction(L, Lpb_delete);
        lua_setfield(L, -2, "__gc");
        lua_setmetatable(L, -2);
        lua_setfield(L, LUA_REGISTRYINDEX, PB_STATE);
    }
    return S;
}

static pb_Name *lpb_str2name(lua_State *L, const char *s) {
    pb_State *S = default_state(L);
    pb_Name *n = pb_name(S, s);
    if (n == NULL && s != NULL && *s != '.') {
        luaL_Buffer B;
        luaL_buffinit(L, &B);
        luaL_addchar(&B, '.');
        luaL_addstring(&B, s);
        luaL_pushresult(&B);
        n = pb_name(S, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    return n;
}

static pb_Name *lpb_toname(lua_State *L, int idx)
{ return lpb_str2name(L, lua_tostring(L, idx)); }

static pb_Name *lpb_checkname(lua_State *L, int idx)
{ return lpb_str2name(L, luaL_checkstring(L, idx)); }

static pb_Type *lpb_checktype(lua_State *L, int idx)
{ return pb_type(default_state(L), lpb_checkname(L, idx)); }

static int Lpb_clear(lua_State *L) {
    pb_State *S = default_state(L);
    if (lua_isnoneornil(L, 1))
        pb_free(S), pb_init(S);
    else
        pb_deltype(S, lpb_checktype(L, 1));
    return 0;
}

static int Lpb_load(lua_State *L) {
    pb_State *S = default_state(L);
    pb_Slice s[2];
    s[0] = s[1] = lpb_checkslice(L, 1);
    lua_pushboolean(L, pb_load(S, s) == PB_OK);
    lua_pushinteger(L, lpb_pos(s));
    return 2;
}

static int Lpb_loadfile(lua_State *L) {
    pb_State *S = default_state(L);
    const char *filename = luaL_checkstring(L, 1);
    size_t size;
    pb_Buffer b;
    pb_Slice s[2];
    int ret;
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL)
        return luaL_fileresult(L, errno, filename);
    pb_initbuffer(&b);
    do {
        size = fread(pb_prepbuffsize(&b, BUFSIZ), 1, BUFSIZ, fp);
        pb_addsize(&b, size);
    } while (size == BUFSIZ);
    s[0] = s[1] = pb_result(&b);
    ret = pb_load(S, s);
    pb_resetbuffer(&b);
    lua_pushboolean(L, ret == PB_OK);
    lua_pushinteger(L, lpb_pos(s));
    return 2;
}

static int lpb_pushtype(lua_State *L, pb_Type *t) {
    if (t == NULL) return 0;
    lua_pushstring(L, (char*)t->name);
    lua_pushstring(L, (char*)t->basename);
    lua_pushstring(L, t->is_map ? "map" : t->is_enum ? "enum" : "message");
    return 3;
}

static int lpb_pushfield(lua_State *L, pb_Field *f) {
    if (f == NULL) return 0;
    lua_pushstring(L, (char*)f->name);
    lua_pushinteger(L, f->number);
    lua_pushstring(L, f->type ? (char*)f->type->name :
            pb_typename(f->type_id, "<unknown>"));
    lua_pushstring(L, (char*)f->default_value);
    lua_pushstring(L, f->packed ? "packed" :
            f->repeated ? "repeated" : "optional");
    return 5;
}

static int Lpb_typesiter(lua_State *L) {
    pb_State *S = default_state(L);
    pb_Name *prev = lpb_toname(L, 2);
    pb_Type *t = pb_type(S, prev);
    if ((t == NULL && !lua_isnoneornil(L, 2)) || !pb_nexttype(S, &t))
        return 0;
    return lpb_pushtype(L, t);
}

static int Lpb_types(lua_State *L) {
    lua_pushcfunction(L, Lpb_typesiter);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
}

static int Lpb_fieldsiter(lua_State *L) {
    pb_Type *t = lpb_checktype(L, 1);
    pb_Name *prev = lpb_toname(L, 2);
    pb_Field *f = pb_fname(t, prev);
    if ((f == NULL && !lua_isnoneornil(L, 2)) || !pb_nextfield(t, &f))
        return 0;
    return lpb_pushfield(L, f);
}

static int Lpb_fields(lua_State *L) {
    lua_pushcfunction(L, Lpb_fieldsiter);
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    return 3;
}

static int Lpb_type(lua_State *L) {
    return lpb_pushtype(L, lpb_checktype(L, 1));
}

static int Lpb_field(lua_State *L) {
    pb_Type *t = lpb_checktype(L, 1);
    int isint, number = (int)lua_tointegerx(L, 2, &isint);
    pb_Field *f = isint ? pb_field(t, number) : pb_fname(t, lpb_toname(L, 2));
    return lpb_pushfield(L, f);
}

static int Lpb_enum(lua_State *L) {
    pb_Type *t = lpb_checktype(L, 1);
    int isint, number = (int)lua_tointegerx(L, 2, &isint);
    pb_Field *f = isint ? pb_field(t, number) : pb_fname(t, lpb_toname(L, 2));
    if (f == NULL) return 0;
    if (isint)
        lua_pushstring(L, (char*)f->name);
    else
        lua_pushinteger(L, f->number);
    return 1;
}


/* encode protobuf */

static void lpb_encode (lua_State *L, pb_Buffer *b, pb_Type *t);

static void lpb_checktable(lua_State *L, pb_Field *f) {
    if (!lua_istable(L, -1))
        argerror(L, 2, "table expected at field '%s', got %s",
                (char*)f->name, luaL_typename(L, -1));
}

static void lpbE_enum(lua_State *L, pb_Buffer *b, pb_Field *f) {
    int type = lua_type(L, -1);
    if (type == LUA_TNUMBER) {
        lua_Integer v = lua_tointeger(L, -1);
        pb_addvarint64(b, (uint64_t)v);
    }
    else if (type != LUA_TSTRING && type != LUA_TUSERDATA)
        argerror(L, 2, "number/string expected at field '%s', %s got",
                (char*)f->name, luaL_typename(L, -1));
    else {
        pb_Name *name = lpb_toname(L, -1);
        pb_Field *ev = pb_fname(f->type, name);;
        if (ev) pb_addvarint32(b, ev->number);
    }
}

static void lpbE_field(lua_State *L, pb_Buffer *b, pb_Field *f, int hastag) {
    size_t len;
    int ltype;
    if (hastag) pb_addvarint32(b, pb_pair(f->number, pb_wtypebytype(f->type_id)));
    switch (f->type_id) {
    case PB_Tenum:
        lpbE_enum(L, b, f);
        break;

    case PB_Tmessage:
        lpb_checktable(L, f);
        len = pb_bufflen(b);
        lpb_encode(L, b, f->type);
        if (pb_bufflen(b) > len) lpb_addlength(L, b, len);
        break;

    default:
        if ((ltype = lpb_addtype(L, b, -1, f->type_id)) != 0)
            argerror(L, 2, "%s expected at field '%s', got %s",
                    lua_typename(L, ltype),
                    (char*)f->name, luaL_typename(L, -1));
    }
}

static void lpbE_map(lua_State *L, pb_Buffer *b, pb_Field *f) {
    size_t len;
    if (f->type == NULL) return;
    lpb_checktable(L, f);
    if (f->packed) {
        pb_addvarint32(b, pb_pair(f->number, PB_TBYTES));
        len = pb_bufflen(b);
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            size_t ilen = pb_bufflen(b);
            lpbE_field(L, b, pb_field(f->type, 2), 1);
            lua_pop(L, 1);
            lpbE_field(L, b, pb_field(f->type, 1), 1);
            lpb_addlength(L, b, ilen);
        }
        lpb_addlength(L, b, len);
    }
    else {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            pb_addvarint32(b, pb_pair(f->number, PB_TBYTES));
            len = pb_bufflen(b);
            lpbE_field(L, b, pb_field(f->type, 2), 1);
            lua_pop(L, 1);
            lpbE_field(L, b, pb_field(f->type, 1), 1);
            lpb_addlength(L, b, len);
        }
    }
    lua_pop(L, 1);
}

static void lpbE_repeated(lua_State *L, pb_Buffer *b, pb_Field *f) {
    int i;
    lpb_checktable(L, f);
    if (f->packed) {
        size_t len;
        pb_addvarint32(b, pb_pair(f->number, PB_TBYTES));
        len = pb_bufflen(b);
        for (i = 1; lua53_rawgeti(L, -1, i) != LUA_TNIL; ++i) {
            lpbE_field(L, b, f, 0);
            lua_pop(L, 1);
        }
        lpb_addlength(L, b, len);
    }
    else {
        for (i = 1; lua53_rawgeti(L, -1, i) != LUA_TNIL; ++i) {
            lpbE_field(L, b, f, 1);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

static void lpb_encode(lua_State *L, pb_Buffer *b, pb_Type *t) {
    luaL_checkstack(L, 3, "message too many levels");
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            pb_Field *f = pb_fname(t, lpb_toname(L, -2));
            if (f == NULL)
                /* skip */;
            else if (f->type && f->type->is_map)
                lpbE_map(L, b, f);
            else if (f->repeated)
                lpbE_repeated(L, b, f);
            else
                lpbE_field(L, b, f, 1);
        }
        lua_pop(L, 1);
    }
}

static int lpb_encode_helper(lua_State *L) {
    pb_Buffer *b = (pb_Buffer*)lua_touserdata(L, 1);
    pb_Type *t = (pb_Type*)lua_touserdata(L, 2);
    lpb_encode(L, b, t);
    lua_pushlstring(L, b->buff, b->size);
    return 1;
}

static int Lpb_encode(lua_State *L) {
    pb_Type *t = lpb_checktype(L, 1);
    pb_Buffer b;
    int ret = 1;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (t == NULL) {
        lua_pushnil(L);
        lua_pushfstring(L, "can not find type '%s'", lua_tostring(L, 1));
        return 2;
    }
    pb_initbuffer(&b);
    lua_pushcfunction(L, lpb_encode_helper);
    lua_pushlightuserdata(L, &b);
    lua_pushlightuserdata(L, t);
    lua_pushvalue(L, 2);
    if (lua_pcall(L, 3, 1, 0) != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        ret = 2;
    }
    pb_resetbuffer(&b);
    return ret;
}


/* decode protobuf */

static int lpb_decode(lua_State *L, pb_Slice *s, pb_Type *t);

static void lpb_fetchtable(lua_State *L, pb_Field *f) {
    if (lua53_getfield(L, -1, (char*)f->name) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, (char*)f->name);
    }
}

static void lpbD_field(lua_State *L, pb_Slice *s, pb_Field *f, uint32_t tag) {
    pb_Slice sv[2];
    pb_Field *ev;
    uint64_t u64;

    switch (f->type_id) {
    case PB_Tenum:
        lpb_readvarint64(L, s, &u64);
        ev = pb_field(f->type, (int32_t)u64);
        if (ev) lua_pushstring(L, (char*)ev->name);
        else    lua_pushinteger(L, (lua_Integer)u64);
        break;

    case PB_Tmessage:
        lpb_readbytes(L, s, sv), sv[1] = s[1];
        if (f->type) lpb_decode(L, sv, f->type);
        break;

    default:
        lpb_readtype(L, f->type_id, s, pb_wtypename(tag, NULL));
    }
}

static void lpbD_mapentry(lua_State *L, pb_Slice *s, pb_Type *t, int top) {
    pb_Slice p[2];
    int mask = 0;
    uint32_t tag;
    lpb_readbytes(L, s, p), p[1] = s[1];
    if (t == NULL) return;
    lua_pushnil(L);
    lua_pushnil(L);
    while (pb_readvarint32(p, &tag)) {
        int n = pb_gettag(tag);
        if (n == 1 || n == 2) {
            mask |= n;
            lpbD_field(L, p, pb_field(t, n), tag);
            lua_replace(L, top+n);
        }
    }
    if (mask == 3) lua_rawset(L, -3);
    else           lua_pop(L, 2);
}

static void lpbD_map(lua_State *L, pb_Slice *s, pb_Field *f) {
    int top = lua_gettop(L);
    lpb_fetchtable(L, f);
    if (!f->packed)
        lpbD_mapentry(L, s, f->type, top);
    else {
        pb_Slice p[2];
        lpb_readbytes(L, s, p), p[1] = s[1];
        while (p->p < p->end)
            lpbD_mapentry(L, p, f->type, top);
    }
    lua_pop(L, 1);
}

static void lpbD_repeated(lua_State *L, pb_Slice *s, pb_Field *f, uint32_t tag) {
    lpb_fetchtable(L, f);
    if (f->packed) {
        int len = lua_rawlen(L, -1);
        pb_Slice p[2];
        lpb_readbytes(L, s, p), p[1] = s[1];
        while (p->p < p->end) {
            lpbD_field(L, p, f, tag);
            lua_rawseti(L, -2, ++len);
        }
    }
    else {
        lpbD_field(L, s, f, tag);
        lua_rawseti(L, -2, lua_rawlen(L, -2) + 1);
    }
    lua_pop(L, 1);
}

static int lpb_decode(lua_State *L, pb_Slice *s, pb_Type *t) {
    uint32_t tag;
    lua_newtable(L);
    while (pb_readvarint32(s, &tag)) {
        pb_Field *f = pb_field(t, pb_gettag(tag));
        if (f == NULL)
            pb_skipvalue(s, tag);
        else if (f->type && f->type->is_map)
            lpbD_map(L, s, f);
        else if (f->repeated)
            lpbD_repeated(L, s, f, tag);
        else if (!f->type || f->type->field_count != 0) {
            lua_pushstring(L, (char*)f->name);
            lpbD_field(L, s, f, tag);
            lua_rawset(L, -3);
        }
    }
    return 1;
}

static int lpb_decode_helper(lua_State *L) {
    return lpb_decode(L,
            (pb_Slice*)lua_touserdata(L, 1),
            (pb_Type*)lua_touserdata(L, 2));
}

static int Lpb_decode(lua_State *L) {
    pb_Type *t = lpb_checktype(L, 1);
    pb_Slice s[2];
    if (t == NULL) {
        lua_pushnil(L);
        lua_pushfstring(L, "can not find type '%s'", lua_tostring(L, 1));
        return 2;
    }
    s[0] = s[1] = lpb_checkslice(L, 2);
    lua_pushcfunction(L, lpb_decode_helper);
    lua_pushlightuserdata(L, s);
    lua_pushlightuserdata(L, t);
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

LUALIB_API int luaopen_pb(lua_State *L) {
    luaL_Reg libs[] = {
        { "pack",   Lbuf_pack     },
        { "unpack", Lslice_unpack },
#define ENTRY(name) { #name, Lpb_##name }
        ENTRY(clear),
        ENTRY(load),
        ENTRY(loadfile),
        ENTRY(encode),
        ENTRY(decode),
        ENTRY(types),
        ENTRY(fields),
        ENTRY(type),
        ENTRY(field),
        ENTRY(enum),
#undef  ENTRY
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
    return 1;
}

/* cc: flags+='-ggdb -pedantic -std=c90 -Wall -Wextra --coverage'
 * cc: flags+='-shared -undefined dynamic_lookup' output='pb.so' */

