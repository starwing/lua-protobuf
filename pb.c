#define PB_STATIC_API
#include "pb.h"

#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>


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


/* protobuf high level decoder/encoder */

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

static int Lpb_clear(lua_State *L) {
    pb_State *S = default_state(L);
    pb_free(S);
    pb_init(S);
    return 0;
}

static int Lpb_load(lua_State *L) {
    pb_State *S = default_state(L);
    pb_Slice s = lpb_checkslice(L, 1);
    lua_pushboolean(L, pb_load(S, &s));
    return 1;
}

static int Lpb_loadfile(lua_State *L) {
    pb_State *S = default_state(L);
    const char *filename = luaL_checkstring(L, 1);
    lua_pushboolean(L, pb_loadfile(S, filename));
    return 1;
}


/* encode protobuf */

static void check_type   (lua_State *L, int type, pb_Field *f);
static void encode_field (lua_State *L, pb_Buffer *b, pb_Field *f);

static lua_Number check_number(lua_State *L, pb_Field *f)
{ check_type(L, LUA_TNUMBER, f); return lua_tonumber(L, -1); }

static lua_Integer check_integer(lua_State *L, pb_Field *f)
{ check_type(L, LUA_TNUMBER, f); return lua_tointeger(L, -1); }

static void check_type(lua_State *L, int type, pb_Field *f) {
    int realtype = lua_type(L, -1);
    if (realtype == type || (type == LUA_TSTRING && realtype == LUA_TUSERDATA
             && testudata(L, -1, PB_SLICE)))
        return;
    lua_pushfstring(L, "%s expected at field '%s', %s got",
            lua_typename(L, type), f->name, lua_typename(L, realtype));
    luaL_argerror(L, 2, lua_tostring(L, -1));
}

static void encode_scalar(lua_State *L, pb_Buffer *b, pb_Field *f) {
    pb_Value v;
    v.tag = f->tag;
    switch (f->type_id) {
    case PB_Tbool:   v.u.boolean = lua_toboolean(L, -1); break;
    case PB_Tdouble: v.u.float64 = (double)check_number(L, f); break;
    case PB_Tfloat:  v.u.float32 = (float)check_number(L, f); break;
    case PB_Tbytes: case PB_Tstring:
        check_type(L, LUA_TSTRING, f);
        v.u.data = lpb_toslice(L, -1);
        break;
    case PB_Tfixed32: case PB_Tint32: case PB_Tsint32: case PB_Tuint32:
        v.u.fixed32 = (uint32_t)check_integer(L, f);
        break;
    case PB_Tfixed64: case PB_Tint64: case PB_Tsint64: case PB_Tuint64:
        v.u.fixed64 = (uint64_t)check_integer(L, f);
        break;
    default:
        lua_pushfstring(L, "unknown type '%s' (%d)",
                f->type->name, f->type_id);
        luaL_argerror(L, 2, lua_tostring(L, -1));
    }
    pb_addvalue(b, &v, f->type_id);
}

static void encode_enum(lua_State *L, pb_Buffer *b, pb_Field *f) {
    int type = lua_type(L, -1);
    if (type == LUA_TNUMBER) {
        lua_Integer v = lua_tointeger(L, -1);
        pb_addkey(b, f->tag, PB_TVARINT);
        pb_addvarint(b, (uint64_t)v);
    }
    else if (type == LUA_TSTRING || type == LUA_TUSERDATA) {
        pb_Slice s;
        pb_Field *ev;
        s = lpb_toslice(L, -1);
        if (!f->type || !s.p) return;
        ev = pb_field(f->type, s);
        if (!ev) return;
        pb_addkey(b, f->tag, PB_TVARINT);
        pb_addvar32(b, (unsigned)ev->u.enum_value);
    }
    else {
        lua_pushfstring(L, "number/string expected at field '%s', %s got",
                f->name, luaL_typename(L, -1));
        luaL_argerror(L, 2, lua_tostring(L, -1));
    }
}

static void encode(lua_State *L, pb_Buffer *b, pb_Type *t) {
    luaL_checkstack(L, 3, "message too many levels");
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            size_t len;
            const char *s = lua_tolstring(L, -2, &len);
            pb_Slice name = pb_lslice(s, len);
            pb_Field *f = pb_field(t, name);
            if (!f) continue;
            encode_field(L, b, f);
        }
        lua_pop(L, 1);
    }
}

