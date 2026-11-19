#include "Domain/ColorDefinition.h"
#include "Domain/ScmpConstants.h"

scmp::ColorDefinition::ColorDefinition(ColorIndex color_index, ColorValue red,
                                      ColorValue green, ColorValue blue)
    : m_color_index{color_index}, m_red{red}, m_green{green}, m_blue{blue}
{
}

scmp::ColorDefinition::ColorDefinition(const ColorDefinition &&c) noexcept
    : m_color_index{c.get_color_index()},
      m_red{c.get_red()},
      m_green{c.get_green()},
      m_blue{c.get_blue()}
{
}

scmp::ColorDefinition::ColorDefinition(const ColorDefinition &c) noexcept
    : m_color_index{c.get_color_index()},
      m_red{c.get_red()},
      m_green{c.get_green()},
      m_blue{c.get_blue()}
{
}

scmp::ColorDefinition::~ColorDefinition()
{
}
