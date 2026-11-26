/* Prefer a live LAN link even when a tunnel owns the default route. */
typedef struct {
    char ip[INET_ADDRSTRLEN];
    char name[IF_NAMESIZE];
    const char *kind;
    int rank;
    unsigned long metric;
} RemoteAddress;

static unsigned long remote_route_metric(const char *routes, const char *name)
{
    unsigned long best = ULONG_MAX;
    for (const char *line = routes; line && *line; ) {
        char iface[IF_NAMESIZE];
        unsigned long dst, flags, metric, mask;
        if (sscanf(line, "%15s %lx %*x %lx %*u %*u %lu %lx",
                   iface, &dst, &flags, &metric, &mask) == 5 &&
            !strcmp(iface, name) && !dst && !mask && (flags & 1) && metric < best)
            best = metric;
        line = strchr(line, '\n');
        if (line) line++;
    }
    return best;
}

static gint remote_address_compare(gconstpointer left, gconstpointer right)
{
    const RemoteAddress *a = left, *b = right;
    if (a->rank != b->rank) return b->rank - a->rank;
    if (a->metric != b->metric) return a->metric < b->metric ? -1 : 1;
    int by_name = strcmp(a->name, b->name);
    return by_name ? by_name : strcmp(a->ip, b->ip);
}

static void remote_address_choices(GtkComboBoxText *combo)
{
    g_autoptr(GArray) addresses = g_array_new(FALSE, FALSE, sizeof(RemoteAddress));
    g_autofree char *routes = NULL;
    g_file_get_contents("/proc/net/route", &routes, NULL, NULL);
    struct ifaddrs *all = NULL;
    if (getifaddrs(&all) != 0) return;
    for (struct ifaddrs *it = all; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET ||
            !(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
        RemoteAddress a = {0};
        struct in_addr ip = ((struct sockaddr_in *)it->ifa_addr)->sin_addr;
        if (!inet_ntop(AF_INET, &ip, a.ip, sizeof(a.ip))) continue;
        g_strlcpy(a.name, it->ifa_name, sizeof(a.name));
        g_autofree char *wifi = g_build_filename("/sys/class/net", a.name, "wireless", NULL);
        g_autofree char *device = g_build_filename("/sys/class/net", a.name, "device", NULL);
        g_autofree char *bridge = g_build_filename("/sys/class/net", a.name, "bridge", NULL);
        gboolean wireless = g_file_test(wifi, G_FILE_TEST_IS_DIR);
        gboolean physical = wireless || g_file_test(device, G_FILE_TEST_EXISTS);
        gboolean bridged = g_file_test(bridge, G_FILE_TEST_IS_DIR);
        gboolean tunnel = (it->ifa_flags & IFF_POINTOPOINT) != 0;
        g_autofree char *type_path = g_build_filename("/sys/class/net", a.name, "type", NULL);
        g_autofree char *type = NULL;
        g_file_get_contents(type_path, &type, NULL, NULL);
        gboolean ethernet = physical && type && !strcmp(g_strstrip(type), "1");
        a.kind = wireless ? "Wi-Fi" : ethernet ? "Ethernet" : physical ? "Hardware" : bridged ? "Bridge" :
                 tunnel ? "Tunnel" : "Virtual / other";
        a.metric = remote_route_metric(routes, a.name);
        a.rank = (it->ifa_flags & IFF_RUNNING ? 16 : 0) +
                 (physical && !tunnel ? 8 : bridged && a.metric != ULONG_MAX ? 8 : 0) +
                 (!tunnel ? 2 : 0) + ((ntohl(ip.s_addr) >> 16) != 0xa9fe);
        g_array_append_val(addresses, a);
    }
    freeifaddrs(all);
    g_array_sort(addresses, remote_address_compare);
    for (guint i = 0; i < addresses->len; i++) {
        RemoteAddress *a = &g_array_index(addresses, RemoteAddress, i);
        g_autofree char *label = g_strdup_printf("%s  —  %s (%s)", a->ip, a->name, a->kind);
        gtk_combo_box_text_append(combo, a->ip, label);
    }
}
