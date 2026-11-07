#include "deco-theme.hpp"
#include <wayfire/core.hpp>
#include <wayfire/opengl.hpp>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <vector>
#include <sys/stat.h>
#include <cmath>
#include <array>
#include <set>

#ifdef HAVE_LIBRSVG
#include <librsvg/rsvg.h>
#endif

namespace wf
{
namespace decor
{
/** Parse CSS color string to wf::color_t */
wf::color_t decoration_theme_t::parse_css_color(const std::string& color_str) const
{
    wf::color_t color{0, 0, 0, 1.0};

    if (color_str == "transparent")
    {
        return wf::color_t{0, 0, 0, 0};
    }

    if (color_str == "white")
    {
        return wf::color_t{1, 1, 1, 1};
    }

    // Match hex color: #rrggbb or #rrggbbaa
    std::regex hex_regex("#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?");
    std::smatch hex_match;
    if (std::regex_search(color_str, hex_match, hex_regex))
    {
        std::string hex = hex_match[1].str();
        color.r = std::stoi(hex.substr(0, 2), nullptr, 16) / 255.0;
        color.g = std::stoi(hex.substr(2, 2), nullptr, 16) / 255.0;
        color.b = std::stoi(hex.substr(4, 2), nullptr, 16) / 255.0;

        if (hex_match[2].matched)
        {
            color.a = std::stoi(hex_match[2].str(), nullptr, 16) / 255.0;
        } else
        {
            color.a = 1.0;
        }
        return color;
    }

    // Match rgba: rgba(r, g, b, a)
    std::regex rgba_regex(R"(rgba?\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*([\d.]+))?\s*\))");
    std::smatch rgba_match;
    if (std::regex_search(color_str, rgba_match, rgba_regex))
    {
        color.r = std::stoi(rgba_match[1].str()) / 255.0;
        color.g = std::stoi(rgba_match[2].str()) / 255.0;
        color.b = std::stoi(rgba_match[3].str()) / 255.0;
        color.a = rgba_match[4].matched ? std::stod(rgba_match[4].str()) : 1.0;
        return color;
    }

    return color;  // Return black if parsing fails
}

/** Get current GTK theme name from settings */
std::string decoration_theme_t::get_gtk_theme_name() const
{
    const char* home = getenv("HOME");
    if (!home)
    {
        return "";
    }

    std::string settings_path = std::string(home) + "/.config/gtk-3.0/settings.ini";
    std::ifstream settings_file(settings_path);
    if (!settings_file.is_open())
    {
        return "";
    }

    std::string line;
    while (std::getline(settings_file, line))
    {
        // cppcheck-suppress stlIfStrFind
        if (line.find("gtk-theme-name=") == 0)
        {
            return line.substr(15);  // Length of "gtk-theme-name="
        }
    }

    return "";
}

/** Get current icon theme name from settings */
std::string decoration_theme_t::get_icon_theme_name() const
{
    const char* home = getenv("HOME");
    if (!home)
    {
        return "hicolor"; // Default fallback
    }

    std::string settings_path = std::string(home) + "/.config/gtk-3.0/settings.ini";
    std::ifstream settings_file(settings_path);
    if (!settings_file.is_open())
    {
        return "hicolor";
    }

    std::string line;
    while (std::getline(settings_file, line))
    {
        // cppcheck-suppress stlIfStrFind
        if (line.find("gtk-icon-theme-name=") == 0)
        {
            return line.substr(20);  // Length of "gtk-icon-theme-name="
        }
    }

    return "hicolor";
}

/** Get GTK font name from settings */
std::string decoration_theme_t::get_gtk_font_name() const
{
    const char* home = getenv("HOME");
    if (!home)
    {
        return "";
    }

    std::string settings_path = std::string(home) + "/.config/gtk-3.0/settings.ini";
    std::ifstream settings_file(settings_path);
    if (!settings_file.is_open())
    {
        return "";
    }

    std::string line;
    while (std::getline(settings_file, line))
    {
        // cppcheck-suppress stlIfStrFind
        if (line.find("gtk-font-name=") == 0)
        {
            return line.substr(14);  // Length of "gtk-font-name="
        }
    }

    return "";
}

/** Find theme CSS file path */
std::string decoration_theme_t::find_theme_css_file(const std::string& theme_name) const
{
    if (theme_name.empty())
    {
        return "";
    }

    const char* home = getenv("HOME");
    std::vector<std::string> search_paths;

    if (home)
    {
        search_paths.push_back(std::string(home) + "/.themes/" + theme_name + "/gtk-3.0/gtk.css");
        search_paths.push_back(std::string(home) + "/.themes/" + theme_name + "/gtk-3.0/gtk-dark.css");
        search_paths.push_back(std::string(home) + "/.local/share/themes/" + theme_name + "/gtk-3.0/gtk.css");
        search_paths.push_back(std::string(home) + "/.local/share/themes/" + theme_name + "/gtk-3.0/gtk-dark.css");
    }

    search_paths.push_back("/usr/share/themes/" + theme_name + "/gtk-3.0/gtk.css");
    search_paths.push_back("/usr/share/themes/" + theme_name + "/gtk-3.0/gtk-dark.css");
    search_paths.push_back("/usr/local/share/themes/" + theme_name + "/gtk-3.0/gtk.css");

    for (const auto& path : search_paths)
    {
        struct stat buffer;
        if (stat(path.c_str(), &buffer) == 0)
        {
            return path;
        }
    }

    return "";
}

/**
 * Locate a metacity-1 titlebutton asset for the given button and state.
 *
 * Themes ship a full set of complete, pre-coloured button images under
 * <theme>/metacity-1/. Two naming conventions are in use:
 *
 *   titlebuttons/titlebutton-<action>[-backdrop][-hover|-active].<ext>
 *       WhiteSur, MacTahoe
 *   <action>_<focused|unfocused>[_normal|_prelight|_pressed].<ext>
 *       Nordic
 *
 * Unlike the symbolic icons used by the drawn style, these are not masks:
 * the colour is baked in, so they are painted as-is.
 */
std::string decoration_theme_t::find_titlebutton_file(button_type_t button,
    const button_state_t& state) const
{
    if (gtk_theme_name.empty())
    {
        return "";
    }

    std::string action;
    switch (button)
    {
      case BUTTON_CLOSE:
        action = "close";
        break;

      case BUTTON_TOGGLE_MAXIMIZE:
        action = "maximize";
        break;

      case BUTTON_MINIMIZE:
        action = "minimize";
        break;

      default:
        return "";
    }

    const bool pressed = state.hover_progress < 0;
    const bool hovered = state.hover_progress > 0;

    /* Two naming conventions are in the wild, and a theme uses one or the
     * other, so both are tried at each state:
     *
     *   WhiteSur, MacTahoe:  metacity-1/titlebuttons/titlebutton-close-hover.svg
     *   Nordic:              metacity-1/close_focused_prelight.png
     *
     * Candidates are ordered most-specific-state first and degrade towards
     * the plain button, since no theme ships every variant. */
    std::vector<std::string> candidates;
    auto add_state = [&] (const std::string& modern, const std::string& classic)
    {
        candidates.push_back("/metacity-1/titlebuttons/titlebutton-" + action + modern);
        candidates.push_back("/metacity-1/" + action + classic);
    };

    if (!state.activated)
    {
        if (pressed)
        {
            add_state("-backdrop-active", "_unfocused_pressed");
        }

        if (hovered)
        {
            add_state("-backdrop-hover", "_unfocused_prelight");
        }

        add_state("-backdrop", "_unfocused");
    } else if (pressed)
    {
        add_state("-active", "_focused_pressed");
        add_state("-hover", "_focused_prelight");
    } else if (hovered)
    {
        add_state("-hover", "_focused_prelight");
    }

    /* Least specific: the plain, focused button. */
    candidates.push_back("/metacity-1/titlebuttons/titlebutton-" + action);
    candidates.push_back("/metacity-1/" + action + "_focused_normal");
    candidates.push_back("/metacity-1/" + action + "_focused");
    candidates.push_back("/metacity-1/" + action);

    const char *home = getenv("HOME");
    std::vector<std::string> bases;
    if (home)
    {
        bases.push_back(std::string(home) + "/.themes/");
        bases.push_back(std::string(home) + "/.local/share/themes/");
    }

    bases.push_back("/usr/share/themes/");
    bases.push_back("/usr/local/share/themes/");

    for (const auto& candidate : candidates)
    {
        for (const auto& base : bases)
        {
            for (const auto *ext : {".svg", ".png"})
            {
                const std::string path = base + gtk_theme_name + candidate + ext;
                struct stat buffer;
                if (stat(path.c_str(), &buffer) == 0)
                {
                    return path;
                }
            }
        }
    }

    return "";
}

/**
 * Render a titlebutton asset at @size, cached so the SVG is not re-parsed on
 * every frame of the hover animation.
 */
cairo_surface_t*decoration_theme_t::get_titlebutton_asset(const std::string& path, int size) const
{
    if (size <= 0)
    {
        return nullptr;
    }

    const std::string key = path + "@" + std::to_string(size);
    auto it = titlebutton_cache.find(key);
    if (it != titlebutton_cache.end())
    {
        return it->second;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *cr = cairo_create(surface);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    bool ok = false;

    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".png") == 0)
    {
        cairo_surface_t *png = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS)
        {
            const double sw = cairo_image_surface_get_width(png);
            const double sh = cairo_image_surface_get_height(png);
            if ((sw > 0) && (sh > 0))
            {
                cairo_scale(cr, size / sw, size / sh);
                cairo_set_source_surface(cr, png, 0, 0);
                cairo_paint(cr);
                ok = true;
            }
        }

