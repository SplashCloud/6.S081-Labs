#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


void func(int * p){
    int p1[2];
    p1[0] = p[0]; p1[1] = p[1];

    pipe(p); // create the new pipeline connect the it and the it's child

    char num[1];
    close(p1[1]); // close the old write
    if( read(p1[0], num, 1) == 1 ){
        int prime = num[0]; // the first number is the prime
        printf("prime %d\n", prime);

        if(fork() == 0){
            func(p); // recursion
        } else {
            close(p[0]); // close the read
            while ( read(p1[0], num, 1) == 1 ){
                int n = num[0];
                if(n % prime != 0){
                    write(p[1], num, 1);
                }
            }
            close(p1[0]);
            close(p[1]);
            wait(0);
        }
    } else { // no data avaliable
        close(p[0]);
        close(p[1]);
        exit(0);
    }
}

int 
main(int argc, char *argv[]){

    int p[2];
    pipe(p);

    if(fork() == 0){
      func(p);
    } else {
        close(p[0]);
        char num[1];
        for (int i = 2; i <= 35; i++){
            num[0] = i;
            write(p[1], num, 1);
        }
        close(p[1]);
        wait(0);
    }
    exit(0);
}