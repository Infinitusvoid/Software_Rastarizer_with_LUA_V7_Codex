#include "Engine.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>


// -----------------------------
// OpenGL / GLFW / GLEW
// -----------------------------
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// -----------------------------
// stb_image & stb_image_write
// (Implementations are compiled in stb_impl.cpp to avoid PCH/unity include-order issues)
// -----------------------------
#include "../External_libs/stb/image/stb_image.h"
#include "../External_libs/stb/image/stb_image_write.h"


namespace fs = std::filesystem;

namespace
{
    // -------------------------
    // helpers
    // -------------------------
    static inline float clampf(float v, float a, float b) { return std::max(a, std::min(b, v)); }
    static inline int   clampi(int v, int a, int b) { return std::max(a, std::min(b, v)); }

    static inline uint8_t to_u8(float v01)
    {
        int v = (int)std::lround(clampf(v01, 0.0f, 1.0f) * 255.0f);
        return (uint8_t)clampi(v, 0, 255);
    }

    static inline float srgb_to_lin(float c) { return c; } // keep simple; tone controls handle look
    static inline float lin_to_srgb(float c) { return c; } // (you can upgrade later)

    // CPU framebuffer is top-left origin: y=0 at top.
    static inline size_t idx_rgba(int w, int x, int y) { return (size_t)((y * w + x) * 4); }