static void encode_message(lua_State *L, pb_Buffer *b, pb_Field *f) {
    pb_Buffer nb;
    pb_Slice s;
    check_type(L, LUA_TTABLE, f);
    if (!f->type) return;
    pb_initbuffer(&nb);
    encode(L, &nb, f->type);
    s = pb_result(&nb);
    pb_addkey(b, f->tag, PB_TBYTES);
    pb_addbytes(b, s);
}

static void encode_field(lua_State *L, pb_Buffer *b, pb_Field *f) {
    if (!f->repeated) {
        switch (f->type_id) {
        case PB_Tmessage: encode_message(L, b, f); break;
        case PB_Tenum:    encode_enum(L, b, f); break;
        default:          encode_scalar(L, b, f); break;
        }
    }
    else if (!lua_isnil(L, -1)) {
        int i;
        check_type(L, LUA_TTABLE, f);
#define L_(s) for (i=1;lua53_rawgeti(L,-1,i)!=LUA_TNIL;++i) {s;lua_pop(L,1);}
        switch (f->type_id) {
        case PB_Tmessage: L_(encode_message(L, b, f)); break;
        case PB_Tenum:    L_(encode_enum(L, b, f)); break;
        default:          L_(encode_scalar(L, b, f)); break;
#undef  L_
        }
        lua_pop(L, 1);
    }
}

static int encode_safe(lua_State *L) {
    pb_Buffer *b = (pb_Buffer*)lua_touserdata(L, 1);
    pb_Type *t = (pb_Type*)lua_touserdata(L, 2);
    encode(L, b, t);
    lua_pushlstring(L, b->buff, b->size);
    return 1;
}

static int Lpb_encode(lua_State *L) {
    pb_State *S = default_state(L);
    pb_Slice tname = lpb_checkslice(L, 1);
    pb_Type *t = pb_type(S, tname);
    pb_Buffer b;
    int ret = 1;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!t) {
        lua_pushnil(L);
        lua_pushfstring(L, "can not find type '%s'", tname.p);
        return 2;
    }
    pb_initbuffer(&b);
    lua_pushcfunction(L, encode_safe);
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

static int parse_slice(lua_State *L, pb_Slice *slice, pb_Type *t);

typedef struct Context {
    pb_Parser p;
    lua_State *L;
} Context;

static void on_field(pb_Parser *p, pb_Value *v, pb_Field *f) {
    Context *ctx = (Context*)p;
    lua_State *L = ctx->L;
    if (!f->repeated)
        lua_pushstring(L, f->name);
    else if (lua53_getfield(L, -1, f->name) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, f->name);
    }
    switch (f->type_id) {
    case PB_Tbool:   lua_pushboolean(L, v->u.boolean); break;
    case PB_Tdouble: lua_pushnumber(L, v->u.float64); break;
    case PB_Tfloat:  lua_pushnumber(L, v->u.float32); break;
    case PB_Tuint32: case PB_Tfixed32:
        lua_pushinteger(L, v->u.fixed32);
    case PB_Tint32: case PB_Tsfixed32: case PB_Tsint32:
        lua_pushinteger(L, v->u.sfixed32);
        break;
    case PB_Tstring:
        lua_pushlstring(L, v->u.data.p, pb_slicelen(&v->u.data));
        break;
    case PB_Tmessage:
        if (!parse_slice(L, &v->u.data, f->type))
            lua_pushnil(L);
        break;
    case PB_Tenum: {
            pb_Field *ev = pb_fieldbytag(f->type, (int)v->u.fixed64);
            if (ev != NULL) lua_pushstring(L, ev->name);
            else lua_pushinteger(L, v->u.fixed64);
        }
        break;
    default:
        lua_pushinteger(L, v->u.fixed64);
        break;
    }
    if (!f->repeated)
        lua_rawset(L, -3);
    else {
        lua_rawseti(L, -2, lua_rawlen(L, -2) + 1);
        lua_pop(L, 1);
    }
}

static int parse_slice(lua_State *L, pb_Slice *slice, pb_Type *t) {
    pb_State *S = default_state(L);
    Context ctx;
    luaL_checkstack(L, 3, "proto nest level too big");
    lua_newtable(L);
    ctx.p.S = S;
    ctx.p.type = t;
    ctx.p.on_field = on_field;
    ctx.p.on_mistype = NULL;
    ctx.p.on_unknown = NULL;
    ctx.L = L;
    if (pb_parse(&ctx.p, slice))
        return 1;
    lua_pop(L, 1);
    return 0;
}

