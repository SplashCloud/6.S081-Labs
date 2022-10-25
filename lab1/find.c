#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

    // Look at user/ls.c to see how to read directories.
    // Use recursion to allow find to descend into sub-directories.
    // Don't recurse into "." and "..".
    // Changes to the file system persist across runs of qemu; to get a clean file system run make clean and then make qemu.
    // You'll need to use C strings. Have a look at K&R (the C book), for example Section 5.5.
    // Note that == does not compare strings like in Python. Use strcmp() instead.
    // Add the program to UPROGS in Makefile. 


void
find(char *dir, char *file){
    struct stat st;
    struct dirent de;
    int fd;
    char buf[512], *p;

    // put the dir_name into the buf
    // buf : dir_name/
    strcpy(buf, dir);
    p = buf + strlen(buf);
    *p++ = '/';

    if( (fd = open(dir, 0)) < 0 ){
        fprintf(2, "find: cannot open %s\n", dir);
        return;
    }

    if( fstat(fd, &st) < 0 ){
        fprintf(2, "find: cannot stat %s\n", dir);
        close(fd);
        return;
    }

    if(st.type != T_DIR){
        fprintf(2, "find: %s is not a dir\n", dir);
        close(fd);
        return;
    }
    
    // read the every file or dir in the `dir` sequentially
    while( read(fd, &de, sizeof(de)) == sizeof(de)){
        if(de.inum == 0) continue;

        char *name = de.name;
        if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue; // not consider the . and ..

        memmove(p, name, DIRSIZ);
        p[DIRSIZ] = 0;

        if(stat(buf, &st) < 0){
            printf("find: cannot stat %s\n", name);
            continue;
        }

        if(st.type == T_DIR){
            find(buf, file);
        } else if(strcmp(name, file) == 0) {
            printf("%s\n", buf);
        }
    }
    close(fd);
}

// find <dir_name> <file_name>
// find all the <file_name> in the <dir_name>
int
main(int argc, char* argv[]){
    if(argc < 3){
        fprintf(1, "the arguments is too few...\n");
        exit(1);
    }

    find(argv[1], argv[2]);
    exit(0);
}
