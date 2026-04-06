/*
 * Integration test: build Lua using strata's graph API.
 * Expects lua source at /tmp/strata_test_lua/
 * Builds liblua.a (static lib from ~33 source files) + lua interpreter.
 */
#include "buildsysdep/neobuild.h"
#include <stdio.h>

int main(void)
{
    neo_set_verbosity(NEO_NORMAL);
    neo_set_build_dir("/tmp/strata_build_lua/");
    neo_set_jobs(0);

    neo_graph_t *g = neo_graph_create();
    neo_graph_enable_content_hash(g);

    /* all lua library sources (everything except lua.c which has main()) */
    const char *lib_srcs[] = {
        "/tmp/strata_test_lua/lapi.c",
        "/tmp/strata_test_lua/lauxlib.c",
        "/tmp/strata_test_lua/lbaselib.c",
        "/tmp/strata_test_lua/lcode.c",
        "/tmp/strata_test_lua/lcorolib.c",
        "/tmp/strata_test_lua/lctype.c",
        "/tmp/strata_test_lua/ldblib.c",
        "/tmp/strata_test_lua/ldebug.c",
        "/tmp/strata_test_lua/ldo.c",
        "/tmp/strata_test_lua/ldump.c",
        "/tmp/strata_test_lua/lfunc.c",
        "/tmp/strata_test_lua/lgc.c",
        "/tmp/strata_test_lua/linit.c",
        "/tmp/strata_test_lua/liolib.c",
        "/tmp/strata_test_lua/llex.c",
        "/tmp/strata_test_lua/lmathlib.c",
        "/tmp/strata_test_lua/lmem.c",
        "/tmp/strata_test_lua/loadlib.c",
        "/tmp/strata_test_lua/lobject.c",
        "/tmp/strata_test_lua/lopcodes.c",
        "/tmp/strata_test_lua/loslib.c",
        "/tmp/strata_test_lua/lparser.c",
        "/tmp/strata_test_lua/lstate.c",
        "/tmp/strata_test_lua/lstring.c",
        "/tmp/strata_test_lua/lstrlib.c",
        "/tmp/strata_test_lua/ltable.c",
        "/tmp/strata_test_lua/ltablib.c",
        "/tmp/strata_test_lua/ltm.c",
        "/tmp/strata_test_lua/lundump.c",
        "/tmp/strata_test_lua/lutf8lib.c",
        "/tmp/strata_test_lua/lvm.c",
        "/tmp/strata_test_lua/lzio.c",
    };
    size_t nlib = sizeof(lib_srcs) / sizeof(lib_srcs[0]);

    neo_target_t *lib = neo_add_static_lib(g, "/tmp/strata_build_lua/liblua.a", lib_srcs, nlib);
    neo_target_add_cflags(lib, "-Wall -O2 -DLUA_USE_LINUX");
    neo_target_add_include_dir(lib, "/tmp/strata_test_lua");

    /* lua interpreter */
    const char *lua_srcs[] = {"/tmp/strata_test_lua/lua.c"};
    neo_target_t *lua = neo_add_executable(g, "/tmp/strata_build_lua/lua", lua_srcs, 1);
    neo_target_add_cflags(lua, "-Wall -O2 -DLUA_USE_LINUX");
    neo_target_add_include_dir(lua, "/tmp/strata_test_lua");
    neo_target_add_ldflags(lua, "-lm -ldl");
    neo_target_depends_on(lua, lib);

    int built = neo_graph_build(g);

    neo_export_compile_commands("/tmp/strata_build_lua/compile_commands.json");
    neo_graph_export_dot(g, "/tmp/strata_build_lua/build_graph.dot");

    neo_graph_destroy(g);

    printf("\n=== Lua integration test: %d targets built ===\n", built);
    return built == 2 ? 0 : 1;
}
