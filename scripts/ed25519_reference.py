"""A small independent Ed25519 (RFC 8032) used only to cross-check HoverNetSign in scripts/test-signing.sh. Not for production use."""
import hashlib

p = 2**255 - 19
q = 2**252 + 27742317777372353535851937790883648493

def inv(x): return pow(x, p - 2, p)
d = -121665 * inv(121666) % p
sqrt_m1 = pow(2, (p - 1) // 4, p)

def sha512(s): return hashlib.sha512(s).digest()

def add(P, Q):
    A = (P[1] - P[0]) * (Q[1] - Q[0]) % p
    B = (P[1] + P[0]) * (Q[1] + Q[0]) % p
    C = 2 * P[3] * Q[3] * d % p
    D = 2 * P[2] * Q[2] % p
    E, F, G, H = B - A, D - C, D + C, B + A
    return (E * F % p, G * H % p, F * G % p, E * H % p)

def mul(s, P):
    Q = (0, 1, 1, 0)
    while s > 0:
        if s & 1: Q = add(Q, P)
        P = add(P, P)
        s >>= 1
    return Q

def equal(P, Q):
    return (P[0] * Q[2] - Q[0] * P[2]) % p == 0 and (P[1] * Q[2] - Q[1] * P[2]) % p == 0

def recover_x(y, sign):
    if y >= p: return None
    x2 = (y * y - 1) * inv(d * y * y + 1) % p
    if x2 == 0: return None if sign else 0
    x = pow(x2, (p + 3) // 8, p)
    if (x * x - x2) % p != 0: x = x * sqrt_m1 % p
    if (x * x - x2) % p != 0: return None
    if (x & 1) != sign: x = p - x
    return x

g_y = 4 * inv(5) % p
g_x = recover_x(g_y, 0)
G = (g_x, g_y, 1, g_x * g_y % p)

def compress(P):
    zinv = inv(P[2])
    x, y = P[0] * zinv % p, P[1] * zinv % p
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")

def decompress(s):
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % p)

def expand(seed):
    h = sha512(seed)
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]

def public_key(seed):
    a, _ = expand(seed)
    return compress(mul(a, G))

def sign(seed, msg):
    a, prefix = expand(seed)
    A = compress(mul(a, G))
    r = int.from_bytes(sha512(prefix + msg), "little") % q
    R = compress(mul(r, G))
    h = int.from_bytes(sha512(R + A + msg), "little") % q
    s = (r + h * a) % q
    return R + int.to_bytes(s, 32, "little")

def verify(pub, msg, sig):
    if len(pub) != 32 or len(sig) != 64: return False
    A = decompress(pub)
    if A is None: return False
    R = decompress(sig[:32])
    if R is None: return False
    s = int.from_bytes(sig[32:], "little")
    if s >= q: return False
    h = int.from_bytes(sha512(sig[:32] + pub + msg), "little") % q
    return equal(mul(s, G), add(R, mul(h, A)))
