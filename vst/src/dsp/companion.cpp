/* companion.cpp — the HTTP listener behind the companion page.
 *
 * Threading: one listener thread for the whole process, started by the first attach().
 * The audio thread only ever stores into the static slots (a seqlock, so a reader never
 * sees half an update); the listener only ever loads from them. No instance pointer ever
 * crosses into the listener, which is what keeps destroy_instance() free of teardown
 * ordering problems.
 *
 * Network: read-only. GET/HEAD only, no cookies, no state, no parameters written back —
 * the page watches the tape, it does not play it.
 */
#include "companion.h"
#include "companion_c.h"

#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "companion_page.h"

namespace djf2 {
namespace {

unsigned now_ms();

/* The address a phone should open. Cached for 10 s: /state is polled at 7 Hz and
 * getifaddrs() allocates, so walking the interface list 250 times a minute on the HTTP
 * thread is waste. The cache also survives a DHCP change within ten seconds. */
void local_ip(char *out, size_t cap) {
  static char cached[16] = "";
  static unsigned cached_at = 0;
  if (cached[0]) {
    const unsigned now = now_ms();
    if (now - cached_at < 10000u) { std::snprintf(out, cap, "%s", cached); return; }
    cached_at = now;
  } else {
    cached_at = now_ms();
  }
  out[0] = 0;
  struct ifaddrs *ifap = nullptr;
  if (getifaddrs(&ifap) == 0) {
    for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
      if (!(ifa->ifa_flags & IFF_UP)) continue;
      struct sockaddr_in *a4 = (struct sockaddr_in *)ifa->ifa_addr;
      if (a4->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) continue;
      inet_ntop(AF_INET, &a4->sin_addr, out, cap);
      break;
    }
    freeifaddrs(ifap);
  }
  if (!out[0]) std::snprintf(out, cap, "127.0.0.1");
  std::snprintf(cached, sizeof cached, "%s", out);
}

/* "add to home screen" parts, so the panel opens like an app (VADER does the same). */
const char kManifest[] =
    "{\"name\":\"MPC DJ FX 2\",\"short_name\":\"Multihead\",\"start_url\":\"/\","
    "\"display\":\"standalone\",\"orientation\":\"any\",\"background_color\":\"#080a07\","
    "\"theme_color\":\"#080a07\",\"icons\":[{\"src\":\"/icon.svg\",\"sizes\":\"any\","
    "\"type\":\"image/svg+xml\"}]}";
const char kIconSvg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\">"
    "<rect width=\"64\" height=\"64\" rx=\"13\" fill=\"#0b0d09\"/>"
    "<circle cx=\"32\" cy=\"32\" r=\"23\" fill=\"#14170f\" stroke=\"#c9a227\" stroke-width=\"3\"/>"
    "<circle cx=\"32\" cy=\"32\" r=\"15\" fill=\"none\" stroke=\"#6b4f2c\" stroke-width=\"7\"/>"
    "<circle cx=\"32\" cy=\"32\" r=\"4\" fill=\"#c9a227\"/>"
    "<g stroke=\"#2fbf63\" stroke-width=\"4\" stroke-linecap=\"round\">"
    "<path d=\"M32 23v-6\"/><path d=\"M40 37l5 3\"/><path d=\"M24 37l-5 3\"/></g></svg>";

const int kDefaultPort = 8102;
const int kPortTries   = 10;
/* A GET request line is < 200 bytes; the browser sends a few hundred more of headers.
 * 2 KB is generous and keeps a slow or hostile client from growing the stack. */
const int kReqMax = 2048;

struct Slot {
  std::atomic<unsigned> seq, attached, stamp_ms;
  std::atomic<float> vu;
  std::atomic<float> peak;
  std::atomic<float> delay_ms;
  std::atomic<float> bpm;
  std::atomic<int> bpm_seen;
  std::atomic<float> mix;
  std::atomic<float> iso_lo;
  std::atomic<float> iso_mid;
  std::atomic<float> iso_hi;
  std::atomic<float> iso_lo_x;
  std::atomic<float> iso_hi_x;
  std::atomic<float> dj_filter;
  std::atomic<float> dj_reso;
  std::atomic<float> rep_gate;
  std::atomic<float> rep_chance;
  std::atomic<float> rep_mix;
  std::atomic<float> dly_time;
  std::atomic<float> dly_fbk;
  std::atomic<float> dly_tone;
  std::atomic<float> dly_mix;
  std::atomic<float> verb_amount;
  std::atomic<float> verb_decay;
  std::atomic<float> verb_damp;
  std::atomic<float> verb_predelay;
  std::atomic<float> hp_hz;
  std::atomic<float> hp_reso;
  std::atomic<float> fl_rate;
  std::atomic<float> fl_depth;
  std::atomic<float> fl_fbk;
  std::atomic<float> fl_mix;
  std::atomic<float> gr_rate;
  std::atomic<float> gr_time;
  std::atomic<float> gr_spray;
  std::atomic<float> gr_mix;
  std::atomic<float> mac_lo;
  std::atomic<float> mac_mid;
  std::atomic<float> mac_hi;
  std::atomic<float> mac_lpf;
  std::atomic<float> mac_glitch;
  std::atomic<float> mac_verb;
  std::atomic<float> mac_fade;
  std::atomic<float> mac_fill;
  std::atomic<float> output_gain;
  std::atomic<float> rep_env;
  std::atomic<float> dly_env;
  std::atomic<float> verb_env;
  std::atomic<int> rep_mode;
  std::atomic<int> rep_count;
  std::atomic<int> fill_mode;
  std::atomic<int> dly_sync;
  std::atomic<int> dly_div;
  std::atomic<int> dly_pingpong;
  std::atomic<int> verb_mode;
  std::atomic<int> limiter;
};

Slot g_slots[kSlots];

/* Last requests, for /diag. Written and read by the server thread only (one at a time),
 * so no lock: this is the "what actually arrived" record when a phone shows something the
 * desktop does not. Fixed size, no allocation. */
const int kDiagLines = 8;
struct DiagLine {
  unsigned ms;
  char remote[16];
  char method[8];
  char target[96];
  char ua[40];
};
DiagLine g_diag[kDiagLines];
int g_diag_at = 0;
unsigned g_requests = 0;
unsigned g_started_ms = 0;

std::atomic<int> g_instances(0);   /* how many instances are alive right now */

/* Command ring: one producer (the HTTP thread, one connection at a time), one consumer (the
 * audio thread). Indices are free-running counters; a full ring drops the newest request,
 * which is the safe end for a control surface (the next tap wins). */
const int kCmdRing = 32;
Command g_cmd[kSlots][kCmdRing];
std::atomic<unsigned> g_cmd_head[kSlots];   /* written requests, per instance */
std::atomic<unsigned> g_cmd_tail[kSlots];   /* taken requests, per instance */
std::atomic<int> g_rw(1);              /* writes allowed (DJF2_HTTP_RW=0 turns them off) */
const char *const *g_keys = nullptr;   /* the module's contract, handed over at load */
int g_key_count = 0;
std::atomic<int> g_stop(0);
std::atomic<int> g_running(0);
std::atomic<int> g_port(0);
pthread_t g_thread;
pthread_mutex_t g_start_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_once_t g_init_once = PTHREAD_ONCE_INIT;

unsigned now_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned)(ts.tv_sec * 1000u + (unsigned)(ts.tv_nsec / 1000000));
}

