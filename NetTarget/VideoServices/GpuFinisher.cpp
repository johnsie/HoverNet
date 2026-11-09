#include "GpuFinisher.h"

#include <SDL.h>

#include <chrono>
#include <cstring>
#include <vector>

namespace HoverNetGfx
{
namespace
{
// Just the GL types and constants this file uses: no GL headers, so it builds the same on every platform and loads the functions at run time.
typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei; typedef unsigned int GLenum; typedef float GLfloat; typedef char GLchar; typedef unsigned char GLboolean; typedef unsigned int GLbitfield;
enum : GLenum {
    kTexture2D = 0x0DE1, kRGB = 0x1907, kRGBA = 0x1908, kRed = 0x1903, kR8 = 0x8229, kRGB8 = 0x8051, kRGBA8 = 0x8058, kUnsignedByte = 0x1401,
    kMinFilter = 0x2801, kMagFilter = 0x2800, kNearest = 0x2600, kLinear = 0x2601, kWrapS = 0x2802, kWrapT = 0x2803, kClampToEdge = 0x812F,
    kFramebuffer = 0x8D40, kColorAttachment0 = 0x8CE0, kFramebufferComplete = 0x8CD5, kFragmentShader = 0x8B30, kVertexShader = 0x8B31,
    kCompileStatus = 0x8B81, kLinkStatus = 0x8B82, kInfoLogLength = 0x8B84, kTriangles = 4, kUnpackAlignment = 0x0CF5, kPackAlignment = 0x0D05,
    kTexture0 = 0x84C0, kR16UI = 0x8234, kR8UI = 0x8232, kRedInteger = 0x8D94, kUnsignedShort = 0x1403, kRenderer = 0x1F01, kBlend = 0x0BE2, kDepthTest = 0x0B71, kCullFace = 0x0B44, kScissorTest = 0x0C11
};

struct Gl
{
    void (*GenTextures)(GLsizei, GLuint*) = nullptr;
    void (*DeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (*BindTexture)(GLenum, GLuint) = nullptr;
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
    void (*TexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void (*PixelStorei)(GLenum, GLint) = nullptr;
    GLuint (*CreateShader)(GLenum) = nullptr;
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*CompileShader)(GLuint) = nullptr;
    void (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    GLuint (*CreateProgram)() = nullptr;
    void (*AttachShader)(GLuint, GLuint) = nullptr;
    void (*LinkProgram)(GLuint) = nullptr;
    void (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*UseProgram)(GLuint) = nullptr;
    void (*DeleteShader)(GLuint) = nullptr;
    GLint (*GetUniformLocation)(GLuint, const GLchar*) = nullptr;
    void (*Uniform1i)(GLint, GLint) = nullptr;
    void (*Uniform1f)(GLint, GLfloat) = nullptr;
    void (*Uniform2f)(GLint, GLfloat, GLfloat) = nullptr;
    void (*Uniform2i)(GLint, GLint, GLint) = nullptr;
    void (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*Uniform4i)(GLint, GLint, GLint, GLint, GLint) = nullptr;
    void (*Uniform4fv)(GLint, GLsizei, const GLfloat*) = nullptr;
    void (*Uniform1iv)(GLint, GLsizei, const GLint*) = nullptr;
    void (*GenFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (*BindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    GLenum (*CheckFramebufferStatus)(GLenum) = nullptr;
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*DrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    void (*GenVertexArrays)(GLsizei, GLuint*) = nullptr;
    void (*BindVertexArray)(GLuint) = nullptr;
    void (*ActiveTexture)(GLenum) = nullptr;
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*Disable)(GLenum) = nullptr;
    const unsigned char* (*GetString)(GLenum) = nullptr;
    void (*Finish)() = nullptr;
    void (*GetIntegerv)(GLenum, GLint*) = nullptr;
};

template <typename T> bool Load(T& fn, const char* name) { fn = reinterpret_cast<T>(SDL_GL_GetProcAddress(name)); return fn != nullptr; }

const char* kVertex = R"(
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";


// The scene pass: everything that needs the depth of the picture. Edge smoothing along the edges the rasteriser knows, the sky-tinted ambient light and haze,
// contact shading, the lights (falloff, spot cones, shadows by looking along the ray, a diffuse term from the surface's slope, glints), the craft's rim light, its
// reflection in the floor and the glows round missiles, mines, power-ups and other craft. It follows the processor's versions (Atmosphere, Lighting, CraftShading,
// Reflection, Halos, EdgeAA) but works per pixel, not per block of four, and takes the surface's normal from the depth around each pixel.
const char* kScene = R"(
in vec2 vUv; out vec4 o;
const int MAXL = 12;
uniform sampler2D uSrc; uniform sampler2D uMask;
uniform highp usampler2D uDepth; uniform highp usampler2D uTags;
uniform ivec2 uSize; uniform ivec4 uWin; uniform vec4 uProj; uniform vec3 uSky; uniform int uHasMask, uHasTags, uFlags, uBlock;
uniform int uLightCount; uniform vec4 uLA[MAXL]; uniform vec4 uLB[MAXL]; uniform vec4 uLC[MAXL]; uniform int uLK[MAXL];
uniform vec4 uCraft;   // craft x, y, radius, ground row

const int TAG_FLOOR = 1, TAG_CEILING = 2, TAG_CRAFT = 5;
bool hud(ivec2 p) { return uHasMask != 0 && texelFetch(uMask, clamp(p, ivec2(0), uSize - 1), 0).r > 0.001; }
float depthRaw(ivec2 p) {
    ivec2 q = p - uWin.xy;
    if (q.x < 0 || q.y < 0 || q.x >= uWin.z || q.y >= uWin.w) return 65535.0;
    return float(texelFetch(uDepth, q, 0).r);
}
int tagAt(ivec2 p) {
    if (uHasTags == 0) return 0;
    ivec2 q = p - uWin.xy;
    if (q.x < 0 || q.y < 0 || q.x >= uWin.z || q.y >= uWin.w) return 0;
    return int(texelFetch(uTags, q, 0).r);
}
vec3 camPos(ivec2 p, float z) { float X = z * 4.0; return vec3(X, (uProj.z - float(p.x)) * X * uProj.x, (uProj.w - float(p.y)) * X * uProj.y); }
float smooth01(float t) { t = clamp(t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }
float hazeAt(float z) { return z >= 65535.0 ? 0.0 : smooth01((z * 4.0 / 1000.0 - 12.0) / 108.0) * 89.0; }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
bool differ(ivec2 a, ivec2 b) {
    float za = depthRaw(a), zb = depthRaw(b);
    if ((za == 65535.0) != (zb == 65535.0)) return true;
    if (za != 65535.0 && abs(za - zb) > 120.0 + za / 10.0) return true;
    int ta = tagAt(a), tb = tagAt(b);
    return ta != tb && ta != 0 && tb != 0;
}
vec3 srcAt(ivec2 p) { return texelFetch(uSrc, clamp(p, ivec2(0), uSize - 1), 0).rgb * 255.0; }
float glowShape(vec2 d, vec2 r) { vec2 q = d / r; float d2 = dot(q, q); return d2 >= 1.0 ? 0.0 : (1.0 - d2) * (1.0 - d2); }

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 src = texelFetch(uSrc, p, 0).rgb;
    if (hud(p)) { o = vec4(src, 1.0); return; }
    vec3 c = src * 255.0;
    float zr = depthRaw(p);
    bool hit = zr < 65535.0;
    int tag = tagAt(p);
    bool inWin = !(p.x < uWin.x || p.y < uWin.y || p.x >= uWin.x + uWin.z || p.y >= uWin.y + uWin.w);
    if (!inWin) { o = vec4(src, 1.0); return; }

    // edge smoothing: a pixel with an edge on a side takes a share of the colour on the other side of it
    if ((uFlags & 64) != 0) {
        bool right = differ(p, p + ivec2(1, 0)), below = differ(p, p + ivec2(0, 1)), left = differ(p + ivec2(-1, 0), p), up = differ(p + ivec2(0, -1), p);
        vec3 sum = vec3(0.0); float n = 0.0;
        if (right && !hud(p + ivec2(1, 0))) { sum += srcAt(p + ivec2(1, 0)); n += 1.0; }
        if (left && !hud(p + ivec2(-1, 0))) { sum += srcAt(p + ivec2(-1, 0)); n += 1.0; }
        if (below && !hud(p + ivec2(0, 1))) { sum += srcAt(p + ivec2(0, 1)); n += 1.0; }
        if (up && !hud(p + ivec2(0, -1))) { sum += srcAt(p + ivec2(0, -1)); n += 1.0; }
        if (n > 0.0) c += (sum / n - c) * (n >= 2.0 ? 120.0 / 256.0 : 84.0 / 256.0);
    }

    if (hit) {
        float z = zr;
        vec3 pos = camPos(p, z);
        // sky-tinted ambient light and haze
        if ((uFlags & 1) != 0) {
            if (tag == TAG_FLOOR) c += uSky * 9.0 / 256.0;
            else if (tag == TAG_CEILING) c *= 236.0 / 256.0;
            float haze = hazeAt(z);
            if (haze > 0.0) {
                c += (luma(c) - c) * (floor(haze * 100.0 / 256.0) / 256.0);
                if (tag == TAG_FLOOR) c += (uSky - c) * (floor(haze * 140.0 / 256.0) / 256.0);
            }
        }
        if ((uFlags & 512) != 0) c += (uSky - c) * (hazeAt(z) / 256.0);   // the lighting pass's own haze
        // contact shading: something closer a little way beside the surface darkens it
        float occ = 0.0;
        if ((uFlags & 4) != 0) {
            int reach = 2 * uBlock;
            ivec2 nb[4] = ivec2[4](ivec2(-reach, 0), ivec2(reach, 0), ivec2(0, -reach), ivec2(0, reach));
            float total = 0.0;
            for (int k = 0; k < 4; ++k) {
                ivec2 q = p + nb[k];
                float zn = depthRaw(q);
                if (zn == 65535.0) continue;
                float delta = (z - zn) * 4.0;
                if (delta <= 150.0 || delta >= 2500.0) continue;
                total += delta < 1000.0 ? min(256.0, (delta - 150.0) * 256.0 / 500.0) : max(0.0, 256.0 - (delta - 1000.0) * 256.0 / 1500.0);
            }
            occ = floor(total * 70.0 / (4.0 * 256.0));
        }
        // the lights
        vec3 mul = vec3(0.0), add = vec3(0.0);
        if ((uFlags & 2) != 0 && uLightCount > 0) {
            // the surface's normal from the depth around the pixel
            vec3 n = -normalize(pos);
            float zl = depthRaw(p + ivec2(-2, 0)), zrr = depthRaw(p + ivec2(2, 0)), zu = depthRaw(p + ivec2(0, -2)), zd = depthRaw(p + ivec2(0, 2));
            float limit = 40.0 + z / 20.0;
            if (zl < 65535.0 && zrr < 65535.0 && zu < 65535.0 && zd < 65535.0 && abs(zl - z) < limit && abs(zrr - z) < limit && abs(zu - z) < limit && abs(zd - z) < limit) {
                vec3 a = camPos(p + ivec2(2, 0), zrr) - camPos(p + ivec2(-2, 0), zl), b = camPos(p + ivec2(0, 2), zd) - camPos(p + ivec2(0, -2), zu);
                vec3 q = cross(a, b);
                if (dot(q, pos) > 0.0) q = -q;
                float ql = length(q);
                if (ql > 1e-3) n = q / ql;
            }
            vec3 view = -pos; float vlen = length(view) + 1.0; view /= vlen;
            for (int li = 0; li < MAXL; ++li) {
                if (li >= uLightCount) break;
                int kind = uLK[li];
                vec3 lp = uLA[li].xyz; float radius = uLA[li].w; float strength = uLC[li].w;
                if (strength <= 0.01 || radius < 50.0) continue;
                vec3 d = pos - lp;
                if (abs(d.x) > radius) continue;
                float d2 = dot(d, d) / (radius * radius);
                if (d2 >= 1.0) continue;
                float f = (1.0 - d2) * (1.0 - d2) * strength;
                float coneCos = uLB[li].w;
                if (coneCos > -1.0) {
                    float along = dot(d, uLB[li].xyz) / (sqrt(d2) * radius + 1.0);
                    f *= smooth01((along - coneCos) / 0.18);
                    if (f <= 0.0) continue;
                }
                if ((uFlags & 128) != 0 && f > 0.01) {   // walls and objects in the way block the light
                    vec3 sp = pos; float span = length(lp - sp);
                    if (span > 1500.0) {
                        float blocked = 0.0;
                        for (int k = 0; k < 3; ++k) {
                            float t = 0.3 + 0.25 * float(k);
                            vec3 pp = sp + (lp - sp) * t;
                            if (pp.x < 300.0) continue;
                            if ((1.0 - t) * span < 1500.0) continue;
                            ivec2 qpix = ivec2(int(uProj.z - pp.y / (pp.x * uProj.x)), int(uProj.w - pp.z / (pp.x * uProj.y)));
                            if (qpix.x < uWin.x || qpix.y < uWin.y || qpix.x >= uWin.x + uWin.z || qpix.y >= uWin.y + uWin.w) continue;
                            if (tagAt(qpix) == TAG_CRAFT) continue;
                            float zq = depthRaw(qpix);
                            if (zq < 65535.0 && zq * 4.0 < pp.x - 350.0) blocked += 1.0;
                        }
                        f *= 1.0 - 0.8 * blocked / 3.0;
                    }
                }
                float dist = sqrt(d2) * radius + 1.0;
                vec3 L = -d / dist;
                float ndotl = dot(n, L);
                float diffuse = 0.25 + 0.75 * clamp(ndotl, 0.0, 1.0);
                vec3 hv = L + view; float hl = length(hv) + 1e-4;
                float nh = clamp(dot(n, hv / hl), 0.0, 1.0); nh *= nh; nh *= nh; nh *= nh; nh *= nh;
                float glint = ndotl > 0.0 ? nh * f * 0.6 : 0.0;
                vec3 col = uLC[li].rgb * 255.0;
                mul += f * diffuse * 150.0 * col / 255.0;
                add += col * (f * diffuse * 0.12 + glint * 0.5);
            }
        }
        float keep = (256.0 - occ) / 256.0;
        if ((uFlags & 256) != 0) c = sqrt(max(c * c * ((256.0 + mul) / 256.0) * keep, vec3(0.0))) + add;
        else c = c * ((256.0 + mul) / 256.0) * keep + add;

        // the craft: a cool rim light and a sheen of sky on its hull, glints from lights near it
        if ((uFlags & 8) != 0 && tag == TAG_CRAFT) {
            vec3 n = -normalize(pos);
            if (tagAt(p + ivec2(-2, 0)) == TAG_CRAFT && tagAt(p + ivec2(2, 0)) == TAG_CRAFT && tagAt(p + ivec2(0, -2)) == TAG_CRAFT && tagAt(p + ivec2(0, 2)) == TAG_CRAFT) {
                float zl = depthRaw(p + ivec2(-2, 0)), zrr = depthRaw(p + ivec2(2, 0)), zu = depthRaw(p + ivec2(0, -2)), zd = depthRaw(p + ivec2(0, 2));
                float limit = 40.0 + z / 20.0;
                if (abs(zl - z) < limit && abs(zrr - z) < limit && abs(zu - z) < limit && abs(zd - z) < limit) {
                    vec3 a = camPos(p + ivec2(2, 0), zrr) - camPos(p + ivec2(-2, 0), zl), b = camPos(p + ivec2(0, 2), zd) - camPos(p + ivec2(0, -2), zu);
                    vec3 q = cross(a, b);
                    if (dot(q, pos) > 0.0) q = -q;
                    n = q / (length(q) + 1e-3);
                }
            }
            vec3 v = -pos / (length(pos) + 1.0);
            float nv = clamp(dot(n, v), 0.0, 1.0);
            float edge = 1.0 - nv, rim = edge * edge * edge;
            vec3 plus = vec3(150.0, 190.0, 255.0) * rim * 0.30 + uSky * (edge * edge * 0.16);
            for (int li = 0; li < MAXL; ++li) {
                if (li >= uLightCount) break;
                int kind = uLK[li];
                if (kind != 1 && kind != 2 && kind != 3) continue;
                vec3 dd = uLA[li].xyz - pos;
                float r2 = uLA[li].w * uLA[li].w; float dist2 = dot(dd, dd);
                if (dist2 >= r2) continue;
                vec3 L = dd / (sqrt(dist2) + 1.0);
                vec3 hv = L + v; float nh = clamp(dot(n, hv / (length(hv) + 1e-4)), 0.0, 1.0);
                nh *= nh; nh *= nh; nh *= nh; nh *= nh; nh *= nh;
                float fall = 1.0 - dist2 / r2;
                plus += uLC[li].rgb * 255.0 * (nh * fall * fall * uLC[li].w * 0.9 * 0.7);
            }
            c += plus;
        }
    }
    else if ((uFlags & 1) != 0) {   // the sky: a warm glow on the horizon
        int band = max(4, uWin.w / 10); float horizon = uProj.w;
        float t = 1.0 - abs(float(p.y) - horizon) / float(band);
        if (t > 0.0) {
            float amount = floor(t * t * 46.0);
            c += (vec3(255.0, 236.0, 200.0) - c) * (amount * 7.0 / 2048.0);
        }
    }

    // the craft's reflection in the floor beside and under it
    if ((uFlags & 16) != 0 && tag == TAG_FLOOR && uCraft.z >= 4.0) {
        float ground = uCraft.w, reach = floor(uCraft.z * 1.3), halfw = floor(uCraft.z * 1.7);
        float dy = float(p.y) - ground;
        if (dy >= 0.0 && dy < reach && abs(float(p.x) - uCraft.x) < halfw) {
            ivec2 mirror = ivec2(p.x, int(ground - dy));
            if (tagAt(mirror) == TAG_CRAFT && !hud(mirror)) {
                float fade = 1.0 - dy / reach;
                float amount = floor(fade * fade * 78.0);
                if (amount > 0.0) c += (srcAt(mirror) - c) * (amount / 256.0);
            }
        }
    }

    // glows round missiles, mines, power-ups and other craft
    if ((uFlags & 32) != 0) {
        for (int li = 0; li < MAXL; ++li) {
            if (li >= uLightCount) break;
            int kind = uLK[li];
            vec3 col; float strength, scale;
            if (kind == 5) { col = vec3(255.0, 205.0, 130.0); strength = 0.85; scale = 0.16; }
            else if (kind == 6) { col = vec3(255.0, 80.0, 70.0); strength = 0.55; scale = 0.14; }
            else if (kind == 7) { col = vec3(120.0, 255.0, 190.0); strength = 0.6; scale = 0.14; }
            else if (kind == 4) { col = vec3(120.0, 185.0, 255.0); strength = 0.38; scale = 0.11; }
            else if (kind == 8) { col = vec3(190.0, 210.0, 255.0); strength = 0.3; scale = 0.11; }
            else continue;
            vec3 lp = uLA[li].xyz;
            if (lp.x < 500.0) continue;
            vec2 centre = vec2(uProj.z - lp.y / (lp.x * uProj.x), uProj.w - lp.z / (lp.x * uProj.y));
            ivec2 cp = ivec2(centre);
            if (cp.x < uWin.x || cp.y < uWin.y || cp.x >= uWin.x + uWin.z || cp.y >= uWin.y + uWin.w) continue;
            if (depthRaw(cp) * 4.0 < lp.x - 700.0) continue;   // hidden behind something
            float radius = min(70.0, max(4.0, uLA[li].w * scale / (lp.x * uProj.x)));
            vec2 d = vec2(p) - centre;
            float g = glowShape(d, vec2(radius));
            if (kind == 5) g += 0.7 * glowShape(d, vec2(radius * 2.6, radius * 0.25)) + 0.5 * glowShape(d, vec2(radius * 0.25, radius * 2.6));
            c += col * g * strength;
        }
    }
    o = vec4(clamp(c / 255.0, 0.0, 1.0), 1.0);
}
)";

// The main pass: de-banding with dither, edge smoothing and sharpening, in one read of the picture. HUD pixels pass through untouched.
const char* kMain = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform sampler2D uMask;
uniform int uHasMask, uDeband, uSmooth; uniform float uSharpen; uniform ivec2 uSize;
const float bayer[64] = float[64](
     0.0, 32.0,  8.0, 40.0,  2.0, 34.0, 10.0, 42.0,
    48.0, 16.0, 56.0, 24.0, 50.0, 18.0, 58.0, 26.0,
    12.0, 44.0,  4.0, 36.0, 14.0, 46.0,  6.0, 38.0,
    60.0, 28.0, 52.0, 20.0, 62.0, 30.0, 54.0, 22.0,
     3.0, 35.0, 11.0, 43.0,  1.0, 33.0,  9.0, 41.0,
    51.0, 19.0, 59.0, 27.0, 49.0, 17.0, 57.0, 25.0,
    15.0, 47.0,  7.0, 39.0, 13.0, 45.0,  5.0, 37.0,
    63.0, 31.0, 55.0, 23.0, 61.0, 29.0, 53.0, 21.0);
vec3 fetch(ivec2 p) { return texelFetch(uSrc, clamp(p, ivec2(0), uSize - 1), 0).rgb; }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 c = fetch(p);
    if (uHasMask != 0 && texelFetch(uMask, p, 0).r > 0.001) { o = vec4(c, 1.0); return; }
    vec3 n0 = fetch(p + ivec2(0, -1)), n1 = fetch(p + ivec2(0, 1)), n2 = fetch(p + ivec2(-1, 0)), n3 = fetch(p + ivec2(1, 0));
    vec3 avg = (n0 + n1 + n2 + n3) * 0.25;
    vec3 outc = c;
    if (uDeband != 0) {
        vec3 worst = max(max(abs(n0 - c), abs(n1 - c)), max(abs(n2 - c), abs(n3 - c)));
        vec3 band = vec3(greaterThan(worst, vec3(0.5 / 255.0))) * vec3(lessThanEqual(worst, vec3(12.0 / 255.0)));
        outc = mix(c, (c * 8.0 + n0 + n1 + n2 + n3) / 12.0, band);
        float d = (bayer[(p.y & 7) * 8 + (p.x & 7)] * 2.0 - 63.0) * 3.0 / 64.0;
        outc += d / 255.0;
    }
    if (uSmooth != 0) {
        float e = clamp((abs(luma(c) - luma(avg)) - 0.06) / 0.20, 0.0, 1.0) * 0.5;
        outc = mix(outc, avg, e);
    }
    if (uSharpen > 0.0) outc += (c - avg) * uSharpen;
    o = vec4(clamp(outc, 0.0, 1.0), 1.0);
}
)";

// Bloom: a bright pass into half size (a 2x2 average of a bilinear read), or a plain half-size step.
const char* kDown = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uTex; uniform vec2 uTexel; uniform int uBright; uniform float uThreshold;
void main() {
    vec3 c = texture(uTex, vUv + uTexel * vec2(-0.5, -0.5)).rgb + texture(uTex, vUv + uTexel * vec2(0.5, -0.5)).rgb
           + texture(uTex, vUv + uTexel * vec2(-0.5, 0.5)).rgb + texture(uTex, vUv + uTexel * vec2(0.5, 0.5)).rgb;
    c *= 0.25;
    if (uBright != 0) {
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        c *= clamp((l - uThreshold) / (1.0 - uThreshold), 0.0, 1.0);
    }
    o = vec4(c, 1.0);
}
)";

const char* kBlur = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uTex; uniform vec2 uDir;
const float w[5] = float[5](0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);
void main() {
    vec3 c = texture(uTex, vUv).rgb * w[0];
    for (int i = 1; i < 5; ++i) { c += texture(uTex, vUv + uDir * float(i)).rgb * w[i]; c += texture(uTex, vUv - uDir * float(i)).rgb * w[i]; }
    o = vec4(c, 1.0);
}
)";

