static gchar **shared_music_roots;

static void load_shared_music(void)
{
    if (shared_music_roots) return;
    g_autoptr(GKeyFile) file = g_key_file_new();
    g_autofree char *path = player_config_path("remote-folders.ini");
    if (g_key_file_load_from_file(file,path,G_KEY_FILE_NONE,NULL))
        shared_music_roots = g_key_file_get_string_list(file,"Remote","Folders",NULL,NULL);
}

static void choose_shared_music(GtkButton *button, gpointer parent)
{
    GtkWidget *chooser = gtk_file_chooser_dialog_new("Shared music folders",GTK_WINDOW(parent),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Cancel",GTK_RESPONSE_CANCEL,"Clear sharing",GTK_RESPONSE_REJECT,
        "Add folders",GTK_RESPONSE_ACCEPT,NULL);
    theme_dialog(chooser);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser),TRUE);
    gint answer = gtk_dialog_run(GTK_DIALOG(chooser));
    if (answer == GTK_RESPONSE_ACCEPT || answer == GTK_RESPONSE_REJECT) {
        g_autoptr(GPtrArray) roots = g_ptr_array_new_with_free_func(g_free);
        if (answer == GTK_RESPONSE_ACCEPT) {
            for (int i=0; shared_music_roots && shared_music_roots[i]; i++)
                g_ptr_array_add(roots,g_strdup(shared_music_roots[i]));
            GSList *chosen = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(chooser));
            for (GSList *it=chosen; it; it=it->next) {
                char *p = realpath(it->data,NULL);
                if (p && !g_ptr_array_find_with_equal_func(roots,p,g_str_equal,NULL)) g_ptr_array_add(roots,p);
                else g_free(p);
            }
            g_slist_free_full(chosen,g_free);
        }
        g_ptr_array_add(roots,NULL);
        g_autoptr(GKeyFile) file = g_key_file_new();
        g_key_file_set_string_list(file,"Remote","Folders",(const char *const *)roots->pdata,roots->len-1);
        g_autofree gchar *path = player_config_path("remote-folders.ini");
        g_autofree gchar *parent_dir = g_path_get_dirname(path);
        if (g_mkdir_with_parents(parent_dir,0700)==0 && g_key_file_save_to_file(file,path,NULL)) {
            g_strfreev(shared_music_roots);
            shared_music_roots=(gchar **)g_ptr_array_steal(roots,NULL);
            g_autofree char *label=g_strdup_printf("Shared music folders (%u)…",g_strv_length(shared_music_roots));
            gtk_button_set_label(button,label);
        } else set_status("Could not save shared music folders");
    }
    gtk_widget_destroy(chooser);
}

static void music_row(JsonArray *items,const char *name,const char *key,gboolean folder)
{
    JsonObject *row=json_object_new();
    json_object_set_string_member(row,"name",name);
    json_object_set_string_member(row,"key",key);
    json_object_set_boolean_member(row,"folder",folder);
    json_array_add_object_element(items,row);
}

static void remote_music(SoupServerMessage *msg,GHashTable *query,gboolean add)
{
    const char *id=query ? g_hash_table_lookup(query,"root") : NULL;
    const char *rel=query ? g_hash_table_lookup(query,"path") : NULL;
    const char *root=NULL;
    if (!rel) rel="";
    load_shared_music();
    g_autoptr(JsonArray) rows=json_array_new();
    for (guint i=0; shared_music_roots && shared_music_roots[i]; ++i) {
        g_autofree char *key=g_compute_checksum_for_string(G_CHECKSUM_SHA256,shared_music_roots[i],-1);
        if (!id) {
            g_autofree char *label=g_filename_display_basename(shared_music_roots[i]);
            music_row(rows,label,key,TRUE);
        } else if (!strcmp(id,key)) root=shared_music_roots[i];
    }
    if (!root && (id || add || *rel)) { soup_server_message_set_status(msg,404,NULL); return; }
    if (root) {
        g_auto(GStrv) parts=g_strsplit(rel,"/",-1);
        for (guint i=0; parts[i]; ++i)
            if (!*parts[i] || parts[i][0]=='.') { soup_server_message_set_status(msg,400,NULL); return; }
        g_autofree char *path=*rel ? g_build_filename(root,rel,NULL) : g_strdup(root);
        g_autofree char *canonical=realpath(path,NULL);
        if (!canonical || strcmp(path,canonical)) { soup_server_message_set_status(msg,404,NULL); return; }
        if (add) {
            g_autofree char *name=g_path_get_basename(path);
            if (!g_file_test(path,G_FILE_TEST_IS_REGULAR) || !playlist_is_audio_filename(name)) {
                soup_server_message_set_status(msg,404,NULL); return;
            }
            GtkTreeIter iter;
            gtk_list_store_insert_with_values(playlist_store,&iter,-1,0,name,1,path,-1);
            soup_server_message_set_status(msg,204,NULL); return;
        }
        g_autoptr(GDir) dir=g_dir_open(path,0,NULL);
        if (!dir) { soup_server_message_set_status(msg,404,NULL); return; }
        g_autoptr(GPtrArray) folders=g_ptr_array_new_with_free_func(g_free);
        g_autoptr(GPtrArray) tracks=g_ptr_array_new_with_free_func(g_free);
        const char *name;
        while ((name=g_dir_read_name(dir))) {
            if (*name=='.' || !g_utf8_validate(name,-1,NULL)) continue;
            g_autofree char *child=g_build_filename(path,name,NULL);
            GStatBuf st;
            if (g_lstat(child,&st)!=0) continue;
            if (S_ISDIR(st.st_mode)) g_ptr_array_add(folders,g_strdup(name));
            else if (S_ISREG(st.st_mode) && playlist_is_audio_filename(name)) g_ptr_array_add(tracks,g_strdup(name));
        }
        GPtrArray *groups[]={folders,tracks};
        for (unsigned group=0;group<2;group++) {
            g_ptr_array_sort(groups[group],playlist_compare_paths);
            for (guint i=0;i<groups[group]->len;i++) {
                const char *entry=g_ptr_array_index(groups[group],i);
                g_autofree char *child=*rel ? g_build_filename(rel,entry,NULL) : g_strdup(entry);
                music_row(rows,entry,child,group==0);
            }
        }
    }
    JsonObject *result=json_object_new();
    json_object_set_array_member(result,"items",g_steal_pointer(&rows));
    JsonNode *node=json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node,result);
    remote_json(msg,node);
}
