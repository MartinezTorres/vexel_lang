#include "process.hpp"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace vx {
std::pair<std::string, std::string> runProcess(const std::vector<std::string>& args, const std::string& request, std::size_t timeoutMs, bool includeStderr,
        const std::string& directory,std::size_t maxOutput) {
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    if (args.empty()) return {{}, "unavailable"};
    // An anonymous input file and a pipe keep the protocol ordinary stdio;
    // no network capability or SIGPIPE handler is needed in the parent.
    FILE* inputFile = std::tmpfile();
    if (!inputFile) return {{}, "unavailable"};
    if (std::fwrite(request.data(), 1, request.size(), inputFile) != request.size() ||
        std::fflush(inputFile) != 0) { std::fclose(inputFile); return {{}, "unavailable"}; }
    std::rewind(inputFile);
    int outputPipe[2];
    if (pipe2(outputPipe, O_CLOEXEC) != 0) { std::fclose(inputFile); return {{}, "unavailable"}; }
    pid_t child = fork();
    if (child == -1) { close(outputPipe[0]); close(outputPipe[1]); std::fclose(inputFile); return {{}, "unavailable"}; }
    if (!child) {
        (void)setpgid(0, 0);
        if (!directory.empty() && chdir(directory.c_str()) != 0) _exit(126);
        (void)dup2(fileno(inputFile), STDIN_FILENO); (void)dup2(outputPipe[1], STDOUT_FILENO);
        int null = includeStderr ? dup(outputPipe[1]) : open("/dev/null", O_WRONLY);
        if (null >= 0) { (void)dup2(null, STDERR_FILENO); if (null > 2) close(null); }
        if (outputPipe[0] > 2) close(outputPipe[0]);
        if (outputPipe[1] > 2) close(outputPipe[1]);
        if (fileno(inputFile) > 2) close(fileno(inputFile));
        // Execute exactly the caller-selected argv. Only explicitly enabled
        // process imports select a shell; model output never becomes argv.
        execvp(argv[0],argv.data());
        _exit(127);
    }
    std::fclose(inputFile); close(outputPipe[1]); (void)setpgid(child, child);
    int channel = outputPipe[0]; (void)fcntl(channel, F_SETFL, O_NONBLOCK);
    auto start = std::chrono::steady_clock::now();
    std::string output, failure;
    bool closed = false, exited = false;
    int status = 0;
    while (!closed || !exited) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        if (static_cast<std::size_t>(elapsed) >= timeoutMs) { failure = "timeout"; break; }
        if (!exited) {
            pid_t got = waitpid(child, &status, WNOHANG);
            if (got == child) exited = true;
            else if (got == -1 && errno != EINTR) { failure = "process-error"; break; }
        }
        pollfd descriptor{channel, POLLIN, 0};
        int ready = poll(&descriptor, 1, 10);
        if (ready < 0 && errno != EINTR) { failure = "io-error"; break; }
        if (ready < 0) continue;
        if (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) {
            char bytes[4096]; auto got = read(channel, bytes, sizeof(bytes));
            if (got > 0) output.append(bytes, static_cast<std::size_t>(got));
            else if (!got) closed = true;
            else if (errno != EINTR && errno != EAGAIN) { failure = "io-error"; break; }
            if (output.size() > maxOutput) { failure = "response-too-large"; break; }
        }
    }
    close(channel);
    // Terminate any helper descendants too; a provider invocation is bounded.
    (void)kill(-child, SIGKILL);
    if (!exited) { (void)kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
    if (!failure.empty()) return {output.substr(0, maxOutput), failure};
    if (!WIFEXITED(status) || WEXITSTATUS(status)) return {output, "provider-failed"};
    return {output, "received"};
}
} // namespace vx