// The last pass: bloom added back, the highlight roll-off, the vignette, the dark edge beside the HUD, and the HUD's own pixels as they were.
const char* kFinal = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uMain; uniform sampler2D uBloomA; uniform sampler2D uBloomB; uniform sampler2D uSrc; uniform sampler2D uMask;
uniform int uHasMask, uExposure, uOutline; uniform float uBloom, uVignette; uniform ivec2 uSize;
bool hud(ivec2 p) { return uHasMask != 0 && texelFetch(uMask, clamp(p, ivec2(0), uSize - 1), 0).r > 0.001; }
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (hud(p)) { o = vec4(texelFetch(uSrc, p, 0).rgb, 1.0); return; }
    vec3 c = texelFetch(uMain, p, 0).rgb;
    vec2 uv = (vec2(p) + 0.5) / vec2(uSize);
    if (uBloom > 0.0) c += (texture(uBloomA, uv).rgb * 0.55 + texture(uBloomB, uv).rgb * 0.45) * uBloom;
    if (uExposure != 0) {
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        if (l > 0.80) { float x = l - 0.80; c *= (0.80 + x / (1.0 + x * 3.0)) / l; }
    }
    if (uVignette > 0.0) { vec2 d = uv * 2.0 - 1.0; float r2 = dot(d, d) * 0.5; c *= 1.0 - uVignette * r2 * r2; }   // the same curve as the processor's: a quarter off at the corners
    if (uOutline != 0) {
        bool near = false;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) if (hud(p + ivec2(dx, dy))) near = true;
        if (near) c *= 0.55;
    }
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";
}