        cairo_surface_destroy(png);
    }
#ifdef HAVE_LIBRSVG
    else
    {
        GError *error = NULL;
        RsvgHandle *handle = rsvg_handle_new_from_file(path.c_str(), &error);
        if (handle)
        {
            /* Full-bleed: the asset is the whole button, padding is the
             * theme's business, not ours. */
            RsvgRectangle viewport = {
                .x = 0.0,
                .y = 0.0,
                .width  = (double)size,
                .height = (double)size
            };
            ok = rsvg_handle_render_document(handle, cr, &viewport, &error);
            g_object_unref(handle);
        }

        if (error)
        {
            LOGE("Failed to render titlebutton ", path, ": ", error->message);
            g_error_free(error);
        }
    }
#endif

    cairo_destroy(cr);

    if (!ok)
    {
        cairo_surface_destroy(surface);
        titlebutton_cache[key] = nullptr;  // negative cache, don't retry every frame
        return nullptr;
    }

    titlebutton_cache[key] = surface;
    return surface;
}

/** Find icon file path in icon theme */
std::string decoration_theme_t::find_icon_file(const std::string& icon_name, int size) const
{
    if (icon_theme_name.empty() || icon_name.empty())
    {
        return "";
    }

    const char* home = getenv("HOME");
    std::vector<std::string> base_paths;

    if (home)
    {
        base_paths.push_back(std::string(home) + "/.icons");
        base_paths.push_back(std::string(home) + "/.local/share/icons");
    }
    base_paths.push_back("/usr/share/icons");
    base_paths.push_back("/usr/local/share/icons");

    // Common size directories to check
    std::vector<std::string> size_dirs = {
        "symbolic",
        std::to_string(size) + "x" + std::to_string(size),
        "scalable",
        "16x16", "22x22", "24x24", "32x32", "48x48"
    };

    // Common subdirectories for window controls
    std::vector<std::string> sub_dirs = {
        "actions", "ui", "status", "places"
    };

    for (const auto& base : base_paths)
    {
        for (const auto& size_dir : size_dirs)
        {
            for (const auto& sub_dir : sub_dirs)
            {
                // Try both possible directory orderings for maximum compatibility
                // Pattern 1: theme/size/subdir/icon (e.g., Adwaita/symbolic/ui/icon.svg)
                std::string path = base + "/" + icon_theme_name + "/" + size_dir + "/" + sub_dir + "/" + icon_name + ".svg";
                struct stat buffer;
                if (stat(path.c_str(), &buffer) == 0)
                {
                    return path;
                }

                // Pattern 2: theme/subdir/size/icon (e.g., elementary/actions/symbolic/icon.svg)
                path = base + "/" + icon_theme_name + "/" + sub_dir + "/" + size_dir + "/" + icon_name + ".svg";
                if (stat(path.c_str(), &buffer) == 0)
                {
                    return path;
                }

                // Try PNG for both patterns
                path = base + "/" + icon_theme_name + "/" + size_dir + "/" + sub_dir + "/" + icon_name + ".png";
                if (stat(path.c_str(), &buffer) == 0)
                {
                    return path;
                }

                path = base + "/" + icon_theme_name + "/" + sub_dir + "/" + size_dir + "/" + icon_name + ".png";
                if (stat(path.c_str(), &buffer) == 0)
                {
                    return path;
                }
            }

            // Also try without subdirectory
            std::string path = base + "/" + icon_theme_name + "/" + size_dir + "/" + icon_name + ".svg";
            struct stat buffer;
            if (stat(path.c_str(), &buffer) == 0)
            {
                return path;
            }

            path = base + "/" + icon_theme_name + "/" + size_dir + "/" + icon_name + ".png";
            if (stat(path.c_str(), &buffer) == 0)
            {
                return path;
            }
        }
    }

    // Fallback to Adwaita if icon not found in theme
    if (icon_theme_name != "Adwaita")
    {
        for (const auto& base : base_paths)
        {
            std::string path = base + "/Adwaita/symbolic/ui/" + icon_name + ".svg";
            struct stat buffer;
            if (stat(path.c_str(), &buffer) == 0)
            {
                return path;
            }
        }
    }

    return "";
}

/**
 * Read the colours a theme gives its window buttons in the stylesheet.
 *
 * Colloid has no button images of its own to load: its traffic lights exist
 * only as rules like button.close.titlebutton { background-color: #bf616a; }.
 * Only the plain selectors count; the longer ones that also match are specific
 * to client-side headerbars.
 *
 * Many themes declare these rules with one neutral colour for all three
 * buttons. That is not colour-coding, so those keep the default drawn style.
 */
void decoration_theme_t::parse_titlebutton_colors(std::string css) const
{
    for (size_t start = css.find("/*"); start != std::string::npos; start = css.find("/*", start))
    {
        const size_t end = css.find("*/", start);
        css.erase(start, end == std::string::npos ? end : end + 2 - start);
    }

    auto trim = [] (const std::string& text)
    {
        const char *space = " \t\r\n";
        const size_t first = text.find_first_not_of(space);
        return (first == std::string::npos) ? std::string{} :
               text.substr(first, text.find_last_not_of(space) - first + 1);
    };

    /* "<selector>/<property>" -> value, later rules overriding earlier ones */
    std::map<std::string, std::string> declared;
    const std::regex not_regex(R"(:not\([^)]*\))");

    size_t pos = 0;
    while (true)
    {
        const size_t open = css.find_first_of("{}", pos);
        if (open == std::string::npos)
        {
            break;
        }

        /* A stray '}' closes a nested block such as @keyframes */
        if (css[open] == '}')
        {
            pos = open + 1;
            continue;
        }

        const size_t close = css.find('}', open);
        if (close == std::string::npos)
        {
            break;
        }

        std::string selectors = css.substr(pos, open - pos);
        const std::string body = css.substr(open + 1, close - open - 1);
        pos = close + 1;

        if (selectors.find(".titlebutton") == std::string::npos)
        {
            continue;
        }

        /* Drop any at-statement such as @define-color ahead of the rule */
        const size_t statement_end = selectors.rfind(';');
        if (statement_end != std::string::npos)
        {
            selectors.erase(0, statement_end + 1);
        }

        selectors = std::regex_replace(selectors, not_regex, "");

        std::stringstream selector_list(selectors);
        std::string selector;
        while (std::getline(selector_list, selector, ','))
        {
            selector = trim(selector);

            std::stringstream declarations(body);
            std::string declaration;
            while (std::getline(declarations, declaration, ';'))
            {
                const size_t colon = declaration.find(':');
                if (colon == std::string::npos)
                {
                    continue;
                }

                const std::string property = trim(declaration.substr(0, colon));
                if ((property == "background-color") || (property == "color"))
                {
                    declared[selector + "/" + property] = trim(declaration.substr(colon + 1));
                }
            }
        }
    }

    auto value_of = [&] (const std::string& action, const std::string& pseudo,
                         const std::string& property)
    {
        auto it = declared.find("button." + action + ".titlebutton" + pseudo + "/" + property);
        return (it == declared.end()) ? std::string{} : it->second;
    };

    const std::string close_bg    = value_of("close", "", "background-color");
    const std::string minimize_bg = value_of("minimize", "", "background-color");
    const std::string maximize_bg = value_of("maximize", "", "background-color");
    if (close_bg.empty() || minimize_bg.empty() || maximize_bg.empty() ||
        ((close_bg == minimize_bg) && (close_bg == maximize_bg)))
    {
        return;
    }

    const std::pair<button_type_t, std::string> buttons[] = {
        {BUTTON_CLOSE, "close"},
        {BUTTON_MINIMIZE, "minimize"},
        {BUTTON_TOGGLE_MAXIMIZE, "maximize"},
    };

    for (const auto& [button, action] : buttons)
    {
        auto color_of = [&, &action = action] (const std::string& pseudo,
                                               const std::string& property, wf::color_t fallback)
        {
            const std::string value = value_of(action, pseudo, property);
            return value.empty() ? fallback : parse_css_color(value);
        };

        titlebutton_colors_t colors;
        colors.bg = parse_css_color(value_of(action, "", "background-color"));
        colors.pressed_bg  = color_of(":active", "background-color", colors.bg);
        colors.backdrop_bg = color_of(":backdrop", "background-color", colors.bg);
        colors.fg = color_of("", "color", wf::color_t{1, 1, 1, 1});
        colors.hover_fg = color_of(":hover", "color", colors.fg);
        colors.backdrop_hover_fg = color_of(":backdrop:hover", "color", colors.hover_fg);
        titlebutton_colors[button] = colors;
    }

    LOGI("Theme colours its window buttons individually");
}