    // -------------------------
    // Minimal Vec/Mat operations
    // -------------------------
    static inline Engine_::Vec3 v3_add(const Engine_::Vec3& a, const Engine_::Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    static inline Engine_::Vec3 v3_sub(const Engine_::Vec3& a, const Engine_::Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    static inline Engine_::Vec3 v3_mul(const Engine_::Vec3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
    static inline float v3_dot(const Engine_::Vec3& a, const Engine_::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static inline Engine_::Vec3 v3_cross(const Engine_::Vec3& a, const Engine_::Vec3& b)
    {
        return { a.y * b.z - a.z * b.y,
                 a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x };
    }
    static inline float v3_len(const Engine_::Vec3& a) { return std::sqrt(v3_dot(a, a)); }
    static inline Engine_::Vec3 v3_norm(const Engine_::Vec3& a)
    {
        float L = v3_len(a);
        if (L <= 1e-8f) return { 0,0,0 };
        return v3_mul(a, 1.0f / L);
    }

    // -------------------------
    // GL presenter
    // -------------------------
    static bool compile_shader(GLuint shader, const char* src, const char* label)
    {
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLint len = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
            std::string log(len, '\0');
            glGetShaderInfoLog(shader, len, &len, log.data());
            std::cerr << "[Engine] Shader compile failed (" << label << "):\n" << log << "\n";
            return false;
        }
        return true;
    }

    static bool link_program(GLuint prog)
    {
        glLinkProgram(prog);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            GLint len = 0;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
            std::string log(len, '\0');
            glGetProgramInfoLog(prog, len, &len, log.data());
            std::cerr << "[Engine] Program link failed:\n" << log << "\n";
            return false;
        }
        return true;
    }

    // -------------------------
    // Postprocess buffers (bloom)
    // -------------------------
    struct BloomBuffers
    {
        int w = 0, h = 0;
        std::vector<float> a; // RGB float
        std::vector<float> b; // RGB float
        std::vector<float> tmp; // optional working
        std::vector<float> kernel;
        int radius = 0;

        void reset() { w = 0; h = 0; a.clear(); b.clear(); tmp.clear(); kernel.clear(); radius = 0; }
    };

    // -------------------------
    // Engine state
    // -------------------------
    struct State
    {
        Engine_::Config cfg{};
        bool initialized = false;

        // window / GL
        GLFWwindow* window = nullptr;
        bool gl_ready = false;
        bool can_present = false;

        int display_w = 0;
        int display_h = 0;

        GLuint tex = 0;
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint program = 0;
        int max_tex_size = 0;

        // CPU framebuffer
        int fb_w = 0, fb_h = 0;
        std::vector<uint8_t> color; // RGBA
        bool depth_on = false;
        std::vector<float> depth;   // if enabled

        // clip rect
        bool clip_on = false;
        int clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;

        // blend
        Engine_::BlendMode blend = Engine_::BlendMode::Overwrite;

        // dirty rect for fast upload (optional)
        bool dirty_on = true;
        bool dirty_empty = true;
        int dirty_minx = 0, dirty_miny = 0, dirty_maxx = 0, dirty_maxy = 0;

        // postprocess
        Engine_::PostProcessSettings post{};

        // output buffer (postprocess result)
        std::vector<uint8_t> post_out; // RGBA (only allocated if needed)
        BloomBuffers bloom;

        // capture
        fs::path capture_dir;
        fs::path capture_hint_png;
        uint64_t frame_idx = 0;

        // timing
        double last_time = 0.0;
        double dt = 0.0;
        bool want_close = false;

        // input
        static constexpr int KEY_MAX = 512;
        static constexpr int MOUSE_BUTTON_MAX = 16;
        bool key_down[KEY_MAX]{};
        bool key_pressed[KEY_MAX]{};
        bool key_released[KEY_MAX]{};

        double mouse_x = 0.0;
        double mouse_y = 0.0;
        double mouse_prev_x = 0.0;
        double mouse_prev_y = 0.0;
        double mouse_dx = 0.0;
        double mouse_dy = 0.0;
        bool mouse_moved = false;

        bool mouse_down[MOUSE_BUTTON_MAX]{};
        bool mouse_pressed[MOUSE_BUTTON_MAX]{};
        bool mouse_released[MOUSE_BUTTON_MAX]{};

        double mouse_scroll_x = 0.0;
        double mouse_scroll_y = 0.0;
        bool mouse_scrolled = false;

        bool mouse_in_window = false;
        bool mouse_entered = false;
        bool mouse_left = false;

        bool cursor_visible = true;
        bool cursor_captured = false;

        // headless timer fallback
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

        // present filter
        bool present_linear = false;
    };

    static State g;

    // -------------------------
    // Input callback
    // -------------------------
    static void glfw_key_cb(GLFWwindow*, int key, int, int action, int)
    {
        if (key < 0 || key >= State::KEY_MAX) return;

        if (action == GLFW_PRESS)
        {
            if (!g.key_down[key]) g.key_pressed[key] = true;
            g.key_down[key] = true;
        }
        else if (action == GLFW_RELEASE)
        {
            g.key_down[key] = false;
            g.key_released[key] = true;
        }
    }

    static void glfw_window_size_cb(GLFWwindow*, int w, int h)
    {
        g.display_w = std::max(1, w);
        g.display_h = std::max(1, h);
    }

    static void glfw_cursor_pos_cb(GLFWwindow*, double x, double y)
    {
        const double prev_x = g.mouse_x;
        const double prev_y = g.mouse_y;

        g.mouse_prev_x = prev_x;
        g.mouse_prev_y = prev_y;
        g.mouse_x = x;
        g.mouse_y = y;

        const double dx = x - prev_x;
        const double dy = y - prev_y;
        g.mouse_dx += dx;
        g.mouse_dy += dy;
        if (dx != 0.0 || dy != 0.0) g.mouse_moved = true;
    }

    static void glfw_mouse_button_cb(GLFWwindow*, int button, int action, int)
    {
        if (button < 0 || button >= State::MOUSE_BUTTON_MAX) return;

        if (action == GLFW_PRESS)
        {
            if (!g.mouse_down[button]) g.mouse_pressed[button] = true;
            g.mouse_down[button] = true;
        }
        else if (action == GLFW_RELEASE)
        {
            g.mouse_down[button] = false;
            g.mouse_released[button] = true;
        }
    }

    static void glfw_scroll_cb(GLFWwindow*, double xoffset, double yoffset)
    {
        g.mouse_scroll_x += xoffset;
        g.mouse_scroll_y += yoffset;
        if (xoffset != 0.0 || yoffset != 0.0) g.mouse_scrolled = true;
    }

    static void glfw_cursor_enter_cb(GLFWwindow*, int entered)
    {
        const bool is_in = (entered == GLFW_TRUE);
        if (is_in)
        {
            g.mouse_in_window = true;
            g.mouse_entered = true;
        }
        else
        {
            g.mouse_in_window = false;
            g.mouse_left = true;
        }
    }

    // -------------------------
    // Dirty rect helpers
    // -------------------------
    static inline void dirty_add(int x, int y)
    {
        if (!g.dirty_on) return;
        if (x < 0 || y < 0 || x >= g.fb_w || y >= g.fb_h) return;

        if (g.dirty_empty)
        {
            g.dirty_minx = g.dirty_maxx = x;
            g.dirty_miny = g.dirty_maxy = y;
            g.dirty_empty = false;
        }
        else
        {
            g.dirty_minx = std::min(g.dirty_minx, x);
            g.dirty_miny = std::min(g.dirty_miny, y);
            g.dirty_maxx = std::max(g.dirty_maxx, x);
            g.dirty_maxy = std::max(g.dirty_maxy, y);
        }
    }

    static inline void dirty_add_rect(int x, int y, int w, int h)
    {
        if (!g.dirty_on) return;
        dirty_add(x, y);
        dirty_add(x + w - 1, y);
        dirty_add(x, y + h - 1);
        dirty_add(x + w - 1, y + h - 1);
    }

    // -------------------------
    // Clip test
    // -------------------------
    static inline bool in_clip(int x, int y)
    {
        if (!g.clip_on) return true;
        return (x >= g.clip_x && y >= g.clip_y &&
            x < (g.clip_x + g.clip_w) &&
            y < (g.clip_y + g.clip_h));
    }

    // -------------------------
    // Blend write
    // -------------------------
    static inline void write_pixel(int x, int y, const Engine_::Color& src)
    {
        if (x < 0 || y < 0 || x >= g.fb_w || y >= g.fb_h) return;
        if (!in_clip(x, y)) return;

        const size_t i = idx_rgba(g.fb_w, x, y);

        Engine_::Color dst;
        dst.r = g.color[i + 0];
        dst.g = g.color[i + 1];
        dst.b = g.color[i + 2];
        dst.a = g.color[i + 3];

        Engine_::Color out = src;

        switch (g.blend)
        {
        case Engine_::BlendMode::Overwrite:
            out = src;
            break;

        case Engine_::BlendMode::Alpha:
        {
            float sa = src.a / 255.0f;
            float da = dst.a / 255.0f;
            float oa = sa + da * (1.0f - sa);

            auto blend_chan = [&](uint8_t sc, uint8_t dc)->uint8_t
                {
                    float s = sc / 255.0f;
                    float d = dc / 255.0f;
                    float o = s * sa + d * (1.0f - sa);
                    return (uint8_t)clampi((int)std::lround(o * 255.0f), 0, 255);
                };

            out.r = blend_chan(src.r, dst.r);
            out.g = blend_chan(src.g, dst.g);
            out.b = blend_chan(src.b, dst.b);
            out.a = (uint8_t)clampi((int)std::lround(oa * 255.0f), 0, 255);
            break;
        }

        case Engine_::BlendMode::Additive:
        {
            out.r = (uint8_t)clampi((int)dst.r + (int)src.r, 0, 255);
            out.g = (uint8_t)clampi((int)dst.g + (int)src.g, 0, 255);
            out.b = (uint8_t)clampi((int)dst.b + (int)src.b, 0, 255);
            out.a = 255;
            break;
        }

        case Engine_::BlendMode::Multiply:
        {
            out.r = (uint8_t)((dst.r * src.r) / 255);
            out.g = (uint8_t)((dst.g * src.g) / 255);
            out.b = (uint8_t)((dst.b * src.b) / 255);
            out.a = 255;
            break;
        }
        }

        g.color[i + 0] = out.r;
        g.color[i + 1] = out.g;
        g.color[i + 2] = out.b;
        g.color[i + 3] = out.a;

        dirty_add(x, y);
    }

    // -------------------------
    // Depth test
    // z in [0..1], smaller = closer
    // -------------------------
    static inline bool depth_test_write(int x, int y, float z01)
    {
        if (!g.depth_on) return true;

        // Bounds
        if (x < 0 || y < 0 || x >= g.fb_w || y >= g.fb_h) return false;

        // IMPORTANT: keep depth consistent with the same clip rules as color writes
        if (!in_clip(x, y)) return false;

        // Reject NaN (NaN comparisons always false, so without this you can get stuck depth)
        if (!(z01 == z01)) return false;

        const size_t idx = (size_t)(y * g.fb_w + x);
        if (z01 < g.depth[idx])
        {
            g.depth[idx] = z01;
            return true;
        }
        return false;
    }


    // -------------------------
    // Line drawing (thick)
    // -------------------------
    static void draw_line_bres(int x0, int y0, int x1, int y1, const Engine_::Color& c, int thickness)
    {
        if (thickness < 1) thickness = 1;
        int dx = std::abs(x1 - x0);
        int sx = (x0 < x1) ? 1 : -1;
        int dy = -std::abs(y1 - y0);
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx + dy;

        int rad = thickness / 2;

        for (;;)
        {
            for (int oy = -rad; oy <= rad; ++oy)
                for (int ox = -rad; ox <= rad; ++ox)
                    write_pixel(x0 + ox, y0 + oy, c);

            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    // -------------------------
    // Circle helpers
    // -------------------------
    static void draw_circle_outline(int cx, int cy, int r, const Engine_::Color& c, int thickness)
    {
        if (r <= 0) return;
        if (thickness < 1) thickness = 1;
        int t = thickness;

        // simple "ring" by drawing multiple radii
        for (int rr = r; rr > std::max(0, r - t); --rr)
        {
            int x = rr;
            int y = 0;
            int err = 0;

            while (x >= y)
            {
                write_pixel(cx + x, cy + y, c);
                write_pixel(cx + y, cy + x, c);
                write_pixel(cx - y, cy + x, c);
                write_pixel(cx - x, cy + y, c);
                write_pixel(cx - x, cy - y, c);
                write_pixel(cx - y, cy - x, c);
                write_pixel(cx + y, cy - x, c);
                write_pixel(cx + x, cy - y, c);

                if (err <= 0) { y++; err += 2 * y + 1; }
                if (err > 0) { x--; err -= 2 * x + 1; }
            }
        }
    }

    static void draw_circle_filled(int cx, int cy, int r, const Engine_::Color& c)
    {
        if (r <= 0) return;
        for (int y = -r; y <= r; ++y)
        {
            int hh = (int)std::floor(std::sqrt((double)r * r - (double)y * y));
            int y0 = cy + y;
            int x0 = cx - hh;
            int x1 = cx + hh;
            for (int x = x0; x <= x1; ++x)
                write_pixel(x, y0, c);
        }
    }

    // -------------------------
    // Triangle rasterization (2D)
    // Uses top-left origin coordinates.
    // -------------------------
    static inline float edge_fn(const Engine_::Vec2& a, const Engine_::Vec2& b, const Engine_::Vec2& p)
    {
        return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
    }

    static void tri_bounds(const Engine_::Vec2& a, const Engine_::Vec2& b, const Engine_::Vec2& c,
        int& minx, int& miny, int& maxx, int& maxy)
    {
        float fx0 = std::min({ a.x, b.x, c.x });
        float fy0 = std::min({ a.y, b.y, c.y });
        float fx1 = std::max({ a.x, b.x, c.x });
        float fy1 = std::max({ a.y, b.y, c.y });

        minx = (int)std::floor(fx0);
        miny = (int)std::floor(fy0);
        maxx = (int)std::ceil(fx1);
        maxy = (int)std::ceil(fy1);

        minx = clampi(minx, 0, g.fb_w - 1);
        miny = clampi(miny, 0, g.fb_h - 1);
        maxx = clampi(maxx, 0, g.fb_w - 1);
        maxy = clampi(maxy, 0, g.fb_h - 1);
    }

    static void draw_tri_flat(const Engine_::Vec2& a, const Engine_::Vec2& b, const Engine_::Vec2& c, Engine_::Color col)
    {
        int minx, miny, maxx, maxy;
        tri_bounds(a, b, c, minx, miny, maxx, maxy);

        Engine_::Vec2 p;
        float area = edge_fn(a, b, c);
        if (std::abs(area) < 1e-8f) return;

        // Make consistent orientation
        bool flip = (area < 0);
        Engine_::Vec2 A = a, B = b, C = c;
        if (flip) { B = c; C = b; area = -area; }

        for (int y = miny; y <= maxy; ++y)
        {
            for (int x = minx; x <= maxx; ++x)
            {
                p = { (float)x + 0.5f, (float)y + 0.5f };
                float w0 = edge_fn(B, C, p);
                float w1 = edge_fn(C, A, p);
                float w2 = edge_fn(A, B, p);
                if (w0 >= 0 && w1 >= 0 && w2 >= 0)
                {
                    write_pixel(x, y, col);
                }
            }
        }

        dirty_add_rect(minx, miny, (maxx - minx + 1), (maxy - miny + 1));
    }

    static void draw_tri_grad(const Engine_::Vec2& a, Engine_::Color ca,
        const Engine_::Vec2& b, Engine_::Color cb,
        const Engine_::Vec2& c, Engine_::Color cc)
    {
        // Work on local copies so we can enforce consistent winding.
        Engine_::Vec2 A = a, B = b, C = c;
        Engine_::Color CA = ca, CB = cb, CC = cc;

        float area = edge_fn(A, B, C);
        if (std::abs(area) < 1e-8f) return;

        // Make triangle CCW (area > 0) so edge tests are consistent.
        if (area < 0.0f)
        {
            std::swap(B, C);
            std::swap(CB, CC);
            area = -area;
        }

        int minx, miny, maxx, maxy;
        tri_bounds(A, B, C, minx, miny, maxx, maxy);

        const float invA = 1.0f / area;

        for (int y = miny; y <= maxy; ++y)
        {
            for (int x = minx; x <= maxx; ++x)
            {
                Engine_::Vec2 p{ (float)x + 0.5f, (float)y + 0.5f };

                // With this edge ordering:
                // l0 corresponds to A, l1 to B, l2 to C
                float w0 = edge_fn(B, C, p);
                float w1 = edge_fn(C, A, p);
                float w2 = edge_fn(A, B, p);

                if (w0 >= 0 && w1 >= 0 && w2 >= 0)
                {
                    float l0 = w0 * invA;
                    float l1 = w1 * invA;
                    float l2 = w2 * invA;

                    float r = (CA.r * l0 + CB.r * l1 + CC.r * l2) / 255.0f;
                    float gch = (CA.g * l0 + CB.g * l1 + CC.g * l2) / 255.0f;
                    float bch = (CA.b * l0 + CB.b * l1 + CC.b * l2) / 255.0f;

                    Engine_::Color out;
                    out.r = to_u8(r);
                    out.g = to_u8(gch);
                    out.b = to_u8(bch);
                    out.a = 255;
                    write_pixel(x, y, out);
                }
            }
        }

        dirty_add_rect(minx, miny, (maxx - minx + 1), (maxy - miny + 1));
    }


    static inline Engine_::Color sample_tex_nearest(const Engine_::Image& tex, float u, float v, Engine_::Color tint)
    {
        u = clampf(u, 0.0f, 1.0f);
        v = clampf(v, 0.0f, 1.0f);
        int x = (int)std::floor(u * (tex.w - 1) + 0.5f);
        int y = (int)std::floor(v * (tex.h - 1) + 0.5f);
        x = clampi(x, 0, tex.w - 1);
        y = clampi(y, 0, tex.h - 1);
        size_t i = idx_rgba(tex.w, x, y);

        Engine_::Color c;
        c.r = (uint8_t)((tex.rgba[i + 0] * tint.r) / 255);
        c.g = (uint8_t)((tex.rgba[i + 1] * tint.g) / 255);
        c.b = (uint8_t)((tex.rgba[i + 2] * tint.b) / 255);
        c.a = (uint8_t)((tex.rgba[i + 3] * tint.a) / 255);
        return c;
    }

    static void draw_tri_tex(const Engine_::Vec2& a, const Engine_::Vec2& ua,
        const Engine_::Vec2& b, const Engine_::Vec2& ub,
        const Engine_::Vec2& c, const Engine_::Vec2& uc,
        const Engine_::Image& tex, Engine_::Color tint)
    {
        if (!tex.valid()) return;

        Engine_::Vec2 A = a, B = b, C = c;
        Engine_::Vec2 UA = ua, UB = ub, UC = uc;

        float area = edge_fn(A, B, C);
        if (std::abs(area) < 1e-8f) return;

        // Force CCW winding
        if (area < 0.0f)
        {
            std::swap(B, C);
            std::swap(UB, UC);
            area = -area;
        }

        int minx, miny, maxx, maxy;
        tri_bounds(A, B, C, minx, miny, maxx, maxy);

        const float invA = 1.0f / area;

        for (int y = miny; y <= maxy; ++y)
        {
            for (int x = minx; x <= maxx; ++x)
            {
                Engine_::Vec2 p{ (float)x + 0.5f, (float)y + 0.5f };

                float w0 = edge_fn(B, C, p);
                float w1 = edge_fn(C, A, p);
                float w2 = edge_fn(A, B, p);

                if (w0 >= 0 && w1 >= 0 && w2 >= 0)
                {
                    float l0 = w0 * invA; // A
                    float l1 = w1 * invA; // B
                    float l2 = w2 * invA; // C

                    float u = UA.x * l0 + UB.x * l1 + UC.x * l2;
                    float v = UA.y * l0 + UB.y * l1 + UC.y * l2;

                    Engine_::Color sc = sample_tex_nearest(tex, u, v, tint);
                    write_pixel(x, y, sc);
                }
            }
        }

        dirty_add_rect(minx, miny, (maxx - minx + 1), (maxy - miny + 1));
    }


    // -------------------------
    // 3D pipeline helpers
    // -------------------------
    struct VOut
    {
        float x = 0, y = 0;   // screen
        float z = 1;        // depth 0..1
        float invw = 1;     // 1/clip.w for perspective-correct
        Engine_::Vec3 col; // 0..1
        Engine_::Vec2 uv;  // 0..1
    };

    static bool project_vertex(const Engine_::Vertex3D& vin, const Engine_::Mat4& mvp, VOut& out)
    {
        Engine_::Vec4 p = { vin.pos.x, vin.pos.y, vin.pos.z, 1.0f };
        Engine_::Vec4 clip = Engine_::mat4_mul(mvp, p);

        if (std::abs(clip.w) < 1e-8f) return false;

        float invw = 1.0f / clip.w;
        float ndc_x = clip.x * invw;
        float ndc_y = clip.y * invw;
        float ndc_z = clip.z * invw; // -1..1 typically

        // basic clip (very light)
        if (ndc_z < -1.2f || ndc_z > 1.2f) return false;

        // NDC -> framebuffer pixels (top-left origin)
        out.x = (ndc_x * 0.5f + 0.5f) * (float)(g.fb_w);
        out.y = (1.0f - (ndc_y * 0.5f + 0.5f)) * (float)(g.fb_h);
        out.z = (ndc_z * 0.5f + 0.5f); // map to 0..1
        out.invw = invw;

        out.col = vin.color;
        out.uv = vin.uv;
        return true;
    }

    static void draw_tri_3d(const VOut& a, const VOut& b, const VOut& c,
        const Engine_::Image* tex, bool depth_test)
    {
        Engine_::Vec2 AA{ a.x, a.y }, BB{ b.x, b.y }, CC{ c.x, c.y };

        int minx, miny, maxx, maxy;
        tri_bounds(AA, BB, CC, minx, miny, maxx, maxy);

        float area = edge_fn(AA, BB, CC);
        if (std::abs(area) < 1e-8f) return;

        // Make CCW so w0/w1/w2 tests are consistent
        VOut A0 = a, B0 = b, C0 = c;
        if (area < 0.0f)
        {
            std::swap(B0, C0);
            std::swap(BB, CC);
            area = -area;
        }

        const float invA = 1.0f / area;

        for (int y = miny; y <= maxy; ++y)
        {
            for (int x = minx; x <= maxx; ++x)
            {
                Engine_::Vec2 p{ (float)x + 0.5f, (float)y + 0.5f };

                // These correspond to weights for AA, BB, CC respectively:
                float wA = edge_fn(BB, CC, p);
                float wB = edge_fn(CC, AA, p);
                float wC = edge_fn(AA, BB, p);

                if (wA >= 0 && wB >= 0 && wC >= 0)
                {
                    float lA = wA * invA; // AA -> A0
                    float lB = wB * invA; // BB -> B0
                    float lC = wC * invA; // CC -> C0

                    // Perspective-correct
                    float iw = (A0.invw * lA) + (B0.invw * lB) + (C0.invw * lC);
                    if (iw <= 1e-12f) continue;
                    float w = 1.0f / iw;

                    float z = (A0.z * A0.invw * lA +
                        B0.z * B0.invw * lB +
                        C0.z * C0.invw * lC) * w;

                    // Optional safety clamp (helps if near-plane no clipping produces weird z)
                    // z = clampf(z, 0.0f, 1.0f);

                    if (depth_test && g.depth_on)
                    {
                        if (!depth_test_write(x, y, z))
                            continue;
                    }

                    Engine_::Vec3 col{
                        (A0.col.x * A0.invw * lA + B0.col.x * B0.invw * lB + C0.col.x * C0.invw * lC) * w,
                        (A0.col.y * A0.invw * lA + B0.col.y * B0.invw * lB + C0.col.y * C0.invw * lC) * w,
                        (A0.col.z * A0.invw * lA + B0.col.z * B0.invw * lB + C0.col.z * C0.invw * lC) * w
                    };

                    Engine_::Vec2 uv{
                        (A0.uv.x * A0.invw * lA + B0.uv.x * B0.invw * lB + C0.uv.x * C0.invw * lC) * w,
                        (A0.uv.y * A0.invw * lA + B0.uv.y * B0.invw * lB + C0.uv.y * C0.invw * lC) * w
                    };

                    Engine_::Color out;
                    if (tex && tex->valid())
                    {
                        Engine_::Color tint;
                        tint.r = to_u8(col.x);
                        tint.g = to_u8(col.y);
                        tint.b = to_u8(col.z);
                        tint.a = 255;
                        out = sample_tex_nearest(*tex, uv.x, uv.y, tint);
                    }
                    else
                    {
                        out.r = to_u8(col.x);
                        out.g = to_u8(col.y);
                        out.b = to_u8(col.z);
                        out.a = 255;
                    }

                    write_pixel(x, y, out);
                }
            }
        }

        dirty_add_rect(minx, miny, (maxx - minx + 1), (maxy - miny + 1));
    }


    // -------------------------
    // Bloom: build gaussian kernel
    // -------------------------
    static void build_gaussian_kernel(float sigma, std::vector<float>& kernel, int& radius)
    {
        sigma = std::max(0.1f, sigma);
        radius = (int)std::ceil(3.0f * sigma);
        radius = clampi(radius, 1, 32);

        kernel.resize((size_t)(radius * 2 + 1));
        float sum = 0.0f;
        float inv2s2 = 1.0f / (2.0f * sigma * sigma);

        for (int i = -radius; i <= radius; ++i)
        {
            float w = std::exp(-(float)(i * i) * inv2s2);
            kernel[(size_t)(i + radius)] = w;
            sum += w;
        }
        for (float& w : kernel) w /= sum;
    }

    static void bloom_ensure_buffers(int w, int h)
    {
        if (g.bloom.w == w && g.bloom.h == h && !g.bloom.a.empty() && !g.bloom.b.empty())
            return;

        g.bloom.w = w;
        g.bloom.h = h;
        g.bloom.a.assign((size_t)w * h * 3, 0.0f);
        g.bloom.b.assign((size_t)w * h * 3, 0.0f);
    }

    static void bloom_brightpass_downsample(const Engine_::BloomSettings& bs)
    {
        int ds = std::max(2, bs.downsample);
        int bw = (g.fb_w + ds - 1) / ds;
        int bh = (g.fb_h + ds - 1) / ds;

        bloom_ensure_buffers(bw, bh);

        const float thr = clampf(bs.threshold, 0.0f, 1.0f);

        // For each bloom pixel, average ds*ds block from framebuffer
        for (int by = 0; by < bh; ++by)
        {
            for (int bx = 0; bx < bw; ++bx)
            {
                float r = 0, gc = 0, b = 0;
                int count = 0;

                int x0 = bx * ds;
                int y0 = by * ds;

                for (int oy = 0; oy < ds; ++oy)
                {
                    int y = y0 + oy;
                    if (y >= g.fb_h) break;
                    for (int ox = 0; ox < ds; ++ox)
                    {
                        int x = x0 + ox;
                        if (x >= g.fb_w) break;
                        size_t i = idx_rgba(g.fb_w, x, y);
                        float fr = g.color[i + 0] / 255.0f;
                        float fg = g.color[i + 1] / 255.0f;
                        float fb = g.color[i + 2] / 255.0f;

                        // luminance
                        float lum = 0.2126f * fr + 0.7152f * fg + 0.0722f * fb;

                        // bright pass
                        float k = (lum - thr);
                        if (k > 0.0f)
                        {
                            k = k / std::max(1e-6f, (1.0f - thr)); // normalize
                            r += fr * k;
                            gc += fg * k;
                            b += fb * k;
                        }

                        count++;
                    }
                }

                if (count > 0)
                {
                    r /= (float)count;
                    gc /= (float)count;
                    b /= (float)count;
                }

                size_t bi = (size_t)((by * bw + bx) * 3);
                g.bloom.a[bi + 0] = r;
                g.bloom.a[bi + 1] = gc;
                g.bloom.a[bi + 2] = b;
            }
        }
    }

    static void bloom_blur_separable(const Engine_::BloomSettings& bs)
    {
        int bw = g.bloom.w;
        int bh = g.bloom.h;
        if (bw <= 0 || bh <= 0) return;

        build_gaussian_kernel(bs.sigma, g.bloom.kernel, g.bloom.radius);
        const int R = g.bloom.radius;
        const auto& K = g.bloom.kernel;

        // Horizontal: a -> b
        for (int y = 0; y < bh; ++y)
        {
            for (int x = 0; x < bw; ++x)
            {
                float rr = 0, gg = 0, bb = 0;
                for (int k = -R; k <= R; ++k)
                {
                    int sx = clampi(x + k, 0, bw - 1);
                    size_t si = (size_t)((y * bw + sx) * 3);
                    float w = K[(size_t)(k + R)];
                    rr += g.bloom.a[si + 0] * w;
                    gg += g.bloom.a[si + 1] * w;
                    bb += g.bloom.a[si + 2] * w;
                }
                size_t di = (size_t)((y * bw + x) * 3);
                g.bloom.b[di + 0] = rr;
                g.bloom.b[di + 1] = gg;
                g.bloom.b[di + 2] = bb;
            }
        }

        // Vertical: b -> a
        for (int y = 0; y < bh; ++y)
        {
            for (int x = 0; x < bw; ++x)
            {
                float rr = 0, gg = 0, bb = 0;
                for (int k = -R; k <= R; ++k)
                {
                    int sy = clampi(y + k, 0, bh - 1);
                    size_t si = (size_t)((sy * bw + x) * 3);
                    float w = K[(size_t)(k + R)];
                    rr += g.bloom.b[si + 0] * w;
                    gg += g.bloom.b[si + 1] * w;
                    bb += g.bloom.b[si + 2] * w;
                }
                size_t di = (size_t)((y * bw + x) * 3);
                g.bloom.a[di + 0] = rr;
                g.bloom.a[di + 1] = gg;
                g.bloom.a[di + 2] = bb;
            }
        }
    }

    static inline void bloom_sample_bilinear(float u, float v, float& r, float& gch, float& b)
    {
        int bw = g.bloom.w;
        int bh = g.bloom.h;
        if (bw <= 0 || bh <= 0) { r = gch = b = 0; return; }

        u = clampf(u, 0.0f, (float)(bw - 1));
        v = clampf(v, 0.0f, (float)(bh - 1));

        int x0 = (int)std::floor(u);
        int y0 = (int)std::floor(v);
        int x1 = clampi(x0 + 1, 0, bw - 1);
        int y1 = clampi(y0 + 1, 0, bh - 1);

        float tx = u - (float)x0;
        float ty = v - (float)y0;

        auto fetch = [&](int x, int y, float& rr, float& gg, float& bb)
            {
                size_t i = (size_t)((y * bw + x) * 3);
                rr = g.bloom.a[i + 0];
                gg = g.bloom.a[i + 1];
                bb = g.bloom.a[i + 2];
            };

        float r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
        fetch(x0, y0, r00, g00, b00);
        fetch(x1, y0, r10, g10, b10);
        fetch(x0, y1, r01, g01, b01);
        fetch(x1, y1, r11, g11, b11);

        float r0 = r00 * (1 - tx) + r10 * tx;
        float g0 = g00 * (1 - tx) + g10 * tx;
        float b0 = b00 * (1 - tx) + b10 * tx;

        float r1 = r01 * (1 - tx) + r11 * tx;
        float g1 = g01 * (1 - tx) + g11 * tx;
        float b1 = b01 * (1 - tx) + b11 * tx;

        r = r0 * (1 - ty) + r1 * ty;
        gch = g0 * (1 - ty) + g1 * ty;
        b = b0 * (1 - ty) + b1 * ty;
    }

    static const uint8_t* build_postprocess_output(bool apply_post)
    {
        if (!apply_post) return g.color.data();

        const bool bloom_on = g.post.bloom.enabled;
        const bool tone_on = g.post.tone.enabled;

        if (!bloom_on && !tone_on)
            return g.color.data();

        // Ensure output buffer
        g.post_out.resize((size_t)g.fb_w * g.fb_h * 4);

        // Bloom pipeline at reduced res
        if (bloom_on)
        {
            bloom_brightpass_downsample(g.post.bloom);
            bloom_blur_separable(g.post.bloom);
        }

        const float exposure = std::max(0.0001f, g.post.tone.exposure);
        const float gamma = std::max(0.1f, g.post.tone.gamma);
        const float invGamma = 1.0f / gamma;

        const int ds = std::max(2, g.post.bloom.downsample);
        const float bloom_intensity = g.post.bloom.intensity;

        for (int y = 0; y < g.fb_h; ++y)
        {
            for (int x = 0; x < g.fb_w; ++x)
            {
                size_t i = idx_rgba(g.fb_w, x, y);

                float r = g.color[i + 0] / 255.0f;
                float gch = g.color[i + 1] / 255.0f;
                float b = g.color[i + 2] / 255.0f;

                // Add bloom (upsample)
                if (bloom_on)
                {
                    // map framebuffer pixel -> bloom pixel coords
                    float bu = ((float)x + 0.5f) / (float)ds - 0.5f;
                    float bv = ((float)y + 0.5f) / (float)ds - 0.5f;

                    float br, bg, bb;
                    bloom_sample_bilinear(bu, bv, br, bg, bb);

                    r += br * bloom_intensity;
                    gch += bg * bloom_intensity;
                    b += bb * bloom_intensity;
                }

                // Tone (simple film-ish exposure + gamma)
                if (tone_on)
                {
                    // exposure curve: 1 - exp(-c*exposure)
                    r = 1.0f - std::exp(-r * exposure);
                    gch = 1.0f - std::exp(-gch * exposure);
                    b = 1.0f - std::exp(-b * exposure);

                    // gamma
                    r = std::pow(clampf(r, 0, 1), invGamma);
                    gch = std::pow(clampf(gch, 0, 1), invGamma);
                    b = std::pow(clampf(b, 0, 1), invGamma);
                }

                g.post_out[i + 0] = to_u8(r);
                g.post_out[i + 1] = to_u8(gch);
                g.post_out[i + 2] = to_u8(b);
                g.post_out[i + 3] = 255;
            }
        }

        // Post touches whole frame
        g.dirty_empty = true;
        return g.post_out.data();
    }

    // -------------------------
    // Capture path
    // -------------------------
    static std::string frame6(uint64_t idx)
    {
        std::ostringstream ss;
        ss << std::setfill('0') << std::setw(6) << idx;
        return ss.str();
    }

    static fs::path resolve_capture_path()
    {
        if (!g.capture_hint_png.empty())
        {
            fs::path p = g.capture_hint_png;
            fs::path dir = p.parent_path();
            if (dir.empty()) dir = fs::current_path();

            const std::string stem = p.stem().string();
            const std::string ext = p.extension().string().empty() ? ".png" : p.extension().string();

            return dir / (stem + "_" + frame6(g.frame_idx) + ext);
        }

        fs::path dir = g.capture_dir.empty() ? fs::path("captures") : g.capture_dir;
        return dir / ("frame_" + frame6(g.frame_idx) + ".png");
    }

    static void ensure_parent_dir(const fs::path& p)
    {
        fs::path dir = p.parent_path();
        if (!dir.empty())
        {
            std::error_code ec;
            fs::create_directories(dir, ec);
        }
    }

    // -------------------------
    // GL presenter creation
    // -------------------------
    static bool create_presenter()
    {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g.max_tex_size);

        if (g.fb_w > g.max_tex_size || g.fb_h > g.max_tex_size)
        {
            std::cerr << "[Engine] Framebuffer " << g.fb_w << "x" << g.fb_h
                << " exceeds GL_MAX_TEXTURE_SIZE=" << g.max_tex_size
                << ". Present disabled (CPU raster + save still works).\n";
            g.can_present = false;
            return true; // not fatal
        }

        // Fullscreen quad (pos+uv), UV flips vertically so CPU top row shows at top
        const float verts[] = {
            //  x,    y,    u,   v
            -1.0f, -1.0f, 0.0f, 1.0f,
             1.0f, -1.0f, 1.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 0.0f,
            -1.0f, -1.0f, 0.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 0.0f
        };

        glGenVertexArrays(1, &g.vao);
        glBindVertexArray(g.vao);

        glGenBuffers(1, &g.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glBindVertexArray(0);

        glGenTextures(1, &g.tex);
        glBindTexture(GL_TEXTURE_2D, g.tex);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g.present_linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g.present_linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g.fb_w, g.fb_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, g.color.data());

        const char* vs = R"GLSL(
            #version 330 core
            layout(location=0) in vec2 aPos;
            layout(location=1) in vec2 aUV;
            out vec2 vUV;
            void main(){
                vUV = aUV;
                gl_Position = vec4(aPos, 0.0, 1.0);
            }
        )GLSL";

        const char* fs = R"GLSL(
            #version 330 core
            in vec2 vUV;
            uniform sampler2D uTex;
            out vec4 FragColor;
            void main(){
                FragColor = texture(uTex, vUV);
            }
        )GLSL";

        GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
        GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
        if (!compile_shader(vsh, vs, "vertex")) return false;
        if (!compile_shader(fsh, fs, "fragment")) return false;

        g.program = glCreateProgram();
        glAttachShader(g.program, vsh);
        glAttachShader(g.program, fsh);
        if (!link_program(g.program)) return false;

        glDeleteShader(vsh);
        glDeleteShader(fsh);

        glUseProgram(g.program);
        GLint loc = glGetUniformLocation(g.program, "uTex");
        glUniform1i(loc, 0);

        g.can_present = true;
        return true;
    }
}

namespace Engine_
{
    // ------------------------------------------------------------
    // Mat4 API
    // ------------------------------------------------------------
    Mat4 mat4_identity()
    {
        Mat4 r{};
        r.m[0] = 1; r.m[5] = 1; r.m[10] = 1; r.m[15] = 1;
        return r;
    }

