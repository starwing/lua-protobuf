#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>


#include <stdint.h>
#include <string.h>


#define LPB_BUFTYPE  "lpb.Buffer"
#define LPB_DECODER  "lpb.Decoder"

#define return_self(L) { lua_settop(L, 1); return 1; }


/* protobuf types */

/* wire type */
#define LPB_WIRETYPES(X)      \
    X(VARINT,  "varint"     ) \
    X(64BIT,   "64bit"      ) \
    X(LENGTH,  "bytes"      ) \
    X(GSTART,  "startgroup" ) \
    X(GEND,    "endgroup"   ) \
    X(32BIT,   "32bit"      ) \

/* real types, must by ordered */
#define LPB_TYPES(X) \
    X(bool)          \
    X(bytes)         \
    X(double)        \
    X(fixed32)       \
    X(fixed64)       \
    X(float)         \
    X(int32)         \
    X(int64)         \
    X(sfixed32)      \
    X(sfixed64)      \
    X(sint32)        \
    X(sint64)        \
    X(string)        \
    X(uint32)        \
    X(uint64)        \

typedef enum lpb_WireType {
#define X(t, name) LPB_T##t,
    LPB_WIRETYPES(X)
#undef  X
    LPB_TWCOUNT
} lpb_WireType;

typedef enum lpb_Type {
#define X(t) LPB_T##t,
    LPB_TYPES(X)
#undef  X
    LPB_TCOUNT
} lpb_Type;

static const char *pb_wiretypes[] = {
#define X(t, name) name,
    LPB_WIRETYPES(X)
#undef  X
};

static const char *pb_types[] = {
#define X(name) #name,
    LPB_TYPES(X)
#undef  X
};

static int find_type(const char *s) {
    size_t start = 0, end = LPB_TCOUNT-1;
    while (start <= end) {
        size_t mid = (start + end) >> 1;
        int res = strcmp(s, pb_types[mid]);
        if (res == 0)
            return mid;
        else if (res > 0)
            start = mid + 1;
        else
            end = mid - 1;
    }
    return -1;
}

static int find_wiretype(const char *s) {
    int i;
    for (i = 0; i < LPB_TWCOUNT; ++i) {
        if (strcmp(s, pb_wiretypes[i]) == 0)
            return i;
    }
    return -1;
}


/* protobuf integer conversion */

static int Lconv_fromint32(lua_State *L) {
    uint64_t n = (uint64_t)luaL_checkinteger(L, 1);
    n &= ((uint64_t)1 << 32) - 1;
    lua_pushinteger(L, (lua_Integer)((n ^ (1 << 31)) - (1 << 31)));
    return 1;
}

static int Lconv_touint32(lua_State *L) {
    uint32_t n = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (lua_Integer)n);
    return 1;
}

static int Lconv_fromsint32(lua_State *L) {
    uint32_t n = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (n >> 1) ^ -(int32_t)(n & 1));
    return 1;
}

static int Lconv_fromsint64(lua_State *L) {
    uint64_t n = (uint64_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (n >> 1) ^ -(int64_t)(n & 1));
    return 1;
}

static int Lconv_tosint32(lua_State *L) {
    uint32_t n = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (n << 1) ^ (n >> 31));
    return 1;
}

static int Lconv_tosint64(lua_State *L) {
    uint64_t n = (uint64_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (n << 1) ^ (n >> 63));
    return 1;
}

static int Lconv_tofloat(lua_State *L) {
    union { uint32_t u32; float f; } u;
    u.u32 = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushnumber(L, (lua_Number)u.f);
    return 1;
}

static int Lconv_todouble(lua_State *L) {
    union { uint64_t u64; double d; } u;
    u.u64 = (uint64_t)luaL_checkinteger(L, 1);
    lua_pushnumber(L, (lua_Number)u.d);
    return 1;
}

