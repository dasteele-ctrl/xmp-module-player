#include "uade_audio.h"
#include <uade/uade.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Filename aliases from the bundled UADE eagleplayer.conf, plus legacy aliases. */
const char *const uade_name_tags[] = {
    "tfhd1.5", "tfmx1.5", "!pm!", "40a", "40b", "41a", "50a", "60a", "61a", "aam", "abk",
    "ac1", "ac1d", "adpcm", "adsc", "agi", "ahx", "alp", "amc", "aon", "aon4", "aon8",
    "aps", "arp", "ash", "ast", "aval", "avp", "bd", "bds", "bfc", "bp", "bp3", "bsi",
    "bss", "bye", "chan", "cin", "cm", "core", "cp", "cplx", "crb", "cus", "cust",
    "custom", "dat", "db", "dh", "di", "digi", "dl", "dl_deli", "dlm1", "dlm2", "dln",
    "dm", "dm1", "dm2", "dmu", "dmu2", "dns", "doda", "dp", "dsc", "dsr", "dss", "dum",
    "dw", "dwold", "dz", "ea", "emod", "ems", "emsv6", "eu", "ex", "fc", "fc-bsi", "fc-m",
    "fc13", "fc14", "fc3", "fc4", "fcm", "fp", "fred", "ft", "ftm", "fuz", "fuzz", "fw",
    "glue", "gm", "gmc", "gray", "gv", "hd", "hip", "hip7", "hipc", "hmc", "hn", "hot",
    "hrt", "hrt!", "hst", "hvl", "ice", "ims", "is", "is20", "ism", "it1", "jam", "jb",
    "jc", "jcb", "jcbo", "jd", "jmf", "jo", "jp", "jpn", "jpnd", "jpo", "jpold", "js",
    "jt", "kef", "kef7", "kh", "kim", "kris", "krs", "ksm", "lax", "lion", "lme", "ma",
    "max", "mc", "mcmd", "mcmd_org", "mco", "mcr", "md", "mdat", "mdst", "med", "mexxmp",
    "mfp", "mg", "midi", "mk2", "mkii", "mkiio", "ml", "mm4", "mm8", "mmd0", "mmd1",
    "mmd2", "mmd3", "mmdc", "mms", "mod", "mod15", "mod15_mst", "mod15_st-iv",
    "mod15_ust", "mod3", "mod_adsc4", "mod_comp", "mod_doc", "mod_flt4", "mod_ntk",
    "mod_ntk1", "mod_ntk2", "mod_ntkamp", "mok", "mon", "mon_old", "mosh", "mpro", "mso",
    "mth", "mtp2", "mug", "mug2", "mus", "mw", "noisepacker2", "noisepacker3", "np",
    "np1", "np2", "np3", "npp", "nr", "nru", "ntp", "ntpk", "octamed", "okt", "okta",
    "oldw", "one", "osp", "oss", "p10", "p21", "p30", "p40a", "p40b", "p41a", "p4x",
    "p50a", "p5a", "p5x", "p60", "p60a", "p61", "p61a", "p6x", "pap", "pat", "pha", "pin",
    "pm", "pm0", "pm01", "pm1", "pm10c", "pm18a", "pm2", "pm20", "pm4", "pm40", "pmz",
    "pn", "polk", "powt", "pp10", "pp20", "pp21", "pp30", "ppk", "pr1", "pr2", "prom",
    "prt", "pru", "pru1", "pru2", "prun", "prun1", "prun2", "ps", "psa", "psf", "pt",
    "ptm", "puma", "pvp", "pwr", "pyg", "pygm", "pygmy", "qc", "qpa", "qts", "rh", "rho",
    "riff", "rj", "rjp", "rk", "rkb", "rmc", "s-c", "s7g", "sa", "sa-p", "sa_old", "sas",
    "sb", "sc", "scn", "scr", "sct", "scumm", "sdata", "sdc", "sdr", "sfx", "sfx13",
    "sfx20", "sg", "sid", "sid1", "sid2", "sj", "sjs", "skt", "skyt", "sm", "sm1", "sm2",
    "sm3", "smn", "smod", "smpro", "smus", "sndmon", "sng", "snk", "snt", "snt!", "snx",
    "soc", "sog", "sonic", "spl", "sqt", "ss", "st", "st2", "st26", "st30", "star",
    "stpk", "sun", "syn", "synmod", "tcb", "tf", "tfhd7v", "tfhdpro", "tfmx", "tfmx7v",
    "tfmxpro", "tfx", "thm", "thn", "thx", "tiny", "tits", "tme", "tmk", "tp", "tp1",
    "tp2", "tp3", "tpu", "trc", "tro", "tron", "tronic", "tw", "two", "uds", "ufo", "un2",
    "unic", "unic2", "vss", "wb", "wn", "xan", "xann", "ym", "ymst", "zen", NULL
};

