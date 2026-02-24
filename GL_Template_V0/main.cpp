
// main_updated.cpp
// C++20, Windows console hot-reload + Lua-driven rendering

#include "FindScriptsFolder.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>

#ifdef _WIN32
#include <conio.h> // _kbhit, _getch
#endif

#include "Engine.h"
#include "Sandbox.h" // <-- generated bridge header (updated)

namespace fs = std::filesystem;

namespace Lua_helpers
{
    static std::string ToLuaPath(const fs::path& p) { return p.generic_string(); }

    static std::string Lower(std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    // ----------- tiny hash (FNV-1a) -----------
    static void HashBytes(uint64_t& h, const void* data, size_t n) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; ++i) {
            h ^= uint64_t(p[i]);
            h *= 1099511628211ull;
        }
    }
    static void HashStr(uint64_t& h, const std::string& s) { HashBytes(h, s.data(), s.size()); }

    // Fingerprint all *.lua recursively by hashing relative path + file bytes
    static std::optional<uint64_t> ComputeLuaFingerprint(const fs::path& scriptsDir) {
        std::error_code ec;
        if (!fs::exists(scriptsDir, ec) || !fs::is_directory(scriptsDir, ec)) return std::nullopt;

        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(scriptsDir, ec), end; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec)) { ec.clear(); continue; }
            fs::path p = it->path();
            if (Lower(p.extension().string()) == ".lua") files.push_back(p);
        }
        std::sort(files.begin(), files.end());

        uint64_t h = 1469598103934665603ull;

        for (const auto& p : files) {
            auto rel = fs::relative(p, scriptsDir, ec);
            if (ec) { ec.clear(); continue; }

            HashStr(h, rel.generic_string());

            std::ifstream f(p, std::ios::binary);
            if (!f) continue;

            char buf[4096];
            while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
                HashBytes(h, buf, (size_t)f.gcount());
            }
        }
        return h;
    }

    enum class KeyAction { None, Quit, ToggleHotReload, ReloadNow, HardReset, SoftReset };

    static KeyAction PollKey()
    {
#ifdef _WIN32
        if (!_kbhit()) return KeyAction::None;
        int c = _getch();

        // Function keys come as prefix 0 or 224, then scan code.
        if (c == 0 || c == 224) {
            int sc = _getch();
            // F5 / F6 / F7 are usually scan codes 63/64/65 in Windows console (_getch)
            if (sc == 63) return KeyAction::ReloadNow;
            if (sc == 64) return KeyAction::HardReset;
            if (sc == 65) return KeyAction::SoftReset;
            return KeyAction::None;
        }

        if (c == 'q' || c == 'Q') return KeyAction::Quit;
        if (c == 'h' || c == 'H') return KeyAction::ToggleHotReload;
        if (c == 'r' || c == 'R') return KeyAction::ReloadNow; // fallback manual reload
        if (c == 't' || c == 'T') return KeyAction::HardReset; // fallback
        if (c == 'x' || c == 'X') return KeyAction::SoftReset; // fallback
#else
        (void)0;
#endif
        return KeyAction::None;
    }
}

// -------------------------
// Runtime assets (owned by C++)
// -------------------------
struct Mesh
{
    std::vector<Engine_::Vertex3D> verts;
    std::vector<uint32_t> idx;
};

struct RuntimeAssets
{
    std::unordered_map<std::string, Engine_::Image> textures;
    std::unordered_map<std::string, Mesh> meshes;
};

// -------------------------
// LuaHost: hot reload + bridge + Lua script instance
// -------------------------
class LuaHost {
public:
    struct Config {
        fs::path scriptsDir;
        std::string entryModule = "main_lua_example_v1_4"; // scripts/main_lua_example_v1_4.lua
        bool hotReloadEnabled = true;
        int pollMs = 200;
    };

    explicit LuaHost(Config cfg, RuntimeAssets& assets)
        : cfg_(std::move(cfg)), assets_(assets) {
    }

    bool Init() {
        std::cout.setf(std::ios::unitbuf);

        if (!fs::exists(cfg_.scriptsDir)) {
            std::cerr << "[C++] ERROR scriptsDir does not exist: " << cfg_.scriptsDir.string() << "\n";
            return false;
        }

        lua_ = sol::state{};
        lua_.open_libraries(
            sol::lib::base,
            sol::lib::package,
            sol::lib::math,
            sol::lib::string,
            sol::lib::table,
            sol::lib::io,
            sol::lib::os
        );

        BuildEngineTable();
        ConfigurePackagePath();
        InstallRequireTracker(); // MUST be before requiring the entry module

        if (!LoadScriptInstance()) return false;

        fp_ = Lua_helpers::ComputeLuaFingerprint(cfg_.scriptsDir);
        lastPoll_ = std::chrono::steady_clock::now();

        Call0(init_, "Init");
        return true;
    }

