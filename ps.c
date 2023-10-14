#include "types.h"
#include "user.h"
#include "stat.h"

int
main(int argc, char **argv)
{
    if(argc != 2) {
        printf(2,"need input pid");
        exit();
    }

    ps(atoi(argv[1]));
    exit();
}
