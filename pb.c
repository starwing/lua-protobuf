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
#define check_slice(L,idx)  ((pb_SliceExt*)checkudata(L,idx,PB_SLICE))
#define test_slice(L,idx)   ((pb_SliceExt*)testudata(L,idx,PB_SLICE))
#define return_self(L) { lua_settop(L, 1); return 1; }

#if LUA_VERSION_NUM < 502
#include <assert.h>

# define LUA_OK        0
# define lua_rawlen    lua_objlen
# define luaL_setfuncs(L,l,n) (assert(n==0), luaL_register(L,NULL,l))
# define luaL_setmetatable(L, name) \
    (luaL_getmetatable((L), (name)), lua_setmetatable(L, -2))

static int relindex(int idx, int offset)
{ return idx < 0 && idx > LUA_REGISTRYINDEX ? idx + offset : idx; }

void lua_rawgetp(lua_State *L, int idx, const void *p) {
    lua_pushlightuserdata(L, (void*)p);
    lua_rawget(L, relindex(idx, 1));
}

void lua_rawsetp(lua_State *L, int idx, const void *p) {
    lua_pushlightuserdata(L, (void*)p);
    lua_insert(L, -2);
    lua_rawset(L, relindex(idx, 1));
}

#ifndef luaL_newlib /* not LuaJIT 2.1 */
#define luaL_newlib(L,l) (lua_newtable(L), luaL_register(L,NULL,l))

static lua_Integer lua_tointegerx(lua_State *L, int idx, int *isint) {
    lua_Integer i = lua_tointeger(L, idx);
    if (isint) *isint = (i != 0 || lua_type(L, idx) == LUA_TNUMBER);
    return i;
}

static lua_Number lua_tonumberx(lua_State *L, int idx, int *isnum) {
    lua_Number i = lua_tonumber(L, idx);
    if (isnum) *isnum = (i != 0 || lua_type(L, idx) == LUA_TNUMBER);
    return i;
}
#endif

#ifdef LUAI_BITSINT /* not LuaJIT */
#include <errno.h>
static int luaL_fileresult(lua_State *L, int stat, const char *fname) {
    int en = errno;
    if (stat) { lua_pushboolean(L, 1); return 1; }
    lua_pushnil(L);
    lua_pushfstring(L, "%s: %s", fname, strerror(en));
    /*if (fname) lua_pushfstring(L, "%s: %s", fname, strerror(en));
      else       lua_pushstring(L, strerror(en));*//* NOT USED */
    lua_pushinteger(L, en);
    return 3;
}
#endif /* not LuaJIT */

#endif

#if LUA_VERSION_NUM >= 503
# define lua53_getfield lua_getfield
# define lua53_rawgeti  lua_rawgeti
# define lua53_rawgetp  lua_rawgetp
#else
static int lua53_getfield(lua_State *L, int idx, const char *field)
{ lua_getfield(L, idx, field); return lua_type(L, -1); }
static int lua53_rawgeti(lua_State *L, int idx, lua_Integer i)
{ lua_rawgeti(L, idx, i); return lua_type(L, -1); }
static int lua53_rawgetp(lua_State *L, int idx, const void *p)
{ lua_rawgetp(L, idx, p); return lua_type(L, -1); }
#endif


typedef struct pb_SliceExt {
    pb_Slice    base;
    const char *head;
} pb_SliceExt;

static int lpb_offset(pb_SliceExt *s) { return (int)(s->base.p-s->head) + 1; }

static pb_SliceExt lpb_initext(pb_Slice s)
{ pb_SliceExt ext; ext.base = s, ext.head = s.p; return ext; }

static void lpb_addlength(lua_State *L, pb_Buffer *b, size_t len)
{ if (pb_addlength(b, len) == 0) luaL_error(L, "encode bytes fail"); }

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
    else return (lua_Integer)len + pos + 1;
}

static lua_Integer rangerelat(lua_State *L, int idx, lua_Integer *i, lua_Integer *j, size_t len) {
    *i = posrelat(luaL_optinteger(L, idx, 1), len);
    *j = posrelat(luaL_optinteger(L, idx+1, len), len);
    if (*i < 1) *i = 1;
    if (*j > (lua_Integer)len) *j = len;
    return *i <= *j ? *j - *i + 1 : 0;
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
        ret = pb_lslice(s, len);
    }
    else if (type == LUA_TUSERDATA) {
        pb_Buffer *buffer;
        pb_SliceExt *s;
        if ((buffer = test_buffer(L, idx)) != NULL)
            ret = pb_result(buffer);
        else if ((s = test_slice(L, idx)) != NULL)
            ret = s->base;
    }
    return ret;
}

