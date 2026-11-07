#pragma once
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-render.hpp>
#include "deco-button.hpp"
#include "deco-metacity.hpp"
#include "deco-css.hpp"
#include <string>
#include <memory>
#include <map>
#include <optional>
#include <vector>

namespace wf
{
namespace decor
{
/**
 * A  class which manages the outlook of decorations.
 * It is responsible for determining the background colors, sizes, etc.
 */
class decoration_theme_t
{
  public:
    /** Create a new theme with the default parameters */
    decoration_theme_t();
    ~decoration_theme_t();

    /** @return The available height for displaying the title (title_height,
     *   or with title_height = -1 the GTK theme's header bar height, less the
     *   border) */
    int get_title_height() const;
    /** @return The size of a window button, in logical pixels */
    int get_button_size() const;
    /** @return The gap between two window buttons */
    int get_button_spacing() const;
    /** @return The gap between the window's edge and the outermost button */
    int get_button_edge() const;
    /** @return The available border for resizing */
    int get_border_size() const;
    /** Set the flags for buttons */
    void set_buttons(button_type_t flags);
    button_type_t button_flags;

    /**
     * Fill the given rectangle with the background color(s).
     *
     * @param data The render data (pass, target, damage)
     * @param rectangle The rectangle to redraw.
     * @param active Whether to use active or inactive colors
     * @param tiled Whether the window is tiled, maximized or fullscreen: square
     *   corners and no shadow, as GTK draws such windows
     */
    void render_background(const wf::scene::render_instruction_t& data,
        wf::geometry_t rectangle, bool active, bool tiled = false) const;

    /** How far the theme's window shadow reaches past each edge */
    struct shadow_extents_t
    {
        int left = 0, right = 0, top = 0, bottom = 0;
    };

    /**
     * @return The reach of the GTK theme's window shadow (the larger of its
     *   focused and unfocused one), or all zero if it has none or shadows are
     *   turned off.
     */
    shadow_extents_t get_shadow_extents() const;

    /**
     * Get corner radius for rounded titlebar
     */
    int get_corner_radius() const;

    /**
     * Render the given text on a cairo_surface_t with the given size.
     * The caller is responsible for freeing the memory afterwards.
     * @param button_area_width Width occupied by buttons at the titlebar edge (mirrored on the other side for centering)
     */
    cairo_surface_t *render_text(const std::string& text, int width, int height,
        int button_area_width = 0, bool active = true) const;

    struct button_state_t
    {
        /** Button width */
        double width;
        /** Button height */
        double height;
        /** Button outline size */
        double border;
        /** Progress of button hover, in range [-1, 1].
         * Negative numbers are usually used for pressed state. */
        double hover_progress;
        /** Whether the window is active/focused */
        bool activated = true;
        /** Whether the window is maximized (for the unmaximize glyph) */
        bool maximized = false;
        /** Which end of the titlebar the buttons are at, and this button's
         * place among them */
        bool on_right = true;
        int index     = 0;
        int count     = 1;
        /** Pixels per logical pixel the surface is rendered at */
        double scale = 1.0;
    };

    /**
     * Get the icon for the given button.
     * The caller is responsible for freeing the memory afterwards.
     *
     * @param button The button type.
     * @param state The button state.
     */
    cairo_surface_t *get_button_surface(button_type_t button,
        const button_state_t& state) const;

    // Force reload of theme (called when theme changes)
    void reload_theme() const;

  private:
    wf::option_wrapper_t<std::string> font{"gtkdecor/font"};
    wf::option_wrapper_t<wf::color_t> font_color{"gtkdecor/font_color"};
    wf::option_wrapper_t<int> title_height{"gtkdecor/title_height"};
    wf::option_wrapper_t<int> border_size{"gtkdecor/border_size"};
    wf::option_wrapper_t<wf::color_t> active_color{"gtkdecor/active_color"};
    wf::option_wrapper_t<wf::color_t> inactive_color{"gtkdecor/inactive_color"};
    /** auto: use the GTK theme's metacity-1 titlebuttons if it ships them,
     * otherwise draw them; gtk: always draw; pixmap: only use theme assets */
    wf::option_wrapper_t<std::string> button_style{"gtkdecor/button_style"};
    /** Draw the GTK theme's window shadow (its "decoration" box-shadow) */
    wf::option_wrapper_t<bool> theme_shadow{"gtkdecor/shadow"};

    // Rounded corner radii: the theme's "decoration" border-radius, else these
    mutable int corner_radius = 12;        // Top corners
    mutable int bottom_corner_radius = 8;  // Bottom corners

    /* Sizes from the theme's header bar and title buttons (0/-1: none given) */
    mutable int theme_title_total    = 0;
    mutable int theme_button_size    = 0;
    mutable int theme_button_spacing = -1;
    mutable int theme_button_edge    = -1;

