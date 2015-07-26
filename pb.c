#define PB_IMPLEMENTATION
#include "pb.h"

#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>


/* Lua utils */

#define PB_BUFFER    "pb.Buffer"
#define PB_SLICE     "pb.Slice"

#define check_buffer(L,idx) ((pb_Buffer*)checkudata(L,idx,PB_BUFFER))
#define test_buffer(L,idx)  ((pb_Buffer*)testudata(L,idx,PB_BUFFER))
#define check_slice(L,idx)  ((pb_Slice*)checkudata(L,idx,PB_SLICE))
#define test_slice(L,idx)   ((pb_Slice*)testudata(L,idx,PB_SLICE))
#define return_self(L) { lua_settop(L, 1); return 1; }

#if LUA_VERSION_NUM < 502
#include <assert.h>

# define luaL_newlib(L,l) (lua_newtable(L), luaL_register(L,NULL,l))
# define luaL_setfuncs(L,l,n) (assert(n==0), luaL_register(L,NULL,l))
# define luaL_setmetatable(L, name) \
    (luaL_getmetatable((L), (name)), lua_setmetatable(L, -2))

static int relindex(int idx, int offset) {
    if (idx < 0 && idx > LUA_REGISTRYINDEX)
        return idx + offset;
    return idx;
}

static lua_Integer lua_tointegerx(lua_State *L, int idx, int *isint) {
    lua_Integer i = lua_tointeger(L, idx);
    if (isint) *isint = (i != 0 || lua_type(L, idx) == LUA_TNUMBER);
    return i;
}
#endif

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

static pb_Slice pb_tolbuffer(lua_State *L, int idx) {
    if (lua_type(L, idx) == LUA_TSTRING) {
        size_t len;
        const char *s = luaL_checklstring(L, idx, &len);
        return pb_lslice(s, len);
    }
    return pb_result(check_buffer(L, idx));
}


/* protobuf integer conversion */
/* from: Lua -> protobuf (encode), to: protobuf -> Lua (deocde) */

static int Lconv_encode_int32(lua_State *L) {
    lua_pushinteger(L, pb_expandsig((uint32_t)luaL_checkinteger(L, 1)));
    return 1;
}

static int Lconv_encode_uint32(lua_State *L) {
    lua_pushinteger(L, (uint32_t)luaL_checkinteger(L, 1));
    return 1;
}

