#include "deco-metacity.hpp"

#ifdef HAVE_METACITY
#include <wayfire/util/log.hpp>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <librsvg/rsvg.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace wf
{
namespace decor
{
namespace
{
bool is_element(xmlNode *node, const char *name)
{
    return node->type == XML_ELEMENT_NODE &&
           xmlStrcmp(node->name, reinterpret_cast<const xmlChar*>(name)) == 0;
}

bool has_attr(xmlNode *node, const char *name)
{
    return xmlHasProp(node, reinterpret_cast<const xmlChar*>(name)) != nullptr;
}

std::string get_attr(xmlNode *node, const char *name)
{
    xmlChar *value = xmlGetProp(node, reinterpret_cast<const xmlChar*>(name));
    if (!value)
    {
        return "";
    }

    std::string result(reinterpret_cast<const char*>(value));
    xmlFree(value);
    return result;
}

std::string trim(const std::string& s)
{
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }

    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

/** Split @s at @sep, except inside parentheses */
std::vector<std::string> split_top_level(const std::string& s, char sep)
{
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    for (char c : s)
    {
        if (c == '(')
        {
            depth++;
        } else if (c == ')')
        {
            depth--;
        }

        if ((c == sep) && (depth == 0))
        {
            parts.push_back(trim(current));
            current.clear();
        } else
        {
            current += c;
        }
    }

    parts.push_back(trim(current));
    return parts;
}

/**
 * Whether an element's version="..." condition holds. We behave like a
 * Metacity 3.x that has every feature, so conditions such as ">= 3.5" hold
 * and "< 3.0" does not.
 */
bool version_matches(const std::string& condition)
{
    const std::string cond = trim(condition);
    if (cond.empty())
    {
        return true;
    }

    size_t i = 0;
    std::string op;
    while (i < cond.size() && strchr("<>=!", cond[i]))
    {
        op += cond[i++];
    }

    int major = 0, minor = 0;
    if (sscanf(cond.c_str() + i, " %d.%d", &major, &minor) < 1)
    {
        return false;
    }

    const int have = 3 * 1000 + 999;
    const int want = major * 1000 + minor;
    if ((op == ">=") || op.empty())
    {
        return have >= want;
    }

    if (op == ">")
    {
        return have > want;
    }

    if (op == "<=")
    {
        return have <= want;
    }

    if (op == "<")
    {
        return have < want;
    }

    if ((op == "=") || (op == "=="))
    {
        return have == want;
    }

    return false;
}

/** Arithmetic over Metacity's position expressions, e.g. "(width - object_width) / 2" */
class expression_t
{
  public:
    expression_t(const std::string& text, const std::map<std::string, double>& vars) :
        text(text), vars(vars)
    {}

    std::optional<double> evaluate()
    {
        double value = sum();
        skip_space();
        if (!ok || (pos != text.size()))
        {
            return {};
        }

        return value;
    }

  private:
    const std::string& text;
    const std::map<std::string, double>& vars;
    size_t pos = 0;
    bool ok    = true;

    void skip_space()
    {
        while (pos < text.size() && isspace(static_cast<unsigned char>(text[pos])))
        {
            pos++;
        }
    }

    bool accept(char c)
    {
        skip_space();
        if ((pos < text.size()) && (text[pos] == c))
        {
            pos++;
            return true;
        }

        return false;
    }

    double sum()
    {
        double value = product();
        while (ok)
        {
            if (accept('+'))
            {
                value += product();
            } else if (accept('-'))
            {
                value -= product();
            } else
            {
                break;
            }
        }

        return value;
    }

    double product()
    {
        double value = unary();
        while (ok)
        {
            if (accept('*'))
            {
                value *= unary();
            } else if (accept('/'))
            {
                double divisor = unary();
                if (divisor == 0)
                {
                    ok = false;
                    return 0;
                }

                value /= divisor;
            } else if (accept('%'))
            {
                double divisor = unary();
                if (divisor == 0)
                {
                    ok = false;
                    return 0;
                }

                value = std::fmod(value, divisor);
            } else
            {
                break;
            }
        }

        return value;
    }

    double unary()
    {
        if (accept('-'))
        {
            return -unary();
        }

        if (accept('+'))
        {
            return unary();
        }

        return primary();
    }

    double primary()
    {
        if (accept('('))
        {
            double value = sum();
            if (!accept(')'))
            {
                ok = false;
            }

            return value;
        }

        skip_space();
        if (pos >= text.size())
        {
            ok = false;
            return 0;
        }

        const char c = text[pos];
        if (isdigit(static_cast<unsigned char>(c)) || (c == '.'))
        {
            char *end = nullptr;
            double value = strtod(text.c_str() + pos, &end);
            pos = end - text.c_str();
            return value;
        }

        if (isalpha(static_cast<unsigned char>(c)) || (c == '_'))
        {
            size_t start = pos;
            while (pos < text.size() &&
                   (isalnum(static_cast<unsigned char>(text[pos])) || (text[pos] == '_')))
            {
                pos++;
            }

            auto it = vars.find(text.substr(start, pos - start));
            if (it == vars.end())
            {
                ok = false;
                return 0;
            }

            return it->second;
        }

        ok = false;
        return 0;
    }
};

std::optional<wf::color_t> parse_hex(const std::string& s)
{
    if (s.empty() || (s[0] != '#'))
    {
        return {};
    }

    std::string hex = s.substr(1);
    if (!std::all_of(hex.begin(), hex.end(), [] (char c) { return isxdigit(static_cast<unsigned char>(c)); }))
    {
        return {};
    }

    if (hex.size() == 3)
    {
        hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
    }

    if ((hex.size() != 6) && (hex.size() != 8))
    {
        return {};
    }

    auto channel = [&] (int i) { return std::stoi(hex.substr(i * 2, 2), nullptr, 16) / 255.0; };
    return wf::color_t{channel(0), channel(1), channel(2), hex.size() == 8 ? channel(3) : 1.0};
}

/** Load a theme image; @return its natural size, or nothing if it can't be read */
std::optional<std::pair<double, double>> image_size(const std::string& path)
{
    if ((path.size() > 4) && (path.compare(path.size() - 4, 4, ".png") == 0))
    {
        cairo_surface_t *png = cairo_image_surface_create_from_png(path.c_str());
        std::optional<std::pair<double, double>> size;
        if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS)
        {
            size = std::make_pair<double, double>(cairo_image_surface_get_width(png),
                cairo_image_surface_get_height(png));
        }

        cairo_surface_destroy(png);
        return size;
    }

    GError *error = nullptr;
    RsvgHandle *handle = rsvg_handle_new_from_file(path.c_str(), &error);
    if (!handle)
    {
        if (error)
        {
            g_error_free(error);
        }

        return {};
    }

    double width = 0, height = 0;
    if (!rsvg_handle_get_intrinsic_size_in_pixels(handle, &width, &height) ||
        (width <= 0) || (height <= 0))
    {
        /* Sized only by a viewBox: use that */
        gboolean has_w, has_h, has_viewbox;
        RsvgLength w, h;
        RsvgRectangle viewbox;
        rsvg_handle_get_intrinsic_dimensions(handle, &has_w, &w, &has_h, &h,
            &has_viewbox, &viewbox);
        width  = has_viewbox ? viewbox.width : 0;
        height = has_viewbox ? viewbox.height : 0;
    }

    g_object_unref(handle);
    if ((width <= 0) || (height <= 0))
    {
        return {};
    }

    return std::make_pair(width, height);
}

/** Render a theme image stretched to @width x @height (Metacity's fill_type="scale") */
cairo_surface_t *render_image(const std::string& path, int width, int height)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);
    bool ok = false;

    if ((path.size() > 4) && (path.compare(path.size() - 4, 4, ".png") == 0))
    {
        cairo_surface_t *png = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS)
        {
            cairo_scale(cr, (double)width / cairo_image_surface_get_width(png),
                (double)height / cairo_image_surface_get_height(png));
            cairo_set_source_surface(cr, png, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            ok = true;
        }

        cairo_surface_destroy(png);
    } else
    {
        GError *error = nullptr;
        RsvgHandle *handle = rsvg_handle_new_from_file(path.c_str(), &error);
        if (handle)
        {
            /* Stretch rather than letterbox: render the document into a unit
             * viewport and scale that to the requested box. */
            double natural_w = width, natural_h = height;
            if (auto natural = image_size(path))
            {
                natural_w = natural->first;
                natural_h = natural->second;
            }

            RsvgRectangle viewport = {0, 0, natural_w, natural_h};
            cairo_scale(cr, width / natural_w, height / natural_h);
            ok = rsvg_handle_render_document(handle, cr, &viewport, &error);
            g_object_unref(handle);
        }

        if (error)
        {
            LOGE("Failed to render Metacity image ", path, ": ", error->message);
            g_error_free(error);
        }
    }

    cairo_destroy(cr);
    if (!ok)
    {
        cairo_surface_destroy(surface);
        return nullptr;
    }

    return surface;
}