/** Parse theme CSS file and extract colors */
void decoration_theme_t::parse_theme_css(const std::string& css_file) const
{
    std::ifstream file(css_file);
    if (!file.is_open())
    {
        LOGE("Failed to open theme CSS file: ", css_file);
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string css_content = buffer.str();

    // Build map of @define-color variables
    std::map<std::string, std::string> color_vars;
    std::regex define_color_regex(R"(@define-color\s+(\w+)\s+([^;]+);)");
    std::smatch match;
    std::string::const_iterator searchStart(css_content.cbegin());
    while (std::regex_search(searchStart, css_content.cend(), match, define_color_regex))
    {
        color_vars[match[1].str()] = match[2].str();
        searchStart = match.suffix().first;
    }

    css_colors = color_vars;

    /* GTK's own frame for a client-side decorated window: the nodes it is
     * drawn with, styled by the theme's cascade */
    css_sheet_t sheet;
    sheet.parse(css_content);

    const css_sheet_t::node_t decoration_node{"decoration", {}, {}};
    const css_sheet_t::node_t headerbar_node{"headerbar", {"titlebar", "default-decoration"}, {}};
    const css_sheet_t::node_t title_node{"label", {"title"}, {}};
    const css_sheet_t::node_t button_node{"button", {"titlebutton", "close"}, {}};
    auto frame_path = [] (bool backdrop, std::vector<css_sheet_t::node_t> nodes)
    {
        std::set<std::string> states;
        if (backdrop)
        {
            states.insert("backdrop");
        }

        css_sheet_t::path_t path{{"window", {"background", "csd"}, states}};
        for (auto& node : nodes)
        {
            node.states = states;
            path.push_back(node);
        }

        return path;
    };

    /* Lengths in px ("8px", "0", "0 2px"); nothing if any is in another unit */
    auto lengths = [&] (const css_sheet_t::path_t& path, const char *property)
    {
        std::vector<double> result;
        auto value = sheet.value(path, property);
        if (!value)
        {
            return result;
        }

        std::stringstream stream(*value);
        std::string token;
        while (stream >> token)
        {
            char *end = nullptr;
            const double length = strtod(token.c_str(), &end);
            const std::string unit = end;
            if ((end == token.c_str()) || (!unit.empty() && (unit != "px")))
            {
                return std::vector<double>{};
            }

            result.push_back(length);
        }

        return result;
    };

    /* CSS box shorthand: top, right, bottom, left */
    auto box = [] (const std::vector<double>& v) -> std::array<double, 4>
    {
        switch (v.size())
        {
          case 0:
            return {0, 0, 0, 0};

          case 1:
            return {v[0], v[0], v[0], v[0]};

          case 2:
            return {v[0], v[1], v[0], v[1]};

          case 3:
            return {v[0], v[1], v[2], v[1]};

          default:
            return {v[0], v[1], v[2], v[3]};
        }
    };

    /* The window shadow: the decoration node's box-shadow */
    if (auto active_shadow = sheet.value(frame_path(false, {decoration_node}), "box-shadow"))
    {
        shadow_active = parse_box_shadow(*active_shadow);
        auto backdrop = sheet.value(frame_path(true, {decoration_node}), "box-shadow");
        shadow_inactive = backdrop ? parse_box_shadow(*backdrop) : shadow_active;
        LOGI("Theme window shadow: ", shadow_active.size(), " layer(s) focused, ",
            shadow_inactive.size(), " unfocused");
    }

    /* Corners: the decoration's border-radius (top-left, top-right,
     * bottom-right, bottom-left), else the header bar's for the top */
    {
        auto radius = lengths(frame_path(false, {decoration_node}), "border-radius");
        if (!radius.empty())
        {
            const auto r = box(radius);  // same expansion order as the box shorthand
            corner_radius = std::lround(r[0]);
            bottom_corner_radius = std::lround(r[3]);
        } else if (!(radius = lengths(frame_path(false, {headerbar_node}), "border-radius")).empty())
        {
            corner_radius = std::lround(box(radius)[0]);
        }

        LOGI("Theme corner radius: ", corner_radius, "px top, ", bottom_corner_radius, "px bottom");
    }

    /* Sizes: the header bar is at least min-height and as tall as its
     * buttons, plus its padding; buttons are at least min-width x min-height
     * and fit a 16px icon plus their padding */
    {
        const auto bar    = frame_path(false, {headerbar_node});
        const auto button = frame_path(false, {headerbar_node, button_node});
        auto first = [] (const std::vector<double>& v) { return v.empty() ? 0.0 : v[0]; };

        const double bar_min  = first(lengths(bar, "min-height"));
        const auto bar_pad    = box(lengths(bar, "padding"));
        const double min_w    = first(lengths(button, "min-width"));
        const double min_h    = first(lengths(button, "min-height"));
        const auto button_pad = box(lengths(button, "padding"));
        const auto margin     = box(lengths(button, "margin"));

        if ((min_w > 0) || (min_h > 0))
        {
            const double w = std::max(min_w, 16 + button_pad[1] + button_pad[3]);
            const double h = std::max(min_h, 16 + button_pad[0] + button_pad[2]);
            theme_button_size    = std::lround(std::max(w, h));
            theme_button_spacing = std::lround(margin[1] + margin[3]);
            theme_button_edge    = std::lround(std::max(bar_pad[1], bar_pad[3]) + margin[3]);
        }

        if ((bar_min > 0) || (theme_button_size > 0))
        {
            theme_title_total = std::lround(std::max(bar_min,
                theme_button_size + margin[0] + margin[2]) + bar_pad[0] + bar_pad[2]);
        }

        LOGI("Theme header bar: ", theme_title_total, "px tall, buttons ", theme_button_size,
            "px, ", theme_button_spacing, "px apart, ", theme_button_edge, "px from the edge");
    }

    // Try to find background color from @define-color theme_bg_color or theme_unfocused_bg_color
    if (color_vars.count("theme_bg_color"))
    {
        theme_titlebar_bg_active = parse_css_color(color_vars["theme_bg_color"]);
        LOGI("Using theme_bg_color for titlebar background");
    }

    if (color_vars.count("theme_unfocused_bg_color"))
    {
        theme_titlebar_bg_inactive = parse_css_color(color_vars["theme_unfocused_bg_color"]);
    } else
    {
        theme_titlebar_bg_inactive = theme_titlebar_bg_active;
    }

    // Try to find foreground/text color
    if (color_vars.count("theme_fg_color"))
    {
        theme_titlebar_fg_active = parse_css_color(color_vars["theme_fg_color"]);
        LOGI("Using theme_fg_color for titlebar text");
    }

    if (color_vars.count("theme_unfocused_fg_color"))
    {
        theme_titlebar_fg_inactive = parse_css_color(color_vars["theme_unfocused_fg_color"]);
    } else if (color_vars.count("unfocused_insensitive_color"))
    {
        theme_titlebar_fg_inactive = parse_css_color(color_vars["unfocused_insensitive_color"]);
    } else
    {
        theme_titlebar_fg_inactive = theme_titlebar_fg_active;
    }

    /* What GTK actually paints its title bar with: the header bar's
     * background colour, with a flat image(<colour>) layer over it if the
     * theme adds one */
    auto over = [] (wf::color_t top, wf::color_t bottom)
    {
        const double a = top.a + bottom.a * (1 - top.a);
        if (a <= 0)
        {
            return wf::color_t{0, 0, 0, 0};
        }

        auto mix = [&] (double t, double b) { return (t * top.a + b * bottom.a * (1 - top.a)) / a; };
        return wf::color_t{mix(top.r, bottom.r), mix(top.g, bottom.g), mix(top.b, bottom.b), a};
    };

    for (bool backdrop : {false, true})
    {
        const auto path = frame_path(backdrop, {headerbar_node});
        std::optional<wf::color_t> bar;
        if (auto value = sheet.value(path, "background-color"))
        {
            bar = resolve_css_value(*value);
        }

        if (auto image = sheet.value(path, "background-image"))
        {
            const std::string img = *image;
            if ((img.rfind("image(", 0) == 0) && (img.back() == ')'))
            {
                if (auto layer = resolve_css_value(img.substr(6, img.size() - 7)))
                {
                    bar = bar ? over(*layer, *bar) : *layer;
                }
            } else if (img != "none")
            {
                LOGI("Header bar background-image not supported, using its colour only: ", img);
            }
        }

        if (bar)
        {
            (backdrop ? theme_titlebar_bg_inactive : theme_titlebar_bg_active) = *bar;
        }

        if (auto value = sheet.inherited_value(frame_path(backdrop, {headerbar_node, title_node}), "color"))
        {
            if (auto color = resolve_css_value(*value))
            {
                (backdrop ? theme_titlebar_fg_inactive : theme_titlebar_fg_active) = *color;
            }
        }
    }

    parse_titlebutton_colors(css_content);

    /* Colour-coded buttons are designed for the titlebar the theme exports to
     * window managers, which can differ from the window background: Colloid
     * pairs a dark wm_bg with a white theme_bg_color, and its unfocused
     * buttons are translucent white. */
    if (!titlebutton_colors.empty())
    {
        auto use_wm_color = [&] (const char *name, wf::color_t& color)
        {
            if (color_vars.count(name))
            {
                color = parse_css_color(color_vars[name]);
            }
        };

        use_wm_color("wm_bg", theme_titlebar_bg_active);
        use_wm_color("wm_unfocused_bg", theme_titlebar_bg_inactive);
        use_wm_color("wm_title", theme_titlebar_fg_active);
        use_wm_color("wm_unfocused_title", theme_titlebar_fg_inactive);
    }

    // Set button colors - matching GTK style (visible circular backgrounds)
    theme_button_bg = wf::color_t{0.4, 0.4, 0.4, 0.3};  // Gray circle in normal state
    theme_button_hover_bg = wf::color_t{0.5, 0.5, 0.5, 0.4};  // Lighter on hover
    theme_button_active_bg = wf::color_t{0.3, 0.3, 0.3, 0.5};  // Darker when pressed

    // Try to find font settings - look for window.titlebar or headerbar font
    std::regex font_regex(R"(font-family:\s*([^;]+);)");
    std::regex font_size_regex(R"(font-size:\s*(\d+)(?:px|pt)?)");
    std::smatch font_match;

    // Search for headerbar or window titlebar font settings
    std::string::const_iterator font_search_start(css_content.cbegin());
    if (std::regex_search(font_search_start, css_content.cend(), font_match, font_regex))
    {
        theme_font_family = font_match[1].str();
        // Remove quotes if present
        theme_font_family.erase(std::remove(theme_font_family.begin(), theme_font_family.end(), '"'),
                                theme_font_family.end());
        theme_font_family.erase(std::remove(theme_font_family.begin(), theme_font_family.end(), '\''),
                                theme_font_family.end());
        LOGI("Found theme font family: ", theme_font_family);
    }

    if (std::regex_search(css_content, font_match, font_size_regex))
    {
        theme_font_size = std::stoi(font_match[1].str());
        LOGI("Found theme font size: ", theme_font_size);
    }

    LOGI("Parsed GTK theme colors from: ", css_file);
    LOGI("Titlebar bg active: rgba(",
         int(theme_titlebar_bg_active.r * 255), ", ",
         int(theme_titlebar_bg_active.g * 255), ", ",
         int(theme_titlebar_bg_active.b * 255), ", ",
         theme_titlebar_bg_active.a, ")");
}

/** Load GTK theme by parsing CSS files */
void decoration_theme_t::load_gtk_theme() const
{
    if (theme_loaded)
    {
        return;
    }

    theme_loaded = true;  // Mark as attempted even if it fails

    // Get icon theme name from GTK settings
    icon_theme_name = get_icon_theme_name();
    LOGI("Found icon theme: ", icon_theme_name);

    // Get font name from GTK settings
    std::string gtk_font = get_gtk_font_name();
    LOGI("GTK font from settings.ini: '", gtk_font, "'");

    // Store whether we got font from GTK settings (to prevent CSS from overwriting it)
    bool gtk_font_found = false;
    if (!gtk_font.empty())
    {
        // Parse font string (format: "Font Name Size")
        size_t last_space = gtk_font.rfind(' ');
        if (last_space != std::string::npos)
        {
            theme_font_family = gtk_font.substr(0, last_space);
            try {
                theme_font_size = std::stoi(gtk_font.substr(last_space + 1));
                gtk_font_found = true;  // Successfully parsed GTK font
            } catch (...) {
                theme_font_size = 11; // Default size
            }
            LOGI("Parsed font family: '", theme_font_family, "', size: ", theme_font_size);
        }
    }

    // Get theme name from GTK settings
    std::string theme_name = get_gtk_theme_name();
    gtk_theme_name = theme_name;  // cached for titlebutton asset lookup
    if (theme_name.empty())
    {
        LOGE("Could not determine GTK theme name, using fallback colors");
        // Set default font if not found from GTK settings
        if (!gtk_font_found)
        {
            theme_font_family = "Sans";
            theme_font_size = 10;
        }
        return;
    }

    LOGI("Found GTK theme: ", theme_name);

    // Find theme CSS file
    std::string css_file = find_theme_css_file(theme_name);
    if (css_file.empty())
    {
        LOGE("Could not find CSS file for theme: ", theme_name);
        // Set default font if not found from GTK settings
        if (!gtk_font_found)
        {
            theme_font_family = "Sans";
            theme_font_size = 10;
        }
        return;
    }

    LOGI("Loading theme CSS from: ", css_file);

    // Save GTK font before parsing CSS (CSS might overwrite it)
    std::string saved_font_family = theme_font_family;
    int saved_font_size = theme_font_size;

    // Parse the CSS file
    parse_theme_css(css_file);

    /* <theme>/gtk-3.0/gtk.css -> <theme> */
    const std::string theme_dir = css_file.substr(0,
        css_file.rfind('/', css_file.rfind('/') - 1));
    metacity = std::make_unique<metacity_theme_t>();
    if (metacity->load(theme_dir, [this] (const std::string& name) { return lookup_css_color(name); }))
    {
        LOGI("Drawing window buttons from the theme's Metacity XML");
    } else
    {
        metacity.reset();
    }

    // Restore GTK font if it was found (prioritize settings.ini over CSS)
    if (gtk_font_found)
    {
        theme_font_family = saved_font_family;
        theme_font_size = saved_font_size;
    }
    // Otherwise use CSS font if available, or default
    else if (theme_font_family.empty())
    {
        theme_font_family = "Sans";
        theme_font_size = 10;
    }
}

/** Create a new theme with the default parameters */
decoration_theme_t::decoration_theme_t() :
    button_flags{},
    theme_loaded(false),
    theme_titlebar_bg_active{0.1, 0.1, 0.15, 1.0},
    theme_titlebar_bg_inactive{0.15, 0.15, 0.2, 1.0},
    theme_titlebar_fg_active{0.9, 0.9, 0.93, 1.0},
    theme_titlebar_fg_inactive{0.7, 0.7, 0.73, 0.7},
    theme_button_bg{0.4, 0.4, 0.4, 0.3},
    theme_button_hover_bg{0.5, 0.5, 0.5, 0.4},
    theme_button_active_bg{0.3, 0.3, 0.3, 0.5},
    theme_font_family(""),
    theme_font_size(0)
{
}

decoration_theme_t::~decoration_theme_t()
{
    clear_titlebutton_cache();
}

void decoration_theme_t::clear_titlebutton_cache() const
{
    for (auto& [key, surface] : titlebutton_cache)
    {
        cairo_surface_destroy(surface);
    }

    titlebutton_cache.clear();
}

/** Force reload of theme - call when GTK theme/icon theme changes */
void decoration_theme_t::reload_theme() const
{
    LOGI("Reloading GTK theme and icons");
    invalidate_cache();
    clear_titlebutton_cache();
    titlebutton_colors.clear();
    css_colors.clear();
    metacity.reset();
    shadow_active.clear();
    shadow_inactive.clear();
    corner_radius = 12;
    bottom_corner_radius = 8;
    theme_title_total    = 0;
    theme_button_size    = 0;
    theme_button_spacing = -1;
    theme_button_edge    = -1;
    theme_loaded = false;
    icon_theme_name.clear();
    gtk_theme_name.clear();
    theme_font_family.clear();
    theme_font_size = 0;
    // Reset colors to defaults - they'll be reloaded
    theme_titlebar_bg_active = wf::color_t{0.1, 0.1, 0.15, 1.0};
    theme_titlebar_bg_inactive = wf::color_t{0.15, 0.15, 0.2, 1.0};
    theme_titlebar_fg_active = wf::color_t{0.9, 0.9, 0.93, 1.0};
    theme_titlebar_fg_inactive = wf::color_t{0.7, 0.7, 0.73, 0.7};
}

/** @return The available height for displaying the title */
int decoration_theme_t::get_title_height() const
{
    if (title_height >= 0)
    {
        return title_height;
    }

    /* -1: as tall as the theme's header bar (our border is part of the bar) */
    load_gtk_theme();
    if (theme_title_total > 0)
    {
        return std::max(0, theme_title_total - (int)border_size);
    }

    return 28;
}

int decoration_theme_t::get_button_size() const
{
    const int title = get_title_height();
    if ((title_height < 0) && (theme_button_size > 0))
    {
        return std::min(theme_button_size, title + (int)border_size);
    }

    return title * 0.64;
}

int decoration_theme_t::get_button_spacing() const
{
    get_title_height();  // loads the theme
    return (title_height < 0) && (theme_button_size > 0) ? theme_button_spacing : 5;
}

int decoration_theme_t::get_button_edge() const
{
    get_title_height();
    return (title_height < 0) && (theme_button_size > 0) ?
           std::max(theme_button_edge, 0) : (int)border_size + 8;
}

/** @return The available border for resizing */
int decoration_theme_t::get_border_size() const
{
    return border_size;
}

/** @return The corner radius for rounded titlebar */
int decoration_theme_t::get_corner_radius() const
{
    return corner_radius;
}

/** @return The available border for resizing */
void decoration_theme_t::set_buttons(button_type_t flags)
{
    button_flags = flags;
}

void decoration_theme_t::invalidate_cache() const
{
    bg_cache.valid = false;
}

/**
 * Fill the given rectangle with the background color(s).
 *
 * @param data The render data (pass, target, damage)
 * @param rectangle The rectangle to redraw.
 * @param active Whether to use active or inactive colors
 */
void decoration_theme_t::render_background(const wf::scene::render_instruction_t& data,
    wf::geometry_t rectangle, bool active, bool tiled) const
{
    // Lazy initialization: load GTK theme on first render
    load_gtk_theme();

    /* Tiled windows have square corners, as in GTK */
    const int corner_radius = tiled ? 0 : this->corner_radius;
    const int bottom_corner_radius = tiled ? 0 : this->bottom_corner_radius;

    const auto& shadow_layers = active ? shadow_active : shadow_inactive;
    const bool use_theme_shadow = theme_shadow && !shadow_active.empty();
    const bool shadow = !tiled && use_theme_shadow && !shadow_layers.empty();

    // Check if cached surfaces are still valid
    bool cache_hit = bg_cache.valid &&
        bg_cache.geometry.width == rectangle.width &&
        bg_cache.geometry.height == rectangle.height &&
        bg_cache.active == active &&
        bg_cache.shadow == shadow &&
        bg_cache.tiled == tiled;

    if (cache_hit)
    {
        // Just re-submit cached textures
        if (bg_cache.shadow_tex)
        {
            data.pass->add_texture(bg_cache.shadow_tex->get_texture(),
                data.target, bg_cache.shadow_rect + wf::pointf_t{rectangle.x, rectangle.y}
                    - wf::pointf_t{bg_cache.geometry.x, bg_cache.geometry.y}, data.damage);
        }
        if (bg_cache.titlebar_tex)
        {
            data.pass->add_texture(bg_cache.titlebar_tex->get_texture(),
                data.target, bg_cache.titlebar_rect + wf::pointf_t{rectangle.x, rectangle.y}
                    - wf::pointf_t{bg_cache.geometry.x, bg_cache.geometry.y}, data.damage);
        }
        if (bg_cache.left_tex)
        {
            data.pass->add_texture(bg_cache.left_tex->get_texture(),
                data.target, bg_cache.left_rect + wf::pointf_t{rectangle.x, rectangle.y}
                    - wf::pointf_t{bg_cache.geometry.x, bg_cache.geometry.y}, data.damage);
        }
        if (bg_cache.right_tex)
        {
            data.pass->add_texture(bg_cache.right_tex->get_texture(),
                data.target, bg_cache.right_rect + wf::pointf_t{rectangle.x, rectangle.y}
                    - wf::pointf_t{bg_cache.geometry.x, bg_cache.geometry.y}, data.damage);
        }
        if (bg_cache.bottom_tex)
        {
            data.pass->add_texture(bg_cache.bottom_tex->get_texture(),
                data.target, bg_cache.bottom_rect + wf::pointf_t{rectangle.x, rectangle.y}
                    - wf::pointf_t{bg_cache.geometry.x, bg_cache.geometry.y}, data.damage);
        }
        if (bg_cache.outline_tex)
        {
            data.pass->add_texture(bg_cache.outline_tex->get_texture(),
                data.target, bg_cache.outline_rect + wf::pointf_t{rectangle.x, rectangle.y}
                    - wf::pointf_t{bg_cache.geometry.x, bg_cache.geometry.y}, data.damage);
        }
        return;
    }

    // Cache miss — regenerate all surfaces
    bg_cache.geometry = rectangle;
    bg_cache.active = active;
    bg_cache.shadow = shadow;
    bg_cache.tiled = tiled;
    bg_cache.valid = true;
    bg_cache.shadow_tex.reset();
    bg_cache.outline_tex.reset();

    /* The theme's window shadow, under everything else */
    if (shadow)
    {
        const auto ext = extents_of(shadow_layers);
        const int pad  = std::max({ext.left, ext.right, ext.top, ext.bottom});
        if (auto surface = render_shadow(shadow_layers, rectangle.width, rectangle.height, pad))
        {
            bg_cache.shadow_rect = {
                rectangle.x - pad, rectangle.y - pad,
                rectangle.width + 2.0 * pad, rectangle.height + 2.0 * pad
            };
            bg_cache.shadow_tex = std::make_unique<wf::owned_texture_t>(surface);
            data.pass->add_texture(bg_cache.shadow_tex->get_texture(), data.target,
                bg_cache.shadow_rect, data.damage);
            cairo_surface_destroy(surface);
        }
    }

    // Use theme colors if available, otherwise fall back to config colors
    wf::color_t bg_color = active ? theme_titlebar_bg_active : theme_titlebar_bg_inactive;

    // If theme colors are still default (black), use config colors as ultimate fallback
    if (bg_color.r == 0.0 && bg_color.g == 0.0 && bg_color.b == 0.0)
    {
        bg_color = active ? active_color : inactive_color;
    }

    // Use a lighter semi-transparent border to contrast with dark backgrounds (like native GTK)
    wf::color_t outline_color;
    outline_color.r = 1.0;
    outline_color.g = 1.0;
    outline_color.b = 1.0;
    outline_color.a = 0.10;  // Subtle semi-transparent white border

    // Calculate titlebar area (top portion with rounded corners)
    int titlebar_h = get_title_height() + border_size;
    wf::geometry_t titlebar_rect = {
        rectangle.x, rectangle.y,
        rectangle.width, (double)titlebar_h
    };

    // Shadow parameters
    int shadow_offset = 2;
    int shadow_blur = 3;

    // --- Titlebar surface ---
    {
        auto titlebar_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            titlebar_rect.width + shadow_blur * 2, titlebar_rect.height + shadow_blur);
        auto cr = cairo_create(titlebar_surface);

        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        double radius = corner_radius;
        double x = shadow_blur, y = shadow_blur;
        double w = titlebar_rect.width, h = titlebar_rect.height;

        auto draw_rounded_rect_path = [&]() {
            cairo_new_sub_path(cr);
            cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
            cairo_line_to(cr, x + w - radius, y);
            cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI / 2, 0);
            cairo_line_to(cr, x + w, y + h);
            cairo_line_to(cr, x, y + h);
            cairo_close_path(cr);
        };

        // Drop shadow
        for (int i = use_theme_shadow ? 0 : shadow_blur; i > 0; i--)
        {
            cairo_save(cr);
            cairo_new_sub_path(cr);
            double shadow_x = shadow_blur + 0.5;
            double shadow_y = shadow_blur + shadow_offset + 0.5;
            cairo_arc(cr, shadow_x + radius, shadow_y + radius, radius, M_PI, 3 * M_PI / 2);
            cairo_line_to(cr, shadow_x + w - radius, shadow_y);
            cairo_arc(cr, shadow_x + w - radius, shadow_y + radius, radius, -M_PI / 2, 0);
            cairo_line_to(cr, shadow_x + w, shadow_y + h);
            cairo_line_to(cr, shadow_x, shadow_y + h);
            cairo_close_path(cr);

            double alpha = 0.1 * (1.0 - (double)i / shadow_blur);
            cairo_set_source_rgba(cr, 0, 0, 0, alpha);
            cairo_set_line_width(cr, i * 2.0);
            cairo_stroke(cr);
            cairo_restore(cr);
        }

        draw_rounded_rect_path();
        cairo_set_source_rgba(cr, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
        cairo_fill(cr);
        cairo_destroy(cr);

        bg_cache.titlebar_rect = {
            titlebar_rect.x - shadow_blur,
            titlebar_rect.y - shadow_blur,
            titlebar_rect.width + shadow_blur * 2,
            titlebar_rect.height + shadow_blur
        };
        bg_cache.titlebar_tex = std::make_unique<wf::owned_texture_t>(titlebar_surface);
        data.pass->add_texture(bg_cache.titlebar_tex->get_texture(), data.target,
            bg_cache.titlebar_rect, data.damage);
        cairo_surface_destroy(titlebar_surface);
    }

    // --- Left border ---
    if (border_size > 0)
    {
        int border_h = rectangle.height - corner_radius - bottom_corner_radius + 1;
        auto left_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            border_size + shadow_blur, border_h);
        auto left_cr = cairo_create(left_surface);

        cairo_set_operator(left_cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(left_cr);
        cairo_set_operator(left_cr, CAIRO_OPERATOR_OVER);

        for (int i = use_theme_shadow ? 0 : shadow_blur; i > 0; i--)
        {
            double alpha = 0.1 * (1.0 - (double)i / shadow_blur);
            cairo_set_source_rgba(left_cr, 0, 0, 0, alpha);
            cairo_set_line_width(left_cr, i * 2.0);
            cairo_move_to(left_cr, shadow_blur + 0.5, 0);
            cairo_line_to(left_cr, shadow_blur + 0.5, border_h);
            cairo_stroke(left_cr);
        }

        cairo_set_source_rgba(left_cr, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
        cairo_rectangle(left_cr, shadow_blur, 0, border_size, border_h);
        cairo_fill(left_cr);
        cairo_destroy(left_cr);

        bg_cache.left_rect = {
            rectangle.x - shadow_blur, rectangle.y + corner_radius - 1,
            (double)(border_size + shadow_blur), (double)border_h
        };
        bg_cache.left_tex = std::make_unique<wf::owned_texture_t>(left_surface);
        data.pass->add_texture(bg_cache.left_tex->get_texture(), data.target,
            bg_cache.left_rect, data.damage);
        cairo_surface_destroy(left_surface);
    }

    // --- Right border ---
    if (border_size > 0)
    {
        int border_h = rectangle.height - corner_radius - bottom_corner_radius + 1;
        auto right_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            border_size + shadow_blur, border_h);
        auto right_cr = cairo_create(right_surface);

        cairo_set_operator(right_cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(right_cr);
        cairo_set_operator(right_cr, CAIRO_OPERATOR_OVER);

        for (int i = use_theme_shadow ? 0 : shadow_blur; i > 0; i--)
        {
            double alpha = 0.1 * (1.0 - (double)i / shadow_blur);
            cairo_set_source_rgba(right_cr, 0, 0, 0, alpha);
            cairo_set_line_width(right_cr, i * 2.0);
            cairo_move_to(right_cr, border_size - 0.5, 0);
            cairo_line_to(right_cr, border_size - 0.5, border_h);
            cairo_stroke(right_cr);
        }

        cairo_set_source_rgba(right_cr, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
        cairo_rectangle(right_cr, 0, 0, border_size, border_h);
        cairo_fill(right_cr);
        cairo_destroy(right_cr);

        bg_cache.right_rect = {
            rectangle.x + rectangle.width - border_size, rectangle.y + corner_radius - 1,
            (double)(border_size + shadow_blur), (double)border_h
        };
        bg_cache.right_tex = std::make_unique<wf::owned_texture_t>(right_surface);
        data.pass->add_texture(bg_cache.right_tex->get_texture(), data.target,
            bg_cache.right_rect, data.damage);
        cairo_surface_destroy(right_surface);
    }

    // --- Bottom border with rounded bottom corners ---
    if (border_size > 0)
    {
        double br = bottom_corner_radius;
        int bottom_h = border_size + (int)br;  // Extra height for corner arcs
        int surface_w = rectangle.width + shadow_blur * 2;
        int surface_h = bottom_h + shadow_blur;
        auto bottom_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            surface_w, surface_h);
        auto bottom_cr = cairo_create(bottom_surface);

        cairo_set_operator(bottom_cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(bottom_cr);
        cairo_set_operator(bottom_cr, CAIRO_OPERATOR_OVER);

        // Draw shadow along the bottom with rounded corners
        for (int i = use_theme_shadow ? 0 : shadow_blur; i > 0; i--)
        {
            double alpha = 0.1 * (1.0 - (double)i / shadow_blur);
            cairo_set_source_rgba(bottom_cr, 0, 0, 0, alpha);
            cairo_set_line_width(bottom_cr, i * 2.0);

            double sx = shadow_blur;
            double sy = shadow_offset;
            double sw = rectangle.width;

            cairo_new_sub_path(bottom_cr);
            cairo_move_to(bottom_cr, sx, sy + 0.5);
            cairo_line_to(bottom_cr, sx + sw, sy + 0.5);
            cairo_line_to(bottom_cr, sx + sw, sy + bottom_h - br + 0.5);
            cairo_arc(bottom_cr, sx + sw - br, sy + bottom_h - br + 0.5, br, 0, M_PI / 2);
            cairo_line_to(bottom_cr, sx + br, sy + bottom_h + 0.5);
            cairo_arc(bottom_cr, sx + br, sy + bottom_h - br + 0.5, br, M_PI / 2, M_PI);
            cairo_close_path(bottom_cr);
            cairo_stroke(bottom_cr);
        }

        // Fill bottom area with rounded bottom corners
        double bx = shadow_blur;
        double by = 0;
        double bw = rectangle.width;

        cairo_new_sub_path(bottom_cr);
        cairo_move_to(bottom_cr, bx, by);
        cairo_line_to(bottom_cr, bx + bw, by);
        cairo_line_to(bottom_cr, bx + bw, by + bottom_h - br);
        cairo_arc(bottom_cr, bx + bw - br, by + bottom_h - br, br, 0, M_PI / 2);
        cairo_line_to(bottom_cr, bx + br, by + bottom_h);
        cairo_arc(bottom_cr, bx + br, by + bottom_h - br, br, M_PI / 2, M_PI);
        cairo_close_path(bottom_cr);

        cairo_set_source_rgba(bottom_cr, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
        cairo_fill(bottom_cr);
        cairo_destroy(bottom_cr);

        bg_cache.bottom_rect = {
            rectangle.x - shadow_blur,
            rectangle.y + rectangle.height - border_size - (int)br,
            (double)surface_w, (double)surface_h
        };
        bg_cache.bottom_tex = std::make_unique<wf::owned_texture_t>(bottom_surface);
        data.pass->add_texture(bg_cache.bottom_tex->get_texture(), data.target,
            bg_cache.bottom_rect, data.damage);
        cairo_surface_destroy(bottom_surface);
    }

    // --- Unified outline: rounded top corners, rounded bottom corners ---
    /* A theme shadow brings its own outline (GTK themes draw it as a 1px
     * spread layer of the box-shadow) */
    if (!use_theme_shadow)
    {
        auto outline_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            rectangle.width, rectangle.height);
        auto outline_cr = cairo_create(outline_surface);

        cairo_set_operator(outline_cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(outline_cr);
        cairo_set_operator(outline_cr, CAIRO_OPERATOR_OVER);

        double r = corner_radius;
        double br = bottom_corner_radius;
        double w = rectangle.width;
        double h = rectangle.height;

        cairo_new_sub_path(outline_cr);
        // Top-left arc
        cairo_arc(outline_cr, 0.5 + r, 0.5 + r, r, M_PI, 3 * M_PI / 2);
        // Top-right arc
        cairo_arc(outline_cr, w - 0.5 - r, 0.5 + r, r, -M_PI / 2, 0);
        // Down to bottom-right arc
        cairo_line_to(outline_cr, w - 0.5, h - 0.5 - br);
        cairo_arc(outline_cr, w - 0.5 - br, h - 0.5 - br, br, 0, M_PI / 2);
        // Across bottom to bottom-left arc
        cairo_arc(outline_cr, 0.5 + br, h - 0.5 - br, br, M_PI / 2, M_PI);
        // Close path (up left side)
        cairo_close_path(outline_cr);

        cairo_set_source_rgba(outline_cr, outline_color.r, outline_color.g,
            outline_color.b, outline_color.a);
        cairo_set_line_width(outline_cr, 1.0);
        cairo_stroke(outline_cr);
        cairo_destroy(outline_cr);

        bg_cache.outline_rect = {
            rectangle.x, rectangle.y,
            rectangle.width, rectangle.height
        };
        bg_cache.outline_tex = std::make_unique<wf::owned_texture_t>(outline_surface);
        data.pass->add_texture(bg_cache.outline_tex->get_texture(), data.target,
            bg_cache.outline_rect, data.damage);
        cairo_surface_destroy(outline_surface);
    }
}

/**
 * Render the given text on a cairo_surface_t with the given size.
 * The caller is responsible for freeing the memory afterwards.
 */
cairo_surface_t*decoration_theme_t::render_text(const std::string& text,
    int width, int height, int button_area_width, bool active) const
{
    const auto format = CAIRO_FORMAT_ARGB32;
    auto surface = cairo_image_surface_create(format, width, height);

    if (height == 0)
    {
        return surface;
    }

    // Lazy initialization: load GTK theme to get font settings
    load_gtk_theme();

    wf::color_t color = active ? theme_titlebar_fg_active : theme_titlebar_fg_inactive;
    // Use config font color as fallback
    if (color.r == 0.0 && color.g == 0.0 && color.b == 0.0)
    {
        color = font_color;
    }

    auto cr = cairo_create(surface);

    PangoFontDescription *font_desc;
    PangoLayout *layout;

    // Use GTK font with scaling to match native GTK titlebar size
    std::string gtk_font_now = get_gtk_font_name();
    std::string font_to_use;

    if (!gtk_font_now.empty())
    {
        // Parse the GTK font (format: "Font Name Weight Size")
        size_t last_space = gtk_font_now.rfind(' ');
        if (last_space != std::string::npos)
        {
            std::string font_family = gtk_font_now.substr(0, last_space);
            std::string font_size_str = gtk_font_now.substr(last_space + 1);
            try {
                int base_size = std::stoi(font_size_str);
                // Scale font size to match native GTK titlebar (typically 1.3-1.4x larger)
                int scaled_size = static_cast<int>(base_size * 1.12);
                font_to_use = font_family + " Bold " + std::to_string(scaled_size);
            } catch (...) {
                font_to_use = gtk_font_now;  // Use as-is if parsing fails
            }
        }
        else
        {
            font_to_use = gtk_font_now + " 11";
        }
    }
    else
    {
        // Fallback if GTK font can't be read
        font_to_use = "Sans 11";
    }

    font_desc = pango_font_description_from_string(font_to_use.c_str());

    layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), text.size());

    // Reserve space for buttons on the left and mirror on the right for centering
    int left_padding = (button_area_width > 0) ? button_area_width : 10;
    int right_padding = left_padding;  // Mirror for true visual centering
    int text_width = width - left_padding - right_padding;
    if (text_width < 0) text_width = width;  // Fallback if window too narrow
    int text_x = left_padding;

    // Center the text horizontally within the available space
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_width(layout, text_width * PANGO_SCALE);

    // Prevent wrapping and add ellipsis for long titles
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);

    // Center vertically
    PangoRectangle ink_rect, logical_rect;
    pango_layout_get_pixel_extents(layout, &ink_rect, &logical_rect);
    int text_y = (height - logical_rect.height) / 2 - logical_rect.y;

    cairo_move_to(cr, text_x, text_y);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    pango_cairo_show_layout(cr, layout);

    pango_font_description_free(font_desc);
    g_object_unref(layout);
    cairo_destroy(cr);

    return surface;
}

