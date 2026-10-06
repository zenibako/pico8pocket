#ifndef P8P_VM_FASTTEXT_H
#define P8P_VM_FASTTEXT_H

#include "of_fastram.h"

/*
 * z8lua is compiled as C++ in the Pocket build.  A section attribute on a
 * prior declaration is inherited by the definition in lvm.cpp, so we can put
 * the upstream VM dispatch loop in openfpgaOS BRAM without patching the pinned
 * dependency itself.
 */
struct lua_State;
struct lua_TValue;
struct Table;
union TString;

/* The inline table fast path is shared by several opcodes; keep one BRAM
 * copy rather than inlining it into each handler of the BRAM-resident
 * dispatch loop. */
#define LUAI_VM_HOT OF_FASTTEXT

OF_FASTTEXT void luaV_execute(struct lua_State *lua);
OF_FASTTEXT int luaD_precall(struct lua_State *lua, struct lua_TValue *function,
                             int result_count);
OF_FASTTEXT void luaD_call(struct lua_State *lua, struct lua_TValue *function,
                           int result_count, int allow_yield);
OF_FASTTEXT int luaD_poscall(struct lua_State *lua,
                             struct lua_TValue *first_result);
OF_FASTTEXT void luaD_callcfast(struct lua_State *lua,
                                struct lua_TValue *function, int result_count);
/* luaV_gettable/luaV_settable are now only the slow path behind the inline
 * table fast path (patches/z8lua-vm-fastpath.patch); their BRAM goes to the
 * larger luaV_execute instead. */
/* luaV_equalobj_ now only handles tables, userdata and long strings: OP_EQ
 * compares numbers, nil, booleans and short strings inline
 * (patches/z8lua-eq-fastpath.patch), so its BRAM went to the __index chain
 * call sites. */
OF_FASTTEXT int luaV_lessthan(struct lua_State *lua,
                              const struct lua_TValue *left,
                              const struct lua_TValue *right);
OF_FASTTEXT const struct lua_TValue *luaV_tonumber(
    const struct lua_TValue *object, struct lua_TValue *number);
OF_FASTTEXT const struct lua_TValue *luaH_get(
    struct Table *table, const struct lua_TValue *key);
OF_FASTTEXT const struct lua_TValue *luaH_getint(struct Table *table, int key);
OF_FASTTEXT const struct lua_TValue *luaH_getstr(
    struct Table *table, union TString *key);
OF_FASTTEXT int luaH_getn(struct Table *table);

#endif
