#include "event_loop.h"

#include "event_loop_backend.h"

#include <memory>

#if defined(__APPLE__)
std::unique_ptr<EventLoopBackend> createKqueueEventLoopBackend();
#elif defined(__linux__)
std::unique_ptr<EventLoopBackend> createEpollEventLoopBackend();
#endif

EventLoop::EventLoop()
#if defined(__APPLE__)
    : backend_(createKqueueEventLoopBackend())
#elif defined(__linux__)
    : backend_(createEpollEventLoopBackend())
#else
    : backend_(nullptr)
#endif
{}

EventLoop::~EventLoop() = default;

bool EventLoop::valid() const {
  return backend_ != nullptr && backend_->valid();
}

bool EventLoop::addRead(int fd) {
  return valid() && backend_->addRead(fd);
}

bool EventLoop::setWrite(int fd, bool enabled) {
  return valid() && backend_->setWrite(fd, enabled);
}

void EventLoop::remove(int fd) {
  if (valid()) {
    backend_->remove(fd);
  }
}

std::vector<Event> EventLoop::wait() {
  if (!valid()) {
    return {};
  }
  return backend_->wait();
}
