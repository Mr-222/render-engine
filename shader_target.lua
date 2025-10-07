function shader_target(name)
    target(name .. "_shader")
    set_kind("static")
    add_rules("utils.glsl2spv",
        {
            outputdir = "$(builddir)/shaders/" .. name,
            debugsource = is_mode("debug")
        })
    add_packages("glslc")
    add_files("**/node/" .. name .. "/*.vert")
    add_files("**/node/" .. name .. "/*.frag")
    add_files("**/node/" .. name .. "/*.geom")
end