static int Lconv_encode_sint32(lua_State *L) {
    lua_pushinteger(L, pb_encode_sint32((uint32_t)luaL_checkinteger(L, 1)));
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
        { "decode_int32", Lconv_encode_uint32 },
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


/* io routines */

#ifdef _WIN32
# include <io.h>
# include <fcntl.h>
#else
# define setmode(a,b)  ((void)0)
#endif

static int io_write(lua_State *L, FILE *f, int arg) {
    int nargs = lua_gettop(L) - arg + 1;
    int status = 1;
    for (; nargs--; arg++) {
        pb_Slice s = pb_tolbuffer(L, arg);
        size_t l = pb_slicelen(&s);
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
        setmode(fileno(stdin), O_BINARY);
    else if ((fp = fopen(fname, "rb")) == NULL)
        return luaL_fileresult(L, 0, fname);
    luaL_buffinit(L, &b);
    do {  /* read file in chunks of LUAL_BUFFERSIZE bytes */
        char *p = luaL_prepbuffer(&b);
        nr = fread(p, sizeof(char), LUAL_BUFFERSIZE, fp);
        luaL_addsize(&b, nr);
    } while (nr == LUAL_BUFFERSIZE);
    if (fp != stdin) fclose(fp);
    else setmode(fileno(stdin), O_TEXT);
    luaL_pushresult(&b);  /* close buffer */
    return 1;
}

static int Lio_write(lua_State *L) {
    int res;
    setmode(fileno(stdout), O_BINARY);
    res = io_write(L, stdout, 1);
    fflush(stdout);
    setmode(fileno(stdout), O_TEXT);
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


/* protobuf types */

static int find_wiretype(const char *s) {
    static const char *wiretypes[] = {
#define X(name,str,num) str,
        PB_WIRETYPES(X)
#undef  X
    };
    int i;
    for (i = 0; i < PB_TWCOUNT; ++i) {
        if (strcmp(s, wiretypes[i]) == 0)
            return i;
    }
    return -1;
}

static int find_type(const char *s) {
    static const char *types[] = {
#define X(name,num) #name,
        PB_TYPES(X)
#undef  X
    };
    int i;
    if (s == NULL) return 0;
    for (i = 0; i < PB_TCOUNT-1; ++i) {
        if (strcmp(s, types[i]) == 0)
            return i+1;
    }
    return 0;
}


/* protobuf encode buffer */

static int Lbuf_tostring(lua_State *L) {
    pb_Buffer *buf = test_buffer(L, 1);
    if (buf != NULL)
        lua_pushfstring(L, "pb.Buffer: %p", buf);
    else
        luaL_tolstring(L, 1, NULL);
    return 1;
}

static int Lbuf_new(lua_State *L) {
    int i, top = lua_gettop(L);
    pb_Buffer *buf = (pb_Buffer*)lua_newuserdata(L, sizeof(pb_Buffer));
    pb_initbuffer(buf);
    luaL_setmetatable(L, PB_BUFFER);
    for (i = 1; i <= top; ++i) {
        pb_Slice s = pb_tolbuffer(L, i);
        pb_addslice(buf, &s);
    }
    return 1;
}

static int Lbuf_reset(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    pb_resetbuffer(buf);
    return_self(L);
}

static int Lbuf_len(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    lua_pushinteger(L, (lua_Integer)buf->used);
    return 1;
}

static int Lbuf_tag(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    lua_Integer tag = luaL_checkinteger(L, 2);
    int isint, wiretype = (int)lua_tointegerx(L, 3, &isint);
    if (!isint && (wiretype = find_wiretype(luaL_checkstring(L, 3)) < 0))
        return luaL_argerror(L, 3, "invalid wire type name");
    if (tag < 0 || tag > (1<<29))
        luaL_argerror(L, 2, "tag too big");
    pb_addpair(buf, (uint32_t)tag, wiretype);
    return_self(L);
}

static int Lbuf_varint(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    int i, top = lua_gettop(L);
    for (i = 2; i <= top; ++i) {
        lua_Integer n = luaL_checkinteger(L, i);
        pb_addvarint(buf, n);
    }
    return_self(L);
}

static int Lbuf_bytes(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    int i, top = lua_gettop(L);
    for (i = 2; i <= top; ++i) {
        pb_Slice s = pb_tolbuffer(L, i);
        pb_adddata(buf, &s);
    }
    return_self(L);
}

static int Lbuf_fixed32(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    int i, top = lua_gettop(L);
    for (i = 2; i <= top; ++i) {
        uint32_t n =  (uint32_t)luaL_checkinteger(L, i);
        pb_addfixed32(buf, n);
    }
    return_self(L);
}

static int Lbuf_fixed64(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    int i, top = lua_gettop(L);
    for (i = 2; i <= top; ++i) {
        uint64_t n =  (uint64_t)luaL_checkinteger(L, i);
        pb_addfixed64(buf, n);
    }
    return_self(L);
}

static int Lbuf_add(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    uint32_t tag = 0, hastag = 0;
    const char *s, *type = luaL_checkstring(L, 3);
    union { float f; uint32_t u32;
            double d; uint64_t u64; } u;
    if (!lua_isnoneornil(L, 2)) {
        tag = (uint32_t)luaL_checkinteger(L, 2);
        hastag = 1;
    }
    printf("%d, %s\n", find_type(type), type);
    switch (find_type(type)) {
    case PB_Tbool:
        u.u32 = lua_toboolean(L, 4);
        if (hastag) pb_addpair(buf, tag, PB_TVARINT);
        pb_prepbuffsize(buf, 1);
        pb_addchar(buf, u.u32 ? 1 : 0);
        break;
    case PB_Tbytes:
    case PB_Tstring:
    case PB_Tmessage:
        s = luaL_checklstring(L, 4, &u.u32);
        if (hastag) pb_addpair(buf, tag, PB_TDATA);
        pb_addvarint(buf, u.u32);
        pb_prepbuffsize(buf, u.u32);
        memcpy(&buf->buff[buf->used], s, u.u32);
        buf->used += u.u32;
        break;
    case PB_Tdouble:
        u.d = (double)luaL_checknumber(L, 4);
        if (hastag) pb_addpair(buf, tag, PB_T64BIT);
        pb_addfixed64(buf, u.u64);
        break;
    case PB_Tfloat:
        u.f = (float)luaL_checknumber(L, 4);
        if (hastag) pb_addpair(buf, tag, PB_T32BIT);
        pb_addfixed32(buf, u.u32);
        break;
    case PB_Tfixed32:
        u.u32 = (uint32_t)luaL_checkinteger(L, 4);
        if (hastag) pb_addpair(buf, tag, PB_T32BIT);
        pb_addfixed32(buf, u.u32);
        break;
    case PB_Tfixed64:
        u.u64 = (uint64_t)luaL_checkinteger(L, 4);
        if (hastag) pb_addpair(buf, tag, PB_T64BIT);
        pb_addfixed64(buf, u.u64);
        break;
    case PB_Tint32:
    case PB_Tuint32:
        u.u32 = (uint32_t)luaL_checkinteger(L, 4);
        if (hastag) pb_addpair(buf, tag, PB_TVARINT);
        pb_addvarint(buf, (uint64_t)u.u32);
        break;
    case PB_Tenum:
    case PB_Tint64:
    case PB_Tuint64:
        u.u64 = (uint64_t)luaL_checkinteger(L, 4);
        if (hastag) pb_addpair(buf, tag, PB_TVARINT);
        pb_addvarint(buf, u.u64);
        break;
    case PB_Tsint32:
        u.u32 = (uint32_t)luaL_checkinteger(L, 4);
        u.u32 = (u.u32 << 1) ^ (u.u32 >> 31);
        if (hastag) pb_addpair(buf, tag, PB_TVARINT);
        pb_addvarint(buf, (uint64_t)u.u32);
        break;
    case PB_Tsint64:
        u.u64 = (uint64_t)luaL_checkinteger(L, 4);
        u.u64 = (u.u64 << 1) ^ (u.u64 >> 63);
        if (hastag) pb_addpair(buf, tag, PB_TVARINT);
        pb_addvarint(buf, u.u64);
        break;
    case PB_Tgroup:
    default:
        lua_pushfstring(L, "unknown type '%s'", type);
        return luaL_argerror(L, 3, lua_tostring(L, -1));
    }
    return_self(L);
}

static int Lbuf_clear(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    size_t sz = (size_t)luaL_optinteger(L, 2, buf->used);
    if (sz > buf->used) sz = buf->used;
    buf->used -= sz;
    if (lua_toboolean(L, 3)) {
        lua_pushlstring(L, &buf->buff[buf->used], sz);
        return 1;
    }
    return_self(L);
}

static int Lbuf_concat(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    int i, top = lua_gettop(L);
    for (i = 2; i <= top; ++i) {
        pb_Slice s = pb_tolbuffer(L, i);
        pb_addslice(buf, &s);
    }
    return_self(L);
}

static int Lbuf_result(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    const char *s = luaL_optstring(L, 2, NULL);
    if (s == NULL)
        lua_pushlstring(L, buf->buff, buf->used);
    else if (strcmp(s, "hex") == 0) {
        const char *hexa = "0123456789ABCDEF";
        luaL_Buffer b;
        char hex[4] = "XX ";
        size_t i;
        luaL_buffinit(L, &b);
        for (i = 0; i < buf->used; ++i) {
            hex[0] = hexa[(buf->buff[i]>>4)&0xF];
            hex[1] = hexa[(buf->buff[i]   )&0xF];
            if (i == buf->used-1) hex[2] = '\0';
            luaL_addstring(&b, hex);
        }
        luaL_pushresult(&b);
    }
    else {
        luaL_argerror(L, 2, "invalid options");
    }
    return 1;
}

LUALIB_API int luaopen_pb_buffer(lua_State *L) {
    luaL_Reg libs[] = {
        { "__gc", Lbuf_reset },
        { "__len", Lbuf_len },
        { "__concat", Lbuf_concat },
        { "__tostring", Lbuf_tostring },
#define ENTRY(name) { #name, Lbuf_##name }
        ENTRY(new),
        ENTRY(reset),
        ENTRY(tag),
        ENTRY(varint),
        ENTRY(bytes),
        ENTRY(fixed32),
        ENTRY(fixed64),
        ENTRY(add),
        ENTRY(clear),
        ENTRY(result),
        ENTRY(concat),
        ENTRY(len),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, PB_BUFFER)) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        lua_pushvalue(L, -1);
    }
    return 1;
}


/* protobuf slice decoder */

static int type_mismatch(lua_State *L, int type, const char *wt) {
    /* assert(type >= 0 && type < PB_TCOUNT); */
    return luaL_error(L, "can not convert from %s to %s",
            wt, pb_typename(type));
}

static int pb_pushvarint(lua_State *L, pb_Slice *dec, int type) {
    uint64_t n;
    lua_Integer out;
    const char *p = dec->p;
    if (!pb_readvarint(dec, &n)) return 0;
    switch (type) {
    case PB_Tint32: out = pb_expandsig((uint32_t)n); break;
    case PB_Tuint32:
        out = (lua_Integer)(n & ~(uint32_t)0);
        break;
    case PB_Tsint32:
        out = pb_decode_sint32((uint32_t)n);
        break;
    case PB_Tsint64:
        out = pb_decode_sint64(n);
        break;
    case 0:
    case PB_Tint64:
    case PB_Tuint64:
    case PB_Tenum:
        out = (lua_Integer)n;
        break;
    case PB_Tbool:
        lua_pushboolean(L, n != (lua_Integer)0);
        return 1;
    default:
        dec->p = p;
        return type_mismatch(L, type, "varint");
    }
    lua_pushinteger(L, out);
    return 1;
}

static int pb_pushfixed32(lua_State *L, pb_Slice *dec, int type) {
    union { uint32_t u32; float f; } u;
    lua_Integer out;
    const char *p = dec->p;
    if (!pb_readfixed32(dec, &u.u32)) return 0;
    switch (type) {
    case -1:
    case PB_Tfixed32:
        out = (lua_Integer)u.u32;
        return 1;
    case PB_Tfloat:
        lua_pushnumber(L, (lua_Number)u.f);
        return 1;
    case PB_Tsfixed32:
        out = (lua_Integer)u.u32;
        if (sizeof(out) > 4)
            out &= ((uint64_t)1 << 32) - 1;
        out = (lua_Integer)((out ^ (1 << 31)) - (1 << 31));
        break;
    default:
        dec->p = p;
        return type_mismatch(L, type, "fixed32");
    }
    lua_pushinteger(L, out);
    return 1;
}

static int pb_pushfixed64(lua_State *L, pb_Slice *dec, int type) {
    union { uint64_t u64; double d; } u;
    lua_Integer out;
    const char *p = dec->p;
    if (!pb_readfixed64(dec, &u.u64)) return 0;
    switch (type) {
    case PB_Tdouble:
        lua_pushnumber(L, (lua_Number)u.d);
        return 1;
    case -1:
    case PB_Tfixed64:
    case PB_Tsfixed64:
        out = (lua_Integer)u.u64;
        return 1;
    default:
        dec->p = p;
        return type_mismatch(L, type, "fixed64");
    }
    lua_pushinteger(L, out);
    return 1;
}

static int pb_pushscalar(lua_State *L, pb_Slice *dec, int wiretype, int type) {
    uint64_t n;
    const char *p = dec->p;
    switch (wiretype) {
    case PB_TVARINT:
        return pb_pushvarint(L, dec, type);
    case PB_T64BIT:
        return pb_pushfixed64(L, dec, type);
    case PB_TDATA:
#if 0 /* not enabled */
        if (type >= 0 && (type != PB_Tbytes
                      ||  type != PB_Tstring
                      ||  type != PB_Tmessage)) {
            restore_decoder(dec);
            return luaL_error(dec->L, "read string with invalid type: %s",
                    pb_types[type]);
        }
#endif
        if (!pb_readvarint(dec, &n)) return 0;
        if (dec->end - dec->p < n) return 0;
        lua_pushlstring(L, dec->p, (size_t)n);
        dec->p += n;
        return 1;
    case PB_T32BIT:
        return pb_pushfixed32(L, dec, type);
    case PB_TGSTART: /* start group */
    case PB_TGEND: /* end group */ /* XXX groups unimplement */
    default:
        dec->p = p;
        return luaL_error(L, "unsupported wire type: %d", wiretype);
    }
}

static void init_decoder(pb_Slice *dec, lua_State *L, int idx) {
    pb_Slice s = pb_tolbuffer(L, idx);
    size_t len = pb_slicelen(&s);
    lua_Integer i = posrelat(luaL_optinteger(L, idx+1, 1), len);
    lua_Integer j = posrelat(luaL_optinteger(L, idx+2, len), len);
    if (i < 1) i = 1;
    if (j > (lua_Integer)len) j = len;
    dec[1] = s;
    dec->p   = s.p + i - 1;
    dec->end = s.p + j;
    lua_pushvalue(L, idx);
    lua_rawsetp(L, LUA_REGISTRYINDEX, dec);
}

static int Lslice_tostring(lua_State *L) {
    pb_Slice *dec = test_slice(L, 1);
    if (dec != NULL)
        lua_pushfstring(L, "pb.Decoder: %p", dec);
    else
        luaL_tolstring(L, 1, NULL);
    return 1;
}

static int Lslice_new(lua_State *L) {
    pb_Slice *dec;
    if (lua_gettop(L) == 0) {
        dec = (pb_Slice*)lua_newuserdata(L, sizeof(pb_Slice)*2);
        memset(dec, 0, sizeof(pb_Slice)*2);
    }
    else {
        lua_settop(L, 3);
        dec = (pb_Slice*)lua_newuserdata(L, sizeof(pb_Slice)*2);
        init_decoder(dec, L, 1);
    }
    luaL_setmetatable(L, PB_SLICE);
    return 1;
}

static int Lslice_reset(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, dec);
    dec->p = dec->end = NULL;
    return 0;
}

