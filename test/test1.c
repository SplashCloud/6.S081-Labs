#include <stdio.h>
#include <string.h>
int
main(int argc, char* argv[]){
        char * buf[32];
        for(int i = 0; i < argc; i++){
                char c[31];
                strcpy(c, argv[i]);
                buf[i] = c;
        }
        printf("%s\n", buf[0]);
	return 0;
}

