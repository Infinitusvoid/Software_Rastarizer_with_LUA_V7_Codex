#pragma once

#include "Engine.h"

#include <sol/sol.hpp>
#include <functional>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <algorithm>

namespace EngineLuaBridge_
{

// -------------------------
// Decode helpers (Lua -> C++)
// All commands are Lua arrays: arr[1] = op string, arr[2..] = args
// -------------------------

inline std::string get_op1(const sol::table& arr)
{
    sol::object o = arr[1];
    if (!o.valid() || o.is<sol::lua_nil_t>())
        throw std::runtime_error("Command array missing op at index 1");
    if (!o.is<std::string>())
        throw std::runtime_error("Command op at index 1 must be a string");
    return o.as<std::string>();
}

inline int clamp_int(int v, int lo, int hi)
{
    return std::max(lo, std::min(hi, v));
}

inline bool obj_is_nil(const sol::object& o)
{
    return !o.valid() || o.is<sol::lua_nil_t>();
}

inline double obj_to_number(const sol::object& o, const char* what)
{
    if (o.is<double>()) return o.as<double>();
    if (o.is<float>())  return (double)o.as<float>();
    if (o.is<int>())    return (double)o.as<int>();
    if (o.is<uint64_t>()) return (double)o.as<uint64_t>();
    throw std::runtime_error(std::string("Expected number for ") + what);
}

inline int get_int(const sol::table& arr, int idx, int def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing int arg at index " + std::to_string(idx));
        return def;
    }
    if (o.is<int>()) return o.as<int>();
    if (o.is<double>()) return (int)o.as<double>();
    if (o.is<bool>()) return o.as<bool>() ? 1 : 0;
    throw std::runtime_error("Expected int at index " + std::to_string(idx));
}

inline uint64_t get_u64(const sol::table& arr, int idx, uint64_t def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing u64 arg at index " + std::to_string(idx));
        return def;
    }
    if (o.is<uint64_t>()) return o.as<uint64_t>();
    if (o.is<int>()) return (uint64_t)o.as<int>();
    if (o.is<double>()) return (uint64_t)o.as<double>();
    throw std::runtime_error("Expected u64 at index " + std::to_string(idx));
}

inline float get_float(const sol::table& arr, int idx, float def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing float arg at index " + std::to_string(idx));
        return def;
    }
    if (o.is<float>()) return o.as<float>();
    if (o.is<double>()) return (float)o.as<double>();
    if (o.is<int>()) return (float)o.as<int>();
    throw std::runtime_error("Expected float at index " + std::to_string(idx));
}

inline double get_double(const sol::table& arr, int idx, double def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing double arg at index " + std::to_string(idx));
        return def;
    }
    if (o.is<double>()) return o.as<double>();
    if (o.is<float>()) return (double)o.as<float>();
    if (o.is<int>()) return (double)o.as<int>();
    throw std::runtime_error("Expected double at index " + std::to_string(idx));
}

inline bool get_bool(const sol::table& arr, int idx, bool def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing bool arg at index " + std::to_string(idx));
        return def;
    }
    if (o.is<bool>()) return o.as<bool>();
    if (o.is<int>()) return o.as<int>() != 0;
    if (o.is<double>()) return o.as<double>() != 0.0;
    throw std::runtime_error("Expected bool at index " + std::to_string(idx));
}

inline std::string get_string(const sol::table& arr, int idx, const std::string& def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing string arg at index " + std::to_string(idx));
        return def;
    }
    if (!o.is<std::string>())
        throw std::runtime_error("Expected string at index " + std::to_string(idx));
    return o.as<std::string>();
}

// Accept {x=,y=} or {1,2}
inline float get_field_float_1_2(const sol::table& t, const char* key, int idx1based, float def, bool required, const char* what)
{
    sol::object o = t[key];
    if (obj_is_nil(o)) o = t[idx1based];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error(std::string("Missing field for ") + what);
        return def;
    }
    return (float)obj_to_number(o, what);
}

inline int get_field_int_1_2_3_4(const sol::table& t, const char* key, int idx1based, int def, bool required, const char* what)
{
    sol::object o = t[key];
    if (obj_is_nil(o)) o = t[idx1based];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error(std::string("Missing field for ") + what);
        return def;
    }
    if (o.is<int>()) return o.as<int>();
    if (o.is<double>()) return (int)o.as<double>();
    if (o.is<bool>()) return o.as<bool>() ? 1 : 0;
    throw std::runtime_error(std::string("Expected int field for ") + what);
}

inline float get_field_float_named_or_index(const sol::table& t,
    const char* key, int idx1based,
    float def, bool required,
    const char* what)
{
    sol::object o = t[key];
    if (obj_is_nil(o)) o = t[idx1based];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error(std::string("Missing field for ") + what);
        return def;
    }
    return (float)obj_to_number(o, what);
}

inline Engine_::Vec2 get_vec2(const sol::table& arr, int idx, Engine_::Vec2 def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing Vec2 arg at index " + std::to_string(idx));
        return def;
    }
    if (!o.is<sol::table>())
        throw std::runtime_error("Expected Vec2 table at index " + std::to_string(idx));
    sol::table t = o.as<sol::table>();
    Engine_::Vec2 v;
    v.x = get_field_float_1_2(t, "x", 1, def.x, required, "Vec2.x");
    v.y = get_field_float_1_2(t, "y", 2, def.y, required, "Vec2.y");
    return v;
}

inline Engine_::Vec3 get_vec3(const sol::table& arr, int idx, Engine_::Vec3 def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing Vec3 arg at index " + std::to_string(idx));
        return def;
    }
    if (!o.is<sol::table>())
        throw std::runtime_error("Expected Vec3 table at index " + std::to_string(idx));
    sol::table t = o.as<sol::table>();
    Engine_::Vec3 v;
    v.x = get_field_float_named_or_index(t, "x", 1, def.x, required, "Vec3.x");
    v.y = get_field_float_named_or_index(t, "y", 2, def.y, required, "Vec3.y");
    v.z = get_field_float_named_or_index(t, "z", 3, def.z, required, "Vec3.z");
    return v;
}