    Mat4 mat4_mul(const Mat4& a, const Mat4& b)
    {
        Mat4 r{};
        // column-major: r = a*b
        for (int c = 0; c < 4; ++c)
        {
            for (int rrow = 0; rrow < 4; ++rrow)
            {
                r.m[c * 4 + rrow] =
                    a.m[0 * 4 + rrow] * b.m[c * 4 + 0] +
                    a.m[1 * 4 + rrow] * b.m[c * 4 + 1] +
                    a.m[2 * 4 + rrow] * b.m[c * 4 + 2] +
                    a.m[3 * 4 + rrow] * b.m[c * 4 + 3];
            }
        }
        return r;
    }

    Vec4 mat4_mul(const Mat4& a, const Vec4& v)
    {
        Vec4 r{};
        r.x = a.m[0] * v.x + a.m[4] * v.y + a.m[8] * v.z + a.m[12] * v.w;
        r.y = a.m[1] * v.x + a.m[5] * v.y + a.m[9] * v.z + a.m[13] * v.w;
        r.z = a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w;
        r.w = a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w;
        return r;
    }

    Mat4 mat4_translate(const Vec3& t)
    {
        Mat4 r = mat4_identity();
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    Mat4 mat4_scale(const Vec3& s)
    {
        Mat4 r{};
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; r.m[15] = 1.0f;
        return r;
    }

    Mat4 mat4_rotate_x(float radians)
    {
        Mat4 r = mat4_identity();
        float c = std::cos(radians);
        float s = std::sin(radians);
        r.m[5] = c;  r.m[9] = -s;
        r.m[6] = s;  r.m[10] = c;
        return r;
    }

    Mat4 mat4_rotate_z(float radians)
    {
        Mat4 r = mat4_identity();
        float c = std::cos(radians);
        float s = std::sin(radians);
        r.m[0] = c;  r.m[4] = -s;
        r.m[1] = s;  r.m[5] = c;
        return r;
    }

    Mat4 mat4_rotate_y(float radians)
    {
        Mat4 r = mat4_identity();
        float c = std::cos(radians);
        float s = std::sin(radians);
        r.m[0] = c;  r.m[8] = s;
        r.m[2] = -s; r.m[10] = c;
        return r;
    }

    Mat4 mat4_perspective(float fovy_radians, float aspect, float znear, float zfar)
    {
        Mat4 r{};
        float f = 1.0f / std::tan(fovy_radians * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zfar + znear) / (znear - zfar);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
        return r;
    }

    Mat4 mat4_look_at(const Vec3& eye, const Vec3& at, const Vec3& up)
    {
        Vec3 f = v3_norm(v3_sub(at, eye));
        Vec3 s = v3_norm(v3_cross(f, up));
        Vec3 u = v3_cross(s, f);

        Mat4 r = mat4_identity();
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;

        r.m[12] = -v3_dot(s, eye);
        r.m[13] = -v3_dot(u, eye);
        r.m[14] = v3_dot(f, eye);
        return r;
    }

    // ------------------------------------------------------------
    // Image API
    // ------------------------------------------------------------
    Image load_image_rgba(const std::string& path)
    {
        Image img;
        int w = 0, h = 0, n = 0;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!data)
        {
            std::cerr << "[Engine] stbi_load failed: " << path << "\n";
            return img;
        }

        img.w = w;
        img.h = h;
        img.rgba.assign((size_t)w * h * 4, 0);
        std::memcpy(img.rgba.data(), data, (size_t)w * h * 4);

        stbi_image_free(data);
        return img;
    }