    void Shutdown() { Call0(shutdown_, "Shutdown"); }

    void Tick(double dt) {
        didPresentThisFrame_ = false;

        LuaEngine_["Dt"] = dt;

        if (cfg_.hotReloadEnabled) PollHotReload();

        if (update_.valid()) {
            sol::protected_function_result pr = update_(dt);
            if (!pr.valid()) {
                sol::error err = pr;
                std::cerr << "[Lua] Update error: " << err.what() << "\n";
            }
        }

        // Safety fallback: if Lua forgot to present, present anyway.
        // This makes iteration smoother (no "black screen" while you’re editing Lua).
        if (!didPresentThisFrame_) {
            Engine_::flush_to_screen(true);
        }
    }

    void ToggleHotReload() {
        cfg_.hotReloadEnabled = !cfg_.hotReloadEnabled;
        std::cout << "[C++] HotReload " << (cfg_.hotReloadEnabled ? "ON" : "OFF") << "\n";
    }

    void ReloadNow() {
        std::cout << "[C++] Manual reload\n";
        if (Reload()) {
            fp_ = Lua_helpers::ComputeLuaFingerprint(cfg_.scriptsDir);
            std::cout << "[C++] Reload OK\n";
        }
        else {
            std::cout << "[C++] Reload FAILED (keeping old instance)\n";
        }
    }

    bool HardReset()
    {
        std::cout << "[C++] HARD RESET (new Lua VM)\n";

        Call0(shutdown_, "Shutdown(old)");

        onReload_ = sol::protected_function{};
        shutdown_ = sol::protected_function{};
        update_ = sol::protected_function{};
        init_ = sol::protected_function{};
        instance_ = sol::table{};
        factory_ = sol::protected_function{};
        LuaEngine_ = sol::table{};

        lua_ = sol::state{};
        lua_.open_libraries(
            sol::lib::base,
            sol::lib::package,
            sol::lib::math,
            sol::lib::string,
            sol::lib::table,
            sol::lib::io,
            sol::lib::os
        );

        BuildEngineTable();
        ConfigurePackagePath();
        InstallRequireTracker();

        if (!LoadScriptInstance())
            return false;

        LuaEngine_["ReloadCount"] = 0;

        Call0(init_, "Init(new VM)");

        fp_ = Lua_helpers::ComputeLuaFingerprint(cfg_.scriptsDir);
        lastPoll_ = std::chrono::steady_clock::now();
        return true;
    }

    void SoftReset()
    {
        std::cout << "[C++] SOFT RESET (clear Engine.State)\n";

        sol::object stObj = LuaEngine_["State"];
        if (stObj.valid() && stObj.get_type() == sol::type::table) {
            sol::table st = stObj.as<sol::table>();
            std::vector<sol::object> keys;
            for (auto& kv : st) keys.push_back(kv.first);
            for (auto& k : keys) st[k] = sol::nil;
        }
        else {
            LuaEngine_["State"] = lua_.create_table();
        }

        sol::object rObj = instance_["Reset"];
        if (rObj.valid() && rObj.get_type() == sol::type::function) {
            sol::protected_function reset = rObj.as<sol::protected_function>();
            sol::protected_function_result pr = reset();
            if (!pr.valid()) {
                sol::error err = pr;
                std::cerr << "[Lua] Reset error: " << err.what() << "\n";
            }
            return;
        }

        Call0(init_, "Init(soft reset)");
    }

private:
    void ExposeKeyConstants()
    {
        // Convenience: expose engine keycodes to Lua
        LuaEngine_["KEY_ESCAPE"] = Engine_::KEY_ESCAPE;

        LuaEngine_["KEY_1"] = Engine_::KEY_1;
        LuaEngine_["KEY_2"] = Engine_::KEY_2;
        LuaEngine_["KEY_3"] = Engine_::KEY_3;
        LuaEngine_["KEY_4"] = Engine_::KEY_4;
        LuaEngine_["KEY_5"] = Engine_::KEY_5;

        LuaEngine_["KEY_B"] = Engine_::KEY_B;
        LuaEngine_["KEY_F"] = Engine_::KEY_F;
        LuaEngine_["KEY_S"] = Engine_::KEY_S;

        LuaEngine_["KEY_LEFT_BRACKET"] = Engine_::KEY_LEFT_BRACKET;
        LuaEngine_["KEY_RIGHT_BRACKET"] = Engine_::KEY_RIGHT_BRACKET;
        LuaEngine_["KEY_MINUS"] = Engine_::KEY_MINUS;
        LuaEngine_["KEY_EQUAL"] = Engine_::KEY_EQUAL;
    }

