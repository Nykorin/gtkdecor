#pragma once
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace wf
{
namespace decor
{
/**
 * Just enough of GTK's CSS cascade to ask a theme how it styles a few known
 * widgets: what value a property has for a node, given the node's ancestry.
 *
 * Selectors are matched on element names, classes, :backdrop, :not() and
 * :dir(), with descendant and child combinators; the winner is decided by
 * specificity, then source order, as in GTK. Any other pseudo-class (:hover,
 * :first-child, ...) does not match, since the nodes asked about are in their
 * plain state.
 */
class css_sheet_t
{
  public:
    /** One CSS node, e.g. headerbar.titlebar.default-decoration:backdrop */
    struct node_t
    {
        std::string element;
        std::set<std::string> classes;
        std::set<std::string> states;
    };

    /** A node and its ancestors, outermost first */
    using path_t = std::vector<node_t>;

    void parse(const std::string& css);
    bool empty() const
    {
        return rules.empty();
    }

    /** The cascaded value of @property for the last node of @path, unresolved */
    std::optional<std::string> value(const path_t& path, const std::string& property) const;

    /** As value(), but inherited from the ancestors if the node sets none
     * (for inherited properties such as color) */
    std::optional<std::string> inherited_value(path_t path, const std::string& property) const;

  private:
    struct compound_t
    {
        std::string element;
        std::vector<std::string> ids;
        std::vector<std::string> classes;
        /** Pseudo-classes, with :not() bodies compiled separately */
        std::vector<std::string> pseudo;
        std::vector<compound_t> negated;
    };

    struct selector_t
    {
        /** Compounds left to right, and the combinator before each (' ' or '>') */
        std::vector<compound_t> parts;
        std::vector<char> combinators;
        int specificity = 0;
        size_t order    = 0;
        size_t block    = 0;
    };

    std::vector<std::vector<std::pair<std::string, std::string>>> blocks;
    std::vector<selector_t> rules;

    static std::optional<compound_t> parse_compound(const std::string& text, int& specificity);
    static std::optional<selector_t> parse_selector(const std::string& text);
    static bool matches(const compound_t& compound, const node_t& node);
    bool matches(const selector_t& selector, const path_t& path, int part, int node) const;
};
}
}