LUALIB_API int luaopen_pb_conv(lua_State *L) {
    luaL_Reg libs[] = {
        { "toint32", Lconv_touint32 },
#define ENTRY(name) { #name, Lconv_##name }
        ENTRY(fromint32),
        ENTRY(touint32),
        ENTRY(fromsint32),
        ENTRY(fromsint64),
        ENTRY(tosint32),
        ENTRY(tosint64),
        ENTRY(tofloat),
        ENTRY(todouble),
#undef  ENTRY
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
    return 1;
}


/* protobuf encode buffer */

typedef struct pb_Buffer {
    size_t used;
    size_t size;
    lua_State *L;
    char *buff;
    char init_buff[LUAL_BUFFERSIZE];
} pb_Buffer;

#define pb_addchar(buff, ch) ((buff)->buff[(buff)->used++] = (ch))

static void pb_initbuffer(pb_Buffer *buff, lua_State *L) {
    buff->used = 0;
    buff->size = LUAL_BUFFERSIZE;
    buff->L = L;
    buff->buff = buff->init_buff;
}

static void pb_resetbuffer(pb_Buffer *buff) {
    if (buff->buff != buff->init_buff) {
        lua_pushnil(buff->L);
        lua_rawsetp(buff->L, LUA_REGISTRYINDEX, buff);
    }
    pb_initbuffer(buff, buff->L);
}

static void pb_prepbuffer(pb_Buffer *buff, size_t need) {
    need += buff->used;
    if (need < buff->size) {
        void *newud;
        size_t newsize = LUAL_BUFFERSIZE;
        while (newsize < need)
            newsize *= 2;
        newud = lua_newuserdata(buff->L, newsize);
        lua_rawsetp(buff->L, LUA_REGISTRYINDEX, buff);
        buff->buff = newud;
        buff->size = newsize;
    }
}

static void pb_pushvarint(pb_Buffer *buff, uint64_t n) {
    pb_prepbuffer(buff, 10);
    while (n != 0) {
        int cur = n & 0x7F;
        n >>= 7;
        pb_addchar(buff, n != 0 ? cur | 0x80 : cur);
    }
}

static void pb_pushbytes(pb_Buffer *buff, const char *s, size_t len) {
    pb_pushvarint(buff, len);
    pb_prepbuffer(buff, len);
    memcpy(&buff->buff[buff->used], s, len);
}

static void pb_pushfix32(pb_Buffer *buff, uint32_t n) {
    int i;
    pb_prepbuffer(buff, 4);
    for (i = 0; i < 4; ++i) {
        pb_addchar(buff, n & 0xFF);
        n >>= 8;
    }
}

static void pb_pushfix64(pb_Buffer *buff, uint64_t n) {
    int i;
    pb_prepbuffer(buff, 8);
    for (i = 0; i < 8; ++i) {
        pb_addchar(buff, n & 0xFF);
        n >>= 8;
    }
}

static int Lbuf_new(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)lua_newuserdata(L, sizeof(pb_Buffer));
    pb_initbuffer(buff, L);
    luaL_setmetatable(L, LPB_BUFTYPE);
    return 1;
}

static int Lbuf_reset(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    pb_resetbuffer(buff);
    return_self(L);
}

static int Lbuf_len(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    lua_pushinteger(L, (lua_Integer)buff->used);
    return 1;
}

static int Lbuf_tag(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    int tag = (int)luaL_checkinteger(L, 2);
    int isint, wiretype = (int)lua_tointegerx(L, 3, &isint);
    if (!isint && (wiretype = find_wiretype(luaL_checkstring(L, 3)) < 0))
        return luaL_argerror(L, 3, "invalid wire type name");
    pb_pushvarint(buff, (wiretype & 0x7) | (tag << 3));
    return_self(L);
}

static int Lbuf_varint(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    lua_Integer n = luaL_checkinteger(L, 2);
    pb_pushvarint(buff, n);
    return_self(L);
}

static int Lbuf_bytes(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    size_t len;
    const char *s = luaL_checklstring(L, 2, &len);
    pb_pushbytes(buff, s, len);
    return_self(L);
}

static int Lbuf_fix32(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    uint32_t n =  (uint32_t)luaL_checkinteger(L, 2);
    pb_pushfix32(buff, n);
    return_self(L);
}

static int Lbuf_fix64(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    uint64_t n =  (uint64_t)luaL_checkinteger(L, 2);
    pb_pushfix64(buff, n);
    return_self(L);
}

static int Lbuf_float(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    union { float f; uint32_t u32; } u;
    u.f = (float)luaL_checknumber(L, 2);
    pb_pushfix32(buff, u.u32);
    return_self(L);
}

