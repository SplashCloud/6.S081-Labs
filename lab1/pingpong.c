#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int 
main(int argc, char *argv[]){
    int p[2];

    char * parent_msg = "p";
    char * child_msg  = "c";
    char buffer[2];

    pipe(p);
    if(fork() == 0){
        printf("%d: received ping\n", getpid());
        close(p[0]);
        write(p[1], child_msg, 1);
        close(p[1]);
        exit(0);
    } else {
        write(p[1], parent_msg, 1);
        close(p[1]);
        wait(0);
        read(p[0], buffer, 1);
        close(p[0]);
        printf("%d: received pong\n", getpid());
        exit(0);
    }
}