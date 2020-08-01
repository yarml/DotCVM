#include <dotcvm/core/modules.hpp>
#include <dotcvm/utils/log.hpp>
#include <dotcvm/utils/config.hpp>
#include <dotcvm/utils/string.hpp>
#include <vector>
#include <filesystem>
#include <dlfcn.h>
#include <sstream>

static std::vector<module> s_modules;

static std::vector<module*> s_modules_first;
static std::vector<module*> s_modules_cpus;
static std::vector<module*> s_modules_normal;
static std::vector<module*> s_modules_last;

static module s_null_module;

static void remove_modules(std::vector<module*>& modules_to_remove)
{
    for(uint i = 0; i < s_modules.size(); i++)
        for(uint j = 0; j < modules_to_remove.size(); j++)
            if(s_modules[i].id == modules_to_remove[j]->id)
            {
                warn("Removing module named: " << s_modules[i].name << " with id: " << s_modules[i].id);
                if(s_modules[i].lib_handle != nullptr)
                    dlclose(s_modules[i].lib_handle);
                s_modules.erase(s_modules.begin() + i);
                i--;
            }
    modules_to_remove.clear();
}

static bool module_exist(uint id)
{
    for(module m : s_modules)
        if(m.id == id)
            return true;
    return false;
}

static module& get_module(uint id)
{
    for(module& m : s_modules)
        if(m.id == id)
            return m;
    warn("Module with id: " << id << "not found returning null module");
    return s_null_module;
}

// This checks wether module with id0 wants to connect to module with id1
static bool module_connect_to(uint id0, uint id1)
{
    module& connect_to_module = get_module(id0);
    if(connect_to_module.connection_mode == module_connection_mode::ACCEPT_ALL)
        return true;
    if(connect_to_module.connection_mode == module_connection_mode::DECLINE_ALL)
        return false;
    for(uint connection : connect_to_module.connect_list)
        if(connection == id1)
            return true;
    return false;
}