static pb_Slice lpb_checkslice(lua_State *L, int idx) {
    pb_Slice ret = lpb_toslice(L, idx);
    if (ret.p == NULL) typeerror(L, idx, "string/buffer/slice");
    return ret;
}

static void lpb_readbytes(lua_State *L, pb_SliceExt *s, pb_SliceExt *pv) {
    uint64_t len = 0;
    if (pb_readvarint64(&s->base, &len) == 0 || len > PB_MAX_SIZET)
        luaL_error(L, "invalid bytes length: %d (at offset %d)",
                (int)len, lpb_offset(s));
    if (pb_readslice(&s->base, (size_t)len, &pv->base) == 0 && len != 0)
        luaL_error(L, "un-finished bytes (len %d at offset %d)",
                (int)len, lpb_offset(s));
    pv->head = pv->base.p;
}

typedef union lpb_Value {
    pb_SliceExt s[1];
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
        if (ret) pb_addvarint32(b, (uint32_t)lua_tointeger(L, idx));
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
        if (ret) pb_addvarint64(b, (uint64_t)v.lint);
        break;
    case PB_Tsint64:
        v.lint = lua_tointegerx(L, idx, &ret);
        if (ret) pb_addvarint64(b, pb_encode_sint64((int64_t)v.lint));
        break;
    case PB_Tbytes: case PB_Tstring:
        v.s->base = lpb_toslice(L, idx);
        if ((ret = v.s->base.p != NULL)) pb_addbytes(b, v.s->base);
        expected = LUA_TSTRING;
        break;
    default:
        /* NOT REACHED */
        /* argerror(L, idx, "unknown type %s",
         *          pb_typename(type, "<unknown>")) */;
    }
    return ret ? 0 : expected;
}

static void lpb_readtype(lua_State *L, int type, pb_SliceExt *s) {
    lpb_Value v;
    switch (type) {
    case PB_Tbool:  case PB_Tenum:
    case PB_Tint32: case PB_Tuint32: case PB_Tsint32:
    case PB_Tint64: case PB_Tuint64: case PB_Tsint64:
        if (pb_readvarint64(&s->base, &v.u64) == 0)
            luaL_error(L, "invalid varint value at offset %d", lpb_offset(s));
        switch (type) {
        case PB_Tbool:   lua_pushboolean(L, v.u64 != 0); break;
         /*case PB_Tenum:   lua_pushinteger(L, v.u64); break; [> NOT REACHED <]*/
        case PB_Tint32:  lua_pushinteger(L, (int32_t)v.u64); break;
        case PB_Tuint32: lua_pushinteger(L, (uint32_t)v.u64); break;
        case PB_Tsint32: lua_pushinteger(L, pb_decode_sint32((uint32_t)v.u64)); break;
        case PB_Tint64:  lua_pushinteger(L, (int64_t)v.u64); break;
        case PB_Tuint64: lua_pushinteger(L, (uint64_t)v.u64); break;
        case PB_Tsint64: lua_pushinteger(L, pb_decode_sint64(v.u64)); break;
        }
        break;
    case PB_Tfloat:
    case PB_Tfixed32:
    case PB_Tsfixed32:
        if (pb_readfixed32(&s->base, &v.u32) == 0)
            luaL_error(L, "invalid fixed32 value at offset %d", lpb_offset(s));
        switch (type) {
        case PB_Tfloat:    lua_pushnumber(L, pb_decode_float(v.u32)); break;
        case PB_Tfixed32:  lua_pushinteger(L, v.u32); break;
        case PB_Tsfixed32: lua_pushinteger(L, (int32_t)v.u32); break;
        }
        break;
    case PB_Tdouble:
    case PB_Tfixed64:
    case PB_Tsfixed64:
        if (pb_readfixed64(&s->base, &v.u64) == 0)
            luaL_error(L, "invalid fixed64 value at offset %d", lpb_offset(s));
        switch (type) {
        case PB_Tdouble:   lua_pushnumber(L, pb_decode_double(v.u64)); break;
        case PB_Tfixed64:  lua_pushinteger(L, v.u64); break;
        case PB_Tsfixed64: lua_pushinteger(L, (int64_t)v.u64); break;
        }
        break;
    case PB_Tbytes:
    case PB_Tstring:
    case PB_Tmessage:
        lpb_readbytes(L, s, v.s);
        lua_pushlstring(L, v.s->base.p, pb_len(v.s->base));
        break;

    default:
        /* NOT REACHED */
        /* luaL_error(L, "unknown type %s", pb_typename(type, NULL)) */;
    }
}


/* io routines */

#ifdef _WIN32
# include <io.h>
# include <fcntl.h>
#else
# define setmode(a,b)  ((void)0)
#endif

