#include "types.h"
#include "stat.h"
#include "user.h"

int main(void){

    int parent = getpid();
    int p;        
    
    printf(1, "=================== Initial ====================\n");
    ps(0);

    printf(1, "=================== Check Vruntime ====================\n");
    ps(0);

    printf(1, "=================== After setnice(1, 5), setnice(2,5) ====================\n");
    setnice(1,5);
    setnice(2,5);    
    ps(0);

    p = fork();
    printf(1, "===================== fork() -> 상속 확인 %d====================\n",p);
    ps(0);
    if (p < 0) {
        printf(1,"fork error!");
    }
    //child
    if(p == 0){
        setnice(parent,39); 
        printf(1, "===================== setnice(parent,39) ====================\n");
        ps(0);
        printf(1,"\n");
        exit();
    }
    //parent
    else{
        wait(); // child first
        setnice(p,38);
        printf(1, "===================== setnice(child,39) ====================\n");
        ps(0);
        printf(1,"\n");
    }
    
    

    printf(1, "===================== After fork ====================\n");
    printf(1,"\n");
    
    //setnice(4,0);
    ps(0);
    setnice(1,20);
    setnice(2,20);
    exit();
}