cairo_surface_t*decoration_theme_t::get_button_surface(button_type_t button,
    const button_state_t& state) const
{
    // Lazy initialization: load GTK theme on first render
    load_gtk_theme();

    /* Pixmap style: if the GTK theme ships metacity-1 titlebuttons, the asset
     * is the complete button - background, colour and glyph - so it replaces
     * the drawn circle and symbolic icon entirely. */
    const std::string style = button_style;
    if (style != "gtk")
    {
        const std::string asset = find_titlebutton_file(button, state);
        if (!asset.empty())
        {
            cairo_surface_t *src = get_titlebutton_asset(asset, static_cast<int>(state.width));
            if (src)
            {
                cairo_surface_t *out = cairo_image_surface_create(
                    CAIRO_FORMAT_ARGB32, state.width, state.height);
                cairo_t *out_cr = cairo_create(out);
                cairo_set_source_surface(out_cr, src, 0, 0);
                cairo_paint(out_cr);
                cairo_destroy(out_cr);
                return out;
            }
        }

        /* Buttons the theme builds from shared images in its Metacity XML */
        if (metacity)
        {
            metacity_theme_t::request_t request;
            switch (button)
            {
              case BUTTON_CLOSE:
                request.function = "close";
                break;

              case BUTTON_TOGGLE_MAXIMIZE:
                request.function = "maximize";
                break;

              case BUTTON_MINIMIZE:
                request.function = "minimize";
                break;
            }

            request.focused   = state.activated;
            request.maximized = state.maximized;
            request.state     = state.hover_progress < 0 ? "pressed" :
                (state.hover_progress > 0 ? "prelight" : "normal");
            request.on_right = state.on_right;
            request.index    = state.index;
            request.count    = state.count;
            request.size     = static_cast<int>(state.width);
            request.scale    = state.scale;

            if (cairo_surface_t *src = metacity->get_button(request))
            {
                cairo_surface_t *out = cairo_image_surface_create(
                    CAIRO_FORMAT_ARGB32, state.width, state.height);
                cairo_t *out_cr = cairo_create(out);
                cairo_set_source_surface(out_cr, src, 0, 0);
                cairo_paint(out_cr);
                cairo_destroy(out_cr);
                return out;
            }
        }
    }

    cairo_surface_t *button_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, state.width, state.height);

    auto cr = cairo_create(button_surface);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

    /* Clear the button background */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_rectangle(cr, 0, 0, state.width, state.height);
    cairo_fill(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Determine button background color based on hover/active state
    wf::color_t bg_color = theme_button_bg;
    if (state.hover_progress > 0)
    {
        // Hover state
        bg_color = theme_button_hover_bg;
    }
    else if (state.hover_progress < 0)
    {
        // Active/pressed state
        bg_color = theme_button_active_bg;
    }

    const bool hovered = state.hover_progress > 0;
    const bool pressed = state.hover_progress < 0;
    bool show_icon     = state.activated || hovered;

    /* A theme that colours its buttons individually decides each state,
     * including whether the glyph shows at all. */
    wf::color_t themed_icon_color{};
    const auto themed = titlebutton_colors.find(button);
    if (themed != titlebutton_colors.end())
    {
        const auto& colors = themed->second;
        bg_color = pressed ? colors.pressed_bg :
            (state.activated ? colors.bg : colors.backdrop_bg);

        themed_icon_color = !(hovered || pressed) ? colors.fg :
            (state.activated ? colors.hover_fg : colors.backdrop_hover_fg);
        show_icon = themed_icon_color.a > 0;
    }

    // Draw circular button background - GTK style
    // Larger circle so there's visible padding between icon and circle edge
    double radius = (state.width / 2.0) - 2.5;
    cairo_arc(cr, state.width / 2, state.height / 2, radius, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    cairo_fill(cr);

    // Draw icon when window is active or when button is hovered (like native GTK)
    if (show_icon)
    {
        // Use theme text color for icon, with fallback
        wf::color_t icon_color = theme_titlebar_fg_active;
        if (icon_color.r == 0.0 && icon_color.g == 0.0 && icon_color.b == 0.0)
        {
            icon_color = font_color;  // Fallback to config color
        }

        // Apply transparency based on hover state - semi-transparent by default, opaque on hover
        if (state.hover_progress > 0)
        {
            // Hovered: full opacity white
            icon_color.r = icon_color.g = icon_color.b = 1.0;
            icon_color.a = 1.0;
        }
        else
        {
            // Default: semi-transparent
            icon_color.a = 0.7;
        }

        if (themed != titlebutton_colors.end())
        {
            icon_color = themed_icon_color;
        }

        // Try to load icon from theme
        std::string icon_name;
        switch (button)
        {
          case BUTTON_CLOSE:
            icon_name = "window-close-symbolic";
            break;
          case BUTTON_TOGGLE_MAXIMIZE:
            icon_name = "window-maximize-symbolic";
            break;
          case BUTTON_MINIMIZE:
            icon_name = "window-minimize-symbolic";
            break;
          default:
            break;
        }

        std::string icon_path = find_icon_file(icon_name, static_cast<int>(state.width));
        bool icon_loaded = false;

#ifdef HAVE_LIBRSVG
        if (!icon_path.empty() && icon_path.find(".svg") != std::string::npos)
        {
            GError *error = NULL;
            RsvgHandle *handle = rsvg_handle_new_from_file(icon_path.c_str(), &error);

            if (handle)
            {
                // Create temporary surface for the SVG
                cairo_surface_t *icon_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                    state.width, state.height);
                cairo_t *icon_cr = cairo_create(icon_surface);

                // Render SVG with padding inside the button circle
                /* GTK's titlebutton icon is 16px; on small buttons, shrink it to fit */
                double icon_size = std::min(state.width * 0.85, 16.0 * state.scale);
                double offset = (state.width - icon_size) / 2.0;
                RsvgRectangle viewport = {
                    .x = offset,
                    .y = offset,
                    .width = icon_size,
                    .height = icon_size
                };

                // Render SVG to temporary surface
                if (rsvg_handle_render_document(handle, icon_cr, &viewport, &error))
                {
                    // Use the SVG as a mask and paint with the icon color
                    cairo_set_source_rgba(cr, icon_color.r, icon_color.g, icon_color.b, icon_color.a);
                    cairo_mask_surface(cr, icon_surface, 0, 0);
                    icon_loaded = true;
                }

                cairo_destroy(icon_cr);
                cairo_surface_destroy(icon_surface);
                g_object_unref(handle);
            }

            if (error)
            {
                g_error_free(error);
            }
        }
#endif

        // Fallback to drawing icons if loading failed
        // cppcheck-suppress knownConditionTrueFalse
        if (!icon_loaded)
        {
            cairo_set_line_width(cr, 1.0 * state.border);
            cairo_set_source_rgba(cr, icon_color.r, icon_color.g, icon_color.b, icon_color.a);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

            switch (button)
            {
              case BUTTON_CLOSE:
                // X icon - much smaller with substantial padding like GTK
                cairo_move_to(cr, state.width * 0.49, state.height * 0.49);
                cairo_line_to(cr, state.width * 0.51, state.height * 0.51);
                cairo_move_to(cr, state.width * 0.51, state.height * 0.49);
                cairo_line_to(cr, state.width * 0.49, state.height * 0.51);
                cairo_stroke(cr);
                break;

              case BUTTON_TOGGLE_MAXIMIZE:
                // Square icon - much smaller with substantial padding like GTK
                {
                    double size = state.width * 0.035;
                    double x = (state.width - size) / 2.0;
                    double y = (state.height - size) / 2.0;
                    cairo_rectangle(cr, x, y, size, size);
                    cairo_stroke(cr);
                }
                break;

              case BUTTON_MINIMIZE:
                // Minus icon - much smaller with substantial padding like GTK
                cairo_move_to(cr, state.width * 0.49, state.height * 0.5);
                cairo_line_to(cr, state.width * 0.51, state.height * 0.5);
                cairo_stroke(cr);
                break;

              default:
                assert(false);
            }
        }
    } // end if (state.activated)

    cairo_destroy(cr);
    return button_surface;
}

