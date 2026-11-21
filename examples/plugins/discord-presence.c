/* Discord Rich Presence: shows the song playing in scmp on your Discord profile as
 * "Listening to <title>", with the artist, album and a progress bar.
 *
 * Setup:
 *   1. Create an application at https://discord.com/developers/applications (its name, for example
 *      "scmp", shows on the activity card) and copy its Application ID.
 *   2. Save the ID in ~/.scmp/discord-client-id, or set SCMP_DISCORD_CLIENT_ID.
 *   3. Optional: under Rich Presence > Art Assets, upload a picture named "scmp" for the large image.
 *   4. Enable discord-presence with P in scmp, and keep the Discord desktop app running.
 *
 * The activity follows scmp's own player: it clears when the song is paused, ends, or scmp quits.
 * All talking to Discord happens on a thread of its own so the UI never waits for it. */

#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <scmp/plugin.h>

#define FIELD 129        /* Discord accepts up to 128 characters per text field */
#define SEND_GAP 2.0     /* seconds between updates; Discord allows about 5 every 20 seconds */
#define RETRY_GAP 15.0   /* seconds between connection attempts while Discord is closed */
#define IO_TIMEOUT 2     /* seconds before a stuck read or write gives up */
#define LARGE_IMAGE "scmp"

#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

enum { OP_HANDSHAKE = 0, OP_FRAME = 1, OP_CLOSE = 2, OP_PING = 3, OP_PONG = 4 };

typedef struct {
    int loaded;
    int playing;
    char path[1024];
    char title[FIELD];
    char artist[FIELD];
    char album[FIELD];
    double duration;
    double started; /* wall-clock seconds at which the track was at 0:00 */
} Track;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int worker_started;
static int stopping;
static Track wanted;            /* latest state from the UI thread */
static unsigned wanted_version; /* bumped whenever wanted changes in a way Discord should see */

/* ---- time ---- */

static double wall_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + tv.tv_usec / 1e6;
}

static double mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static void wait_for(double seconds)
{
    const double end = wall_now() + seconds;
    struct timespec until;
    until.tv_sec = (time_t)end;
    until.tv_nsec = (long)((end - (double)until.tv_sec) * 1e9);
    pthread_cond_timedwait(&wake, &lock, &until);
}

/* ---- text ---- */

/* Copies at most size - 1 bytes without cutting a UTF-8 character in half. */
static void copy_text(char *out, size_t size, const char *text)
{
    size_t n = text ? strlen(text) : 0;
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) --n;
    }
    if (n) memcpy(out, text, n);
    out[n] = 0;
}

typedef struct {
    char data[4096];
    size_t used;
} Buffer;

