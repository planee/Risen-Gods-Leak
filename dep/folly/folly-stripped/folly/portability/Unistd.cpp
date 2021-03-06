/*
 * Copyright 2017 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// We need to prevent winnt.h from defining the core STATUS codes,
// otherwise they will conflict with what we're getting from ntstatus.h
#define UMDF_USING_NTSTATUS

#include <folly/portability/Unistd.h>

#ifdef _WIN32

#include <cstdio>

#include <fcntl.h>

// #include <folly/portability/Sockets.h>
#include <folly/portability/Windows.h>

// Including ntdef.h requires building as a driver, but all we want
// is a status code, but we need NTSTATUS defined for that. Luckily
// bcrypt.h also defines NTSTATUS, so we'll use that one instead.
#include <bcrypt.h> // @manual
#include <ntstatus.h> // @manual

namespace folly {
namespace portability {
namespace unistd {
int access(char const* fn, int am) {
  return _access(fn, am);
}

int chdir(const char* path) {
  return _chdir(path);
}

int dup(int fh) {
  return _dup(fh);
}

int dup2(int fhs, int fhd) {
  return _dup2(fhs, fhd);
}

int fsync(int fd) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }
  if (!FlushFileBuffers(h)) {
    return -1;
  }
  return 0;
}

int ftruncate(int fd, off_t len) {
  if (_lseek(fd, len, SEEK_SET) == -1) {
    return -1;
  }

  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }
  if (!SetEndOfFile(h)) {
    return -1;
  }
  return 0;
}

char* getcwd(char* buf, int sz) {
  return _getcwd(buf, sz);
}

int getdtablesize() {
  return _getmaxstdio();
}

int getgid() {
  return 1;
}

pid_t getpid() {
  return (pid_t)uint64_t(GetCurrentProcessId());
}

// No major need to implement this, and getting a non-potentially
// stale ID on windows is a bit involved.
pid_t getppid() {
  return (pid_t)1;
}

int getuid() {
  return 1;
}

int isatty(int fh) {
  return _isatty(fh);
}

int lockf(int fd, int cmd, off_t len) {
  return _locking(fd, cmd, len);
}

off_t lseek(int fh, off_t off, int orig) {
  return _lseek(fh, off, orig);
}

int rmdir(const char* path) {
  return _rmdir(path);
}

ssize_t readlink(const char* path, char* buf, size_t buflen) {
  if (!buflen) {
    return -1;
  }

  HANDLE h = CreateFileA(
      path,
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS,
      nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }

  DWORD ret =
      GetFinalPathNameByHandleA(h, buf, DWORD(buflen - 1), VOLUME_NAME_DOS);
  if (ret >= buflen || ret >= MAX_PATH || !ret) {
    CloseHandle(h);
    return -1;
  }

  CloseHandle(h);
  buf[ret] = '\0';
  return ret;
}

void* sbrk(intptr_t /* i */) {
  return (void*)-1;
}

unsigned int sleep(unsigned int seconds) {
  Sleep((DWORD)(seconds * 1000));
  return 0;
}

long sysconf(int tp) {
  switch (tp) {
    case _SC_PAGESIZE: {
      SYSTEM_INFO inf;
      GetSystemInfo(&inf);
      return (long)inf.dwPageSize;
    }
    case _SC_NPROCESSORS_ONLN: {
      SYSTEM_INFO inf;
      GetSystemInfo(&inf);
      return (long)inf.dwNumberOfProcessors;
    }
    default:
      return -1L;
  }
}

int truncate(const char* path, off_t len) {
  int fd = _open(path, O_WRONLY);
  if (!fd) {
    return -1;
  }
  if (ftruncate(fd, len)) {
    _close(fd);
    return -1;
  }
  return _close(fd) ? -1 : 0;
}

int usleep(unsigned int ms) {
  Sleep((DWORD)(ms / 1000));
  return 0;
}

}
}
}

#endif