void init_slots() {
  for (int i = 0; i < kSlots; ++i) {
    Slot &s = g_slots[i];
    s.seq.store(0); s.attached.store(0); s.stamp_ms.store(0);
    s.vu.store(0.0f);
    s.peak.store(0.0f);
    s.delay_ms.store(0.0f);
    s.bpm.store(0.0f);
    s.mix.store(0.0f);
    s.iso_lo.store(0.0f);
    s.iso_mid.store(0.0f);
    s.iso_hi.store(0.0f);
    s.iso_lo_x.store(0.0f);
    s.iso_hi_x.store(0.0f);
    s.dj_filter.store(0.0f);
    s.dj_reso.store(0.0f);
    s.rep_gate.store(0.0f);
    s.rep_chance.store(0.0f);
    s.rep_mix.store(0.0f);
    s.dly_time.store(0.0f);
    s.dly_fbk.store(0.0f);
    s.dly_tone.store(0.0f);
    s.dly_mix.store(0.0f);
    s.verb_amount.store(0.0f);
    s.verb_decay.store(0.0f);
    s.verb_damp.store(0.0f);
    s.verb_predelay.store(0.0f);
    s.hp_hz.store(0.0f);
    s.hp_reso.store(0.0f);
    s.fl_rate.store(0.0f);
    s.fl_depth.store(0.0f);
    s.fl_fbk.store(0.0f);
    s.fl_mix.store(0.0f);
    s.gr_rate.store(0.0f);
    s.gr_time.store(0.0f);
    s.gr_spray.store(0.0f);
    s.gr_mix.store(0.0f);
    s.mac_lo.store(0.0f);
    s.mac_mid.store(0.0f);
    s.mac_hi.store(0.0f);
    s.mac_lpf.store(0.0f);
    s.mac_glitch.store(0.0f);
    s.mac_verb.store(0.0f);
    s.mac_fade.store(0.0f);
    s.mac_fill.store(0.0f);
    s.output_gain.store(0.0f);
    s.rep_env.store(0.0f);
    s.dly_env.store(0.0f);
    s.verb_env.store(0.0f);
    s.rep_mode.store(0);
    s.rep_count.store(0);
    s.fill_mode.store(0);
    s.dly_sync.store(0);
    s.dly_div.store(0);
    s.dly_pingpong.store(0);
    s.verb_mode.store(0);
    s.limiter.store(0);
  }
  /* The documented read-only switch: DJF2_HTTP_RW=0 serves the page and /state but refuses
   * every write (the reference port declared this and never read it, found 2026-10-31). */
  const char *rw = std::getenv("DJF2_HTTP_RW");
  if (rw && std::atoi(rw) == 0) g_rw.store(0);
}

/* ---- HTTP ---------------------------------------------------------------- */

