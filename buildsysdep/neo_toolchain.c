#include "neo_internal.h"

neo_toolchain_t *neo_toolchain_create(const char *prefix)
{
    neo_toolchain_t *tc = (neo_toolchain_t *)calloc(1, sizeof(neo_toolchain_t));
    if (!tc) return NULL;
    if (prefix) tc->prefix = strdup(prefix);
    return tc;
}

void neo_toolchain_set_sysroot(neo_toolchain_t *tc, const char *sysroot)
{
    if (!tc) return;
    free(tc->sysroot);
    tc->sysroot = sysroot ? strdup(sysroot) : NULL;
}

void neo_toolchain_set_cc(neo_toolchain_t *tc, const char *cc)
{
    if (!tc) return;
    free(tc->cc);
    tc->cc = cc ? strdup(cc) : NULL;
}

void neo_toolchain_set_cxx(neo_toolchain_t *tc, const char *cxx)
{
    if (!tc) return;
    free(tc->cxx);
    tc->cxx = cxx ? strdup(cxx) : NULL;
}

void neo_toolchain_destroy(neo_toolchain_t *tc)
{
    if (!tc) return;
    free(tc->prefix); free(tc->sysroot);
    free(tc->cc); free(tc->cxx);
    free(tc->ar); free(tc->ranlib);
    free(tc);
}