inline Engine_::Vec4 get_vec4(const sol::table& arr, int idx, Engine_::Vec4 def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing Vec4 arg at index " + std::to_string(idx));
        return def;
    }
    if (!o.is<sol::table>())
        throw std::runtime_error("Expected Vec4 table at index " + std::to_string(idx));
    sol::table t = o.as<sol::table>();
    Engine_::Vec4 v;
    v.x = get_field_float_named_or_index(t, "x", 1, def.x, required, "Vec4.x");
    v.y = get_field_float_named_or_index(t, "y", 2, def.y, required, "Vec4.y");
    v.z = get_field_float_named_or_index(t, "z", 3, def.z, required, "Vec4.z");
    v.w = get_field_float_named_or_index(t, "w", 4, def.w, required, "Vec4.w");
    return v;
}

inline Engine_::Mat4 get_mat4(const sol::table& arr, int idx, Engine_::Mat4 def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing Mat4 arg at index " + std::to_string(idx));
        return def;
    }
    if (!o.is<sol::table>())
        throw std::runtime_error("Expected Mat4 table at index " + std::to_string(idx));
    sol::table t = o.as<sol::table>();
    Engine_::Mat4 m = def;
    for (int i = 0; i < 16; ++i) {
        sol::object oi = t[i + 1];
        if (obj_is_nil(oi)) {
            if (required) throw std::runtime_error("Mat4 missing element " + std::to_string(i + 1));
            m.m[i] = def.m[i];
        }
        else {
            m.m[i] = (float)obj_to_number(oi, "Mat4[i]");
        }
    }
    return m;
}

inline Engine_::Color get_color(const sol::table& arr, int idx, Engine_::Color def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing Color arg at index " + std::to_string(idx));
        return def;
    }
    if (!o.is<sol::table>())
        throw std::runtime_error("Expected Color table at index " + std::to_string(idx));
    sol::table t = o.as<sol::table>();

    int r = get_field_int_1_2_3_4(t, "r", 1, def.r, required, "Color.r");
    int g = get_field_int_1_2_3_4(t, "g", 2, def.g, required, "Color.g");
    int b = get_field_int_1_2_3_4(t, "b", 3, def.b, required, "Color.b");

    sol::object oa = t["a"];
    if (obj_is_nil(oa)) oa = t[4];
    int a = def.a;
    if (!obj_is_nil(oa)) {
        if (oa.is<int>()) a = oa.as<int>();
        else if (oa.is<double>()) a = (int)oa.as<double>();
        else a = def.a;
    }

    Engine_::Color c;
    c.r = (uint8_t)clamp_int(r, 0, 255);
    c.g = (uint8_t)clamp_int(g, 0, 255);
    c.b = (uint8_t)clamp_int(b, 0, 255);
    c.a = (uint8_t)clamp_int(a, 0, 255);
    return c;
}

inline Engine_::BlendMode get_blend_mode(const sol::table& arr, int idx, Engine_::BlendMode def, bool required)
{
    sol::object o = arr[idx];
    if (obj_is_nil(o)) {
        if (required) throw std::runtime_error("Missing BlendMode arg at index " + std::to_string(idx));
        return def;
    }
    if (o.is<int>()) {
        return (Engine_::BlendMode)o.as<int>();
    }
    if (o.is<std::string>()) {
        const std::string s = o.as<std::string>();
        if (s == "Overwrite") return Engine_::BlendMode::Overwrite;
        if (s == "Alpha") return Engine_::BlendMode::Alpha;
        if (s == "Additive") return Engine_::BlendMode::Additive;
        if (s == "Multiply") return Engine_::BlendMode::Multiply;
        throw std::runtime_error("Unknown BlendMode string: " + s);
    }
    throw std::runtime_error("Expected BlendMode (string or int) at index " + std::to_string(idx));
}

inline const char* blend_mode_to_cstr(Engine_::BlendMode m)
{
    switch (m) {
    case Engine_::BlendMode::Overwrite: return "Overwrite";
    case Engine_::BlendMode::Alpha: return "Alpha";
    case Engine_::BlendMode::Additive: return "Additive";
    case Engine_::BlendMode::Multiply: return "Multiply";
    default: return "Overwrite";
    }
}

// -------------------------
// Return helpers (C++ -> Lua)
// -------------------------
inline sol::table color_to_table(sol::state_view lua, Engine_::Color c)
{
    sol::table t = lua.create_table();
    t["r"] = (int)c.r;
    t["g"] = (int)c.g;
    t["b"] = (int)c.b;
    t["a"] = (int)c.a;
    return t;
}

inline sol::table vec2_to_table(sol::state_view lua, const Engine_::Vec2& v)
{
    sol::table t = lua.create_table();
    t["x"] = v.x;
    t["y"] = v.y;
    return t;
}

inline sol::table vec3_to_table(sol::state_view lua, const Engine_::Vec3& v)
{
    sol::table t = lua.create_table();
    t["x"] = v.x;
    t["y"] = v.y;
    t["z"] = v.z;
    return t;
}

inline sol::table vec4_to_table(sol::state_view lua, const Engine_::Vec4& v)
{
    sol::table t = lua.create_table();
    t["x"] = v.x;
    t["y"] = v.y;
    t["z"] = v.z;
    t["w"] = v.w;
    return t;
}

inline sol::table mat4_to_table(sol::state_view lua, const Engine_::Mat4& m)
{
    sol::table t = lua.create_table(16, 0);
    for (int i = 0; i < 16; ++i) t[i + 1] = m.m[i];
    return t;
}

// -------------------------
// Global callbacks (set these from C++).
// If a callback is not set, dispatch() will throw.
// -------------------------

