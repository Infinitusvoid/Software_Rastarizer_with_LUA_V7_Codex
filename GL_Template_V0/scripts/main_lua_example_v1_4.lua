print("[Lua] main_lua_example_v1_4.lua loaded (Lua-driven scene)")

-- This module returns a factory function that creates a script instance.
return function(Engine)
  Engine.State = Engine.State or {}
  local state = Engine.State

  -- Bridge command helper
  local function cmd(t)
    return LuaEngine_(t)
  end

  -- Bootstrap fresh require (used only to load util the first time)
  local function bootstrap_require_fresh(name)
    package.loaded[name] = nil
    local ok, mod = pcall(require, name)
    if not ok then
      error("Failed to require '" .. name .. "': " .. tostring(mod), 2)
    end
    return mod
  end

  -- Load runtime utilities (fresh)
  local make_util = bootstrap_require_fresh("main_lua_example_v1_4_util")
  if type(make_util) ~= "function" then
    error("main_lua_example_v1_4_util.lua must return function(ctx) -> util", 2)
  end

  local util = make_util({
    Engine = Engine,
    state = state,
    cmd = cmd,
    scene_module_default = "game_scene_demo_" .. "0000",
    gfx_module = "gfx",
    sandbox_generator_module = "main_lua_example_v1_4_generating",
  })

  -- Shared defaults (you can later move these into scene-specific defaults if desired)
  util.ensure_shared_state_defaults()

  -- Runtime objects (active)
  local gfx = util.load_gfx()
  local scene = util.load_scene(gfx)

  local function ensure_resources()
    return scene.ensure_resources()
  end

  local M = {}

  function M.Init()
    util.log(("Init (ReloadCount=%d)"):format(Engine.ReloadCount or 0))
    ensure_resources()
  end

  function M.Update(dt)
    -- Fast direct dispatch. If desired, wrap with pcall later.
    return scene.update(dt)
  end

  function M.OnReload()
    util.log("OnReload called (Engine.State persists)")

    -- 1) Regenerate C++ bridge header (Sandbox.h)
    util.run_sandbox_generator()

    -- 2) Stage fresh util (so helper changes hot-reload too)
    local new_util
    local ok_util, err_util = pcall(function()
      local make_new_util = util.require_fresh("main_lua_example_v1_4_util")
      if type(make_new_util) ~= "function" then
        error("main_lua_example_v1_4_util.lua must return function(ctx) -> util", 2)
      end

      new_util = make_new_util({
        Engine = Engine,
        state = state,
        cmd = cmd,
        scene_module_default = "game_scene_demo_0000",
        gfx_module = "gfx",
        sandbox_generator_module = "main_lua_example_v1_4_generating",
      })

      new_util.ensure_shared_state_defaults()
    end)

    if not ok_util then
      util.log("util reload failed: " .. tostring(err_util))
      return
    end

    -- 3) Stage fresh gfx
    local new_gfx
    local ok_gfx, err_gfx = pcall(function()
      new_gfx = new_util.load_gfx()
    end)

    if not ok_gfx then
      util.log("gfx reload failed: " .. tostring(err_gfx))
      return
    end

    -- 4) Stage fresh scene using fresh gfx
    local new_scene
    local ok_scene, err_scene = pcall(function()
      new_scene = new_util.load_scene(new_gfx)
    end)

    if not ok_scene then
      util.log("scene reload failed: " .. tostring(err_scene))
      return
    end

    -- 5) Stage resource re-ensure BEFORE commit (atomic-ish behavior)
    local ok_res, err_res = pcall(function()
      new_scene.ensure_resources()
    end)

    if not ok_res then
      util.log("scene.ensure_resources() after reload failed: " .. tostring(err_res))
      return
    end

    -- 6) Commit staged runtime only after all steps succeeded
    util = new_util
    gfx = new_gfx
    scene = new_scene

    util.log("util reloaded")
    util.log("gfx reloaded")
    util.log("scene reloaded: " .. tostring(Engine.SceneModule or util.SCENE_MODULE_DEFAULT))
    util.log("scene resources ensured")
  end

  function M.Reset()
    util.log("Reset called")

    -- Keep your current reset behavior
    state.t = 0
    state.counter = 0

    -- Optional scene-specific reset hook
    if scene and type(scene.reset) == "function" then
      local ok, err = pcall(function() scene.reset() end)
      if not ok then
        util.log("scene.reset failed: " .. tostring(err))
      end
    end
  end

  function M.Shutdown()
    util.log("Shutdown called")

    -- Optional scene-specific shutdown hook
    if scene and type(scene.shutdown) == "function" then
      local ok, err = pcall(function() scene.shutdown() end)
      if not ok then
        util.log("scene.shutdown failed: " .. tostring(err))
      end
    end
  end

  return M
end