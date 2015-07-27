#include <ext2fs/ext2fs.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#define ITERATE_OPT_DIRECT     1
#define ITERATE_OPT_RECURSIVE  2
#define ITERATE_OPT_LIST_FILES 4
#define ITERATE_OPT_LIST_DIRS  8

void exit_str(char *message, ...)
{
    va_list args;
    va_start(args,  message);
    fprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

void *ecalloc(size_t size)
{
    void *ptr = calloc(1, size);
    if (ptr == NULL)
        exit_str("Error allocating %d bytes of memory", size);
    return ptr;
}

char *prog_name;

#define CHECK_FATAL(FUNC, MSG, ...) \
{ \
    errcode_t err = FUNC; \
    if (err != 0) \
    { \
        com_err(prog_name, err, MSG, ##__VA_ARGS__); \
        exit(1); \
    } \
}

struct inode_list
{
    ext2_ino_t index;
    char *path;
    struct inode_list *next;
};

struct dir_tree_entry
{
    char *path;
    struct dir_tree_entry *parent;
};

struct dir_entry_cb_data
{
    ext2_filsys fs;
    int flags;
    struct dir_tree_entry *dir;
    struct inode_list *list_start;
    struct inode_list *list_end;
};


char *dir_path_append_name(struct dir_tree_entry *dir, char *name)
{
    char *path;
    if (dir == NULL)
    {
        path = malloc(strlen(name)+1);
        strcpy(path, name);
    }
    else
    {
        path = malloc(strlen(dir->path) + strlen(name) + 2);
        sprintf(path, "%s/%s", dir->path, name);
    }
    return path;
}

void dir_entry_add_file(ext2_ino_t ino, char *name,
                        struct dir_entry_cb_data *cb_data)
{
    struct inode_list *list = ecalloc(sizeof(struct inode_list));
    list->index = ino;
    list->path = dir_path_append_name(cb_data->dir, name);

    if (cb_data->list_start == NULL)
    {
        cb_data->list_start = list;
        cb_data->list_end = list;
    }
    else
    {
        cb_data->list_end->next = list;
        cb_data->list_end = list;
    }
}

int dir_entry_cb(ext2_ino_t dir_ino, int entry, struct ext2_dir_entry *dirent,
                 int offset, int blocksize, char *buf, void *private)
{
    // saw the 0xFF in e2fsprogs sources; don't know why the high bits would be
    // set
    int name_len = dirent->name_len & 0xFF;
    char name[name_len+1];
    memcpy(name, dirent->name, name_len);
    name[name_len] = '\0';

    if (entry == DIRENT_OTHER_FILE)
    {
        struct dir_entry_cb_data *cb_data = (struct dir_entry_cb_data *)private;

        struct ext2_inode inode_contents;
        CHECK_FATAL(ext2fs_read_inode(cb_data->fs, dirent->inode,
                                      &inode_contents),
                "while reading inode contents");
        if (LINUX_S_ISDIR(inode_contents.i_mode))
        {
            if (cb_data->flags & ITERATE_OPT_LIST_DIRS)
                dir_entry_add_file(dirent->inode, name, cb_data);

            if (cb_data->flags & ITERATE_OPT_RECURSIVE)
            {
                struct dir_tree_entry dir;
                dir.parent = cb_data->dir;
                dir.path = dir_path_append_name(dir.parent, name);

                char block_buf[cb_data->fs->blocksize*3];

                cb_data->dir = &dir;
                CHECK_FATAL(ext2fs_dir_iterate2(cb_data->fs, dirent->inode, 0,
                                                block_buf, dir_entry_cb,
                                                cb_data),
                        "while iterating over directory %s", name);
                cb_data->dir = cb_data->dir->parent;

                free(dir.path);
            }
        }
        else if (cb_data->flags & ITERATE_OPT_LIST_FILES
                 && !S_ISLNK(inode_contents.i_mode))
        {
            dir_entry_add_file(dirent->inode, name, cb_data);
        }
    }

    return 0;
}

void list_files(char *dev_path, char *target_path, int flags)
{
    ext2_filsys fs;
    CHECK_FATAL(ext2fs_open(dev_path, 0, 0, 0, unix_io_manager, &fs),
            "while opening file system on device %s", dev_path);

    int fd;
    int open_flags = O_RDONLY;
    if (flags & ITERATE_OPT_DIRECT)
        open_flags |= O_DIRECT;
    if ((fd = open(dev_path, open_flags)) < 0)
        exit_str("Error opening block device %s", dev_path);

    ext2_ino_t ino;
    CHECK_FATAL(ext2fs_namei_follow(fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
                                    target_path, &ino),
            "while looking up path %s", target_path);

    struct dir_entry_cb_data cb_data = { fs, flags, NULL, NULL, NULL };
    struct ext2_inode inode_contents;
    CHECK_FATAL(ext2fs_read_inode(fs, ino, &inode_contents),
            "while reading inode contents");

    struct dir_tree_entry dir = { target_path, NULL };
    cb_data.dir = &dir;
    if (LINUX_S_ISDIR(inode_contents.i_mode))
    {
        CHECK_FATAL(ext2fs_dir_iterate2(fs, ino, 0, NULL, dir_entry_cb,
                                        &cb_data),
                "while iterating over directory %s", target_path);
    }
    else
        exit_str("Unexpected file mode %x", inode_contents.i_mode);

    struct inode_list *l = cb_data.list_start;
    while (l != NULL)
    {
        fprintf(stderr, "%s\n", l->path);
        l = l->next;
    }

    if (close(fd) != 0)
        exit_str("Error closing block device");

    if (ext2fs_close(fs) != 0)
        exit_str("Error closing file system");
}

void print_name(char *base_path, char *d_name)
{
    char *path = alloca(strlen(base_path) + strlen(d_name) + 2);
    sprintf(path, "%s/%s", base_path, d_name);
    path[strlen(base_path) + strlen(d_name) + 1] = '\0';
    printf("%s\n", path);
}

void recurse_dir(char *base, char *dir_name, int flags)
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
                if (flags & ITERATE_OPT_LIST_FILES)
                    print_name(dir_path, ent->d_name);
            }
            else if (ent->d_type == DT_DIR)
            {
                if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, ".."))
                {
                    if (flags & ITERATE_OPT_LIST_DIRS)
                        print_name(dir_path, ent->d_name);
                    if (flags & ITERATE_OPT_RECURSIVE)
                        recurse_dir(dir_path, ent->d_name, flags);
                }
            }
        }
        closedir(dir);
    } else {
        perror("opendir");
        exit(1);
    }
}