struct GpuFinisher::Impl
{
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    bool es = false;
    Gl gl;
    GLuint vao = 0;
    GLuint progScene = 0, progMain = 0, progDown = 0, progBlur = 0, progFinal = 0;
    GLuint texDepth = 0, texTags = 0, texScene = 0, fboScene = 0;
    int depthW = 0, depthH = 0, tagsW = 0, tagsH = 0;
    float skyR = 0, skyG = 0, skyB = 0; bool skyKnown = false;
    GLuint texSrc = 0, texMask = 0, texMain = 0, texA1 = 0, texB1 = 0, texA2 = 0, texB2 = 0, texOut = 0;
    GLuint fboMain = 0, fboA1 = 0, fboB1 = 0, fboA2 = 0, fboB2 = 0, fboOut = 0;
    int width = 0, height = 0;
    std::vector<unsigned char> readback;
    GLuint sharedTexture = 0;      // the renderer's texture the picture is written into (0: read it back instead)
    int sharedWidth = 0, sharedHeight = 0;
    bool lastShared = false;
    bool shared = false;           // this context shares objects with the renderer's

    GLuint Compile(GLenum type, const std::string& source, std::string& error)
    {
        const GLuint shader = gl.CreateShader(type);
        const GLchar* text = source.c_str();
        gl.ShaderSource(shader, 1, &text, nullptr);
        gl.CompileShader(shader);
        GLint ok = 0;
        gl.GetShaderiv(shader, kCompileStatus, &ok);
        if (!ok) {
            char log[1024] = {0};
            gl.GetShaderInfoLog(shader, sizeof log - 1, nullptr, log);
            error = std::string("shader: ") + log;
            return 0;
        }
        return shader;
    }

