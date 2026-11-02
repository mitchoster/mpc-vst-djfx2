/* companion_test.cpp — offline proof of the companion feed (no device, no MPC).
 *
 * Creates real engine instances, feeds them a tone so the meters have something to show, then
 * talks to the embedded HTTP listener over a socket:
 *
 *   1. /state answers 200 + JSON, and the numbers match what the engine holds
 *   2. the delay time follows the tempo sync, not just the knob
 *   3. a macro fans out to the parameter it owns (that is the whole point of a macro)
 *   4. / serves the page, /healthz answers, anything else is 404, POST to /state is refused
 *   5. the write path reaches the engine, refuses unknown keys and header-less posts, and a
 *      write to ?i=1 leaves instance 0 alone
 *   6. the listener stops with the last instance and comes back on a fresh insert
 *   7. DJF2_HTTP_RW=0 keeps the feed and refuses every write
 */

#include <arpa/inet.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

/* the engine is C: include its interface with C linkage, the way the wrapper sees it */
extern "C" {
#include "engine.h"
}

static int g_fail = 0;
static int g_port = 18102;

static void check(bool ok, const char *name, const std::string &detail) {
  std::printf("%s: %s = %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
  if (!ok) ++g_fail;
}

static std::string http(int port, const char *method, const char *path, int *status_out,
                 bool with_header = true) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (status_out) *status_out = 0;
  if (fd < 0) return "";
  struct timeval tv;
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  struct sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return ""; }
  char req[256];
  const int n = std::snprintf(req, sizeof req,
                              "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\n%s\r\n",
                              method, path, with_header ? "X-DJF2: 1\r\n" : "");
  if (send(fd, req, (size_t)n, 0) <= 0) { close(fd); return ""; }
  std::string raw;
  char buf[4096];
  for (;;) {
    const ssize_t r = recv(fd, buf, sizeof buf, 0);
    if (r <= 0) break;
    raw.append(buf, (size_t)r);
  }
  close(fd);
  if (status_out) {
    int code = 0;
    if (std::sscanf(raw.c_str(), "HTTP/1.1 %d", &code) == 1) *status_out = code;
  }
  const size_t hdr = raw.find("\r\n\r\n");
  return hdr == std::string::npos ? std::string() : raw.substr(hdr + 4);
}

static double num(const std::string &js, const char *key) {
  const std::string pat = std::string("\"") + key + "\":";
  size_t p = js.find(pat);
  if (p == std::string::npos) return -999.0;
  return std::atof(js.c_str() + p + pat.size());
}

static std::string str_field(const std::string &js, const char *key) {
  const std::string pat = std::string("\"") + key + "\":";
  size_t p = js.find(pat);
  if (p == std::string::npos) return "";
  p += pat.size();
  const size_t e = js.find_first_of(",}", p);
  return js.substr(p, e - p);
}

/* the engine is an effect: 128 frames, interleaved int16, in and out are separate buffers */
static void feed(const mpc_engine_t *E, void *inst, int blocks, double freq, double amp) {
  static int16_t in[128 * 2], out[128 * 2];
  double ph = 0.0;
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < 128; ++i) {
      const double s = std::sin(ph) * amp;
      ph += 2.0 * M_PI * freq / 44100.0;
      const int16_t v = (int16_t)(s * 32767.0);
      in[i * 2] = v;
      in[i * 2 + 1] = v;
    }
    E->process(inst, in, out, 128);
  }
}

static bool listening(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  struct sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const bool ok = connect(fd, (struct sockaddr *)&a, sizeof a) == 0;
  close(fd);
  return ok;
}

