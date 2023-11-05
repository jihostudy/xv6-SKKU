#include "types.h"
#include "user.h"
#include "stat.h"
#include "param.h"
#include "fcntl.h"

int
main(int argc, char **argv)
{
    if(argc != 7) {
        printf(2,"need input pid");
        exit();
    }

    int fd = open("README",O_RDWR);
    printf(2,"File Used is %d\n",fd);

    uint test = mmap(0, 4096, PROT_READ|PROT_WRITE,MAP_POPULATE, fd, 0);
    printf(2,"Test : %d\n",test);
    uint test2 = mmap(atoi(argv[0]), atoi(argv[1]), atoi(argv[2]),atoi(argv[3]), atoi(argv[4]),atoi(argv[5])); 
    printf(2, "Test2 : %d\n", test2);
    exit();
}
