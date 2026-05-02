#pragma once

#include <memory>
#include <vector>

class EventLoopBackend;

struct Event {
  int fd = -1;
  bool readable = false;
  bool writable = false;
  bool closed = false;
};

class EventLoop {
 public:
  EventLoop();
  ~EventLoop();

  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;

  bool valid() const;
  bool addRead(int fd);
  bool setWrite(int fd, bool enabled);
  void remove(int fd);
  std::vector<Event> wait();

 private:
  std::unique_ptr<EventLoopBackend> backend_;
};
