// Keep the owned session leader alive through cancellation, even when Python
// exits before a stubborn GPT descendant. Never signal a persisted process ID.
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t cancelled = 0;
void cancel(int) {
  cancelled = 1;
}
} // namespace
int main(int argc, char **argv) {
  char *end = nullptr;
  const long grace = argc > 1 ? std::strtol(argv[1], &end, 10) : 0;
  if (argc < 3 || !end || *end || grace < 1 || grace > 30 || getpgrp() != getpid() ||
      getsid(0) != getpid()) {
    std::fputs("Required processing host must lead a new isolated session.\n", stderr);
    return 2;
  }
  struct sigaction action{};
  action.sa_handler = cancel;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);
  if (cancelled)
    return 143;
  const pid_t worker = fork();
  if (worker < 0) {
    std::perror("Cannot start raster worker");
    return 127;
  }
  if (worker == 0) {
    action.sa_handler = SIG_DFL;
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    execv(argv[2], argv + 2);
    std::perror("Cannot execute raster Python");
    _exit(127);
  }
  bool reaped = false, stopping = false;
  int status = 0;
  std::chrono::steady_clock::time_point started;
  for (;;) {
    if (cancelled && !stopping) {
      stopping = true;
      started = std::chrono::steady_clock::now();
      // This process remains the live session/group leader until the final
      // signal. Its ID therefore cannot be reused for an unrelated job.
      kill(0, SIGTERM);
    }
    if (!reaped) {
      const auto result = waitpid(worker, &status, WNOHANG);
      reaped = result == worker;
      if (result < 0 && errno != EINTR) {
        status = 127 << 8;
        reaped = true;
      }
    }
    if (stopping) {
      if (std::chrono::steady_clock::now() - started >= std::chrono::seconds(grace)) {
        kill(0, SIGKILL);
        _exit(137);
      }
    } else if (reaped) {
      return WIFEXITED(status) ? WEXITSTATUS(status)
                               : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    }
    timespec delay{0, 20 * 1000 * 1000};
    nanosleep(&delay, nullptr);
  }
}