    GLuint Program(const char* fragment, std::string& error)
    {
        const std::string header = es ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n" : "#version 330 core\n";
        const GLuint vs = Compile(kVertexShader, header + kVertex, error);
        const GLuint fs = vs != 0 ? Compile(kFragmentShader, header + fragment, error) : 0;
        if (vs == 0 || fs == 0) return 0;
        const GLuint program = gl.CreateProgram();
        gl.AttachShader(program, vs);
        gl.AttachShader(program, fs);
        gl.LinkProgram(program);
        GLint ok = 0;
        gl.GetProgramiv(program, kLinkStatus, &ok);
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        if (!ok) {
            char log[1024] = {0};
            gl.GetProgramInfoLog(program, sizeof log - 1, nullptr, log);
            error = std::string("link: ") + log;
            return 0;
        }
        return program;
    }

    void MakeTarget(GLuint& texture, GLuint& fbo, int w, int h, GLenum internal)
    {
        if (texture == 0) gl.GenTextures(1, &texture);
        gl.BindTexture(kTexture2D, texture);
        gl.TexImage2D(kTexture2D, 0, static_cast<GLint>(internal), w, h, 0, kRGBA, kUnsignedByte, nullptr);
        gl.TexParameteri(kTexture2D, kMinFilter, kLinear); gl.TexParameteri(kTexture2D, kMagFilter, kLinear);
        gl.TexParameteri(kTexture2D, kWrapS, kClampToEdge); gl.TexParameteri(kTexture2D, kWrapT, kClampToEdge);
        if (fbo == 0) gl.GenFramebuffers(1, &fbo);
        gl.BindFramebuffer(kFramebuffer, fbo);
        gl.FramebufferTexture2D(kFramebuffer, kColorAttachment0, kTexture2D, texture, 0);
    }

