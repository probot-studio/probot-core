#pragma once
#ifdef ESP32
#include <new>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <Arduino.h>
#include <FS.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <probot/core/core_config.hpp>
#include <probot/core/lifecycle.hpp>
#include <probot/files/files.hpp>
#include <probot/robot/state.hpp>

// HTTP side of probot::files: GET /files lists the shared folders, GET
// /files/get?share=<name>&name=<file> downloads one file. The Driver Station
// handler checks ownership first. Both answer only while the robot is idle
// (stopped, nothing pending). The server task never touches a file system:
// a worker task answers through an async request, at low priority, so a
// slow card or a lock left by the killed user task after an e-stop cannot
// stall the Driver Station. One listing or download runs at a time and it
// is cut off as soon as a run is requested or its share is replaced or
// removed; the share is marked while it is read, so share() and unshare()
// can wait for it to be released (at most 1 s) and files::busy() reports it
// until then, for a sketch that unmounts. A share is an
// Arduino fs::FS folder or a VFS path read with POSIX calls.
namespace probot::driverstation::esp32::files_http {

  namespace pf = probot::files;
  namespace pfd = probot::files::detail;

  constexpr size_t kMaxEntries = 256;          // listed per share
  constexpr size_t kMaxVisits = 2 * kMaxEntries;  // looked at per share, skipped ones included
  constexpr size_t kChunk = 4096;
  constexpr uint32_t kTaskStack = 8192;
  // Before 2024 the robot clock was not set when the file was written.
  constexpr int64_t kMinValidMtime = 1704067200;

  inline volatile uint32_t g_busy = 0;

  inline bool robotIdle(robot::StateService& rs) {
    auto s = rs.read();
    return pfd::transferAllowed(s.phase, s.status,
                                __atomic_load_n(&robot::g_estop_latched, __ATOMIC_SEQ_CST) != 0);
  }

