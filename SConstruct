# Copyright Francesco Di Blasi 2026 - All Rights Reserved

import os

from methods import print_error

root = Dir("#").abspath
if not os.path.isfile(os.path.join(root, "godot-cpp", "SConstruct")):
    print_error("Initialize godot-cpp with git submodule update --init.")
    Exit(1)

local_env = Environment(
    tools=["default"],
    PLATFORM="",
    build_profile=os.path.join(root, "build_profile.json"),
    disable_exceptions=False,
)
env = SConscript("godot-cpp/SConstruct", {"env": local_env, "api_version": "4.7"})

if (env["platform"] != "windows" or env["arch"] != "x86_64"
        or env["precision"] != "single" or env["target"] not in ("template_debug", "template_release")):
    print_error("Build Crimson Framework for Windows x86-64 single precision with target=template_debug or target=template_release.")
    Exit(1)

env.Append(CPPPATH=["#src", "#src/modules/save/third_party"], LIBS=["bcrypt"])
sources = Glob("src/*.cpp")
for module in ("inventory", "save", "session", "scene_load", "match_runtime", "replication"):
    sources += Glob(f"src/modules/{module}/*.cpp")
sources += Glob("src/integration/*.cpp")
doc_xml = Glob("src/modules/inventory/doc_classes/*.xml")
if env["target"] == "template_debug" and doc_xml:
    sources.append(env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=doc_xml))
addon = "project/addons/crimson-framework/bin"
suffix = env["suffix"].replace(".dev", "")
filename = f"crimson_framework{suffix}{env['SHLIBSUFFIX']}"
library = env.SharedLibrary(f"bin/windows/{filename}", source=sources)
Default(env.InstallAs(f"{addon}/{filename}", library[0]))
