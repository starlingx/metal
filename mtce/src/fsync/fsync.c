/*
 * Copyright (c) 2014 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* helper app to fsync a single file/directory */

int main(int argc, char **argv)
{
    int fd,rc;

    if (argc != 2) {
        printf("usage: %s <path/to/file>\n", argv[0]);
        return -1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        printf("unable to open file %s: %m\n", argv[1]);
        return -1;
    }

    rc = fsync(fd);
    if (rc == -1) {
        printf("error fsyncing file %s: %m\n", argv[1]);
    }
    
    if (close(fd) == -1) {
        printf("error closing file %s: %m\n", argv[1]);
    }
    
    return rc;
}