static int Lpb_decode(lua_State *L) {
    pb_State *S = default_state(L);
    pb_Slice tname = lpb_checkslice(L, 1);
    pb_Slice data = lpb_checkslice(L, 2);
    pb_Type *t = pb_type(S, tname);
    if (!t) return 0;
    return parse_slice(L, &data, t);
}

LUALIB_API int luaopen_pb(lua_State *L) {
    luaL_Reg libs[] = {
#define ENTRY(name) { #name, Lpb_##name }
        ENTRY(clear),
        ENTRY(load),
        ENTRY(loadfile),
        ENTRY(encode),
        ENTRY(decode),
#undef  ENTRY
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
    return 1;
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
        pb_Slice s = lpb_checkslice(L, arg);
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
    if (buf != NULL) {
        lua_pushfstring(L, "pb.Buffer: %p", buf);
        return 1;
    }
    return 0;
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

static int Lbuf_reset(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    pb_resetbuffer(buf);
    return_self(L);
}

static int Lbuf_len(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    lua_pushinteger(L, (lua_Integer)buf->size);
    return 1;
}

static int Lbuf_key(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    lua_Integer tag = luaL_checkinteger(L, 2);
    int isint, wiretype = (int)lua_tointegerx(L, 3, &isint);
    if (!isint && (wiretype = find_wiretype(luaL_checkstring(L, 3))) < 0)
        return luaL_argerror(L, 3, "invalid wire type name");
    if (tag < 0 || tag > (1<<29))
        luaL_argerror(L, 2, "tag out of range");
    pb_addkey(buf, (uint32_t)tag, wiretype);
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
    for (i = 2; i <= top; ++i)
        pb_addbytes(buf, lpb_checkslice(L, i));
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
    uint32_t tag = (uint32_t)luaL_optinteger(L, 2, 0);
    int type = find_type(luaL_checkstring(L, 3));
    pb_Value v;
    luaL_argcheck(L, 2, tag < 1<<29, "tag out of range");
    v.tag = tag;
    switch (type) {
    case PB_Tbool:   v.u.boolean = lua_toboolean(L, 4); break;
    case PB_Tdouble: v.u.float64 = (double)luaL_checknumber(L, 4); break;
    case PB_Tfloat:  v.u.float32 = (float)luaL_checknumber(L, 4); break;
    case PB_Tbytes:   case PB_Tstring:
    case PB_Tmessage: case PB_Tgroup:
        v.u.data = lpb_checkslice(L, 4);
        break;
    case PB_Tfixed32: case PB_Tint32: case PB_Tuint32: case PB_Tsint32:
        v.u.fixed32 = (uint32_t)luaL_checkinteger(L, 4);
        break;
    case PB_Tenum:
    case PB_Tfixed64: case PB_Tint64: case PB_Tuint64: case PB_Tsint64:
        v.u.fixed64 = (uint64_t)luaL_checkinteger(L, 4);
        break;
    default:
        lua_pushfstring(L, "unknown type '%s'", type);
        return luaL_argerror(L, 3, lua_tostring(L, -1));
    }
    pb_addvalue(buf, &v, type);
    return_self(L);
}

static int Lbuf_clear(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    size_t sz = (size_t)luaL_optinteger(L, 2, buf->size);
    if (sz > buf->size) sz = buf->size;
    buf->size -= sz;
    if (lua_toboolean(L, 3)) {
        lua_pushlstring(L, &buf->buff[buf->size], sz);
        return 1;
    }
    return_self(L);
}

static int Lbuf_concat(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    int i, top = lua_gettop(L);
    for (i = 2; i <= top; ++i)
        pb_addslice(buf, lpb_checkslice(L, i));
    return_self(L);
}

static int Lbuf_result(lua_State *L) {
    pb_Buffer *buf = check_buffer(L, 1);
    const char *s = luaL_optstring(L, 2, NULL);
    if (s == NULL)
        lua_pushlstring(L, buf->buff, buf->size);
    else if (strcmp(s, "hex") == 0) {
        const char *hexa = "0123456789ABCDEF";
        luaL_Buffer b;
        char hex[4] = "XX ";
        size_t i;
        luaL_buffinit(L, &b);
        for (i = 0; i < buf->size; ++i) {
            hex[0] = hexa[(buf->buff[i]>>4)&0xF];
            hex[1] = hexa[(buf->buff[i]   )&0xF];
            if (i == buf->size-1) hex[2] = '\0';
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
        ENTRY(key),
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
    uint64_t v;
    const char *p = dec->p;
    if (!pb_readvarint(dec, &v)) return 0;
    switch (type) {
    case PB_Tint32:
    case PB_Tint64:
        lua_pushinteger(L, (lua_Integer)(int64_t)v);
        return 1;
    case PB_Tuint32:
        lua_pushinteger(L, (uint32_t)v);
        return 1;
    case PB_Tsint32:
        lua_pushinteger(L, pb_decode_sint32((uint32_t)v));
        return 1;
    case PB_Tsint64:
        lua_pushinteger(L, (lua_Integer)pb_decode_sint64(v));
        return 1;
    case 0:
    case PB_Tuint64:
    case PB_Tenum:
        lua_pushinteger(L, (lua_Integer)v);
        return 1;
    case PB_Tbool:
        lua_pushboolean(L, v != 0);
        return 1;
    default:
        dec->p = p;
        return type_mismatch(L, type, "varint");
    }
}

static int pb_pushfixed32(lua_State *L, pb_Slice *dec, int type) {
    uint32_t v;
    const char *p = dec->p;
    if (!pb_readfixed32(dec, &v)) return 0;
    switch (type) {
    case -1:
    case PB_Tfixed32:
        lua_pushinteger(L, (lua_Integer)v);
        return 1;
    case PB_Tfloat:
        lua_pushnumber(L, pb_decode_float(v));
        return 1;
    case PB_Tsfixed32:
        lua_pushinteger(L, (int32_t)v);
        return 1;
    default:
        dec->p = p;
        return type_mismatch(L, type, "fixed32");
    }
}

static int pb_pushfixed64(lua_State *L, pb_Slice *dec, int type) {
    uint64_t v;
    const char *p = dec->p;
    if (!pb_readfixed64(dec, &v)) return 0;
    switch (type) {
    case PB_Tdouble:
        lua_pushnumber(L, pb_decode_double(v));
        return 1;
    case -1:
    case PB_Tfixed64:
        lua_pushinteger(L, (lua_Integer)v);
        return 1;
    case PB_Tsfixed64:
        lua_pushinteger(L, (lua_Integer)(int64_t)v);
        return 1;
    default:
        dec->p = p;
        return type_mismatch(L, type, "fixed64");
    }
}

static int pb_pushscalar(lua_State *L, pb_Slice *dec, int wiretype, int type) {
    uint64_t n;
    const char *p = dec->p;
    switch (wiretype) {
    case PB_TVARINT: return pb_pushvarint(L, dec, type);
    case PB_T64BIT:  return pb_pushfixed64(L, dec, type);
    case PB_T32BIT:  return pb_pushfixed32(L, dec, type);
    case PB_TBYTES:
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
        if (n > pb_slicelen(dec)) return 0;
        lua_pushlstring(L, dec->p, (size_t)n);
        dec->p += n;
        return 1;
    case PB_TGSTART: /* start group */
    case PB_TGEND: /* end group */ /* XXX groups unimplement */
    default:
        dec->p = p;
        return luaL_error(L, "unsupported wire type: %d", wiretype);
    }
}

static void init_decoder(pb_Slice *dec, lua_State *L, int idx) {
    pb_Slice s = lpb_checkslice(L, idx);
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
    if (dec != NULL) {
        lua_pushfstring(L, "pb.Decoder: %p", dec);
        return 1;
    }
    return 0;
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

static int Lslice_key(lua_State *L) {
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
    if (n > pb_slicelen(dec)) {
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
    if (buf->size == dec->p - dec[1].p) {
        dec->p = dec[1].p;
        buf->size = 0;
    }
    dec->p = buf->buff + (dec->p - dec[1].p);
    dec[1].p = buf->buff;
    dec[1].end = buf->buff + buf->size;
    dec->end = buf->buff + buf->size;
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
        ENTRY(key),
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

/* win32cc: flags+='-ggdb -O3 -mdll -DLUA_BUILD_AS_DLL'
 * win32cc: output='pb.dll' libs+='-llua53'
 * maccc: flags+='-ggdb -O0 -bundle -undefined dynamic_lookup'
 * maccc: output='pb.so'
 * xcc: flags+='-ID:\luajit\include' libs+='-LD:\luajit\' */

