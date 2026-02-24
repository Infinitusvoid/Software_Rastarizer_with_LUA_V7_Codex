-- game_scene_demo.lua
-- Swappable "game/scene" module.
-- Owns scene-specific state defaults, resource creation, input handling, and rendering.

return function(ctx)
  assert(type(ctx) == "table", "game_scene_demo: ctx table required")
  assert(ctx.Engine, "game_scene_demo: ctx.Engine required")
  assert(ctx.state,  "game_scene_demo: ctx.state required")
  assert(ctx.gfx,    "game_scene_demo: ctx.gfx required")

  local Engine = ctx.Engine
  local state  = ctx.state
  local gfx    = ctx.gfx
  local C      = gfx.color

  local Scene = {}

  local function bind_gfx(new_gfx)
    assert(new_gfx, "bind_gfx: new_gfx is nil")
    gfx = new_gfx
    C = gfx.color
  end

  local function ensure_state_defaults()
    -- Persistent state across reloads (lives in Engine.State)
    state.t = state.t or 0.0
    state.counter = state.counter or 0

    state.showLines = (state.showLines == nil) and true or state.showLines
    state.showRects = (state.showRects == nil) and true or state.showRects
    state.showCircs = (state.showCircs == nil) and true or state.showCircs
    state.showTri2D = (state.showTri2D == nil) and true or state.showTri2D
    state.showCube  = (state.showCube  == nil) and true or state.showCube

    state.bloomOn = state.bloomOn or false
    state.linearPresent = state.linearPresent or false

    state.bloom_threshold = state.bloom_threshold or 0.75
    state.bloom_intensity = state.bloom_intensity or 1.25

    state.toneOn = state.toneOn or false
    state.tone_exposure = state.tone_exposure or 1.25
    state.tone_gamma = state.tone_gamma or 2.2

    state.mouseTrail = state.mouseTrail or {}
    state.mouseTrailMax = state.mouseTrailMax or 24
    state.mousePulse = state.mousePulse or 0.0
  end


  local function push_mouse_trail(x, y, pressed)
    local trail = state.mouseTrail
    trail[#trail + 1] = { x = x, y = y, pressed = pressed, t = state.t }
    while #trail > state.mouseTrailMax do
      table.remove(trail, 1)
    end
  end

  local function apply_pp_from_state()
    gfx.pp.set_bloom(state.bloomOn, state.bloom_threshold, state.bloom_intensity, 4, 6.0)
    gfx.pp.set_tone(state.toneOn, state.tone_exposure, state.tone_gamma)
    gfx.frame.set_present_filter_linear(state.linearPresent)
  end

  function Scene.ensure_resources()
    ensure_state_defaults()

    -- Basic resources
    if not gfx.tex.exists("checker") then
      gfx.tex.make_checker("checker", 256, 256, 16)
    end

    if not gfx.mesh.exists("cube") then
      gfx.mesh.make_cube("cube", 1.0)
    end

    -- Create a “procedurally generated” texture ONCE by drawing into the framebuffer then capturing it.
    if not gfx.tex.exists("paint_tex") then
      local W, H = gfx.frame.fb_size()

      gfx.clear(C(0, 0, 0, 255))
      gfx.clear_depth(1.0)
      gfx.blend_alpha()

      gfx.draw.circle(W * 0.25, H * 0.35, 160, C(255,  90, 120, 220), true)
      gfx.draw.circle(W * 0.25, H * 0.35, 175, C(255, 255, 255, 255), false, 3)

      gfx.draw.tri_grad(
        gfx.v2(W * 0.55, H * 0.10), C(255,  80,  80, 255),
        gfx.v2(W * 0.90, H * 0.50), C( 80, 255,  80, 255),
        gfx.v2(W * 0.60, H * 0.80), C( 80,  80, 255, 255)
      )

      gfx.blend_additive()
      gfx.draw.line(0,     0,     W - 1, H - 1, C(255, 255, 255, 255), 2)
      gfx.draw.line(0, H - 1,     W - 1,     0, C(255, 255, 255, 255), 2)
      gfx.blend_alpha()

      -- Capture raw framebuffer -> named texture
      gfx.tex.from_framebuffer("paint_tex")
    end

    -- Re-apply current PP settings (important after gfx reloads)
    apply_pp_from_state()
  end

  function Scene.update(dt)
    ensure_state_defaults()

    dt = dt or 0.0
    state.t = state.t + dt
    state.counter = state.counter + 1

    -- Window / input
    gfx.input.poll_events()
    if gfx.input.key_pressed(Engine.KEY_ESCAPE) then
      gfx.input.request_close()
      return
    end

    -- Toggles (match old C++ demo)
    if gfx.input.key_pressed(Engine.KEY_1) then state.showLines = not state.showLines end
    if gfx.input.key_pressed(Engine.KEY_2) then state.showRects = not state.showRects end
    if gfx.input.key_pressed(Engine.KEY_3) then state.showCircs = not state.showCircs end
    if gfx.input.key_pressed(Engine.KEY_4) then state.showTri2D = not state.showTri2D end
    if gfx.input.key_pressed(Engine.KEY_5) then state.showCube  = not state.showCube  end

    -- Bloom toggle / tuning
    if gfx.input.key_pressed(Engine.KEY_B) then
      state.bloomOn = not state.bloomOn
      gfx.pp.set_bloom(state.bloomOn, state.bloom_threshold, state.bloom_intensity, 4, 6.0)
      Engine.cpp_log("Bloom: " .. tostring(state.bloomOn))
    end

    if gfx.input.key_pressed(Engine.KEY_LEFT_BRACKET) then
      state.bloom_threshold = math.max(0.0, state.bloom_threshold - 0.05)
      gfx.pp.set_bloom(state.bloomOn, state.bloom_threshold, state.bloom_intensity, 4, 6.0)
      Engine.cpp_log(("Bloom threshold: %.2f"):format(state.bloom_threshold))
    end

    if gfx.input.key_pressed(Engine.KEY_RIGHT_BRACKET) then
      state.bloom_threshold = math.min(1.0, state.bloom_threshold + 0.05)
      gfx.pp.set_bloom(state.bloomOn, state.bloom_threshold, state.bloom_intensity, 4, 6.0)
      Engine.cpp_log(("Bloom threshold: %.2f"):format(state.bloom_threshold))
    end

    if gfx.input.key_pressed(Engine.KEY_MINUS) then
      state.bloom_intensity = math.max(0.0, state.bloom_intensity - 0.1)
      gfx.pp.set_bloom(state.bloomOn, state.bloom_threshold, state.bloom_intensity, 4, 6.0)
      Engine.cpp_log(("Bloom intensity: %.2f"):format(state.bloom_intensity))
    end

    if gfx.input.key_pressed(Engine.KEY_EQUAL) then
      state.bloom_intensity = state.bloom_intensity + 0.1
      gfx.pp.set_bloom(state.bloomOn, state.bloom_threshold, state.bloom_intensity, 4, 6.0)
      Engine.cpp_log(("Bloom intensity: %.2f"):format(state.bloom_intensity))
    end

    -- Linear present toggle
    if gfx.input.key_pressed(Engine.KEY_F) then
      state.linearPresent = not state.linearPresent
      gfx.frame.set_present_filter_linear(state.linearPresent)
      Engine.cpp_log("Present linear: " .. tostring(state.linearPresent))
    end

    -- Save frame
    if gfx.input.key_pressed(Engine.KEY_S) then
      gfx.frame.save_png(true)
      gfx.frame.next()
      Engine.cpp_log("Saved frame.")
    end

    local mouse = gfx.mouse.events()
    if mouse.moved or mouse.left_pressed or mouse.right_pressed or mouse.middle_pressed then
      push_mouse_trail(mouse.x, mouse.y, mouse.left_down or mouse.right_down or mouse.middle_down)
    end

    if mouse.left_pressed then
      Engine.cpp_log(("Mouse left pressed @ (%.1f, %.1f)"):format(mouse.x, mouse.y))
    end
    if mouse.right_pressed then
      Engine.cpp_log(("Mouse right pressed @ (%.1f, %.1f)"):format(mouse.x, mouse.y))
    end
    if mouse.middle_pressed then
      Engine.cpp_log(("Mouse middle pressed @ (%.1f, %.1f)"):format(mouse.x, mouse.y))
    end

    if mouse.scrolled then
      Engine.cpp_log(("Mouse scroll (%.2f, %.2f)"):format(mouse.scroll_x, mouse.scroll_y))
    end

    -- --------------------------------------------
    -- Render
    -- --------------------------------------------
    local W, H = gfx.frame.begin(C(0, 0, 0, 255))

    -- Emissive strokes for bloom test
    gfx.blend_additive()
    gfx.draw.line(W/2 - 300, H/2, W/2 + 300, H/2, C(255, 255, 255, 255), 3)

    local xoff = math.sin(state.t * 1.2) * 200.0
    gfx.draw.circle(W/2 + xoff, H/2, 120, C(255, 220, 180, 255), false, 4)

    gfx.blend_alpha()

    -- Test / demo lines
    gfx.draw.line(10, 10, 200 + math.sin(state.t) * 100.0, 200, C(255, 255, 255, 255), 10)
    gfx.draw.line(200, 200, 200 + math.sin(state.t) * 100.0, 200, C(0, 255, 0, 255))

    -- Grid
    if state.showLines then
      gfx.blend_additive()
      for x = 0, W - 1, 80 do
        gfx.draw.line(x, 0, x, H - 1, C(18, 18, 18, 255), 1)
      end
      for y = 0, H - 1, 80 do
        gfx.draw.line(0, y, W - 1, y, C(18, 18, 18, 255), 1)
      end
      gfx.blend_alpha()
    end

    -- Rectangles
    if state.showRects then
      gfx.draw.rect(80, 80, 320, 200, C(80, 120, 255, 200), true)
      gfx.draw.rect(90, 90, 300, 180, C(255, 255, 255, 255), false, 2)
    end

    -- Circles
    if state.showCircs then
      gfx.draw.circle(600, 200, 100, C(255, 90, 120, 200), true)
      gfx.draw.circle(600, 200, 110, C(255, 255, 255, 255), false, 2)
    end

    -- 2D triangles
    if state.showTri2D then
      gfx.draw.tri_filled(
        gfx.v2(900, 120),
        gfx.v2(1150, 220),
        gfx.v2(980, 360),
        C(60, 255, 140, 220)
      )

      gfx.draw.tri_grad(
        gfx.v2(900, 520),  C(255, 0, 0, 255),
        gfx.v2(1180, 520), C(0, 255, 0, 255),
        gfx.v2(1040, 760), C(0, 0, 255, 255)
      )

      gfx.draw.tri_textured_named(
        gfx.v2(1200, 120), gfx.v2(0,   0),
        gfx.v2(1500, 220), gfx.v2(1,   0),
        gfx.v2(1300, 420), gfx.v2(0.5, 1),
        "checker",
        C(255, 255, 255, 255)
      )
    end

    -- 3D rotating cube (depth + texture)
    if state.showCube then
      local t = gfx.time.seconds()

      local M_model = gfx.mat4.mul(
        gfx.mat4.translate(gfx.v3(0, 0, 0)),
        gfx.mat4.rotate_y(t * 0.9)
      )

      local V_view = gfx.mat4.look_at(
        gfx.v3(0, 0, 4),
        gfx.v3(0, 0, 0),
        gfx.v3(0, 1, 0)
      )

      local aspect = (H ~= 0) and (W / H) or 1.0
      local P_proj = gfx.mat4.perspective(
        60.0 * math.pi / 180.0,
        aspect,
        0.1,
        100.0
      )

      local MVP = gfx.mat4.mul(P_proj, gfx.mat4.mul(V_view, M_model))

      local tex = (gfx.tex.exists("paint_tex") and "paint_tex") or "checker"
      gfx.mesh.draw_named("cube", MVP, tex, true)
    end


    -- Mouse event showcase (cursor halo + trail + button states)
    local pulse = (math.sin(state.t * 8.0) * 0.5 + 0.5)
    local haloR = 16 + pulse * 8
    local haloColor = mouse.in_window and C(120, 200, 255, 220) or C(120, 120, 120, 180)

    gfx.draw.circle(mouse.x, mouse.y, haloR, haloColor, false, 2)
    gfx.draw.line(mouse.x - 12, mouse.y, mouse.x + 12, mouse.y, C(255, 255, 255, 220), 1)
    gfx.draw.line(mouse.x, mouse.y - 12, mouse.x, mouse.y + 12, C(255, 255, 255, 220), 1)

    if mouse.left_down then
      gfx.draw.circle(mouse.x, mouse.y, 8, C(100, 255, 100, 220), true)
    elseif mouse.right_down then
      gfx.draw.circle(mouse.x, mouse.y, 8, C(255, 120, 120, 220), true)
    elseif mouse.middle_down then
      gfx.draw.circle(mouse.x, mouse.y, 8, C(120, 120, 255, 220), true)
    end

    for i = 1, #state.mouseTrail do
      local p = state.mouseTrail[i]
      local alpha = math.floor((i / math.max(1, #state.mouseTrail)) * 180)
      local col = p.pressed and C(255, 220, 120, alpha) or C(120, 220, 255, alpha)
      gfx.draw.circle(p.x, p.y, 2 + i * 0.15, col, true)
    end

    local panelX, panelY = 20, H - 170
    gfx.draw.rect(panelX, panelY, 430, 140, C(10, 10, 16, 200), true)
    gfx.draw.rect(panelX, panelY, 430, 140, C(180, 200, 255, 220), false, 1)

    local dx_vis = math.max(-60, math.min(60, mouse.dx * 8))
    local dy_vis = math.max(-60, math.min(60, mouse.dy * 8))
    gfx.draw.line(panelX + 90, panelY + 70, panelX + 90 + dx_vis, panelY + 70, C(120, 255, 120, 255), 3)
    gfx.draw.line(panelX + 90, panelY + 70, panelX + 90, panelY + 70 + dy_vis, C(255, 120, 120, 255), 3)

    local sx = panelX + 240
    local sy = panelY + 70
    gfx.draw.circle(sx, sy, 20, C(90, 120, 255, 220), false, 1)
    local sdx = math.max(-18, math.min(18, mouse.scroll_x * 6))
    local sdy = math.max(-18, math.min(18, mouse.scroll_y * 6))
    gfx.draw.line(sx, sy, sx + sdx, sy, C(120, 200, 255, 255), 2)
    gfx.draw.line(sx, sy, sx, sy - sdy, C(255, 220, 120, 255), 2)

    local by = panelY + 112
    gfx.draw.rect(panelX + 24, by, 24, 16, mouse.left_down and C(100,255,100,255) or C(60,60,60,255), true)
    gfx.draw.rect(panelX + 56, by, 24, 16, mouse.middle_down and C(120,120,255,255) or C(60,60,60,255), true)
    gfx.draw.rect(panelX + 88, by, 24, 16, mouse.right_down and C(255,120,120,255) or C(60,60,60,255), true)

    gfx.frame["end"](true)
  end

  

  -- Optional helper if you ever want to keep the same scene object and just rebind gfx
  function Scene.set_gfx(new_gfx)
    bind_gfx(new_gfx)
  end

  return Scene
end