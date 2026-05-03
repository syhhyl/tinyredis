#pragma once

#include "event_loop.h"

#include <unistd.h>
#include <vector>

class EventLoopBackend {
 public:
  virtual ~EventLoopBackend() {
    if (backendFd_ >= 0) {
      close(backendFd_);
    }
  }

  virtual bool valid() const { return backendFd_ >= 0; }
  virtual bool addRead(int fd) = 0;
  virtual bool setWrite(int fd, bool enabled) = 0;
  virtual void remove(int fd) = 0;
  virtual std::vector<Event> wait() = 0;

 protected:
  int backendFd_ = -1;
};
