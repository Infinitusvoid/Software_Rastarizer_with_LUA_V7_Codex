-- main_lua_example_v1_4_util.lua
-- Shared runtime helpers for main_lua_example_v1_4.lua
-- Responsibilities:
--   - logging
--   - hot-reload-safe require
--   - state defaults
--   - loading gfx wrapper
--   - loading scene module
--   - running sandbox generator (optional)

return function(ctx)
  if type(ctx) ~= "table" then
    error("util ctx must be a table", 2)
  end

  local Engine = ctx.Engine
  local state  = ctx.state
  local cmd    = ctx.cmd

  if type(Engine) ~= "table" then error("ctx.Engine must be a table", 2) end
  if type(state)  ~= "table" then error("ctx.state must be a table", 2) end
  if type(cmd)    ~= "function" then error("ctx.cmd must be a function", 2) end

  local U = {}

  U.SCENE_MODULE_DEFAULT     = ctx.scene_module_default or "game_scene_demo"
  U.GFX_MODULE               = ctx.gfx_module or "gfx"
  U.SANDBOX_GENERATOR_MODULE = ctx.sandbox_generator_module or "main_lua_example_v1_4_generating"

  function U.log(msg)
    Engine.cpp_log(tostring(msg))
  end

  function U.require_fresh(name)
    if type(name) ~= "string" then
      error("require_fresh(name): name must be a string", 2)
    end
    package.loaded[name] = nil
    local ok, mod = pcall(require, name)
    if not ok then
      error("Failed to require '" .. name .. "': " .. tostring(mod), 2)
    end
    return mod
  end

  function U.ensure_shared_state_defaults()
    -- Time / counters
    state.t = state.t or 0.0
    state.counter = state.counter or 0

    -- Demo toggles
    state.showLines = (state.showLines == nil) and true or state.showLines
    state.showRects = (state.showRects == nil) and true or state.showRects
    state.showCircs = (state.showCircs == nil) and true or state.showCircs
    state.showTri2D = (state.showTri2D == nil) and true or state.showTri2D
    state.showCube  = (state.showCube  == nil) and true or state.showCube

    -- Presentation / post FX toggles
    state.bloomOn = (state.bloomOn == nil) and false or state.bloomOn
    state.linearPresent = (state.linearPresent == nil) and false or state.linearPresent

    -- Bloom params
    state.bloom_threshold = state.bloom_threshold or 0.75
    state.bloom_intensity = state.bloom_intensity or 1.25

    -- Tone mapping params
    state.toneOn = (state.toneOn == nil) and false or state.toneOn
    state.tone_exposure = state.tone_exposure or 1.25
    state.tone_gamma = state.tone_gamma or 2.2
  end

  function U.load_gfx()
    local make_gfx = U.require_fresh(U.GFX_MODULE)
    if type(make_gfx) ~= "function" then
      error(U.GFX_MODULE .. ".lua must return function(cmd) -> gfx", 2)
    end

    local gfx = make_gfx(cmd)
    if type(gfx) ~= "table" then
      error(U.GFX_MODULE .. " factory must return gfx table", 2)
    end

    return gfx
  end

  local function validate_scene(scene_module_name, scene_obj)
    if type(scene_obj) ~= "table" then
      error(scene_module_name .. " must return a scene table", 3)
    end

    if type(scene_obj.ensure_resources) ~= "function" then
      error(scene_module_name .. " scene missing ensure_resources()", 3)
    end

    if type(scene_obj.update) ~= "function" then
      error(scene_module_name .. " scene missing update(dt)", 3)
    end
  end

  function U.load_scene(gfx_instance)
    if type(gfx_instance) ~= "table" then
      error("load_scene(gfx): gfx_instance must be a table", 2)
    end

    local scene_module_name = Engine.SceneModule or U.SCENE_MODULE_DEFAULT
    local make_scene = U.require_fresh(scene_module_name)

    if type(make_scene) ~= "function" then
      error(scene_module_name .. ".lua must return function(ctx) -> scene", 2)
    end

    local scene_obj = make_scene({
      Engine = Engine,
      state  = state,
      gfx    = gfx_instance,
    })

    validate_scene(scene_module_name, scene_obj)
    return scene_obj
  end

  function U.run_sandbox_generator()
    local ok_gen_mod, gen = pcall(U.require_fresh, U.SANDBOX_GENERATOR_MODULE)
    if not (ok_gen_mod and gen and gen.generate) then
      U.log("Sandbox generator not available: " .. tostring(gen))
      return false
    end

    local outp = Engine.SandboxOutPath
    local ok_gen_run, gen_err = pcall(function()
      gen.generate(outp)
    end)

    if not ok_gen_run then
      U.log("Sandbox generator generate() failed: " .. tostring(gen_err))
      return false
    end

    U.log("Sandbox generator wrote: " .. tostring(outp))
    return true
  end

  

  return U
end