/**
 * Metacity's colorize: the image becomes a ramp through @color by brightness,
 * black - color - white, with mid grey mapping to the colour itself. The
 * colour's own alpha is not used, only the image's.
 */
void colorize(cairo_surface_t *surface, const wf::color_t& color)
{
    cairo_surface_flush(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    const int width  = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const double target[3] = {color.r, color.g, color.b};

    for (int y = 0; y < height; y++)
    {
        uint32_t *row = reinterpret_cast<uint32_t*>(data + y * stride);
        for (int x = 0; x < width; x++)
        {
            const uint32_t px = row[x];
            const uint32_t a  = px >> 24;
            if (a == 0)
            {
                continue;
            }

            /* Cairo stores premultiplied colour */
            const double r = ((px >> 16) & 0xff) / (double)a;
            const double g = ((px >> 8) & 0xff) / (double)a;
            const double b = (px & 0xff) / (double)a;
            const double intensity = r * 0.30 + g * 0.59 + b * 0.11;

            uint32_t out[3];
            for (int c = 0; c < 3; c++)
            {
                double v = intensity <= 0.5 ? target[c] * intensity * 2.0 :
                    target[c] + (1.0 - target[c]) * (intensity - 0.5) * 2.0;
                v = std::clamp(v, 0.0, 1.0);
                out[c] = static_cast<uint32_t>(std::lround(v * a));
            }

            row[x] = (a << 24) | (out[0] << 16) | (out[1] << 8) | out[2];
        }
    }

    cairo_surface_mark_dirty(surface);
}
}