static int io_read(lua_State *L) {
    FILE *fp = (FILE*)lua_touserdata(L, 1);
    size_t nr;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    do {  /* read file in chunks of LUAL_BUFFERSIZE bytes */
        char *p = luaL_prepbuffer(&b);
        nr = fread(p, sizeof(char), LUAL_BUFFERSIZE, fp);
        luaL_addsize(&b, nr);
    } while (nr == LUAL_BUFFERSIZE);
    luaL_pushresult(&b);  /* close buffer */
    return 1;
}

static int io_write(lua_State *L, FILE *f, int idx) {
    int nargs = lua_gettop(L) - idx + 1;
    int status = 1;
    for (; nargs--; idx++) {
        pb_Slice s = lpb_checkslice(L, idx);
        size_t l = pb_len(s);
        status = status && (fwrite(s.p, sizeof(char), l, f) == l);
    }
    return status ? 1 : luaL_fileresult(L, 0, NULL);
}

static int Lio_read(lua_State *L) {
    const char *fname = luaL_optstring(L, 1, NULL);
    FILE *fp = stdin;
    int ret;
    if (fname == NULL)
        (void)setmode(fileno(stdin), O_BINARY);
    else if ((fp = fopen(fname, "rb")) == NULL)
        return luaL_fileresult(L, 0, fname);
    lua_pushcfunction(L, io_read);
    lua_pushlightuserdata(L, fp);
    ret = lua_pcall(L, 1, 1, 0);
    if (fp != stdin) fclose(fp);
    else (void)setmode(fileno(stdin), O_TEXT);
    if (ret != LUA_OK) { lua_pushnil(L); lua_insert(L, -2); return 2; }
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

static int lpb_typefmt(const char *fmt) {
    switch (*fmt) {
    case 'b': return PB_Tbool;
    case 'f': return PB_Tfloat;
    case 'F': return PB_Tdouble;
    case 'i': return PB_Tint32;
    case 'j': return PB_Tsint32;
    case 'u': return PB_Tuint32;
    case 'x': return PB_Tfixed32;
    case 'y': return PB_Tsfixed32;
    case 'I': return PB_Tint64;
    case 'J': return PB_Tsint64;
    case 'U': return PB_Tuint64;
    case 'X': return PB_Tfixed64;
    case 'Y': return PB_Tsfixed64;
    }
    return -1;
}

static int lpb_packfmt(lua_State *L, int idx, pb_Buffer *b, const char **pfmt, int level) {
    const char *fmt = *pfmt;
    int type, ltype;
    size_t len;
    luaL_argcheck(L, 1, level <= 100, "format level overflow");
    for (; *fmt != '\0'; ++fmt) {
        switch (*fmt) {
        case 'v': pb_addvarint64(b, (uint64_t)luaL_checkinteger(L, idx++)); break;
        case 'd': pb_addfixed32(b, (uint32_t)luaL_checkinteger(L, idx++)); break;
        case 'q': pb_addfixed64(b, (uint64_t)luaL_checkinteger(L, idx++)); break;
        case 'c': pb_addslice(b, lpb_checkslice(L, idx++)); break;
        case 's': pb_addbytes(b, lpb_checkslice(L, idx++)); break;
        case '#': lpb_addlength(L, b, (size_t)luaL_checkinteger(L, idx++)); break;
        case '(':
            len = pb_bufflen(b);
            ++fmt;
            idx = lpb_packfmt(L, idx, b, &fmt, level+1);
            lpb_addlength(L, b, len);
            break;
        case ')':
            if (level == 0) luaL_argerror(L, 1, "unexpected ')' in format");
            *pfmt = fmt;
            return idx;
        case '\0':
        default:
            if ((type = lpb_typefmt(fmt)) < 0)
                argerror(L, 1, "invalid formater: '%c'", *fmt);
            if ((ltype = lpb_addtype(L, b, idx, type)) != 0)
                argerror(L, idx, "%s expected for type '%s', got %s",
                        lua_typename(L, ltype), pb_typename(type, "<unknown>"),
                        luaL_typename(L, idx));
            ++idx;
        }
    }
    if (level != 0) luaL_argerror(L, 1, "unmatch '(' in format");
    *pfmt = fmt;
    return idx;
}

static int Lpb_tohex(lua_State *L) {
    pb_Slice s = lpb_checkslice(L, 1);
    const char *hexa = "0123456789ABCDEF";
    char hex[4] = "XX ";
    lua_Integer i = 1, j = -1;
    luaL_Buffer lb;
    rangerelat(L, 2, &i, &j, pb_len(s));
    luaL_buffinit(L, &lb);
    for (; i <= j; ++i) {
        unsigned int ch = s.p[i-1];
        hex[0] = hexa[(ch>>4)&0xF];
        hex[1] = hexa[(ch   )&0xF];
        if (i == j) hex[2] = '\0';
        luaL_addstring(&lb, hex);
    }
    luaL_pushresult(&lb);
    return 1;
}

static int Lpb_result(lua_State *L) {
    pb_Slice s = lpb_checkslice(L, 1);
    lua_Integer i = 1, j = -1;
    lua_Integer range = rangerelat(L, 2, &i, &j, pb_len(s));
    lua_pushlstring(L, s.p+i-1, (size_t)range);
    return 1;
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
    luaL_setmetatable(L, PB_BUFFER);
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
    if (pb == NULL) pb_initbuffer(pb = &b);
    lpb_packfmt(L, idx, pb, &fmt, 0);
    if (pb != &b)
        lua_settop(L, 1);
    else {
        pb_Slice ret = pb_result(pb);
        lua_pushlstring(L, ret.p, pb_len(ret));
        pb_resetbuffer(pb);
    }
    return 1;
}

LUALIB_API int luaopen_pb_buffer(lua_State *L) {
    luaL_Reg libs[] = {
        { "__tostring", Lbuf_tostring },
        { "__len",      Lbuf_len },
        { "__gc",       Lbuf_reset },
        { "delete",     Lbuf_reset },
        { "tohex",      Lpb_tohex },
        { "result",     Lpb_result },
#define ENTRY(name) { #name, Lbuf_##name }
        ENTRY(new),
        ENTRY(reset),
        ENTRY(pack),
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
    pb_SliceExt  curr;
    pb_SliceExt *buff;
    size_t       used;
    size_t       size;
    pb_SliceExt  init_buff[LPB_INITSTACKLEN];
} lpb_Slice;

static void lpb_resetslice(lua_State *L, lpb_Slice *s) {
    if (s->buff != s->init_buff)
        free(s->buff);
    memset(s, 0, sizeof(lpb_Slice));
    s->buff = s->init_buff;
    s->size = LPB_INITSTACKLEN;
    lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, s);
}

static pb_SliceExt lpb_checkview(lua_State *L, int idx, pb_SliceExt *ps) {
    pb_Slice src = lpb_checkslice(L, idx);
    lua_Integer i = 1, j = -1;
    lua_Integer range = rangerelat(L, idx+1, &i, &j, pb_len(src));
    pb_SliceExt ret;
    if (ps) ps->base = src, ps->head = src.p;
    ret.base.p   = src.p + i - 1;
    ret.base.end = ret.base.p + range;
    ret.head     = src.p;
    return ret;
}

static void lpb_enterview(lua_State *L, lpb_Slice *s, pb_SliceExt view) {
    if (s->used >= s->size) {
        size_t newsize = s->size * 2;
        pb_SliceExt *oldp = s->buff != s->init_buff ? s->buff : NULL;
        pb_SliceExt *newp = (pb_SliceExt*)realloc(oldp, newsize*sizeof(pb_SliceExt));
        if (newp == NULL) { luaL_error(L, "out of memory"); return; }
        if (oldp == NULL) memcpy(newp, s->buff, s->used*sizeof(pb_SliceExt));
        s->buff = newp;
        s->size = newsize;
    }
    s->buff[s->used++] = s->curr;
    s->curr = view;
}

static void lpb_initslice(lua_State *L, int idx, lpb_Slice *s) {
    memset(s, 0, sizeof(lpb_Slice));
    s->buff = s->init_buff;
    s->size = LPB_INITSTACKLEN;
    if (!lua_isnoneornil(L, idx)) {
        pb_SliceExt base, view = lpb_checkview(L, idx, &base);
        s->curr = base;
        lpb_enterview(L, s, view);
        lua_pushvalue(L, idx);
        lua_rawsetp(L, LUA_REGISTRYINDEX, s);
    }
}

static int lpb_unpackscalar(lua_State *L, int *pidx, int top, int fmt, pb_SliceExt *s) {
    lua_Integer i;
    lpb_Value v;
    switch (fmt) {
    case 'v':
        if (pb_readvarint64(&s->base, &v.u64) == 0)
            luaL_error(L, "invalid varint value at offset %d", lpb_offset(s));
        lua_pushinteger(L, v.u64);
        break;
    case 'd':
        if (pb_readfixed32(&s->base, &v.u32) == 0)
            luaL_error(L, "invalid fixed32 value at offset %d", lpb_offset(s));
        lua_pushinteger(L, v.u32);
        break;
    case 'q':
        if (pb_readfixed64(&s->base, &v.u64) == 0)
            luaL_error(L, "invalid fixed64 value at offset %d", lpb_offset(s));
        lua_pushinteger(L, v.u64);
        break;
    case 's':
        if (pb_readbytes(&s->base, &v.s->base) == 0)
            luaL_error(L, "invalid bytes value at offset %d", lpb_offset(s));
        lua_pushlstring(L, v.s->base.p, pb_len(v.s->base));
        break;
    case 'c':
        luaL_argcheck(L, 1, *pidx <= top, "format argument exceed");
        i = luaL_checkinteger(L, *pidx++);
        if (pb_readslice(&s->base, (size_t)i, &v.s->base) == 0)
            luaL_error(L, "invalid sub string at offset %d", lpb_offset(s));
        lua_pushlstring(L, v.s->base.p, pb_len(v.s->base));
        break;
    default:
        return 0;
    }
    return 1;
}

static int lpb_unpackloc(lua_State *L, int *pidx, int top, int fmt, pb_SliceExt *s, int *prets) {
    lua_Integer li;
    size_t len = s->base.end - s->head;
    switch (fmt) {
    case '@':
        lua_pushinteger(L, lpb_offset(s));
        ++*prets;
        break;

    case '*': case '+':
        luaL_argcheck(L, 1, *pidx <= top, "format argument exceed");
        if (fmt == '*')
            li = posrelat(luaL_checkinteger(L, *pidx++), len);
        else
            li = lpb_offset(s) + luaL_checkinteger(L, *pidx++);
        if (li == 0) li = 1;
        if (li > (lua_Integer)len) li = len + 1;
        s->base.p = s->head + li - 1;
        break;

    default:
        return 0;
    }
    return 1;
}

static int lpb_unpackfmt(lua_State *L, int idx, const char *fmt, pb_SliceExt *s) {
    int rets = 0, top = lua_gettop(L), type;
    for (; *fmt != '\0'; ++fmt) {
        if (lpb_unpackloc(L, &idx, top, *fmt, s, &rets))
            continue;
        if (s->base.p >= s->base.end) { lua_pushnil(L); return rets + 1; }
        luaL_checkstack(L, 1, "too many values");
        if (!lpb_unpackscalar(L, &idx, top, *fmt, s)) {
            if ((type = lpb_typefmt(fmt)) < 0)
                argerror(L, 1, "invalid formater: '%c'", *fmt);
            lpb_readtype(L, type, s);
        }
        ++rets;
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
    luaL_setmetatable(L, PB_SLICE);
    return 1;
}

static int Lslice_reset(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    lpb_resetslice(L, s);
    if (!lua_isnoneornil(L, 2))
        lpb_initslice(L, 2, s);
    return_self(L);
}

static int Lslice_tostring(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    lua_pushfstring(L, "pb.Slice: %p", s);
    return 1;
}

static int Lslice_len(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    lua_pushinteger(L, (lua_Integer)pb_len(s->curr.base));
    lua_pushinteger(L, (lua_Integer)lpb_offset(&s->curr));
    return 2;
}

static int Lslice_level(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    if (!lua_isnoneornil(L, 2)) {
        pb_SliceExt *se;
        lua_Integer level = posrelat(luaL_checkinteger(L, 2), s->used);
        if (level > (lua_Integer)s->used)
            return 0;
        else if (level == (lua_Integer)s->used)
            se = &s->curr;
        else
            se = &s->buff[level];
        lua_pushinteger(L, se->base.p   - s->buff[0].head + 1);
        lua_pushinteger(L, se->head     - s->buff[0].head + 1);
        lua_pushinteger(L, se->base.end - s->buff[0].head);
        return 3;
    }
    lua_pushinteger(L, s->used);
    return 1;
}

static int Lslice_enter(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    pb_SliceExt view;
    if (lua_isnoneornil(L, 2)) {
        if (pb_readbytes(&s->curr.base, &view.base) == 0)
            return argerror(L, 1, "bytes wireformat expected at offset %d",
                    lpb_offset(&s->curr));
        view.head = view.base.p;
        lpb_enterview(L, s, view);
    }
    else {
        lua_Integer i = 1, j = -1;
        lua_Integer range = rangerelat(L, 2, &i, &j, s->curr.base.end - s->curr.head);
        view.base.p   = s->curr.head + i - 1;
        view.base.end = view.base.p + range;
        view.head     = s->curr.head;
        lpb_enterview(L, s, view);
    }
    return_self(L);
}

static int Lslice_leave(lua_State *L) {
    lpb_Slice *s = (lpb_Slice*)check_slice(L, 1);
    lua_Integer count = posrelat(luaL_optinteger(L, 2, 1), s->used);
    if (count > (lua_Integer)s->used)
        argerror(L, 2, "level (%d) exceed max level %d",
                (int)count, (int)s->used);
    else if (count == (lua_Integer)s->used) {
        s->curr = s->buff[0];
        s->used = 1;
    }
    else {
        s->used -= (size_t)count;
        s->curr = s->buff[s->used];
    }
    lua_settop(L, 1);
    lua_pushinteger(L, s->used);
    return 2;
}

static int Lslice_unpack(lua_State *L) {
    pb_SliceExt view, *s = test_slice(L, 1);
    const char *fmt = luaL_checkstring(L, 2);
    if (s == NULL) view = lpb_initext(lpb_checkslice(L, 1)), s = &view;
    return lpb_unpackfmt(L, 3, fmt, s);
}

LUALIB_API int luaopen_pb_slice(lua_State *L) {
    luaL_Reg libs[] = {
        { "__tostring", Lslice_tostring },
        { "__len",      Lslice_len   },
        { "__gc",       Lslice_reset },
        { "delete",     Lslice_reset },
        { "tohex",      Lpb_tohex   },
        { "result",     Lpb_result  },
#define ENTRY(name) { #name, Lslice_##name }
        ENTRY(new),
        ENTRY(reset),
        ENTRY(level),
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


/* high level typeinfo/encode/decode routines */

#define default_state(L) ((pb_State*)default_lstate(L))

static const char state_name[] = PB_STATE;

typedef struct lpb_State {
    pb_State base;
    int enum_as_value;
} lpb_State;

static int Lpb_delete(lua_State *L) {
    if (lua53_rawgetp(L, LUA_REGISTRYINDEX, state_name) == LUA_TUSERDATA) {
        lpb_State *LS = (lpb_State*)lua_touserdata(L, -1);
        if (LS != NULL) {
            pb_free(&LS->base);
            lua_pushnil(L);
            lua_setfield(L, LUA_REGISTRYINDEX, PB_STATE);
        }
    }
    return 0;
}

static lpb_State *default_lstate(lua_State *L) {
    lpb_State *LS;
    if (lua53_rawgetp(L, LUA_REGISTRYINDEX, state_name) == LUA_TUSERDATA) {
        LS = (lpb_State*)lua_touserdata(L, -1);
        lua_pop(L, 1);
    }
    else {
        LS = lua_newuserdata(L, sizeof(lpb_State));
        lua_replace(L, -2);
        memset(LS, 0, sizeof(lpb_State));
        pb_init(&LS->base);
        lua_createtable(L, 0, 1);
        lua_pushcfunction(L, Lpb_delete);
        lua_setfield(L, -2, "__gc");
        lua_setmetatable(L, -2);
        lua_rawsetp(L, LUA_REGISTRYINDEX, state_name);
    }
    return LS;
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
    else if (lua_isnoneornil(L, 2))
        pb_deltype(S, lpb_checktype(L, 1));
    else {
        pb_Type *t = lpb_checktype(L, 1);
        pb_Field *f = pb_fname(t, lpb_checkname(L, 2));
        if (f) pb_delfield(S, t, f);
    }
    return 0;
}

static int Lpb_load(lua_State *L) {
    pb_State *S = default_state(L);
    pb_SliceExt s = lpb_initext(lpb_checkslice(L, 1));
    lua_pushboolean(L, pb_load(S, &s.base) == PB_OK);
    lua_pushinteger(L, lpb_offset(&s));
    return 2;
}

static int Lpb_loadfile(lua_State *L) {
    pb_State *S = default_state(L);
    const char *filename = luaL_checkstring(L, 1);
    size_t size;
    pb_Buffer b;
    pb_SliceExt s;
    int ret;
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL)
        return luaL_fileresult(L, 0, filename);
    pb_initbuffer(&b);
    do {
        void *d = pb_prepbuffsize(&b, BUFSIZ);
        if (d == NULL) { fclose(fp); return luaL_error(L, "out of memory"); }
        size = fread(d, 1, BUFSIZ, fp);
        pb_addsize(&b, size);
    } while (size == BUFSIZ);
    fclose(fp);
    s = lpb_initext(pb_result(&b));
    ret = pb_load(S, &s.base);
    pb_resetbuffer(&b);
    lua_pushboolean(L, ret == PB_OK);
    lua_pushinteger(L, lpb_offset(&s));
    return 2;
}

static int lpb_pushtype(lua_State *L, pb_Type *t) {
    if (t == NULL) return 0;
    lua_pushstring(L, (char*)t->name);
    lua_pushstring(L, (char*)t->basename);
    lua_pushstring(L, t->is_map ? "map" : t->is_enum ? "enum" : "message");
    return 3;
}

static int lpb_pushfield(lua_State *L, pb_Type *t, pb_Field *f) {
    pb_OneofEntry *e;
    if (f == NULL) return 0;
    lua_pushstring(L, (char*)f->name);
    lua_pushinteger(L, f->number);
    lua_pushstring(L, f->type ? (char*)f->type->name :
            pb_typename(f->type_id, "<unknown>"));
    lua_pushstring(L, (char*)f->default_value);
    lua_pushstring(L, f->packed ? "packed" :
            f->repeated ? "repeated" : "optional");
    e = (pb_OneofEntry*)pb_gettable(&t->oneof_index, (pb_Key)f);
    if (e) {
        lua_pushstring(L, (const char*)e->name);
        lua_pushinteger(L, e->index-1);
        return 7;
    }
    return 5;
}

static int Lpb_typesiter(lua_State *L) {
    pb_State *S = default_state(L);
    pb_Name *prev = lpb_toname(L, 2);
    pb_Type *t = pb_type(S, prev);
    if ((t == NULL && !lua_isnoneornil(L, 2)))
        return 0;
    while (pb_nexttype(S, &t) && t->field_count == 0)
        continue;
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
    return lpb_pushfield(L, t, f);
}

static int Lpb_fields(lua_State *L) {
    lua_pushcfunction(L, Lpb_fieldsiter);
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    return 3;
}

static int Lpb_type(lua_State *L) {
    pb_Type *t = lpb_checktype(L, 1);
    if (t == NULL || t->field_count == 0)
        return 0;
    return lpb_pushtype(L, t);
}

static int Lpb_field(lua_State *L) {
    pb_Type *t = lpb_checktype(L, 1);
    int isint, number = (int)lua_tointegerx(L, 2, &isint);
    pb_Field *f = isint ? pb_field(t, number) : pb_fname(t, lpb_toname(L, 2));
    return lpb_pushfield(L, t, f);
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
        argerror(L, 2, "number/string expected at field '%s', got %s",
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
        lpb_addlength(L, b, len);
        break;

    default:
        if ((ltype = lpb_addtype(L, b, -1, f->type_id)) != 0)
            argerror(L, 2, "%s expected at field '%s', got %s",
                    lua_typename(L, ltype),
                    (char*)f->name, luaL_typename(L, -1));
    }
}

static void lpbE_map(lua_State *L, pb_Buffer *b, pb_Field *f) {
    pb_Field *kf = pb_field(f->type, 1);
    pb_Field *vf = pb_field(f->type, 2);
    size_t len;
    if (kf == NULL || vf == NULL) return;
    lpb_checktable(L, f);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        pb_addvarint32(b, pb_pair(f->number, PB_TBYTES));
        len = pb_bufflen(b);
        lpbE_field(L, b, vf, 1);
        lua_pop(L, 1);
        lpbE_field(L, b, kf, 1);
        lpb_addlength(L, b, len);
    }
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
            else if (!f->type || f->type->field_count != 0)
                lpbE_field(L, b, f, 1);
        }
        lua_pop(L, 1);
    }
}

