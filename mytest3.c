#include "types.h"
#include "stat.h"
#include "user.h"

int main(void){

    int parent = getpid();
    int child;

    printf(1, "====================Project 3 Test====================\n\n");
   
    printf(1, "=================== Initial table ====================\n");
    ps(0);
    printf(1,"\n");

    setnice(1,5);
    setnice(2,5);
    setnice(parent,5);

    child = fork();
    ps(0);
    if (child < 0) {
        printf(1,"fork error!");
    }
    //자식
    if(child==0){
        setnice(getpid(),39);
        ps(0);
        printf(1,"\n");
        exit();
    }
    //부모
    else{               
        setnice(child,39);  
        for(int i = 0; i < 10000; i++){
            for(int j = 0; j < 10000; j++){
                asm("nop"); // No operation, just to prevent loop optimization by the compiler
            }    
        }
        ps(0);
        printf(1,"\n");
        exit();
    }
    
    printf(1,"\n");

    printf(1, "===================== After fork ====================\n");
    
    
    setnice(4,0);
    ps(0);

    exit();
}