    bool Resize(int w, int h)
    {
        if (w == width && h == height) return true;
        width = w; height = h;
        const int w1 = std::max(1, w / 2), h1 = std::max(1, h / 2), w2 = std::max(1, w / 4), h2 = std::max(1, h / 4);
        MakeTarget(texMain, fboMain, w, h, kRGBA8);
        MakeTarget(texScene, fboScene, w, h, kRGBA8);
        MakeTarget(texA1, fboA1, w1, h1, kRGBA8); MakeTarget(texB1, fboB1, w1, h1, kRGBA8);
        MakeTarget(texA2, fboA2, w2, h2, kRGBA8); MakeTarget(texB2, fboB2, w2, h2, kRGBA8);
        if (sharedTexture != 0 && w == sharedWidth && h == sharedHeight) {
            if (fboOut == 0) gl.GenFramebuffers(1, &fboOut);
            gl.BindFramebuffer(kFramebuffer, fboOut);
            gl.FramebufferTexture2D(kFramebuffer, kColorAttachment0, kTexture2D, sharedTexture, 0);
        }
        else {
            sharedTexture = 0;
            MakeTarget(texOut, fboOut, w, h, kRGBA8);
        }
        if (texSrc == 0) gl.GenTextures(1, &texSrc);
        gl.BindTexture(kTexture2D, texSrc);
        gl.PixelStorei(kUnpackAlignment, 1);
        gl.TexImage2D(kTexture2D, 0, kRGB8, w, h, 0, kRGB, kUnsignedByte, nullptr);
        gl.TexParameteri(kTexture2D, kMinFilter, kNearest); gl.TexParameteri(kTexture2D, kMagFilter, kNearest);
        gl.TexParameteri(kTexture2D, kWrapS, kClampToEdge); gl.TexParameteri(kTexture2D, kWrapT, kClampToEdge);
        if (texMask == 0) gl.GenTextures(1, &texMask);
        gl.BindTexture(kTexture2D, texMask);
        gl.TexImage2D(kTexture2D, 0, kR8, w, h, 0, kRed, kUnsignedByte, nullptr);
        gl.TexParameteri(kTexture2D, kMinFilter, kNearest); gl.TexParameteri(kTexture2D, kMagFilter, kNearest);
        gl.TexParameteri(kTexture2D, kWrapS, kClampToEdge); gl.TexParameteri(kTexture2D, kWrapT, kClampToEdge);
        return gl.CheckFramebufferStatus(kFramebuffer) == kFramebufferComplete || true;
    }

    void Draw(GLuint program, GLuint fbo, int w, int h) { gl.UseProgram(program); gl.BindFramebuffer(kFramebuffer, fbo); gl.Viewport(0, 0, w, h); gl.DrawArrays(kTriangles, 0, 3); }
    void Bind(int unit, GLuint texture) { gl.ActiveTexture(kTexture0 + static_cast<GLenum>(unit)); gl.BindTexture(kTexture2D, texture); }
    void Set1i(GLuint program, const char* name, int v) { gl.Uniform1i(gl.GetUniformLocation(program, name), v); }
    void Set1f(GLuint program, const char* name, float v) { gl.Uniform1f(gl.GetUniformLocation(program, name), v); }
    void Set2f(GLuint program, const char* name, float a, float b) { gl.Uniform2f(gl.GetUniformLocation(program, name), a, b); }
    void Set2i(GLuint program, const char* name, int a, int b) { gl.Uniform2i(gl.GetUniformLocation(program, name), a, b); }
};

bool GpuFinisher::OutputIsShared() const { return mImpl->lastShared; }

bool GpuFinisher::AdoptTarget(SDL_Texture* texture, int width, int height)
{
#ifdef __EMSCRIPTEN__
    (void)texture; (void)width; (void)height;
    return false;
#else
    Impl& m = *mImpl;
    if (!mReady || !m.shared || texture == nullptr || SDL_GL_GetCurrentContext() == nullptr || SDL_GL_GetCurrentContext() == m.context) return false;
    float tw = 0, th = 0;
    if (SDL_GL_BindTexture(texture, &tw, &th) != 0) return false;
    GLint id = 0;
    m.gl.GetIntegerv(0x8069 /* GL_TEXTURE_BINDING_2D */, &id);
    SDL_GL_UnbindTexture(texture);
    if (id <= 0) return false;
    m.sharedTexture = static_cast<GLuint>(id);
    m.sharedWidth = width; m.sharedHeight = height;
    m.width = 0; m.height = 0;   // the targets are made again for this
    return true;
#endif
}

