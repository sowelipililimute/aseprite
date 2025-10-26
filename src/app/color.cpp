// Aseprite
// Copyright (C) 2020-2023  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include <algorithm>

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/color.h"

#include "app/color_utils.h"
#include "app/modules/palettes.h"
#include "base/debug.h"
#include "doc/image.h"
#include "doc/palette.h"
#include "doc/primitives.h"
#include "doc/tile.h"
#include "gfx/hsl.h"
#include "gfx/hsv.h"
#include "gfx/rgb.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

namespace app {

using namespace doc;
using namespace gfx;

struct LAB {
  double L = 0;
  double a = 0;
  double b = 0;
};

struct LinearRGB {
  double r, g, b;

  static double f(double x)
  {
    return x;
    if (x >= 0.0031308)
      return (1.055) * pow(x, (1.0/2.4)) - 0.055;
    else
      return 12.92 * x;
  }
  static double f_inv(double x)
  {
    return x;
    if (x >= 0.04045)
      return pow(((x + 0.055)/(1 + 0.055)), 2.4);
    else 
      return x / 12.92;
  }
  static LinearRGB from(Rgb r)
  {
    return {f(r.red()/255.0), f(r.green()/255.0), f(r.blue()/255.0)};
  }
  Rgb into()
  {
    return Rgb(f_inv(r)*255, f_inv(g)*255, f_inv(b)*255);
  }
};

inline double cubeRootOf(double num)
{
  return pow(num, 1.0 / 3.0);
}

inline double cubed(double num)
{
  return num * num * num;
}

LAB linearSRGBToOKLab(const Rgb &c)
{
  const auto c_ = LinearRGB::from(c);
  double r = c_.r, g = c_.g, b = c_.b;
  // convert from srgb to linear lms

  const auto l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
  const auto m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
  const auto s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;

  // convert from linear lms to non-linear lms

  const auto l_ = cubeRootOf(l);
  const auto m_ = cubeRootOf(m);
  const auto s_ = cubeRootOf(s);

  // convert from non-linear lms to lab

  return LAB{.L = 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
             .a = 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
             .b = 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_};
}

Rgb OKLabToLinearSRGB(LAB lab)
{
    // convert from lab to non-linear lms

    const auto l_ = lab.L + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
    const auto m_ = lab.L - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
    const auto s_ = lab.L - 0.0894841775 * lab.a - 1.2914855480 * lab.b;

    // convert from non-linear lms to linear lms

    const auto l = cubed(l_);
    const auto m = cubed(m_);
    const auto s = cubed(s_);

    // convert from linear lms to linear srgb

    const auto r = +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    const auto g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    const auto b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;

    return LinearRGB{std::clamp(r, 0.0, 1.0), std::clamp(g, 0.0, 1.0), std::clamp(b, 0.0, 1.0)}.into();
}

// static
Color Color::fromOklab(double l, double a, double b, int alpha)
{
  ASSERT(0.0 <= l); ASSERT(l <= 1.0);
  ASSERT(-0.4 <= a); ASSERT(a <= 0.4);
  ASSERT(-0.4 <= b); ASSERT(b <= 0.4);

  Color color(Color::OklabType);
  color.m_value.oklab.l = l;
  color.m_value.oklab.a = a;
  color.m_value.oklab.b = b;
  color.m_value.oklab.alpha = alpha;
  return color;
}

// static
Color Color::fromMask()
{
  return Color(Color::MaskType);
}

// static
Color Color::fromRgb(int r, int g, int b, int a)
{
  Color color(Color::RgbType);
  color.m_value.rgb.r = r;
  color.m_value.rgb.g = g;
  color.m_value.rgb.b = b;
  color.m_value.rgb.a = a;
  return color;
}

// static
Color Color::fromHsv(double h, double s, double v, int a)
{
  Color color(Color::HsvType);
  color.m_value.hsv.h = h;
  color.m_value.hsv.s = s;
  color.m_value.hsv.v = v;
  color.m_value.hsv.a = a;
  return color;
}

// static
Color Color::fromHsl(double h, double s, double l, int a)
{
  Color color(Color::HslType);
  color.m_value.hsl.h = h;
  color.m_value.hsl.s = s;
  color.m_value.hsl.l = l;
  color.m_value.hsl.a = a;
  return color;
}

// static
Color Color::fromGray(int g, int a)
{
  Color color(Color::GrayType);
  color.m_value.gray.g = g;
  color.m_value.gray.a = a;
  return color;
}

// static
Color Color::fromIndex(int index)
{
  ASSERT(index >= 0);

  Color color(Color::IndexType);
  color.m_value.index = index;
  return color;
}

// static
Color Color::fromTile(doc::tile_t tile)
{
  Color color(Color::TileType);
  color.m_value.tile = tile;
  return color;
}

// static
Color Color::fromImage(PixelFormat pixelFormat, color_t c)
{
  Color color = app::Color::fromMask();

  switch (pixelFormat) {
    case IMAGE_RGB:
      if (rgba_geta(c) > 0) {
        color = Color::fromRgb(rgba_getr(c), rgba_getg(c), rgba_getb(c), rgba_geta(c));
      }
      break;

    case IMAGE_GRAYSCALE:
      if (graya_geta(c) > 0) {
        color = Color::fromGray(graya_getv(c), graya_geta(c));
      }
      break;

    case IMAGE_INDEXED: color = Color::fromIndex(c); break;

    case IMAGE_TILEMAP: color = Color::fromTile(c); break;
  }

  return color;
}

// static
Color Color::fromImageGetPixel(Image* image, int x, int y)
{
  if ((x >= 0) && (y >= 0) && (x < image->width()) && (y < image->height()))
    return Color::fromImage(image->pixelFormat(), doc::get_pixel(image, x, y));
  else
    return Color::fromMask();
}

// static
Color Color::fromString(const std::string& str)
{
  Color color = app::Color::fromMask();

  if (str != "mask") {
    if (str.find("rgb{") == 0 || str.find("hsv{") == 0 || str.find("hsl{") == 0 ||
        str.find("gray{") == 0) {
      int c = 0;
      double table[4] = { 0.0, 0.0, 0.0, 255.0 };
      std::string::size_type i = str.find_first_of('{') + 1, j;

      while ((j = str.find_first_of(",}", i)) != std::string::npos) {
        std::string element = str.substr(i, j - i);
        if (c < 4)
          table[c++] = std::strtod(element.c_str(), NULL);
        if (c >= 4)
          break;
        i = j + 1;
      }

      if (str[0] == 'r')
        color = Color::fromRgb(int(table[0]), int(table[1]), int(table[2]), int(table[3]));
      else if (str[0] == 'h' && str[1] == 's' && str[2] == 'v')
        color = Color::fromHsv(table[0], table[1] / 100.0, table[2] / 100.0, int(table[3]));
      else if (str[0] == 'h' && str[1] == 's' && str[2] == 'l')
        color = Color::fromHsl(table[0], table[1] / 100.0, table[2] / 100.0, int(table[3]));
      else if (str[0] == 'g')
        color = Color::fromGray(int(table[0]), (c >= 2 ? int(table[1]) : 255));
    }
    else if (str.find("index{") == 0) {
      color = Color::fromIndex(std::strtol(str.c_str() + 6, NULL, 10));
    }
  }

  return color;
}

Color Color::toRgb() const
{
  return Color::fromRgb(getRed(), getGreen(), getBlue(), getAlpha());
}

std::string Color::toString() const
{
  std::stringstream result;

  switch (getType()) {
    case Color::MaskType: result << "mask"; break;

    case Color::RgbType:
      result << "rgb{" << m_value.rgb.r << "," << m_value.rgb.g << "," << m_value.rgb.b << ","
             << m_value.rgb.a << "}";
      break;

    case Color::HsvType:
      result << "hsv{" << std::setprecision(2) << std::fixed << m_value.hsv.h << ","
             << std::clamp(m_value.hsv.s * 100.0, 0.0, 100.0) << ","
             << std::clamp(m_value.hsv.v * 100.0, 0.0, 100.0) << "," << m_value.hsv.a << "}";
      break;

    case Color::HslType:
      result << "hsl{" << std::setprecision(2) << std::fixed << m_value.hsl.h << ","
             << std::clamp(m_value.hsl.s * 100.0, 0.0, 100.0) << ","
             << std::clamp(m_value.hsl.l * 100.0, 0.0, 100.0) << "," << m_value.hsl.a << "}";
      break;

    case Color::GrayType:
      result << "gray{" << m_value.gray.g << "," << m_value.gray.a << "}";
      break;

    case Color::IndexType: result << "index{" << m_value.index << "}"; break;
  }

  return result.str();
}

std::string Color::toHumanReadableString(PixelFormat pixelFormat,
                                         HumanReadableString humanReadable) const
{
  std::stringstream result;

  if (humanReadable == LongHumanReadableString) {
    switch (getType()) {
      case Color::MaskType: result << "Mask"; break;

      case Color::RgbType:
        if (pixelFormat == IMAGE_GRAYSCALE) {
          result << "Gray " << getGray();
        }
        else {
          result << "RGB " << m_value.rgb.r << " " << m_value.rgb.g << " " << m_value.rgb.b;

          if (pixelFormat == IMAGE_INDEXED)
            result << " Index " << color_utils::color_for_image(*this, IMAGE_INDEXED);
        }
        break;

      case Color::OklabType:
        if (pixelFormat == IMAGE_GRAYSCALE) {
          result << "Gray " << getGray();
        }
        else {
          result << "Oklab "
                 << m_value.oklab.l << " "
                 << m_value.oklab.a << " "
                 << m_value.oklab.b;

          if (pixelFormat == IMAGE_INDEXED)
            result << " Index "
                   << color_utils::color_for_image(*this, IMAGE_INDEXED);
        }
        break;

      case Color::HsvType:
        if (pixelFormat == IMAGE_GRAYSCALE) {
          result << "Gray " << getGray();
        }
        else {
          result << "HSV " << int(m_value.hsv.h) << "\xc2\xb0 "
                 << std::clamp(int(m_value.hsv.s * 100.0), 0, 100) << "% "
                 << std::clamp(int(m_value.hsv.v * 100.0), 0, 100) << "%";

          if (pixelFormat == IMAGE_INDEXED)
            result << " Index " << color_utils::color_for_image(*this, IMAGE_INDEXED);

          result << " (RGB " << getRed() << " " << getGreen() << " " << getBlue() << ")";
        }
        break;

      case Color::HslType:
        if (pixelFormat == IMAGE_GRAYSCALE) {
          result << "Gray " << getGray();
        }
        else {
          result << "HSL " << int(m_value.hsl.h) << "\xc2\xb0 "
                 << std::clamp(int(m_value.hsl.s * 100.0), 0, 100) << "% "
                 << std::clamp(int(m_value.hsl.l * 100.0), 0, 100) << "%";

          if (pixelFormat == IMAGE_INDEXED)
            result << " Index " << color_utils::color_for_image(*this, IMAGE_INDEXED);

          result << " (RGB " << getRed() << " " << getGreen() << " " << getBlue() << ")";
        }
        break;

      case Color::GrayType:  result << "Gray " << m_value.gray.g; break;

      case Color::IndexType: {
        int i = m_value.index;
        if (i >= 0 && i < (int)get_current_palette()->size()) {
          uint32_t _c = get_current_palette()->getEntry(i);
          result << "Index " << i << " (RGB " << (int)rgba_getr(_c) << " " << (int)rgba_getg(_c)
                 << " " << (int)rgba_getb(_c) << ")";
        }
        else {
          result << "Index " << i << " (out of range)";
        }
        break;
      }

      default: ASSERT(false); break;
    }

    result << " #" << std::hex << std::setfill('0') << std::setw(2) << getRed() << std::setw(2)
           << getGreen() << std::setw(2) << getBlue();
  }
  else if (humanReadable == ShortHumanReadableString) {
    switch (getType()) {
      case Color::MaskType: result << "Mask"; break;

      case Color::RgbType:
        if (pixelFormat == IMAGE_GRAYSCALE) {
          result << "Gry-" << getGray();
        }
        else {
          result << "#" << std::hex << std::setfill('0') << std::setw(2) << m_value.rgb.r
                 << std::setw(2) << m_value.rgb.g << std::setw(2) << m_value.rgb.b;
        }
        break;

      case Color::HsvType:
        if (pixelFormat == IMAGE_GRAYSCALE) {
          result << "Gry-" << getGray();
        }
        else {
          result << int(m_value.hsv.h) << "\xc2\xb0"
                 << std::clamp(int(m_value.hsv.s * 100.0), 0, 100) << ","
                 << std::clamp(int(m_value.hsv.v * 100.0), 0, 100);
        }
        break;

      case Color::HslType:
        if (pixelFormat == IMAGE_GRAYSCALE) {
          result << "Gry-" << getGray();
        }
        else {
          result << int(m_value.hsl.h) << "\xc2\xb0"
                 << std::clamp(int(m_value.hsl.s * 100.0), 0, 100) << ","
                 << std::clamp(int(m_value.hsl.l * 100.0), 0, 100);
        }
        break;

      case Color::GrayType:  result << "Gry-" << m_value.gray.g; break;

      case Color::IndexType: result << "Idx-" << m_value.index; break;

      case Color::OklabType:
        result << "Oklab-"<< std::hex << std::setfill('0')
                 << std::setw(2) << m_value.oklab.l
                 << std::setw(2) << m_value.oklab.a
                 << std::setw(2) << m_value.oklab.b;
        break;

      default:
        ASSERT(false);
        break;
    }
  }

  return result.str();
}

bool Color::operator==(const Color& other) const
{
  if (getType() != other.getType())
    return false;

  switch (getType()) {
    case Color::MaskType: return true;

    case Color::RgbType:
      return m_value.rgb.r == other.m_value.rgb.r && m_value.rgb.g == other.m_value.rgb.g &&
             m_value.rgb.b == other.m_value.rgb.b && m_value.rgb.a == other.m_value.rgb.a;

    case Color::HsvType:
      return (std::fabs(m_value.hsv.h - other.m_value.hsv.h) < 0.001) &&
             (std::fabs(m_value.hsv.s - other.m_value.hsv.s) < 0.00001) &&
             (std::fabs(m_value.hsv.v - other.m_value.hsv.v) < 0.00001) &&
             (m_value.hsv.a == other.m_value.hsv.a);

    case Color::HslType:
      return (std::fabs(m_value.hsl.h - other.m_value.hsl.h) < 0.001) &&
             (std::fabs(m_value.hsl.s - other.m_value.hsl.s) < 0.00001) &&
             (std::fabs(m_value.hsl.l - other.m_value.hsl.l) < 0.00001) &&
             (m_value.hsl.a == other.m_value.hsl.a);

    case Color::OklabType:
      return
        (std::fabs(m_value.oklab.l - other.m_value.oklab.l) < 0.001) &&
        (std::fabs(m_value.oklab.a - other.m_value.oklab.a) < 0.00001) &&
        (std::fabs(m_value.oklab.b - other.m_value.oklab.b) < 0.00001) &&
        (m_value.hsl.a == other.m_value.hsl.a);

    case Color::GrayType:
      return m_value.gray.g == other.m_value.gray.g && m_value.gray.a == other.m_value.gray.a;

    case Color::IndexType: return m_value.index == other.m_value.index;

    default:               ASSERT(false); return false;
  }
}

// Returns false only if the color is a index and it is outside the
// valid range (outside the maximum number of colors in the current
// palette)
bool Color::isValid() const
{
  switch (getType()) {
    case Color::IndexType: {
      int i = m_value.index;
      return (i >= 0 && i < get_current_palette()->size());
    }
  }
  return true;
}

int Color::getRed() const
{
  switch (getType()) {
    case Color::MaskType:  return 0;

    case Color::RgbType:   return m_value.rgb.r;

    case Color::HsvType:   return Rgb(Hsv(m_value.hsv.h, m_value.hsv.s, m_value.hsv.v)).red();

    case Color::HslType:   return Rgb(Hsl(m_value.hsl.h, m_value.hsl.s, m_value.hsl.l)).red();

    case Color::GrayType:  return m_value.gray.g;

    case Color::OklabType:
      return OKLabToLinearSRGB(LAB{m_value.oklab.l, m_value.oklab.a, m_value.oklab.b}).red();

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size())
        return rgba_getr(get_current_palette()->getEntry(i));
      else
        return 0;
    }
  }

  ASSERT(false);
  return -1;
}

