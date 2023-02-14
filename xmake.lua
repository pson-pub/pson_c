add_rules("mode.debug", "mode.release")


target("pson_c")
    set_kind("static")
    add_includedirs("src")
    set_rundir(".")
    add_files("src/*.c")

target("demo")
    set_kind("binary")
    add_includedirs("src")
    set_rundir(".")
    add_deps("pson_c")
    add_files( "example.c")