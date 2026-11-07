#include "deco-css.hpp"
#include <algorithm>
#include <cctype>

namespace wf
{
namespace decor
{
namespace
{
std::string trim(const std::string& s)
{
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }

    return s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
}

std::string strip_comments(const std::string& css)
{
    std::string out;
    out.reserve(css.size());
    for (size_t i = 0; i < css.size(); i++)
    {
        if ((css[i] == '/') && (i + 1 < css.size()) && (css[i + 1] == '*'))
        {
            const size_t end = css.find("*/", i + 2);
            if (end == std::string::npos)
            {
                break;
            }

            i = end + 1;
            out += ' ';
        } else
        {
            out += css[i];
        }
    }

    return out;
}

/** Split @s at @sep outside parentheses */
std::vector<std::string> split_top_level(const std::string& s, char sep)
{
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    for (char c : s)
    {
        depth += (c == '(') - (c == ')');
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

bool is_name_char(char c)
{
    return isalnum(static_cast<unsigned char>(c)) || (c == '-') || (c == '_');
}
}

std::optional<css_sheet_t::compound_t> css_sheet_t::parse_compound(const std::string& text,
    int& specificity)
{
    compound_t compound;
    size_t i = 0;
    auto read_name = [&] ()
    {
        size_t start = i;
        while (i < text.size() && is_name_char(text[i]))
        {
            i++;
        }

        return text.substr(start, i - start);
    };

    if ((i < text.size()) && (text[i] == '*'))
    {
        i++;
    } else if ((i < text.size()) && is_name_char(text[i]))
    {
        compound.element = read_name();
        specificity += 1;
    }

    while (i < text.size())
    {
        const char c = text[i++];
        if (c == '.')
        {
            compound.classes.push_back(read_name());
            specificity += 1000;
        } else if (c == '#')
        {
            compound.ids.push_back(read_name());
            specificity += 1000000;
        } else if (c == ':')
        {
            std::string name = read_name();
            std::string args;
            if ((i < text.size()) && (text[i] == '('))
            {
                int depth = 0;
                size_t start = ++i;
                while (i < text.size() && (text[i] != ')' || depth > 0))
                {
                    depth += (text[i] == '(') - (text[i] == ')');
                    i++;
                }

                if (i >= text.size())
                {
                    return {};
                }

                args = text.substr(start, i - start);
                i++;
            }

            if (name == "not")
            {
                int inner = 0;
                auto negated = parse_compound(trim(args), inner);
                if (!negated)
                {
                    return {};
                }

                compound.negated.push_back(*negated);
                specificity += inner;
            } else
            {
                compound.pseudo.push_back(args.empty() ? name : name + "(" + trim(args) + ")");
                specificity += 1000;
            }
        } else
        {
            /* Attribute selectors and the like: not used by GTK themes */
            return {};
        }
    }

    return compound;
}

std::optional<css_sheet_t::selector_t> css_sheet_t::parse_selector(const std::string& raw)
{
    selector_t selector;
    const std::string text = trim(raw);
    std::string current;
    char pending = 0;
    int depth = 0;

    auto flush = [&] () -> bool
    {
        if (current.empty())
        {
            return true;
        }

        auto compound = parse_compound(current, selector.specificity);
        if (!compound)
        {
            return false;
        }

        selector.combinators.push_back(selector.parts.empty() ? 0 : (pending ? pending : ' '));
        selector.parts.push_back(*compound);
        current.clear();
        pending = 0;
        return true;
    };

    for (char c : text)
    {
        depth += (c == '(') - (c == ')');
        if ((depth == 0) && (isspace(static_cast<unsigned char>(c)) || (c == '>') || (c == '+') ||
                             (c == '~')))
        {
            if (!flush())
            {
                return {};
            }

            if ((c == '+') || (c == '~'))
            {
                return {};  // sibling combinators: never needed here
            }

            if (c == '>')
            {
                pending = '>';
            }
        } else
        {
            current += c;
        }
    }

    if (!flush() || selector.parts.empty())
    {
        return {};
    }

    return selector;
}

void css_sheet_t::parse(const std::string& raw_css)
{
    const std::string css = strip_comments(raw_css);
    size_t i = 0;
    size_t order = 0;
    while (i < css.size())
    {
        while (i < css.size() && isspace(static_cast<unsigned char>(css[i])))
        {
            i++;
        }

        if (i >= css.size())
        {
            break;
        }

        if (css[i] == '@')
        {
            /* @define-color ...; or @keyframes name { ... } */
            const size_t semi  = css.find(';', i);
            const size_t brace = css.find('{', i);
            if ((brace == std::string::npos) || ((semi != std::string::npos) && (semi < brace)))
            {
                i = semi == std::string::npos ? css.size() : semi + 1;
                continue;
            }

            int depth = 0;
            for (i = brace; i < css.size(); i++)
            {
                depth += (css[i] == '{') - (css[i] == '}');
                if (depth == 0)
                {
                    break;
                }
            }

            i++;
            continue;
        }

        const size_t open = css.find('{', i);
        if (open == std::string::npos)
        {
            break;
        }

        const size_t close = css.find('}', open);
        if (close == std::string::npos)
        {
            break;
        }

        const std::string selectors = css.substr(i, open - i);
        const std::string body = css.substr(open + 1, close - open - 1);
        i = close + 1;

        std::vector<std::pair<std::string, std::string>> declarations;
        for (const auto& declaration : split_top_level(body, ';'))
        {
            const size_t colon = declaration.find(':');
            if (colon == std::string::npos)
            {
                continue;
            }

            std::string name = trim(declaration.substr(0, colon));
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            declarations.emplace_back(name, trim(declaration.substr(colon + 1)));
        }

        if (declarations.empty())
        {
            continue;
        }

        blocks.push_back(std::move(declarations));
        for (const auto& text : split_top_level(selectors, ','))
        {
            if (auto selector = parse_selector(text))
            {
                selector->order = order++;
                selector->block = blocks.size() - 1;
                rules.push_back(std::move(*selector));
            }
        }
    }
}

bool css_sheet_t::matches(const compound_t& compound, const node_t& node)
{
    if (!compound.element.empty() && (compound.element != node.element))
    {
        return false;
    }

    if (!compound.ids.empty())
    {
        return false;
    }

    for (const auto& cls : compound.classes)
    {
        if (!node.classes.count(cls))
        {
            return false;
        }
    }

    for (const auto& pseudo : compound.pseudo)
    {
        if (pseudo == "dir(ltr)")
        {
            continue;
        }

        if (!node.states.count(pseudo))
        {
            return false;
        }
    }

    for (const auto& negated : compound.negated)
    {
        if (matches(negated, node))
        {
            return false;
        }
    }

    return true;
}

/** Whether parts [0, @part] of @selector match, with @part on path[@node] */
bool css_sheet_t::matches(const selector_t& selector, const path_t& path, int part, int node) const
{
    if ((node < 0) || !matches(selector.parts[part], path[node]))
    {
        return false;
    }

    if (part == 0)
    {
        return true;
    }

    if (selector.combinators[part] == '>')
    {
        return matches(selector, path, part - 1, node - 1);
    }

    for (int ancestor = node - 1; ancestor >= 0; ancestor--)
    {
        if (matches(selector, path, part - 1, ancestor))
        {
            return true;
        }
    }

    return false;
}

std::optional<std::string> css_sheet_t::value(const path_t& path, const std::string& property) const
{
    if (path.empty())
    {
        return {};
    }

    const selector_t *best = nullptr;
    const std::string *result = nullptr;
    for (const auto& rule : rules)
    {
        if (best && ((rule.specificity < best->specificity) ||
                     ((rule.specificity == best->specificity) && (rule.order < best->order))))
        {
            continue;
        }

        const std::string *found = nullptr;
        for (const auto& [name, value] : blocks[rule.block])
        {
            if (name == property)
            {
                found = &value;  // the last declaration in a block wins
            }
        }

        if (found && matches(rule, path, rule.parts.size() - 1, path.size() - 1))
        {
            best   = &rule;
            result = found;
        }
    }

    if (!result || (*result == "inherit"))
    {
        return {};
    }

    return *result;
}

std::optional<std::string> css_sheet_t::inherited_value(path_t path, const std::string& property) const
{
    while (!path.empty())
    {
        if (auto found = value(path, property))
        {
            return found;
        }

        path.pop_back();
    }

    return {};
}
}
}
