#define LUA_LIB
#include "lpb.h"

static int test_slice(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    return lpb_newslice(L, s, len);
}

static int test_encode(lua_State *L) {
    lpb_State *LS = lpb_lstate(L);
    const pb_Type *t = lpb_type(LS, lpb_checkslice(L, 1));
    /* only for test, do not use in real code, will leak memory */
    pb_Buffer b;
    lpb_encode(L, LS, t, &b);
    return lua_pushlstring(L, pb_buffer(&b), pb_bufflen(&b)), 1;
}

static int test_decode(lua_State *L) {
    lpb_State *LS = lpb_lstate(L);
    const pb_Type *t = lpb_type(LS, lpb_checkslice(L, 1));
    pb_Slice s = lpb_checkslice(L, 2);
    /* only for test, do not use in real code, will leak memory */
    lua_newtable(L);
    lpb_decode(L, LS, t, &s);
    lua_pushinteger(L, s.p - s.start);
    return 2;
}

LUALIB_API int luaopen_pbtest(lua_State *L) {
    luaL_Reg libs[] = {
        {"test_slice", test_slice},
        {"test_encode", test_encode},
        {"test_decode", test_decode},
        {NULL, NULL},
    };
    luaL_newlib(L, libs);
    return 0;
}

/* cc: flags+='-O3 -ggdb -pedantic -std=c90 -Wall -Wextra --coverage'
 * maccc: flags+='-ggdb -shared -undefined dynamic_lookup' output='pbtest.so'
 * win32cc: flags+='-s -mdll -DLUA_BUILD_AS_DLL ' output='pbtest.dll' libs+='-L. -lpb -llua54' */