inline std::function<double()> cb_time_seconds;
inline std::function<double()> cb_delta_seconds;
inline std::function<bool(int key)> cb_key_down;
inline std::function<bool(int key)> cb_key_pressed;
inline std::function<bool(int key)> cb_key_released;
inline std::function<double()> cb_mouse_x;
inline std::function<double()> cb_mouse_y;
inline std::function<double()> cb_mouse_prev_x;
inline std::function<double()> cb_mouse_prev_y;
inline std::function<double()> cb_mouse_dx;
inline std::function<double()> cb_mouse_dy;
inline std::function<bool()> cb_mouse_moved;
inline std::function<bool(int button)> cb_mouse_down;
inline std::function<bool(int button)> cb_mouse_pressed;
inline std::function<bool(int button)> cb_mouse_released;
inline std::function<double()> cb_mouse_scroll_x;
inline std::function<double()> cb_mouse_scroll_y;
inline std::function<bool()> cb_mouse_scrolled;
inline std::function<bool()> cb_mouse_in_window;
inline std::function<bool()> cb_mouse_entered;
inline std::function<bool()> cb_mouse_left;
inline std::function<double()> cb_mouse_fb_x;
inline std::function<double()> cb_mouse_fb_y;
inline std::function<int()> cb_mouse_fb_ix;
inline std::function<int()> cb_mouse_fb_iy;
inline std::function<void(bool visible)> cb_set_cursor_visible;
inline std::function<bool()> cb_cursor_visible;
inline std::function<void(bool captured)> cb_set_cursor_captured;
inline std::function<bool()> cb_cursor_captured;
inline std::function<bool()> cb_should_close;
inline std::function<void()> cb_request_close;
inline std::function<void()> cb_poll_events;
inline std::function<int()> cb_fb_width;
inline std::function<int()> cb_fb_height;
inline std::function<int()> cb_display_width;
inline std::function<int()> cb_display_height;
inline std::function<void(int w, int h)> cb_resize_framebuffer;
inline std::function<void(bool enabled)> cb_enable_depth;
inline std::function<bool()> cb_depth_enabled;
inline std::function<void(Engine_::BlendMode mode)> cb_set_blend_mode;
inline std::function<Engine_::BlendMode()> cb_blend_mode;
inline std::function<void(int x, int y, int w, int h)> cb_set_clip_rect;
inline std::function<void()> cb_disable_clip_rect;
inline std::function<void(Engine_::Color c)> cb_clear_color;
inline std::function<void(float z)> cb_clear_depth;
inline std::function<void(bool linear)> cb_set_present_filter_linear;
inline std::function<void(bool apply_postprocess)> cb_flush_to_screen;
inline std::function<void(const std::string& filepath)> cb_set_capture_filepath;
inline std::function<void(uint64_t idx)> cb_set_frame_index;
inline std::function<uint64_t()> cb_frame_index;
inline std::function<void()> cb_next_frame;
inline std::function<void(bool apply_postprocess)> cb_save_frame_png;
inline std::function<void(int x, int y, Engine_::Color c)> cb_set_pixel;
inline std::function<Engine_::Color(int x, int y)> cb_get_pixel;
inline std::function<void(int x0, int y0, int x1, int y1, Engine_::Color c, int thickness)> cb_draw_line;
inline std::function<void(int x, int y, int w, int h, Engine_::Color c, bool filled, int thickness)> cb_draw_rect;
inline std::function<void(int cx, int cy, int radius, Engine_::Color c, bool filled, int thickness)> cb_draw_circle;
inline std::function<void(Engine_::Vec2 a, Engine_::Vec2 b, Engine_::Vec2 c, Engine_::Color col, int thickness)> cb_draw_triangle_outline;
inline std::function<void(Engine_::Vec2 a, Engine_::Vec2 b, Engine_::Vec2 c, Engine_::Color col)> cb_draw_triangle_filled;
inline std::function<void(Engine_::Vec2 a, Engine_::Color ca, Engine_::Vec2 b, Engine_::Color cb, Engine_::Vec2 c, Engine_::Color cc)> cb_draw_triangle_filled_grad;
inline std::function<void(Engine_::Vec2 a, Engine_::Vec2 ua, Engine_::Vec2 b, Engine_::Vec2 ub, Engine_::Vec2 c, Engine_::Vec2 uc, const std::string& texture_name, Engine_::Color tint)> cb_draw_triangle_textured_named;
inline std::function<Engine_::Mat4()> cb_mat4_identity;
inline std::function<Engine_::Mat4(const Engine_::Mat4& a, const Engine_::Mat4& b)> cb_mat4_mul;
inline std::function<Engine_::Mat4(Engine_::Vec3 t)> cb_mat4_translate;
inline std::function<Engine_::Mat4(float radians)> cb_mat4_rotate_x;
inline std::function<Engine_::Mat4(float radians)> cb_mat4_rotate_y;
inline std::function<Engine_::Mat4(float radians)> cb_mat4_rotate_z;
inline std::function<Engine_::Mat4(float fovy_radians, float aspect, float znear, float zfar)> cb_mat4_perspective;
inline std::function<Engine_::Mat4(Engine_::Vec3 eye, Engine_::Vec3 center, Engine_::Vec3 up)> cb_mat4_look_at;
inline std::function<bool(const std::string& name, int w, int h, int cell)> cb_tex_make_checker;
inline std::function<bool(const std::string& name, const std::string& filepath)> cb_tex_load;
inline std::function<bool(const std::string& name)> cb_tex_delete;
inline std::function<bool(const std::string& name)> cb_tex_exists;
inline std::function<bool(const std::string& name)> cb_tex_from_framebuffer;
inline std::function<bool(const std::string& name, float size)> cb_mesh_make_cube;
inline std::function<bool(const std::string& name)> cb_mesh_delete;
inline std::function<bool(const std::string& name)> cb_mesh_exists;
inline std::function<void(const std::string& mesh_name, const Engine_::Mat4& mvp, const std::string& texture_name, bool enable_depth_test)> cb_draw_mesh_named;
inline std::function<void(bool enabled, float threshold, float intensity, int downsample, float sigma)> cb_pp_set_bloom;
inline std::function<void(bool enabled, float exposure, float gamma)> cb_pp_set_tone;
inline std::function<void()> cb_pp_reset;