GpuFinisher::GpuFinisher() : mImpl(new Impl) {}

GpuFinisher::~GpuFinisher()
{
    if (mImpl->context != nullptr) SDL_GL_DeleteContext(mImpl->context);
    if (mImpl->window != nullptr) SDL_DestroyWindow(mImpl->window);
    delete mImpl;
}

bool GpuFinisher::Init()
{
    if (mReady) return true;
#ifdef __EMSCRIPTEN__
    mError = "the browser build keeps the CPU passes (its one WebGL context belongs to the page's renderer)";
    return false;
#else
    Impl& m = *mImpl;
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0 && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) { mError = SDL_GetError(); return false; }
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver != nullptr && (std::strcmp(driver, "dummy") == 0 || std::strcmp(driver, "offscreen") == 0)) { mError = "this video driver has no OpenGL"; return false; }
    SDL_Window* previousWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext previousContext = SDL_GL_GetCurrentContext();
    const bool canShare = previousContext != nullptr;
    struct Attempt { int profile, major, minor; bool es; };
    const Attempt attempts[] = {{SDL_GL_CONTEXT_PROFILE_CORE, 3, 3, false}, {SDL_GL_CONTEXT_PROFILE_ES, 3, 0, true}};
    for (const Attempt& a : attempts) {
        SDL_GL_ResetAttributes();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, a.profile);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, a.major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, a.minor);
        for (int share = canShare ? 1 : 0; share >= 0 && m.context == nullptr; --share) {   // with the renderer's context first, alone if that is not allowed
            if (share == 1 && previousWindow != nullptr) SDL_GL_MakeCurrent(previousWindow, previousContext);
            SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, share);
            m.window = SDL_CreateWindow("HoverNet GPU", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            if (m.window == nullptr) continue;
            m.context = SDL_GL_CreateContext(m.window);
            if (m.context != nullptr) { m.es = a.es; m.shared = share == 1; break; }
            SDL_DestroyWindow(m.window); m.window = nullptr;
        }
        if (m.context != nullptr) break;
    }
    if (m.context == nullptr) { mError = std::string("no OpenGL 3.3 or ES 3.0 context: ") + SDL_GetError(); if (previousWindow != nullptr) SDL_GL_MakeCurrent(previousWindow, previousContext); return false; }
    SDL_GL_MakeCurrent(m.window, m.context);
    SDL_GL_SetSwapInterval(0);
    Gl& g = m.gl;
    bool ok = Load(g.GenTextures, "glGenTextures") && Load(g.DeleteTextures, "glDeleteTextures") && Load(g.BindTexture, "glBindTexture") && Load(g.TexImage2D, "glTexImage2D") &&
              Load(g.TexSubImage2D, "glTexSubImage2D") && Load(g.TexParameteri, "glTexParameteri") && Load(g.PixelStorei, "glPixelStorei") && Load(g.CreateShader, "glCreateShader") &&
              Load(g.ShaderSource, "glShaderSource") && Load(g.CompileShader, "glCompileShader") && Load(g.GetShaderiv, "glGetShaderiv") && Load(g.GetShaderInfoLog, "glGetShaderInfoLog") &&
              Load(g.CreateProgram, "glCreateProgram") && Load(g.AttachShader, "glAttachShader") && Load(g.LinkProgram, "glLinkProgram") && Load(g.GetProgramiv, "glGetProgramiv") &&
              Load(g.GetProgramInfoLog, "glGetProgramInfoLog") && Load(g.UseProgram, "glUseProgram") && Load(g.DeleteShader, "glDeleteShader") && Load(g.GetUniformLocation, "glGetUniformLocation") &&
              Load(g.Uniform1i, "glUniform1i") && Load(g.Uniform1f, "glUniform1f") && Load(g.Uniform2f, "glUniform2f") && Load(g.Uniform2i, "glUniform2i") && Load(g.GenFramebuffers, "glGenFramebuffers") &&
              Load(g.BindFramebuffer, "glBindFramebuffer") && Load(g.FramebufferTexture2D, "glFramebufferTexture2D") && Load(g.CheckFramebufferStatus, "glCheckFramebufferStatus") &&
              Load(g.Viewport, "glViewport") && Load(g.DrawArrays, "glDrawArrays") && Load(g.GenVertexArrays, "glGenVertexArrays") && Load(g.BindVertexArray, "glBindVertexArray") &&
              Load(g.ActiveTexture, "glActiveTexture") && Load(g.ReadPixels, "glReadPixels") && Load(g.Disable, "glDisable") && Load(g.GetString, "glGetString") && Load(g.Finish, "glFinish") && Load(g.GetIntegerv, "glGetIntegerv") &&
              Load(g.Uniform3f, "glUniform3f") && Load(g.Uniform4f, "glUniform4f") && Load(g.Uniform4i, "glUniform4i") && Load(g.Uniform4fv, "glUniform4fv") && Load(g.Uniform1iv, "glUniform1iv");
    if (!ok) { mError = "OpenGL functions are missing"; }
    else {
        g.GenVertexArrays(1, &m.vao);
        g.BindVertexArray(m.vao);
        std::string error;
        m.progScene = m.Program(kScene, error);
        if (m.progScene != 0) m.progMain = m.Program(kMain, error);
        if (m.progMain != 0) m.progDown = m.Program(kDown, error);
        if (m.progDown != 0) m.progBlur = m.Program(kBlur, error);
        if (m.progBlur != 0) m.progFinal = m.Program(kFinal, error);
        if (m.progFinal == 0) { ok = false; mError = error; }
    }
    if (ok) {
        const unsigned char* name = g.GetString(kRenderer);
        mRenderer = name != nullptr ? reinterpret_cast<const char*>(name) : "";
        mReady = true;
    }
    if (previousWindow != nullptr) SDL_GL_MakeCurrent(previousWindow, previousContext);
    return mReady;
#endif
}

