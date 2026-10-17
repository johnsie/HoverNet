#include "Crypto.h"

#include <cstring>
#include <random>

#include "Hmac.h"

namespace Crypto
{
// ---- ChaCha20 (RFC 8439 section 2) -------------------------------------------------------------------------
namespace
{
inline std::uint32_t Rotl(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
inline std::uint32_t Load32(const std::uint8_t* p) { return p[0] | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
inline void Store32(std::uint8_t* p, std::uint32_t v) { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); p[2] = static_cast<std::uint8_t>(v >> 16); p[3] = static_cast<std::uint8_t>(v >> 24); }

#define QR(a, b, c, d) a += b; d ^= a; d = Rotl(d, 16); c += d; b ^= c; b = Rotl(b, 12); a += b; d ^= a; d = Rotl(d, 8); c += d; b ^= c; b = Rotl(b, 7);
}

void ChaCha20Block(const std::uint8_t key[32], std::uint32_t counter, const std::uint8_t nonce[12], std::uint8_t out[64])
{
    std::uint32_t s[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (int i = 0; i < 8; ++i) s[4 + i] = Load32(key + 4 * i);
    s[12] = counter;
    for (int i = 0; i < 3; ++i) s[13 + i] = Load32(nonce + 4 * i);
    std::uint32_t x[16];
    std::memcpy(x, s, sizeof(x));
    for (int round = 0; round < 10; ++round) {
        QR(x[0], x[4], x[8], x[12]) QR(x[1], x[5], x[9], x[13]) QR(x[2], x[6], x[10], x[14]) QR(x[3], x[7], x[11], x[15])
        QR(x[0], x[5], x[10], x[15]) QR(x[1], x[6], x[11], x[12]) QR(x[2], x[7], x[8], x[13]) QR(x[3], x[4], x[9], x[14])
    }
    for (int i = 0; i < 16; ++i) Store32(out + 4 * i, x[i] + s[i]);
}

void ChaCha20Xor(const std::uint8_t key[32], std::uint32_t counter, const std::uint8_t nonce[12], const std::uint8_t* in, std::size_t size, std::uint8_t* out)
{
    std::uint8_t block[64];
    for (std::size_t offset = 0; offset < size; offset += 64, ++counter) {
        ChaCha20Block(key, counter, nonce, block);
        const std::size_t n = size - offset < 64 ? size - offset : 64;
        for (std::size_t i = 0; i < n; ++i) out[offset + i] = in[offset + i] ^ block[i];
    }
}

// ---- Poly1305 (RFC 8439 section 2.5), the 17-limb byte-wise form of TweetNaCl ----------------------------------
namespace
{
const std::uint32_t kMinusP[17] = {5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 252};

void Add1305(std::uint32_t* h, const std::uint32_t* c)
{
    std::uint32_t u = 0;
    for (int j = 0; j < 17; ++j) { u += h[j] + c[j]; h[j] = u & 255; u >>= 8; }
}
}

void Poly1305(const std::uint8_t key[32], const std::uint8_t* m, std::size_t n, std::uint8_t out[16])
{
    std::uint32_t x[17], r[17], h[17], c[17], g[17];
    for (int j = 0; j < 17; ++j) r[j] = h[j] = 0;
    for (int j = 0; j < 16; ++j) r[j] = key[j];
    r[3] &= 15; r[4] &= 252; r[7] &= 15; r[8] &= 252; r[11] &= 15; r[12] &= 252; r[15] &= 15;
    while (n > 0) {
        for (int j = 0; j < 17; ++j) c[j] = 0;
        std::size_t j = 0;
        for (; j < 16 && j < n; ++j) c[j] = m[j];
        c[j] = 1;
        m += j; n -= j;
        Add1305(h, c);
        for (int i = 0; i < 17; ++i) {
            x[i] = 0;
            for (int k = 0; k < 17; ++k) x[i] += h[k] * ((k <= i) ? r[i - k] : 320 * r[i + 17 - k]);
        }
        for (int i = 0; i < 17; ++i) h[i] = x[i];
        std::uint32_t u = 0;
        for (int k = 0; k < 16; ++k) { u += h[k]; h[k] = u & 255; u >>= 8; }
        u += h[16]; h[16] = u & 3;
        u = 5 * (u >> 2);
        for (int k = 0; k < 16; ++k) { u += h[k]; h[k] = u & 255; u >>= 8; }
        u += h[16]; h[16] = u;
    }
    for (int j = 0; j < 17; ++j) g[j] = h[j];
    Add1305(h, kMinusP);
    const std::uint32_t s = 0u - (h[16] >> 7);
    for (int j = 0; j < 17; ++j) h[j] ^= s & (g[j] ^ h[j]);
    for (int j = 0; j < 16; ++j) c[j] = key[j + 16];
    c[16] = 0;
    Add1305(h, c);
    for (int j = 0; j < 16; ++j) out[j] = static_cast<std::uint8_t>(h[j]);
}

// ---- AEAD (RFC 8439 section 2.8) -------------------------------------------------------------------------------
namespace
{
void AeadTag(const std::uint8_t key[32], const std::uint8_t nonce[12], const std::uint8_t* aad, std::size_t aadSize,
             const std::uint8_t* cipher, std::size_t size, std::uint8_t tag[16])
{
    std::uint8_t block[64];
    ChaCha20Block(key, 0, nonce, block);   // the first 32 bytes are the one-time Poly1305 key
    Bytes mac;
    auto pad = [&mac]() { while (mac.size() % 16 != 0) mac.push_back(0); };
    mac.insert(mac.end(), aad, aad + aadSize);
    pad();
    mac.insert(mac.end(), cipher, cipher + size);
    pad();
    for (int i = 0; i < 8; ++i) mac.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(aadSize) >> (8 * i)));
    for (int i = 0; i < 8; ++i) mac.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(size) >> (8 * i)));
    Poly1305(block, mac.data(), mac.size(), tag);
}
}