static int Lbuf_double(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    union { double d; uint64_t u64; } u;
    u.d = (double)luaL_checknumber(L, 2);
    pb_pushfix64(buff, u.u64);
    return_self(L);
}

static int Lbuf_add(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    uint32_t tag = (uint32_t)luaL_checkinteger(L, 2);
    const char *s, *type = luaL_checkstring(L, 3);
    union { float f; uint32_t u32;
            double d; uint64_t u64; } u;
    switch (find_type(type)) {
    case LPB_Tbool:
        u.u32 = lua_toboolean(L, 4);
        pb_pushvarint(buff, tag << 3 | LPB_TVARINT);
        pb_prepbuffer(buff, 1);
        pb_addchar(buff, u.u32 ? 1 : 0);
        break;
    case LPB_Tbytes:
    case LPB_Tstring:
        s = luaL_checklstring(L, 4, &u.u32);
        pb_pushvarint(buff, tag << 3 | LPB_TLENGTH);
        pb_pushbytes(buff, s, u.u32);
        break;
    case LPB_Tdouble:
        u.d = (double)luaL_checknumber(L, 4);
        pb_pushvarint(buff, tag << 3 | LPB_T64BIT);
        pb_pushfix64(buff, u.u64);
        break;
    case LPB_Tfloat:
        u.f = (float)luaL_checknumber(L, 4);
        pb_pushvarint(buff, tag << 3 | LPB_T32BIT);
        pb_pushfix32(buff, u.u32);
        break;
    case LPB_Tfixed32:
        u.u32 = (uint32_t)luaL_checkinteger(L, 4);
        pb_pushvarint(buff, tag << 3 | LPB_T32BIT);
        pb_pushfix32(buff, u.u32);
        break;
    case LPB_Tfixed64:
        u.u64 = (uint64_t)luaL_checkinteger(L, 4);
        pb_pushvarint(buff, tag << 3 | LPB_T64BIT);
        pb_pushfix64(buff, u.u64);
        break;
    case LPB_Tint32:
    case LPB_Tuint32:
        u.u32 = (uint32_t)luaL_checkinteger(L, 4);
        pb_pushvarint(buff, tag << 3 | LPB_TVARINT);
        pb_pushvarint(buff, (uint64_t)u.u32);
        break;
    case LPB_Tint64:
    case LPB_Tuint64:
        u.u64 = (uint64_t)luaL_checkinteger(L, 4);
        pb_pushvarint(buff, tag << 3 | LPB_TVARINT);
        pb_pushvarint(buff, u.u64);
        break;
    case LPB_Tsint32:
        u.u32 = (uint32_t)luaL_checkinteger(L, 4);
        u.u32 = (u.u32 << 1) ^ (u.u32 >> 31);
        pb_pushvarint(buff, tag << 3 | LPB_TVARINT);
        pb_pushvarint(buff, (uint64_t)u.u32);
        break;
    case LPB_Tsint64:
        u.u64 = (uint64_t)luaL_checkinteger(L, 4);
        u.u64 = (u.u64 << 1) ^ (u.u64 >> 63);
        pb_pushvarint(buff, tag << 3 | LPB_TVARINT);
        pb_pushvarint(buff, u.u64);
        break;
    default:
        lua_pushfstring(L, "unknown type '%s'", type);
        return luaL_argerror(L, 3, lua_tostring(L, -1));
    }
    return_self(L);
}

static int Lbuf_concat(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    pb_Buffer *other;
    size_t len;
    const char *s;
    switch (lua_type(L, 2)) {
    case LUA_TSTRING:
        s = lua_tolstring(L, 2, &len);
        break;
    case LUA_TUSERDATA:
        other = (pb_Buffer*)luaL_checkudata(L, 2, LPB_BUFTYPE);
        s = other->buff;
        len = other->used;
        break;
    default:
        lua_pushfstring(L, "string/buffer expected, got %s",
                luaL_typename(L, 2));
        return luaL_argerror(L, 2, lua_tostring(L, -1));
    }
    pb_prepbuffer(buff, len);
    memcpy(&buff->buff[buff->used], s, len);
    buff->used += len;
    return_self(L);
}

