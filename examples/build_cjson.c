/*
 * Integration test: build cJSON using strata's graph API.
 * Expects cJSON source at /tmp/strata_test_cjson/
 */
#include "buildsysdep/neobuild.h"
#include <stdio.h>

int main(void)
{
    neo_set_verbosity(NEO_VERBOSE);
    neo_set_build_dir("/tmp/strata_build_cjson/");
    neo_set_jobs(0);

    neo_graph_t *g = neo_graph_create();
    neo_graph_enable_content_hash(g);

    /* static library */
    const char *lib_srcs[] = {"/tmp/strata_test_cjson/cJSON.c"};
    neo_target_t *lib = neo_add_static_lib(g, "/tmp/strata_build_cjson/libcjson.a", lib_srcs, 1);
    neo_target_add_cflags(lib, "-Wall -Wextra -std=c89 -pedantic");
    neo_target_add_include_dir(lib, "/tmp/strata_test_cjson");

    /* test executable */
    const char *test_srcs[] = {"/tmp/strata_test_cjson/test.c"};
    neo_target_t *test = neo_add_executable(g, "/tmp/strata_build_cjson/cjson_test", test_srcs, 1);
    neo_target_add_include_dir(test, "/tmp/strata_test_cjson");
    neo_target_add_ldflags(test, "-lm");
    neo_target_depends_on(test, lib);

    int built = neo_graph_build(g);

    /* export artifacts */
    neo_export_compile_commands("/tmp/strata_build_cjson/compile_commands.json");
    neo_graph_export_dot(g, "/tmp/strata_build_cjson/build_graph.dot");

    neo_graph_destroy(g);

    printf("\n=== cJSON integration test: %d targets built ===\n", built);
    return built == 2 ? 0 : 1;
}
