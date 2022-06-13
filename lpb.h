#ifndef lpb_h
#define lpb_h

#include "pb.h"

#include <lua.h>
#include <lauxlib.h>

PB_NS_BEGIN


/* export entries */

LUALIB_API int (luaopen_pb_io)(lua_State *L);
LUALIB_API int (luaopen_pb_conv)(lua_State *L);
LUALIB_API int (luaopen_pb_buffer)(lua_State *L);
LUALIB_API int (luaopen_pb_slice)(lua_State *L);
LUALIB_API int (luaopen_pb)(lua_State *L);
LUALIB_API int (luaopen_pb_unsafe)(lua_State *L);

/* C APIs */

typedef struct lpb_State lpb_State;


LUALIB_API int (lpb_newslice)(lua_State *L, const char *s, size_t len);
LUALIB_API pb_Slice (lpb_checkslice)(lua_State *L, int idx);

LUALIB_API lpb_State *(lpb_lstate)(lua_State *L);
#define lpb_state(L) (*(pb_State**)lpb_lstate(L))


LUALIB_API const pb_Type *(lpb_type)(lpb_State *LS, pb_Slice name);

LUALIB_API void (lpb_encode)(lua_State *L, lpb_State *LS, const pb_Type *t, pb_Buffer *b);
LUALIB_API void (lpb_decode)(lua_State *L, lpb_State *LS, const pb_Type *t, pb_Slice *s);



PB_NS_END

#endif /* lpb_h */