struct UadeAudio {
    struct uade_state *state;
    char title[256];
    char format[256];
    int64_t duration_ms;
};

UadeAudio *uade_audio_open(const char *path, char *error, size_t size)
{
    UadeAudio *a = calloc(1, sizeof(*a));
    struct uade_config *config = uade_new_config();
    if (!a || !config) {
        free(a);
        free(config);
        if (size) snprintf(error, size, "Could not allocate UADE state.");
        return NULL;
    }
    uade_config_set_option(config, UC_FREQUENCY, "44100");
    /* Overrides also allow a future AppImage to use its bundled runtime. */
    const char *base = getenv("XMP_UADE_BASE_DIR");
    const char *core = getenv("XMP_UADE_CORE");
    const char *appdir = getenv("APPDIR");
    char bundled_base[4096], bundled_core[4096];
    char bundled_score[4096], bundled_config[4096];
    if (appdir && *appdir) {
        int nbase = snprintf(bundled_base, sizeof(bundled_base), "%s/usr/share/uade", appdir);
        int ncore = snprintf(bundled_core, sizeof(bundled_core), "%s/usr/bin/uadecore", appdir);
        if (nbase < 0 || nbase >= (int)sizeof(bundled_base) ||
            ncore < 0 || ncore >= (int)sizeof(bundled_core)) {
            if (size) snprintf(error, size, "AppImage runtime path is too long.");
            free(config);
            free(a);
            return NULL;
        }
        if (!base || !*base) base = bundled_base;
        if (!core || !*core) core = bundled_core;
        /* score and uaerc otherwise can retain the build machine's prefix. */
        int nscore = snprintf(bundled_score, sizeof(bundled_score), "%s/score", base);
        int nconfig = snprintf(bundled_config, sizeof(bundled_config), "%s/uaerc", base);
        if (nscore < 0 || nscore >= (int)sizeof(bundled_score) ||
            nconfig < 0 || nconfig >= (int)sizeof(bundled_config)) {
            if (size) snprintf(error, size, "UADE runtime path is too long.");
            free(config);
            free(a);
            return NULL;
        }
        uade_config_set_option(config, UC_SCORE_FILE, bundled_score);
        uade_config_set_option(config, UC_UAE_CONFIG_FILE, bundled_config);
    }
    if (base && *base) uade_config_set_option(config, UC_BASE_DIR, base);
    if (core && *core) uade_config_set_option(config, UC_UADECORE_FILE, core);
    a->state = uade_new_state(config);
    free(config);
    if (!a->state || !uade_is_our_file(path, a->state) ||
        uade_play(path, -1, a->state) != 1) {
        if (size) snprintf(error, size, "UADE could not play this file; check its companion files and UADE runtime.");
        uade_audio_close(a);
        return NULL;
    }
    if (uade_get_sampling_rate(a->state) != 44100) {
        if (size) snprintf(error, size, "UADE did not select 44100 Hz.");
        uade_audio_close(a);
        return NULL;
    }
    const struct uade_song_info *info = uade_get_song_info(a->state);
    if (info) {
        snprintf(a->title, sizeof(a->title), "%s", info->modulename);
        snprintf(a->format, sizeof(a->format), "%s", info->formatname);
        if (isfinite(info->duration) && info->duration > 0 &&
            info->duration < (double)INT64_MAX / 1000.0)
            a->duration_ms = (int64_t)(info->duration * 1000.0);
    }
    return a;
}

int uade_audio_read(UadeAudio *a, int16_t *pcm, int frames)
{
    if (!a || !pcm || frames <= 0) return -1;
    ssize_t bytes = uade_read(pcm, (size_t)frames * UADE_BYTES_PER_FRAME, a->state);
    struct uade_notification note;
    while (uade_read_notification(&note, a->state))
        uade_cleanup_notification(&note);
    if (bytes < 0 || bytes % UADE_BYTES_PER_FRAME) return -1;
    return (int)(bytes / UADE_BYTES_PER_FRAME);
}

const char *uade_audio_title(const UadeAudio *a) { return a ? a->title : ""; }
const char *uade_audio_format(const UadeAudio *a) { return a ? a->format : ""; }
int64_t uade_audio_duration_ms(const UadeAudio *a) { return a ? a->duration_ms : 0; }
int uade_audio_seek(UadeAudio *a, int64_t ms)
{
    if (!a || ms < 0) return -1;
    return uade_seek(UADE_SEEK_SONG_RELATIVE, ms / 1000.0, 0, a->state);
}
void uade_audio_close(UadeAudio *a)
{
    if (!a) return;
    /* cleanup includes stop; do not issue a second stop on a failed state. */
    if (a->state) uade_cleanup_state(a->state);
    free(a);
}