static int Lslice_source(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    size_t oi = dec->p - dec[1].p + 1;
    size_t oj = dec->end - dec[1].p;
    int top = lua_gettop(L);
    if (top != 1) lua_settop(L, 3);
    lua_rawgetp(L, LUA_REGISTRYINDEX, dec);
    lua_pushinteger(L, oi);
    lua_pushinteger(L, oj);
    if (top != 1) init_decoder(dec, L, 2);
    return 3;
}

static int Lslice_pos(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    size_t pos = dec->p - dec[1].p + 1;
    lua_pushinteger(L, (lua_Integer)pos);
    if (lua_gettop(L) != 1) {
        lua_Integer npos = posrelat(luaL_optinteger(L, 2, pos),
                dec->end - dec[1].p);
        if (npos < 1) npos = 1;
        dec->p = dec[1].p + npos - 1;
    }
    return 1;
}

static int Lslice_len(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    lua_pushinteger(L, (lua_Integer)(dec->end - dec[1].p));
    lua_pushinteger(L, (lua_Integer)pb_slicelen(dec + 1));
    if (lua_type(L, 2) == LUA_TNUMBER) {
        size_t len = (size_t)lua_tointeger(L, 2);
        dec->end = dec[1].p + len;
        if (dec->end > dec[1].end)
            dec->end = dec[1].end;
    }
    return 2;
}