void initialize_dj(char *error_prog_name)
{
    prog_name = error_prog_name;
    initialize_ext2_error_table();
}

void usage(char *prog_name)
{
    fprintf(stderr, "Usage: %s DEVICE DIRECTORY [--list-files] [--list-dirs] "
                    "[-R]\n", prog_name);
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc < 3)
        usage(argv[0]);

    initialize_dj(argv[0]);

    int flags = 0;
    int device_index = 0;
    int dir_index = 0;
    int command;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp("-R", argv[i]))
            flags |= ITERATE_OPT_RECURSIVE;
        else if (!strcmp("--list-files", argv[i]))
            flags |= ITERATE_OPT_LIST_FILES;
        else if (!strcmp("--list-dirs", argv[i]))
            flags |= ITERATE_OPT_LIST_DIRS;
        else if (!strcmp("--no-buffer-cache", argv[i]))
            flags |= ITERATE_OPT_DIRECT;
        else if (device_index == 0)
            device_index = i;
        else if (dir_index == 0)
            dir_index = i;
        else
        {
            fprintf(stderr, "Unrecognized option %s\n", argv[i]);
            usage(argv[0]);
        }
    }

    if (device_index == 0)
    {
        fprintf(stderr, "Please specify device file\n");
        usage(argv[0]);
    }
    else if (device_index == 0)
    {
        fprintf(stderr, "Please specify directory on device\n");
        usage(argv[0]);
    }

    char *device = argv[device_index];
    char *dir = argv[dir_index];

    if (flags & ITERATE_OPT_DIRECT)
        list_files(device, dir, flags);
    else
        recurse_dir(dir, NULL, flags);
    return 0;
}
