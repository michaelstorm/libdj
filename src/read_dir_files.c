#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int buf_len = 1048576;
char *buf;

void recurse_dir(char *base, char *dir_name)
{
    char *dir_path;
    if (dir_name == NULL)
        dir_path = base;
    else
    {
        dir_path = alloca(strlen(base) + strlen(dir_name) + 2);
        sprintf(dir_path, "%s/%s", base, dir_name);
    }

    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(dir_path)) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            if (ent->d_type == DT_REG)
            {
                char *path = alloca(strlen(dir_path) + strlen(ent->d_name) + 2);
                sprintf(path, "%s/%s", dir_path, ent->d_name);
                path[strlen(dir_path) + strlen(ent->d_name) + 1] = '\0';
                printf("%s\n", path);

                /*FILE *f = fopen(path, "r");
                while (!feof(f))
                    fread(buf, buf_len, 1, f);
                fclose(f);*/
                int fd = open(path, O_RDONLY | O_DIRECT);
                int bytes_read;
                while ((bytes_read = read(fd, buf, buf_len)) > 0);
                if (bytes_read == -1)
                    perror("Error reading file");
                close(fd);
            }
            else if (ent->d_type == DT_DIR)
            {
                if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, ".."))
                    recurse_dir(dir_path, ent->d_name);
            }
        }
        closedir(dir);
    } else {
        perror("opendir");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    if (posix_memalign((void **)&buf, 512, buf_len))
        perror("Error allocating aligned memory");
    recurse_dir(argv[1], NULL);
    return 0;
}