    void BuildEngineTable() {
        LuaEngine_ = lua_.create_named_table("Engine");

        if (!LuaEngine_["State"].valid())
            LuaEngine_["State"] = lua_.create_table();

        LuaEngine_["ReloadCount"] = 0;
        LuaEngine_["Dt"] = 0.0;
        LuaEngine_["version"] = "0.5";

        // Let Lua know where scripts live (used by generator to write Sandbox.h)
        LuaEngine_["ScriptsDir"] = Lua_helpers::ToLuaPath(cfg_.scriptsDir);
        LuaEngine_["SandboxOutPath"] = Lua_helpers::ToLuaPath(cfg_.scriptsDir.parent_path() / "Sandbox.h");


        ExposeKeyConstants();

        LuaEngine_.set_function("cpp_log", [](const std::string& s) {
            std::cout << "[Lua] " << s << "\n";
            });

        LuaEngine_.set_function("rand01", []() -> double {
            static uint32_t x = 123456789u;
            x = 1664525u * x + 1013904223u;
            return (x & 0xFFFFFF) / double(0x1000000);
            });

        // Default callbacks (Engine_::*)
        EngineLuaBridge_::bind_engine_defaults();

        // Track whether Lua presented this frame (so C++ can safely fallback)
        EngineLuaBridge_::cb_flush_to_screen = [this](bool apply_postprocess) {
            didPresentThisFrame_ = true;
            Engine_::flush_to_screen(apply_postprocess);
            };

        // --- named texture support ---
        EngineLuaBridge_::cb_tex_make_checker = [this](const std::string& name, int w, int h, int cell) -> bool
            {
                if (w <= 0 || h <= 0 || cell <= 0) return false;
                assets_.textures[name] = Engine_::make_checker_rgba(w, h, cell);
                return assets_.textures[name].valid();
            };

        EngineLuaBridge_::cb_tex_load = [this](const std::string& name, const std::string& filepath) -> bool
            {
                Engine_::Image img = Engine_::load_image_rgba(filepath);
                if (!img.valid()) return false;
                assets_.textures[name] = std::move(img);
                return true;
            };

        EngineLuaBridge_::cb_tex_delete = [this](const std::string& name) -> bool
            {
                return assets_.textures.erase(name) > 0;
            };

        EngineLuaBridge_::cb_tex_exists = [this](const std::string& name) -> bool
            {
                auto it = assets_.textures.find(name);
                return it != assets_.textures.end() && it->second.valid();
            };

        EngineLuaBridge_::cb_tex_from_framebuffer = [this](const std::string& name) -> bool
            {
                const int W = Engine_::fb_width();
                const int H = Engine_::fb_height();
                if (W <= 0 || H <= 0) return false;

                const uint8_t* src = Engine_::fb_rgba();
                if (!src) return false;

                Engine_::Image img;
                img.w = W;
                img.h = H;
                img.rgba.assign(src, src + (size_t)W * (size_t)H * 4u);

                assets_.textures[name] = std::move(img);
                return true;
            };

        // --- draw textured tri using a named texture ---
        EngineLuaBridge_::cb_draw_triangle_textured_named =
            [this](Engine_::Vec2 a, Engine_::Vec2 ua,
                Engine_::Vec2 b, Engine_::Vec2 ub,
                Engine_::Vec2 c, Engine_::Vec2 uc,
                const std::string& texture_name,
                Engine_::Color tint)
            {
                auto it = assets_.textures.find(texture_name);
                if (it == assets_.textures.end())
                    throw std::runtime_error("Unknown texture_name: " + texture_name);
                Engine_::draw_triangle_textured(a, ua, b, ub, c, uc, it->second, tint);
            };

        // --- mesh registry ---
        EngineLuaBridge_::cb_mesh_make_cube = [this](const std::string& name, float size) -> bool
            {
                if (size <= 0.0f) size = 1.0f;

                using V = Engine_::Vertex3D;

                // NOTE: This is the same "8 verts + 12 tris" demo cube you had in C++.
                // It’s not perfect UV mapping (shared vertices across faces), but it matches your old look.
                std::vector<V> cubeVerts =
                {
                    // front
                    {{-size,-size, size}, {1,0,0}, {0,1}},
                    {{ size,-size, size}, {0,1,0}, {1,1}},
                    {{ size, size, size}, {0,0,1}, {1,0}},
                    {{-size, size, size}, {1,1,0}, {0,0}},
                    // back
                    {{-size,-size,-size}, {1,0,1}, {1,1}},
                    {{ size,-size,-size}, {0,1,1}, {0,1}},
                    {{ size, size,-size}, {1,1,1}, {0,0}},
                    {{-size, size,-size}, {0.5f,0.5f,0.5f}, {1,0}},
                };

                std::vector<uint32_t> cubeIdx =
                {
                    0,1,2,  0,2,3, // front
                    1,5,6,  1,6,2, // right
                    5,4,7,  5,7,6, // back
                    4,0,3,  4,3,7, // left
                    3,2,6,  3,6,7, // top
                    4,5,1,  4,1,0  // bottom
                };

                Mesh m;
                m.verts = std::move(cubeVerts);
                m.idx = std::move(cubeIdx);
                assets_.meshes[name] = std::move(m);
                return true;
            };

        EngineLuaBridge_::cb_mesh_delete = [this](const std::string& name) -> bool
            {
                return assets_.meshes.erase(name) > 0;
            };

        EngineLuaBridge_::cb_mesh_exists = [this](const std::string& name) -> bool
            {
                return assets_.meshes.find(name) != assets_.meshes.end();
            };

        EngineLuaBridge_::cb_draw_mesh_named =
            [this](const std::string& mesh_name, const Engine_::Mat4& mvp, const std::string& texture_name, bool enable_depth_test)
            {
                auto it = assets_.meshes.find(mesh_name);
                if (it == assets_.meshes.end())
                    throw std::runtime_error("Unknown mesh_name: " + mesh_name);

                const Engine_::Image* tex = nullptr;
                if (!texture_name.empty()) {
                    auto tt = assets_.textures.find(texture_name);
                    if (tt == assets_.textures.end())
                        throw std::runtime_error("Unknown texture_name: " + texture_name);
                    tex = &tt->second;
                }

                const Mesh& m = it->second;
                Engine_::draw_mesh(
                    m.verts.data(), (int)m.verts.size(),
                    m.idx.data(), (int)m.idx.size(),
                    mvp,
                    tex,
                    enable_depth_test
                );
            };

        // --- postprocess setters (Lua owns the knobs) ---
        EngineLuaBridge_::cb_pp_set_bloom = [](bool enabled, float threshold, float intensity, int downsample, float sigma)
            {
                Engine_::PostProcessSettings s = Engine_::postprocess();
                s.bloom.enabled = enabled;
                s.bloom.threshold = std::clamp(threshold, 0.0f, 1.0f);
                s.bloom.intensity = std::max(0.0f, intensity);
                s.bloom.downsample = std::max(1, downsample);
                s.bloom.sigma = std::max(0.0f, sigma);
                Engine_::set_postprocess(s);
            };

        EngineLuaBridge_::cb_pp_set_tone = [](bool enabled, float exposure, float gamma)
            {
                Engine_::PostProcessSettings s = Engine_::postprocess();
                s.tone.enabled = enabled;
                s.tone.exposure = std::max(0.0f, exposure);
                s.tone.gamma = std::max(0.01f, gamma);
                Engine_::set_postprocess(s);
            };

        EngineLuaBridge_::cb_pp_reset = []()
            {
                Engine_::PostProcessSettings s{};
                Engine_::set_postprocess(s);
            };

        // Register dispatcher
        EngineLuaBridge_::register_into(lua_, "LuaEngine_");

        // Also expose it on Engine table for convenience
        LuaEngine_["cmd"] = lua_["LuaEngine_"];
    }