int Color::getGreen() const
{
  switch (getType()) {
    case Color::MaskType:  return 0;

    case Color::RgbType:   return m_value.rgb.g;

    case Color::HsvType:   return Rgb(Hsv(m_value.hsv.h, m_value.hsv.s, m_value.hsv.v)).green();

    case Color::HslType:   return Rgb(Hsl(m_value.hsl.h, m_value.hsl.s, m_value.hsl.l)).green();

    case Color::GrayType:  return m_value.gray.g;

    case Color::OklabType:
      return OKLabToLinearSRGB(LAB{m_value.oklab.l, m_value.oklab.a, m_value.oklab.b}).green();

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size())
        return rgba_getg(get_current_palette()->getEntry(i));
      else
        return 0;
    }
  }

  ASSERT(false);
  return -1;
}

int Color::getBlue() const
{
  switch (getType()) {
    case Color::MaskType:  return 0;

    case Color::RgbType:   return m_value.rgb.b;

    case Color::HsvType:   return Rgb(Hsv(m_value.hsv.h, m_value.hsv.s, m_value.hsv.v)).blue();

    case Color::HslType:   return Rgb(Hsl(m_value.hsl.h, m_value.hsl.s, m_value.hsl.l)).blue();

    case Color::GrayType:  return m_value.gray.g;

    case Color::OklabType:
      return OKLabToLinearSRGB(LAB{m_value.oklab.l, m_value.oklab.a, m_value.oklab.b}).blue();

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size())
        return rgba_getb(get_current_palette()->getEntry(i));
      else
        return 0;
    }
  }

  ASSERT(false);
  return -1;
}