static int lpb_encode_helper(lua_State *L) {
    pb_Buffer *b = (pb_Buffer*)lua_touserdata(L, 1);
    pb_Type *t = (pb_Type*)lua_touserdata(L, 2);
    pb_Buffer *r = test_buffer(L, 3);
    lpb_encode(L, b, t);
    if (r != b)
        lua_pushlstring(L, b->buff, b->size);
    else
        lua_pop(L, 1);
    return 1;
}

static int Lpb_encode(lua_State *L) {
    pb_Type *t = lpb_checktype(L, 1);
    pb_Buffer buf, *b = test_buffer(L, 3);
    luaL_checktype(L, 2, LUA_TTABLE);
    if (t == NULL) {
        lua_pushnil(L);
        lua_pushfstring(L, "type '%s' does not exists", lua_tostring(L, 1));
        return 2;
    }
    if (b == NULL) pb_initbuffer(&buf), b = &buf;
    lua_pushcfunction(L, lpb_encode_helper);
    lua_pushlightuserdata(L, b);
    lua_pushlightuserdata(L, t);
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 2);
    if (lua_pcall(L, 4, 1, 0) != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (b == &buf) pb_resetbuffer(&buf);
    return 1;
}


/* decode protobuf */

static int lpb_decode(lua_State *L, pb_SliceExt *s, pb_Type *t);

