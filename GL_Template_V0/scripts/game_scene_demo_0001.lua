-- game_scene_demo_advanced.lua
-- Advanced showcase scene for your Lua + C++ engine.
-- Demonstrates:
--   * hot-reload-friendly persistent scene state
--   * procedural texture baking via framebuffer capture
--   * 2D primitives (lines/rects/circles/triangles)
--   * textured triangles as quads
--   * 3D rotating cube gallery (multiple instances)
--   * post-processing (bloom/tone)
--   * present filter toggle
--   * pixel-level effects using set_pixel / get_pixel
--
-- Controls (safe-checked; missing keys won't crash):
--   ESC         = quit
--   1           = grid layer
--   2           = neon strokes layer
--   3           = triangle gallery layer
--   4           = textured quads layer
--   5           = 3D cube gallery layer
--   6           = pixel lab layer (set_pixel/get_pixel)
--   B           = bloom on/off
--   T           = tone map on/off
--   F           = present filter linear on/off
--   [ / ]       = bloom threshold -/+
--   - / =       = bloom intensity -/+
--   R           = regenerate procedural baked textures
--   SPACE       = pause / resume time
--   S           = save current frame PNG
--
-- Notes:
-- * This scene avoids assuming text rendering exists. "HUD" is visual only + console logs.
-- * Uses only APIs visible in the provided gfx.lua wrapper.

return function(ctx)
  assert(type(ctx) == "table", "game_scene_demo_advanced: ctx table required")
  assert(ctx.Engine, "game_scene_demo_advanced: ctx.Engine required")
  assert(ctx.state,  "game_scene_demo_advanced: ctx.state required")
  assert(ctx.gfx,    "game_scene_demo_advanced: ctx.gfx required")

  local Engine = ctx.Engine
  local state  = ctx.state
  local gfx    = ctx.gfx
  local C      = gfx.color

  local Scene = {}

  ---------------------------------------------------------------------------
  -- Small local helpers
  ---------------------------------------------------------------------------

  local TAU = math.pi * 2.0

  local function clamp(x, a, b)
    if x < a then return a end
    if x > b then return b end
    return x
  end

  local function lerp(a, b, t)
    return a + (b - a) * t
  end

  local function smooth01(t)
    t = clamp(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)
  end

  local function fract(x)
    return x - math.floor(x)
  end

  local function hash11(x)
    return fract(math.sin(x * 127.1 + 311.7) * 43758.5453123)
  end

  local function hash21(x, y)
    return fract(math.sin(x * 127.1 + y * 311.7 + 74.7) * 43758.5453123)
  end

  local function palette_neon(t)
    -- Nice neon-ish palette (0..1 -> color)
    local r = 0.5 + 0.5 * math.sin(TAU * (t + 0.00))
    local g = 0.5 + 0.5 * math.sin(TAU * (t + 0.33))
    local b = 0.5 + 0.5 * math.sin(TAU * (t + 0.67))
    return C(r * 255, g * 255, b * 255, 255)
  end

  local function col_lerp(ca, cb, t)
    return C(
      lerp(ca.r, cb.r, t),
      lerp(ca.g, cb.g, t),
      lerp(ca.b, cb.b, t),
      lerp(ca.a or 255, cb.a or 255, t)
    )
  end

  local function key_pressed_safe(keycode)
    if type(keycode) ~= "number" then return false end
    return gfx.input.key_pressed(keycode)
  end

  local function cpp_log(msg)
    if Engine and Engine.cpp_log then
      Engine.cpp_log(tostring(msg))
    else
      print(tostring(msg))
    end
  end

  local function bind_gfx(new_gfx)
    assert(new_gfx, "bind_gfx: new_gfx is nil")
    gfx = new_gfx
    C = gfx.color
  end

  ---------------------------------------------------------------------------
  -- State defaults (persistent across reloads)
  ---------------------------------------------------------------------------

  local function ensure_state_defaults()
    state.t = state.t or 0.0
    state.sim_t = state.sim_t or 0.0
    state.counter = state.counter or 0
    state.paused = (state.paused == nil) and false or state.paused

    -- Layer toggles
    state.showGrid      = (state.showGrid      == nil) and true or state.showGrid
    state.showNeon      = (state.showNeon      == nil) and true or state.showNeon
    state.showTriangles = (state.showTriangles == nil) and true or state.showTriangles
    state.showTextures  = (state.showTextures  == nil) and true or state.showTextures
    state.showCube      = (state.showCube      == nil) and true or state.showCube
    state.showPixelLab  = (state.showPixelLab  == nil) and true or state.showPixelLab

    -- Post-process / presentation
    state.bloomOn = (state.bloomOn == nil) and true or state.bloomOn
    state.linearPresent = state.linearPresent or false
    state.bloom_threshold = state.bloom_threshold or 0.72
    state.bloom_intensity = state.bloom_intensity or 1.35

    state.toneOn = (state.toneOn == nil) and true or state.toneOn
    state.tone_exposure = state.tone_exposure or 1.15
    state.tone_gamma = state.tone_gamma or 2.2

    -- Procedural baking / runtime texture content
    state.texture_seed = state.texture_seed or 1
    state.last_bake_counter = state.last_bake_counter or -1

    -- Pixel lab config (small region to keep per-pixel calls reasonable)
    state.pixel_lab = state.pixel_lab or {
      x = 24, y = 24, w = 160, h = 96,
      step = 1,          -- set_pixel spacing; keep 1 for crispness
      update_every = 2,  -- update every N frames
    }

    -- Simple visual "HUD bars" state
    state.hudPulse = state.hudPulse or 0.0
  end

  local function apply_pp_from_state()
    gfx.pp.set_bloom(state.bloomOn, state.bloom_threshold, state.bloom_intensity, 4, 6.0)
    gfx.pp.set_tone(state.toneOn, state.tone_exposure, state.tone_gamma)
    gfx.frame.set_present_filter_linear(state.linearPresent)
  end

  ---------------------------------------------------------------------------
  -- Utility drawing helpers
  ---------------------------------------------------------------------------

  local function draw_textured_quad(x, y, w, h, tex_name, tint, uv_scroll_u, uv_scroll_v)
    tint = tint or C(255, 255, 255, 255)
    local u0 = uv_scroll_u or 0.0
    local v0 = uv_scroll_v or 0.0
    local u1 = u0 + 1.0
    local v1 = v0 + 1.0

    local p0 = gfx.v2(x,     y)
    local p1 = gfx.v2(x + w, y)
    local p2 = gfx.v2(x + w, y + h)
    local p3 = gfx.v2(x,     y + h)

    local uv00 = gfx.v2(u0, v0)
    local uv10 = gfx.v2(u1, v0)
    local uv11 = gfx.v2(u1, v1)
    local uv01 = gfx.v2(u0, v1)

    -- Two textured triangles
    gfx.draw.tri_textured_named(p0, uv00, p1, uv10, p2, uv11, tex_name, tint)
    gfx.draw.tri_textured_named(p0, uv00, p2, uv11, p3, uv01, tex_name, tint)
  end

  local function draw_rect_outline(x, y, w, h, color, t)
    gfx.draw.rect(x, y, w, h, color, false, t or 1)
  end

  local function draw_cross(x, y, s, color)
    gfx.draw.line(x - s, y, x + s, y, color, 1)
    gfx.draw.line(x, y - s, x, y + s, color, 1)
  end

  ---------------------------------------------------------------------------
  -- Procedural framebuffer capture -> named textures
  ---------------------------------------------------------------------------

  local function bake_procedural_textures(seed)
    ensure_state_defaults()
    local W, H = gfx.frame.fb_size()
    seed = seed or state.texture_seed or 1

    -- Helper: draw full-screen content then capture as a texture
    local function bake_one(tex_name, painter_fn)
      gfx.clear(C(0, 0, 0, 255))
      gfx.clear_depth(1.0)
      gfx.blend_alpha()
      painter_fn(W, H, seed)
      gfx.tex.from_framebuffer(tex_name)
    end

    -- 1) Neon radial + line web texture
    bake_one("paint_tex", function(w, h, s)
      local cx, cy = w * 0.5, h * 0.5

      -- radial circles
      for i = 1, 18 do
        local t = i / 18.0
        local r = lerp(40, math.min(w, h) * 0.48, t)
        local col = palette_neon(fract(t + s * 0.07))
        col.a = math.floor(lerp(70, 220, 1.0 - t))
        gfx.draw.circle(cx, cy, r, col, false, 1 + (i % 3))
      end

      -- triangle fan-ish line web
      gfx.blend_additive()
      for i = 0, 72 do
        local a = (i / 72.0) * TAU + s * 0.13
        local x = cx + math.cos(a) * (w * 0.42)
        local y = cy + math.sin(a * 1.3) * (h * 0.35)
        local col = palette_neon(fract(i * 0.031 + s * 0.1))
        col.a = 210
        gfx.draw.line(cx, cy, x, y, col, 1)
      end
      gfx.blend_alpha()

      -- gradient triangle accents
      gfx.draw.tri_grad(
        gfx.v2(w * 0.12, h * 0.10), C(255,  60,  80, 220),
        gfx.v2(w * 0.88, h * 0.20), C( 60, 255, 180, 200),
        gfx.v2(w * 0.58, h * 0.85), C(100, 120, 255, 230)
      )

      -- sparse pixel sparkles (uses set_pixel)
      for i = 0, 2500 do
        local px = math.floor(hash11(i + s * 11.0) * (w - 1))
        local py = math.floor(hash11(i * 3.7 + s * 23.0) * (h - 1))
        local v  = hash21(px + s * 13.0, py + s * 17.0)
        if v > 0.90 then
          local col = palette_neon(fract(v + i * 0.001))
          col.a = 255
          gfx.set_pixel(px, py, col)
        end
      end
    end)

    -- 2) Checker + rings + scanlines texture
    bake_one("rings_tex", function(w, h, s)
      -- base checker from primitives (manual overlay)
      local cell = math.max(8, math.floor(math.min(w, h) / 32))
      for y = 0, h - 1, cell do
        for x = 0, w - 1, cell do
          local checker = (((math.floor(x / cell) + math.floor(y / cell)) % 2) == 0)
          local c0 = checker and C(20, 22, 30, 255) or C(8, 9, 14, 255)
          gfx.draw.rect(x, y, cell, cell, c0, true)
        end
      end

      local cx, cy = w * 0.5, h * 0.5
      gfx.blend_additive()
      for i = 0, 12 do
        local r = (i + 1) * math.min(w, h) * 0.035 + (i % 3) * 5
        local c = palette_neon(fract(i * 0.08 + s * 0.03))
        c.a = 160
        gfx.draw.circle(cx, cy, r, c, false, 2)
      end

      for i = 0, 220 do
        local t = i / 220.0
        local x0 = lerp(0, w - 1, t)
        local y0 = cy + math.sin(t * TAU * 6.0 + s * 0.35) * h * 0.22
        local x1 = x0
        local y1 = y0 + math.cos(t * TAU * 9.0 + s * 0.15) * 20
        local c = palette_neon(fract(t + s * 0.05))
        c.a = 120
        gfx.draw.line(x0, y0, x1, y1, c, 1)
      end
      gfx.blend_alpha()

      -- subtle scanlines
      for y = 0, h - 1, 4 do
        gfx.draw.line(0, y, w - 1, y, C(255, 255, 255, 12), 1)
      end
    end)

    -- 3) Glyph-ish geometric texture (great on cubes)
    bake_one("glyph_tex", function(w, h, s)
      gfx.clear(C(6, 8, 14, 255))
      local cols, rows = 8, 5
      local margin = 24
      local cellW = (w - margin * 2) / cols
      local cellH = (h - margin * 2) / rows

      for gy = 0, rows - 1 do
        for gx = 0, cols - 1 do
          local x = margin + gx * cellW
          local y = margin + gy * cellH
          local cx = x + cellW * 0.5
          local cy = y + cellH * 0.5
          local h1 = hash21(gx + s * 3.0, gy + s * 5.0)
          local h2 = hash21(gx + s * 7.0, gy + s * 11.0)
          local base = palette_neon(fract(h1 + gx * 0.07 + gy * 0.03))
          base.a = 220

          gfx.draw.rect(x + 2, y + 2, cellW - 4, cellH - 4, C(18, 22, 36, 255), true)
          gfx.draw.rect(x + 2, y + 2, cellW - 4, cellH - 4, C(255, 255, 255, 24), false, 1)

          local mode = math.floor(h1 * 4)
          if mode == 0 then
            gfx.draw.circle(cx, cy, math.min(cellW, cellH) * (0.18 + 0.18 * h2), base, false, 2)
            gfx.draw.line(x + 8, cy, x + cellW - 8, cy, base, 1)
            gfx.draw.line(cx, y + 8, cx, y + cellH - 8, base, 1)
          elseif mode == 1 then
            gfx.draw.tri_grad(
              gfx.v2(cx, y + 8), col_lerp(base, C(255,255,255,255), 0.4),
              gfx.v2(x + cellW - 8, y + cellH - 8), base,
              gfx.v2(x + 8, y + cellH - 8), C(80, 120, 255, 220)
            )
          elseif mode == 2 then
            for i = 0, 3 do
              local t = i / 3
              gfx.draw.rect(
                x + 8 + t * 8,
                y + 8 + t * 6,
                cellW - 16 - t * 16,
                cellH - 16 - t * 12,
                col_lerp(base, C(255,255,255,255), t * 0.2),
                false, 1
              )
            end
          else
            gfx.blend_additive()
            for i = 0, 8 do
              local a = (i / 9) * TAU + h2 * 2
              local rr = math.min(cellW, cellH) * 0.35
              gfx.draw.line(
                cx, cy,
                cx + math.cos(a) * rr,
                cy + math.sin(a) * rr,
                base, 1
              )
            end
            gfx.blend_alpha()
          end
        end
      end
    end)

    state.last_bake_counter = state.counter
    cpp_log(("Baked procedural textures (seed=%d)"):format(seed))
  end

  ---------------------------------------------------------------------------
  -- Scene resources
  ---------------------------------------------------------------------------

  function Scene.ensure_resources()
    ensure_state_defaults()

    if not gfx.tex.exists("checker") then
      gfx.tex.make_checker("checker", 256, 256, 16)
    end

    if not gfx.mesh.exists("cube") then
      gfx.mesh.make_cube("cube", 1.0)
    end

    -- Bake procedural textures once (or if hot reload re-created gfx state)
    if (not gfx.tex.exists("paint_tex")) or (not gfx.tex.exists("rings_tex")) or (not gfx.tex.exists("glyph_tex")) then
      bake_procedural_textures(state.texture_seed)
    end

    apply_pp_from_state()
  end

  ---------------------------------------------------------------------------
  -- Input / state updates
  ---------------------------------------------------------------------------

  local function log_toggle(name, value)
    cpp_log(name .. ": " .. tostring(value))
  end

  local function handle_input()
    gfx.input.poll_events()

    if key_pressed_safe(Engine.KEY_ESCAPE) then
      gfx.input.request_close()
      return true
    end

    -- Layer toggles
    if key_pressed_safe(Engine.KEY_1) then state.showGrid      = not state.showGrid      ; log_toggle("showGrid", state.showGrid) end
    if key_pressed_safe(Engine.KEY_2) then state.showNeon      = not state.showNeon      ; log_toggle("showNeon", state.showNeon) end
    if key_pressed_safe(Engine.KEY_3) then state.showTriangles = not state.showTriangles ; log_toggle("showTriangles", state.showTriangles) end
    if key_pressed_safe(Engine.KEY_4) then state.showTextures  = not state.showTextures  ; log_toggle("showTextures", state.showTextures) end
    if key_pressed_safe(Engine.KEY_5) then state.showCube      = not state.showCube      ; log_toggle("showCube", state.showCube) end
    if key_pressed_safe(Engine.KEY_6) then state.showPixelLab  = not state.showPixelLab  ; log_toggle("showPixelLab", state.showPixelLab) end

    -- Pause
    if key_pressed_safe(Engine.KEY_SPACE) then
      state.paused = not state.paused
      log_toggle("paused", state.paused)
    end

    -- Bloom
    if key_pressed_safe(Engine.KEY_B) then
      state.bloomOn = not state.bloomOn
      apply_pp_from_state()
      log_toggle("Bloom", state.bloomOn)
    end
    if key_pressed_safe(Engine.KEY_LEFT_BRACKET) then
      state.bloom_threshold = math.max(0.0, state.bloom_threshold - 0.05)
      apply_pp_from_state()
      cpp_log(("Bloom threshold: %.2f"):format(state.bloom_threshold))
    end
    if key_pressed_safe(Engine.KEY_RIGHT_BRACKET) then
      state.bloom_threshold = math.min(1.0, state.bloom_threshold + 0.05)
      apply_pp_from_state()
      cpp_log(("Bloom threshold: %.2f"):format(state.bloom_threshold))
    end
    if key_pressed_safe(Engine.KEY_MINUS) then
      state.bloom_intensity = math.max(0.0, state.bloom_intensity - 0.1)
      apply_pp_from_state()
      cpp_log(("Bloom intensity: %.2f"):format(state.bloom_intensity))
    end
    if key_pressed_safe(Engine.KEY_EQUAL) then
      state.bloom_intensity = state.bloom_intensity + 0.1
      apply_pp_from_state()
      cpp_log(("Bloom intensity: %.2f"):format(state.bloom_intensity))
    end

    -- Tone map
    if key_pressed_safe(Engine.KEY_T) then
      state.toneOn = not state.toneOn
      apply_pp_from_state()
      log_toggle("Tone", state.toneOn)
    end

    -- Present filter
    if key_pressed_safe(Engine.KEY_F) then
      state.linearPresent = not state.linearPresent
      apply_pp_from_state()
      log_toggle("Present linear", state.linearPresent)
    end

    -- Re-bake procedural textures
    if key_pressed_safe(Engine.KEY_R) then
      state.texture_seed = (state.texture_seed or 1) + 1
      bake_procedural_textures(state.texture_seed)
    end

    -- Save frame
    if key_pressed_safe(Engine.KEY_S) then
      gfx.frame.save_png(true)
      gfx.frame.next()
      cpp_log("Saved frame PNG.")
    end

    return false
  end

  ---------------------------------------------------------------------------
  -- Render layers
  ---------------------------------------------------------------------------

  local function draw_background_and_grid(W, H, t)
    -- base background
    gfx.draw.rect(0, 0, W, H, C(6, 8, 12, 255), true)

    -- soft vignette-ish rings
    gfx.blend_additive()
    local cx, cy = W * 0.5, H * 0.5
    for i = 0, 8 do
      local rr = math.min(W, H) * (0.18 + i * 0.055)
      local c = palette_neon(fract(i * 0.08 + t * 0.03))
      c.a = (i == 0) and 70 or 28
      gfx.draw.circle(cx, cy, rr, c, false, (i % 2) + 1)
    end
    gfx.blend_alpha()

    if not state.showGrid then return end

    -- animated grid
    local spacing = 64
    local xshift = (t * 35.0) % spacing
    local yshift = (t * 20.0) % spacing

    gfx.blend_additive()
    for x = -spacing, W + spacing, spacing do
      local xx = x + xshift
      local a = math.floor(18 + 10 * (0.5 + 0.5 * math.sin(xx * 0.015 + t)))
      gfx.draw.line(xx, 0, xx, H - 1, C(40, 80, 140, a), 1)
    end
    for y = -spacing, H + spacing, spacing do
      local yy = y + yshift
      local a = math.floor(14 + 8 * (0.5 + 0.5 * math.cos(yy * 0.012 + t * 1.2)))
      gfx.draw.line(0, yy, W - 1, yy, C(30, 60, 100, a), 1)
    end

    -- central axes
    gfx.draw.line(0, H * 0.5, W - 1, H * 0.5, C(140, 160, 200, 40), 1)
    gfx.draw.line(W * 0.5, 0, W * 0.5, H - 1, C(140, 160, 200, 40), 1)
    gfx.blend_alpha()
  end

  local function draw_neon_strokes(W, H, t)
    if not state.showNeon then return end

    local cx, cy = W * 0.5, H * 0.5

    -- Bloom-friendly emissive lines / circles
    gfx.blend_additive()

    -- horizontal beam
    gfx.draw.line(cx - 360, cy, cx + 360, cy, C(255, 255, 255, 255), 3)

    -- oscillating rings
    local xoff = math.sin(t * 1.35) * 220.0
    local yoff = math.cos(t * 0.85) * 110.0
    gfx.draw.circle(cx + xoff, cy + yoff, 110 + math.sin(t * 2.1) * 12, C(255, 220, 160, 230), false, 4)
    gfx.draw.circle(cx - xoff * 0.6, cy - yoff * 0.4, 72 + math.cos(t * 2.6) * 8, C(140, 255, 210, 220), false, 3)

    -- Lissajous line fan
    for i = 0, 48 do
      local u = i / 48.0
      local a = u * TAU + t * 0.6
      local px = cx + math.cos(a * 2.0 + 0.4) * (W * 0.28)
      local py = cy + math.sin(a * 3.0 + 1.2) * (H * 0.20)
      local qx = cx + math.sin(a * 4.0 + t * 0.8) * (W * 0.14)
      local qy = cy + math.cos(a * 5.0 - t * 1.1) * (H * 0.10)
      local c = palette_neon(fract(u + t * 0.04))
      c.a = 90
      gfx.draw.line(px, py, qx, qy, c, 1)
    end

    gfx.blend_alpha()

    -- Crisp outline accents
    gfx.draw.circle(cx, cy, 18, C(255, 255, 255, 180), false, 1)
    draw_cross(cx, cy, 8, C(255, 255, 255, 160))
  end

  local function draw_triangle_gallery(W, H, t)
    if not state.showTriangles then return end

    local left = 40
    local top  = H * 0.58
    local panelW = W * 0.35
    local panelH = H * 0.34

    gfx.draw.rect(left, top, panelW, panelH, C(10, 12, 18, 210), true)
    draw_rect_outline(left, top, panelW, panelH, C(255, 255, 255, 50), 1)

    -- animated gradient triangle
    local cx = left + panelW * 0.30
    local cy = top + panelH * 0.45
    local r  = math.min(panelW, panelH) * 0.28
    for i = 0, 2 do
      local a0 = t * 0.55 + i * TAU / 3
      local a1 = a0 + TAU / 3
      local a2 = a1 + TAU / 3
      gfx.draw.tri_grad(
        gfx.v2(cx + math.cos(a0) * r, cy + math.sin(a0) * r), palette_neon(fract(i * 0.33 + t * 0.02)),
        gfx.v2(cx + math.cos(a1) * r, cy + math.sin(a1) * r), palette_neon(fract(i * 0.33 + 0.2 + t * 0.02)),
        gfx.v2(cx + math.cos(a2) * r, cy + math.sin(a2) * r), palette_neon(fract(i * 0.33 + 0.4 + t * 0.02))
      )
      r = r * 0.72
    end

    -- ribbon of filled triangles
    for i = 0, 10 do
      local u = i / 10.0
      local x = left + panelW * 0.52 + u * panelW * 0.40
      local y = top + panelH * (0.20 + 0.55 * (0.5 + 0.5 * math.sin(u * TAU * 1.5 + t * 1.2)))
      local s = 18 + 10 * math.sin(t * 2.0 + i)
      local col = palette_neon(fract(u + t * 0.03))
      col.a = 190
      gfx.draw.tri_filled(
        gfx.v2(x, y - s),
        gfx.v2(x + s * 0.85, y + s),
        gfx.v2(x - s * 0.85, y + s),
        col
      )
    end

    -- border lines for "technical diagram" feel
    gfx.blend_additive()
    for i = 0, 8 do
      local yy = top + 8 + i * ((panelH - 16) / 8)
      gfx.draw.line(left + 6, yy, left + panelW - 6, yy, C(120, 180, 255, 20), 1)
    end
    gfx.blend_alpha()
  end

  local function draw_texture_gallery(W, H, t)
    if not state.showTextures then return end

    local rightW = W * 0.38
    local x0 = W - rightW - 24
    local y0 = H * 0.52
    local pad = 12

    local panelW = rightW
    local panelH = H * 0.40
    gfx.draw.rect(x0, y0, panelW, panelH, C(10, 12, 18, 210), true)
    draw_rect_outline(x0, y0, panelW, panelH, C(255, 255, 255, 50), 1)

    local cellW = (panelW - pad * 4) / 3
    local cellH = panelH - pad * 2
    local y = y0 + pad

    local texA = gfx.tex.exists("paint_tex") and "paint_tex" or "checker"
    local texB = gfx.tex.exists("rings_tex") and "rings_tex" or "checker"
    local texC = gfx.tex.exists("glyph_tex") and "glyph_tex" or "checker"

    -- animated UV scroll / tint modulation
    local t0 = 0.05 * math.sin(t * 0.7)
    local t1 = 0.06 * math.cos(t * 0.9)
    local t2 = 0.08 * math.sin(t * 0.4 + 1.7)

    draw_textured_quad(x0 + pad,               y, cellW, cellH, texA, C(255,255,255,230), t0, 0.0)
    draw_textured_quad(x0 + pad * 2 + cellW,   y, cellW, cellH, texB, C(230,255,255,235), 0.0, t1)
    draw_textured_quad(x0 + pad * 3 + cellW*2, y, cellW, cellH, texC, C(255,230,255,235), t2, t2 * 0.7)

    -- animated frames + glows
    gfx.blend_additive()
    for i = 0, 2 do
      local bx = x0 + pad + i * (cellW + pad)
      local glow = 30 + 20 * (0.5 + 0.5 * math.sin(t * 2.0 + i))
      local c = palette_neon(fract(i * 0.23 + t * 0.02))
      c.a = glow
      draw_rect_outline(bx - 2, y - 2, cellW + 4, cellH + 4, c, 1)
      draw_rect_outline(bx - 5, y - 5, cellW + 10, cellH + 10, c, 1)
    end
    gfx.blend_alpha()
  end

  local function draw_pixel_lab(W, H, t)
    if not state.showPixelLab then return end

    local cfg = state.pixel_lab
    local px, py, pw, ph = cfg.x, cfg.y, cfg.w, cfg.h
    local step = math.max(1, cfg.step or 1)
    local update_every = math.max(1, cfg.update_every or 1)

    -- panel background
    gfx.draw.rect(px - 4, py - 4, pw + 8, ph + 8, C(8, 10, 16, 220), true)
    draw_rect_outline(px - 4, py - 4, pw + 8, ph + 8, C(255, 255, 255, 70), 1)

    -- update the pixel field only every N frames (performance-friendly)
    if (state.counter % update_every) == 0 then
      for y = 0, ph - 1, step do
        for x = 0, pw - 1, step do
          local u = (pw > 1) and (x / (pw - 1)) or 0.0
          local v = (ph > 1) and (y / (ph - 1)) or 0.0

          -- Plasma-ish field
          local wave =
              math.sin((u * 7.0 + t * 0.8) * TAU) * 0.35 +
              math.sin((v * 5.0 - t * 1.1) * TAU) * 0.30 +
              math.sin(((u + v) * 6.0 + t * 0.5) * TAU) * 0.20 +
              (hash21(x + state.texture_seed * 13, y + state.texture_seed * 29) - 0.5) * 0.15

          local n = 0.5 + 0.5 * wave
          local p = palette_neon(fract(n + t * 0.03))
          local edge = smooth01(1.0 - math.abs(v * 2.0 - 1.0))
          p.r = clamp(p.r * (0.65 + edge * 0.5), 0, 255)
          p.g = clamp(p.g * (0.60 + edge * 0.6), 0, 255)
          p.b = clamp(p.b * (0.80 + edge * 0.4), 0, 255)
          p.a = 255

          gfx.set_pixel(px + x, py + y, p)
        end
      end
    end

    -- Sparse get_pixel probes drive accents (demonstrates pixel reads)
    local sample_positions = {
      {0.15, 0.20}, {0.50, 0.35}, {0.82, 0.68}, {0.33, 0.78}
    }

    for i = 1, #sample_positions do
      local sx = px + math.floor((pw - 1) * sample_positions[i][1])
      local sy = py + math.floor((ph - 1) * sample_positions[i][2])
      local c = gfx.get_pixel(sx, sy) or C(255,255,255,255)

      -- markers in panel
      draw_cross(sx, sy, 2, C(255,255,255,170))

      -- echo those colors into circles below panel
      local ox = px + 18 + (i - 1) * 36
      local oy = py + ph + 18
      gfx.draw.circle(ox, oy, 11, C(c.r, c.g, c.b, 180), true)
      gfx.draw.circle(ox, oy, 13, C(255,255,255,200), false, 1)
    end

    -- small color strip using sampled colors
    for i = 0, 15 do
      local sx = px + math.floor((i / 15) * (pw - 1))
      local sy = py + math.floor((0.5 + 0.45 * math.sin(i * 0.7 + t)) * (ph - 1))
      local c = gfx.get_pixel(sx, sy) or C(255,255,255,255)
      gfx.draw.rect(px + i * 10, py + ph + 38, 9, 8, C(c.r, c.g, c.b, 255), true)
    end
  end

  local function draw_cube_gallery(W, H, t)
    if not state.showCube then return end

    -- 3D "stage" panel (subtle framing in 2D)
    local stageX = W * 0.27
    local stageY = H * 0.12
    local stageW = W * 0.46
    local stageH = H * 0.34
    gfx.draw.rect(stageX, stageY, stageW, stageH, C(12, 14, 20, 130), true)
    draw_rect_outline(stageX, stageY, stageW, stageH, C(255,255,255,36), 1)

    local aspect = (H ~= 0) and (W / H) or 1.0
    local P = gfx.mat4.perspective(60.0 * math.pi / 180.0, aspect, 0.1, 100.0)

    local cam_r = 4.2
    local cam_x = math.sin(t * 0.25) * 0.6
    local cam_z = cam_r + math.cos(t * 0.30) * 0.4
    local V = gfx.mat4.look_at(
      gfx.v3(cam_x, 0.8 + math.sin(t * 0.2) * 0.1, cam_z),
      gfx.v3(0, 0, 0),
      gfx.v3(0, 1, 0)
    )

    local texA = gfx.tex.exists("paint_tex") and "paint_tex" or "checker"
    local texB = gfx.tex.exists("rings_tex") and "rings_tex" or "checker"
    local texC = gfx.tex.exists("glyph_tex") and "glyph_tex" or "checker"

    local cubes = {
      { x = -1.35, y =  0.15, z = 0.00, s = 0.85, tex = texA, speed = 0.90 },
      { x =  0.00, y = -0.10, z = 0.00, s = 1.00, tex = texB, speed = 1.15 },
      { x =  1.35, y =  0.20, z = 0.10, s = 0.75, tex = texC, speed = 1.40 },
      { x = -0.25, y =  1.00, z = -0.60, s = 0.55, tex = texC, speed = 1.80 },
      { x =  0.80, y = -0.95, z = -0.40, s = 0.50, tex = texA, speed = 2.10 },
    }

    for i = 1, #cubes do
      local c = cubes[i]
      local tt = t * c.speed + i * 0.73

      -- We only have translate + rotate matrices in exposed API. If you later expose scale,
      -- this becomes even nicer. For now, different "sizes" can be faked by different z/y layout
      -- and the baked textures already create nice variation.
      local T  = gfx.mat4.translate(gfx.v3(
        c.x + math.sin(tt * 0.7) * 0.15,
        c.y + math.cos(tt * 0.9) * 0.10,
        c.z + math.sin(tt * 1.1) * 0.10
      ))
      local Rx = gfx.mat4.rotate_x(tt * 0.7)
      local Ry = gfx.mat4.rotate_y(tt * 1.1)
      local Rz = gfx.mat4.rotate_z(tt * 0.4)

      local Rxy = gfx.mat4.mul(Ry, Rx)
      local R   = gfx.mat4.mul(Rz, Rxy)
      local M   = gfx.mat4.mul(T, R)
      local MVP = gfx.mat4.mul(P, gfx.mat4.mul(V, M))

      gfx.mesh.draw_named("cube", MVP, c.tex, true)
    end

    -- Add a 2D "orbit ring" overlay to tie 2D+3D layers together
    gfx.blend_additive()
    gfx.draw.circle(W * 0.5, H * 0.29, math.min(W, H) * 0.12, C(255, 255, 255, 40), false, 1)
    gfx.draw.circle(W * 0.5, H * 0.29, math.min(W, H) * 0.16, C(120, 200, 255, 30), false, 1)
    gfx.blend_alpha()
  end

  local function draw_visual_hud(W, H, t)
    -- Visual-only mini HUD (no text API assumed)
    local x = 14
    local y = H - 30
    local barW = 14
    local gap = 4

    local items = {
      { on = state.showGrid,      c = C(100, 160, 255, 255) },
      { on = state.showNeon,      c = C(255, 220, 160, 255) },
      { on = state.showTriangles, c = C(120, 255, 160, 255) },
      { on = state.showTextures,  c = C(220, 180, 255, 255) },
      { on = state.showCube,      c = C(255, 255, 255, 255) },
      { on = state.showPixelLab,  c = C(255, 120, 200, 255) },
      { on = state.bloomOn,       c = C(255, 255, 180, 255) },
      { on = state.toneOn,        c = C(180, 255, 255, 255) },
      { on = state.linearPresent, c = C(180, 220, 255, 255) },
    }

    gfx.draw.rect(x - 6, y - 8, (#items) * (barW + gap) + 12, 22, C(8, 10, 16, 180), true)
    draw_rect_outline(x - 6, y - 8, (#items) * (barW + gap) + 12, 22, C(255,255,255,35), 1)

    for i = 1, #items do
      local bx = x + (i - 1) * (barW + gap)
      local col = items[i].c
      if items[i].on then
        gfx.draw.rect(bx, y, barW, 8, col, true)
        gfx.blend_additive()
        gfx.draw.rect(bx, y - 2, barW, 2, C(col.r, col.g, col.b, 90), true)
        gfx.blend_alpha()
      else
        gfx.draw.rect(bx, y, barW, 8, C(60, 70, 90, 180), true)
      end
    end

    -- bloom/tone status pulse dot
    local pulse = 0.5 + 0.5 * math.sin(t * 4.0)
    local dot = state.bloomOn and C(255, 230, 120, 220 * pulse) or C(80, 90, 110, 180)
    gfx.draw.circle(W - 18, 18, 5, dot, true)
    gfx.draw.circle(W - 18, 18, 7, C(255,255,255,80), false, 1)
  end

  ---------------------------------------------------------------------------
  -- Main update/render
  ---------------------------------------------------------------------------

  function Scene.update(dt)
    Scene.ensure_resources()
    ensure_state_defaults()

    dt = dt or 0.0
    state.counter = state.counter + 1

    if handle_input() then
      return
    end

    if not state.paused then
      state.sim_t = state.sim_t + dt
    end
    state.t = state.t + dt -- raw wall-ish time accumulator (kept even when paused if you want)
    local t = state.paused and state.sim_t or state.sim_t

    -- Begin frame
    local W, H = gfx.frame.begin(C(0, 0, 0, 255))

    -- Core layers
    draw_background_and_grid(W, H, t)
    draw_neon_strokes(W, H, t)
    draw_triangle_gallery(W, H, t)
    draw_texture_gallery(W, H, t)
    draw_cube_gallery(W, H, t)
    draw_pixel_lab(W, H, t)

    -- Some extra animated accents across the whole screen
    gfx.blend_additive()
    for i = 0, 7 do
      local u = i / 7.0
      local x = lerp(30, W - 30, u)
      local y = H * 0.12 + math.sin(t * 1.4 + i * 0.7) * 10
      local c = palette_neon(fract(u + t * 0.02))
      c.a = 90
      gfx.draw.circle(x, y, 4 + (i % 3), c, false, 1)
    end
    gfx.blend_alpha()

    draw_visual_hud(W, H, t)

    gfx.frame["end"](true)
  end

  ---------------------------------------------------------------------------
  -- Optional hooks / hot-reload friendliness
  ---------------------------------------------------------------------------

  function Scene.set_gfx(new_gfx)
    bind_gfx(new_gfx)
    apply_pp_from_state()
  end

  function Scene.reset_soft()
    -- keep resources, reset scene runtime state
    state.sim_t = 0.0
    state.counter = 0
    cpp_log("Scene.reset_soft()")
  end

  function Scene.reset_hard()
    -- scene state reset (textures can be re-baked on next ensure)
    for k, _ in pairs(state) do
      state[k] = nil
    end
    ensure_state_defaults()
    cpp_log("Scene.reset_hard()")
  end

  function Scene.on_reload()
    -- Re-apply settings after gfx reload / Lua hot reload
    ensure_state_defaults()
    apply_pp_from_state()
    cpp_log("Scene.on_reload()")
  end

  return Scene
end