int main(int argc, char **argv) {
  const char *env = std::getenv("DJF2_HTTP_PORT");
  if (env) g_port = std::atoi(env);

  if (argc > 2 && std::strcmp(argv[1], "--probe") == 0) {
    const bool open = listening(std::atoi(argv[2]));
    std::printf("%s\n", open ? "LISTENING" : "CLOSED");
    return open ? 3 : 0;
  }
  const bool rw_off = argc > 2 && std::strcmp(argv[1], "--rw-off") == 0;
  if (rw_off) g_port = std::atoi(argv[2]);

  const mpc_engine_t *E = mpc_engine();
  if (!E) { std::printf("FAIL: no engine\n"); return 1; }

  const char *dir = "/tmp/opencode/companion-fake-plugin";
  if (std::system("rm -rf /tmp/opencode/companion-fake-plugin && "
                  "mkdir -p '/tmp/opencode/companion-fake-plugin'") != 0) {
    std::printf("FAIL: cannot prepare the fake plugin folder\n");
    return 1;
  }

  void *inst = E->create(dir);
  if (!inst) { std::printf("FAIL: no instance\n"); return 1; }

  if (argc > 1 && std::strcmp(argv[1], "--instance-only") == 0) {
    feed(E, inst, 40, 220.0, 0.2);
    usleep(1500 * 1000);
    E->destroy(inst);
    return 0;
  }
  if (rw_off) {
    feed(E, inst, 40, 220.0, 0.25);
    int st = 0;
    std::string body = http(g_port, "GET", "/state", &st);
    check(st == 200 && body.find("\"active\":1") != std::string::npos,
          "read-only mode still serves /state", body.substr(0, 60));
    body = http(g_port, "POST", "/set?k=rep_gate&v=0.9", &st);
    check(st == 403, "read-only mode refuses a write", std::to_string(st));
    E->destroy(inst);
    std::printf("%s\n", g_fail == 0 ? "PASSED" : "FAILED");
    return g_fail == 0 ? 0 : 1;
  }

  int status = 0;
  std::string js = http(g_port, "GET", "/state", &status);
  check(status == 200 && js.find("\"active\":1") != std::string::npos,
        "the listener answers before the first block", js.substr(0, 120));

  feed(E, inst, 400, 220.0, 0.25);

  js = http(g_port, "GET", "/state", &status);
  check(status == 200 && js.find("\"active\":1") != std::string::npos, "/state is live", str_field(js, "active"));
  check(js.find("\"count\":1") != std::string::npos, "one instance is reported", str_field(js, "count"));
  const double vu = num(js, "vu");
  check(vu > 0.02 && vu <= 1.0, "the VU reaches the page", std::to_string(vu));
  check(num(js, "rep_mode") == 0.0, "default repeat grid is Off", std::to_string(num(js, "rep_mode")));
  const double ms = num(js, "delay_ms");
  check(ms > 0.0 && ms < 8000.0, "the delay time is published", std::to_string(ms));
  check(js.find("\"mix\":") != std::string::npos, "the wet mix is published", str_field(js, "mix"));
  check(num(js, "bpm_seen") == 0.0, "a host that sends no tempo is visible as such", std::to_string(num(js, "bpm_seen")));

  /* the address a phone opens: the bound port, and a URL that names this listener */
  check(num(js, "port") == (double)g_port, "the reported port is the bound one",
        std::to_string(num(js, "port")));
  {
    const std::string u = js.substr(js.find("\"url\":\"") + 7);
    const std::string url_val = u.substr(0, u.find('"'));
    check(url_val.compare(0, 7, "http://") == 0 &&
          url_val.find(":" + std::to_string(g_port) + "/") != std::string::npos,
          "the URL is this listener, ready to type or scan", url_val);
  }

  /* the delay follows the sync, not just the knob */
  E->set_param(inst, "lfo_bpm", "96");
  E->set_param(inst, "dly_sync", "1");
  E->set_param(inst, "dly_div", "5");   /* 1/8 */
  feed(E, inst, 80, 220.0, 0.25);
  js = http(g_port, "GET", "/state", &status);
  check(num(js, "dly_sync") == 1.0, "sync reaches the page", std::to_string(num(js, "dly_sync")));
  check(num(js, "bpm_seen") == 1.0, "once the host reports tempo, the page knows it", std::to_string(num(js, "bpm_seen")));
  const double sync_ms = num(js, "delay_ms");
  check(std::fabs(sync_ms - 312.5) < 4.0, "1/8 at 96 BPM is 312 ms", std::to_string(sync_ms));
  E->set_param(inst, "dly_sync", "0");

  /* ---- writing: the page drives the machine ---------------------------------- */
  std::string set = http(g_port, "POST", "/set?k=rep_gate&v=0.7", &status);
  check(status == 200 && set.find("\"ok\":1") != std::string::npos, "POST /set takes a value",
        set.substr(0, 40));
  feed(E, inst, 20, 220.0, 0.25);
  js = http(g_port, "GET", "/state", &status);
  check(std::fabs(num(js, "rep_gate") - 0.7) < 0.01, "the change reached the engine",
        std::to_string(num(js, "rep_gate")));
  check(std::fabs(num(js, "rep_chance") - 1.0) < 0.01, "and nothing else moved",
        std::to_string(num(js, "rep_chance")));

  /* an option by index, the way the grid sends it */
  http(g_port, "POST", "/set?k=rep_mode&v=3", &status);
  feed(E, inst, 20, 220.0, 0.25);
  js = http(g_port, "GET", "/state", &status);
  check(num(js, "rep_mode") == 3.0, "an option index works", std::to_string(num(js, "rep_mode")));

  /* a macro owns its targets: moving LOW-EQ must move the isolator's low band */
  const double iso_before = num(http(g_port, "GET", "/state", &status), "iso_lo");
  http(g_port, "POST", "/set?k=mac_lo&v=0.6", &status);
  feed(E, inst, 20, 220.0, 0.25);
  js = http(g_port, "GET", "/state", &status);
  check(std::fabs(num(js, "iso_lo") - iso_before) > 0.5, "a macro fans out to its target",
        "iso_lo " + std::to_string(iso_before) + " -> " + std::to_string(num(js, "iso_lo")));

  http(g_port, "POST", "/set?k=nonsense&v=1", &status);
  check(status == 400, "an unknown key is refused", std::to_string(status));
  http(g_port, "POST", "/set?k=rep_gate&v=abc", &status);
  check(status == 400, "a non-number is refused", std::to_string(status));
  http(g_port, "POST", "/set?k=rep_gate&v=0.1", &status, false);
  check(status == 403, "a write without X-DJF2 is refused", std::to_string(status));

  /* the page itself */
  std::string page = http(g_port, "GET", "/", &status);
  check(status == 200 && page.find("<canvas") != std::string::npos,
        "/ serves the page", std::to_string(page.size()) + " bytes");
  check(page.find("/state") != std::string::npos, "the page polls /state", "found");
  check(page.find("http://") == std::string::npos || page.find("http://127.0.0.1") != std::string::npos,
        "the page needs no network of its own", "no external reference");

  std::string health = http(g_port, "GET", "/healthz", &status);
  check(status == 200 && health == "ok\n", "/healthz", health.substr(0, 8));
  std::string nf = http(g_port, "GET", "/nope", &status);
  check(status == 404, "an unknown path is 404", std::to_string(status));
  check(nf.find("<!doctype html>") != std::string::npos && nf.find("/state") != std::string::npos,
        "the 404 is a page, not naked text a JSON viewer can hijack", nf.substr(0, 40));

  char abs_path[96];
  std::snprintf(abs_path, sizeof abs_path, "http://127.0.0.1:%d/", g_port);
  std::string abs = http(g_port, "GET", abs_path, &status);
  check(status == 200 && abs.find("<canvas") != std::string::npos,
        "an absolute-form target still serves the page", std::to_string(abs.size()) + " bytes");

  std::string diag = http(g_port, "GET", "/diag", &status);
  check(status == 200 && diag.find("requests=") != std::string::npos,
        "/diag counts requests", diag.substr(0, 40));
  /* Browsers send header names in their own case (often lowercase); the check must not
   * depend on the exact bytes curl happens to send. */
  {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)g_port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int code = 0;
    if (fd >= 0 && connect(fd, (struct sockaddr *)&a, sizeof a) == 0) {
      char req[192];
      const int n = std::snprintf(req, sizeof req,
                                  "POST /set?k=hp_hz&v=30 HTTP/1.1\r\nHost: x\r\nx-djf2: 1\r\n\r\n");
      if (send(fd, req, (size_t)n, 0) > 0) {
        char buf[256];
        const ssize_t r = recv(fd, buf, sizeof buf - 1, 0);
        if (r > 0) { buf[r] = 0; std::sscanf(buf, "HTTP/1.1 %d", &code); }
      }
    }
    if (fd >= 0) close(fd);
    check(code == 200, "a lower-case write header is accepted", std::to_string(code));
  }

  http(g_port, "POST", "/state", &status);
  check(status == 405, "the feed is read-only (POST refused)", std::to_string(status));

  /* a second instance gets its own slot and its own command ring */
  void *inst2 = E->create(dir);
  feed(E, inst2, 80, 220.0, 0.25);
  js = http(g_port, "GET", "/state", &status);
  check(js.find("\"count\":2") != std::string::npos, "a second instance takes its own slot", str_field(js, "count"));
  js = http(g_port, "GET", "/state?i=1", &status);
  check(status == 200 && js.find("\"instance\":1") != std::string::npos, "?i= selects the instance",
        str_field(js, "instance"));

  const double before0 = num(http(g_port, "GET", "/state?i=0", &status), "rep_gate");
  http(g_port, "POST", "/set?i=1&k=rep_gate&v=0.9", &status);
  feed(E, inst2, 20, 220.0, 0.25);
  feed(E, inst, 20, 220.0, 0.25);
  const double after0 = num(http(g_port, "GET", "/state?i=0", &status), "rep_gate");
  const double after1 = num(http(g_port, "GET", "/state?i=1", &status), "rep_gate");
  check(std::fabs(after0 - before0) < 0.001 && std::fabs(after1 - 0.9) < 0.01,
        "a write reaches only the addressed instance",
        "i0 " + std::to_string(after0) + ", i1 " + std::to_string(after1));

  E->destroy(inst2);
  feed(E, inst, 40, 220.0, 0.25);
  js = http(g_port, "GET", "/state", &status);
  check(js.find("\"count\":1") != std::string::npos, "destroy releases the slot", str_field(js, "count"));

  E->destroy(inst);

  /* The listener must NOT outlive the last instance (device crash of 2026-11-01). */
  check(!listening(g_port), "the listener stops with the last instance", "port closed");

  void *inst3 = E->create(dir);
  feed(E, inst3, 40, 220.0, 0.2);
  js = http(g_port, "GET", "/state", &status);
  check(status == 200 && js.find("\"active\":1") != std::string::npos,
        "a new instance restarts the listener", std::to_string(status));
  E->destroy(inst3);
  check(!listening(g_port), "and stops it again", "port closed");

  std::printf("%s\n", g_fail == 0 ? "PASSED" : "FAILED");
  return g_fail == 0 ? 0 : 1;
}
