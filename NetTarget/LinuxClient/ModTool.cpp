// HoverNetModTool -- the moderators' command line client for HoverNetAuthServer's console, and the audit log checker.
//
//   HoverNetModTool --port P --name ada --secret-file FILE "REPORTS" "MUTE bob 10" ...    run commands, print the replies
//   HoverNetModTool audit-verify audit.log                                              check the audit log's hash chain
//
// The secret file holds the moderator's secret on one line; it never crosses the network (only an HMAC of the server's challenge).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "AuthSocket.h"
#include "../Authority/Hmac.h"
#include "../Authority/Moderation.h"

int main(int argc, char** argv)
{
    if (argc >= 3 && std::string(argv[1]) == "audit-verify") {
        std::string error;
        if (Authority::AuditLog::VerifyFile(argv[2], error)) { std::printf("audit log intact\n"); return 0; }
        std::printf("AUDIT LOG BROKEN: %s\n", error.c_str());
        return 1;
    }
    int port = 0;
    std::string name, secretFile;
    std::vector<std::string> commands;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--name" && i + 1 < argc) name = argv[++i];
        else if (a == "--secret-file" && i + 1 < argc) secretFile = argv[++i];
        else commands.push_back(a);
    }
    if (port == 0 || name.empty() || secretFile.empty()) { std::fprintf(stderr, "usage: %s --port P --name NAME --secret-file FILE COMMAND...\n", argv[0]); return 2; }
    std::string secret;
    { std::ifstream f(secretFile.c_str()); std::getline(f, secret); }
    while (!secret.empty() && (secret.back() == '\r' || secret.back() == ' ')) secret.pop_back();
    if (secret.empty()) { std::fprintf(stderr, "empty secret\n"); return 2; }
    if (!AuthNet::Startup()) return 1;
    AuthSocket s = AuthNet::Connect("127.0.0.1", port);
    if (s == kInvalidSocket) { std::fprintf(stderr, "cannot connect to the console on port %d\n", port); return 1; }
    std::string inbox;
    auto readLine = [&](std::string& line) {
        for (int waited = 0; waited < 5000; ++waited) {
            const std::size_t nl = inbox.find('\n');
            if (nl != std::string::npos) { line = inbox.substr(0, nl); inbox.erase(0, nl + 1); return true; }
            char buf[1024];
            const int n = static_cast<int>(recv(s, buf, sizeof(buf), 0));
            if (n > 0) inbox.append(buf, static_cast<std::size_t>(n));
            else if (n == 0) return false;
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };
    auto sendLine = [&](const std::string& text) { const std::string l = text + "\n"; return send(s, l.data(), static_cast<int>(l.size()), 0) == static_cast<int>(l.size()); };
    std::string line;
    if (!readLine(line) || line.compare(0, 10, "CHALLENGE ") != 0) { std::fprintf(stderr, "no challenge from the console\n"); return 1; }
    const std::string challenge = line.substr(10);
    sendLine("AUTH " + name + " " + Hmac::Sha256Hex(secret, challenge + ":" + name));
    if (!readLine(line) || line.compare(0, 2, "OK") != 0) { std::fprintf(stderr, "login refused: %s\n", line.c_str()); return 1; }
    std::printf("%s\n", line.c_str());
    for (const std::string& c : commands) {
        sendLine(c);
        // A reply ends with a line that starts with OK or ERR.
        while (readLine(line)) {
            std::printf("%s\n", line.c_str());
            if (line.compare(0, 2, "OK") == 0 || line.compare(0, 3, "ERR") == 0) break;
        }
    }
    sendLine("QUIT");
    AuthNet::CloseSocket(s);
    return 0;
}
