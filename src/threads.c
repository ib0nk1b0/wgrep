#include "threads.h"

internal File_Node* atomic_queue_pop(File_Queue* queue)
{
    File_Node* result = NULL;

    if (queue == NULL)
    {
        return result;
    }

    EnterCriticalSection(&queue->lock);

    while (queue->first == NULL && atomic_uint32_eval(&queue->threads_working) > 0)
    {
        SleepConditionVariableCS(&queue->not_empty, &queue->lock, INFINITE);
    }

    result = queue->first;
    if (result != NULL)
    {
        if (result->next)
        {
            queue->first = result->next;
            result->next = NULL;
        }
        else
        {
            queue->first = NULL;
            queue->last = NULL;
        }

        queue->count--;
    }

    LeaveCriticalSection(&queue->lock);

    return result;
}

internal void atomic_queue_push(File_Queue* queue, File_Node* node)
{
    if (queue == NULL || node == NULL)
    {
        return;
    }

    node->next = NULL;

    EnterCriticalSection(&queue->lock);

    if (queue->first == NULL)
    {
        queue->first = node;
    }
    else
    {
        queue->last->next = node;
    }

    queue->last = node;

    queue->count++;

    WakeConditionVariable(&queue->not_empty);

    LeaveCriticalSection(&queue->lock);
}

internal void atomic_grow_file_queue(Arena* arena, File_Queue* queue, uint32_t amount)
{
    EnterCriticalSection(&queue->lock);

    File_Node* nodes = ArenaPushArray(arena, File_Node, amount);
    queue->first = nodes;
    queue->last = nodes;
    queue->count += amount;
    for (int i = 1; i < amount; i++)
    {
        File_Node* node = &nodes[i];
        queue->last->next = node;
        queue->last = node;
    }

    LeaveCriticalSection(&queue->lock);
}

// NOTE: full_path will be setup as reusable buffer for placing the path into
internal void atomic_recurse_directory(Arena* arena, char* full_path, File_Node* node, char* file_read_buffer, File_Queue* files_queue, File_Queue* free_list, char* pattern, uint32_t flags, char* out_buffer, uint32_t out_buffer_size, uint32_t* buffer_idx)
{
    if (node->kind == FILE_NODE_DIR)
    {
        assert(full_path);
        WIN32_FIND_DATA ffd = {0};
        TCHAR szDir[MAX_PATH];
        HANDLE hFind = INVALID_HANDLE_VALUE;

        char* dir = node->path;
        uint32_t dir_len = strlen(dir);
        if (dir[dir_len-1] == '\\' || dir[dir_len-1] == '/')
        {
            snprintf(szDir, MAX_PATH, "%s*", dir);
        }
        else if (dir[dir_len-1] != '*')
        {
            snprintf(szDir, MAX_PATH, "%s\\*", dir);
        }

        hFind = FindFirstFile(szDir, &ffd);

        do
        {
            if (dir[dir_len-1] == '\\' || dir[dir_len-1] == '/')
            {
                snprintf(full_path, MAX_PATH, "%s%s", dir, ffd.cFileName);
            }
            else
            {
                snprintf(full_path, MAX_PATH, "%s\\%s", dir, ffd.cFileName);
            }

            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (ffd.cFileName[0] != '.')
                {
                    if (atomic_uint32_eval(&free_list->count) == 0)
                    {
                        atomic_grow_file_queue(arena, free_list, MAX_FILE_NODES);
                    }

                    File_Node* node = atomic_queue_pop(free_list);

                    if (node->path == NULL)
                    {
                        node->path = ArenaPushArray(arena, char, MAX_PATH);
                    }

                    node->kind = FILE_NODE_DIR;

                    strcpy(node->path, full_path);

                    atomic_queue_push(files_queue, node);
                }
            }
            else
            {
                    if (atomic_uint32_eval(&free_list->count) == 0)
                    {
                        atomic_grow_file_queue(arena, free_list, MAX_FILE_NODES);
                    }

                    File_Node* node = atomic_queue_pop(free_list);

                    if (node->path == NULL)
                    {
                        node->path = ArenaPushArray(arena, char, MAX_PATH);
                    }

                    node->kind = FILE_NODE_PATH;

                    strcpy(node->path, full_path);

                    atomic_queue_push(files_queue, node);
            }
        }
        while (FindNextFile(hFind, &ffd) != 0);
    }
    else
    {
        uint32_t bytes_read = 0;
        uint32_t final_bytes_read = 0;

        HANDLE file = CreateFileA(node->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);

        LARGE_INTEGER file_size;
        GetFileSizeEx(file, &file_size);

        uint32_t remaining = (uint32_t)file_size.QuadPart;
        size_t total_matches = 0;
        while (remaining > 0)
        {
            uint32_t bytes_to_read = remaining < FILE_BUFFER_SIZE ? remaining : FILE_BUFFER_SIZE;

            if(!ReadFile(file, file_read_buffer, bytes_to_read, &bytes_read, NULL))
            {
                break;
            }

            StringView sv = sv_from_parts(file_read_buffer, bytes_read);
            total_matches += match_pattern_in_sv(arena, out_buffer, out_buffer_size, buffer_idx, pattern, sv, node->path, flags);

            final_bytes_read += bytes_read;
            remaining -= bytes_read;
        }

        CloseHandle(file);

        // EnterCriticalSection(&files_queue->lock);
        atomic_uint64_add(&g_total_matches, total_matches);
        atomic_uint32_inc(&g_Stats.files_searched);
        atomic_uint64_add(&g_Stats.bytes_searched, final_bytes_read);
        // LeaveCriticalSection(&files_queue->lock);
    }
}

DWORD thread_main(void* data)
{
    Thread_Data* thread_data = (Thread_Data*)data;

    Arena* arena = arena_create(Megabytes(256));
    char* file_read_buffer = ArenaPushArray(arena, char, FILE_BUFFER_SIZE);
    char* full_path_buffer = ArenaPushArray(arena, char, MAX_PATH);
    char* out_buffer       = ArenaPushArray(arena, char, OUT_BUFFER_SIZE);
    uint32_t buffer_idx    = 0;

    File_Node* node = atomic_queue_pop(thread_data->queue);
    while (node != NULL || atomic_uint32_eval(&thread_data->queue->threads_working) > 0)
    {
        atomic_uint32_inc(&thread_data->queue->threads_working);
        atomic_uint32_inc(&thread_data->free_list->threads_working);

        atomic_recurse_directory(arena, full_path_buffer, node, file_read_buffer, thread_data->queue, thread_data->free_list, thread_data->pattern, thread_data->flags, out_buffer, OUT_BUFFER_SIZE, &buffer_idx);

        atomic_uint32_dec(&thread_data->queue->threads_working);
        atomic_uint32_dec(&thread_data->free_list->threads_working);

        atomic_queue_push(thread_data->free_list, node);
        node = atomic_queue_pop(thread_data->queue);
    }

    if (buffer_idx > 0)
    {
        print_buffer(out_buffer, &buffer_idx);
    }

    WakeAllConditionVariable(&thread_data->queue->not_empty);
    WakeAllConditionVariable(&thread_data->free_list->not_empty);

    return 0;
}
