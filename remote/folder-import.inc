/* Walk one directory at a time. Yield to GTK between batches so playback,
 * transport requests and the phone's progress display stay responsive. */
typedef struct {
    GPtrArray *pending, *files;
    GDir *dir;
    gchar *current;
    guint index, added, skipped;
    gboolean appending;
} FolderImport;
static guint import_source;
static gchar *import_status;

static void import_free(gpointer data)
{
    FolderImport *job=data;
    g_clear_pointer(&job->dir,g_dir_close);
    g_free(job->current);
    g_ptr_array_unref(job->pending);
    g_ptr_array_unref(job->files);
    g_free(job);
    import_source=0;
}

static gboolean unchanged_path(const gchar *path)
{
    g_autofree char *actual=realpath(path,NULL);
    return actual && !strcmp(path,actual);
}

static gboolean import_step(gpointer data)
{
    FolderImport *job=data;
    const gint64 deadline=g_get_monotonic_time()+5000;
    do {
        if (job->appending) {
            if (job->index==job->files->len) {
                g_free(import_status);
                import_status=g_strdup_printf("Added %u tracks · %u unreadable or changed",job->added,job->skipped);
                return G_SOURCE_REMOVE;
            }
            const char *path=g_ptr_array_index(job->files,job->index++);
            if (!unchanged_path(path) || !g_file_test(path,G_FILE_TEST_IS_REGULAR)) { ++job->skipped; continue; }
            g_autofree char *name=g_path_get_basename(path);
            GtkTreeIter iter;
            gtk_list_store_insert_with_values(playlist_store,&iter,-1,0,name,1,path,-1);
            ++job->added;
        } else if (!job->dir) {
            if (!job->pending->len) {
                g_ptr_array_sort(job->files,playlist_compare_paths);
                job->appending=TRUE;
                continue;
            }
            g_free(job->current);
            job->current=g_ptr_array_steal_index_fast(job->pending,job->pending->len-1);
            if (unchanged_path(job->current)) job->dir=g_dir_open(job->current,0,NULL);
            if (!job->dir) ++job->skipped;
        } else {
            const char *name=g_dir_read_name(job->dir);
            if (!name) { g_clear_pointer(&job->dir,g_dir_close); continue; }
            if (*name=='.' || !g_utf8_validate(name,-1,NULL)) continue;
            g_autofree char *path=g_build_filename(job->current,name,NULL);
            GStatBuf st;
            if (g_lstat(path,&st)) { ++job->skipped; continue; }
            if (S_ISDIR(st.st_mode)) g_ptr_array_add(job->pending,g_steal_pointer(&path));
            else if (S_ISREG(st.st_mode) && playlist_is_audio_filename(name)) g_ptr_array_add(job->files,g_steal_pointer(&path));
        }
    } while (g_get_monotonic_time()<deadline);
    g_free(import_status);
    import_status=job->appending ? g_strdup_printf("Adding tracks · %u of %u",job->added,job->files->len) :
                                  g_strdup_printf("Scanning folder · %u tracks found",job->files->len);
    return G_SOURCE_CONTINUE;
}

static void start_folder_import(SoupServerMessage *msg,const char *path)
{
    if (import_source) { soup_server_message_set_status(msg,409,NULL); return; }
    if (!g_file_test(path,G_FILE_TEST_IS_DIR)) { soup_server_message_set_status(msg,404,NULL); return; }
    FolderImport *job=g_new0(FolderImport,1);
    job->pending=g_ptr_array_new_with_free_func(g_free);
    job->files=g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(job->pending,g_strdup(path));
    g_free(import_status);
    import_status=g_strdup("Scanning folder…");
    import_source=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,import_step,job,import_free);
    soup_server_message_set_status(msg,204,NULL);
}