int listen_on(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((unsigned short)port);
  if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0 || listen(fd, 8) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

/* MSG_NOSIGNAL: a browser that closes early must not raise SIGPIPE and take MPC down. */
bool send_all(int fd, const char *p, size_t n) {
  while (n > 0) {
    ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
    if (w <= 0) {
      if (w < 0 && errno == EINTR) continue;
      return false;
    }
    p += w;
    n -= (size_t)w;
  }
  return true;
}

void send_headers(int fd, int code, const char *reason, const char *type, size_t len,
                  bool head_only) {
  char h[256];
  int n = std::snprintf(h, sizeof h,
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Cache-Control: no-store\r\n"
                        "Connection: close\r\n\r\n",
                        code, reason, type, len);
  if (n > 0) send_all(fd, h, (size_t)n);
  (void)head_only;
}

void send_body(int fd, int code, const char *reason, const char *type, const char *body,
               size_t len, bool head_only) {
  send_headers(fd, code, reason, type, len, head_only);
  if (!head_only && len > 0) send_all(fd, body, len);
}

/* One slot, read consistently. Returns false when the writer was mid-update for all
 * attempts; the caller then answers with whatever it got (the page retries at 10 Hz). */
struct Values {
  float vu;
  float peak;
  float delay_ms;
  float bpm;
  int bpm_seen;
  float mix;
  float iso_lo;
  float iso_mid;
  float iso_hi;
  float iso_lo_x;
  float iso_hi_x;
  float dj_filter;
  float dj_reso;
  float rep_gate;
  float rep_chance;
  float rep_mix;
  float dly_time;
  float dly_fbk;
  float dly_tone;
  float dly_mix;
  float verb_amount;
  float verb_decay;
  float verb_damp;
  float verb_predelay;
  float hp_hz;
  float hp_reso;
  float fl_rate;
  float fl_depth;
  float fl_fbk;
  float fl_mix;
  float gr_rate;
  float gr_time;
  float gr_spray;
  float gr_mix;
  float mac_lo;
  float mac_mid;
  float mac_hi;
  float mac_lpf;
  float mac_glitch;
  float mac_verb;
  float mac_fade;
  float mac_fill;
  float output_gain;
  float rep_env;
  float dly_env;
  float verb_env;
  int rep_mode;
  int rep_count;
  int fill_mode;
  int dly_sync;
  int dly_div;
  int dly_pingpong;
  int verb_mode;
  int limiter;
  unsigned stamp_ms;
};

bool read_values(int idx, Values *v) {
  Slot &s = g_slots[idx];
  for (int attempt = 0; attempt < 6; ++attempt) {
    const unsigned a = s.seq.load(std::memory_order_acquire);
    if (a & 1u) continue;
    v->vu = s.vu.load(std::memory_order_relaxed);
    v->peak = s.peak.load(std::memory_order_relaxed);
    v->delay_ms = s.delay_ms.load(std::memory_order_relaxed);
    v->bpm = s.bpm.load(std::memory_order_relaxed);
    v->bpm_seen = s.bpm_seen.load(std::memory_order_relaxed);
    v->mix = s.mix.load(std::memory_order_relaxed);
    v->iso_lo = s.iso_lo.load(std::memory_order_relaxed);
    v->iso_mid = s.iso_mid.load(std::memory_order_relaxed);
    v->iso_hi = s.iso_hi.load(std::memory_order_relaxed);
    v->iso_lo_x = s.iso_lo_x.load(std::memory_order_relaxed);
    v->iso_hi_x = s.iso_hi_x.load(std::memory_order_relaxed);
    v->dj_filter = s.dj_filter.load(std::memory_order_relaxed);
    v->dj_reso = s.dj_reso.load(std::memory_order_relaxed);
    v->rep_gate = s.rep_gate.load(std::memory_order_relaxed);
    v->rep_chance = s.rep_chance.load(std::memory_order_relaxed);
    v->rep_mix = s.rep_mix.load(std::memory_order_relaxed);
    v->dly_time = s.dly_time.load(std::memory_order_relaxed);
    v->dly_fbk = s.dly_fbk.load(std::memory_order_relaxed);
    v->dly_tone = s.dly_tone.load(std::memory_order_relaxed);
    v->dly_mix = s.dly_mix.load(std::memory_order_relaxed);
    v->verb_amount = s.verb_amount.load(std::memory_order_relaxed);
    v->verb_decay = s.verb_decay.load(std::memory_order_relaxed);
    v->verb_damp = s.verb_damp.load(std::memory_order_relaxed);
    v->verb_predelay = s.verb_predelay.load(std::memory_order_relaxed);
    v->hp_hz = s.hp_hz.load(std::memory_order_relaxed);
    v->hp_reso = s.hp_reso.load(std::memory_order_relaxed);
    v->fl_rate = s.fl_rate.load(std::memory_order_relaxed);
    v->fl_depth = s.fl_depth.load(std::memory_order_relaxed);
    v->fl_fbk = s.fl_fbk.load(std::memory_order_relaxed);
    v->fl_mix = s.fl_mix.load(std::memory_order_relaxed);
    v->gr_rate = s.gr_rate.load(std::memory_order_relaxed);
    v->gr_time = s.gr_time.load(std::memory_order_relaxed);
    v->gr_spray = s.gr_spray.load(std::memory_order_relaxed);
    v->gr_mix = s.gr_mix.load(std::memory_order_relaxed);
    v->mac_lo = s.mac_lo.load(std::memory_order_relaxed);
    v->mac_mid = s.mac_mid.load(std::memory_order_relaxed);
    v->mac_hi = s.mac_hi.load(std::memory_order_relaxed);
    v->mac_lpf = s.mac_lpf.load(std::memory_order_relaxed);
    v->mac_glitch = s.mac_glitch.load(std::memory_order_relaxed);
    v->mac_verb = s.mac_verb.load(std::memory_order_relaxed);
    v->mac_fade = s.mac_fade.load(std::memory_order_relaxed);
    v->mac_fill = s.mac_fill.load(std::memory_order_relaxed);
    v->output_gain = s.output_gain.load(std::memory_order_relaxed);
    v->rep_env = s.rep_env.load(std::memory_order_relaxed);
    v->dly_env = s.dly_env.load(std::memory_order_relaxed);
    v->verb_env = s.verb_env.load(std::memory_order_relaxed);
    v->rep_mode = s.rep_mode.load(std::memory_order_relaxed);
    v->rep_count = s.rep_count.load(std::memory_order_relaxed);
    v->fill_mode = s.fill_mode.load(std::memory_order_relaxed);
    v->dly_sync = s.dly_sync.load(std::memory_order_relaxed);
    v->dly_div = s.dly_div.load(std::memory_order_relaxed);
    v->dly_pingpong = s.dly_pingpong.load(std::memory_order_relaxed);
    v->verb_mode = s.verb_mode.load(std::memory_order_relaxed);
    v->limiter = s.limiter.load(std::memory_order_relaxed);
    v->stamp_ms = s.stamp_ms.load(std::memory_order_relaxed);
    if (s.seq.load(std::memory_order_acquire) == a) return true;
  }
  return false;
}

/* The slot the page watches without ?i=: the one that published most recently. */
int pick_slot(int requested) {
  if (requested >= 0 && requested < kSlots) return requested;
  int best = -1;
  unsigned best_stamp = 0;
  for (int i = 0; i < kSlots; ++i) {
    if (!g_slots[i].attached.load(std::memory_order_relaxed)) continue;
    const unsigned st = g_slots[i].stamp_ms.load(std::memory_order_relaxed);
    if (best < 0 || st > best_stamp) {
      best = i;
      best_stamp = st;
    }
  }
  return best < 0 ? 0 : best;
}

int attached_count() {
  int n = 0;
  for (int i = 0; i < kSlots; ++i) {
    n += g_slots[i].attached.load(std::memory_order_relaxed) ? 1 : 0;
  }
  return n;
}

int state_json(char *buf, size_t cap, int requested) {
  const int idx = pick_slot(requested);
  Values v;
  std::memset(&v, 0, sizeof v);   /* read_values may give up mid-update; never print garbage */
  read_values(idx, &v);
  const bool active = g_slots[idx].attached.load(std::memory_order_relaxed) != 0;
  const unsigned age = active ? (now_ms() - v.stamp_ms) : 0u;
  char ip[16] = {0};
  local_ip(ip, sizeof ip);
  const int n = std::snprintf(
      buf, cap,
      "{\"instance\":%d,\"count\":%d,\"active\":%d,\"age_ms\":%u,\"vu\":%.3f,\"peak\":%.3f,\"delay_ms\":%.1f,\"bpm\":%.1f,\"bpm_seen\":%d,\"mix\":%.4f,\"iso_lo\":%.4f,\"iso_mid\":%.4f,\"iso_hi\":%.4f,\"iso_lo_x\":%.4f,\"iso_hi_x\":%.4f,\"dj_filter\":%.4f,\"dj_reso\":%.4f,\"rep_gate\":%.4f,\"rep_chance\":%.4f,\"rep_mix\":%.4f,\"dly_time\":%.4f,\"dly_fbk\":%.4f,\"dly_tone\":%.4f,\"dly_mix\":%.4f,\"verb_amount\":%.4f,\"verb_decay\":%.4f,\"verb_damp\":%.4f,\"verb_predelay\":%.4f,\"hp_hz\":%.4f,\"hp_reso\":%.4f,\"fl_rate\":%.4f,\"fl_depth\":%.4f,\"fl_fbk\":%.4f,\"fl_mix\":%.4f,\"gr_rate\":%.4f,\"gr_time\":%.4f,\"gr_spray\":%.4f,\"gr_mix\":%.4f,\"mac_lo\":%.4f,\"mac_mid\":%.4f,\"mac_hi\":%.4f,\"mac_lpf\":%.4f,\"mac_glitch\":%.4f,\"mac_verb\":%.4f,\"mac_fade\":%.4f,\"mac_fill\":%.4f,\"output_gain\":%.4f,\"rep_env\":%.4f,\"dly_env\":%.4f,\"verb_env\":%.4f,\"rep_mode\":%d,\"rep_count\":%d,\"fill_mode\":%d,\"dly_sync\":%d,\"dly_div\":%d,\"dly_pingpong\":%d,\"verb_mode\":%d,\"limiter\":%d,\"port\":%d,\"url\":\"http://%s:%d/\"}",
      idx, attached_count(), active ? 1 : 0, age,
      (double)v.vu,
      (double)v.peak,
      (double)v.delay_ms,
      (double)v.bpm,
      v.bpm_seen,
      (double)v.mix,
      (double)v.iso_lo,
      (double)v.iso_mid,
      (double)v.iso_hi,
      (double)v.iso_lo_x,
      (double)v.iso_hi_x,
      (double)v.dj_filter,
      (double)v.dj_reso,
      (double)v.rep_gate,
      (double)v.rep_chance,
      (double)v.rep_mix,
      (double)v.dly_time,
      (double)v.dly_fbk,
      (double)v.dly_tone,
      (double)v.dly_mix,
      (double)v.verb_amount,
      (double)v.verb_decay,
      (double)v.verb_damp,
      (double)v.verb_predelay,
      (double)v.hp_hz,
      (double)v.hp_reso,
      (double)v.fl_rate,
      (double)v.fl_depth,
      (double)v.fl_fbk,
      (double)v.fl_mix,
      (double)v.gr_rate,
      (double)v.gr_time,
      (double)v.gr_spray,
      (double)v.gr_mix,
      (double)v.mac_lo,
      (double)v.mac_mid,
      (double)v.mac_hi,
      (double)v.mac_lpf,
      (double)v.mac_glitch,
      (double)v.mac_verb,
      (double)v.mac_fade,
      (double)v.mac_fill,
      (double)v.output_gain,
      (double)v.rep_env,
      (double)v.dly_env,
      (double)v.verb_env,
      v.rep_mode,
      v.rep_count,
      v.fill_mode,
      v.dly_sync,
      v.dly_div,
      v.dly_pingpong,
      v.verb_mode,
      v.limiter,
      g_port.load(), ip, g_port.load());
  return n < 0 ? 0 : (int)((size_t)n < cap ? (size_t)n : cap);
}

/* Absolute-form targets ("GET http://host:port/path") come from proxies and from some
 * mobile browsers; matching them against "/" is what made a phone answer 404 while the
 * desktop worked (2026-11-01). Reduce them to the path. */
void normalise_target(char *target) {
  char *scheme = std::strstr(target, "://");
  if (!scheme) return;
  char *path = std::strchr(scheme + 3, '/');
  if (!path) {
    target[0] = '/'; 
    target[1] = 0;
    return;
  }
  std::memmove(target, path, std::strlen(path) + 1);
}

void remember(const char *remote, const char *method, const char *target, const char *req) {
  ++g_requests;
  DiagLine &d = g_diag[g_diag_at];
  g_diag_at = (g_diag_at + 1) % kDiagLines;
  d.ms = now_ms() - g_started_ms;
  std::snprintf(d.remote, sizeof d.remote, "%s", remote ? remote : "-");
  std::snprintf(d.method, sizeof d.method, "%s", method ? method : "-");
  std::snprintf(d.target, sizeof d.target, "%s", target ? target : "-");
  const char *ua = req ? std::strstr(req, "User-Agent: ") : nullptr;
  char ua_buf[sizeof d.ua] = {0};
  if (ua) {
    ua += 12;
    size_t n = 0;
    while (ua[n] && ua[n] != '\r' && ua[n] != '\n' && n + 1 < sizeof ua_buf) ++n;
    std::memcpy(ua_buf, ua, n);
  }
  std::snprintf(d.ua, sizeof d.ua, "%s", ua_buf);
}


/* Header NAMES are case-insensitive (RFC 7230), and browsers send them in their own case -
 * often all lowercase. A literal strstr(req, "X-TEC: 1") therefore rejected every tap from a
 * real browser ("missing X-TEC header") while curl with the exact bytes worked. Match the name
 * case-insensitively, allow the optional whitespace after the colon, and require the value 1. */
bool header_is_set(const char *req, const char *name) {
  const size_t n = std::strlen(name);
  for (const char *p = req; p && *p;) {
    const char *eol = std::strchr(p, '\n');
    const char *end = eol ? eol : p + std::strlen(p);
    bool match = (size_t)(end - p) > n;
    for (size_t i = 0; match && i < n; ++i)
      if (std::tolower((unsigned char)p[i]) != std::tolower((unsigned char)name[i])) match = false;
    if (match && p[n] == ':') {
      const char *v = p + n + 1;
      while (v < end && (*v == ' ' || *v == '\t')) ++v;
      return v < end && *v == '1';
    }
    if (!eol) break;
    p = eol + 1;
  }
  return false;
}

void handle_client(int fd, const char *remote) {
  char req[kReqMax];
  size_t used = 0;
  /* Read until the end of the headers, the buffer is full, or the client goes quiet. */
  while (used + 1 < sizeof req) {
    ssize_t r = recv(fd, req + used, sizeof req - 1 - used, 0);
    if (r < 0) {
      if (errno == EINTR) continue;
      return;
    }
    if (r == 0) break;
    used += (size_t)r;
    req[used] = 0;
    if (std::strstr(req, "\r\n\r\n") || std::strstr(req, "\n\n")) break;
  }
  req[used] = 0;
  if (used == 0) return;

  char method[8] = {0};
  char target[512] = {0};
  if (std::sscanf(req, "%7s %511s", method, target) != 2) {
    /* Not HTTP: almost always a TLS ClientHello, i.e. the browser tried https:// on our
     * plain port (Chrome's HTTPS-first does this). Say so instead of hanging up. */
    remember(remote, (unsigned char)req[0] == 0x16 ? "TLS?" : "-", "(unparsable)", nullptr);
    send_body(fd, 400, "Bad Request", "text/plain",
              "this is a plain HTTP port - use http://, not https://\n", 53, false);
    return;
  }
  remember(remote, method, target, req);
  normalise_target(target);
  const bool head_only = std::strcmp(method, "HEAD") == 0;
  const bool post = std::strcmp(method, "POST") == 0;
  if (!head_only && !post && std::strcmp(method, "GET") != 0) {
    send_body(fd, 405, "Method Not Allowed", "text/plain", "GET or HEAD\n", 12, false);
    return;
  }

  char *q = std::strchr(target, '?');
  int requested = -1;
  char arg_k[24] = {0}, arg_v[24] = {0};
  if (q) {
    *q++ = 0;
    const char *ip = std::strstr(q, "i=");
    if (ip) requested = std::atoi(ip + 2);
    const char *kp = std::strstr(q, "k=");
    const char *vp = std::strstr(q, "v=");
    if (kp) std::snprintf(arg_k, sizeof arg_k, "%s", kp + 2);
    if (vp) std::snprintf(arg_v, sizeof arg_v, "%s", vp + 2);
    char *amp;
    if ((amp = std::strpbrk(arg_k, "&"))) *amp = 0;
    if ((amp = std::strpbrk(arg_v, "&"))) *amp = 0;
  }

  if (post && !std::strcmp(target, "/set")) {
    /* A custom header means a cross-site page would need a CORS preflight, which this server
     * never grants; a plain form post or a no-cors fetch cannot set one (review, 2026-11-02). */
    if (!header_is_set(req, "X-DJF2")) {
      send_body(fd, 403, "Forbidden", "text/plain", "missing X-DJF2 header\n", 24, false);
      return;
    }
    /* The page drives the machine here. Validation is deliberately narrow: the key must be
     * one of the contract's keys, the value a finite number in range; everything else is
     * refused with a reason the page can show. The actual change happens on the audio
     * thread (see provide()/take_commands()). */
    if (!g_rw.load(std::memory_order_relaxed)) {
      send_body(fd, 403, "Forbidden", "text/plain", "writes are off (DJF2_HTTP_RW=0)\n", 35,
                false);
      return;
    }
    const int slot = pick_slot(requested);
    if (!g_slots[slot].attached.load(std::memory_order_relaxed)) {
      send_body(fd, 409, "Conflict", "text/plain", "no instance\n", 12, false);
      return;
    }
    int idx = -1;
    const char *const *keys = g_keys;
    const int nkeys = g_key_count;
    if (keys && arg_k[0]) {
      for (int i = 0; i < nkeys; ++i) {
        if (!std::strcmp(keys[i], arg_k)) { idx = i; break; }
      }
    }
    if (idx < 0) {
      send_body(fd, 400, "Bad Request", "text/plain", "unknown key\n", 12, false);
      return;
    }
    char *end = nullptr;
    const double d = std::strtod(arg_v, &end);
    if (end == arg_v || !(d == d) || d < -100000.0 || d > 100000.0) {
      send_body(fd, 400, "Bad Request", "text/plain", "bad value\n", 10, false);
      return;
    }
    Command c;
    c.idx = idx;
    c.val = (float)d;
    if (!provide(slot, c)) {
      send_body(fd, 503, "Service Unavailable", "text/plain", "busy\n", 5, false);
      return;
    }
    char ok[48];
    const int n = std::snprintf(ok, sizeof ok, "{\"ok\":1,\"key\":\"%s\"}", arg_k);
    send_body(fd, 200, "OK", "application/json", ok, (size_t)n, false);
    return;
  }

  if (post) {   /* a POST to anywhere but /set is not a control message */
    send_body(fd, 405, "Method Not Allowed", "text/plain", "POST /set only\n", 15, false);
    return;
  }
  if (!std::strcmp(target, "/") || !std::strcmp(target, "/index.html")) {
    send_body(fd, 200, "OK", "text/html; charset=utf-8", kCompanionPage,
              sizeof(kCompanionPage) - 1, head_only);
    return;
  }
  if (!std::strcmp(target, "/state")) {
    char json[2048];
    const int n = state_json(json, sizeof json, requested);
    send_body(fd, 200, "OK", "application/json", json, (size_t)n, head_only);
    return;
  }
  if (!std::strcmp(target, "/manifest.json")) {
    send_body(fd, 200, "OK", "application/manifest+json", kManifest, sizeof(kManifest) - 1,
              head_only);
    return;
  }
  if (!std::strcmp(target, "/icon.svg")) {
    send_body(fd, 200, "OK", "image/svg+xml", kIconSvg, sizeof(kIconSvg) - 1, head_only);
    return;
  }
  if (!std::strcmp(target, "/diag")) {
    char txt[1400];
    int p2 = std::snprintf(txt, sizeof txt, "port=%d slots=%d attached=%d requests=%u\n",
                           g_port.load(), kSlots, attached_count(), g_requests);
    for (int i = 0; i < kDiagLines && p2 > 0 && (size_t)p2 + 160 < sizeof txt; ++i) {
      const DiagLine &d = g_diag[(g_diag_at + i) % kDiagLines];
      if (!d.target[0]) continue;
      p2 += std::snprintf(txt + p2, sizeof txt - (size_t)p2,
                          "t=%ums %s %s from %s ua=\"%s\"\n", d.ms, d.method, d.target,
                          d.remote, d.ua);
    }
    send_body(fd, 200, "OK", "text/plain; charset=utf-8", txt, (size_t)(p2 < 0 ? 0 : p2),
              head_only);
    return;
  }
  if (!std::strcmp(target, "/healthz")) {
    send_body(fd, 200, "OK", "text/plain", "ok\n", 3, head_only);
    return;
  }
  send_body(fd, 404, "Not Found", "text/html; charset=utf-8", kNotFoundPage,
            sizeof(kNotFoundPage) - 1, head_only);
}

void *server_main(void *) {
  int port = kDefaultPort;
  const char *env = std::getenv("DJF2_HTTP_PORT");
  if (env) {
    const int p = std::atoi(env);
    if (p > 1024 && p < 65000) port = p;
  }

  int fd = -1;
  for (int i = 0; i < kPortTries && fd < 0; ++i) fd = listen_on(port + i);
  if (fd < 0) {
    g_running.store(0);
    return nullptr;
  }
  g_port.store(port);
  g_started_ms = now_ms();

  while (!g_stop.load(std::memory_order_relaxed)) {
    struct pollfd p;
    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    const int rc = poll(&p, 1, 200);
    if (rc <= 0) continue;
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof peer;
    const int c = accept(fd, (struct sockaddr *)&peer, &peer_len);
    if (c < 0) continue;
    /* One blocking accept loop: a reader that stops draining (phone asleep, tab in the
     * background) must time out instead of stalling every other client, /set included. */
    struct timeval tmo;
    tmo.tv_sec = 2;
    tmo.tv_usec = 0;
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof tmo);
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof tmo);
    char remote[16] = "-";
    if (peer_len >= (socklen_t)sizeof peer) {
      struct in_addr a4 = peer.sin_addr;
      const unsigned char *b = (const unsigned char *)&a4.s_addr;
      std::snprintf(remote, sizeof remote, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    }
    handle_client(c, remote);
    close(c);
  }
  close(fd);
  g_port.store(0);
  g_running.store(0);
  return nullptr;
}

