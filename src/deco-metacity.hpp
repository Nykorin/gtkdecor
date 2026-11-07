#pragma once
#include <wayfire/config/types.hpp>
#include <cairo.h>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

typedef struct _xmlDoc xmlDoc;
typedef struct _xmlNode xmlNode;

namespace wf
{
namespace decor
{
/**
 * Window buttons drawn from a Metacity theme (metacity-1/metacity-theme-*.xml).
 *
 * Many themes build their buttons at draw time out of shared images: a glyph
 * and a circle, recoloured with colours from the GTK stylesheet (Orchis,
 * Qogir and their derivatives work this way). This implements just the part
 * of the Metacity theme format those buttons use:
 *
 *  - <window type="normal">, <frame_style_set> and <frame_style>, with their
 *    parent= inheritance, to find the button for a focus/maximized state;
 *  - <draw_ops> made only of <image> and <include> operations;
 *  - colours given as hex, <constant>s, gtk:custom(), gtk:fg/bg/base/text[],
 *    blend/ and shade/;
 *  - position and size expressions (+ - * / % and parentheses over width,
 *    height, object_width, object_height and numeric constants).
 *
 * Anything else (gradients, rectangles, lines, arcs, tiled images, ...) makes
 * the button unsupported, and the caller falls back to drawing its own.
 */
class metacity_theme_t
{
  public:
    /** Looks up a GTK stylesheet colour by its @define-color name. */
    using color_lookup_t = std::function<std::optional<wf::color_t>(const std::string&)>;

    struct request_t
    {
        /** "close", "maximize" or "minimize" */
        std::string function;
        bool focused   = true;
        bool maximized = false;
        /** "normal", "prelight" or "pressed" */
        std::string state = "normal";
        /** Which end of the titlebar the button group is at, and the
         * button's place in it, for the background piece under it */
        bool on_right = true;
        int index     = 0;
        int count     = 1;
        /** Output size in pixels, and how many pixels make one theme pixel */
        int size     = 0;
        double scale = 1.0;
    };

    metacity_theme_t() = default;
    ~metacity_theme_t();
    metacity_theme_t(const metacity_theme_t&) = delete;
    metacity_theme_t& operator =(const metacity_theme_t&) = delete;

    /**
     * Read the Metacity theme of the GTK theme at @theme_dir.
     * @return Whether the theme defines buttons this class can draw.
     */
    bool load(const std::string& theme_dir, color_lookup_t css_color);

    /**
     * Draw the button for @request.
     * @return A surface owned by the cache, or nullptr if the theme's button
     *   uses anything unsupported.
     */
    cairo_surface_t *get_button(const request_t& request);

  private:
    struct layer_t
    {
        std::string file;
        std::string x, y, width, height;
        double alpha = 1.0;
        std::optional<wf::color_t> colorize;
    };

    struct frame_style_t
    {
        std::string parent;
        /** "<function>/<state>" -> its <button> element */
        std::map<std::string, xmlNode*> buttons;
    };

    struct style_set_t
    {
        struct frame_t
        {
            std::string focus, state, resize, style;
        };

        std::string parent;
        std::vector<frame_t> frames;
    };

    xmlDoc *doc = nullptr;
    std::string metacity_dir;
    color_lookup_t css_color;
    std::string normal_style_set;

    std::map<std::string, std::string> constants;
    std::map<std::string, xmlNode*> draw_ops;
    std::map<std::string, frame_style_t> frame_styles;
    std::map<std::string, style_set_t> style_sets;

    std::map<std::string, cairo_surface_t*> cache;

    std::string find_frame_style(const std::string& set, bool focused,
        bool maximized, int depth = 0) const;
    xmlNode *find_ops(const std::string& style, const std::string& function,
        const std::string& state) const;
    xmlNode *find_button_ops(const std::string& style, const std::string& function,
        const std::string& state) const;
    bool collect_layers(xmlNode *ops, std::vector<layer_t>& out, int depth = 0) const;
    std::optional<wf::color_t> resolve_color(const std::string& spec, int depth = 0) const;
    cairo_surface_t *draw(const request_t& request);
};
}
}
