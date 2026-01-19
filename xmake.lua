add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})
set_languages("c++23")

target("ps-tree")
    set_kind("binary")
    add_files("src/pstree/*.cpp")