namespace
{
std::string trim_ws(const std::string& s)
{
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }

    return s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
}

/** Split @s at @sep (or at whitespace if @sep is ' '), except inside parentheses */
std::vector<std::string> split_css(const std::string& s, char sep)
{
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    for (char c : s)
    {
        depth += (c == '(') - (c == ')');
        const bool split = depth == 0 && (sep == ' ' ? isspace(static_cast<unsigned char>(c)) : c == sep);
        if (split)
        {
            if (!trim_ws(current).empty() || (sep != ' '))
            {
                parts.push_back(trim_ws(current));
            }

            current.clear();
        } else
        {
            current += c;
        }
    }

    if (!trim_ws(current).empty() || (sep != ' '))
    {
        parts.push_back(trim_ws(current));
    }

    return parts;
}

/** GTK's shade(): scale lightness and saturation in HLS space */
wf::color_t shade_color(wf::color_t c, double factor)
{
    const double max = std::max({c.r, c.g, c.b}), min = std::min({c.r, c.g, c.b});
    double h = 0, s = 0, l = (max + min) / 2;
    if (max != min)
    {
        const double d = max - min;
        s = l <= 0.5 ? d / (max + min) : d / (2 - max - min);
        if (max == c.r)
        {
            h = (c.g - c.b) / d;
        } else if (max == c.g)
        {
            h = 2 + (c.b - c.r) / d;
        } else
        {
            h = 4 + (c.r - c.g) / d;
        }

        h *= 60;
        if (h < 0)
        {
            h += 360;
        }
    }

    l = std::clamp(l * factor, 0.0, 1.0);
    s = std::clamp(s * factor, 0.0, 1.0);
    if (s == 0)
    {
        return {l, l, l, c.a};
    }

    const double m2 = l <= 0.5 ? l * (1 + s) : l + s - l * s, m1 = 2 * l - m2;
    auto channel = [&] (double hue)
    {
        hue = std::fmod(hue + 360, 360);
        if (hue < 60)
        {
            return m1 + (m2 - m1) * hue / 60;
        }

        if (hue < 180)
        {
            return m2;
        }

        if (hue < 240)
        {
            return m1 + (m2 - m1) * (240 - hue) / 60;
        }

        return m1;
    };

    return {channel(h + 120), channel(h), channel(h - 120), c.a};
}

