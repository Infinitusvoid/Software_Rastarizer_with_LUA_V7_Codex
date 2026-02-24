-- gfx.lua
-- Extended safe wrapper layer over LuaEngine_ command bridge.
-- Keeps current API compatible and adds input / PP / texture / mesh / matrix helpers.

return function(cmd)
    local gfx = {}

    print("gfx init (extended)")

    -- ============================================================
    -- Private helpers
    -- ============================================================

    local function clamp(value, minv, maxv)
        return math.min(math.max(minv, value), maxv)
    end

    local function clamp_0_255(value)
        return clamp(value, 0, 255)
    end

    local function expect_number(v, name, level)
        if type(v) ~= "number" then
            error((name or "value") .. " must be a number", level or 2)
        end
        return v
    end

    local function expect_string(v, name, level)
        if type(v) ~= "string" then
            error((name or "value") .. " must be a string", level or 2)
        end
        return v
    end

    local function expect_bool(v, name, level)
        if type(v) ~= "boolean" then
            error((name or "value") .. " must be a boolean", level or 2)
        end
        return v
    end

    local function expect_table(v, name, level)
        if type(v) ~= "table" then
            error((name or "value") .. " must be a table", level or 2)
        end
        return v
    end

    local function iround(v, name, level)
        v = expect_number(v, name, (level or 2) + 1)
        return math.floor(v + 0.5)
    end

    local function safe_color(color, level)
        expect_table(color, "color", (level or 2) + 1)

        local r = (color.r == nil) and 255 or expect_number(color.r, "color.r", (level or 2) + 1)
        local g = (color.g == nil) and 255 or expect_number(color.g, "color.g", (level or 2) + 1)
        local b = (color.b == nil) and 255 or expect_number(color.b, "color.b", (level or 2) + 1)
        local a = (color.a == nil) and 255 or expect_number(color.a, "color.a", (level or 2) + 1)

        r = clamp_0_255(math.floor(r + 0.5))
        g = clamp_0_255(math.floor(g + 0.5))
        b = clamp_0_255(math.floor(b + 0.5))
        a = clamp_0_255(math.floor(a + 0.5))

        return { r = r, g = g, b = b, a = a }
    end

    local function safe_vec2(v, name, level)
        expect_table(v, name or "vec2", (level or 2) + 1)
        local x = expect_number(v.x, (name or "vec2") .. ".x", (level or 2) + 1)
        local y = expect_number(v.y, (name or "vec2") .. ".y", (level or 2) + 1)
        return { x = x, y = y }
    end

    local function safe_vec3(v, name, level)
        expect_table(v, name or "vec3", (level or 2) + 1)
        local x = expect_number(v.x, (name or "vec3") .. ".x", (level or 2) + 1)
        local y = expect_number(v.y, (name or "vec3") .. ".y", (level or 2) + 1)
        local z = expect_number(v.z, (name or "vec3") .. ".z", (level or 2) + 1)
        return { x = x, y = y, z = z }
    end

    local function make_cmd(op, ...)
        expect_string(op, "op", 3)
        local t = { op }
        local n = select("#", ...)
        for i = 1, n do
            t[#t + 1] = select(i, ...)
        end
        return t
    end

    -- ============================================================
    -- Generic / raw bridge
    -- ============================================================

    -- Raw passthrough with varargs: gfx.raw("draw_line", x0,y0,x1,y1,color,thickness)
    function gfx.raw(op, ...)
        return cmd(make_cmd(op, ...))
    end

    -- Alias some people like:
    gfx.call = gfx.raw
    gfx.cmd  = gfx.raw

    -- ============================================================
    -- Constructors / shared values
    -- ============================================================

    function gfx.color(r, g, b, a)
        return safe_color({ r = r, g = g, b = b, a = (a == nil and 255 or a) }, 2)
    end

    function gfx.v2(x, y)
        return { x = expect_number(x, "x", 2), y = expect_number(y, "y", 2) }
    end

    function gfx.v3(x, y, z)
        return {
            x = expect_number(x, "x", 2),
            y = expect_number(y, "y", 2),
            z = expect_number(z, "z", 2),
        }
    end

    -- ============================================================
    -- Window / input / time
    -- ============================================================

    function gfx.poll_events()
        return cmd({"poll_events"})
    end

    function gfx.key_pressed(keycode)
        keycode = expect_number(keycode, "keycode", 2)
        return cmd({"key_pressed", keycode})
    end

    function gfx.mouse_x()
        return cmd({"mouse_x"})
    end

    function gfx.mouse_y()
        return cmd({"mouse_y"})
    end

    function gfx.mouse_pos()
        return gfx.mouse_x(), gfx.mouse_y()
    end

    function gfx.mouse_prev_x()
        return cmd({"mouse_prev_x"})
    end

    function gfx.mouse_prev_y()
        return cmd({"mouse_prev_y"})
    end

    function gfx.mouse_dx()
        return cmd({"mouse_dx"})
    end

    function gfx.mouse_dy()
        return cmd({"mouse_dy"})
    end

    function gfx.mouse_moved()
        return cmd({"mouse_moved"})
    end

    function gfx.mouse_down(button)
        button = expect_number(button, "button", 2)
        return cmd({"mouse_down", button})
    end

    function gfx.mouse_pressed(button)
        button = expect_number(button, "button", 2)
        return cmd({"mouse_pressed", button})
    end

    function gfx.mouse_released(button)
        button = expect_number(button, "button", 2)
        return cmd({"mouse_released", button})
    end

    function gfx.mouse_scroll_x()
        return cmd({"mouse_scroll_x"})
    end

    function gfx.mouse_scroll_y()
        return cmd({"mouse_scroll_y"})
    end

    function gfx.mouse_scrolled()
        return cmd({"mouse_scrolled"})
    end

    function gfx.mouse_in_window()
        return cmd({"mouse_in_window"})
    end

    function gfx.mouse_entered()
        return cmd({"mouse_entered"})
    end

    function gfx.mouse_left()
        return cmd({"mouse_left"})
    end

    function gfx.mouse_fb_x()
        return cmd({"mouse_fb_x"})
    end

    function gfx.mouse_fb_y()
        return cmd({"mouse_fb_y"})
    end

    function gfx.mouse_fb_ix()
        return cmd({"mouse_fb_ix"})
    end

    function gfx.mouse_fb_iy()
        return cmd({"mouse_fb_iy"})
    end

    function gfx.set_cursor_visible(visible)
        visible = expect_bool(visible, "visible", 2)
        return cmd({"set_cursor_visible", visible})
    end

    function gfx.cursor_visible()
        return cmd({"cursor_visible"})
    end

    function gfx.set_cursor_captured(captured)
        captured = expect_bool(captured, "captured", 2)
        return cmd({"set_cursor_captured", captured})
    end

    function gfx.cursor_captured()
        return cmd({"cursor_captured"})
    end

    function gfx.request_close()
        return cmd({"request_close"})
    end

    function gfx.fb_width()
        return cmd({"fb_width"})
    end

    function gfx.fb_height()
        return cmd({"fb_height"})
    end

    function gfx.fb_size()
        local W = cmd({"fb_width"})
        local H = cmd({"fb_height"})
        return W, H
    end

    function gfx.time_seconds()
        return cmd({"time_seconds"})
    end

    -- ============================================================
    -- Presentation / frame lifecycle
    -- ============================================================

    function gfx.clear(color)
        cmd({"clear_color", safe_color(color, 2)})
    end

    function gfx.clear_depth(z)
        z = expect_number(z, "z", 2)
        cmd({"clear_depth", z})
    end

    function gfx.set_blend_mode(mode)
        mode = expect_string(mode, "mode", 2)
        cmd({"set_blend_mode", mode})
    end

    function gfx.blend_alpha()
        cmd({"set_blend_mode", "Alpha"})
    end

    function gfx.blend_additive()
        cmd({"set_blend_mode", "Additive"})
    end

    function gfx.set_present_filter_linear(enabled)
        enabled = expect_bool(enabled, "enabled", 2)
        cmd({"set_present_filter_linear", enabled})
    end

    function gfx.present(vsync)
        if vsync == nil then vsync = true end
        vsync = expect_bool(vsync, "vsync", 2)
        cmd({"flush_to_screen", vsync})
    end

    function gfx.begin_frame(clearColor)
        gfx.clear(clearColor or gfx.color(0, 0, 0, 255))
        gfx.clear_depth(1.0)
        gfx.blend_alpha()
        gfx.W, gfx.H = gfx.fb_size()
        return gfx.W, gfx.H
    end

    function gfx.end_frame(vsync)
        gfx.present(vsync)
    end

    function gfx.next_frame()
        return cmd({"next_frame"})
    end

    function gfx.save_frame_png(include_alpha)
        if include_alpha == nil then include_alpha = true end
        include_alpha = expect_bool(include_alpha, "include_alpha", 2)
        return cmd({"save_frame_png", include_alpha})
    end

    -- ============================================================
    -- 2D drawing
    -- ============================================================

    function gfx.line(x0, y0, x1, y1, color, thickness)
        local c = safe_color(color, 2)

        local ix0 = iround(x0, "x0", 2)
        local iy0 = iround(y0, "y0", 2)
        local ix1 = iround(x1, "x1", 2)
        local iy1 = iround(y1, "y1", 2)

        thickness = (thickness == nil) and 2 or expect_number(thickness, "thickness", 2)
        thickness = math.max(1, iround(thickness, "thickness", 2))

        cmd({"draw_line", ix0, iy0, ix1, iy1, c, thickness})
    end

    function gfx.rect(x, y, w, h, color, filled, thickness)
        local c = safe_color(color, 2)

        local ix = iround(x, "x", 2)
        local iy = iround(y, "y", 2)
        local iw = iround(w, "w", 2)
        local ih = iround(h, "h", 2)

        if filled == nil then filled = true end
        filled = expect_bool(filled, "filled", 2)

        if filled then
            cmd({"draw_rect", ix, iy, iw, ih, c, true})
        else
            thickness = (thickness == nil) and 1 or expect_number(thickness, "thickness", 2)
            thickness = math.max(1, iround(thickness, "thickness", 2))
            cmd({"draw_rect", ix, iy, iw, ih, c, false, thickness})
        end
    end

    function gfx.circle(cx, cy, r, color, filled, thickness)
        local c = safe_color(color, 2)

        local icx = iround(cx, "cx", 2)
        local icy = iround(cy, "cy", 2)
        local ir  = math.max(0, iround(r, "r", 2))

        if filled == nil then filled = true end
        filled = expect_bool(filled, "filled", 2)

        if filled then
            cmd({"draw_circle", icx, icy, ir, c, true})
        else
            thickness = (thickness == nil) and 1 or expect_number(thickness, "thickness", 2)
            thickness = math.max(1, iround(thickness, "thickness", 2))
            cmd({"draw_circle", icx, icy, ir, c, false, thickness})
        end
    end

    function gfx.tri_filled(v0, v1, v2, color)
        cmd({
            "draw_triangle_filled",
            safe_vec2(v0, "v0", 2),
            safe_vec2(v1, "v1", 2),
            safe_vec2(v2, "v2", 2),
            safe_color(color, 2)
        })
    end

    function gfx.tri_grad(v0, c0, v1, c1, v2, c2)
        cmd({
            "draw_triangle_filled_grad",
            safe_vec2(v0, "v0", 2), safe_color(c0, 2),
            safe_vec2(v1, "v1", 2), safe_color(c1, 2),
            safe_vec2(v2, "v2", 2), safe_color(c2, 2)
        })
    end

    function gfx.tri_textured_named(v0, uv0, v1, uv1, v2, uv2, tex_name, tint)
        tex_name = expect_string(tex_name, "tex_name", 2)
        if tint == nil then tint = gfx.color(255, 255, 255, 255) end

        cmd({
            "draw_triangle_textured_named",
            safe_vec2(v0, "v0", 2), safe_vec2(uv0, "uv0", 2),
            safe_vec2(v1, "v1", 2), safe_vec2(uv1, "uv1", 2),
            safe_vec2(v2, "v2", 2), safe_vec2(uv2, "uv2", 2),
            tex_name,
            safe_color(tint, 2)
        })
    end

    -- ============================================================
    -- Post-processing
    -- ============================================================

    function gfx.pp_set_bloom(enabled, threshold, intensity, blur_passes, radius)
        enabled = expect_bool(enabled, "enabled", 2)
        threshold = expect_number(threshold, "threshold", 2)
        intensity = expect_number(intensity, "intensity", 2)
        blur_passes = (blur_passes == nil) and 4 or expect_number(blur_passes, "blur_passes", 2)
        radius = (radius == nil) and 6.0 or expect_number(radius, "radius", 2)

        cmd({"pp_set_bloom", enabled, threshold, intensity, blur_passes, radius})
    end

    function gfx.pp_set_tone(enabled, exposure, gamma)
        enabled = expect_bool(enabled, "enabled", 2)
        exposure = expect_number(exposure, "exposure", 2)
        gamma = expect_number(gamma, "gamma", 2)
        cmd({"pp_set_tone", enabled, exposure, gamma})
    end

    -- ============================================================
    -- Textures / meshes
    -- ============================================================

    function gfx.tex_exists(name)
        name = expect_string(name, "name", 2)
        return cmd({"tex_exists", name})
    end

    function gfx.tex_make_checker(name, w, h, cell)
        name = expect_string(name, "name", 2)
        w = math.max(1, iround(w, "w", 2))
        h = math.max(1, iround(h, "h", 2))
        cell = math.max(1, iround(cell, "cell", 2))
        return cmd({"tex_make_checker", name, w, h, cell})
    end

    function gfx.tex_from_framebuffer(name)
        name = expect_string(name, "name", 2)
        return cmd({"tex_from_framebuffer", name})
    end

    function gfx.mesh_exists(name)
        name = expect_string(name, "name", 2)
        return cmd({"mesh_exists", name})
    end

    function gfx.mesh_make_cube(name, size)
        name = expect_string(name, "name", 2)
        size = expect_number(size, "size", 2)
        return cmd({"mesh_make_cube", name, size})
    end

    function gfx.draw_mesh_named(mesh_name, mvp, tex_name, depth_test)
        mesh_name = expect_string(mesh_name, "mesh_name", 2)
        expect_table(mvp, "mvp", 2) -- matrix shape validated by engine/bridge
        tex_name = expect_string(tex_name, "tex_name", 2)
        if depth_test == nil then depth_test = true end
        depth_test = expect_bool(depth_test, "depth_test", 2)

        return cmd({"draw_mesh_named", mesh_name, mvp, tex_name, depth_test})
    end

    -- ============================================================
    -- Matrix / camera wrappers (3D)
    -- ============================================================

    function gfx.mat4_mul(a, b)
        expect_table(a, "a", 2)
        expect_table(b, "b", 2)
        return cmd({"mat4_mul", a, b})
    end

    function gfx.mat4_translate(v3)
        return cmd({"mat4_translate", safe_vec3(v3, "v3", 2)})
    end

    function gfx.mat4_rotate_x(rad)
        rad = expect_number(rad, "rad", 2)
        return cmd({"mat4_rotate_x", rad})
    end

    function gfx.mat4_rotate_y(rad)
        rad = expect_number(rad, "rad", 2)
        return cmd({"mat4_rotate_y", rad})
    end

    function gfx.mat4_rotate_z(rad)
        rad = expect_number(rad, "rad", 2)
        return cmd({"mat4_rotate_z", rad})
    end

    function gfx.mat4_look_at(eye, target, up)
        return cmd({
            "mat4_look_at",
            safe_vec3(eye, "eye", 2),
            safe_vec3(target, "target", 2),
            safe_vec3(up, "up", 2)
        })
    end

    function gfx.mat4_perspective(fovy_rad, aspect, znear, zfar)
        fovy_rad = expect_number(fovy_rad, "fovy_rad", 2)
        aspect   = expect_number(aspect, "aspect", 2)
        znear    = expect_number(znear, "znear", 2)
        zfar     = expect_number(zfar, "zfar", 2)
        return cmd({"mat4_perspective", fovy_rad, aspect, znear, zfar})
    end

    -- Single-pixel write (validated wrapper)
    function gfx.set_pixel(x, y, color)
        local ix = iround(x, "x", 2)
        local iy = iround(y, "y", 2)
        local c  = safe_color(color, 2)
        cmd({"set_pixel", ix, iy, c})
    end

    -- Single-pixel read (returns {r,g,b,a})
    function gfx.get_pixel(x, y)
        local ix = iround(x, "x", 2)
        local iy = iround(y, "y", 2)
        return cmd({"get_pixel", ix, iy})
    end

    -- Nice short aliases (optional)
    gfx.pset = gfx.set_pixel
    gfx.pget = gfx.get_pixel

    function gfx.key_down(keycode)
        keycode = expect_number(keycode, "keycode", 2)
        return cmd({"key_down", keycode})
    end

    function gfx.key_released(keycode)
        keycode = expect_number(keycode, "keycode", 2)
        return cmd({"key_released", keycode})
    end

    -- ============================================================
    -- Namespaced views (optional nice ergonomics)
    -- ============================================================

    gfx.mouse = {
    x = gfx.mouse_x,
    y = gfx.mouse_y,
    pos = gfx.mouse_pos,          -- optional returns x,y
    prev_x = gfx.mouse_prev_x,
    prev_y = gfx.mouse_prev_y,
    dx = gfx.mouse_dx,
    dy = gfx.mouse_dy,
    moved = gfx.mouse_moved,
    down = gfx.mouse_down,
    pressed = gfx.mouse_pressed,
    released = gfx.mouse_released,
    scroll_x = gfx.mouse_scroll_x,
    scroll_y = gfx.mouse_scroll_y,
    scrolled = gfx.mouse_scrolled,
    in_window = gfx.mouse_in_window,
    entered = gfx.mouse_entered,
    left = gfx.mouse_left,
    fb_x = gfx.mouse_fb_x,
    fb_y = gfx.mouse_fb_y,
    fb_ix = gfx.mouse_fb_ix,
    fb_iy = gfx.mouse_fb_iy,
    set_cursor_visible = gfx.set_cursor_visible,
    cursor_visible = gfx.cursor_visible,
    set_cursor_captured = gfx.set_cursor_captured,
    cursor_captured = gfx.cursor_captured,
}


    

    gfx.input = {
        poll_events   = gfx.poll_events,
        key_down      = gfx.key_down,
        key_pressed   = gfx.key_pressed,
        key_released  = gfx.key_released,
        request_close = gfx.request_close,
    }

    gfx.frame = {
        begin = gfx.begin_frame,
        ["end"] = gfx.end_frame, -- Lua keyword-safe indexing
        present = gfx.present,
        next = gfx.next_frame,
        save_png = gfx.save_frame_png,
        fb_size = gfx.fb_size,
        fb_width = gfx.fb_width,
        fb_height = gfx.fb_height,
        set_present_filter_linear = gfx.set_present_filter_linear,
    }

    gfx.draw = {
        line = gfx.line,
        rect = gfx.rect,
        circle = gfx.circle,
        tri_filled = gfx.tri_filled,
        tri_grad = gfx.tri_grad,
        tri_textured_named = gfx.tri_textured_named,
    }

    gfx.pp = {
        set_bloom = gfx.pp_set_bloom,
        set_tone = gfx.pp_set_tone,
    }

    gfx.tex = {
        exists = gfx.tex_exists,
        make_checker = gfx.tex_make_checker,
        from_framebuffer = gfx.tex_from_framebuffer,
    }

    gfx.mesh = {
        exists = gfx.mesh_exists,
        make_cube = gfx.mesh_make_cube,
        draw_named = gfx.draw_mesh_named,
    }

    gfx.mat4 = {
        mul = gfx.mat4_mul,
        translate = gfx.mat4_translate,
        rotate_x = gfx.mat4_rotate_x,
        rotate_y = gfx.mat4_rotate_y,
        rotate_z = gfx.mat4_rotate_z,
        look_at = gfx.mat4_look_at,
        perspective = gfx.mat4_perspective,
    }

    gfx.time = {
        seconds = gfx.time_seconds,
    }

    -- Small debug function
    function gfx.run()
        print("gfx.run")
    end

    return gfx
end