double Color::getHsvHue() const
{
  switch (getType()) {
    case Color::MaskType:  return 0.0;

    case Color::RgbType:   return Hsv(Rgb(m_value.rgb.r, m_value.rgb.g, m_value.rgb.b)).hue();

    case Color::HsvType:   return m_value.hsv.h;

    case Color::HslType:   return m_value.hsl.h;

    case Color::GrayType:  return 0.0;

    case Color::OklabType:
      return Hsv(Rgb(getRed(),
                     getGreen(),
                     getBlue())).hue();

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size()) {
        uint32_t c = get_current_palette()->getEntry(i);
        return Hsv(Rgb(rgba_getr(c), rgba_getg(c), rgba_getb(c))).hue();
      }
      else
        return 0.0;
    }
  }

  ASSERT(false);
  return -1.0;
}

double Color::getHsvSaturation() const
{
  switch (getType()) {
    case Color::MaskType:  return 0;

    case Color::RgbType:   return Hsv(Rgb(m_value.rgb.r, m_value.rgb.g, m_value.rgb.b)).saturation();

    case Color::HsvType:   return m_value.hsv.s;

    case Color::HslType:   return Hsv(Rgb(getRed(), getGreen(), getBlue())).saturation();

    case Color::GrayType:  return 0;

    case Color::OklabType:
      return Hsv(Rgb(getRed(),
                     getGreen(),
                     getBlue())).saturation();

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size()) {
        uint32_t c = get_current_palette()->getEntry(i);
        return Hsv(Rgb(rgba_getr(c), rgba_getg(c), rgba_getb(c))).saturation();
      }
      else
        return 0.0;
    }
  }

  ASSERT(false);
  return -1.0;
}