/** In-place box blur of an A8 surface, three passes each way (about a Gaussian of @sigma) */
void blur_alpha(cairo_surface_t *surface, double sigma)
{
    if (sigma <= 0.25)
    {
        return;
    }

    cairo_surface_flush(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    const int width  = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);

    /* Box sizes whose repeated application approximates the Gaussian */
    const int passes = 3;
    const double ideal = std::sqrt(12 * sigma * sigma / passes + 1);
    int lower = std::floor(ideal);
    if (lower % 2 == 0)
    {
        lower--;
    }

    const int upper = lower + 2;
    const int lower_count = std::lround((12 * sigma * sigma - passes * lower * lower -
        4 * passes * lower - 3 * passes) / (-4.0 * lower - 4));

    std::vector<int> line(std::max(width, height));
    auto blur_line = [&] (unsigned char *start, int length, int step, int radius)
    {
        for (int i = 0; i < length; i++)
        {
            line[i] = start[i * step];
        }

        int sum = 0;
        for (int i = -radius; i <= radius; i++)
        {
            sum += (i >= 0 && i < length) ? line[i] : 0;
        }

        const int window = 2 * radius + 1;
        for (int i = 0; i < length; i++)
        {
            start[i * step] = static_cast<unsigned char>((sum + window / 2) / window);
            const int add = i + radius + 1, drop = i - radius;
            sum += (add < length ? line[add] : 0) - (drop >= 0 ? line[drop] : 0);
        }
    };

    for (int pass = 0; pass < passes; pass++)
    {
        const int radius = ((pass < lower_count ? lower : upper) - 1) / 2;
        if (radius <= 0)
        {
            continue;
        }

        for (int y = 0; y < height; y++)
        {
            blur_line(data + y * stride, width, 1, radius);
        }

        for (int x = 0; x < width; x++)
        {
            blur_line(data + x, height, stride, radius);
        }
    }

    cairo_surface_mark_dirty(surface);
}
}

