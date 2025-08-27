#include "sh.h"
#include <filesystem>

std::string exec(const std::string& cmd)
{
    system(cmd.c_str());
    return "";
}

std::string sed(const std::string& expr, const std::filesystem::path& in, const std::filesystem::path& out)
{
    std::string cmd;
    if (in == out) {
        cmd = std::string("sed -i ")
            + expr + " "
            + in.string();
    } else {
        cmd = std::string("sed ")
            + expr + " "
            + in.string()
            + " > "
            + out.string();
    }
    return exec(cmd);
}

std::string replaceDefine(const std::string& name, const int value, const std::filesystem::path& in, const std::filesystem::path& out)
{
    std::string expr = "\"s/#define " + name + " [0-9]*/#define " + name + " "
        + std::to_string(value) + "/g\"";
    return sed(expr, in, out);
}

std::string replaceDefine(const std::string& name, const float value, const std::filesystem::path& in, const std::filesystem::path& out)
{
    std::string expr = "\"s/#define " + name + " [0-9]*\\.\\?[0-9]*/#define " + name + " "
        + std::to_string(value) + "/g\"";
    return sed(expr, in, out);
}

std::string replaceInclude(const std::string& prev_include, const std::string& new_include, const std::filesystem::path& in, const std::filesystem::path& out)
{
    std::string expr = "\"s|#include[ ]*\\\"" + prev_include + "\\\"|#include \\\"" + new_include + "\\\"|g\"";
    return sed(expr, in, out);
}

void glslc(const std::filesystem::path& in, const std::filesystem::path& out)
{
#ifdef DEBUG
    std::string debugsource = " -gVS ";
#else
    std::string debugsource = "";
#endif
    std::string cmd = std::string("glslang ")
        + in.string()
        + " -o "
        + out.string()
        + " -V --target-env vulkan1.2"
        + debugsource;
#ifdef DEBUG
    exec(cmd);
#else
    cmd = std::string("spirv-opt -O ")
        + out.string()
        + " -o "
        + out.string() + ".opt";
    exec(cmd);
    std::filesystem::remove(out.string());
    std::filesystem::rename(out.string() + ".opt", out.string());
#endif
}