void stop_locked() {
  if (!g_running.load(std::memory_order_relaxed)) return;
  g_stop.store(1);
  pthread_join(g_thread, nullptr);   /* the loop wakes within 200 ms and returns */
  g_stop.store(0);
}

void start_locked() {
  if (g_running.load(std::memory_order_relaxed)) return;
  g_stop.store(0);
  g_running.store(1);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_attr_setstacksize(&attr, 256 * 1024);
  if (pthread_create(&g_thread, &attr, server_main, nullptr) != 0) {
    g_running.store(0);
  }
  pthread_attr_destroy(&attr);
}

/* The listener must NEVER outlive the last instance. MPC dlclose()s the .so when the
 * plugin is removed, and a thread still sitting in poll() would then run in unmapped
 * memory — that took the device down twice on 2026-11-01 ("OOPS! Your MPC recovered from
 * an internal error"), and it only showed up once the port had a listener at all. */
void stop_if_last() {
  if (g_instances.fetch_sub(1) != 1) return;
  pthread_mutex_lock(&g_start_lock);
  stop_locked();
  pthread_mutex_unlock(&g_start_lock);
}

}  // namespace

void register_param_keys(const char *const *keys, int count) {
  g_key_count = count;
  g_keys = keys;
}

int provide(int slot, const Command &c) {
  if (slot < 0 || slot >= kSlots) return 0;
  if (!g_rw.load(std::memory_order_relaxed)) return 0;
  const unsigned head = g_cmd_head[slot].load(std::memory_order_relaxed);
  if (head - g_cmd_tail[slot].load(std::memory_order_relaxed) >= (unsigned)kCmdRing) return 0;
  g_cmd[slot][head % kCmdRing] = c;
  g_cmd_head[slot].store(head + 1, std::memory_order_release);
  return 1;
}

