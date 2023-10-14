#include "types.h"
#include "user.h"
#include "stat.h"

int
main(int argc, char **argv)
{
    if(argc != 2){
        printf(2,"need input pid\n"); 
        exit();
    }

    printf(1, "%d\n", getnice(atoi(argv[1])));
    exit();
}