static int Lslice_finished(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    lua_pushboolean(L, dec->p >= dec->end);
    return 1;
}

static int Lslice_tag(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    uint64_t n = 0;
    int wiretype;
    if (!pb_readvarint(dec, &n)) return 0;
    wiretype = (int)(n & 0x7);
    lua_pushinteger(L, (lua_Integer)(n >> 3));
    lua_pushinteger(L, (lua_Integer)wiretype);
    if (wiretype >= 0 && wiretype < PB_TWCOUNT) {
        lua_pushstring(L, pb_wirename(wiretype));
        return 3;
    }
    return 2;
}

static int Lslice_varint(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    uint64_t n = 0;
    if (!pb_readvarint(dec, &n)) return 0;
    lua_pushinteger(L, (lua_Integer)n);
    return 1;
}

static int Lslice_fixed32(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    uint32_t n = 0;
    if (!pb_readfixed32(dec, &n)) return 0;
    lua_pushinteger(L, (lua_Integer)n);
    return 1;
}

static int Lslice_fixed64(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    uint64_t n = 0;
    if (!pb_readfixed64(dec, &n)) return 0;
    lua_pushinteger(L, (lua_Integer)n);
    return 1;
}

static int Lslice_bytes(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    const char *p = dec->p;
    uint64_t n = (uint64_t)luaL_optinteger(L, 2, 0);
    if (n == 0 && !pb_readvarint(dec, &n))
        return 0;
    if (dec->end - dec->p < n) {
        dec->p = p;
        return 0;
    }
    lua_pushlstring(L, dec->p, (size_t)n);
    dec->p += n;
    return 1;
}