double Color::getHsvValue() const
{
  switch (getType()) {
    case Color::MaskType:  return 0.0;

    case Color::RgbType:   return Hsv(Rgb(m_value.rgb.r, m_value.rgb.g, m_value.rgb.b)).value();

    case Color::HsvType:   return m_value.hsv.v;

    case Color::HslType:   return Hsv(Rgb(getRed(), getGreen(), getBlue())).value();

    case Color::GrayType:  return m_value.gray.g / 255.0;

    case Color::OklabType:
      return Hsv(Rgb(getRed(),
                     getGreen(),
                     getBlue())).value();

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size()) {
        uint32_t c = get_current_palette()->getEntry(i);
        return Hsv(Rgb(rgba_getr(c), rgba_getg(c), rgba_getb(c))).value();
      }
      else
        return 0.0;
    }
  }

  ASSERT(false);
  return -1.0;
}

double Color::getHslHue() const
{
  switch (getType()) {
    case Color::MaskType:  return 0.0;

    case Color::RgbType:   return Hsl(Rgb(m_value.rgb.r, m_value.rgb.g, m_value.rgb.b)).hue();

    case Color::HsvType:   return m_value.hsv.h;

    case Color::HslType:   return m_value.hsl.h;

    case Color::GrayType:  return 0.0;

    case Color::OklabType:
      return Hsl(Rgb(getRed(),
                     getGreen(),
                     getBlue())).hue();

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size()) {
        uint32_t c = get_current_palette()->getEntry(i);
        return Hsl(Rgb(rgba_getr(c), rgba_getg(c), rgba_getb(c))).hue();
      }
      else
        return 0.0;
    }
  }

  ASSERT(false);
  return -1.0;
}

