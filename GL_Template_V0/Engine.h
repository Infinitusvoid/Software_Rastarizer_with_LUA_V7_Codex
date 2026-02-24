#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine_
{
    // ------------------------------------------------------------
    // Small "GLFW-like" keycodes (letters/digits match ASCII)
    // (Enough for the demo + Lua later; you can expand as needed.)
    // ------------------------------------------------------------
    constexpr int KEY_SPACE = 32;
    constexpr int KEY_APOSTROPHE = 39;
    constexpr int KEY_COMMA = 44;
    constexpr int KEY_MINUS = 45;
    constexpr int KEY_PERIOD = 46;
    constexpr int KEY_SLASH = 47;
    constexpr int KEY_0 = 48;
    constexpr int KEY_1 = 49;
    constexpr int KEY_2 = 50;
    constexpr int KEY_3 = 51;
    constexpr int KEY_4 = 52;
    constexpr int KEY_5 = 53;
    constexpr int KEY_6 = 54;
    constexpr int KEY_7 = 55;
    constexpr int KEY_8 = 56;
    constexpr int KEY_9 = 57;
    constexpr int KEY_SEMICOLON = 59;
    constexpr int KEY_EQUAL = 61;
    constexpr int KEY_A = 65;
    constexpr int KEY_B = 66;
    constexpr int KEY_C = 67;
    constexpr int KEY_D = 68;
    constexpr int KEY_E = 69;
    constexpr int KEY_F = 70;
    constexpr int KEY_G = 71;
    constexpr int KEY_H = 72;
    constexpr int KEY_I = 73;
    constexpr int KEY_J = 74;
    constexpr int KEY_K = 75;
    constexpr int KEY_L = 76;
    constexpr int KEY_M = 77;
    constexpr int KEY_N = 78;
    constexpr int KEY_O = 79;
    constexpr int KEY_P = 80;
    constexpr int KEY_Q = 81;
    constexpr int KEY_R = 82;
    constexpr int KEY_S = 83;
    constexpr int KEY_T = 84;
    constexpr int KEY_U = 85;
    constexpr int KEY_V = 86;
    constexpr int KEY_W = 87;
    constexpr int KEY_X = 88;
    constexpr int KEY_Y = 89;
    constexpr int KEY_Z = 90;
    constexpr int KEY_LEFT_BRACKET = 91;
    constexpr int KEY_BACKSLASH = 92;
    constexpr int KEY_RIGHT_BRACKET = 93;
    constexpr int KEY_ESCAPE = 256;

    // ------------------------------------------------------------
    // Basic math types (tiny, no glm dependency)
    // ------------------------------------------------------------
    struct Vec2 { float x = 0, y = 0; };
    struct Vec3 { float x = 0, y = 0, z = 0; };
    struct Vec4 { float x = 0, y = 0, z = 0, w = 1; };

    struct Mat4
    {
        // column-major (OpenGL style)
        float m[16]{};
    };

    Mat4 mat4_identity();
    Mat4 mat4_mul(const Mat4& a, const Mat4& b);
    Vec4 mat4_mul(const Mat4& a, const Vec4& v);

    Mat4 mat4_translate(const Vec3& t);
    Mat4 mat4_scale(const Vec3& s);
    Mat4 mat4_rotate_y(float radians);
    Mat4 mat4_rotate_x(float radians);
    Mat4 mat4_rotate_z(float radians);
    Mat4 mat4_perspective(float fovy_radians, float aspect, float znear, float zfar);
    Mat4 mat4_look_at(const Vec3& eye, const Vec3& at, const Vec3& up);

    // ------------------------------------------------------------
    // Color + blending
    // ------------------------------------------------------------
    struct Color
    {
        uint8_t r = 0, g = 0, b = 0, a = 255;
    };

    enum class BlendMode
    {
        Overwrite,
        Alpha,
        Additive,
        Multiply
    };

    // ------------------------------------------------------------
    // Images (CPU-side)
    // ------------------------------------------------------------
    struct Image
    {
        int w = 0;
        int h = 0;
        std::vector<uint8_t> rgba; // size = w*h*4
        bool valid() const { return w > 0 && h > 0 && (int)rgba.size() == w * h * 4; }
    };

    Image load_image_rgba(const std::string& path);
    Image make_checker_rgba(int w, int h, int cell);

    // ------------------------------------------------------------
    // Post-processing (CPU)
    // ------------------------------------------------------------
    struct BloomSettings
    {
        bool  enabled = true;

        // Bright-pass threshold in [0..1]
        float threshold = 0.75f;

        // Strength of added bloom
        float intensity = 1.25f;

        // Downsample factor for bloom buffers (2, 4, 8...)
        int   downsample = 4;

        // Gaussian sigma (blur amount) at bloom resolution
        float sigma = 6.0f;
    };

    struct ToneSettings
    {
        bool  enabled = true;
        float exposure = 1.25f; // >1 brighter
        float gamma = 2.2f;  // typical display gamma
    };

    struct PostProcessSettings
    {
        BloomSettings bloom{};
        ToneSettings  tone{};
    };

    // ------------------------------------------------------------
    // Engine config
    // ------------------------------------------------------------
    struct Config
    {
        // Display (window) size
        int display_w = 960;
        int display_h = 540;

        // CPU framebuffer size (can be 8K+)
        int fb_w = 1920;
        int fb_h = 1080;

        std::string title = "Software Rasterizer (CPU -> OpenGL Texture)";

        bool resizable = true;
        bool vsync = false;
        bool linear_filter = false; // present filter
        bool hidden_window = false; // good for offline dumping
        bool headless = false; // no window, no OpenGL (still rasterize + save)
    };

    // ------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------
    bool init(const Config& cfg);
    void shutdown();

    // ------------------------------------------------------------
    // Loop + input
    // ------------------------------------------------------------
    bool should_close();
    void request_close();
    void poll_events();         // call every loop
    double time_seconds();      // from GLFW timer (or monotonic fallback headless)
    double delta_seconds();     // time since last poll_events()

    bool key_down(int key);
    bool key_pressed(int key);  // true only on the frame the key transitions up->down
    bool key_released(int key);

    // ------------------------------------------------------------
    // Framebuffer / state
    // ------------------------------------------------------------
    int  fb_width();
    int  fb_height();
    int  display_width();
    int  display_height();

    // Resize CPU framebuffer (reallocates)
    void resize_framebuffer(int new_w, int new_h);

    // Depth buffer enable/disable
    void enable_depth(bool enabled);
    bool depth_enabled();

    // Blending mode
    void set_blend_mode(BlendMode m);
    BlendMode blend_mode();

    // Clip rect (optional) - coordinates in framebuffer pixels
    // If disabled, draws to full framebuffer.
    void set_clip_rect(int x, int y, int w, int h);
    void disable_clip_rect();

    // Clear operations
    void clear_color(Color c);
    void clear_depth(float z = 1.0f);

    // Present
    bool can_present();                 // false if headless or framebuffer too large for GPU texture
    void set_present_filter_linear(bool linear);
    void flush_to_screen(bool apply_postprocess = true);

    // Post process
    void set_postprocess(const PostProcessSettings& s);
    const PostProcessSettings& postprocess();

    // ------------------------------------------------------------
    // Capture
    // ------------------------------------------------------------
    void set_capture_filepath(const std::string& filepath); // dir or .png hint
    void set_frame_index(uint64_t idx);
    uint64_t frame_index();
    void next_frame(); // increments frame index

    // Saves post-processed output if apply_postprocess=true, otherwise raw framebuffer.
    void save_frame_png(bool apply_postprocess = true);

    // ------------------------------------------------------------
    // Raw buffer access (Lua friendly)
    // ------------------------------------------------------------
    const uint8_t* fb_rgba();
    uint8_t* fb_rgba_mut();

    // ------------------------------------------------------------
    // 2D primitives (software rasterizer)
    // ------------------------------------------------------------
    void set_pixel(int x, int y, Color c);
    Color get_pixel(int x, int y);

    void draw_line(int x0, int y0, int x1, int y1, Color c, int thickness = 1);

    void draw_rect(int x, int y, int w, int h, Color c, bool filled = true, int thickness = 1);

    void draw_circle(int cx, int cy, int radius, Color c, bool filled = true, int thickness = 1);

    // Triangles in framebuffer pixel coordinates.
    void draw_triangle_outline(Vec2 a, Vec2 b, Vec2 c, Color col, int thickness = 1);

    // Flat-color filled triangle (no depth)
    void draw_triangle_filled(Vec2 a, Vec2 b, Vec2 c, Color col);

    // Gouraud shaded filled triangle (per-vertex colors, no depth)
    void draw_triangle_filled_grad(Vec2 a, Color ca, Vec2 b, Color cb, Vec2 c, Color cc);

    // Textured filled triangle (no depth). UV in [0..1], nearest sampling.
    void draw_triangle_textured(Vec2 a, Vec2 ua, Vec2 b, Vec2 ub, Vec2 c, Vec2 uc,
        const Image& tex, Color tint = { 255,255,255,255 });

    // Blit image (top-left) with optional alpha blend
    void draw_image(const Image& img, int dstx, int dsty, bool alpha_blend = true);

    // ------------------------------------------------------------
    // 3D mesh pipeline (MVP -> triangles -> depth + texture optional)
    // ------------------------------------------------------------
    struct Vertex3D
    {
        Vec3 pos;
        Vec3 color; // 0..1
        Vec2 uv;    // 0..1
    };

    // Draw indexed triangles (icount must be multiple of 3).
    // If texture==nullptr, uses vertex colors only.
    void draw_mesh(const Vertex3D* verts, int vcount,
        const uint32_t* indices, int icount,
        const Mat4& mvp,
        const Image* texture = nullptr,
        bool enable_depth_test = true);
}