bool GpuFinisher::Process(std::uint8_t* rgb, int width, int height, const Options& options, const FrameHints& hints, const std::uint8_t* hudMask)
{
    if (!mReady || rgb == nullptr || width < 64 || height < 48) return false;
    const auto start = std::chrono::steady_clock::now();
    Impl& m = *mImpl;
    Gl& g = m.gl;
    SDL_Window* previousWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext previousContext = SDL_GL_GetCurrentContext();
    if (SDL_GL_MakeCurrent(m.window, m.context) != 0) return false;
    g.Disable(kBlend); g.Disable(kDepthTest); g.Disable(kCullFace); g.Disable(kScissorTest);
    g.BindVertexArray(m.vao);
    m.Resize(width, height);

    g.PixelStorei(kUnpackAlignment, 1);
    g.BindTexture(kTexture2D, m.texSrc);
    g.TexSubImage2D(kTexture2D, 0, 0, 0, width, height, kRGB, kUnsignedByte, rgb);
    const bool hasMask = hudMask != nullptr;
    if (hasMask) { g.BindTexture(kTexture2D, m.texMask); g.TexSubImage2D(kTexture2D, 0, 0, 0, width, height, kRed, kUnsignedByte, hudMask); }

    const bool deband = options.quality >= 1 && options.deband, smooth = options.quality >= 2 && options.smoothing, sharpen = options.quality >= 2 && options.sharpen;
    const bool bloom = options.quality >= 2 && options.bloom, exposure = options.quality >= 1 && options.exposure, vignette = options.grade, outline = hints.valid && options.hudOutline;

    // 0: the scene pass (needs the depth): edge smoothing, haze and ambient light, contact shading, lights, craft, reflection, halos
    const bool lighting = options.lighting;
    int flags = 0;
    if (options.quality >= 1 && options.atmosphere && lighting) flags |= 1;
    if (lighting) flags |= 2;
    if (options.quality >= 1 && lighting) flags |= 4 | 512;
    if (options.quality >= 2 && options.craftShading && !hints.cockpit && hints.tags != nullptr) flags |= 8;
    if (options.quality >= 1 && options.reflections && !hints.cockpit && hints.craftVisible && hints.tags != nullptr) flags |= 16;
    if (options.halos && lighting) flags |= 32;
    if (options.quality >= 1 && options.edgeAA) flags |= 64;
    if (options.quality >= 1 && options.occlusion) flags |= 128;
    if (options.quality >= 2 && options.linearLight) flags |= 256;
    GLuint pictureTexture = m.texSrc;
    const bool scene = hints.valid && hints.depth != nullptr && hints.depthWidth > 0 && hints.depthHeight > 0 && flags != 0 && hints.projKx > 0.0f && hints.projKz > 0.0f;
    if (scene) {
        // the sky's colour, from sky pixels of the upper half of the view, smoothed from frame to frame (as the processor does)
        {
            const int x0 = std::max(0, hints.depthX0), y0 = std::max(0, hints.depthY0), x1 = std::min(width, hints.depthX0 + hints.depthWidth), y1 = std::min(height, hints.depthY0 + hints.depthHeight);
            int sr = 0, sg = 0, sb = 0, count = 0;
            for (int y = y0; y < y0 + (y1 - y0) / 2; y += 6)
                for (int x = x0; x < x1; x += 9) {
                    if (hints.depth[static_cast<std::size_t>(y - hints.depthY0) * hints.depthWidth + (x - hints.depthX0)] != 0xFFFF || (hudMask != nullptr && hudMask[static_cast<std::size_t>(y) * width + x])) continue;
                    const std::uint8_t* px = rgb + (static_cast<std::size_t>(y) * width + x) * 3;
                    sr += px[0]; sg += px[1]; sb += px[2]; ++count;
                }
            if (count >= 8) {
                const float r = static_cast<float>(sr) / count, gg = static_cast<float>(sg) / count, b = static_cast<float>(sb) / count;
                if (!m.skyKnown) { m.skyR = r; m.skyG = gg; m.skyB = b; m.skyKnown = true; }
                else { m.skyR += (r - m.skyR) * 0.25f; m.skyG += (gg - m.skyG) * 0.25f; m.skyB += (b - m.skyB) * 0.25f; }
            }
        }
        // the depth and the surface tags as integer textures
        g.PixelStorei(kUnpackAlignment, 1);
        if (m.texDepth == 0) g.GenTextures(1, &m.texDepth);
        g.BindTexture(kTexture2D, m.texDepth);
        if (m.depthW != hints.depthWidth || m.depthH != hints.depthHeight) {
            g.TexImage2D(kTexture2D, 0, kR16UI, hints.depthWidth, hints.depthHeight, 0, kRedInteger, kUnsignedShort, hints.depth);
            g.TexParameteri(kTexture2D, kMinFilter, kNearest); g.TexParameteri(kTexture2D, kMagFilter, kNearest);
            g.TexParameteri(kTexture2D, kWrapS, kClampToEdge); g.TexParameteri(kTexture2D, kWrapT, kClampToEdge);
            m.depthW = hints.depthWidth; m.depthH = hints.depthHeight;
        }
        else g.TexSubImage2D(kTexture2D, 0, 0, 0, hints.depthWidth, hints.depthHeight, kRedInteger, kUnsignedShort, hints.depth);
        const bool hasTags = hints.tags != nullptr;
        if (hasTags) {
            if (m.texTags == 0) g.GenTextures(1, &m.texTags);
            g.BindTexture(kTexture2D, m.texTags);
            if (m.tagsW != hints.depthWidth || m.tagsH != hints.depthHeight) {
                g.TexImage2D(kTexture2D, 0, kR8UI, hints.depthWidth, hints.depthHeight, 0, kRedInteger, kUnsignedByte, hints.tags);
                g.TexParameteri(kTexture2D, kMinFilter, kNearest); g.TexParameteri(kTexture2D, kMagFilter, kNearest);
                g.TexParameteri(kTexture2D, kWrapS, kClampToEdge); g.TexParameteri(kTexture2D, kWrapT, kClampToEdge);
                m.tagsW = hints.depthWidth; m.tagsH = hints.depthHeight;
            }
            else g.TexSubImage2D(kTexture2D, 0, 0, 0, hints.depthWidth, hints.depthHeight, kRedInteger, kUnsignedByte, hints.tags);
        }
        m.Bind(0, m.texSrc); m.Bind(1, m.texMask); m.Bind(2, m.texDepth); m.Bind(3, hasTags ? m.texTags : m.texDepth);
        g.UseProgram(m.progScene);
        m.Set1i(m.progScene, "uSrc", 0); m.Set1i(m.progScene, "uMask", 1); m.Set1i(m.progScene, "uDepth", 2); m.Set1i(m.progScene, "uTags", 3);
        m.Set1i(m.progScene, "uHasMask", hasMask ? 1 : 0); m.Set1i(m.progScene, "uHasTags", hasTags ? 1 : 0); m.Set1i(m.progScene, "uFlags", flags);
        m.Set1i(m.progScene, "uBlock", HoverNetGfx::Presenter::BlockShift(width) == 3 ? 8 : 4);
        m.Set2i(m.progScene, "uSize", width, height);
        g.Uniform4i(g.GetUniformLocation(m.progScene, "uWin"), hints.depthX0, hints.depthY0, hints.depthWidth, hints.depthHeight);
        g.Uniform4f(g.GetUniformLocation(m.progScene, "uProj"), hints.projKx, hints.projKz, static_cast<float>(hints.projCenterX), static_cast<float>(hints.projCenterY));
        g.Uniform3f(g.GetUniformLocation(m.progScene, "uSky"), m.skyKnown ? m.skyR : 150.0f, m.skyKnown ? m.skyG : 180.0f, m.skyKnown ? m.skyB : 220.0f);
        g.Uniform4f(g.GetUniformLocation(m.progScene, "uCraft"), static_cast<float>(hints.craftX), static_cast<float>(hints.craftY), static_cast<float>(hints.craftRadius), static_cast<float>(hints.craftGroundY));
        const int lightCount = std::min(hints.lightCount, static_cast<int>(kMaxLights));
        float la[kMaxLights * 4] = {0}, lb[kMaxLights * 4] = {0}, lc[kMaxLights * 4] = {0};
        GLint lk[kMaxLights] = {0};
        for (int i = 0; i < lightCount; ++i) {
            const Light& l = hints.lights[i];
            la[i * 4] = l.x; la[i * 4 + 1] = l.y; la[i * 4 + 2] = l.z; la[i * 4 + 3] = l.radius;
            lb[i * 4] = l.dirx; lb[i * 4 + 1] = l.diry; lb[i * 4 + 2] = l.dirz; lb[i * 4 + 3] = l.coneCos;
            lc[i * 4] = l.r / 255.0f; lc[i * 4 + 1] = l.g / 255.0f; lc[i * 4 + 2] = l.b / 255.0f; lc[i * 4 + 3] = l.strength;
            lk[i] = l.kind;
        }
        m.Set1i(m.progScene, "uLightCount", lightCount);
        g.Uniform4fv(g.GetUniformLocation(m.progScene, "uLA"), kMaxLights, la);
        g.Uniform4fv(g.GetUniformLocation(m.progScene, "uLB"), kMaxLights, lb);
        g.Uniform4fv(g.GetUniformLocation(m.progScene, "uLC"), kMaxLights, lc);
        g.Uniform1iv(g.GetUniformLocation(m.progScene, "uLK"), kMaxLights, lk);
        m.Draw(m.progScene, m.fboScene, width, height);
        pictureTexture = m.texScene;
    }

    // 1: de-band, smooth, sharpen
    m.Bind(0, pictureTexture); m.Bind(1, m.texMask);
    g.UseProgram(m.progMain);
    m.Set1i(m.progMain, "uSrc", 0); m.Set1i(m.progMain, "uMask", 1); m.Set1i(m.progMain, "uHasMask", hasMask ? 1 : 0);
    m.Set1i(m.progMain, "uDeband", deband ? 1 : 0); m.Set1i(m.progMain, "uSmooth", smooth ? 1 : 0); m.Set1f(m.progMain, "uSharpen", sharpen ? 0.35f : 0.0f);
    m.Set2i(m.progMain, "uSize", width, height);
    m.Draw(m.progMain, m.fboMain, width, height);

    // 2: bloom at half and quarter size, blurred
    const int w1 = std::max(1, width / 2), h1 = std::max(1, height / 2), w2 = std::max(1, width / 4), h2 = std::max(1, height / 4);
    if (bloom) {
        g.UseProgram(m.progDown);
        m.Set1i(m.progDown, "uTex", 0);
        m.Bind(0, m.texMain);
        m.Set2f(m.progDown, "uTexel", 1.0f / width, 1.0f / height); m.Set1i(m.progDown, "uBright", 1); m.Set1f(m.progDown, "uThreshold", 0.70f);
        m.Draw(m.progDown, m.fboA1, w1, h1);
        g.UseProgram(m.progBlur);
        m.Set1i(m.progBlur, "uTex", 0);
        m.Bind(0, m.texA1); m.Set2f(m.progBlur, "uDir", 1.0f / w1, 0.0f); m.Draw(m.progBlur, m.fboB1, w1, h1);
        m.Bind(0, m.texB1); m.Set2f(m.progBlur, "uDir", 0.0f, 1.0f / h1); m.Draw(m.progBlur, m.fboA1, w1, h1);
        g.UseProgram(m.progDown);
        m.Bind(0, m.texA1); m.Set2f(m.progDown, "uTexel", 1.0f / w1, 1.0f / h1); m.Set1i(m.progDown, "uBright", 0);
        m.Draw(m.progDown, m.fboA2, w2, h2);
        g.UseProgram(m.progBlur);
        m.Bind(0, m.texA2); m.Set2f(m.progBlur, "uDir", 1.0f / w2, 0.0f); m.Draw(m.progBlur, m.fboB2, w2, h2);
        m.Bind(0, m.texB2); m.Set2f(m.progBlur, "uDir", 0.0f, 1.0f / h2); m.Draw(m.progBlur, m.fboA2, w2, h2);
    }

    // 3: put it together
    g.UseProgram(m.progFinal);
    m.Bind(0, m.texMain); m.Bind(1, m.texA1); m.Bind(2, m.texA2); m.Bind(3, m.texSrc); m.Bind(4, m.texMask);
    m.Set1i(m.progFinal, "uMain", 0); m.Set1i(m.progFinal, "uBloomA", 1); m.Set1i(m.progFinal, "uBloomB", 2); m.Set1i(m.progFinal, "uSrc", 3); m.Set1i(m.progFinal, "uMask", 4);
    m.Set1i(m.progFinal, "uHasMask", hasMask ? 1 : 0); m.Set1i(m.progFinal, "uExposure", exposure ? 1 : 0); m.Set1i(m.progFinal, "uOutline", outline ? 1 : 0);
    m.Set1f(m.progFinal, "uBloom", bloom ? 0.55f : 0.0f); m.Set1f(m.progFinal, "uVignette", vignette ? 0.25f : 0.0f); m.Set2i(m.progFinal, "uSize", width, height);
    m.Draw(m.progFinal, m.fboOut, width, height);

    // The picture is in the renderer's own texture (no read-back), or it comes back to the processor.
    if (m.sharedTexture != 0 && m.sharedWidth == width && m.sharedHeight == height) {
        g.Finish();   // the renderer's context uses it next: it must be complete
        m.lastShared = true;
        if (previousWindow != nullptr) SDL_GL_MakeCurrent(previousWindow, previousContext);
        mLastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return true;
    }
    m.lastShared = false;
    m.readback.resize(static_cast<std::size_t>(width) * height * 4);
    g.PixelStorei(kPackAlignment, 1);
    g.BindFramebuffer(kFramebuffer, m.fboOut);
    g.ReadPixels(0, 0, width, height, kRGBA, kUnsignedByte, m.readback.data());
    const unsigned char* in = m.readback.data();
    for (std::size_t i = 0, n = static_cast<std::size_t>(width) * height; i < n; ++i) { rgb[i * 3] = in[i * 4]; rgb[i * 3 + 1] = in[i * 4 + 1]; rgb[i * 3 + 2] = in[i * 4 + 2]; }

    if (previousWindow != nullptr) SDL_GL_MakeCurrent(previousWindow, previousContext);
    mLastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return true;
}
}