// Optional: bind defaults to Engine_::* functions directly.
// Call this once after including the generated header.
// NOTE: ops marked no_default=true are intentionally NOT bound here.
inline void bind_engine_defaults()
{
    cb_time_seconds = [](){ return Engine_::time_seconds(); };
    cb_delta_seconds = [](){ return Engine_::delta_seconds(); };
    cb_key_down = [](int key){ return Engine_::key_down(key); };
    cb_key_pressed = [](int key){ return Engine_::key_pressed(key); };
    cb_key_released = [](int key){ return Engine_::key_released(key); };
    cb_mouse_x = [](){ return Engine_::mouse_x(); };
    cb_mouse_y = [](){ return Engine_::mouse_y(); };
    cb_mouse_prev_x = [](){ return Engine_::mouse_prev_x(); };
    cb_mouse_prev_y = [](){ return Engine_::mouse_prev_y(); };
    cb_mouse_dx = [](){ return Engine_::mouse_dx(); };
    cb_mouse_dy = [](){ return Engine_::mouse_dy(); };
    cb_mouse_moved = [](){ return Engine_::mouse_moved(); };
    cb_mouse_down = [](int button){ return Engine_::mouse_down(button); };
    cb_mouse_pressed = [](int button){ return Engine_::mouse_pressed(button); };
    cb_mouse_released = [](int button){ return Engine_::mouse_released(button); };
    cb_mouse_scroll_x = [](){ return Engine_::mouse_scroll_x(); };
    cb_mouse_scroll_y = [](){ return Engine_::mouse_scroll_y(); };
    cb_mouse_scrolled = [](){ return Engine_::mouse_scrolled(); };
    cb_mouse_in_window = [](){ return Engine_::mouse_in_window(); };
    cb_mouse_entered = [](){ return Engine_::mouse_entered(); };
    cb_mouse_left = [](){ return Engine_::mouse_left(); };
    cb_mouse_fb_x = [](){ return Engine_::mouse_fb_x(); };
    cb_mouse_fb_y = [](){ return Engine_::mouse_fb_y(); };
    cb_mouse_fb_ix = [](){ return Engine_::mouse_fb_ix(); };
    cb_mouse_fb_iy = [](){ return Engine_::mouse_fb_iy(); };
    cb_set_cursor_visible = [](bool visible){ Engine_::set_cursor_visible(visible); };
    cb_cursor_visible = [](){ return Engine_::cursor_visible(); };
    cb_set_cursor_captured = [](bool captured){ Engine_::set_cursor_captured(captured); };
    cb_cursor_captured = [](){ return Engine_::cursor_captured(); };
    cb_should_close = [](){ return Engine_::should_close(); };
    cb_request_close = [](){ Engine_::request_close(); };
    cb_poll_events = [](){ Engine_::poll_events(); };
    cb_fb_width = [](){ return Engine_::fb_width(); };
    cb_fb_height = [](){ return Engine_::fb_height(); };
    cb_display_width = [](){ return Engine_::display_width(); };
    cb_display_height = [](){ return Engine_::display_height(); };
    cb_resize_framebuffer = [](int w, int h){ Engine_::resize_framebuffer(w, h); };
    cb_enable_depth = [](bool enabled){ Engine_::enable_depth(enabled); };
    cb_depth_enabled = [](){ return Engine_::depth_enabled(); };
    cb_set_blend_mode = [](Engine_::BlendMode mode){ Engine_::set_blend_mode(mode); };
    cb_blend_mode = [](){ return Engine_::blend_mode(); };
    cb_set_clip_rect = [](int x, int y, int w, int h){ Engine_::set_clip_rect(x, y, w, h); };
    cb_disable_clip_rect = [](){ Engine_::disable_clip_rect(); };
    cb_clear_color = [](Engine_::Color c){ Engine_::clear_color(c); };
    cb_clear_depth = [](float z){ Engine_::clear_depth(z); };
    cb_set_present_filter_linear = [](bool linear){ Engine_::set_present_filter_linear(linear); };
    cb_flush_to_screen = [](bool apply_postprocess){ Engine_::flush_to_screen(apply_postprocess); };
    cb_set_capture_filepath = [](const std::string& filepath){ Engine_::set_capture_filepath(filepath); };
    cb_set_frame_index = [](uint64_t idx){ Engine_::set_frame_index(idx); };
    cb_frame_index = [](){ return Engine_::frame_index(); };
    cb_next_frame = [](){ Engine_::next_frame(); };
    cb_save_frame_png = [](bool apply_postprocess){ Engine_::save_frame_png(apply_postprocess); };
    cb_set_pixel = [](int x, int y, Engine_::Color c){ Engine_::set_pixel(x, y, c); };
    cb_get_pixel = [](int x, int y){ return Engine_::get_pixel(x, y); };
    cb_draw_line = [](int x0, int y0, int x1, int y1, Engine_::Color c, int thickness){ Engine_::draw_line(x0, y0, x1, y1, c, thickness); };
    cb_draw_rect = [](int x, int y, int w, int h, Engine_::Color c, bool filled, int thickness){ Engine_::draw_rect(x, y, w, h, c, filled, thickness); };
    cb_draw_circle = [](int cx, int cy, int radius, Engine_::Color c, bool filled, int thickness){ Engine_::draw_circle(cx, cy, radius, c, filled, thickness); };
    cb_draw_triangle_outline = [](Engine_::Vec2 a, Engine_::Vec2 b, Engine_::Vec2 c, Engine_::Color col, int thickness){ Engine_::draw_triangle_outline(a, b, c, col, thickness); };
    cb_draw_triangle_filled = [](Engine_::Vec2 a, Engine_::Vec2 b, Engine_::Vec2 c, Engine_::Color col){ Engine_::draw_triangle_filled(a, b, c, col); };
    cb_draw_triangle_filled_grad = [](Engine_::Vec2 a, Engine_::Color ca, Engine_::Vec2 b, Engine_::Color cb, Engine_::Vec2 c, Engine_::Color cc){ Engine_::draw_triangle_filled_grad(a, ca, b, cb, c, cc); };
    cb_mat4_identity = [](){ return Engine_::mat4_identity(); };
    cb_mat4_mul = [](const Engine_::Mat4& a, const Engine_::Mat4& b){ return Engine_::mat4_mul(a, b); };
    cb_mat4_translate = [](Engine_::Vec3 t){ return Engine_::mat4_translate(t); };
    cb_mat4_rotate_x = [](float radians){ return Engine_::mat4_rotate_x(radians); };
    cb_mat4_rotate_y = [](float radians){ return Engine_::mat4_rotate_y(radians); };
    cb_mat4_rotate_z = [](float radians){ return Engine_::mat4_rotate_z(radians); };
    cb_mat4_perspective = [](float fovy_radians, float aspect, float znear, float zfar){ return Engine_::mat4_perspective(fovy_radians, aspect, znear, zfar); };
    cb_mat4_look_at = [](Engine_::Vec3 eye, Engine_::Vec3 center, Engine_::Vec3 up){ return Engine_::mat4_look_at(eye, center, up); };
}