static void add(Buffer *b, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void add(Buffer *b, const char *format, ...)
{
    if (b->used >= sizeof b->data) return;
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(b->data + b->used, sizeof b->data - b->used, format, args);
    va_end(args);
    if (n > 0) b->used += (size_t)n;
    if (b->used >= sizeof b->data) b->used = sizeof b->data - 1;
}

/* Adds "key":"text" as JSON. Discord rejects text shorter than two characters, so pad it. */
static void add_string(Buffer *b, const char *key, const char *text)
{
    add(b, ",\"%s\":\"", key);
    for (const unsigned char *c = (const unsigned char *)text; *c; ++c) {
        if (*c == '"' || *c == '\\') add(b, "\\%c", *c);
        else if (*c < 0x20) add(b, "\\u%04x", *c);
        else add(b, "%c", *c);
    }
    add(b, "%s\"", strlen(text) < 2 ? "\\u200b\\u200b" : "");
}

/* ---- Discord IPC: frames of [opcode u32][length u32][JSON], little endian ---- */

static int send_all(int fd, const void *data, size_t size)
{
    const char *p = data;
    while (size) {
        const ssize_t n = send(fd, p, size, SEND_FLAGS);
        if (n <= 0) return 0;
        p += n;
        size -= (size_t)n;
    }
    return 1;
}

static int recv_all(int fd, void *data, size_t size)
{
    char *p = data;
    while (size) {
        const ssize_t n = recv(fd, p, size, 0);
        if (n <= 0) return 0;
        p += n;
        size -= (size_t)n;
    }
    return 1;
}

static void put_u32(unsigned char *out, uint32_t v)
{
    out[0] = (unsigned char)v;
    out[1] = (unsigned char)(v >> 8);
    out[2] = (unsigned char)(v >> 16);
    out[3] = (unsigned char)(v >> 24);
}

static uint32_t get_u32(const unsigned char *in)
{
    return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}

static int write_frame(int fd, uint32_t op, const char *json, size_t length)
{
    unsigned char header[8];
    put_u32(header, op);
    put_u32(header + 4, (uint32_t)length);
    return send_all(fd, header, sizeof header) && send_all(fd, json, length);
}

/* Reads one frame into out (truncated to size - 1, always terminated). */
static int read_frame(int fd, uint32_t *op, char *out, size_t size)
{
    unsigned char header[8];
    if (!recv_all(fd, header, sizeof header)) return 0;
    *op = get_u32(header);
    uint32_t length = get_u32(header + 4);
    const size_t keep = length < size - 1 ? length : size - 1;
    if (!recv_all(fd, out, keep)) return 0;
    out[keep] = 0;
    for (length -= (uint32_t)keep; length;) {
        char skip[512];
        const size_t n = length < sizeof skip ? length : sizeof skip;
        if (!recv_all(fd, skip, n)) return 0;
        length -= (uint32_t)n;
    }
    return 1;
}

static int handshake(int fd, const char *client_id)
{
    char json[128];
    const int n = snprintf(json, sizeof json, "{\"v\":1,\"client_id\":\"%s\"}", client_id);
    if (!write_frame(fd, OP_HANDSHAKE, json, (size_t)n)) return 0;
    uint32_t op;
    char reply[2048];
    return read_frame(fd, &op, reply, sizeof reply) && op == OP_FRAME && strstr(reply, "\"READY\"");
}

static int open_socket(const char *path)
{
    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof address.sun_path) return -1;
    strcpy(address.sun_path, path);
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    const struct timeval timeout = {IO_TIMEOUT, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
#ifdef SO_NOSIGPIPE
    const int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on); /* a closed Discord must not kill scmp */
#endif
    if (connect(fd, (const struct sockaddr *)&address, sizeof address) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Discord listens on discord-ipc-0..9 in the temp or runtime folder (or a Flatpak/Snap one). */
static int connect_discord(const char *client_id)
{
    const char *bases[] = {getenv("XDG_RUNTIME_DIR"), getenv("TMPDIR"), getenv("TMP"), getenv("TEMP"), "/tmp"};
    const char *folders[] = {"", "app/com.discordapp.Discord/", "snap.discord/"};
    for (size_t b = 0; b < sizeof bases / sizeof *bases; ++b) {
        const char *base = bases[b];
        if (!base || !*base) continue;
        const char *slash = base[strlen(base) - 1] == '/' ? "" : "/";
        for (size_t f = 0; f < sizeof folders / sizeof *folders; ++f) {
            for (int i = 0; i < 10; ++i) {
                char path[512];
                snprintf(path, sizeof path, "%s%s%sdiscord-ipc-%d", base, slash, folders[f], i);
                const int fd = open_socket(path);
                if (fd < 0) continue;
                if (handshake(fd, client_id)) return fd;
                close(fd);
            }
        }
    }
    return -1;
}

/* Answers pings and drops replies. Returns 0 once Discord has closed the connection. */
static int drain(int fd)
{
    struct pollfd p = {fd, POLLIN, 0};
    while (poll(&p, 1, 0) > 0) {
        if (p.revents & (POLLHUP | POLLERR)) return 0;
        uint32_t op;
        char payload[2048];
        if (!read_frame(fd, &op, payload, sizeof payload) || op == OP_CLOSE) return 0;
        if (op == OP_PING && !write_frame(fd, OP_PONG, payload, strlen(payload))) return 0;
    }
    return 1;
}

static int send_activity(int fd, const Track *track)
{
    static unsigned nonce;
    Buffer b = {{0}, 0};
    add(&b, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%u\",\"args\":{\"pid\":%d", ++nonce, (int)getpid());
    if (track->loaded && track->playing) {
        /* type 2 is "Listening to"; status_display_type 2 puts the title (details) in the member list. */
        add(&b, ",\"activity\":{\"type\":2,\"status_display_type\":2");
        add_string(&b, "details", track->title);
        if (*track->artist) add_string(&b, "state", track->artist);
        const long long start = llround(track->started * 1000.0);
        add(&b, ",\"timestamps\":{\"start\":%lld", start);
        if (track->duration > 0) add(&b, ",\"end\":%lld", start + llround(track->duration * 1000.0));
        add(&b, "},\"assets\":{\"large_image\":\"" LARGE_IMAGE "\"");
        if (*track->album) add_string(&b, "large_text", track->album);
        add(&b, "}}");
    }
    add(&b, "}}");
    return write_frame(fd, OP_FRAME, b.data, b.used);
}

/* ---- settings ---- */

static int valid_id(const char *id)
{
    const size_t n = strlen(id);
    if (n < 15 || n > 25) return 0;
    for (size_t i = 0; i < n; ++i)
        if (id[i] < '0' || id[i] > '9') return 0;
    return 1;
}

/* Read on every connection attempt, so adding the ID later needs no restart. */
static int read_client_id(char *out, size_t size)
{
    const char *env = getenv("SCMP_DISCORD_CLIENT_ID");
    if (env && *env) {
        copy_text(out, size, env);
        return valid_id(out);
    }
    const char *home = getenv("HOME");
    if (!home) return 0;
    char path[512];
    snprintf(path, sizeof path, "%s/.scmp/discord-client-id", home);
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    char line[64] = {0};
    const int ok = fgets(line, sizeof line, file) != NULL;
    fclose(file);
    if (!ok) return 0;
    line[strcspn(line, " \t\r\n")] = 0;
    copy_text(out, size, line);
    return valid_id(out);
}

/* ---- worker thread ---- */

static void *run(void *unused)
{
    (void)unused;
    int fd = -1;
    unsigned sent_version = 0;
    double last_send = -1e9, last_try = -1e9;
    pthread_mutex_lock(&lock);
    while (!stopping) {
        const Track track = wanted;
        const unsigned version = wanted_version;
        pthread_mutex_unlock(&lock);

        const double now = mono_now();
        if (fd < 0 && now - last_try >= RETRY_GAP) {
            char client_id[32];
            last_try = now;
            if (read_client_id(client_id, sizeof client_id)) fd = connect_discord(client_id);
            if (fd >= 0) sent_version = version - 1; /* a new connection starts with no activity */
        }
        if (fd >= 0 && !drain(fd)) {
            close(fd);
            fd = -1;
        }
        int pending = version != sent_version;
        if (fd >= 0 && pending && now - last_send >= SEND_GAP) {
            if (send_activity(fd, &track)) {
                sent_version = version;
                last_send = now;
                pending = 0;
            } else {
                close(fd);
                fd = -1;
            }
        }

        pthread_mutex_lock(&lock);
        if (!stopping && wanted_version == version) wait_for(pending && fd >= 0 ? 0.25 : 1.0);
    }
    pthread_mutex_unlock(&lock);
    if (fd >= 0) close(fd); /* Discord clears the activity when the connection closes */
    return NULL;
}

__attribute__((destructor)) static void stop_worker(void)
{
    pthread_mutex_lock(&lock);
    stopping = 1;
    const int started = worker_started;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
    if (started) pthread_join(worker, NULL);
}

/* ---- scmp hooks ---- */

static int changed(const Track *old, const Track *next)
{
    if (old->loaded != next->loaded || old->playing != next->playing) return 1;
    if (!next->loaded) return 0;
    if (strcmp(old->path, next->path) || strcmp(old->title, next->title) || strcmp(old->artist, next->artist) ||
        strcmp(old->album, next->album))
        return 1;
    if (fabs(old->duration - next->duration) > 1.0) return 1;
    /* While playing, the start time only moves when the user seeks. */
    return next->playing && fabs(old->started - next->started) > 2.0;
}

void scmp_plugin_now_playing(const ScmpNowPlaying *now)
{
    Track next;
    memset(&next, 0, sizeof next);
    if (now && now->title) {
        next.loaded = 1;
        next.playing = now->playing != 0;
        copy_text(next.path, sizeof next.path, now->path);
        copy_text(next.title, sizeof next.title, now->title);
        copy_text(next.artist, sizeof next.artist, now->artist);
        copy_text(next.album, sizeof next.album, now->album);
        next.duration = now->duration > 0 ? now->duration : 0;
        next.started = wall_now() - (now->elapsed > 0 ? now->elapsed : 0);
    }
    pthread_mutex_lock(&lock);
    /* Start talking to Discord only once something plays, not when the plugin manager loads us. */
    if (!worker_started && !stopping && next.loaded)
        worker_started = pthread_create(&worker, NULL, run, NULL) == 0;
    if (changed(&wanted, &next)) {
        wanted = next;
        ++wanted_version;
        pthread_cond_signal(&wake);
    }
    pthread_mutex_unlock(&lock);
}

static const ScmpPluginV1 plugin = {SCMP_PLUGIN_ABI_VERSION, "Discord", NULL, NULL, NULL};

const ScmpPluginV1 *scmp_plugin_v1(void)
{
    return &plugin;
}