std::optional<wf::color_t> decoration_theme_t::lookup_css_color(const std::string& name, int depth) const
{
    auto it = css_colors.find(trim_ws(name));
    if ((it == css_colors.end()) || (depth > 8))
    {
        return {};
    }

    return resolve_css_value(it->second, depth + 1);
}

std::optional<wf::color_t> decoration_theme_t::resolve_css_value(const std::string& raw, int depth) const
{
    const std::string value = trim_ws(raw);
    if (value.empty() || (depth > 8))
    {
        return {};
    }

    if (value[0] == '@')
    {
        return lookup_css_color(value.substr(1), depth + 1);
    }

    std::vector<std::string> args;
    auto call = [&] (const std::string& fn)
    {
        if ((value.size() <= fn.size() + 1) || (value.compare(0, fn.size(), fn) != 0) ||
            (value[fn.size()] != '(') || (value.back() != ')'))
        {
            return false;
        }

        args = split_css(value.substr(fn.size() + 1, value.size() - fn.size() - 2), ',');
        return true;
    };

    if (call("alpha") && (args.size() == 2))
    {
        auto color = resolve_css_value(args[0], depth + 1);
        if (color)
        {
            color->a *= strtod(args[1].c_str(), nullptr);
        }

        return color;
    }

    if (call("shade") && (args.size() == 2))
    {
        auto color = resolve_css_value(args[0], depth + 1);
        if (!color)
        {
            return {};
        }

        return shade_color(*color, strtod(args[1].c_str(), nullptr));
    }

    if ((call("lighter") || call("darker")) && (args.size() == 1))
    {
        auto color = resolve_css_value(args[0], depth + 1);
        if (!color)
        {
            return {};
        }

        return shade_color(*color, value[0] == 'l' ? 1.3 : 0.7);
    }

    if (call("mix") && (args.size() == 3))
    {
        auto a = resolve_css_value(args[0], depth + 1);
        auto b = resolve_css_value(args[1], depth + 1);
        if (!a || !b)
        {
            return {};
        }

        const double t = strtod(args[2].c_str(), nullptr);
        return wf::color_t{a->r + (b->r - a->r) * t, a->g + (b->g - a->g) * t,
            a->b + (b->b - a->b) * t, a->a + (b->a - a->a) * t};
    }

    if ((value == "transparent") || (value == "white") || (value[0] == '#') ||
        (value.rfind("rgb", 0) == 0))
    {
        return parse_css_color(value);
    }

    if (value == "black")
    {
        return wf::color_t{0, 0, 0, 1};
    }

    return {};
}

