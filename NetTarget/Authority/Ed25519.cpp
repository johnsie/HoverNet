#include "Ed25519.h"

#include <cstring>
#include <vector>

namespace Ed25519
{
namespace
{
typedef std::int64_t i64;
typedef std::uint64_t u64;
typedef i64 gf[16];

// ---- SHA-512 ------------------------------------------------------------------------------------------------------
const u64 kK[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

inline u64 Rotr(u64 x, int n) { return (x >> n) | (x << (64 - n)); }
}

void Sha512(const std::uint8_t* data, std::size_t size, std::uint8_t out[64])
{
    u64 h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    std::vector<std::uint8_t> m(data, data + size);
    m.push_back(0x80);
    while (m.size() % 128 != 112) m.push_back(0);
    for (int i = 0; i < 8; ++i) m.push_back(0);                                           // the high 64 bits of the 128-bit length
    const u64 bits = static_cast<u64>(size) * 8;
    for (int i = 7; i >= 0; --i) m.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    for (std::size_t chunk = 0; chunk < m.size(); chunk += 128) {
        u64 w[80];
        for (int i = 0; i < 16; ++i) { w[i] = 0; for (int j = 0; j < 8; ++j) w[i] = (w[i] << 8) | m[chunk + 8 * i + j]; }
        for (int i = 16; i < 80; ++i) {
            const u64 s0 = Rotr(w[i - 15], 1) ^ Rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const u64 s1 = Rotr(w[i - 2], 19) ^ Rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        u64 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            const u64 S1 = Rotr(e, 14) ^ Rotr(e, 18) ^ Rotr(e, 41);
            const u64 ch = (e & f) ^ (~e & g);
            const u64 t1 = hh + S1 + ch + kK[i] + w[i];
            const u64 S0 = Rotr(a, 28) ^ Rotr(a, 34) ^ Rotr(a, 39);
            const u64 maj = (a & b) ^ (a & c) ^ (b & c);
            const u64 t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; ++i) for (int j = 0; j < 8; ++j) out[8 * i + j] = static_cast<std::uint8_t>(h[i] >> (56 - 8 * j));
}

namespace
{
// ---- the field of integers modulo 2^255 - 19 (TweetNaCl representation: 16 limbs of 16 bits in signed 64-bit) ---------------
const gf gf0 = {0}, gf1 = {1};
const gf D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
const gf D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
const gf X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
const gf Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
const gf I = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};

void Set(gf r, const gf a) { for (int i = 0; i < 16; ++i) r[i] = a[i]; }

void Car(gf o)
{
    for (int i = 0; i < 16; ++i) {
        o[i] += (1LL << 16);
        const i64 c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

void Sel(gf p, gf q, int b)
{
    const i64 c = ~(static_cast<i64>(b) - 1);
    for (int i = 0; i < 16; ++i) { const i64 t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}

void Pack(std::uint8_t* o, const gf n)
{
    gf m, t;
    Set(t, n);
    Car(t); Car(t); Car(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) { m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1); m[i - 1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = static_cast<int>((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        Sel(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) { o[2 * i] = static_cast<std::uint8_t>(t[i] & 0xff); o[2 * i + 1] = static_cast<std::uint8_t>(t[i] >> 8); }
}

int Neq(const gf a, const gf b)
{
    std::uint8_t c[32], d[32];
    Pack(c, a); Pack(d, b);
    unsigned diff = 0;
    for (int i = 0; i < 32; ++i) diff |= c[i] ^ d[i];
    return diff != 0;
}

std::uint8_t Par(const gf a) { std::uint8_t d[32]; Pack(d, a); return d[0] & 1; }

void Unpack(gf o, const std::uint8_t* n)
{
    for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + (static_cast<i64>(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

void Add(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i]; }
void Sub(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i]; }

void Mul(gf o, const gf a, const gf b)
{
    i64 t[31];
    for (int i = 0; i < 31; ++i) t[i] = 0;
    for (int i = 0; i < 16; ++i) for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    Car(o); Car(o);
}

void Sq(gf o, const gf a) { Mul(o, a, a); }

void Inv(gf o, const gf in)
{
    gf c;
    Set(c, in);
    for (int a = 253; a >= 0; --a) { Sq(c, c); if (a != 2 && a != 4) Mul(c, c, in); }
    Set(o, c);
}

void Pow2523(gf o, const gf in)
{
    gf c;
    Set(c, in);
    for (int a = 250; a >= 0; --a) { Sq(c, c); if (a != 1) Mul(c, c, in); }
    Set(o, c);
}

// ---- the curve (extended coordinates) -------------------------------------------------------------------------------
void AddPoints(gf p[4], gf q[4])
{
    gf a, b, c, d, t, e, f, g, h;
    Sub(a, p[1], p[0]); Sub(t, q[1], q[0]); Mul(a, a, t);
    Add(b, p[0], p[1]); Add(t, q[0], q[1]); Mul(b, b, t);
    Mul(c, p[3], q[3]); Mul(c, c, D2);
    Mul(d, p[2], q[2]); Add(d, d, d);
    Sub(e, b, a); Sub(f, d, c); Add(g, d, c); Add(h, b, a);
    Mul(p[0], e, f); Mul(p[1], h, g); Mul(p[2], g, f); Mul(p[3], e, h);
}

void CSwap(gf p[4], gf q[4], std::uint8_t b) { for (int i = 0; i < 4; ++i) Sel(p[i], q[i], b); }

void PackPoint(std::uint8_t* r, gf p[4])
{
    gf tx, ty, zi;
    Inv(zi, p[2]); Mul(tx, p[0], zi); Mul(ty, p[1], zi);
    Pack(r, ty);
    r[31] ^= Par(tx) << 7;
}

void ScalarMult(gf p[4], gf q[4], const std::uint8_t* s)
{
    Set(p[0], gf0); Set(p[1], gf1); Set(p[2], gf1); Set(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        const std::uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        CSwap(p, q, b);
        AddPoints(q, p);
        AddPoints(p, p);
        CSwap(p, q, b);
    }
}

void ScalarBase(gf p[4], const std::uint8_t* s)
{
    gf q[4];
    Set(q[0], X); Set(q[1], Y); Set(q[2], gf1); Mul(q[3], X, Y);
    ScalarMult(p, q, s);
}

const u64 kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};

void ModL(std::uint8_t* r, i64 x[64])
{
    i64 carry;
    for (int i = 63; i >= 32; --i) {
        carry = 0;
        int j;
        for (j = i - 32; j < i - 12; ++j) { x[j] += carry - 16 * x[i] * static_cast<i64>(kL[j - (i - 32)]); carry = (x[j] + 128) >> 8; x[j] -= carry << 8; }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (int j = 0; j < 32; ++j) { x[j] += carry - (x[31] >> 4) * static_cast<i64>(kL[j]); carry = x[j] >> 8; x[j] &= 255; }
    for (int j = 0; j < 32; ++j) x[j] -= carry * static_cast<i64>(kL[j]);
    for (int i = 0; i < 32; ++i) { x[i + 1] += x[i] >> 8; r[i] = static_cast<std::uint8_t>(x[i] & 255); }
}

void Reduce(std::uint8_t* r)
{
    i64 x[64];
    for (int i = 0; i < 64; ++i) x[i] = static_cast<u64>(r[i]);
    for (int i = 0; i < 64; ++i) r[i] = 0;
    ModL(r, x);
}

int UnpackNeg(gf r[4], const std::uint8_t p[32])
{
    gf t, chk, num, den, den2, den4, den6;
    Set(r[2], gf1);
    Unpack(r[1], p);
    Sq(num, r[1]);
    Mul(den, num, D);
    Sub(num, num, r[2]);
    Add(den, r[2], den);
    Sq(den2, den); Sq(den4, den2); Mul(den6, den4, den2);
    Mul(t, den6, num); Mul(t, t, den);
    Pow2523(t, t);
    Mul(t, t, num); Mul(t, t, den); Mul(t, t, den);
    Mul(r[0], t, den);
    Sq(chk, r[0]); Mul(chk, chk, den);
    if (Neq(chk, num)) Mul(r[0], r[0], I);
    Sq(chk, r[0]); Mul(chk, chk, den);
    if (Neq(chk, num)) return -1;
    if (Par(r[0]) == (p[31] >> 7)) Sub(r[0], gf0, r[0]);
    Mul(r[3], r[0], r[1]);
    return 0;
}

void Expand(const std::uint8_t seed[32], std::uint8_t h[64])
{
    Sha512(seed, 32, h);
    h[0] &= 248; h[31] &= 127; h[31] |= 64;
}
}

void PublicKey(const std::uint8_t seed[32], std::uint8_t publicKey[32])
{
    std::uint8_t h[64];
    gf p[4];
    Expand(seed, h);
    ScalarBase(p, h);
    PackPoint(publicKey, p);
}

void Sign(const std::uint8_t seed[32], const std::uint8_t* message, std::size_t size, std::uint8_t signature[64])
{
    std::uint8_t h[64], pk[32], r[64], hram[64];
    Expand(seed, h);
    PublicKey(seed, pk);
    std::vector<std::uint8_t> buf(32 + size);
    std::memcpy(buf.data(), h + 32, 32);
    if (size) std::memcpy(buf.data() + 32, message, size);
    Sha512(buf.data(), buf.size(), r);                       // r = H(prefix || M)
    Reduce(r);
    gf p[4];
    ScalarBase(p, r);
    PackPoint(signature, p);                                 // R
    buf.assign(64 + size, 0);
    std::memcpy(buf.data(), signature, 32);
    std::memcpy(buf.data() + 32, pk, 32);
    if (size) std::memcpy(buf.data() + 64, message, size);
    Sha512(buf.data(), buf.size(), hram);                    // H(R || A || M)
    Reduce(hram);
    i64 x[64];
    for (int i = 0; i < 64; ++i) x[i] = 0;
    for (int i = 0; i < 32; ++i) x[i] = static_cast<u64>(r[i]);
    for (int i = 0; i < 32; ++i) for (int j = 0; j < 32; ++j) x[i + j] += static_cast<i64>(hram[i]) * static_cast<i64>(h[j]);
    ModL(signature + 32, x);                                 // S = r + H(...) * a mod L
}

bool Verify(const std::uint8_t publicKey[32], const std::uint8_t* message, std::size_t size, const std::uint8_t signature[64])
{
    // S must be below L, so a signature cannot be altered into another valid one.
    bool below = false;
    for (int i = 31; i >= 0; --i) {
        if (signature[32 + i] < kL[i]) { below = true; break; }
        if (signature[32 + i] > kL[i]) return false;
    }
    if (!below) return false;
    gf p[4], q[4];
    if (UnpackNeg(q, publicKey) != 0) return false;
    std::vector<std::uint8_t> buf(64 + size);
    std::memcpy(buf.data(), signature, 32);
    std::memcpy(buf.data() + 32, publicKey, 32);
    if (size) std::memcpy(buf.data() + 64, message, size);
    std::uint8_t h[64];
    Sha512(buf.data(), buf.size(), h);
    Reduce(h);
    ScalarMult(p, q, h);
    gf s[4];
    ScalarBase(s, signature + 32);
    AddPoints(p, s);
    std::uint8_t t[32];
    PackPoint(t, p);
    unsigned diff = 0;
    for (int i = 0; i < 32; ++i) diff |= t[i] ^ signature[i];
    return diff == 0;
}
}
