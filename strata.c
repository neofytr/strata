#include "buildsysdep/neobuild.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);
    return EXIT_SUCCESS;
}