// Execute a single command array immediately.
// Returns 0 values for void ops, or 1+ values for query ops.
inline sol::variadic_results dispatch(sol::this_state ts, const sol::table& arr)
{
    sol::state_view lua(ts);
    sol::variadic_results out;
    const std::string op = get_op1(arr);
    
    if (op == "time_seconds")
    {
        if (!cb_time_seconds) throw std::runtime_error("Callback not set for op: time_seconds");
        auto r = cb_time_seconds();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "delta_seconds")
    {
        if (!cb_delta_seconds) throw std::runtime_error("Callback not set for op: delta_seconds");
        auto r = cb_delta_seconds();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "key_down")
    {
        const int key = get_int(arr, 2, 0, true);
        if (!cb_key_down) throw std::runtime_error("Callback not set for op: key_down");
        auto r = cb_key_down(key);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "key_pressed")
    {
        const int key = get_int(arr, 2, 0, true);
        if (!cb_key_pressed) throw std::runtime_error("Callback not set for op: key_pressed");
        auto r = cb_key_pressed(key);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "key_released")
    {
        const int key = get_int(arr, 2, 0, true);
        if (!cb_key_released) throw std::runtime_error("Callback not set for op: key_released");
        auto r = cb_key_released(key);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_x")
    {
        if (!cb_mouse_x) throw std::runtime_error("Callback not set for op: mouse_x");
        auto r = cb_mouse_x();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_y")
    {
        if (!cb_mouse_y) throw std::runtime_error("Callback not set for op: mouse_y");
        auto r = cb_mouse_y();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_prev_x")
    {
        if (!cb_mouse_prev_x) throw std::runtime_error("Callback not set for op: mouse_prev_x");
        auto r = cb_mouse_prev_x();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_prev_y")
    {
        if (!cb_mouse_prev_y) throw std::runtime_error("Callback not set for op: mouse_prev_y");
        auto r = cb_mouse_prev_y();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_dx")
    {
        if (!cb_mouse_dx) throw std::runtime_error("Callback not set for op: mouse_dx");
        auto r = cb_mouse_dx();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_dy")
    {
        if (!cb_mouse_dy) throw std::runtime_error("Callback not set for op: mouse_dy");
        auto r = cb_mouse_dy();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_moved")
    {
        if (!cb_mouse_moved) throw std::runtime_error("Callback not set for op: mouse_moved");
        auto r = cb_mouse_moved();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_down")
    {
        const int button = get_int(arr, 2, 0, true);
        if (!cb_mouse_down) throw std::runtime_error("Callback not set for op: mouse_down");
        auto r = cb_mouse_down(button);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_pressed")
    {
        const int button = get_int(arr, 2, 0, true);
        if (!cb_mouse_pressed) throw std::runtime_error("Callback not set for op: mouse_pressed");
        auto r = cb_mouse_pressed(button);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_released")
    {
        const int button = get_int(arr, 2, 0, true);
        if (!cb_mouse_released) throw std::runtime_error("Callback not set for op: mouse_released");
        auto r = cb_mouse_released(button);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_scroll_x")
    {
        if (!cb_mouse_scroll_x) throw std::runtime_error("Callback not set for op: mouse_scroll_x");
        auto r = cb_mouse_scroll_x();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_scroll_y")
    {
        if (!cb_mouse_scroll_y) throw std::runtime_error("Callback not set for op: mouse_scroll_y");
        auto r = cb_mouse_scroll_y();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_scrolled")
    {
        if (!cb_mouse_scrolled) throw std::runtime_error("Callback not set for op: mouse_scrolled");
        auto r = cb_mouse_scrolled();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_in_window")
    {
        if (!cb_mouse_in_window) throw std::runtime_error("Callback not set for op: mouse_in_window");
        auto r = cb_mouse_in_window();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_entered")
    {
        if (!cb_mouse_entered) throw std::runtime_error("Callback not set for op: mouse_entered");
        auto r = cb_mouse_entered();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_left")
    {
        if (!cb_mouse_left) throw std::runtime_error("Callback not set for op: mouse_left");
        auto r = cb_mouse_left();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_fb_x")
    {
        if (!cb_mouse_fb_x) throw std::runtime_error("Callback not set for op: mouse_fb_x");
        auto r = cb_mouse_fb_x();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_fb_y")
    {
        if (!cb_mouse_fb_y) throw std::runtime_error("Callback not set for op: mouse_fb_y");
        auto r = cb_mouse_fb_y();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_fb_ix")
    {
        if (!cb_mouse_fb_ix) throw std::runtime_error("Callback not set for op: mouse_fb_ix");
        auto r = cb_mouse_fb_ix();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mouse_fb_iy")
    {
        if (!cb_mouse_fb_iy) throw std::runtime_error("Callback not set for op: mouse_fb_iy");
        auto r = cb_mouse_fb_iy();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "set_cursor_visible")
    {
        const bool visible = get_bool(arr, 2, false, true);
        if (!cb_set_cursor_visible) throw std::runtime_error("Callback not set for op: set_cursor_visible");
        cb_set_cursor_visible(visible);
        return out;
    }
    
    else if (op == "cursor_visible")
    {
        if (!cb_cursor_visible) throw std::runtime_error("Callback not set for op: cursor_visible");
        auto r = cb_cursor_visible();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "set_cursor_captured")
    {
        const bool captured = get_bool(arr, 2, false, true);
        if (!cb_set_cursor_captured) throw std::runtime_error("Callback not set for op: set_cursor_captured");
        cb_set_cursor_captured(captured);
        return out;
    }
    
    else if (op == "cursor_captured")
    {
        if (!cb_cursor_captured) throw std::runtime_error("Callback not set for op: cursor_captured");
        auto r = cb_cursor_captured();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "should_close")
    {
        if (!cb_should_close) throw std::runtime_error("Callback not set for op: should_close");
        auto r = cb_should_close();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "request_close")
    {
        if (!cb_request_close) throw std::runtime_error("Callback not set for op: request_close");
        cb_request_close();
        return out;
    }
    
    else if (op == "poll_events")
    {
        if (!cb_poll_events) throw std::runtime_error("Callback not set for op: poll_events");
        cb_poll_events();
        return out;
    }
    
    else if (op == "fb_width")
    {
        if (!cb_fb_width) throw std::runtime_error("Callback not set for op: fb_width");
        auto r = cb_fb_width();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "fb_height")
    {
        if (!cb_fb_height) throw std::runtime_error("Callback not set for op: fb_height");
        auto r = cb_fb_height();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "display_width")
    {
        if (!cb_display_width) throw std::runtime_error("Callback not set for op: display_width");
        auto r = cb_display_width();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "display_height")
    {
        if (!cb_display_height) throw std::runtime_error("Callback not set for op: display_height");
        auto r = cb_display_height();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "resize_framebuffer")
    {
        const int w = get_int(arr, 2, 0, true);
        const int h = get_int(arr, 3, 0, true);
        if (!cb_resize_framebuffer) throw std::runtime_error("Callback not set for op: resize_framebuffer");
        cb_resize_framebuffer(w, h);
        return out;
    }
    
    else if (op == "enable_depth")
    {
        const bool enabled = get_bool(arr, 2, false, true);
        if (!cb_enable_depth) throw std::runtime_error("Callback not set for op: enable_depth");
        cb_enable_depth(enabled);
        return out;
    }
    
    else if (op == "depth_enabled")
    {
        if (!cb_depth_enabled) throw std::runtime_error("Callback not set for op: depth_enabled");
        auto r = cb_depth_enabled();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "set_blend_mode")
    {
        const Engine_::BlendMode mode = get_blend_mode(arr, 2, Engine_::BlendMode::Overwrite, true);
        if (!cb_set_blend_mode) throw std::runtime_error("Callback not set for op: set_blend_mode");
        cb_set_blend_mode(mode);
        return out;
    }
    
    else if (op == "blend_mode")
    {
        if (!cb_blend_mode) throw std::runtime_error("Callback not set for op: blend_mode");
        auto r = cb_blend_mode();
        out.push_back(sol::make_object(lua, std::string(blend_mode_to_cstr(r))));
        return out;
    }
    
    else if (op == "set_clip_rect")
    {
        const int x = get_int(arr, 2, 0, true);
        const int y = get_int(arr, 3, 0, true);
        const int w = get_int(arr, 4, 0, true);
        const int h = get_int(arr, 5, 0, true);
        if (!cb_set_clip_rect) throw std::runtime_error("Callback not set for op: set_clip_rect");
        cb_set_clip_rect(x, y, w, h);
        return out;
    }
    
    else if (op == "disable_clip_rect")
    {
        if (!cb_disable_clip_rect) throw std::runtime_error("Callback not set for op: disable_clip_rect");
        cb_disable_clip_rect();
        return out;
    }
    
    else if (op == "clear_color")
    {
        const Engine_::Color c = get_color(arr, 2, Engine_::Color{0,0,0,255}, true);
        if (!cb_clear_color) throw std::runtime_error("Callback not set for op: clear_color");
        cb_clear_color(c);
        return out;
    }
    
    else if (op == "clear_depth")
    {
        const float z = get_float(arr, 2, 1.0f, false);
        if (!cb_clear_depth) throw std::runtime_error("Callback not set for op: clear_depth");
        cb_clear_depth(z);
        return out;
    }
    
    else if (op == "set_present_filter_linear")
    {
        const bool linear = get_bool(arr, 2, false, true);
        if (!cb_set_present_filter_linear) throw std::runtime_error("Callback not set for op: set_present_filter_linear");
        cb_set_present_filter_linear(linear);
        return out;
    }
    
    else if (op == "flush_to_screen")
    {
        const bool apply_postprocess = get_bool(arr, 2, true, false);
        if (!cb_flush_to_screen) throw std::runtime_error("Callback not set for op: flush_to_screen");
        cb_flush_to_screen(apply_postprocess);
        return out;
    }
    
    else if (op == "set_capture_filepath")
    {
        const std::string filepath = get_string(arr, 2, std::string{}, true);
        if (!cb_set_capture_filepath) throw std::runtime_error("Callback not set for op: set_capture_filepath");
        cb_set_capture_filepath(filepath);
        return out;
    }
    
    else if (op == "set_frame_index")
    {
        const uint64_t idx = get_u64(arr, 2, 0, true);
        if (!cb_set_frame_index) throw std::runtime_error("Callback not set for op: set_frame_index");
        cb_set_frame_index(idx);
        return out;
    }
    
    else if (op == "frame_index")
    {
        if (!cb_frame_index) throw std::runtime_error("Callback not set for op: frame_index");
        auto r = cb_frame_index();
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "next_frame")
    {
        if (!cb_next_frame) throw std::runtime_error("Callback not set for op: next_frame");
        cb_next_frame();
        return out;
    }
    
    else if (op == "save_frame_png")
    {
        const bool apply_postprocess = get_bool(arr, 2, true, false);
        if (!cb_save_frame_png) throw std::runtime_error("Callback not set for op: save_frame_png");
        cb_save_frame_png(apply_postprocess);
        return out;
    }
    
    else if (op == "set_pixel")
    {
        const int x = get_int(arr, 2, 0, true);
        const int y = get_int(arr, 3, 0, true);
        const Engine_::Color c = get_color(arr, 4, Engine_::Color{0,0,0,255}, true);
        if (!cb_set_pixel) throw std::runtime_error("Callback not set for op: set_pixel");
        cb_set_pixel(x, y, c);
        return out;
    }
    
    else if (op == "get_pixel")
    {
        const int x = get_int(arr, 2, 0, true);
        const int y = get_int(arr, 3, 0, true);
        if (!cb_get_pixel) throw std::runtime_error("Callback not set for op: get_pixel");
        auto r = cb_get_pixel(x, y);
        out.push_back(sol::make_object(lua, color_to_table(lua, r)));
        return out;
    }
    
    else if (op == "draw_line")
    {
        const int x0 = get_int(arr, 2, 0, true);
        const int y0 = get_int(arr, 3, 0, true);
        const int x1 = get_int(arr, 4, 0, true);
        const int y1 = get_int(arr, 5, 0, true);
        const Engine_::Color c = get_color(arr, 6, Engine_::Color{0,0,0,255}, true);
        const int thickness = get_int(arr, 7, 1, false);
        if (!cb_draw_line) throw std::runtime_error("Callback not set for op: draw_line");
        cb_draw_line(x0, y0, x1, y1, c, thickness);
        return out;
    }
    
    else if (op == "draw_rect")
    {
        const int x = get_int(arr, 2, 0, true);
        const int y = get_int(arr, 3, 0, true);
        const int w = get_int(arr, 4, 0, true);
        const int h = get_int(arr, 5, 0, true);
        const Engine_::Color c = get_color(arr, 6, Engine_::Color{0,0,0,255}, true);
        const bool filled = get_bool(arr, 7, true, false);
        const int thickness = get_int(arr, 8, 1, false);
        if (!cb_draw_rect) throw std::runtime_error("Callback not set for op: draw_rect");
        cb_draw_rect(x, y, w, h, c, filled, thickness);
        return out;
    }
    
    else if (op == "draw_circle")
    {
        const int cx = get_int(arr, 2, 0, true);
        const int cy = get_int(arr, 3, 0, true);
        const int radius = get_int(arr, 4, 0, true);
        const Engine_::Color c = get_color(arr, 5, Engine_::Color{0,0,0,255}, true);
        const bool filled = get_bool(arr, 6, true, false);
        const int thickness = get_int(arr, 7, 1, false);
        if (!cb_draw_circle) throw std::runtime_error("Callback not set for op: draw_circle");
        cb_draw_circle(cx, cy, radius, c, filled, thickness);
        return out;
    }
    
    else if (op == "draw_triangle_outline")
    {
        const Engine_::Vec2 a = get_vec2(arr, 2, Engine_::Vec2{0,0}, true);
        const Engine_::Vec2 b = get_vec2(arr, 3, Engine_::Vec2{0,0}, true);
        const Engine_::Vec2 c = get_vec2(arr, 4, Engine_::Vec2{0,0}, true);
        const Engine_::Color col = get_color(arr, 5, Engine_::Color{0,0,0,255}, true);
        const int thickness = get_int(arr, 6, 1, false);
        if (!cb_draw_triangle_outline) throw std::runtime_error("Callback not set for op: draw_triangle_outline");
        cb_draw_triangle_outline(a, b, c, col, thickness);
        return out;
    }
    
    else if (op == "draw_triangle_filled")
    {
        const Engine_::Vec2 a = get_vec2(arr, 2, Engine_::Vec2{0,0}, true);
        const Engine_::Vec2 b = get_vec2(arr, 3, Engine_::Vec2{0,0}, true);
        const Engine_::Vec2 c = get_vec2(arr, 4, Engine_::Vec2{0,0}, true);
        const Engine_::Color col = get_color(arr, 5, Engine_::Color{0,0,0,255}, true);
        if (!cb_draw_triangle_filled) throw std::runtime_error("Callback not set for op: draw_triangle_filled");
        cb_draw_triangle_filled(a, b, c, col);
        return out;
    }
    
    else if (op == "draw_triangle_filled_grad")
    {
        const Engine_::Vec2 a = get_vec2(arr, 2, Engine_::Vec2{0,0}, true);
        const Engine_::Color ca = get_color(arr, 3, Engine_::Color{0,0,0,255}, true);
        const Engine_::Vec2 b = get_vec2(arr, 4, Engine_::Vec2{0,0}, true);
        const Engine_::Color cb = get_color(arr, 5, Engine_::Color{0,0,0,255}, true);
        const Engine_::Vec2 c = get_vec2(arr, 6, Engine_::Vec2{0,0}, true);
        const Engine_::Color cc = get_color(arr, 7, Engine_::Color{0,0,0,255}, true);
        if (!cb_draw_triangle_filled_grad) throw std::runtime_error("Callback not set for op: draw_triangle_filled_grad");
        cb_draw_triangle_filled_grad(a, ca, b, cb, c, cc);
        return out;
    }
    
    else if (op == "draw_triangle_textured_named")
    {
        const Engine_::Vec2 a = get_vec2(arr, 2, Engine_::Vec2{0,0}, true);
        const Engine_::Vec2 ua = get_vec2(arr, 3, Engine_::Vec2{0,0}, true);
        const Engine_::Vec2 b = get_vec2(arr, 4, Engine_::Vec2{0,0}, true);
        const Engine_::Vec2 ub = get_vec2(arr, 5, Engine_::Vec2{0,0}, true);
        const Engine_::Vec2 c = get_vec2(arr, 6, Engine_::Vec2{0,0}, true);
        const Engine_::Vec2 uc = get_vec2(arr, 7, Engine_::Vec2{0,0}, true);
        const std::string texture_name = get_string(arr, 8, std::string{}, true);
        const Engine_::Color tint = get_color(arr, 9, Engine_::Color{255,255,255,255}, false);
        if (!cb_draw_triangle_textured_named) throw std::runtime_error("Callback not set for op: draw_triangle_textured_named");
        cb_draw_triangle_textured_named(a, ua, b, ub, c, uc, texture_name, tint);
        return out;
    }
    
    else if (op == "mat4_identity")
    {
        if (!cb_mat4_identity) throw std::runtime_error("Callback not set for op: mat4_identity");
        auto r = cb_mat4_identity();
        out.push_back(sol::make_object(lua, mat4_to_table(lua, r)));
        return out;
    }
    
    else if (op == "mat4_mul")
    {
        const Engine_::Mat4 a = get_mat4(arr, 2, Engine_::mat4_identity(), true);
        const Engine_::Mat4 b = get_mat4(arr, 3, Engine_::mat4_identity(), true);
        if (!cb_mat4_mul) throw std::runtime_error("Callback not set for op: mat4_mul");
        auto r = cb_mat4_mul(a, b);
        out.push_back(sol::make_object(lua, mat4_to_table(lua, r)));
        return out;
    }
    
    else if (op == "mat4_translate")
    {
        const Engine_::Vec3 t = get_vec3(arr, 2, Engine_::Vec3{0,0,0}, true);
        if (!cb_mat4_translate) throw std::runtime_error("Callback not set for op: mat4_translate");
        auto r = cb_mat4_translate(t);
        out.push_back(sol::make_object(lua, mat4_to_table(lua, r)));
        return out;
    }
    
    else if (op == "mat4_rotate_x")
    {
        const float radians = get_float(arr, 2, 0.0f, true);
        if (!cb_mat4_rotate_x) throw std::runtime_error("Callback not set for op: mat4_rotate_x");
        auto r = cb_mat4_rotate_x(radians);
        out.push_back(sol::make_object(lua, mat4_to_table(lua, r)));
        return out;
    }
    
    else if (op == "mat4_rotate_y")
    {
        const float radians = get_float(arr, 2, 0.0f, true);
        if (!cb_mat4_rotate_y) throw std::runtime_error("Callback not set for op: mat4_rotate_y");
        auto r = cb_mat4_rotate_y(radians);
        out.push_back(sol::make_object(lua, mat4_to_table(lua, r)));
        return out;
    }
    
    else if (op == "mat4_rotate_z")
    {
        const float radians = get_float(arr, 2, 0.0f, true);
        if (!cb_mat4_rotate_z) throw std::runtime_error("Callback not set for op: mat4_rotate_z");
        auto r = cb_mat4_rotate_z(radians);
        out.push_back(sol::make_object(lua, mat4_to_table(lua, r)));
        return out;
    }
    
    else if (op == "mat4_perspective")
    {
        const float fovy_radians = get_float(arr, 2, 0.0f, true);
        const float aspect = get_float(arr, 3, 0.0f, true);
        const float znear = get_float(arr, 4, 0.0f, true);
        const float zfar = get_float(arr, 5, 0.0f, true);
        if (!cb_mat4_perspective) throw std::runtime_error("Callback not set for op: mat4_perspective");
        auto r = cb_mat4_perspective(fovy_radians, aspect, znear, zfar);
        out.push_back(sol::make_object(lua, mat4_to_table(lua, r)));
        return out;
    }
    
    else if (op == "mat4_look_at")
    {
        const Engine_::Vec3 eye = get_vec3(arr, 2, Engine_::Vec3{0,0,0}, true);
        const Engine_::Vec3 center = get_vec3(arr, 3, Engine_::Vec3{0,0,0}, true);
        const Engine_::Vec3 up = get_vec3(arr, 4, Engine_::Vec3{0,0,0}, true);
        if (!cb_mat4_look_at) throw std::runtime_error("Callback not set for op: mat4_look_at");
        auto r = cb_mat4_look_at(eye, center, up);
        out.push_back(sol::make_object(lua, mat4_to_table(lua, r)));
        return out;
    }
    
    else if (op == "tex_make_checker")
    {
        const std::string name = get_string(arr, 2, std::string{}, true);
        const int w = get_int(arr, 3, 256, false);
        const int h = get_int(arr, 4, 256, false);
        const int cell = get_int(arr, 5, 16, false);
        if (!cb_tex_make_checker) throw std::runtime_error("Callback not set for op: tex_make_checker");
        auto r = cb_tex_make_checker(name, w, h, cell);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "tex_load")
    {
        const std::string name = get_string(arr, 2, std::string{}, true);
        const std::string filepath = get_string(arr, 3, std::string{}, true);
        if (!cb_tex_load) throw std::runtime_error("Callback not set for op: tex_load");
        auto r = cb_tex_load(name, filepath);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "tex_delete")
    {
        const std::string name = get_string(arr, 2, std::string{}, true);
        if (!cb_tex_delete) throw std::runtime_error("Callback not set for op: tex_delete");
        auto r = cb_tex_delete(name);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "tex_exists")
    {
        const std::string name = get_string(arr, 2, std::string{}, true);
        if (!cb_tex_exists) throw std::runtime_error("Callback not set for op: tex_exists");
        auto r = cb_tex_exists(name);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "tex_from_framebuffer")
    {
        const std::string name = get_string(arr, 2, std::string{}, true);
        if (!cb_tex_from_framebuffer) throw std::runtime_error("Callback not set for op: tex_from_framebuffer");
        auto r = cb_tex_from_framebuffer(name);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mesh_make_cube")
    {
        const std::string name = get_string(arr, 2, std::string{}, true);
        const float size = get_float(arr, 3, 1.0f, false);
        if (!cb_mesh_make_cube) throw std::runtime_error("Callback not set for op: mesh_make_cube");
        auto r = cb_mesh_make_cube(name, size);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mesh_delete")
    {
        const std::string name = get_string(arr, 2, std::string{}, true);
        if (!cb_mesh_delete) throw std::runtime_error("Callback not set for op: mesh_delete");
        auto r = cb_mesh_delete(name);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "mesh_exists")
    {
        const std::string name = get_string(arr, 2, std::string{}, true);
        if (!cb_mesh_exists) throw std::runtime_error("Callback not set for op: mesh_exists");
        auto r = cb_mesh_exists(name);
        out.push_back(sol::make_object(lua, r));
        return out;
    }
    
    else if (op == "draw_mesh_named")
    {
        const std::string mesh_name = get_string(arr, 2, std::string{}, true);
        const Engine_::Mat4 mvp = get_mat4(arr, 3, Engine_::mat4_identity(), true);
        const std::string texture_name = get_string(arr, 4, std::string{}, false);
        const bool enable_depth_test = get_bool(arr, 5, true, false);
        if (!cb_draw_mesh_named) throw std::runtime_error("Callback not set for op: draw_mesh_named");
        cb_draw_mesh_named(mesh_name, mvp, texture_name, enable_depth_test);
        return out;
    }
    
    else if (op == "pp_set_bloom")
    {
        const bool enabled = get_bool(arr, 2, true, false);
        const float threshold = get_float(arr, 3, 0.75f, false);
        const float intensity = get_float(arr, 4, 1.25f, false);
        const int downsample = get_int(arr, 5, 4, false);
        const float sigma = get_float(arr, 6, 6.0f, false);
        if (!cb_pp_set_bloom) throw std::runtime_error("Callback not set for op: pp_set_bloom");
        cb_pp_set_bloom(enabled, threshold, intensity, downsample, sigma);
        return out;
    }
    
    else if (op == "pp_set_tone")
    {
        const bool enabled = get_bool(arr, 2, true, false);
        const float exposure = get_float(arr, 3, 1.25f, false);
        const float gamma = get_float(arr, 4, 2.2f, false);
        if (!cb_pp_set_tone) throw std::runtime_error("Callback not set for op: pp_set_tone");
        cb_pp_set_tone(enabled, exposure, gamma);
        return out;
    }
    
    else if (op == "pp_reset")
    {
        if (!cb_pp_reset) throw std::runtime_error("Callback not set for op: pp_reset");
        cb_pp_reset();
        return out;
    }
    
    else
    {
        throw std::runtime_error(std::string("Unknown op: ") + op);
    }
    return out;
}

// Convenience: register a single function into Lua.
// Example: EngineLuaBridge_::register_into(lua, "LuaEngine_"); then in Lua: LuaEngine_({"get_pixel", 10, 20})
inline void register_into(sol::state_view lua, const char* fn_name = "LuaEngine_")
{
    lua[fn_name] = &EngineLuaBridge_::dispatch;
}

} // namespace