double Color::getHslSaturation() const
{
  switch (getType()) {
    case Color::MaskType:  return 0;

    case Color::RgbType:   return Hsl(Rgb(m_value.rgb.r, m_value.rgb.g, m_value.rgb.b)).saturation();

    case Color::HsvType:   return Hsl(Rgb(getRed(), getGreen(), getBlue())).saturation();

    case Color::HslType:   return m_value.hsl.s;

    case Color::GrayType:  return 0;

    case Color::OklabType:
      return Hsl(Rgb(getRed(),
                     getGreen(),
                     getBlue())).saturation();

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size()) {
        uint32_t c = get_current_palette()->getEntry(i);
        return Hsl(Rgb(rgba_getr(c), rgba_getg(c), rgba_getb(c))).saturation();
      }
      else
        return 0.0;
    }
  }

  ASSERT(false);
  return -1.0;
}

double Color::getHslLightness() const
{
  switch (getType()) {
    case Color::MaskType:  return 0.0;

    case Color::RgbType:   return Hsl(Rgb(m_value.rgb.r, m_value.rgb.g, m_value.rgb.b)).lightness();

    case Color::HsvType:   return Hsl(Rgb(getRed(), getGreen(), getBlue())).lightness();

    case Color::HslType:   return m_value.hsl.l;

    case Color::GrayType:  return m_value.gray.g / 255.0;

    case Color::OklabType:
      return Hsl(Rgb(getRed(),
                     getGreen(),
                     getBlue())).lightness();

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size()) {
        uint32_t c = get_current_palette()->getEntry(i);
        return Hsl(Rgb(rgba_getr(c), rgba_getg(c), rgba_getb(c))).lightness();
      }
      else
        return 0.0;
    }
  }

  ASSERT(false);
  return -1.0;
}