static int Lbuf_result(lua_State *L) {
    pb_Buffer *buff = (pb_Buffer*)luaL_checkudata(L, 1, LPB_BUFTYPE);
    lua_pushlstring(L, buff->buff, buff->used);
    pb_resetbuffer(buff);
    return 1;
}

LUALIB_API int luaopen_pb_buffer(lua_State *L) {
    luaL_Reg libs[] = {
        { "__gc", Lbuf_reset },
        { "__len", Lbuf_len },
        { "__concat", Lbuf_concat },
#define ENTRY(name) { #name, Lbuf_##name }
        ENTRY(new),
        ENTRY(reset),
        ENTRY(tag),
        ENTRY(varint),
        ENTRY(bytes),
        ENTRY(fix32),
        ENTRY(fix64),
        ENTRY(float),
        ENTRY(double),
        ENTRY(add),
        ENTRY(result),
        ENTRY(concat),
        ENTRY(len),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, LPB_BUFTYPE)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        luaL_setfuncs(L, libs, 0);
    }
    return 1;
}


/* protobuf decoder */

typedef struct pb_Decoder {
    size_t len;
    const char *s;
    const char *p, *start, *end;
} pb_Decoder;

static int pb_readvarint(pb_Decoder *dec, uint64_t *pv) {
    uint64_t n = 0;
    const char *p = dec->p, *end;
    while (p < dec->end && (*p & 0x80) != 0)
        ++p;
    if (p >= dec->end)
        return 0;
    end = p + 1;
    while (p >= dec->p) {
        n <<= 7;
        n |= *p-- & 0x7F;
    }
    dec->p = end;
    *pv = n;
    return 1;
}

static int pb_skipvarint(pb_Decoder *dec) {
    const char *p = dec->p;
    while (p < dec->end && (*p & 0x80) != 0)
        ++p;
    if (p >= dec->end)
        return 0;
    dec->p = p + 1;
    return 1;
}

static int pb_skipsize(pb_Decoder *dec, size_t len) {
    if (dec->p + len > dec->end)
        return 0;
    dec->p += len;
    return 1;
}

static int pb_readfix32(pb_Decoder *dec, uint32_t *pv) {
    int i;
    uint32_t n = 0;
    if (dec->p + 4 > dec->end)
        return 0;
    for (i = 3; i >= 0; --i) {
        n <<= 8;
        n |= dec->p[i] & 0xFF;
    }
    *pv = n;
    return 1;
}

static int pb_readfix64(pb_Decoder *dec, uint64_t *pv) {
    int i;
    uint64_t n = 0;
    if (dec->p + 8 < dec->end)
        return 0;
    for (i = 7; i >= 0; --i) {
        n <<= 8;
        n |= dec->p[i] & 0xFF;
    }
    *pv = n;
    return 1;
}

static lua_Integer posrelat(lua_Integer pos, size_t len) {
    if (pos >= 0) return pos;
    else if (0u - (size_t)pos > len) return 0;
    else return (lua_Integer)len + pos;
}

static int rangerelat(lua_Integer *i, lua_Integer *j, size_t len) {
    lua_Integer ni = posrelat(*i, len);
    lua_Integer nj = posrelat(*j, len);
    if (ni < 1) ni = 1;
    if (nj > (lua_Integer)len) nj = len;
    *i = ni, *j = nj;
    return ni < nj;
}

static void init_decoder(pb_Decoder *dec, lua_State *L, int idx) {
    size_t len;
    const char *s = luaL_checklstring(L, idx, &len);
    lua_Integer i = luaL_optinteger(L, idx+1, 1);
    lua_Integer j = luaL_optinteger(L, idx+2, len);
    rangerelat(&i, &j, len);
    dec->s = s;
    dec->len = len;
    dec->p = s + i - 1;
    dec->start = s;
    dec->end = dec->p + j;
    lua_pushvalue(L, idx);
    lua_rawsetp(L, LUA_REGISTRYINDEX, dec);
}

static int Ldec_new(lua_State *L) {
    pb_Decoder *dec;
    lua_settop(L, 3);
    dec = (pb_Decoder*)lua_newuserdata(L, sizeof(pb_Decoder));
    init_decoder(dec, L, 1);
    luaL_setmetatable(L, LPB_DECODER);
    return 1;
}