void load_modules()
{
    std::vector<module_report> modules_reports;
    if(!std::filesystem::exists(MODULES_DIR))
        std::filesystem::create_directory(MODULES_DIR);
    uint uid_count = 0;
    for(auto& entry : std::filesystem::directory_iterator(MODULES_DIR))
        if(entry.is_directory() && std::filesystem::exists(entry.path().string().append("/device.dpf")))
        {
            s_modules.push_back(module{.uid = uid_count, .config = read_config_file(entry.path().string().append("/device.dpf"))});
            uid_count++;
            log("Found module at: " << entry.path().string().append("/device.dpf"));
        }
    for(int i = 0; i < s_modules.size(); i++)
    {
        module& m = s_modules[i];
        if(m.config.contains("name"))
            m.name = m.config["name"];
        if(m.config.contains("lib_file"))
            m.lib_file = m.config["lib_file"];
        if(m.config.contains("clock_mode"))
            m.clock_mode = (module_clock)(std::stoi(m.config["clock_mode"]));
        if(m.config.contains("connection_mode"))
            m.connection_mode = (module_connection_mode)(std::stoi(m.config["connection_mode"]));
        if(m.config.contains("require"))
            m.require_list = string_to_uint_array(m.config["require"]);
        if(m.config.contains("connect_to"))
            m.connect_list = string_to_uint_array(m.config["connect_to"]);
        if(m.config.contains("id"))
            m.id = std::stoi(m.config["id"]);
        else
        {
            warn("module: " << m.name << " is missing 'id' property, ignoring it");
            s_modules.erase(s_modules.begin() + i);
            i--;   
        }
    }
    {
        std::vector<module*> modules_to_remove;
        for(module& m : s_modules)
            for(module& m1 : s_modules)
                if(m.id == m1.id && m.uid != m1.uid)
                {
                    modules_to_remove.push_back(&m);
                    modules_to_remove.push_back(&m1);
                    warn("Found duplicate of module with id: " << m.id << ", on modules named: " << m.name << " and " << m1.name);
                    debug(s_modules.size());
                }
        remove_modules(modules_to_remove);

        for(module& m : s_modules)
        {
            std::stringstream file_path_stream;
            file_path_stream << "modules/lib/";
            file_path_stream << m.lib_file;
#ifdef __linux__
            file_path_stream << ".linux.so"; 
#endif
#ifdef __WIN32
            file_path_stream << ".win.so";
#endif
#ifdef __APPLE__
            file_path_stream << ".mac.so";
#endif
            debug(m.name << ":" << m.id << ", lib file = " << file_path_stream.str().c_str());
            m.lib_handle = dlopen(file_path_stream.str().c_str(), RTLD_LAZY);
            if(m.lib_handle != nullptr)
            {
                dlerror(); // Make sure that there is no error waiting to be displayed
                m.fp_create_device = (device_ptr(*)())(dlsym(m.lib_handle, "create_device"));
                if(dlerror() != nullptr)
                {
                    warn("Cannot load module named: " << m.name << " with id: " << m.id << ". Can't load create_device function.");
                    modules_to_remove.push_back(&m);   
                }
                m.fp_destroy_device = (void(*)(device_ptr))(dlsym(m.lib_handle, "destroy_device"));
                if(dlerror() != nullptr)
                {
                    warn("Cannot load module named: " << m.name << " with id: " << m.id << ". Can't load destroy_device(device_ptr) function.");
                    modules_to_remove.push_back(&m);   
                }
                m.fp_report     = (void(*)(uint,uint))  (dlsym(m.lib_handle, "report"));
                m.fp_pre_clock  = (void(*)())           (dlsym(m.lib_handle, "pre_clock"));
                m.fp_clock      = (void(*)())           (dlsym(m.lib_handle, "clock"));
                m.fp_post_clock = (void(*)())           (dlsym(m.lib_handle, "post_clock"));
            }
            else
            {
                warn("Cannot load module named: " << m.name << " with id: " << m.id << ".");
                modules_to_remove.push_back(&m);
            }
        }
        remove_modules(modules_to_remove);
        for(module& m : s_modules)
            for(uint dep : m.require_list)
                if(!module_exist(dep))
                {
                    modules_to_remove.push_back(&m);
                    warn("Missing module dependecie with id: " << dep << " for the module named: " << m.name << " with id: " << m.id);
                }
        remove_modules(modules_to_remove);
        for(module& m : s_modules)
            for(uint connection : m.connect_list)
                if(module_exist(connection))
                {
                    if(module_connect_to(connection, m.id))
                        m.actual_connections.push_back(connection);
                    else
                    {
                        warn("Cannot establish connection between device with id: " << m.id << " and device with id: " << connection << ", the second device doesn't agree with the connection.");
                        modules_reports.push_back(module_report{&m, DC_CONNECTION_INAGREEMENT, connection});
                    }  
                }
                else
                {
                    warn("Cannot establish connection between device with id: " << m.id << " and device with id: " << connection << ", the second device doesn't exist.");
                    modules_reports.push_back(module_report{&m, DC_CONNECTION_INEXISTANT, connection});
                }
        for(module& m : s_modules)
        {
            using mc = module_clock;
            mc& c = m.clock_mode;
            if(c == mc::FIRST || c == mc::FIRST_CPU || c == mc::FIRST_NORMAL || c == FIRST_LAST)
                s_modules_first.push_back(&m);
            if(c == mc::CPU || c == mc::FIRST_CPU || c == mc::CPU_LAST)
                s_modules_cpus.push_back(&m);
            if(c == mc::NORMAL || c == mc::FIRST_NORMAL || c == mc::NORMAL_LAST)
                s_modules_normal.push_back(&m);
            if(c == mc::LAST || c == mc::FIRST_LAST || c == mc::CPU_LAST || c == mc::NORMAL_LAST)
                s_modules_last.push_back(&m);
        }
        for(module_report& r : modules_reports)
            r.target_module->report(r.additional_data0, r.additional_data1);
        for(module& m : s_modules)
            m.create_device();
    }
}

void clock_modules()
{
    for(module* m : s_modules_first)
        m->pre_clock();
    for(module* m : s_modules_normal)
        m->clock();
    for(module* m : s_modules_last)
        m->post_clock();
    for(module* m : s_modules_cpus)
        m->clock();
}

void unload_modules()
{
    for(module& m : s_modules)
    {
        m.destroy_device();
        dlclose(m.lib_handle);
    }
}

void module::report(uint additional_data0, uint additional_data1)
{
    if(fp_report != nullptr)
        fp_report(additional_data0, additional_data1);
}
void module::create_device()
{
    if(fp_create_device != nullptr)
        p_device = fp_create_device();
}
void module::pre_clock()
{
    if(fp_pre_clock != nullptr)
        fp_pre_clock();
}
void module::clock()
{
    if(fp_clock != nullptr)
        fp_clock();
}
void module::post_clock()
{
    if(fp_post_clock != nullptr)
        fp_post_clock();
}
void module::destroy_device()
{
    if(fp_destroy_device != nullptr)
        fp_destroy_device(p_device);
}