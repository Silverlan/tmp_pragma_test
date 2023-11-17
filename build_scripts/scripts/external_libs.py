import scripts.shared
from sys import platform

scripts.shared.init_global_vars()
from scripts.shared import *

curDir = os.getcwd()

external_libs_dir = curDir +"/external_libs"
os.chdir(external_libs_dir)
get_submodule("alsoundsystem","https://github.com/Silverlan/alsoundsystem.git","4ffcd98")
get_submodule("datasystem","https://github.com/Silverlan/datasystem.git","a6dfff6")
get_submodule("iglfw","https://github.com/Silverlan/iglfw.git","dff0063cc37d248c4eb11fc9fec356e091f7364c")
get_submodule("luasystem","https://github.com/Silverlan/luasystem.git","e22584368ce0f9a5c0c223b8a632d968377d1ae3")
get_submodule("materialsystem","https://github.com/Silverlan/materialsystem.git","6214d38")
get_submodule("mathutil","https://github.com/Silverlan/mathutil.git","24823c71714aebe103c784c5933ef467dd320b1f")
get_submodule("networkmanager","https://github.com/Silverlan/networkmanager.git","68d2f5c")
get_submodule("sharedutils","https://github.com/Slaweknowy/sharedutils.git","f651b5b")
get_submodule("panima","https://github.com/Silverlan/panima.git","bef720d8a8b001b8f8b6df9c6151eece6926c516")
get_submodule("prosper","https://github.com/Silverlan/prosper.git","18fffa94a21dd3d83f0cea727b8b06108993ac1b")
get_submodule("util_bsp","https://github.com/Silverlan/util_bsp.git","3c11053")
get_submodule("util_formatted_text","https://github.com/Silverlan/util_formatted_text.git","3bd9e2de37d0cb14bf8228fde5e25c97698e927b")
get_submodule("util_image","https://github.com/Silverlan/util_image.git","e22c12f")
get_submodule("util_pad","https://github.com/Silverlan/util_pad.git","7a827f8")
get_submodule("util_pragma_doc","https://github.com/Silverlan/util_pragma_doc.git","6a4a089")
get_submodule("util_smdmodel","https://github.com/Silverlan/util_smdmodel.git","5e581ab")
get_submodule("util_sound","https://github.com/Slaweknowy/util_sound.git","db844a7b053b3f0de86ba9cc6a0668c771d8059f")
get_submodule("util_source2","https://github.com/Silverlan/util_source2.git","cf553d6")
get_submodule("util_source_script","https://github.com/Silverlan/util_source_script.git","ea0d03a")
get_submodule("util_timeline_scene","https://github.com/Silverlan/util_timeline_scene.git","76d02ff")
get_submodule("util_udm","https://github.com/Silverlan/util_udm.git","9ddf3999f811e16b421ca5e0d154454ddaed2019")
get_submodule("util_versioned_archive","https://github.com/Silverlan/util_versioned_archive.git","be6d3fe")
get_submodule("util_vmf","https://github.com/Silverlan/util_vmf.git","cdba99d")
get_submodule("util_zip","https://github.com/Silverlan/util_zip.git","63b2609")
get_submodule("vfilesystem","https://github.com/Silverlan/vfilesystem.git","f32d72c8869aa7db4b64d4e3fe04ddeeb15e89e1")
get_submodule("wgui","https://github.com/Silverlan/wgui.git","72487effcba714883170af3251ebb5c6531bf2d6")

os.chdir(curDir)
