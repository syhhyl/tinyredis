#include "event_loop.h"

#include <cassert>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

struct SocketPair {
  int a = -1;
  int b = -1;
};

SocketPair makeSocketPair() {
  int fds[2] = {-1, -1};
  int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
  assert(rc == 0);
  return {fds[0], fds[1]};
}

void closePair(const SocketPair& pair) {
  if (pair.a >= 0) {
    close(pair.a);
  }
  if (pair.b >= 0) {
    close(pair.b);
  }
}

bool hasEvent(const std::vector<Event>& events, int fd, bool* readable = nullptr,
              bool* writable = nullptr, bool* closed = nullptr) {
  for (const Event& event : events) {
    if (event.fd != fd) {
      continue;
    }
    if (readable != nullptr) {
      *readable = event.readable;
    }
    if (writable != nullptr) {
      *writable = event.writable;
    }
    if (closed != nullptr) {
      *closed = event.closed;
    }
    return true;
  }
  return false;
}

void testEventLoopValid() {
  EventLoop loop;
  assert(loop.valid());
  std::cout << "PASS testEventLoopValid\n";
}

void testAddReadAndWaitReadable() {
  EventLoop loop;
  assert(loop.valid());

  SocketPair pair = makeSocketPair();
  assert(loop.addRead(pair.a));

  const std::string payload = "PING";
  ssize_t sent = write(pair.b, payload.data(), payload.size());
  assert(sent == static_cast<ssize_t>(payload.size()));

  std::vector<Event> events = loop.wait();
  bool readable = false;
  assert(hasEvent(events, pair.a, &readable));
  assert(readable);

  char buffer[16];
  ssize_t n = read(pair.a, buffer, sizeof(buffer));
  assert(n == sent);

  closePair(pair);
  std::cout << "PASS testAddReadAndWaitReadable\n";
}

void testSetWriteToggle() {
  EventLoop loop;
  assert(loop.valid());

  SocketPair pair = makeSocketPair();
  assert(loop.addRead(pair.a));

  assert(loop.setWrite(pair.a, true));
  std::vector<Event> writableEvents = loop.wait();

  bool writable = false;
  assert(hasEvent(writableEvents, pair.a, nullptr, &writable));
  assert(writable);

  assert(loop.setWrite(pair.a, false));

  closePair(pair);
  std::cout << "PASS testSetWriteToggle\n";
}

void testRemoveStopsReadNotifications() {
  EventLoop loop;
  assert(loop.valid());

  SocketPair monitored = makeSocketPair();
  SocketPair wakeup = makeSocketPair();

  assert(loop.addRead(monitored.a));
  assert(loop.addRead(wakeup.a));
  loop.remove(monitored.a);

  const std::string monitoredPayload = "X";
  assert(write(monitored.b, monitoredPayload.data(), monitoredPayload.size()) == 1);

  const std::string wakePayload = "W";
  assert(write(wakeup.b, wakePayload.data(), wakePayload.size()) == 1);

  std::vector<Event> events = loop.wait();
  assert(!hasEvent(events, monitored.a));

  bool wakeReadable = false;
  assert(hasEvent(events, wakeup.a, &wakeReadable));
  assert(wakeReadable);

  char discard = 0;
  assert(read(wakeup.a, &discard, 1) == 1);

  closePair(monitored);
  closePair(wakeup);
  std::cout << "PASS testRemoveStopsReadNotifications\n";
}

void testSetWriteFailsOnInvalidFd() {
  EventLoop loop;
  assert(loop.valid());
  assert(!loop.setWrite(-1, true));
  std::cout << "PASS testSetWriteFailsOnInvalidFd\n";
}

#if defined(__linux__)
void testSetWriteFailsWhenFdNotRegistered() {
  EventLoop loop;
  assert(loop.valid());

  SocketPair pair = makeSocketPair();
  assert(!loop.setWrite(pair.a, true));

  closePair(pair);
  std::cout << "PASS testSetWriteFailsWhenFdNotRegistered\n";
}
#endif

}  // namespace

int main() {
  testEventLoopValid();
  testAddReadAndWaitReadable();
  testSetWriteToggle();
  testRemoveStopsReadNotifications();
  testSetWriteFailsOnInvalidFd();
#if defined(__linux__)
  testSetWriteFailsWhenFdNotRegistered();
#endif
  std::cout << "PASS all EventLoop tests\n";
  return 0;
}
