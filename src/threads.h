#ifndef WGREP_THREADS_H
#define WGREP_THREADS_H

#define atomic_uint32_eval(x)   (uint32_t)__iso_volatile_load32((__int32*)(x))
#define atomic_uint32_inc(x)    _InterlockedIncrement((long*)(x))
#define atomic_uint32_dec(x)    _InterlockedDecrement((long*)(x))
#define atomic_uint64_add(x, v) _interlockedadd64((__int64 *)(x), (v))

// TODO: this shouldn't really be here but will do for now

#define MAX_FILE_NODES 256

typedef struct FileNode File_Node;
typedef struct
{
    File_Node*       first;
    File_Node*       last;

    CRITICAL_SECTION lock;
    CONDITION_VARIABLE not_empty;

    uint32_t count;

    uint32_t threads_working;
} File_Queue;

typedef enum
{
    FILE_NODE_DIR,
    FILE_NODE_PATH
} File_Node_Kind;
typedef struct FileNode
{
    File_Node*       next;
    char*            path;
    File_Node_Kind   kind;
} File_Node;

typedef struct
{
    uint32_t    index;
    File_Queue* queue;
    File_Queue* free_list;
    char*       pattern;
    uint32_t    flags;
} Thread_Data;

internal File_Node* atomic_queue_pop(File_Queue* queue);
internal void atomic_queue_push(File_Queue* queue, File_Node* node);
internal void atomic_grow_file_queue(Arena* arena, File_Queue* queue, uint32_t amount);

internal void atomic_recurse_directory(Arena* arena, char* full_path, File_Node* node, char* file_read_buffer, File_Queue* files_queue, File_Queue* free_list, char* pattern, uint32_t flags, char* out_buffer, uint32_t out_buffer_size, uint32_t* buffer_idx);

internal DWORD thread_main(void* data);

#endif // WGREP_THREADS_H