metacity_theme_t::~metacity_theme_t()
{
    for (auto& [key, surface] : cache)
    {
        if (surface)
        {
            cairo_surface_destroy(surface);
        }
    }

    if (doc)
    {
        xmlFreeDoc(doc);
    }
}

bool metacity_theme_t::load(const std::string& theme_dir, color_lookup_t lookup)
{
    css_color    = std::move(lookup);
    metacity_dir = theme_dir + "/metacity-1/";

    for (const char *name : {"metacity-theme-3.xml", "metacity-theme-2.xml", "metacity-theme-1.xml"})
    {
        const std::string path = metacity_dir + name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
        {
            continue;
        }

        /* Themes write conditions such as version="< 3.1" unescaped, which
         * Metacity's parser accepts and XML does not */
        std::ifstream file(path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string text = buffer.str();
        for (size_t at = text.find("=\"<"); at != std::string::npos; at = text.find("=\"<", at))
        {
            text.replace(at + 2, 1, "&lt;");
        }

        doc = xmlReadMemory(text.data(), text.size(), path.c_str(), nullptr,
            XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
        if (doc)
        {
            LOGI("Reading Metacity theme ", path);
            break;
        }
    }

    if (!doc)
    {
        return false;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root || !is_element(root, "metacity_theme"))
    {
        return false;
    }

    for (xmlNode *node = root->children; node; node = node->next)
    {
        if ((node->type != XML_ELEMENT_NODE) || !version_matches(get_attr(node, "version")))
        {
            continue;
        }

        if (is_element(node, "constant"))
        {
            constants[get_attr(node, "name")] = get_attr(node, "value");
        } else if (is_element(node, "draw_ops"))
        {
            draw_ops[get_attr(node, "name")] = node;
        } else if (is_element(node, "frame_style"))
        {
            frame_style_t style;
            style.parent = get_attr(node, "parent");
            for (xmlNode *child = node->children; child; child = child->next)
            {
                if (is_element(child, "button") && version_matches(get_attr(child, "version")))
                {
                    style.buttons[get_attr(child, "function") + "/" + get_attr(child, "state")] = child;
                }
            }

            frame_styles[get_attr(node, "name")] = std::move(style);
        } else if (is_element(node, "frame_style_set"))
        {
            style_set_t set;
            set.parent = get_attr(node, "parent");
            for (xmlNode *child = node->children; child; child = child->next)
            {
                if (is_element(child, "frame"))
                {
                    set.frames.push_back({get_attr(child, "focus"), get_attr(child, "state"),
                        get_attr(child, "resize"), get_attr(child, "style")});
                }
            }

            style_sets[get_attr(node, "name")] = std::move(set);
        } else if (is_element(node, "window") && (get_attr(node, "type") == "normal"))
        {
            normal_style_set = get_attr(node, "style_set");
        }
    }

    /* Usable only if the close button of a plain window draws */
    request_t probe;
    probe.function = "close";
    probe.size     = 24;
    return get_button(probe) != nullptr;
}

std::string metacity_theme_t::find_frame_style(const std::string& set, bool focused,
    bool maximized, int depth) const
{
    auto it = style_sets.find(set);
    if ((it == style_sets.end()) || (depth > 8))
    {
        return "";
    }

    const std::string focus = focused ? "yes" : "no";
    const std::string state = maximized ? "maximized" : "normal";
    for (bool strict : {true, false})
    {
        for (const auto& frame : it->second.frames)
        {
            if ((frame.focus == focus) && (frame.state == state) &&
                (!strict || frame.resize.empty() || (frame.resize == "both")))
            {
                return frame.style;
            }
        }
    }

    return find_frame_style(it->second.parent, focused, maximized, depth + 1);
}

/** The draw_ops of a <button>, following the frame style's parents */
xmlNode*metacity_theme_t::find_ops(const std::string& style, const std::string& function,
    const std::string& state) const
{
    std::string current = style;
    for (int depth = 0; depth < 8 && !current.empty(); depth++)
    {
        auto it = frame_styles.find(current);
        if (it == frame_styles.end())
        {
            return nullptr;
        }

        auto button = it->second.buttons.find(function + "/" + state);
        if (button != it->second.buttons.end())
        {
            xmlNode *node = button->second;
            if (has_attr(node, "draw_ops"))
            {
                auto ops = draw_ops.find(get_attr(node, "draw_ops"));
                return ops == draw_ops.end() ? nullptr : ops->second;
            }

            for (xmlNode *child = node->children; child; child = child->next)
            {
                if (is_element(child, "draw_ops"))
                {
                    return child;
                }
            }

            return nullptr;
        }

        current = it->second.parent;
    }

    return nullptr;
}

/** find_ops() with Metacity's fallbacks for missing pieces and states */
xmlNode*metacity_theme_t::find_button_ops(const std::string& style, const std::string& function,
    const std::string& state) const
{
    if (xmlNode *ops = find_ops(style, function, state))
    {
        return ops;
    }

    auto ends_with = [&] (const std::string& suffix)
    {
        return function.size() > suffix.size() &&
               function.compare(function.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    if (ends_with("_single_background"))
    {
        const std::string side = function.substr(0, function.find('_'));
        if (xmlNode *ops = find_button_ops(style, side + "_left_background", state))
        {
            return ops;
        }
    }

    if (ends_with("_left_background") || ends_with("_right_background"))
    {
        const std::string side = function.substr(0, function.find('_'));
        if (xmlNode *ops = find_ops(style, side + "_middle_background", state))
        {
            return ops;
        }
    }

    if (state != "normal")
    {
        return find_button_ops(style, function, "normal");
    }

    return nullptr;
}

bool metacity_theme_t::collect_layers(xmlNode *ops, std::vector<layer_t>& out, int depth) const
{
    if (depth > 8)
    {
        return false;
    }

    for (xmlNode *op = ops->children; op; op = op->next)
    {
        if (op->type != XML_ELEMENT_NODE)
        {
            continue;
        }

        if (is_element(op, "include"))
        {
            /* An include drawn into a sub-rectangle needs real geometry */
            if (has_attr(op, "x") || has_attr(op, "y") || has_attr(op, "width") ||
                has_attr(op, "height"))
            {
                return false;
            }

            auto it = draw_ops.find(get_attr(op, "name"));
            if ((it == draw_ops.end()) || !collect_layers(it->second, out, depth + 1))
            {
                return false;
            }
        } else if (is_element(op, "image"))
        {
            const std::string fill = get_attr(op, "fill_type");
            if ((!fill.empty() && (fill != "scale")) ||
                has_attr(op, "top") || has_attr(op, "bottom") ||
                has_attr(op, "left") || has_attr(op, "right"))
            {
                return false;
            }

            layer_t layer;
            layer.file   = get_attr(op, "filename");
            layer.x      = get_attr(op, "x");
            layer.y      = get_attr(op, "y");
            layer.width  = get_attr(op, "width");
            layer.height = get_attr(op, "height");
            if (layer.file.empty() || layer.x.empty() || layer.y.empty() ||
                layer.width.empty() || layer.height.empty())
            {
                return false;
            }

            const std::string alpha = get_attr(op, "alpha");
            if (!alpha.empty())
            {
                /* "0.2:0.8" is an alpha gradient */
                char *end = nullptr;
                layer.alpha = strtod(alpha.c_str(), &end);
                if (!trim(end).empty())
                {
                    return false;
                }
            }

            if (has_attr(op, "colorize"))
            {
                layer.colorize = resolve_color(get_attr(op, "colorize"));
                if (!layer.colorize)
                {
                    return false;
                }
            }

            out.push_back(layer);
        } else
        {
            return false;
        }
    }

    return true;
}

std::optional<wf::color_t> metacity_theme_t::resolve_color(const std::string& raw, int depth) const
{
    const std::string spec = trim(raw);
    if (depth > 8)
    {
        return {};
    }

    auto constant = constants.find(spec);
    if (constant != constants.end())
    {
        return resolve_color(constant->second, depth + 1);
    }

    if (auto hex = parse_hex(spec))
    {
        return hex;
    }

    if (spec == "white")
    {
        return wf::color_t{1, 1, 1, 1};
    }

    if (spec == "black")
    {
        return wf::color_t{0, 0, 0, 1};
    }

    if ((spec.rfind("gtk:custom(", 0) == 0) && (spec.back() == ')'))
    {
        auto args = split_top_level(spec.substr(11, spec.size() - 12), ',');
        if (args.size() != 2)
        {
            return {};
        }

        if (auto color = css_color(args[0]))
        {
            return color;
        }

        return resolve_color(args[1], depth + 1);
    }

    if (spec.rfind("gtk:", 0) == 0)
    {
        /* gtk:fg[NORMAL] and friends, the GTK 2 style colours, mapped to
         * the names GTK 3 themes export them under */
        static const std::map<std::string, std::vector<std::string>> names = {
            {"fg[NORMAL]", {"theme_fg_color"}},
            {"fg[PRELIGHT]", {"theme_fg_color"}},
            {"fg[ACTIVE]", {"theme_fg_color"}},
            {"fg[SELECTED]", {"theme_selected_fg_color"}},
            {"fg[INSENSITIVE]", {"insensitive_fg_color", "theme_unfocused_fg_color"}},
            {"bg[NORMAL]", {"theme_bg_color"}},
            {"bg[PRELIGHT]", {"theme_bg_color"}},
            {"bg[ACTIVE]", {"theme_bg_color"}},
            {"bg[SELECTED]", {"theme_selected_bg_color"}},
            {"bg[INSENSITIVE]", {"insensitive_bg_color", "theme_unfocused_bg_color"}},
            {"base[NORMAL]", {"theme_base_color"}},
            {"base[SELECTED]", {"theme_selected_bg_color"}},
            {"base[INSENSITIVE]", {"insensitive_base_color", "theme_unfocused_base_color"}},
            {"text[NORMAL]", {"theme_text_color"}},
            {"text[SELECTED]", {"theme_selected_fg_color"}},
            {"text[INSENSITIVE]", {"insensitive_fg_color"}},
        };

        auto it = names.find(spec.substr(4));
        if (it == names.end())
        {
            return {};
        }

        for (const auto& name : it->second)
        {
            if (auto color = css_color(name))
            {
                return color;
            }
        }

        return {};
    }

    if (spec.rfind("blend/", 0) == 0)
    {
        auto parts = split_top_level(spec, '/');
        if (parts.size() != 4)
        {
            return {};
        }

        auto a = resolve_color(parts[1], depth + 1);
        auto b = resolve_color(parts[2], depth + 1);
        if (!a || !b)
        {
            return {};
        }

        const double t = strtod(parts[3].c_str(), nullptr);
        return wf::color_t{a->r + (b->r - a->r) * t, a->g + (b->g - a->g) * t,
            a->b + (b->b - a->b) * t, a->a + (b->a - a->a) * t};
    }

    if (spec.rfind("shade/", 0) == 0)
    {
        auto parts = split_top_level(spec, '/');
        if (parts.size() != 3)
        {
            return {};
        }

        auto c = resolve_color(parts[1], depth + 1);
        if (!c)
        {
            return {};
        }

        const double f = strtod(parts[2].c_str(), nullptr);
        return wf::color_t{std::clamp(c->r * f, 0.0, 1.0), std::clamp(c->g * f, 0.0, 1.0),
            std::clamp(c->b * f, 0.0, 1.0), c->a};
    }

    return {};
}

cairo_surface_t*metacity_theme_t::get_button(const request_t& r)
{
    if (!doc || (r.size <= 0))
    {
        return nullptr;
    }

    const std::string key = r.function + "/" + r.state + "/" + (r.focused ? "f" : "u") +
        (r.maximized ? "m" : "n") + (r.on_right ? "r" : "l") + std::to_string(r.index) + ":" +
        std::to_string(r.count) + "@" + std::to_string(r.size) + "x" + std::to_string(r.scale);
    auto it = cache.find(key);
    if (it != cache.end())
    {
        return it->second;
    }

    cairo_surface_t *surface = draw(r);
    cache[key] = surface;  // nullptr too, so a failure isn't retried every frame
    return surface;
}

cairo_surface_t*metacity_theme_t::draw(const request_t& r)
{
    std::string style = find_frame_style(normal_style_set, r.focused, r.maximized);
    if (style.empty() && r.maximized)
    {
        style = find_frame_style(normal_style_set, r.focused, false);
    }

    if (style.empty())
    {
        return nullptr;
    }

    /* The background piece under the button depends on where it is in the group */
    const std::string side = r.on_right ? "right" : "left";
    std::string piece;
    if (r.count == 1)
    {
        piece = side + "_single_background";
    } else if (r.index == 0)
    {
        piece = side + "_left_background";
    } else if (r.index == r.count - 1)
    {
        piece = side + "_right_background";
    } else
    {
        piece = side + "_middle_background";
    }

    std::vector<layer_t> layers;
    if (xmlNode *background = find_button_ops(style, piece, r.state))
    {
        if (!collect_layers(background, layers))
        {
            return nullptr;
        }
    }

    xmlNode *button = find_button_ops(style, r.function, r.state);
    if (!button || !collect_layers(button, layers) || layers.empty())
    {
        return nullptr;
    }

    /* Metacity lays a button out in theme pixels. Our button may be smaller
     * than the theme's images (they are sized for Metacity's taller
     * titlebars), so lay it out in a box that fits the largest image and
     * scale the result down to our size. */
    std::vector<std::pair<double, double>> sizes;
    double box = r.size / r.scale;
    for (const auto& layer : layers)
    {
        auto size = image_size(metacity_dir + layer.file);
        if (!size)
        {
            return nullptr;
        }

        sizes.push_back(*size);
        box = std::max({box, size->first, size->second});
    }

    std::map<std::string, double> vars;
    for (const auto& [name, value] : constants)
    {
        char *end = nullptr;
        double number = strtod(value.c_str(), &end);
        if ((end != value.c_str()) && trim(end).empty())
        {
            vars[name] = number;
        }
    }

    vars["width"]  = box;
    vars["height"] = box;

    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.size, r.size);
    cairo_t *cr = cairo_create(out);
    const double k = r.size / box;
    bool ok = true;

    for (size_t i = 0; i < layers.size() && ok; i++)
    {
        const auto& layer = layers[i];
        vars["object_width"]  = sizes[i].first;
        vars["object_height"] = sizes[i].second;
        auto x = expression_t(layer.x, vars).evaluate();
        auto y = expression_t(layer.y, vars).evaluate();
        auto w = expression_t(layer.width, vars).evaluate();
        auto h = expression_t(layer.height, vars).evaluate();
        if (!x || !y || !w || !h)
        {
            ok = false;
            break;
        }

        const int pw = std::lround(*w * k), ph = std::lround(*h * k);
        if ((pw <= 0) || (ph <= 0))
        {
            continue;
        }

        cairo_surface_t *image = render_image(metacity_dir + layer.file, pw, ph);
        if (!image)
        {
            ok = false;
            break;
        }

        if (layer.colorize)
        {
            colorize(image, *layer.colorize);
        }

        cairo_set_source_surface(cr, image, *x * k, *y * k);
        cairo_paint_with_alpha(cr, layer.alpha);
        cairo_surface_destroy(image);
    }

    cairo_destroy(cr);
    if (!ok)
    {
        cairo_surface_destroy(out);
        return nullptr;
    }

    return out;
}
}
}

#else

/* Built without libxml2 or librsvg: no theme draws its buttons this way */
namespace wf
{
namespace decor
{
metacity_theme_t::~metacity_theme_t()
{}

bool metacity_theme_t::load(const std::string&, color_lookup_t)
{
    return false;
}

cairo_surface_t*metacity_theme_t::get_button(const request_t&)
{
    return nullptr;
}
}
}
#endif
