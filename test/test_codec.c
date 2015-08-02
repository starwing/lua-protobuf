#define PB_STATIC_API
#include "../pb.h"

#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>

#include <stdarg.h>

static pb_State S;

static int Lpb_loadfile(lua_State *L) {
    const char *filename = luaL_checkstring(L, 1);
    pb_loadfile(&S, filename);
    return 0;
}

static pb_Slice check_slice(lua_State *L, int idx) {
    size_t len;
    const char *s = luaL_checklstring(L, idx, &len);
    return pb_lslice(s, len);
}

static pb_Slice to_slice(lua_State *L, int idx) {
    size_t len;
    const char *s = lua_tolstring(L, idx, &len);
    return pb_lslice(s, len);
}


/* encode protobuf */

static int  check_type   (lua_State *L, int type, pb_Field *f);
static void encode_field (lua_State *L, pb_Buffer *b, pb_Field *f);

static lua_Number check_number(lua_State *L, pb_Field *f)
{ check_type(L, LUA_TNUMBER, f); return lua_tonumber(L, -1); }

static lua_Integer check_integer(lua_State *L, pb_Field *f)
{ check_type(L, LUA_TNUMBER, f); return lua_tointeger(L, -1); }

static int check_type(lua_State *L, int type, pb_Field *f) {
    int realtype = lua_type(L, -1);
    if (realtype != type) {
        lua_pushfstring(L, "%s expected at field '%s', %s got",
                lua_typename(L, type), f->name, lua_typename(L, realtype));
        return luaL_argerror(L, 2, lua_tostring(L, -1));
    }
    return 1;
}

static void encode_scalar(lua_State *L, pb_Buffer *b, pb_Field *f) {
    pb_Value v;
    switch (f->type_id) {
    case PB_Tbool:
        v.u.fixed32 = lua_toboolean(L, -1);
        pb_addpair(b, f->tag, PB_TVARINT);
        pb_addchar(b, v.u.fixed32 ? 1 : 0);
        break;
    case PB_Tbytes:
    case PB_Tstring:
        check_type(L, LUA_TSTRING, f);
        v.u.data = to_slice(L, -1);
        pb_addpair(b, f->tag, PB_TDATA);
        pb_adddata(b, &v.u.data);
        break;
    case PB_Tdouble:
        v.u.float64 = (double)check_number(L, f);
        pb_addpair(b, f->tag, PB_T64BIT);
        pb_addfixed64(b, v.u.fixed64);
        break;
    case PB_Tfloat:
        v.u.float32 = (float)check_number(L, f);
        pb_addpair(b, f->tag, PB_T32BIT);
        pb_addfixed32(b, v.u.fixed32);
        break;
    case PB_Tfixed32:
        v.u.fixed32 = (uint32_t)check_integer(L, f);
        pb_addpair(b, f->tag, PB_T32BIT);
        pb_addfixed32(b, v.u.fixed32);
        break;
    case PB_Tfixed64:
        v.u.fixed64 = (uint64_t)check_integer(L, f);
        pb_addpair(b, f->tag, PB_T64BIT);
        pb_addfixed64(b, v.u.fixed64);
        break;
    case PB_Tint32:
    case PB_Tuint32:
        v.u.fixed32 = (uint32_t)check_integer(L, f);
        pb_addpair(b, f->tag, PB_TVARINT);
        pb_addvarint(b, (uint64_t)v.u.fixed32);
        break;
    case PB_Tenum:
    case PB_Tint64:
    case PB_Tuint64:
        v.u.fixed64 = (uint64_t)check_integer(L, f);
        pb_addpair(b, f->tag, PB_TVARINT);
        pb_addvarint(b, v.u.fixed64);
        break;
    case PB_Tsint32:
        v.u.fixed32 = (uint32_t)check_integer(L, f);
        v.u.fixed32 = (v.u.fixed32 << 1) ^ (v.u.fixed32 >> 31);
        pb_addpair(b, f->tag, PB_TVARINT);
        pb_addvarint(b, (uint64_t)v.u.fixed32);
        break;
    case PB_Tsint64:
        v.u.fixed64 = (uint64_t)check_integer(L, f);
        v.u.fixed64 = (v.u.fixed64 << 1) ^ (v.u.fixed64 >> 63);
        pb_addpair(b, f->tag, PB_TVARINT);
        pb_addvarint(b, v.u.fixed64);
        break;
    case PB_Tgroup:
    default:
        lua_pushfstring(L, "unknown type '%s' (%d)",
                f->type->name, f->type_id);
        luaL_argerror(L, 2, lua_tostring(L, -1));
    }
}