int Color::getGray() const
{
  switch (getType()) {
    case Color::MaskType: return 0;

    case Color::RgbType:
      return int(255.0 * Hsl(Rgb(m_value.rgb.r, m_value.rgb.g, m_value.rgb.b)).lightness());

    case Color::OklabType:
    case Color::HsvType:   return int(255.0 * Hsl(Rgb(getRed(), getGreen(), getBlue())).lightness());

    case Color::HslType:   return int(255.0 * m_value.hsl.l);

    case Color::GrayType:  return m_value.gray.g;

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size()) {
        uint32_t c = get_current_palette()->getEntry(i);
        return int(255.0 * Hsl(Rgb(rgba_getr(c), rgba_getg(c), rgba_getb(c))).lightness());
      }
      else
        return 0;
    }
  }

  ASSERT(false);
  return -1;
}

double Color::getOklabLightness() const
{
  switch (getType()) {
    case Color::MaskType:
      return 0;

    case Color::RgbType:
    case Color::HsvType:
    case Color::HslType:
    case Color::GrayType:
      return linearSRGBToOKLab(Rgb(getRed(), getGreen(), getBlue())).L;

    case Color::OklabType:
      return m_value.oklab.l;

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size()) {
        uint32_t c = get_current_palette()->getEntry(i);
        return linearSRGBToOKLab(Rgb(rgba_getr(c),
         rgba_getg(c),
         rgba_getb(c))).L;
      }
      else
        return 0;
    }
  }

  ASSERT(false);
  return -1;
}