int take_commands(int slot, Command *out, int max) {
  if (slot < 0 || slot >= kSlots) return 0;
  int n = 0;
  unsigned tail = g_cmd_tail[slot].load(std::memory_order_relaxed);
  const unsigned head = g_cmd_head[slot].load(std::memory_order_acquire);
  while (tail != head && n < max) {
    out[n++] = g_cmd[slot][tail % kCmdRing];
    ++tail;
  }
  if (n) g_cmd_tail[slot].store(tail, std::memory_order_release);
  return n;
}

int attach() {
  if (std::getenv("DJF2_HTTP") && std::atoi(std::getenv("DJF2_HTTP")) == 0) return -1;
  pthread_once(&g_init_once, init_slots);
  for (int i = 0; i < kSlots; ++i) {
    unsigned expected = 0;
    if (g_slots[i].attached.compare_exchange_strong(expected, 1u)) {
      g_slots[i].stamp_ms.store(now_ms());
      g_instances.fetch_add(1);
      pthread_mutex_lock(&g_start_lock);
      start_locked();
      pthread_mutex_unlock(&g_start_lock);
      /* Wait (bounded) until the listener is actually bound. Without this, create() returns a
       * slot whose thread may still be inside bind(), and the first request after an insert
       * gets a refused connection - the phone's very first paint. 200 ms is the worst case and
       * costs nothing when the bind is immediate. */
      for (int spin = 0; spin < 200 && g_running.load() && g_port.load() == 0; ++spin)
        usleep(1000);
      return i;
    }
  }
  return -1;
}