static void encode_enum(lua_State *L, pb_Buffer *b, pb_Field *f) {
    int type = lua_type(L, -1);
    if (type == LUA_TNUMBER) {
        lua_Integer v = lua_tointeger(L, -1);
        pb_addpair(b, f->tag, PB_TVARINT);
        pb_addvarint(b, (uint64_t)v);
    }
    else if (type == LUA_TSTRING) {
        pb_Slice s = to_slice(L, -1);
        pb_Field *ev;
        if (!f->type) return;
        ev = pb_field(f->type, &s);
        if (!ev) return;
        pb_addpair(b, f->tag, PB_TVARINT);
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
            pb_Field *f = pb_field(t, &name);
            if (!f) continue;
            encode_field(L, b, f);
        }
        lua_pop(L, 1);
    }
}

static void encode_message(lua_State *L, pb_Buffer *b, pb_Field *f) {
    pb_Buffer nb;
    check_type(L, LUA_TTABLE, f);
    if (!f->type) return;
    pb_initbuffer(&nb);
    encode(L, &nb, f->type);
    {
        pb_Slice s;
        s = pb_result(&nb);
        pb_addpair(b, f->tag, PB_TDATA);
        pb_adddata(b, &s);
    }
}

static void encode_field(lua_State *L, pb_Buffer *b, pb_Field *f) {
    if (!f->repeated) {
        switch (f->type_id) {
        case PB_Tmessage: encode_message(L, b, f); break;
        case PB_Tenum:    encode_enum(L, b, f); break;
        default:          encode_scalar(L, b, f); break;
        }
    }
    else if (lua_type(L, -1) == LUA_TTABLE) {
        int i;
        switch (f->type_id) {
        case PB_Tmessage:
            printf("{{ %d\n", lua_gettop(L));
            for (i = 1; lua_rawgeti(L, -1, i) != LUA_TNIL; ++i) {
                encode_message(L, b, f);
                lua_pop(L, 1);
            }
            printf("}} %d\n", lua_gettop(L));
            break;
        case PB_Tenum:
            for (i = 1; lua_rawgeti(L, -1, i) != LUA_TNIL; ++i) {
                encode_enum(L, b, f);
                lua_pop(L, 1);
            }
            break;
        default:
            for (i = 1; lua_rawgeti(L, -1, i) != LUA_TNIL; ++i) {
                encode_scalar(L, b, f);
                lua_pop(L, 1);
            }
            break;
        }
        lua_pop(L, 1);
    }
}

static int encode_safe(lua_State *L) {
    pb_Buffer *b = (pb_Buffer*)lua_touserdata(L, 1);
    pb_Type *t = (pb_Type*)lua_touserdata(L, 2);
    encode(L, b, t);
    lua_pushlstring(L, b->buff, b->used);
    return 1;
}

static int Lpb_encode(lua_State *L) {
    pb_Slice tname = check_slice(L, 1);
    pb_Type *t = pb_type(&S, &tname);
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
    else if (lua_getfield(L, -1, f->name) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, f->name);
    }
    switch (f->type_id) {
    case PB_Tdouble:
        lua_pushnumber(L, v->u.float64);
        break;
    case PB_Tfloat:
        lua_pushnumber(L, v->u.float32);
        break;
    case PB_Tint32: case PB_Tuint32: case PB_Tfixed32:
    case PB_Tsfixed32: case PB_Tsint32:
        lua_pushinteger(L, v->u.fixed32);
        break;
    case PB_Tbool:
        lua_pushboolean(L, v->u.boolean);
        break;
    case PB_Tstring:
        lua_pushlstring(L, v->u.data.p, pb_slicelen(&v->u.data));
        break;
    case PB_Tmessage:
        if (!parse_slice(L, &v->u.data, f->type))
            lua_pushnil(L);
        break;
    case PB_Tenum:
        {
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
    Context ctx;
    luaL_checkstack(L, 3, "proto nest level too big");
    lua_newtable(L);
    ctx.p.S = &S;
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
    pb_Slice tname = check_slice(L, 1);
    pb_Slice data = check_slice(L, 2);
    pb_Type *t = pb_type(&S, &tname);
    if (!t) return 0;
    return parse_slice(L, &data, t);
}

LUALIB_API int luaopen_pbt(lua_State *L) {
    luaL_Reg libs[] = {
#define ENTRY(name) { #name, Lpb_##name }
        ENTRY(loadfile),
        ENTRY(encode),
        ENTRY(decode),
#undef  ENTRY
        { NULL, NULL }
    };
    pb_init(&S);
    luaL_newlib(L, libs);
    return 1;
}
/* cc: flags+='-s -O3 -mdll -DLUA_BUILD_AS_DLL'
 * cc: libs+='-llua53' output='pbt.dll' */
