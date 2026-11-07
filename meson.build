project('gtkdecor', 'cpp',
    version: '0.4.2',
    default_options: ['cpp_std=c++17'])

wayfire = dependency('wayfire', version: '>=0.11.0')
librsvg = dependency('librsvg-2.0', required: false)

gtkdecor_deps = [wayfire]
gtkdecor_cpp_args = []

libxml = dependency('libxml-2.0', required: false)

if librsvg.found()
    gtkdecor_deps += librsvg
    gtkdecor_cpp_args += '-DHAVE_LIBRSVG'
endif

# Window buttons from a theme's Metacity XML need both
if librsvg.found() and libxml.found()
    gtkdecor_deps += libxml
    gtkdecor_cpp_args += '-DHAVE_METACITY'
endif

subdir('src')
subdir('metadata')
subdir('icons')
