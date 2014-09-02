#include <math.h>
#include <stdio.h>

#include <audacious/input.h>
#include <audacious/plugin.h>
#include <libaudcore/audstrings.h>

#include <uade/uade.h>

#define DEBUG 1

#ifdef DEBUG
# define DBG(fmt,...) printf(fmt, ## __VA_ARGS__)
#else
# define DBG(fmt,...) while (0)
#endif

#define ERR(fmt,...) fprintf(stderr, fmt, ## __VA_ARGS__)

int parse_uri(const char *uri, char **path, char **name) {
    int subsong;
    const char *tmpName, *sub;
    char *tmpPath = uri_to_filename(uri);

    uri_parse(uri, &tmpName, NULL, &sub, &subsong);

    if (path) {
        *path = str_nget(tmpPath, strlen(tmpPath) - strlen(sub));
    }

    if (name) {
        *name = str_nget(tmpName, strlen(tmpName) - strlen(sub));
    }

    str_unref(tmpPath);

    return subsong <= 0 ? 1 : subsong;
}

bool_t plugin_is_our_file_from_vfs (const char *uri, VFSFile *file) {
    DBG("uade_plugin_is_our_file_from_vfs %s\n", uri);

    bool_t is_our_file = FALSE;
    char *path;
    struct uade_state *state = uade_new_state(NULL);

    parse_uri(uri, &path, NULL);

    if (state && path) {
        is_our_file = uade_is_our_file(path, state);
    }

    uade_cleanup_state(state);
    str_unref(path);

    return is_our_file;
}

void update_tuple(Tuple *tuple, char *name, int subsong, struct uade_state *state) {
    const struct uade_song_info* info = uade_get_song_info(state);

    tuple_set_str(tuple, FIELD_TITLE,
            strnlen(info->modulename, 256) > 0 ? info->modulename : name);
    tuple_set_str(tuple, FIELD_CODEC,
            strnlen(info->formatname, 256) > 0 ? info->formatname : info->playername);
    tuple_set_int(tuple, FIELD_LENGTH, info->duration * 1000);

    if (info->subsongs.max > 1) {
        tuple_set_int(tuple, FIELD_SUBSONG_NUM, info->subsongs.max);
        tuple_set_int(tuple, FIELD_SUBSONG_ID, subsong);
        tuple_set_int(tuple, FIELD_TRACK_NUMBER, subsong);
        tuple_set_subtunes(tuple, info->subsongs.max, NULL);
    }
}

Tuple * plugin_probe_for_tuple (const char *uri, VFSFile *file) {
    DBG("uade_plugin_probe_for_tuple %s\n", uri);

    char *path, *name;
    int subsong;
    Tuple *tuple = tuple_new_from_filename(uri);
    struct uade_state *state = uade_new_state(NULL);

    subsong = parse_uri(uri, &path, &name);

    if (state && path && tuple) {
        if (uade_play(path, subsong, state) == 1) {
            update_tuple(tuple, name, subsong, state);
        }
    }

    uade_cleanup_state(state);
    str_unref(path);
    str_unref(name);

    return tuple;
}

ssize_t render_audio(void *buffer, struct uade_state *state) {
    struct uade_notification n;
    ssize_t nbytes = uade_read(buffer, sizeof buffer, state);
    while (uade_read_notification(&n, state)) {
        switch (n.type) {
            case UADE_NOTIFICATION_MESSAGE:
                DBG("Amiga message: %s\n", n.msg);
                break;
            case UADE_NOTIFICATION_SONG_END:
                DBG("%s: %s\n", n.song_end.happy ? "song end" : "bad song end",
                        n.song_end.reason);
                nbytes = 0;
                break;
            default:
                DBG("Unknown notification type from libuade\n");
        }
        uade_cleanup_notification(&n);
    }
    return nbytes;
}

int playback_loop(char *buffer, struct uade_state* state) {
    while (!aud_input_check_stop()) {
        ssize_t nbytes = render_audio(buffer, state);
        if (nbytes < 0) {
            ERR("Playback error.\n");
            return FALSE;
        } else if (nbytes == 0) {
            DBG("Song end.\n");
            break;
        }
        aud_input_write_audio(buffer, nbytes);
    }
    return TRUE;
}

bool_t plugin_play (const char *uri, VFSFile *file) {
    DBG("uade_plugin_play %s\n", uri);

    char *path, *name, buffer[4096];
    int subsong, rate;
    bool_t ret = FALSE;
    struct uade_state *state = NULL;

    subsong = parse_uri(uri, &path, &name);

    state = uade_new_state(NULL);
    if (!state) {
        ERR("Could not init uade state\n");
        goto out;
    }

    rate = uade_get_sampling_rate(state);

    if (!aud_input_open_audio(FMT_S16_NE, rate, 2)) {
        ERR("Could not open audio with rate %d\n", rate);
        goto out;
    }

    if (uade_play(path, subsong, state) == 1) {
        Tuple *tuple = tuple_new_from_filename(uri);
        update_tuple(tuple, name, subsong, state);
        aud_input_set_tuple(tuple);

        ret = playback_loop(buffer, state);
    }

out:
    uade_cleanup_state(state);
    str_unref(path);
    str_unref(name);

    return ret;
}

AUD_INPUT_PLUGIN (
    .name = "UADE",
    .about_text = "Plugin for UADE",
    .have_subtune = TRUE,
    .is_our_file_from_vfs = plugin_is_our_file_from_vfs,
    .probe_for_tuple = plugin_probe_for_tuple,
    .play = plugin_play,
    //.priority = 10
)