    void ConfigurePackagePath() {
        sol::table package = lua_["package"];
        std::string path = package["path"];

        const std::string s = Lua_helpers::ToLuaPath(cfg_.scriptsDir);
        path += ";" + s + "/?.lua";
        path += ";" + s + "/?/init.lua";

        package["path"] = path;
    }

    void InstallRequireTracker() {
        const char* code = R"(
            Engine._required = Engine._required or {}
            local old_require = require
            function require(name)
                Engine._required[name] = true
                return old_require(name)
            end
        )";
        auto r = lua_.safe_script(code, &sol::script_pass_on_error);
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[Lua] require tracker install error: " << err.what() << "\n";
        }
    }

    bool LoadScriptInstance() {
        sol::protected_function requireFn = lua_["require"];
        sol::protected_function_result pr = requireFn(cfg_.entryModule);
        if (!pr.valid()) {
            sol::error err = pr;
            std::cerr << "[Lua] require(\"" << cfg_.entryModule << "\") error: " << err.what() << "\n";
            return false;
        }

        sol::object obj = pr;
        if (obj.get_type() != sol::type::function) {
            std::cerr << "[Lua] Entry module must return a function (factory)\n";
            return false;
        }

        factory_ = obj.as<sol::protected_function>();

        sol::protected_function_result prInst = factory_(LuaEngine_);
        if (!prInst.valid()) {
            sol::error err = prInst;
            std::cerr << "[Lua] factory(Engine) error: " << err.what() << "\n";
            return false;
        }

        sol::object instObj = prInst;
        if (instObj.get_type() != sol::type::table) {
            std::cerr << "[Lua] factory must return a table (instance)\n";
            return false;
        }

        instance_ = instObj.as<sol::table>();
        BindFunctions();
        return true;
    }

    void BindFunctions() {
        init_ = instance_["Init"];
        update_ = instance_["Update"];
        shutdown_ = instance_["Shutdown"];
        onReload_ = instance_["OnReload"];
    }

    void ClearTrackedPackageLoaded() {
        sol::table package = lua_["package"];
        sol::table loaded = package["loaded"];

        sol::table req = LuaEngine_["_required"];
        if (req.valid()) {
            for (auto& kv : req) {
                if (kv.first.get_type() == sol::type::string) {
                    std::string name = kv.first.as<std::string>();
                    loaded[name] = sol::nil;
                }
            }
            req.clear();
        }

        loaded[cfg_.entryModule] = sol::nil;
    }

    bool Reload() {
        sol::table oldInstance = instance_;
        auto oldInit = init_;
        auto oldUpdate = update_;
        auto oldShutdown = shutdown_;
        auto oldOnReload = onReload_;
        auto oldFactory = factory_;

        ClearTrackedPackageLoaded();

        sol::protected_function newFactory;
        sol::table newInstance;
        sol::protected_function newInit, newUpdate, newShutdown, newOnReload;

        {
            sol::protected_function requireFn = lua_["require"];
            sol::protected_function_result pr = requireFn(cfg_.entryModule);
            if (!pr.valid()) {
                sol::error err = pr;
                std::cerr << "[Lua] require reload error: " << err.what() << "\n";
                instance_ = oldInstance;
                init_ = oldInit; update_ = oldUpdate; shutdown_ = oldShutdown; onReload_ = oldOnReload; factory_ = oldFactory;
                return false;
            }

            sol::object obj = pr;
            if (obj.get_type() != sol::type::function) {
                std::cerr << "[Lua] Reloaded entry did not return factory function\n";
                instance_ = oldInstance;
                init_ = oldInit; update_ = oldUpdate; shutdown_ = oldShutdown; onReload_ = oldOnReload; factory_ = oldFactory;
                return false;
            }

            newFactory = obj.as<sol::protected_function>();

            sol::protected_function_result prInst = newFactory(LuaEngine_);
            if (!prInst.valid()) {
                sol::error err = prInst;
                std::cerr << "[Lua] new factory(Engine) error: " << err.what() << "\n";
                instance_ = oldInstance;
                init_ = oldInit; update_ = oldUpdate; shutdown_ = oldShutdown; onReload_ = oldOnReload; factory_ = oldFactory;
                return false;
            }

            sol::object instObj = prInst;
            if (instObj.get_type() != sol::type::table) {
                std::cerr << "[Lua] new factory did not return instance table\n";
                instance_ = oldInstance;
                init_ = oldInit; update_ = oldUpdate; shutdown_ = oldShutdown; onReload_ = oldOnReload; factory_ = oldFactory;
                return false;
            }

            newInstance = instObj.as<sol::table>();
            newInit = newInstance["Init"];
            newUpdate = newInstance["Update"];
            newShutdown = newInstance["Shutdown"];
            newOnReload = newInstance["OnReload"];
        }

        Call0(oldShutdown, "Shutdown(old)");

        factory_ = newFactory;
        instance_ = newInstance;
        init_ = newInit; update_ = newUpdate; shutdown_ = newShutdown; onReload_ = newOnReload;

        int rc = LuaEngine_["ReloadCount"].get_or(0);
        LuaEngine_["ReloadCount"] = rc + 1;

        Call0(init_, "Init(new)");
        Call0(onReload_, "OnReload(new)");
        return true;
    }

    void PollHotReload() {
        using namespace std::chrono;
        auto now = steady_clock::now();
        if (now - lastPoll_ < milliseconds(cfg_.pollMs)) return;
        lastPoll_ = now;

        auto newFp = Lua_helpers::ComputeLuaFingerprint(cfg_.scriptsDir);
        if (!newFp.has_value()) return;

        if (!fp_.has_value() || *newFp != *fp_) {
            std::cout << "[C++] change detected -> reload\n";
            if (Reload()) {
                fp_ = newFp;
                std::cout << "[C++] Hot reload OK\n";
            }
            else {
                std::cout << "[C++] Hot reload FAILED (keeping old)\n";
            }
        }
    }

    void Call0(sol::protected_function& fn, const char* label) {
        if (!fn.valid()) return;
        sol::protected_function_result pr = fn();
        if (!pr.valid()) {
            sol::error err = pr;
            std::cerr << "[Lua] " << label << " error: " << err.what() << "\n";
        }
    }

