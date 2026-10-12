#include "util/process.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>

// glibc's <unistd.h> declares this too, but macOS and other libcs don't.
extern char** environ; // NOLINT(readability-redundant-declaration)

namespace lv::process {

std::optional<std::string> capture(const std::vector<std::string>& argv) {
    std::array<int, 2> out{};
    if (pipe(out.data()) != 0) return std::nullopt;
    posix_spawn_file_actions_t actions{};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, out[0]);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    std::vector<std::string> args = argv;
    std::vector<char*> ptrs;
    ptrs.reserve(args.size() + 1);
    for (auto& a : args) ptrs.push_back(a.data());
    ptrs.push_back(nullptr);
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, ptrs[0], &actions, nullptr, ptrs.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(out[1]);
    std::string result;
    if (rc == 0) {
        std::array<char, 4096> buf{};
        while (true) {
            const ssize_t n = read(out[0], buf.data(), buf.size());
            if (n > 0) {
                result.append(buf.data(), static_cast<std::size_t>(n));
            } else if (n == 0 || errno != EINTR) {
                break;
            }
        }
    }
    close(out[0]);
    if (rc != 0) return std::nullopt;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return std::nullopt;
    while (result.ends_with('\n') || result.ends_with('\r')) result.pop_back();
    return result;
}

} // namespace lv::process
