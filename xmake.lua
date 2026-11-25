-- Drop Overhaul SF
set_xmakever("3.0.0")

set_project("DropOverhaulSF")
set_version("0.3.1")
set_license("GPL-3.0")

set_arch("x64")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")  -- optimized, with a .pdb for crash loggers such as Trainwreck

-- CommonLibSF (OSF's fork) is expected next to this project, in ../commonlibsf, see README: "Build".
includes("../commonlibsf")

target("DropOverhaulSF", function()
    add_rules("commonlibsf.plugin", {
        name = "DropOverhaulSF",
        author = "drvictorvs",
        description = "Pick up any loose object and place it precisely"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")
    add_syslinks("user32")  -- GetAsyncKeyState, GetForegroundWindow

    add_installfiles("dist/Data/SFSE/Plugins/DropOverhaulSF.ini", { prefixdir = "SFSE/Plugins" })
    add_installfiles("dist/Data/SFSE/Plugins/OSF/Settings/schemas/dropoverhaul-sf.json", { prefixdir = "SFSE/Plugins/OSF/Settings/schemas" })
end)