private:
    Config cfg_;
    RuntimeAssets& assets_;

    sol::state lua_;
    sol::table LuaEngine_;
    sol::protected_function factory_;
    sol::table instance_;

    sol::protected_function init_;
    sol::protected_function update_;
    sol::protected_function shutdown_;
    sol::protected_function onReload_;

    bool didPresentThisFrame_ = false;

    std::optional<uint64_t> fp_;
    std::chrono::steady_clock::time_point lastPoll_{};
};

int main()
{
    std::cout << "[C++] step_by_step (Lua-driven)\n";

    // -------------------------
    // Engine init (same settings as your old C++ scene)
    // -------------------------
    Engine_::Config cfg;
    cfg.display_w = 960;
    cfg.display_h = 540;
    cfg.fb_w = 1920 / 2;
    cfg.fb_h = 1080 / 2;

    cfg.resizable = true;
    cfg.vsync = false;
    cfg.linear_filter = false;
    cfg.hidden_window = false;
    cfg.headless = false;

    if (!Engine_::init(cfg)) {
        std::cerr << "Engine init failed.\n";
        return 1;
    }

    Engine_::set_capture_filepath("captures");
    Engine_::set_frame_index(0);

    // Enable depth for 3D
    Engine_::enable_depth(true);

    // Start with bloom/tone OFF (match your old demo), Lua can enable/tune via pp_set_* ops
    Engine_::PostProcessSettings pp = Engine_::postprocess();
    pp.bloom.enabled = false;
    pp.tone.enabled = false;
    Engine_::set_postprocess(pp);

    // -------------------------
    // Lua init
    // -------------------------
    fs::path scriptsDir = find_scripts_folder();
    std::cout << "[C++] scriptsDir: " << scriptsDir << "\n";
    std::cout << "[C++] Console keys: F5=reload, F6=hard reset, F7=soft reset, "
        "R=reload, T=hard reset, X=soft reset, H=toggle hot reload, Q=quit\n";

    RuntimeAssets assets;

    LuaHost::Config lcfg;
    lcfg.scriptsDir = scriptsDir;
    lcfg.entryModule = "main_lua_example_v1_4";
    lcfg.hotReloadEnabled = true;
    lcfg.pollMs = 200;

    LuaHost host(lcfg, assets);
    if (!host.Init()) {
        return 1;
    }

    auto last = std::chrono::steady_clock::now();
    bool running = true;

    while (running)
    {
        // dt
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (dt > 0.1) dt = 0.1;

        // console hot-reload keys
        switch (Lua_helpers::PollKey()) {
        case Lua_helpers::KeyAction::Quit: running = false; break;
        case Lua_helpers::KeyAction::ToggleHotReload: host.ToggleHotReload(); break;
        case Lua_helpers::KeyAction::ReloadNow: host.ReloadNow(); break;
        case Lua_helpers::KeyAction::HardReset:
            if (!host.HardReset())
                std::cout << "[C++] HardReset FAILED (see Lua error above)\n";
            else
                std::cout << "[C++] HardReset OK\n";
            break;
        case Lua_helpers::KeyAction::SoftReset:
            host.SoftReset();
            break;
        default: break;
        }

        host.Tick(dt);

        running = running && !Engine_::should_close();
    }

    host.Shutdown();
    Engine_::shutdown();
    return 0;
}
