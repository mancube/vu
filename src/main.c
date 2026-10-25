/* Task 3 stub: --list / --help only. Replaced by the full app in Task 5. */
#include "backend.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc > 1 && (!strcmp(argv[1], "--list") || !strcmp(argv[1], "-l"))) {
        char names[VU_MAX_MONITORS][128];
        int count = 0;
        if (backend_list_monitors(names, &count) != 0 || count == 0) {
            fprintf(stderr, "vu: no monitor sources found (%s)\n", backend_error());
            return 1;
        }
        for (int i = 0; i < count; i++) printf("%s\n", names[i]);
        return 0;
    }
    printf("usage: vu [--list] [-h]\n");
    return 0;
}
