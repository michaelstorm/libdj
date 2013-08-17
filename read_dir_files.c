#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>

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

                FILE *f = fopen(path, "r");
                while (!feof(f))
                    fread(buf, buf_len, 1, f);
                fclose(f);
            }
            else if (ent->d_type == DT_DIR)
            {
                if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, ".."))
                    recurse_dir(dir_path, ent->d_name);
            }
        }
        closedir(dir);
    } else {
        /* could not open directory */
        perror ("");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    buf = malloc(buf_len);
    recurse_dir(argv[1], NULL);
    return 0;
}