static void lpb_fetchtable(lua_State *L, pb_Field *f) {
    if (lua53_getfield(L, -1, (char*)f->name) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, (char*)f->name);
    }
}

static void lpbD_field(lua_State *L, pb_SliceExt *s, pb_Field *f, uint32_t tag) {
    pb_SliceExt sv;
    pb_Field *ev;
    uint64_t u64;

    switch (f->type_id) {
    case PB_Tenum:
        if (pb_readvarint64(&s->base, &u64) == 0)
            luaL_error(L, "invalid varint value at offset %d", lpb_offset(s));
        ev = pb_field(f->type, (int32_t)u64);
        if (default_lstate(L)->enum_as_value) ev = NULL;
        if (ev) lua_pushstring(L, (char*)ev->name);
        else    lua_pushinteger(L, (lua_Integer)u64);
        break;

    case PB_Tmessage:
        lpb_readbytes(L, s, &sv);
        if (f->type) lpb_decode(L, &sv, f->type);
        break;

    default:
        if (!f->packed && pb_wtypebytype(f->type_id) != (int)pb_gettype(tag))
            luaL_error(L, "type mismatch at offset %d, %s expected for type %s, got %s",
                    lpb_offset(s),
                    pb_wtypename(pb_wtypebytype(f->type_id), NULL),
                    pb_typename(f->type_id, NULL),
                    pb_wtypename(pb_gettype(tag), NULL));
        lpb_readtype(L, f->type_id, s);
    }
}

