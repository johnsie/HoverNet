// HoverNetSign -- sign and verify files with Ed25519 (NetTarget/Authority/Ed25519.*), for signed catalogues and release files.
//
//   HoverNetSign keygen KEYFILE              create a new secret key (64 hex characters, owner-only) and print its public key
//   HoverNetSign pubkey KEYFILE              print the public key (64 hex characters)
//   HoverNetSign sign   KEYFILE FILE [SIG]   write the signature of the file's bytes (128 hex characters) to SIG (default FILE.sig)
//   HoverNetSign verify PUBKEY FILE [SIG]    PUBKEY is 64 hex characters, or a file holding them; exit status 0 when the signature is valid
//
// The secret key never leaves its file; keep it off the build machines that only need to verify. Pure Ed25519 signs the file's bytes
// directly (RFC 8032), so a signature can be checked with any conforming implementation.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "../Authority/Ed25519.h"

namespace
{
std::string Hex(const std::uint8_t* d, std::size_t n)
{
    static const char kDigits[] = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) { s += kDigits[d[i] >> 4]; s += kDigits[d[i] & 15]; }
    return s;
}

bool FromHex(const std::string& text, std::uint8_t* out, std::size_t n)
{
    std::string t;
    for (char c : text) if (c != '\n' && c != '\r' && c != ' ') t += c;
    if (t.size() != 2 * n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        int v[2];
        for (int k = 0; k < 2; ++k) {
            const char c = t[2 * i + k];
            if (c >= '0' && c <= '9') v[k] = c - '0'; else if (c >= 'a' && c <= 'f') v[k] = c - 'a' + 10; else if (c >= 'A' && c <= 'F') v[k] = c - 'A' + 10; else return false;
        }
        out[i] = static_cast<std::uint8_t>(v[0] * 16 + v[1]);
    }
    return true;
}

bool ReadAll(const std::string& path, std::vector<std::uint8_t>& out)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool ReadText(const std::string& path, std::string& out)
{
    std::vector<std::uint8_t> b;
    if (!ReadAll(path, b)) return false;
    out.assign(b.begin(), b.end());
    return true;
}

bool LoadSeed(const std::string& path, std::uint8_t seed[32])
{
    std::string text;
    return ReadText(path, text) && FromHex(text, seed, 32);
}
}

int main(int argc, char** argv)
{
    const std::string cmd = argc >= 2 ? argv[1] : "";
    std::uint8_t seed[32], pub[32];
    if (cmd == "keygen" && argc >= 3) {
        std::ifstream exists(argv[2]);
        if (exists) { std::fprintf(stderr, "%s exists; refusing to overwrite a key\n", argv[2]); return 1; }
        std::random_device rd;
        for (int i = 0; i < 32; ++i) seed[i] = static_cast<std::uint8_t>(rd());
        {
            std::ofstream f(argv[2]);
            f << Hex(seed, 32) << "\n";
        }
#ifndef _WIN32
        chmod(argv[2], 0600);
#endif
        Ed25519::PublicKey(seed, pub);
        std::printf("%s\n", Hex(pub, 32).c_str());
        return 0;
    }
    if (cmd == "pubkey" && argc >= 3) {
        if (!LoadSeed(argv[2], seed)) { std::fprintf(stderr, "cannot read a key from %s\n", argv[2]); return 1; }
        Ed25519::PublicKey(seed, pub);
        std::printf("%s\n", Hex(pub, 32).c_str());
        return 0;
    }
    if (cmd == "sign" && argc >= 4) {
        std::vector<std::uint8_t> data;
        if (!LoadSeed(argv[2], seed)) { std::fprintf(stderr, "cannot read a key from %s\n", argv[2]); return 1; }
        if (!ReadAll(argv[3], data)) { std::fprintf(stderr, "cannot read %s\n", argv[3]); return 1; }
        std::uint8_t sig[64];
        Ed25519::Sign(seed, data.data(), data.size(), sig);
        const std::string out = argc >= 5 ? argv[4] : std::string(argv[3]) + ".sig";
        std::ofstream f(out.c_str());
        f << Hex(sig, 64) << "\n";
        if (!f) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
        return 0;
    }
    if (cmd == "verify" && argc >= 4) {
        std::string keyText = argv[2], sigText;
        std::vector<std::uint8_t> data;
        if (keyText.size() != 64) { std::string fromFile; if (ReadText(argv[2], fromFile)) keyText = fromFile; }
        std::uint8_t sig[64];
        const std::string sigPath = argc >= 5 ? argv[4] : std::string(argv[3]) + ".sig";
        if (!FromHex(keyText, pub, 32)) { std::fprintf(stderr, "the public key is not 64 hex characters\n"); return 1; }
        if (!ReadText(sigPath, sigText) || !FromHex(sigText, sig, 64)) { std::fprintf(stderr, "cannot read a signature from %s\n", sigPath.c_str()); return 1; }
        if (!ReadAll(argv[3], data)) { std::fprintf(stderr, "cannot read %s\n", argv[3]); return 1; }
        if (!Ed25519::Verify(pub, data.data(), data.size(), sig)) { std::fprintf(stderr, "BAD signature on %s\n", argv[3]); return 1; }
        std::printf("good signature on %s\n", argv[3]);
        return 0;
    }
    std::fprintf(stderr, "usage: %s keygen KEY | pubkey KEY | sign KEY FILE [SIG] | verify PUBKEY FILE [SIG]\n", argv[0]);
    return 2;
}
