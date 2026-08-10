#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <thread>
#include <unordered_map>

#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include <elden-x/params.hpp>
#include <elden-x/singletons.hpp>
#include <elden-x/utils/modutils.hpp>

namespace custom_drop_fxrs
{

static std::filesystem::path dll_folder;

static constexpr uintptr_t get_weapon_fxr_rva = 0xee35c0;
static constexpr const char *config_filename = "custom_drop_fxrs.yaml";

// wepType -> fxr ID, loaded from config_filename
static std::unordered_map<uint32_t, uint16_t> custom_drop_fxrs;

using get_weapon_fxr_fn = uint32_t*(uint32_t*, uint16_t);
static get_weapon_fxr_fn *get_weapon_fxr_original = nullptr;

// loads wepType -> fxr ID from config_filename next to the dll
static void load_custom_drop_fxrs()
{
    auto path = dll_folder / config_filename;
    if (!std::filesystem::exists(path))
    {
        spdlog::error("[custom_drop_fxrs] config not found: {}", path.string());
        return;
    }

    YAML::Node root;
    try
    {
        root = YAML::LoadFile(path.string());
    }
    catch (const YAML::Exception &e)
    {
        spdlog::error("[custom_drop_fxrs] failed to parse config: {}", e.what());
        return;
    }

    for (const auto &entry : root)
    {
        auto wep_type = entry.first.as<uint32_t>();
        auto fxrid = entry.second.as<uint16_t>();
        custom_drop_fxrs[wep_type] = std::move(fxrid);
    }

    spdlog::info("[custom_drop_fxrs] loaded {} wepType->fxr pairs from config", custom_drop_fxrs.size());
}

static uint32_t* get_weapon_fxr_detour(uint32_t* ret, uint16_t wep_type)
{
    auto it = custom_drop_fxrs.find(wep_type);
    if (it != custom_drop_fxrs.end())
    {
        *ret = it->second;
        return ret;
    }

    return get_weapon_fxr_original(ret, wep_type);
}

static std::optional<uintptr_t> rva_to_va(uint32_t rva)
{
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"nightreign.exe"));
    if (base == 0)
        return std::nullopt;
    return base + rva;
}

static void install_hooks()
{
    auto va = rva_to_va(get_weapon_fxr_rva);
    if (!va)
    {
        spdlog::error("[custom_drop_fxrs] could not resolve get_weapon_fxr_rva");
        return;
    }

    modutils::hook<get_weapon_fxr_fn>(
        {.address = reinterpret_cast<void *>(*va)},
        get_weapon_fxr_detour, 
        get_weapon_fxr_original
    );
}

static void setup_logger()
{
    // elden-x builds spdlog with the default logger disabled, so install one.
    auto logger = std::make_shared<spdlog::logger>("custom_drop_fxrs");
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%l]%$ %v");
    logger->sinks().push_back(std::make_shared<spdlog::sinks::daily_file_sink_st>(
        (dll_folder / "logs" / "custom_drop_fxrs.log").string(), 0, 0, false, 5));
    logger->sinks().push_back(std::make_shared<spdlog::sinks::stdout_color_sink_st>());
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);
}

static void setup()
{
    modutils::initialize();
    er::FD4::find_singletons();

    load_custom_drop_fxrs();
    install_hooks();
    modutils::enable_hooks();

    spdlog::info("[custom_drop_fxrs] is now active!");
}

}

bool WINAPI DllMain(HINSTANCE dll_instance, unsigned int reason, void *reserved)
{
    using namespace custom_drop_fxrs;

    if (reason == DLL_PROCESS_ATTACH)
    {
        wchar_t dll_filename[MAX_PATH] = {0};
        GetModuleFileNameW(dll_instance, dll_filename, MAX_PATH);
        dll_folder = std::filesystem::path(dll_filename).parent_path();

        setup_logger();

        std::thread([] {
            try
            {
                setup();
            }
            catch (const std::exception &e)
            {
                spdlog::error("[custom_drop_fxrs] {}", e.what());
                modutils::deinitialize();
            }
        }).detach();
    }

    return true;
}