void detach(int slot) {
  if (slot < 0 || slot >= kSlots) return;
  unsigned expected = 1;
  /* Only the owner may release the slot: a double destroy must not decrement the instance
   * count twice and leave the listener running. */
  if (!g_slots[slot].attached.compare_exchange_strong(expected, 0u)) return;
  stop_if_last();
}

void publish(int slot, const Snapshot &s) {
  if (slot < 0 || slot >= kSlots) return;
  Slot &d = g_slots[slot];
  if (!d.attached.load(std::memory_order_relaxed)) return;
  d.seq.fetch_add(1, std::memory_order_release);   /* odd: do not read */
  d.vu.store(s.vu);
  d.peak.store(s.peak);
  d.delay_ms.store(s.delay_ms);
  d.bpm.store(s.bpm);
  d.bpm_seen.store(s.bpm_seen);
  d.mix.store(s.mix);
  d.iso_lo.store(s.iso_lo);
  d.iso_mid.store(s.iso_mid);
  d.iso_hi.store(s.iso_hi);
  d.iso_lo_x.store(s.iso_lo_x);
  d.iso_hi_x.store(s.iso_hi_x);
  d.dj_filter.store(s.dj_filter);
  d.dj_reso.store(s.dj_reso);
  d.rep_gate.store(s.rep_gate);
  d.rep_chance.store(s.rep_chance);
  d.rep_mix.store(s.rep_mix);
  d.dly_time.store(s.dly_time);
  d.dly_fbk.store(s.dly_fbk);
  d.dly_tone.store(s.dly_tone);
  d.dly_mix.store(s.dly_mix);
  d.verb_amount.store(s.verb_amount);
  d.verb_decay.store(s.verb_decay);
  d.verb_damp.store(s.verb_damp);
  d.verb_predelay.store(s.verb_predelay);
  d.hp_hz.store(s.hp_hz);
  d.hp_reso.store(s.hp_reso);
  d.fl_rate.store(s.fl_rate);
  d.fl_depth.store(s.fl_depth);
  d.fl_fbk.store(s.fl_fbk);
  d.fl_mix.store(s.fl_mix);
  d.gr_rate.store(s.gr_rate);
  d.gr_time.store(s.gr_time);
  d.gr_spray.store(s.gr_spray);
  d.gr_mix.store(s.gr_mix);
  d.mac_lo.store(s.mac_lo);
  d.mac_mid.store(s.mac_mid);
  d.mac_hi.store(s.mac_hi);
  d.mac_lpf.store(s.mac_lpf);
  d.mac_glitch.store(s.mac_glitch);
  d.mac_verb.store(s.mac_verb);
  d.mac_fade.store(s.mac_fade);
  d.mac_fill.store(s.mac_fill);
  d.output_gain.store(s.output_gain);
  d.rep_env.store(s.rep_env);
  d.dly_env.store(s.dly_env);
  d.verb_env.store(s.verb_env);
  d.rep_mode.store(s.rep_mode);
  d.rep_count.store(s.rep_count);
  d.fill_mode.store(s.fill_mode);
  d.dly_sync.store(s.dly_sync);
  d.dly_div.store(s.dly_div);
  d.dly_pingpong.store(s.dly_pingpong);
  d.verb_mode.store(s.verb_mode);
  d.limiter.store(s.limiter);
  d.stamp_ms.store(now_ms());
  d.seq.fetch_add(1, std::memory_order_release);   /* even: stable */
}

