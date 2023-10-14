#include "types.h"
#include "user.h"
#include "stat.h"

int
main(int argc, char **argv)
{
    if(argc != 3){
        printf(2, "need 2 inputs: pid & nice_value");
        exit();
    }
    // 음수가 들어오는 경우 (atoi로 해결이 안돼서 따로 exit)
    if(argv[2][0] == '-'){
        exit();
    }
    setnice(atoi(argv[1]), atoi(argv[2]));
    exit();
}