  inline esp_err_t reply(httpd_req_t* req, const char* status, const char* body) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
  }

  inline esp_err_t notIdle(httpd_req_t* req) {
    return reply(req, "409 Conflict", "[PB-E409] STOP the robot to list or download files - docs/hatalar#pb-e409");
  }

  // A robot that shares nothing answers every client in every phase like firmware without
  // file sharing, so a Driver Station can show its files card for any other reply.
  inline bool replyIfNoShares(httpd_req_t* req) {
    if (pfd::count() != 0) return false;
    reply(req, "404 Not Found", "no shared folders");
    return true;
  }

  struct Job {
    httpd_req_t* req;
    robot::StateService* rs;
    bool listing;
    pfd::Share sh;                             // the share to download from
    char path[pf::kMaxPath + 1];
  };

  // The entries of a shared folder.
  class DirReader {
  public:
    struct Entry {
      char name[pf::kMaxFileName + 1];
      bool file;                               // a regular file with a valid name
      uint64_t size;
      int64_t mtime;
    };

    explicit DirReader(const pfd::Share& sh) : _sh(sh) {
      if (sh.fs) {
        _dir = static_cast<fs::FS*>(sh.fs)->open(sh.dir);
        _ok = _dir && _dir.isDirectory();
      } else {
        _vfs = opendir(sh.dir);
        _ok = _vfs != nullptr;
      }
    }
    ~DirReader() { if (_vfs) closedir(_vfs); }
    DirReader(const DirReader&) = delete;
    DirReader& operator=(const DirReader&) = delete;

    bool ok() const { return _ok; }

    // False at the end of the folder.
    bool next(Entry& e) {
      e.file = false;
      e.name[0] = '\0';
      if (!_ok) return false;
      if (!_vfs) {
        fs::File f = _dir.openNextFile();
        if (!f) return false;
        if (!copyName(f.name(), e)) return true;
        e.file = !f.isDirectory();
        e.size = f.size();
        e.mtime = (int64_t)f.getLastWrite();
        return true;
      }
      struct dirent* d = readdir(_vfs);
      if (!d) return false;
      char path[pf::kMaxPath + 1];
      struct stat st;
      if (!copyName(d->d_name, e) || !pfd::joinPath(_sh.dir, e.name, path, sizeof path) ||
          stat(path, &st) != 0) {
        return true;
      }
      e.file = S_ISREG(st.st_mode);
      e.size = (uint64_t)st.st_size;
      e.mtime = (int64_t)st.st_mtime;
      return true;
    }

  private:
    static bool copyName(const char* name, Entry& e) {
      if (!name || !pfd::validFileName(name)) return false;
      strcpy(e.name, name);
      return true;
    }

    const pfd::Share& _sh;
    fs::File _dir;
    DIR* _vfs = nullptr;
    bool _ok = false;
  };

  // One shared file, read from the start.
  class FileReader {
  public:
    FileReader(const pfd::Share& sh, const char* path) {
      if (sh.fs) {
        _file = static_cast<fs::FS*>(sh.fs)->open(path, "r");
        _ok = _file && !_file.isDirectory();
        if (_ok) _size = _file.size();
      } else {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
          _vfs = fopen(path, "rb");
          _ok = _vfs != nullptr;
          _size = (size_t)st.st_size;
        }
      }
    }
    ~FileReader() { if (_vfs) fclose(_vfs); }
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    bool ok() const { return _ok; }
    size_t size() const { return _size; }
    size_t read(uint8_t* buf, size_t n) {
      return _vfs ? fread(buf, 1, n, _vfs) : _file.read(buf, n);
    }

  private:
    fs::File _file;
    FILE* _vfs = nullptr;
    size_t _size = 0;
    bool _ok = false;
  };

  // Marks a share as read for its scope (pfd::enterShare). Declare it
  // before the reader so the handle is closed first.
  class ShareUse {
  public:
    explicit ShareUse(const pfd::Share& sh) : _ok(pfd::enterShare(sh)) {}
    ~ShareUse() { if (_ok) pfd::leaveShare(); }
    ShareUse(const ShareUse&) = delete;
    ShareUse& operator=(const ShareUse&) = delete;
    bool ok() const { return _ok; }
  private:
    bool _ok;
  };

  // {"shares":[{"name":"logs","ok":true,"truncated":false,"files":[..]},..]}
  // False when the client is gone or a run was requested.
  inline bool listShares(httpd_req_t* req, robot::StateService& rs) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char buf[1024];
    char entry[pf::kMaxFileName * 6 + 80];
    static_assert(sizeof entry <= sizeof buf, "one entry must fit a chunk");
    size_t n = 0;
    // Batches entries into one chunk.
    auto put = [&](const char* s, size_t len) {
      if (n + len > sizeof buf) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) return false;
        n = 0;
      }
      memcpy(buf + n, s, len);
      n += len;
      return true;
    };
    auto putStr = [&](const char* s) { return put(s, strlen(s)); };

    if (!putStr("{\"shares\":[")) return false;
    pfd::Share sh;
    DirReader::Entry e;
    bool first = true;
    for (size_t i = 0; pfd::shareAt(i, sh); ++i) {
      if (!robotIdle(rs)) return false;
      ShareUse use(sh);
      if (!use.ok()) continue;                                      // replaced or removed meanwhile
      DirReader dir(sh);
      char head[64];
      snprintf(head, sizeof head, "%s{\"name\":\"%s\",\"ok\":%s,\"files\":[",
               first ? "" : ",", sh.name, dir.ok() ? "true" : "false");
      first = false;
      if (!putStr(head)) return false;
      size_t count = 0;
      bool truncated = false;
      for (size_t visits = 0; dir.ok(); ++visits) {
        if (!robotIdle(rs)) return false;
        if (!pfd::shareCurrent(sh)) { truncated = true; break; }   // replaced or removed
        if (visits == kMaxVisits || count == kMaxEntries) { truncated = dir.next(e); break; }
        if (!dir.next(e)) break;
        if (!e.file) continue;
        size_t len = pfd::jsonFile(e.name, e.size, e.mtime >= kMinValidMtime ? e.mtime : 0,
                                   count == 0, entry, sizeof entry);
        if (len && !put(entry, len)) return false;
        if (len) ++count;
      }
      snprintf(head, sizeof head, "],\"truncated\":%s}", truncated ? "true" : "false");
      if (!putStr(head)) return false;
    }
    if (!putStr("]}")) return false;
    if (n && httpd_resp_send_chunk(req, buf, n) != ESP_OK) return false;
    return httpd_resp_send_chunk(req, nullptr, 0) == ESP_OK;
  }

  // Answers with a status while nothing was sent; false when a started
  // download had to stop.
  inline bool sendFile(httpd_req_t* req, robot::StateService& rs, Job& job) {
    ShareUse use(job.sh);
    if (!use.ok()) { reply(req, "404 Not Found", "no such file"); return true; }
    FileReader f(job.sh, job.path);
    if (!f.ok()) { reply(req, "404 Not Found", "no such file"); return true; }
    char* buf = static_cast<char*>(malloc(kChunk));
    if (!buf) { reply(req, "503 Service Unavailable", "out of memory"); return true; }
    // Logs keep growing while they are downloaded: send the size seen at
    // open, so the copy is a consistent prefix.
    size_t left = f.size();
    bool sent = false, ok = true, idle = true, current = true;
    for (;;) {
      idle = robotIdle(rs);
      current = pfd::shareCurrent(job.sh);
      if (!idle || !current) { ok = false; break; }   // a run was requested or the share changed
      size_t n = left ? f.read(reinterpret_cast<uint8_t*>(buf), left < kChunk ? left : kChunk) : 0;
      if (left && n == 0) { ok = false; break; }
      if (!sent) {
        httpd_resp_set_type(req, "application/octet-stream");
        httpd_resp_set_hdr(req, "Content-Disposition", "attachment");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        sent = true;
      }
      if (n == 0) { ok = httpd_resp_send_chunk(req, nullptr, 0) == ESP_OK; break; }
      if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) { ok = false; break; }
      left -= n;
    }
    free(buf);
    if (ok || sent) return ok;
    if (!idle) notIdle(req);
    else if (!current) reply(req, "404 Not Found", "no such file");
    else reply(req, "500 Internal Server Error", "read failed");
    return true;
  }

  inline void fileTask(void* arg) {
    auto* job = static_cast<Job*>(arg);
    httpd_req_t* req = job->req;
    bool ok = true;
    if (!robotIdle(*job->rs)) notIdle(req);   // a run was requested meanwhile
    else if (job->listing) ok = listShares(req, *job->rs);
    else ok = sendFile(req, *job->rs, *job);
    if (!ok) {
      // An unterminated chunked body: the browser reports a failed
      // transfer instead of keeping a truncated file.
      httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
      Serial.println("[DS   ] file transfer aborted");
    }
    httpd_req_async_handler_complete(req);
    delete job;
    __atomic_store_n(&g_busy, 0u, __ATOMIC_SEQ_CST);
    vTaskDelete(nullptr);
  }

  // Hands the request to a worker task. The caller filled the job.
  inline esp_err_t startJob(httpd_req_t* req, Job* job) {
    uint32_t idle = 0;
    if (!__atomic_compare_exchange_n(&g_busy, &idle, 1u, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      delete job;
      return reply(req, "503 Service Unavailable", "another file transfer is running");
    }
    httpd_req_t* async = nullptr;
    if (httpd_req_async_handler_begin(req, &async) != ESP_OK) {
      delete job;
      __atomic_store_n(&g_busy, 0u, __ATOMIC_SEQ_CST);
      return reply(req, "503 Service Unavailable", "out of memory");
    }
    job->req = async;
    if (xTaskCreatePinnedToCore(fileTask, "ds_file", kTaskStack, job, PRIO_USER, nullptr,
                                CORE_UI) != pdPASS) {
      reply(async, "503 Service Unavailable", "out of memory");
      httpd_req_async_handler_complete(async);
      delete job;
      __atomic_store_n(&g_busy, 0u, __ATOMIC_SEQ_CST);
    }
    return ESP_OK;
  }

  inline esp_err_t list(httpd_req_t* req, robot::StateService& rs) {
    if (!robotIdle(rs)) return notIdle(req);
    auto* job = new (std::nothrow) Job{nullptr, &rs, true, {}, {}};
    if (!job) return reply(req, "503 Service Unavailable", "out of memory");
    return startJob(req, job);
  }

  inline esp_err_t download(httpd_req_t* req, robot::StateService& rs) {
    if (!robotIdle(rs)) return notIdle(req);
    char query[512];
    char shareName[pf::kMaxShareName + 1];
    char rawName[pf::kMaxFileName * 3 + 1];
    char name[pf::kMaxFileName + 1];
    if (httpd_req_get_url_query_str(req, query, sizeof query) != ESP_OK ||
        httpd_query_key_value(query, "share", shareName, sizeof shareName) != ESP_OK ||
        httpd_query_key_value(query, "name", rawName, sizeof rawName) != ESP_OK ||
        !pfd::urlDecode(rawName, name, sizeof name) || !pfd::validFileName(name)) {
      return reply(req, "400 Bad Request", "bad share or file name");
    }
    auto* job = new (std::nothrow) Job{nullptr, &rs, false, {}, {}};
    if (!job) return reply(req, "503 Service Unavailable", "out of memory");
    if (!pfd::findShare(shareName, job->sh) ||
        !pfd::joinPath(job->sh.dir, name, job->path, sizeof job->path)) {
      delete job;
      return reply(req, "404 Not Found", "no such file");
    }
    return startJob(req, job);
  }

} // namespace probot::driverstation::esp32::files_http
#endif