int port() { return g_port.load(std::memory_order_relaxed); }

void stop_server() {
  pthread_mutex_lock(&g_start_lock);
  stop_locked();
  pthread_mutex_unlock(&g_start_lock);
}

}  // namespace djf2

/* ---- the C engine's face (companion_c.h) ------------------------------------- */
extern "C" {

void djfx_companion_register_keys(const char *const *keys, int count) {
  djf2::register_param_keys(keys, count);
}

int djfx_companion_attach(void) { return djf2::attach(); }

void djfx_companion_detach(int slot) { djf2::detach(slot); }

int djfx_companion_take(int slot, int *idx, float *val, int max) {
  djf2::Command cmds[8];
  int n = djf2::take_commands(slot, cmds, max < 8 ? max : 8);
  for (int i = 0; i < n; i++) { idx[i] = cmds[i].idx; val[i] = cmds[i].val; }
  return n;
}

void djfx_companion_publish(int slot, const djfx_snap *p) {
  if (!p) return;
  djf2::Snapshot s;
  s.vu = p->vu;
  s.peak = p->peak;
  s.delay_ms = p->delay_ms;
  s.bpm = p->bpm;
  s.bpm_seen = p->bpm_seen;
  s.mix = p->mix;
  s.iso_lo = p->iso_lo;
  s.iso_mid = p->iso_mid;
  s.iso_hi = p->iso_hi;
  s.iso_lo_x = p->iso_lo_x;
  s.iso_hi_x = p->iso_hi_x;
  s.dj_filter = p->dj_filter;
  s.dj_reso = p->dj_reso;
  s.rep_gate = p->rep_gate;
  s.rep_chance = p->rep_chance;
  s.rep_mix = p->rep_mix;
  s.dly_time = p->dly_time;
  s.dly_fbk = p->dly_fbk;
  s.dly_tone = p->dly_tone;
  s.dly_mix = p->dly_mix;
  s.verb_amount = p->verb_amount;
  s.verb_decay = p->verb_decay;
  s.verb_damp = p->verb_damp;
  s.verb_predelay = p->verb_predelay;
  s.hp_hz = p->hp_hz;
  s.hp_reso = p->hp_reso;
  s.fl_rate = p->fl_rate;
  s.fl_depth = p->fl_depth;
  s.fl_fbk = p->fl_fbk;
  s.fl_mix = p->fl_mix;
  s.gr_rate = p->gr_rate;
  s.gr_time = p->gr_time;
  s.gr_spray = p->gr_spray;
  s.gr_mix = p->gr_mix;
  s.mac_lo = p->mac_lo;
  s.mac_mid = p->mac_mid;
  s.mac_hi = p->mac_hi;
  s.mac_lpf = p->mac_lpf;
  s.mac_glitch = p->mac_glitch;
  s.mac_verb = p->mac_verb;
  s.mac_fade = p->mac_fade;
  s.mac_fill = p->mac_fill;
  s.output_gain = p->output_gain;
  s.rep_env = p->rep_env;
  s.dly_env = p->dly_env;
  s.verb_env = p->verb_env;
  s.rep_mode = p->rep_mode;
  s.rep_count = p->rep_count;
  s.fill_mode = p->fill_mode;
  s.dly_sync = p->dly_sync;
  s.dly_div = p->dly_div;
  s.dly_pingpong = p->dly_pingpong;
  s.verb_mode = p->verb_mode;
  s.limiter = p->limiter;
  djf2::publish(slot, s);
}

}  /* extern "C" */
