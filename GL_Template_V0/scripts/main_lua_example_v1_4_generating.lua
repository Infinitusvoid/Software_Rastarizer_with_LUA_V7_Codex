-- scripts/main_lua_example_v1_4_generating.lua
--
-- Generates Sandbox.h (C++ header) that implements:
--   - EngineLuaBridge_::dispatch (Lua -> C++)
--   - a list of inline std::function callbacks (C++ sets these)
--   - bind_engine_defaults() that binds callbacks to Engine_::* where possible
--
-- NOTE: ops marked no_default=true are NOT auto-bound (they need custom C++ state),
-- e.g. draw_demo_cube (needs cube mesh) and draw_triangle_textured_named (needs texture lookup).

local U = {}

local function Builder()
  local b = { buf = {}, indent = "" }
  function b:push(s) self.buf[#self.buf+1] = s end
  function b:ln(s) self.buf[#self.buf+1] = (s or "") .. "\n" end
  function b:nl() self.buf[#self.buf+1] = "\n" end
  function b:to_string() return table.concat(self.buf) end
  return b
end

local function enhance_builder(b)
  local old_push = b.push
  function b:push(s)
    if s == nil then return end
    old_push(self, s)
  end
  function b:iln(s) b:ln((b.indent or "") .. (s or "")) end
  function b:tab() b.indent = (b.indent or "") .. "    " end
  function b:untab()
    local s = b.indent or ""
    b.indent = s:sub(1, math.max(0, #s - 4))
  end
  function b:block(s)
    s = s or ""
    b:push(s)
    if #s == 0 or s:sub(-1) ~= "\n" then b:push("\n") end
  end
end

local function cpp_q(s)
  s = tostring(s)
  s = s:gsub("\\", "\\\\")
  s = s:gsub("\"", "\\\"")
  s = s:gsub("\r", "\\r")
  s = s:gsub("\n", "\\n")
  s = s:gsub("\t", "\\t")
  s = s:gsub("[%z\1-\31]", function(c) return string.format("\\x%02X", c:byte()) end)
  return "\"" .. s .. "\""
end

local function write_file(path, data)
  local f = assert(io.open(path, "wb"))
  f:write(data)
  f:close()
end

-- ------------------------------------------------------------
-- Ops spec
-- ------------------------------------------------------------

local OPS = {
  -- --- loop / input queries ---
  { name="time_seconds",  callback_name="cb_time_seconds",  ret="double", args={} },
  { name="delta_seconds", callback_name="cb_delta_seconds", ret="double", args={} },

  { name="key_down",     callback_name="cb_key_down",     ret="bool", args={ {"int","key"} } },
  { name="key_pressed",  callback_name="cb_key_pressed",  ret="bool", args={ {"int","key"} } },
  { name="key_released", callback_name="cb_key_released", ret="bool", args={ {"int","key"} } },

  { name="mouse_x",        callback_name="cb_mouse_x",        ret="double", args={} },
  { name="mouse_y",        callback_name="cb_mouse_y",        ret="double", args={} },
  { name="mouse_prev_x",   callback_name="cb_mouse_prev_x",   ret="double", args={} },
  { name="mouse_prev_y",   callback_name="cb_mouse_prev_y",   ret="double", args={} },
  { name="mouse_dx",       callback_name="cb_mouse_dx",       ret="double", args={} },
  { name="mouse_dy",       callback_name="cb_mouse_dy",       ret="double", args={} },
  { name="mouse_moved",    callback_name="cb_mouse_moved",    ret="bool",   args={} },

  { name="mouse_down",     callback_name="cb_mouse_down",     ret="bool", args={ {"int","button"} } },
  { name="mouse_pressed",  callback_name="cb_mouse_pressed",  ret="bool", args={ {"int","button"} } },
  { name="mouse_released", callback_name="cb_mouse_released", ret="bool", args={ {"int","button"} } },

  { name="mouse_scroll_x", callback_name="cb_mouse_scroll_x", ret="double", args={} },
  { name="mouse_scroll_y", callback_name="cb_mouse_scroll_y", ret="double", args={} },
  { name="mouse_scrolled", callback_name="cb_mouse_scrolled", ret="bool",   args={} },

  { name="mouse_in_window", callback_name="cb_mouse_in_window", ret="bool", args={} },
  { name="mouse_entered",   callback_name="cb_mouse_entered",   ret="bool", args={} },
  { name="mouse_left",      callback_name="cb_mouse_left",      ret="bool", args={} },

  { name="mouse_fb_x",   callback_name="cb_mouse_fb_x",   ret="double", args={} },
  { name="mouse_fb_y",   callback_name="cb_mouse_fb_y",   ret="double", args={} },
  { name="mouse_fb_ix",  callback_name="cb_mouse_fb_ix",  ret="int",    args={} },
  { name="mouse_fb_iy",  callback_name="cb_mouse_fb_iy",  ret="int",    args={} },

  { name="set_cursor_visible",  callback_name="cb_set_cursor_visible",  ret=nil,    args={ {"bool","visible"} } },
  { name="cursor_visible",      callback_name="cb_cursor_visible",      ret="bool", args={} },
  { name="set_cursor_captured", callback_name="cb_set_cursor_captured", ret=nil,    args={ {"bool","captured"} } },
  { name="cursor_captured",     callback_name="cb_cursor_captured",     ret="bool", args={} },

  { name="should_close",  callback_name="cb_should_close",  ret="bool", args={} },
  { name="request_close", callback_name="cb_request_close", ret=nil,    args={} },
  { name="poll_events",   callback_name="cb_poll_events",   ret=nil,    args={} },

  -- --- framebuffer / state ---
  { name="fb_width",       callback_name="cb_fb_width",       ret="int",  args={} },
  { name="fb_height",      callback_name="cb_fb_height",      ret="int",  args={} },
  { name="display_width",  callback_name="cb_display_width",  ret="int",  args={} },
  { name="display_height", callback_name="cb_display_height", ret="int",  args={} },

  { name="resize_framebuffer", callback_name="cb_resize_framebuffer", ret=nil, args={ {"int","w"}, {"int","h"} } },

  { name="enable_depth",  callback_name="cb_enable_depth",  ret=nil,   args={ {"bool","enabled"} } },
  { name="depth_enabled", callback_name="cb_depth_enabled", ret="bool", args={} },

  { name="set_blend_mode", callback_name="cb_set_blend_mode", ret=nil, args={ {"BlendMode","mode"} } },
  { name="blend_mode",     callback_name="cb_blend_mode",     ret="BlendMode", args={} },

  { name="set_clip_rect",     callback_name="cb_set_clip_rect",     ret=nil, args={ {"int","x"}, {"int","y"}, {"int","w"}, {"int","h"} } },
  { name="disable_clip_rect", callback_name="cb_disable_clip_rect", ret=nil, args={} },

  { name="clear_color", callback_name="cb_clear_color", ret=nil, args={ {"Color","c"} } },
  { name="clear_depth", callback_name="cb_clear_depth", ret=nil, args={ {"float","z",{def="1.0f"}} } },

  { name="set_present_filter_linear", callback_name="cb_set_present_filter_linear", ret=nil, args={ {"bool","linear"} } },
  { name="flush_to_screen",            callback_name="cb_flush_to_screen",            ret=nil, args={ {"bool","apply_postprocess",{def="true"}} } },

  -- --- capture ---
  { name="set_capture_filepath", callback_name="cb_set_capture_filepath", ret=nil, args={ {"string","filepath"} } },
  { name="set_frame_index",      callback_name="cb_set_frame_index",      ret=nil, args={ {"u64","idx"} } },
  { name="frame_index",          callback_name="cb_frame_index",          ret="u64", args={} },
  { name="next_frame",           callback_name="cb_next_frame",           ret=nil, args={} },
  { name="save_frame_png",       callback_name="cb_save_frame_png",       ret=nil, args={ {"bool","apply_postprocess",{def="true"}} } },

  -- --- 2D primitives ---
  { name="set_pixel", callback_name="cb_set_pixel", ret=nil, args={ {"int","x"}, {"int","y"}, {"Color","c"} } },
  { name="get_pixel", callback_name="cb_get_pixel", ret="Color", args={ {"int","x"}, {"int","y"} } },

  { name="draw_line", callback_name="cb_draw_line", ret=nil, args={
      {"int","x0"}, {"int","y0"}, {"int","x1"}, {"int","y1"},
      {"Color","c"},
      {"int","thickness",{def="1"}}
    }
  },

  { name="draw_rect", callback_name="cb_draw_rect", ret=nil, args={
      {"int","x"}, {"int","y"}, {"int","w"}, {"int","h"},
      {"Color","c"},
      {"bool","filled",{def="true"}},
      {"int","thickness",{def="1"}}
    }
  },

  { name="draw_circle", callback_name="cb_draw_circle", ret=nil, args={
      {"int","cx"}, {"int","cy"}, {"int","radius"},
      {"Color","c"},
      {"bool","filled",{def="true"}},
      {"int","thickness",{def="1"}}
    }
  },

  { name="draw_triangle_outline", callback_name="cb_draw_triangle_outline", ret=nil, args={
      {"Vec2","a"}, {"Vec2","b"}, {"Vec2","c"},
      {"Color","col"},
      {"int","thickness",{def="1"}}
    }
  },

  { name="draw_triangle_filled", callback_name="cb_draw_triangle_filled", ret=nil, args={
      {"Vec2","a"}, {"Vec2","b"}, {"Vec2","c"},
      {"Color","col"}
    }
  },

  { name="draw_triangle_filled_grad", callback_name="cb_draw_triangle_filled_grad", ret=nil, args={
      {"Vec2","a"}, {"Color","ca"},
      {"Vec2","b"}, {"Color","cb"},
      {"Vec2","c"}, {"Color","cc"},
    }
  },

  -- Textured triangle (named texture; C++ resolves name -> Engine_::Image)
  { name="draw_triangle_textured_named", callback_name="cb_draw_triangle_textured_named", ret=nil, no_default=true, args={
      {"Vec2","a"}, {"Vec2","ua"},
      {"Vec2","b"}, {"Vec2","ub"},
      {"Vec2","c"}, {"Vec2","uc"},
      {"string","texture_name"},
      {"Color","tint",{def="Engine_::Color{255,255,255,255}"}},
    }
  },

  -- --- Mat4 / 3D helpers ---
  { name="mat4_identity",     callback_name="cb_mat4_identity",     ret="Mat4", args={} },
  { name="mat4_mul",          callback_name="cb_mat4_mul",          ret="Mat4", args={ {"Mat4","a"}, {"Mat4","b"} } },
  { name="mat4_translate",    callback_name="cb_mat4_translate",    ret="Mat4", args={ {"Vec3","t"} } },
  { name="mat4_rotate_x",     callback_name="cb_mat4_rotate_x",     ret="Mat4", args={ {"float","radians"} } },
  { name="mat4_rotate_y",     callback_name="cb_mat4_rotate_y",     ret="Mat4", args={ {"float","radians"} } },
  { name="mat4_rotate_z",     callback_name="cb_mat4_rotate_z",     ret="Mat4", args={ {"float","radians"} } },
  { name="mat4_perspective",  callback_name="cb_mat4_perspective",  ret="Mat4", args={ {"float","fovy_radians"}, {"float","aspect"}, {"float","znear"}, {"float","zfar"} } },
  { name="mat4_look_at",      callback_name="cb_mat4_look_at",      ret="Mat4", args={ {"Vec3","eye"}, {"Vec3","center"}, {"Vec3","up"} } },

  -- --- textures (C++ owned) ---
  { name="tex_make_checker",     callback_name="cb_tex_make_checker",     ret="bool", no_default=true, args={ {"string","name"}, {"int","w",{def="256"}}, {"int","h",{def="256"}}, {"int","cell",{def="16"}} } },
  { name="tex_load",             callback_name="cb_tex_load",             ret="bool", no_default=true, args={ {"string","name"}, {"string","filepath"} } },
  { name="tex_delete",           callback_name="cb_tex_delete",           ret="bool", no_default=true, args={ {"string","name"} } },
  { name="tex_exists",           callback_name="cb_tex_exists",           ret="bool", no_default=true, args={ {"string","name"} } },
  { name="tex_from_framebuffer", callback_name="cb_tex_from_framebuffer", ret="bool", no_default=true, args={ {"string","name"} } },

  -- --- meshes (C++ owned) ---
  { name="mesh_make_cube", callback_name="cb_mesh_make_cube", ret="bool", no_default=true, args={ {"string","name"}, {"float","size",{def="1.0f"}} } },
  { name="mesh_delete",    callback_name="cb_mesh_delete",    ret="bool", no_default=true, args={ {"string","name"} } },
  { name="mesh_exists",    callback_name="cb_mesh_exists",    ret="bool", no_default=true, args={ {"string","name"} } },

  { name="draw_mesh_named", callback_name="cb_draw_mesh_named", ret=nil, no_default=true, args={
      {"string","mesh_name"},
      {"Mat4","mvp"},
      {"string","texture_name",{def="std::string{}"}},
      {"bool","enable_depth_test",{def="true"}},
    }
  },

  -- --- post-process knobs (C++ applies to Engine_::PostProcessSettings) ---
  { name="pp_set_bloom", callback_name="cb_pp_set_bloom", ret=nil, no_default=true, args={
      {"bool","enabled",{def="true"}},
      {"float","threshold",{def="0.75f"}},
      {"float","intensity",{def="1.25f"}},
      {"int","downsample",{def="4"}},
      {"float","sigma",{def="6.0f"}},
    }
  },

  { name="pp_set_tone", callback_name="cb_pp_set_tone", ret=nil, no_default=true, args={
      {"bool","enabled",{def="true"}},
      {"float","exposure",{def="1.25f"}},
      {"float","gamma",{def="2.2f"}},
    }
  },

  { name="pp_reset", callback_name="cb_pp_reset", ret=nil, no_default=true, args={} },
}


-- ------------------------------------------------------------
-- Type mapping helpers
-- ------------------------------------------------------------

local function cpp_type(ty)
  if ty == "int" then return "int" end
  if ty == "float" then return "float" end
  if ty == "double" then return "double" end
  if ty == "bool" then return "bool" end
  if ty == "u64" then return "uint64_t" end
  if ty == "string" then return "std::string" end
  if ty == "Color" then return "Engine_::Color" end
  if ty == "Vec2" then return "Engine_::Vec2" end
  if ty == "Vec3" then return "Engine_::Vec3" end
  if ty == "Vec4" then return "Engine_::Vec4" end
  if ty == "Mat4" then return "Engine_::Mat4" end
  if ty == "BlendMode" then return "Engine_::BlendMode" end
  error("Unknown type: " .. tostring(ty))
end

local function cpp_arg_type(ty)
  if ty == "string" then return "const std::string&" end
  if ty == "Mat4" then return "const Engine_::Mat4&" end
  return cpp_type(ty)
end

local function default_cpp(ty)
  if ty == "int" then return "0" end
  if ty == "float" then return "0.0f" end
  if ty == "double" then return "0.0" end
  if ty == "bool" then return "false" end
  if ty == "u64" then return "0" end
  if ty == "string" then return "std::string{}" end
  if ty == "Color" then return "Engine_::Color{0,0,0,255}" end
  if ty == "Vec2" then return "Engine_::Vec2{0,0}" end
  if ty == "Vec3" then return "Engine_::Vec3{0,0,0}" end
  if ty == "Vec4" then return "Engine_::Vec4{0,0,0,1}" end
  if ty == "Mat4" then return "Engine_::mat4_identity()" end
  if ty == "BlendMode" then return "Engine_::BlendMode::Overwrite" end
  return "/*default?*/"
end

local function decode_fn_name(ty)
  if ty == "int" then return "get_int" end
  if ty == "float" then return "get_float" end
  if ty == "double" then return "get_double" end
  if ty == "bool" then return "get_bool" end
  if ty == "u64" then return "get_u64" end
  if ty == "string" then return "get_string" end
  if ty == "Color" then return "get_color" end
  if ty == "Vec2" then return "get_vec2" end
  if ty == "Vec3" then return "get_vec3" end
  if ty == "Vec4" then return "get_vec4" end
  if ty == "Mat4" then return "get_mat4" end
  if ty == "BlendMode" then return "get_blend_mode" end
  error("No decoder for type: " .. tostring(ty))
end

local function ret_push_kind(ty)
  if ty == "Color" then return "color_table" end
  if ty == "Vec2" or ty == "Vec3" or ty == "Vec4" then return "vec_table" end
  if ty == "Mat4" then return "mat4_table" end
  if ty == "BlendMode" then return "blend_string" end
  return "plain"
end

-- ------------------------------------------------------------
-- Emit: C++ header
-- ------------------------------------------------------------

local function emit_prelude(b, ns)
  b:block([=[
#pragma once

#include "Engine.h"

#include <sol/sol.hpp>
#include <functional>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <algorithm>

]=])
  b:ln("namespace " .. ns)
  b:ln("{")
  b:ln("")
end

-- The helper block is long; keep it as a single literal (this is the same helper set you already use).
-- It includes decoding for: int/float/double/bool/u64/string + Vec2/Vec3/Vec4/Mat4 + Color + BlendMode
local function emit_helpers_cpp(b)
  b:block([=[
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

]=])
end

local function emit_callback_decls(b, ops)
  b:ln("// -------------------------")
  b:ln("// Global callbacks (set these from C++).")
  b:ln("// If a callback is not set, dispatch() will throw.")
  b:ln("// -------------------------")
  b:ln("")
  for _, op in ipairs(ops) do
    local ret = op.ret
    local ret_ty = "void"
    if ret ~= nil and ret ~= "void" then
      ret_ty = cpp_type(ret)
    end

    local arg_parts = {}
    for _, a in ipairs(op.args or {}) do
      local ty, name = a[1], a[2]
      arg_parts[#arg_parts+1] = (cpp_arg_type(ty) .. " " .. name)
    end
    local args_sig = table.concat(arg_parts, ", ")

    b:ln(("inline std::function<%s(%s)> %s;"):format(ret_ty, args_sig, op.callback_name))
  end
  b:ln("")
end

local function emit_bind_defaults(b, ops)
  b:ln("// Optional: bind defaults to Engine_::* functions directly.")
  b:ln("// Call this once after including the generated header.")
  b:ln("// NOTE: ops marked no_default=true are intentionally NOT bound here.")
  b:ln("inline void bind_engine_defaults()")
  b:ln("{")
  b:tab()

  for _, op in ipairs(ops) do
    if not op.no_default then
      local arg_names = {}
      local arg_sig = {}
      for _, a in ipairs(op.args or {}) do
        local ty, name = a[1], a[2]
        arg_sig[#arg_sig+1] = (cpp_arg_type(ty) .. " " .. name)
        arg_names[#arg_names+1] = name
      end
      local sig = table.concat(arg_sig, ", ")
      local call = table.concat(arg_names, ", ")
      local ret = op.ret

      if ret == nil or ret == "void" then
        b:iln(("%s = [](%s){ Engine_::%s(%s); };"):format(op.callback_name, sig, op.name, call))
      else
        b:iln(("%s = [](%s){ return Engine_::%s(%s); };"):format(op.callback_name, sig, op.name, call))
      end
    end
  end

  b:untab()
  b:ln("}")
  b:ln("")
end

local function emit_dispatch(b, ops)
  b:ln("// Execute a single command array immediately.")
  b:ln("// Returns 0 values for void ops, or 1+ values for query ops.")
  b:ln("inline sol::variadic_results dispatch(sol::this_state ts, const sol::table& arr)")
  b:ln("{")
  b:tab()
  b:iln("sol::state_view lua(ts);")
  b:iln("sol::variadic_results out;")
  b:iln("const std::string op = get_op1(arr);")
  b:iln("")

  for i, op in ipairs(ops) do
    local prefix = (i == 1) and "if" or "else if"
    b:iln(("%s (op == %s)"):format(prefix, cpp_q(op.name)))
    b:iln("{")
    b:tab()

    local idx = 2
    local arg_names = {}
    for _, a in ipairs(op.args or {}) do
      local ty, name, opts = a[1], a[2], a[3]
      opts = opts or {}
      local def = opts.def or default_cpp(ty)
      local required
      if opts.required ~= nil then
        required = opts.required and "true" or "false"
      else
        required = (opts.def == nil) and "true" or "false"
      end

      local decl_ty = cpp_type(ty)
      local fn = decode_fn_name(ty)
      b:iln(("const %s %s = %s(arr, %d, %s, %s);"):format(decl_ty, name, fn, idx, def, required))
      arg_names[#arg_names+1] = name
      idx = idx + 1
    end

    b:iln(("if (!%s) throw std::runtime_error(%s);"):format(op.callback_name, cpp_q("Callback not set for op: " .. op.name)))

    local call_args = table.concat(arg_names, ", ")
    local ret = op.ret

    if ret == nil or ret == "void" then
      b:iln(("%s(%s);"):format(op.callback_name, call_args))
      b:iln("return out;")
    else
      b:iln(("auto r = %s(%s);"):format(op.callback_name, call_args))

      local kind = ret_push_kind(ret)
      if kind == "plain" then
        b:iln("out.push_back(sol::make_object(lua, r));")
      elseif kind == "color_table" then
        b:iln("out.push_back(sol::make_object(lua, color_to_table(lua, r)));")
      elseif kind == "vec_table" then
        if ret == "Vec2" then
          b:iln("out.push_back(sol::make_object(lua, vec2_to_table(lua, r)));")
        elseif ret == "Vec3" then
          b:iln("out.push_back(sol::make_object(lua, vec3_to_table(lua, r)));")
        else
          b:iln("out.push_back(sol::make_object(lua, vec4_to_table(lua, r)));")
        end
      elseif kind == "mat4_table" then
        b:iln("out.push_back(sol::make_object(lua, mat4_to_table(lua, r)));")
      elseif kind == "blend_string" then
        b:iln("out.push_back(sol::make_object(lua, std::string(blend_mode_to_cstr(r))));")
      else
        b:iln("out.push_back(sol::make_object(lua, r));")
      end

      b:iln("return out;")
    end

    b:untab()
    b:iln("}")
    b:iln("")
  end

  b:iln("else")
  b:iln("{")
  b:tab()
  b:iln('throw std::runtime_error(std::string("Unknown op: ") + op);')
  b:untab()
  b:iln("}")

  b:iln("return out;")
  b:untab()
  b:ln("}")
  b:ln("")
end

local function emit_register(b, ns)
  b:ln("// Convenience: register a single function into Lua.")
  b:ln("// Example: EngineLuaBridge_::register_into(lua, \"LuaEngine_\"); then in Lua: LuaEngine_({\"get_pixel\", 10, 20})")
  b:ln("inline void register_into(sol::state_view lua, const char* fn_name = \"LuaEngine_\")")
  b:ln("{")
  b:tab()
  b:iln("lua[fn_name] = &" .. ns .. "::dispatch;")
  b:untab()
  b:ln("}")
  b:ln("")
end

local function emit_epilogue(b)
  b:ln("} // namespace")
end

-- Default output path:
-- - If Engine.ScriptsDir is available (set by C++), write ../Sandbox.h relative to scripts dir.
-- - Otherwise write ./Sandbox.h
local function default_out_path()
  local ok, Engine = pcall(function() return _G.Engine end)
  if ok and Engine and Engine.SandboxOutPath then
    return tostring(Engine.SandboxOutPath)
  end
  if ok and Engine and Engine.ScriptsDir then
    local sd = tostring(Engine.ScriptsDir)
    sd = sd:gsub("\\", "/")
    -- crude parent: strip trailing /scripts or last segment
    local parent = sd:gsub("/?$", "")
    parent = parent:gsub("/[^/]+$", "")
    return parent .. "/Sandbox.h"
  end
  return "Sandbox2.h"
end

function U.generate(out_path)
  out_path = out_path or default_out_path()

  local b = Builder()
  enhance_builder(b)

  local ns = "EngineLuaBridge_"

  emit_prelude(b, ns)
  emit_helpers_cpp(b)
  emit_callback_decls(b, OPS)
  emit_bind_defaults(b, OPS)
  emit_dispatch(b, OPS)
  emit_register(b, ns)
  emit_epilogue(b)

  write_file(out_path, b:to_string())
  print("[Lua] wrote: " .. out_path)
end

return U