    // GTK theme parsing (mutable for lazy initialization)
    mutable bool theme_loaded;
    mutable wf::color_t theme_titlebar_bg_active;
    mutable wf::color_t theme_titlebar_bg_inactive;
    mutable wf::color_t theme_titlebar_fg_active;
    mutable wf::color_t theme_titlebar_fg_inactive;
    mutable wf::color_t theme_button_bg;
    mutable wf::color_t theme_button_hover_bg;
    mutable wf::color_t theme_button_active_bg;
    mutable std::string theme_font_family;
    mutable int theme_font_size;
    mutable std::string icon_theme_name;
    mutable std::string gtk_theme_name;

    void load_gtk_theme() const;
    std::string get_gtk_theme_name() const;
    std::string get_icon_theme_name() const;
    std::string get_gtk_font_name() const;
    std::string find_theme_css_file(const std::string& theme_name) const;
    std::string find_icon_file(const std::string& icon_name, int size) const;

    /**
     * Locate a metacity-1 titlebutton asset for the given button and state,
     * e.g. <theme>/metacity-1/titlebuttons/titlebutton-close-hover.svg
     * @return Path to the asset, or "" if the theme ships none.
     */
    std::string find_titlebutton_file(button_type_t button,
        const button_state_t& state) const;

    /**
     * Render a titlebutton asset at @size, cached by path and size so the
     * SVG is not re-parsed on every frame of the hover animation.
     * @return Borrowed surface owned by the cache, or nullptr on failure.
     */
    cairo_surface_t *get_titlebutton_asset(const std::string& path, int size) const;

    /** Rendered titlebutton assets, keyed by "<path>@<size>" */
    mutable std::map<std::string, cairo_surface_t*> titlebutton_cache;
    void clear_titlebutton_cache() const;
    void parse_theme_css(const std::string& css_file) const;

    /** The theme's @define-color variables, unresolved */
    mutable std::map<std::string, std::string> css_colors;
    /** Resolve a @define-color name, following @references and alpha()/shade()/mix() */
    std::optional<wf::color_t> lookup_css_color(const std::string& name, int depth = 0) const;
    std::optional<wf::color_t> resolve_css_value(const std::string& value, int depth = 0) const;

    /** Buttons drawn from the theme's Metacity XML; null if it has none we can draw */
    mutable std::unique_ptr<metacity_theme_t> metacity;

    /** One layer of a CSS box-shadow */
    struct css_shadow_t
    {
        double dx = 0, dy = 0, blur = 0, spread = 0;
        wf::color_t color{0, 0, 0, 0};
    };

    /** The "decoration" and "decoration:backdrop" box-shadows, outer layers only */
    mutable std::vector<css_shadow_t> shadow_active, shadow_inactive;
    std::vector<css_shadow_t> parse_box_shadow(const std::string& value) const;
    shadow_extents_t extents_of(const std::vector<css_shadow_t>& layers) const;
    /** Render @layers around a w x h frame; the frame sits at (pad, pad) */
    cairo_surface_t *render_shadow(const std::vector<css_shadow_t>& layers,
        int width, int height, int pad) const;

    /** A window button's colours, as declared in the theme's stylesheet */
    struct titlebutton_colors_t
    {
        wf::color_t bg;
        wf::color_t pressed_bg;
        wf::color_t backdrop_bg;
        wf::color_t fg;
        wf::color_t hover_fg;
        wf::color_t backdrop_hover_fg;
    };

    /** Filled only for themes that colour each button differently */
    mutable std::map<button_type_t, titlebutton_colors_t> titlebutton_colors;
    void parse_titlebutton_colors(std::string css) const;
    wf::color_t parse_css_color(const std::string& color_str) const;

    // Background surface cache — avoids recreating Cairo surfaces every frame
    mutable struct bg_cache_t
    {
        wf::geometry_t geometry{};
        bool active = false;
        bool shadow = false;
        bool tiled  = false;
        bool valid = false;

        std::unique_ptr<wf::owned_texture_t> shadow_tex;
        wf::geometry_t shadow_rect{};

        std::unique_ptr<wf::owned_texture_t> titlebar_tex;
        wf::geometry_t titlebar_rect{};

        std::unique_ptr<wf::owned_texture_t> left_tex;
        wf::geometry_t left_rect{};

        std::unique_ptr<wf::owned_texture_t> right_tex;
        wf::geometry_t right_rect{};

        std::unique_ptr<wf::owned_texture_t> bottom_tex;
        wf::geometry_t bottom_rect{};

        std::unique_ptr<wf::owned_texture_t> outline_tex;
        wf::geometry_t outline_rect{};
    } bg_cache;
    void invalidate_cache() const;
};
}
}