static int Ldec_reset(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, dec);
    dec->s = NULL;
    dec->len = 0;
    dec->p = dec->start = dec->end = NULL;
    return 0;
}

static int Ldec_source(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    size_t oi = dec->p - dec->start + 1;
    size_t oj = dec->end - dec->start;
    lua_rawgetp(L, LUA_REGISTRYINDEX, dec);
    lua_pushinteger(L, oi);
    lua_pushinteger(L, oj);
    if (lua_gettop(L) != 1)
        init_decoder(dec, L, 2);
    return 3;
}

static int Ldec_pos(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    size_t pos = dec->p - dec->start + 1;
    lua_pushinteger(L, (lua_Integer)pos);
    if (lua_gettop(L) != 1) {
        lua_Integer npos = posrelat(luaL_optinteger(L, 2, pos),
                dec->end - dec->start);
        if (npos < 1) npos = 1;
        dec->p = dec->start + npos - 1;
    }
    return 1;
}

static int Ldec_len(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    size_t len = dec->end - dec->start;
    lua_pushinteger(L, (lua_Integer)len);
    if (lua_gettop(L) != 1) {
        lua_Integer len;
        len = luaL_optinteger(L, 2, dec->len);
        if (len > dec->len) len = dec->len;
        dec->end = dec->start + len;
    }
    return 1;
}

static int Ldec_rawlen(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    lua_pushinteger(L, (lua_Integer)dec->len);
    return 1;
}

static int Ldec_finished(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    lua_pushboolean(L, dec->p >= dec->end);
    return 1;
}

static int Ldec_tag(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    uint64_t n = 0;
    if (!pb_readvarint(dec, &n)) return 0;
    lua_pushinteger(L, (lua_Integer)(n >> 3));
    lua_pushinteger(L, (lua_Integer)(n & 0x7));
    lua_pushstring(L, pb_wiretypes[n & 0x7]);
    return 3;
}

static int Ldec_varint(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    uint64_t n = 0;
    if (!pb_readvarint(dec, &n)) return 0;
    lua_pushinteger(L, (lua_Integer)n);
    return 1;
}

static int Ldec_fix32(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    uint32_t n = 0;
    if (!pb_readfix32(dec, &n)) return 0;
    lua_pushinteger(L, (lua_Integer)n);
    return 1;
}

static int Ldec_fix64(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    uint64_t n = 0;
    if (!pb_readfix64(dec, &n)) return 0;
    lua_pushinteger(L, (lua_Integer)n);
    return 1;
}

static int Ldec_float(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    union { float f; uint32_t u32; } u;
    if (!pb_readfix32(dec, &u.u32)) return 0;
    lua_pushnumber(L, (lua_Number)u.f);
    return 1;
}

static int Ldec_double(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    union { double d; uint64_t u64; } u;
    if (!pb_readfix64(dec, &u.u64)) return 0;
    lua_pushnumber(L, (lua_Number)u.d);
    return 1;
}