void AeadSeal(const std::uint8_t key[32], const std::uint8_t nonce[12], const std::uint8_t* aad, std::size_t aadSize,
              const std::uint8_t* plain, std::size_t size, std::uint8_t* cipher, std::uint8_t tag[16])
{
    ChaCha20Xor(key, 1, nonce, plain, size, cipher);
    AeadTag(key, nonce, aad, aadSize, cipher, size, tag);
}

bool AeadOpen(const std::uint8_t key[32], const std::uint8_t nonce[12], const std::uint8_t* aad, std::size_t aadSize,
              const std::uint8_t* cipher, std::size_t size, const std::uint8_t tag[16], std::uint8_t* plain)
{
    std::uint8_t expected[16];
    AeadTag(key, nonce, aad, aadSize, cipher, size, expected);
    if (!ConstantTimeEqual(expected, tag, 16)) return false;
    ChaCha20Xor(key, 1, nonce, cipher, size, plain);
    return true;
}

// ---- X25519 (RFC 7748), field arithmetic in 16 limbs of 16 bits after TweetNaCl ---------------------------------
namespace
{
typedef std::int64_t gf[16];
const gf k121665 = {0xDB41, 1};

void Car25519(gf o)
{
    for (int i = 0; i < 16; ++i) {
        o[i] += (1LL << 16);
        const std::int64_t c = o[i] >> 16;          // arithmetic shift of a non-negative biased value
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * 65536;
    }
}

void Sel25519(gf p, gf q, std::int64_t b)
{
    const std::int64_t c = ~(b - 1);
    for (int i = 0; i < 16; ++i) { const std::int64_t t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}

void Pack25519(std::uint8_t* o, const gf n)
{
    gf m, t;
    for (int i = 0; i < 16; ++i) t[i] = n[i];
    Car25519(t); Car25519(t); Car25519(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) { m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1); m[i - 1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const std::int64_t b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        Sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) { o[2 * i] = static_cast<std::uint8_t>(t[i] & 0xff); o[2 * i + 1] = static_cast<std::uint8_t>(t[i] >> 8); }
}

void Unpack25519(gf o, const std::uint8_t* n)
{
    for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + (static_cast<std::int64_t>(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

void A(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i]; }
void Z(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i]; }
void M(gf o, const gf a, const gf b)
{
    std::int64_t t[31];
    for (int i = 0; i < 31; ++i) t[i] = 0;
    for (int i = 0; i < 16; ++i) for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    Car25519(o);
    Car25519(o);
}
void S(gf o, const gf a) { M(o, a, a); }

void Inv25519(gf o, const gf in)
{
    gf c;
    for (int a = 0; a < 16; ++a) c[a] = in[a];
    for (int a = 253; a >= 0; --a) {
        S(c, c);
        if (a != 2 && a != 4) M(c, c, in);
    }
    for (int a = 0; a < 16; ++a) o[a] = c[a];
}
}

void X25519(std::uint8_t q[32], const std::uint8_t n[32], const std::uint8_t p[32])
{
    std::uint8_t z[32];
    std::int64_t x[80];
    gf a, b, c, d, e, f;
    for (int i = 0; i < 31; ++i) z[i] = n[i];
    z[31] = (n[31] & 127) | 64;
    z[0] &= 248;
    Unpack25519(x, p);
    for (int i = 0; i < 16; ++i) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        const std::int64_t r = (z[i >> 3] >> (i & 7)) & 1;
        Sel25519(a, b, r);
        Sel25519(c, d, r);
        A(e, a, c); Z(a, a, c); A(c, b, d); Z(b, b, d);
        S(d, e); S(f, a); M(a, c, a); M(c, b, e);
        A(e, a, c); Z(a, a, c); S(b, a); Z(c, d, f);
        M(a, c, k121665); A(a, a, d); M(c, c, a); M(a, d, f);
        M(d, b, x); S(b, e);
        Sel25519(a, b, r);
        Sel25519(c, d, r);
    }
    for (int i = 0; i < 16; ++i) { x[i + 16] = a[i]; x[i + 32] = c[i]; x[i + 48] = b[i]; x[i + 64] = d[i]; }
    Inv25519(x + 32, x + 32);
    M(x + 16, x + 16, x + 32);
    Pack25519(q, x + 16);
}

void X25519Public(std::uint8_t out[32], const std::uint8_t secret[32])
{
    std::uint8_t base[32] = {9};
    X25519(out, secret, base);
}

// ---- HKDF (RFC 5869) with HMAC-SHA256 --------------------------------------------------------------------------
Bytes HkdfExtract(const Bytes& salt, const Bytes& ikm)
{
    return Hmac::Sha256Raw(std::string(salt.begin(), salt.end()), std::string(ikm.begin(), ikm.end()));
}

Bytes HkdfExpand(const Bytes& prk, const Bytes& info, std::size_t length)
{
    Bytes out, t;
    for (std::uint8_t counter = 1; out.size() < length; ++counter) {
        std::string message(t.begin(), t.end());
        message.append(info.begin(), info.end());
        message.push_back(static_cast<char>(counter));
        t = Hmac::Sha256Raw(std::string(prk.begin(), prk.end()), message);
        out.insert(out.end(), t.begin(), t.end());
    }
    out.resize(length);
    return out;
}

// ---- helpers -------------------------------------------------------------------------------------------------
bool RandomBytes(std::uint8_t* out, std::size_t size)
{
    try {
        std::random_device device;
        for (std::size_t i = 0; i < size; i += 4) {
            const std::uint32_t v = device();
            for (std::size_t k = 0; k < 4 && i + k < size; ++k) out[i + k] = static_cast<std::uint8_t>(v >> (8 * k));
        }
        return true;
    }
    catch (...) {
        return false;
    }
}

std::string ToHex(const std::uint8_t* data, std::size_t size)
{
    static const char digits[] = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < size; ++i) { s.push_back(digits[data[i] >> 4]); s.push_back(digits[data[i] & 15]); }
    return s;
}

bool FromHex(const std::string& hex, Bytes& out)
{
    out.clear();
    if (hex.size() % 2 != 0) return false;
    auto nibble = [](char c, int& v) { if (c >= '0' && c <= '9') v = c - '0'; else if (c >= 'a' && c <= 'f') v = c - 'a' + 10; else if (c >= 'A' && c <= 'F') v = c - 'A' + 10; else return false; return true; };
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int hi, lo;
        if (!nibble(hex[i], hi) || !nibble(hex[i + 1], lo)) return false;
        out.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
    }
    return true;
}

bool ConstantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, std::size_t size)
{
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < size; ++i) diff |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}
}