static void lpbD_map(lua_State *L, pb_SliceExt *s, pb_Field *f) {
    pb_SliceExt p;
    int mask = 0, top = lua_gettop(L) + 1;
    uint32_t tag;
    lpb_fetchtable(L, f);
    lpb_readbytes(L, s, &p);
    if (f->type == NULL) return;
    lua_pushnil(L);
    lua_pushnil(L);
    while (pb_readvarint32(&p.base, &tag)) {
        int n = pb_gettag(tag);
        if (n == 1 || n == 2) {
            mask |= n;
            lpbD_field(L, &p, pb_field(f->type, n), tag);
            lua_replace(L, top+n);
        }
    }
    if (mask == 3) lua_rawset(L, -3);
    else           lua_pop(L, 2);
    lua_pop(L, 1);
}

static void lpbD_repeated(lua_State *L, pb_SliceExt *s, pb_Field *f, uint32_t tag) {
    lpb_fetchtable(L, f);
    if (f->packed) {
        int len = lua_rawlen(L, -1);
        pb_SliceExt p;
        lpb_readbytes(L, s, &p);
        while (p.base.p < p.base.end) {
            lpbD_field(L, &p, f, tag);
            lua_rawseti(L, -2, ++len);
        }
    }
    else {
        lpbD_field(L, s, f, tag);
        lua_rawseti(L, -2, lua_rawlen(L, -2) + 1);
    }
    lua_pop(L, 1);
}