static int Ldec_bytes(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
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

static int get_wiretype(pb_Decoder *dec, lua_State *L, int idx, int *wiretype) {
    uint64_t n;
    switch (lua_type(L, idx)) {
    case LUA_TNIL:
    case LUA_TNONE:
        if (!pb_readvarint(dec, &n)) return -1;
        lua_pushinteger(L, (lua_Integer)(n >> 3));
        *wiretype = n & 0x7;
        return 1;
    case LUA_TNUMBER:
        *wiretype = (int)lua_tointeger(L, idx);
        return 0;
    case LUA_TSTRING:
        *wiretype = find_wiretype(lua_tostring(L, idx));
        if (*wiretype < 0) luaL_argerror(L, idx, "invalid wire type name");
        return 0;
    default:
        lua_pushfstring(L, "nil/number/string expected, got %s",
                luaL_typename(L, idx));
        luaL_argerror(L, idx, lua_tostring(L, -1));
        return -1;
    }
}

static int fetch_with_type(lua_State *L, pb_Decoder *dec, int wiretype) {
    uint32_t u32;
    uint64_t u64;
    switch (wiretype) {
    case LPB_TVARINT:
        if (!pb_readvarint(dec, &u64)) return 0;
        lua_pushinteger(L, (lua_Integer)u64);
        return 1;
    case LPB_T64BIT:
        if (!pb_readfix64(dec, &u64)) return 0;
        lua_pushinteger(L, (lua_Integer)u64);
        return 1;
    case LPB_TLENGTH:
        if (!pb_readvarint(dec, &u64)) return 0;
        if (dec->end - dec->p < u64) return 0;
        lua_pushlstring(L, dec->p, (size_t)u64);
        dec->p += u64;
        return 1;
    case LPB_T32BIT:
        if (!pb_readfix32(dec, &u32)) return 0;
        lua_pushinteger(L, (lua_Integer)u32);
        return 1;
    case LPB_TGSTART: /* start group */
        lua_pushstring(L, pb_wiretypes[LPB_TGSTART]);
        return 1;
    case LPB_TGEND: /* end group */
        lua_pushstring(L, pb_wiretypes[LPB_TGEND]);
        return 1;
    default:
        return -1; /* unknown type */
    }
}

static int Ldec_fetch(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    const char *p = dec->p;
    int res, wiretype, extra = get_wiretype(dec, L, 2, &wiretype);
    if (extra < 0) return 0;
    if ((res = fetch_with_type(L, dec, wiretype)) > 0)
        return res + extra;
    dec->p = p;
    if (res == 0) return 0;
    return luaL_error(L, "unsupported wire type: %d", wiretype);
}

static int Ldec_skip(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    const char *p = dec->p;
    int wiretype, extra = get_wiretype(dec, L, 2, &wiretype);
    uint64_t n;
    if (extra < 0) return 0;
    if (extra == 0) lua_pushboolean(L, 1);
    switch (wiretype) {
    case LPB_TVARINT:
        if (pb_skipvarint(dec)) return 1;
        break;
    case LPB_T64BIT:
        if (pb_skipsize(dec, 8)) return 1;
        break;
    case LPB_TLENGTH:
        if (pb_readvarint(dec, &n) && pb_skipsize(dec, (size_t)n))
            return 1;
        break;
    case LPB_T32BIT:
        if (pb_skipsize(dec, 4)) return 1;
        break;
    default:
    case LPB_TGSTART: /* start group */
    case LPB_TGEND: /* end group */
        if (pb_skipsize(dec, 1)) return 1;
    }
    dec->p = p;
    return 0;
}

static int values_iter(lua_State *L) {
    pb_Decoder *dec = (pb_Decoder*)luaL_checkudata(L, 1, LPB_DECODER);
    const char *p = dec->p;
    uint64_t n;
    int res;
    if (dec->p >= dec->end)
        return 0;
    if (!pb_readvarint(dec, &n))
        return luaL_error(L, "incomplete proto messages");
    lua_pushinteger(L, (lua_Integer)(n >> 3));
    if ((res = fetch_with_type(L, dec, n & 0x7)) > 0)
        return res + 1;
    dec->p = p;
    if (res == 0) return 0;
    return luaL_error(L, "unsupported wire type: %d", n & 0x7);
}

static int Ldec_values(lua_State *L) {
    luaL_checkudata(L, 1, LPB_DECODER);
    lua_pushcfunction(L, values_iter);
    lua_pushvalue(L, 1);
    return 2;
}

LUALIB_API int luaopen_pb_decoder(lua_State *L) {
    luaL_Reg libs[] = {
        { "__gc", Ldec_reset },
        { "__len", Ldec_len },
#define ENTRY(name) { #name, Ldec_##name }
        ENTRY(new),
        ENTRY(reset),
        ENTRY(source),
        ENTRY(pos),
        ENTRY(len),
        ENTRY(rawlen),
        ENTRY(tag),
        ENTRY(varint),
        ENTRY(bytes),
        ENTRY(fix32),
        ENTRY(fix64),
        ENTRY(float),
        ENTRY(double),
        ENTRY(fetch),
        ENTRY(skip),
        ENTRY(values),
        ENTRY(finished),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, LPB_DECODER)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        luaL_setfuncs(L, libs, 0);
    }
    return 1;
}

/* cc: flags+='-mdll -s -O3 -DLUA_BUILD_AS_DLL'
 * cc: output='pb.dll' libs+='-llua53' */