    Image make_checker_rgba(int w, int h, int cell)
    {
        Image img;
        img.w = w;
        img.h = h;
        img.rgba.resize((size_t)w * h * 4);
        cell = std::max(1, cell);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                bool on = ((x / cell) ^ (y / cell)) & 1;
                uint8_t c = on ? 230 : 40;
                size_t i = idx_rgba(w, x, y);
                img.rgba[i + 0] = c;
                img.rgba[i + 1] = c;
                img.rgba[i + 2] = c;
                img.rgba[i + 3] = 255;
            }
        }
        return img;
    }

    // ------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------
    bool init(const Config& cfg)
    {
        if (g.initialized) return true;
        g.cfg = cfg;

        g.fb_w = std::max(1, cfg.fb_w);
        g.fb_h = std::max(1, cfg.fb_h);

        g.display_w = std::max(1, cfg.display_w);
        g.display_h = std::max(1, cfg.display_h);

        g.present_linear = cfg.linear_filter;

        g.color.assign((size_t)g.fb_w * g.fb_h * 4, 0);
        for (int y = 0; y < g.fb_h; ++y)
            for (int x = 0; x < g.fb_w; ++x)
                g.color[idx_rgba(g.fb_w, x, y) + 3] = 255;

        g.depth_on = false;
        g.depth.clear();

        g.post = PostProcessSettings{};
        g.dirty_empty = true;

        if (cfg.headless)
        {
            g.gl_ready = false;
            g.can_present = false;
            g.initialized = true;
            g.want_close = false;
            g.last_time = 0.0;
            g.t0 = std::chrono::steady_clock::now();
            return true;
        }

        if (!glfwInit())
        {
            std::cerr << "[Engine] glfwInit failed.\n";
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_RESIZABLE, cfg.resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, cfg.hidden_window ? GLFW_FALSE : GLFW_TRUE);

        g.window = glfwCreateWindow(g.display_w, g.display_h, cfg.title.c_str(), nullptr, nullptr);
        if (!g.window)
        {
            std::cerr << "[Engine] glfwCreateWindow failed.\n";
            glfwTerminate();
            return false;
        }

        glfwMakeContextCurrent(g.window);
        glfwSwapInterval(cfg.vsync ? 1 : 0);

        glfwSetKeyCallback(g.window, glfw_key_cb);
        glfwSetWindowSizeCallback(g.window, glfw_window_size_cb);
        glfwSetCursorPosCallback(g.window, glfw_cursor_pos_cb);
        glfwSetMouseButtonCallback(g.window, glfw_mouse_button_cb);
        glfwSetScrollCallback(g.window, glfw_scroll_cb);
        glfwSetCursorEnterCallback(g.window, glfw_cursor_enter_cb);

        glfwGetCursorPos(g.window, &g.mouse_x, &g.mouse_y);
        g.mouse_prev_x = g.mouse_x;
        g.mouse_prev_y = g.mouse_y;
        g.mouse_in_window = true;

        glewExperimental = GL_TRUE;
        GLenum err = glewInit();
        if (err != GLEW_OK)
        {
            std::cerr << "[Engine] glewInit failed: " << glewGetErrorString(err) << "\n";
            shutdown();
            return false;
        }
        glGetError(); // clear

        if (!create_presenter())
        {
            std::cerr << "[Engine] create_presenter failed.\n";
            shutdown();
            return false;
        }

        g.gl_ready = true;
        g.initialized = true;
        g.want_close = false;
        g.last_time = glfwGetTime();
        return true;
    }

    void shutdown()
    {
        if (g.gl_ready)
        {
            if (g.program) { glDeleteProgram(g.program); g.program = 0; }
            if (g.tex) { glDeleteTextures(1, &g.tex); g.tex = 0; }
            if (g.vbo) { glDeleteBuffers(1, &g.vbo); g.vbo = 0; }
            if (g.vao) { glDeleteVertexArrays(1, &g.vao); g.vao = 0; }
        }

        if (g.window)
        {
            glfwDestroyWindow(g.window);
            g.window = nullptr;
        }

        if (!g.cfg.headless)
            glfwTerminate();

        g.color.clear();
        g.depth.clear();
        g.post_out.clear();
        g.bloom.reset();

        for (int i = 0; i < State::KEY_MAX; ++i)
        {
            g.key_down[i] = false;
            g.key_pressed[i] = false;
            g.key_released[i] = false;
        }
        for (int i = 0; i < State::MOUSE_BUTTON_MAX; ++i)
        {
            g.mouse_down[i] = false;
            g.mouse_pressed[i] = false;
            g.mouse_released[i] = false;
        }
        g.mouse_x = g.mouse_y = 0.0;
        g.mouse_prev_x = g.mouse_prev_y = 0.0;
        g.mouse_dx = g.mouse_dy = 0.0;
        g.mouse_moved = false;
        g.mouse_scroll_x = g.mouse_scroll_y = 0.0;
        g.mouse_scrolled = false;
        g.mouse_in_window = false;
        g.mouse_entered = false;
        g.mouse_left = false;
        g.cursor_visible = true;
        g.cursor_captured = false;

        g.initialized = false;
        g.gl_ready = false;
        g.can_present = false;
    }

    // ------------------------------------------------------------
    // Loop + input
    // ------------------------------------------------------------
    bool should_close()
    {
        if (g.want_close) return true;
        if (g.cfg.headless) return g.want_close;
        if (!g.window) return true;
        return glfwWindowShouldClose(g.window) != 0;
    }

    void request_close()
    {
        g.want_close = true;
        if (g.window) glfwSetWindowShouldClose(g.window, GLFW_TRUE);
    }

    void poll_events()
    {
        // clear key edge flags
        for (int i = 0; i < State::KEY_MAX; ++i)
        {
            g.key_pressed[i] = false;
            g.key_released[i] = false;
        }

        // clear mouse per-frame flags/deltas
        for (int i = 0; i < State::MOUSE_BUTTON_MAX; ++i)
        {
            g.mouse_pressed[i] = false;
            g.mouse_released[i] = false;
        }
        g.mouse_prev_x = g.mouse_x;
        g.mouse_prev_y = g.mouse_y;
        g.mouse_dx = 0.0;
        g.mouse_dy = 0.0;
        g.mouse_moved = false;
        g.mouse_scroll_x = 0.0;
        g.mouse_scroll_y = 0.0;
        g.mouse_scrolled = false;
        g.mouse_entered = false;
        g.mouse_left = false;

        double now = 0.0;
        if (g.cfg.headless)
        {
            auto t = std::chrono::steady_clock::now();
            now = std::chrono::duration<double>(t - g.t0).count();
        }
        else
        {
            glfwPollEvents();
            now = glfwGetTime();
        }

        g.dt = now - g.last_time;
        if (g.last_time == 0.0) g.dt = 0.0;
        g.last_time = now;
    }

    double time_seconds()
    {
        if (g.cfg.headless)
        {
            auto t = std::chrono::steady_clock::now();
            return std::chrono::duration<double>(t - g.t0).count();
        }
        return g.gl_ready ? glfwGetTime() : 0.0;
    }

    double delta_seconds() { return g.dt; }

    bool key_down(int key)
    {
        if (key < 0 || key >= State::KEY_MAX) return false;
        return g.key_down[key];
    }

    bool key_pressed(int key)
    {
        if (key < 0 || key >= State::KEY_MAX) return false;
        return g.key_pressed[key];
    }

    bool key_released(int key)
    {
        if (key < 0 || key >= State::KEY_MAX) return false;
        return g.key_released[key];
    }

    double mouse_x() { return g.mouse_x; }
    double mouse_y() { return g.mouse_y; }
    double mouse_prev_x() { return g.mouse_prev_x; }
    double mouse_prev_y() { return g.mouse_prev_y; }
    double mouse_dx() { return g.mouse_dx; }
    double mouse_dy() { return g.mouse_dy; }
    bool mouse_moved() { return g.mouse_moved; }

    bool mouse_down(int button)
    {
        if (button < 0 || button >= State::MOUSE_BUTTON_MAX) return false;
        return g.mouse_down[button];
    }

    bool mouse_pressed(int button)
    {
        if (button < 0 || button >= State::MOUSE_BUTTON_MAX) return false;
        return g.mouse_pressed[button];
    }

    bool mouse_released(int button)
    {
        if (button < 0 || button >= State::MOUSE_BUTTON_MAX) return false;
        return g.mouse_released[button];
    }

    double mouse_scroll_x() { return g.mouse_scroll_x; }
    double mouse_scroll_y() { return g.mouse_scroll_y; }
    bool mouse_scrolled() { return g.mouse_scrolled; }

    bool mouse_in_window() { return g.mouse_in_window; }
    bool mouse_entered() { return g.mouse_entered; }
    bool mouse_left() { return g.mouse_left; }

    double mouse_fb_x()
    {
        if (g.display_w <= 0 || g.fb_w <= 0) return 0.0;
        return (g.mouse_x / (double)g.display_w) * (double)g.fb_w;
    }

    double mouse_fb_y()
    {
        if (g.display_h <= 0 || g.fb_h <= 0) return 0.0;
        return (g.mouse_y / (double)g.display_h) * (double)g.fb_h;
    }

    int mouse_fb_ix()
    {
        return (int)std::floor(mouse_fb_x());
    }

    int mouse_fb_iy()
    {
        return (int)std::floor(mouse_fb_y());
    }

    void set_cursor_visible(bool visible)
    {
        g.cursor_visible = visible;
        if (g.cfg.headless || !g.window) return;

        if (g.cursor_captured) return;
        glfwSetInputMode(g.window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    }

    bool cursor_visible() { return g.cursor_visible; }

    void set_cursor_captured(bool captured)
    {
        g.cursor_captured = captured;
        if (g.cfg.headless || !g.window) return;

        if (captured)
        {
            glfwSetInputMode(g.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else
        {
            glfwSetInputMode(g.window, GLFW_CURSOR, g.cursor_visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
        }
    }

    bool cursor_captured() { return g.cursor_captured; }

    // ------------------------------------------------------------
    // Framebuffer / state
    // ------------------------------------------------------------
    int fb_width() { return g.fb_w; }
    int fb_height() { return g.fb_h; }
    int display_width() { return g.display_w; }
    int display_height() { return g.display_h; }

    void resize_framebuffer(int new_w, int new_h)
    {
        new_w = std::max(1, new_w);
        new_h = std::max(1, new_h);

        g.fb_w = new_w;
        g.fb_h = new_h;

        g.color.assign((size_t)new_w * new_h * 4, 0);
        for (int y = 0; y < new_h; ++y)
            for (int x = 0; x < new_w; ++x)
                g.color[idx_rgba(new_w, x, y) + 3] = 255;

        if (g.depth_on)
        {
            g.depth.assign((size_t)new_w * new_h, 1.0f);
        }

        g.post_out.clear();
        g.bloom.reset();

        g.dirty_empty = true;

        // Recreate GL texture if possible
        if (g.gl_ready)
        {
            if (g.tex) { glDeleteTextures(1, &g.tex); g.tex = 0; }
            g.can_present = false;
            create_presenter();
        }
    }

    void enable_depth(bool enabled)
    {
        g.depth_on = enabled;
        if (enabled)
            g.depth.assign((size_t)g.fb_w * g.fb_h, 1.0f);
        else
            g.depth.clear();
    }

    bool depth_enabled() { return g.depth_on; }

    void set_blend_mode(BlendMode m) { g.blend = m; }
    BlendMode blend_mode() { return g.blend; }

    void set_clip_rect(int x, int y, int w, int h)
    {
        g.clip_on = true;
        g.clip_x = clampi(x, 0, g.fb_w);
        g.clip_y = clampi(y, 0, g.fb_h);
        g.clip_w = clampi(w, 0, g.fb_w - g.clip_x);
        g.clip_h = clampi(h, 0, g.fb_h - g.clip_y);
    }

    void disable_clip_rect() { g.clip_on = false; }

    void clear_color(Color c)
    {
        for (int y = 0; y < g.fb_h; ++y)
        {
            for (int x = 0; x < g.fb_w; ++x)
            {
                size_t i = idx_rgba(g.fb_w, x, y);
                g.color[i + 0] = c.r;
                g.color[i + 1] = c.g;
                g.color[i + 2] = c.b;
                g.color[i + 3] = c.a;
            }
        }
        g.dirty_empty = false;
        g.dirty_minx = 0; g.dirty_miny = 0;
        g.dirty_maxx = g.fb_w - 1; g.dirty_maxy = g.fb_h - 1;
    }

    void clear_depth(float z)
    {
        if (!g.depth_on) return;
        std::fill(g.depth.begin(), g.depth.end(), z);
    }

    bool can_present() { return g.can_present && g.gl_ready && !g.cfg.headless; }

    void set_present_filter_linear(bool linear)
    {
        g.present_linear = linear;
        if (g.gl_ready && g.tex)
        {
            glBindTexture(GL_TEXTURE_2D, g.tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        }
    }

    void flush_to_screen(bool apply_postprocess)
    {
        if (!can_present()) return;

        // Query actual window framebuffer size (for HiDPI)
        int ww = 0, hh = 0;
        glfwGetFramebufferSize(g.window, &ww, &hh);
        ww = std::max(1, ww);
        hh = std::max(1, hh);

        glViewport(0, 0, ww, hh);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        const uint8_t* src = build_postprocess_output(apply_postprocess);

        glBindTexture(GL_TEXTURE_2D, g.tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // If postprocess is on, it touched whole frame -> upload full
        // If not, you could optimize with dirty rect. Keeping it simple & robust:
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g.fb_w, g.fb_h, GL_RGBA, GL_UNSIGNED_BYTE, src);

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(g.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g.tex);

        glBindVertexArray(g.vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glfwSwapBuffers(g.window);
    }

    void set_postprocess(const PostProcessSettings& s) { g.post = s; }
    const PostProcessSettings& postprocess() { return g.post; }

    // ------------------------------------------------------------
    // Capture
    // ------------------------------------------------------------
    void set_capture_filepath(const std::string& filepath)
    {
        fs::path p(filepath);
        if (p.has_extension())
        {
            g.capture_hint_png = p;
            g.capture_dir.clear();
        }
        else
        {
            g.capture_dir = p;
            g.capture_hint_png.clear();
        }
    }

    void set_frame_index(uint64_t idx) { g.frame_idx = idx; }
    uint64_t frame_index() { return g.frame_idx; }
    void next_frame() { g.frame_idx++; }

    void save_frame_png(bool apply_postprocess)
    {
        fs::path out = resolve_capture_path();
        ensure_parent_dir(out);

        const uint8_t* src = build_postprocess_output(apply_postprocess);
        int stride = g.fb_w * 4;

        int ok = stbi_write_png(out.string().c_str(), g.fb_w, g.fb_h, 4, src, stride);
        if (!ok)
            std::cerr << "[Engine] stbi_write_png failed: " << out.string() << "\n";
        else
            std::cout << "[Engine] Saved: " << out.string() << "\n";
    }

    // ------------------------------------------------------------
    // Raw buffer access
    // ------------------------------------------------------------
    const uint8_t* fb_rgba() { return g.color.data(); }
    uint8_t* fb_rgba_mut() { return g.color.data(); }

    // ------------------------------------------------------------
    // 2D primitives public
    // ------------------------------------------------------------
    void set_pixel(int x, int y, Color c) { write_pixel(x, y, c); }

    Color get_pixel(int x, int y)
    {
        Color c{};
        if (x < 0 || y < 0 || x >= g.fb_w || y >= g.fb_h) return c;
        size_t i = idx_rgba(g.fb_w, x, y);
        c.r = g.color[i + 0];
        c.g = g.color[i + 1];
        c.b = g.color[i + 2];
        c.a = g.color[i + 3];
        return c;
    }

    void draw_line(int x0, int y0, int x1, int y1, Color c, int thickness)
    {
        draw_line_bres(x0, y0, x1, y1, c, thickness);
        dirty_add_rect(std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0) + 1, std::abs(y1 - y0) + 1);
    }

    void draw_rect(int x, int y, int w, int h, Color c, bool filled, int thickness)
    {
        if (w <= 0 || h <= 0) return;
        if (filled)
        {
            for (int yy = y; yy < y + h; ++yy)
                for (int xx = x; xx < x + w; ++xx)
                    write_pixel(xx, yy, c);
        }
        else
        {
            if (thickness < 1) thickness = 1;
            for (int t = 0; t < thickness; ++t)
            {
                int xx0 = x + t, yy0 = y + t, ww = w - 2 * t, hh = h - 2 * t;
                if (ww <= 0 || hh <= 0) break;
                for (int xx = xx0; xx < xx0 + ww; ++xx) { write_pixel(xx, yy0, c); write_pixel(xx, yy0 + hh - 1, c); }
                for (int yy = yy0; yy < yy0 + hh; ++yy) { write_pixel(xx0, yy, c); write_pixel(xx0 + ww - 1, yy, c); }
            }
        }
        dirty_add_rect(x, y, w, h);
    }

    void draw_circle(int cx, int cy, int radius, Color c, bool filled, int thickness)
    {
        if (filled) draw_circle_filled(cx, cy, radius, c);
        else draw_circle_outline(cx, cy, radius, c, thickness);
        dirty_add_rect(cx - radius, cy - radius, radius * 2 + 1, radius * 2 + 1);
    }

    void draw_triangle_outline(Vec2 a, Vec2 b, Vec2 c, Color col, int thickness)
    {
        draw_line((int)a.x, (int)a.y, (int)b.x, (int)b.y, col, thickness);
        draw_line((int)b.x, (int)b.y, (int)c.x, (int)c.y, col, thickness);
        draw_line((int)c.x, (int)c.y, (int)a.x, (int)a.y, col, thickness);
    }

    void draw_triangle_filled(Vec2 a, Vec2 b, Vec2 c, Color col)
    {
        draw_tri_flat(a, b, c, col);
    }

    void draw_triangle_filled_grad(Vec2 a, Color ca, Vec2 b, Color cb, Vec2 c, Color cc)
    {
        draw_tri_grad(a, ca, b, cb, c, cc);
    }

    void draw_triangle_textured(Vec2 a, Vec2 ua, Vec2 b, Vec2 ub, Vec2 c, Vec2 uc,
        const Image& tex, Color tint)
    {
        draw_tri_tex(a, ua, b, ub, c, uc, tex, tint);
    }

    void draw_image(const Image& img, int dstx, int dsty, bool alpha_blend)
    {
        if (!img.valid()) return;

        BlendMode old = g.blend;
        if (alpha_blend) g.blend = BlendMode::Alpha;
        else g.blend = BlendMode::Overwrite;

        for (int y = 0; y < img.h; ++y)
        {
            int yy = dsty + y;
            if (yy < 0 || yy >= g.fb_h) continue;

            for (int x = 0; x < img.w; ++x)
            {
                int xx = dstx + x;
                if (xx < 0 || xx >= g.fb_w) continue;

                size_t si = idx_rgba(img.w, x, y);
                Color c;
                c.r = img.rgba[si + 0];
                c.g = img.rgba[si + 1];
                c.b = img.rgba[si + 2];
                c.a = img.rgba[si + 3];

                write_pixel(xx, yy, c);
            }
        }

        g.blend = old;
        dirty_add_rect(dstx, dsty, img.w, img.h);
    }

    // ------------------------------------------------------------
    // 3D mesh
    // ------------------------------------------------------------
    void draw_mesh(const Vertex3D* verts, int vcount,
        const uint32_t* indices, int icount,
        const Mat4& mvp,
        const Image* texture,
        bool enable_depth_test)
    {
        if (!verts || vcount <= 0 || !indices || icount <= 0) return;
        if ((icount % 3) != 0) return;

        // Project all vertices
        std::vector<VOut> proj((size_t)vcount);
        std::vector<bool> ok((size_t)vcount, false);

        for (int i = 0; i < vcount; ++i)
            ok[(size_t)i] = project_vertex(verts[i], mvp, proj[(size_t)i]);

        // Draw triangles
        for (int i = 0; i < icount; i += 3)
        {
            uint32_t ia = indices[i + 0];
            uint32_t ib = indices[i + 1];
            uint32_t ic = indices[i + 2];
            if (ia >= (uint32_t)vcount || ib >= (uint32_t)vcount || ic >= (uint32_t)vcount) continue;
            if (!ok[ia] || !ok[ib] || !ok[ic]) continue;

            draw_tri_3d(proj[ia], proj[ib], proj[ic], texture, enable_depth_test);
        }
    }
}