static int get_wiretype(lua_State *L, pb_Slice *dec, int idx, int *wiretype) {
    uint64_t n;
    switch (lua_type(L, idx)) {
    case LUA_TNIL: case LUA_TNONE:
        if (!pb_readvarint(dec, &n)) return -1;
        lua_pushinteger(L, (lua_Integer)(n >> 3));
        *wiretype = n & 0x7;
        return 1;
    case LUA_TNUMBER:
        *wiretype = (int)lua_tointeger(L, idx);
        return 0;
    case LUA_TSTRING:
        if ((*wiretype = find_wiretype(lua_tostring(L, idx))) < 0)
            luaL_argerror(L, idx, "invalid wire type name");
        return 0;
    default:
        *wiretype = -1;
        return typeerror(L, idx, "nil/number/string");
    }
}

static int Lslice_fetch(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    const char *p = dec->p;
    int type = find_type(luaL_optstring(L, 3, NULL));
    int wiretype, extra = get_wiretype(L, dec, 2, &wiretype);
    if (extra >= 0 && pb_pushscalar(L, dec, wiretype, type))
        return extra + 1;
    dec->p = p;
    return 0;
}

static int Lslice_skip(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    const char *p = dec->p;
    int wiretype, extra = get_wiretype(L, dec, 2, &wiretype);
    if (extra >= 0 && pb_skipvalue(dec, wiretype)) {
        lua_pushinteger(L, wiretype);
        return extra + 1;
    }
    dec->p = p;
    return 0;
}