double Color::getOklabA() const
{
  switch (getType()) {
    case Color::MaskType:
      return 0;

    case Color::RgbType:
    case Color::HsvType:
    case Color::HslType:
    case Color::GrayType: { 
      const auto color = linearSRGBToOKLab(Rgb(getRed(), getGreen(), getBlue()));
      return color.a;
    }

    case Color::OklabType:
      return m_value.oklab.a;

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size()) {
        uint32_t c = get_current_palette()->getEntry(i);
        return linearSRGBToOKLab(Rgb(rgba_getr(c),
         rgba_getg(c),
         rgba_getb(c))).a;
      }
      else
        return 0;
    }
  }

  ASSERT(false);
  return -1;
}

double Color::getOklabB() const
{
  switch (getType()) {
    case Color::MaskType:
      return 0;

    case Color::RgbType:
    case Color::HsvType:
    case Color::HslType:
    case Color::GrayType:
      return linearSRGBToOKLab(Rgb(getRed(), getGreen(), getBlue())).b;

    case Color::OklabType:
      return m_value.oklab.b;

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size()) {
        uint32_t c = get_current_palette()->getEntry(i);
        return linearSRGBToOKLab(Rgb(rgba_getr(c),
         rgba_getg(c),
         rgba_getb(c))).b;
      }
      else
        return 0;
    }
  }

  ASSERT(false);
  return -1;
}

int Color::getIndex() const
{
  switch (getType()) {
    case Color::MaskType: return 0;

    case Color::RgbType:
    case Color::HsvType:
    case Color::HslType:
    case Color::GrayType: {
      int i =
        get_current_palette()->findExactMatch(getRed(), getGreen(), getBlue(), getAlpha(), -1);
      if (i >= 0)
        return i;
      else
        return get_current_palette()->findBestfit(getRed(), getGreen(), getBlue(), getAlpha(), 0);
    }

    case Color::IndexType: return m_value.index;

    case Color::TileType:  return doc::tile_geti(m_value.tile);
  }

  ASSERT(false);
  return -1;
}

doc::tile_t Color::getTile() const
{
  switch (getType()) {
    case Color::IndexType: return m_value.index;

    case Color::TileType:  return m_value.tile;
  }

  ASSERT(false);
  return doc::notile;
}

int Color::getAlpha() const
{
  switch (getType()) {
    case Color::MaskType:  return 0;

    case Color::RgbType:   return m_value.rgb.a;

    case Color::HsvType:   return m_value.hsv.a;

    case Color::HslType:   return m_value.hsl.a;

    case Color::GrayType:  return m_value.gray.a;

    case Color::IndexType: {
      int i = m_value.index;
      if (i >= 0 && i < get_current_palette()->size())
        return rgba_geta(get_current_palette()->getEntry(i));
      else
        return 0;
    }

    case Color::OklabType: return m_value.oklab.alpha;

    case Color::TileType: return 255;
  }

  ASSERT(false);
  return -1;
}

void Color::setAlpha(int alpha)
{
  alpha = std::clamp(alpha, 0, 255);

  switch (getType()) {
    case Color::MaskType:  break;

    case Color::RgbType:   m_value.rgb.a = alpha; break;

    case Color::HsvType:   m_value.hsv.a = alpha; break;

    case Color::HslType:   m_value.hsl.a = alpha; break;

    case Color::GrayType:  m_value.gray.a = alpha; break;

    case Color::IndexType: *this = Color::fromRgb(getRed(), getGreen(), getBlue(), alpha); break;
  }
}

} // namespace app