std::vector<decoration_theme_t::css_shadow_t> decoration_theme_t::parse_box_shadow(
    const std::string& value) const
{
    std::vector<css_shadow_t> layers;
    if (trim_ws(value) == "none")
    {
        return layers;
    }

    for (const auto& item : split_css(value, ','))
    {
        std::vector<double> lengths;
        std::string color;
        bool usable = true;
        for (const auto& token : split_css(item, ' '))
        {
            if (token == "inset")
            {
                usable = false;  // inner shadows fall on the window, not around it
                break;
            }

            const char c = token[0];
            if (isdigit(static_cast<unsigned char>(c)) || (c == '-') || (c == '.'))
            {
                char *end = nullptr;
                const double length = strtod(token.c_str(), &end);
                const std::string unit = end;
                if (!unit.empty() && (unit != "px"))
                {
                    usable = false;
                    break;
                }

                lengths.push_back(length);
            } else if (color.empty())
            {
                color = token;
            } else
            {
                usable = false;
                break;
            }
        }

        if (!usable || (lengths.size() < 2) || (lengths.size() > 4) || color.empty())
        {
            continue;
        }

        auto resolved = resolve_css_value(color);
        if (!resolved || (resolved->a <= 0))
        {
            continue;
        }

        css_shadow_t layer;
        layer.dx     = lengths[0];
        layer.dy     = lengths[1];
        layer.blur   = lengths.size() > 2 ? std::max(0.0, lengths[2]) : 0;
        layer.spread = lengths.size() > 3 ? lengths[3] : 0;
        layer.color  = *resolved;
        layers.push_back(layer);
    }

    return layers;
}

decoration_theme_t::shadow_extents_t decoration_theme_t::extents_of(
    const std::vector<css_shadow_t>& layers) const
{
    shadow_extents_t ext;
    for (const auto& layer : layers)
    {
        /* A CSS blur radius b is a Gaussian of sigma b/2, visible to about 3 sigma */
        const double reach = std::ceil(1.5 * layer.blur) + layer.spread;
        ext.left   = std::max(ext.left, (int)std::ceil(reach - layer.dx));
        ext.right  = std::max(ext.right, (int)std::ceil(reach + layer.dx));
        ext.top    = std::max(ext.top, (int)std::ceil(reach - layer.dy));
        ext.bottom = std::max(ext.bottom, (int)std::ceil(reach + layer.dy));
    }

    return ext;
}

decoration_theme_t::shadow_extents_t decoration_theme_t::get_shadow_extents() const
{
    load_gtk_theme();
    if (!theme_shadow || shadow_active.empty())
    {
        return {};
    }

    const auto a = extents_of(shadow_active), b = extents_of(shadow_inactive);
    return {std::max(a.left, b.left), std::max(a.right, b.right),
        std::max(a.top, b.top), std::max(a.bottom, b.bottom)};
}

/**
 * Render the box-shadow @layers around a @width x @height frame, which sits
 * at (@pad, @pad) in the result; the frame itself is left clear, as CSS
 * clips an outer shadow to outside the box.
 *
 * Blurring a whole window-sized surface on every resize would be slow, so the
 * shadow is rendered for a small frame (just big enough that its corners
 * don't influence each other) and stretched: corners copied, the middle row
 * and column repeated.
 */
cairo_surface_t*decoration_theme_t::render_shadow(const std::vector<css_shadow_t>& layers,
    int width, int height, int pad) const
{
    if ((width <= 0) || (height <= 0))
    {
        return nullptr;
    }

    const double rt = corner_radius, rb = bottom_corner_radius;
    auto frame_path = [&] (cairo_t *cr, double x, double y, double w, double h, double grow)
    {
        x -= grow;
        y -= grow;
        w += 2 * grow;
        h += 2 * grow;
        if ((w <= 0) || (h <= 0))
        {
            return;
        }

        const double limit = std::min(w, h) / 2;
        const double t = std::clamp(rt + grow, 0.0, limit), b = std::clamp(rb + grow, 0.0, limit);
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + t, y + t, t, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, x + w - t, y + t, t, -M_PI / 2, 0);
        cairo_arc(cr, x + w - b, y + h - b, b, 0, M_PI / 2);
        cairo_arc(cr, x + b, y + h - b, b, M_PI / 2, M_PI);
        cairo_close_path(cr);
    };

    /* The small frame the shadow is actually rendered for */
    const int core = 2 * (static_cast<int>(std::max(rt, rb)) + pad) + 1;
    const int tw   = std::min(width, core), th = std::min(height, core);
    const int TW   = tw + 2 * pad, TH = th + 2 * pad;

    cairo_surface_t *tmpl = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, TW, TH);
    cairo_t *cr = cairo_create(tmpl);

    /* CSS paints the first shadow on top */
    for (auto it = layers.rbegin(); it != layers.rend(); ++it)
    {
        cairo_surface_t *mask = cairo_image_surface_create(CAIRO_FORMAT_A8, TW, TH);
        cairo_t *mask_cr = cairo_create(mask);
        frame_path(mask_cr, pad + it->dx, pad + it->dy, tw, th, it->spread);
        cairo_set_source_rgba(mask_cr, 1, 1, 1, 1);
        cairo_fill(mask_cr);
        cairo_destroy(mask_cr);

        blur_alpha(mask, it->blur / 2);

        cairo_set_source_rgba(cr, it->color.r, it->color.g, it->color.b, it->color.a);
        cairo_mask_surface(cr, mask, 0, 0);
        cairo_surface_destroy(mask);
    }

    cairo_destroy(cr);

    cairo_surface_t *out = tmpl;
    if ((tw != width) || (th != height))
    {
        const int FW = width + 2 * pad, FH = height + 2 * pad;
        out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, FW, FH);
        cairo_surface_flush(tmpl);
        cairo_surface_flush(out);

        const unsigned char *src = cairo_image_surface_get_data(tmpl);
        unsigned char *dst = cairo_image_surface_get_data(out);
        const int src_stride = cairo_image_surface_get_stride(tmpl);
        const int dst_stride = cairo_image_surface_get_stride(out);

        /* The template's middle column/row stands for everything between its corners */
        const int cx = pad + tw / 2, cy = pad + th / 2;
        auto map = [] (int f, int c, int F, int T)
        {
            if (f < c)
            {
                return f;
            }

            if (f >= F - (T - c - 1))
            {
                return f - (F - T);
            }

            return c;
        };

        for (int y = 0; y < FH; y++)
        {
            const uint32_t *src_row =
                reinterpret_cast<const uint32_t*>(src + map(y, cy, FH, TH) * src_stride);
            uint32_t *dst_row = reinterpret_cast<uint32_t*>(dst + y * dst_stride);
            for (int x = 0; x < FW; x++)
            {
                dst_row[x] = src_row[map(x, cx, FW, TW)];
            }
        }

        cairo_surface_mark_dirty(out);
        cairo_surface_destroy(tmpl);
    }

    /* Clear the shadow under the (translucent) frame */
    cr = cairo_create(out);
    frame_path(cr, pad, pad, width, height, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_fill(cr);
    cairo_destroy(cr);

    return out;
}
}
}