static int lpb_decode(lua_State *L, pb_SliceExt *s, pb_Type *t) {
    uint32_t tag;
    lua_newtable(L);
    while (pb_readvarint32(&s->base, &tag)) {
        pb_Field *f = pb_field(t, pb_gettag(tag));
        if (f == NULL)
            pb_skipvalue(&s->base, tag);
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
            (pb_SliceExt*)lua_touserdata(L, 1),
            (pb_Type*)lua_touserdata(L, 2));
}

static int Lpb_decode(lua_State *L) {
    pb_Type *t = lpb_checktype(L, 1);
    pb_SliceExt s;
    if (t == NULL) {
        lua_pushnil(L);
        lua_pushfstring(L, "type '%s' does not exists", lua_tostring(L, 1));
        return 2;
    }
    s = lpb_initext(lpb_checkslice(L, 2));
    lua_pushcfunction(L, lpb_decode_helper);
    lua_pushlightuserdata(L, &s);
    lua_pushlightuserdata(L, t);
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}


/* pb module interface */

static int Lpb_option(lua_State *L) {
    static const char *opts[] = {
        "enum_as_value", "enum_as_name", NULL
    };
    lpb_State *LS = default_lstate(L);
    switch (luaL_checkoption(L, 1, NULL, opts)) {
    case 0: /* enum_as_value */
        LS->enum_as_value = 1;
        break;
    case 1: /* enum_as_name */
        LS->enum_as_value = 0;
        break;
    }
    return 0;
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
        ENTRY(tohex),
        ENTRY(result),
        ENTRY(option),
#undef  ENTRY
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
    return 1;
}

/* cc: flags+='-O3 -ggdb -pedantic -std=c90 -Wall -Wextra --coverage'
 * maccc: flags+='-shared -undefined dynamic_lookup' output='pb.so'
 * win32cc: flags+='-s -mdll -DLUA_BUILD_AS_DLL ' output='pb.dll' libs+='-llua53' */