static int values_iter(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    const char *p = dec->p;
    uint64_t n;
    if (dec->p >= dec->end)
        return 0;
    if (!pb_readvarint(dec, &n))
        return luaL_error(L, "incomplete proto messages");
    lua_pushinteger(L, (lua_Integer)(n >> 3));
    if (pb_pushscalar(L, dec, pb_gettype(n), -1))
        return 2;
    dec->p = p;
    return 0;
}

static int Lslice_values(lua_State *L) {
    check_slice(L, 1);
    lua_pushcfunction(L, values_iter);
    lua_pushvalue(L, 1);
    return 2;
}

static int Lslice_update(lua_State *L) {
    pb_Slice *dec = check_slice(L, 1);
    pb_Buffer *buf;
    lua_rawgetp(L, LUA_REGISTRYINDEX, dec);
    if ((buf = test_buffer(L, -1)) == NULL)
        return 0;
    if (buf->used == dec->p - dec[1].p) {
        dec->p = dec[1].p;
        buf->used = 0;
    }
    dec->p = buf->buff + (dec->p - dec[1].p);
    dec[1].p = buf->buff;
    dec[1].end = buf->buff + buf->used;
    dec->end = buf->buff + buf->used;
    return_self(L);
}

LUALIB_API int luaopen_pb_slice(lua_State *L) {
    luaL_Reg libs[] = {
        { "__gc", Lslice_reset },
        { "__len", Lslice_len },
        { "__tostring", Lslice_tostring },
#define ENTRY(name) { #name, Lslice_##name }
        ENTRY(new),
        ENTRY(reset),
        ENTRY(source),
        ENTRY(pos),
        ENTRY(len),
        ENTRY(tag),
        ENTRY(bytes),
        ENTRY(fixed32),
        ENTRY(fixed64),
        ENTRY(varint),
        ENTRY(fetch),
        ENTRY(skip),
        ENTRY(values),
        ENTRY(finished),
        ENTRY(update),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, PB_SLICE)) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        lua_pushvalue(L, -1);
    }
    return 1;
}

/* cc: flags+='-ggdb -O3 -mdll -DLUA_BUILD_AS_DLL'
 * xcc: flags+='-ID:\luajit\include' libs+='-LD:\luajit\'
 * cc: output='pb.dll' libs+='-llua53' */

