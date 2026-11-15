shared_module('gtkdecor',
    ['decoration.cpp', 'deco-subsurface.cpp', 'deco-button.cpp',
     'deco-layout.cpp', 'deco-theme.cpp', 'deco-metacity.cpp', 'deco-css.cpp'],
    dependencies: gtkdecor_deps,
    cpp_args: gtkdecor_cpp_args,
    link_args: '-Wl,--version-script=' + meson.current_source_dir() / 'gtkdecor.map',
    link_depends: 'gtkdecor.map',
    install: true,
    install_dir: join_paths(get_option('libdir'), 'wayfire'))
