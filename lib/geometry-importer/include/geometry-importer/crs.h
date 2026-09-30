#pragma once

#include <string>

namespace gi
{

struct Crs
{
   int epsg = 0;               // 0 = неизвестно
   bool is_geographic = false; // true → градусы, false → проекция (обычно метры)
   std::string units; // "degree", "metre", ...
   std::string name;  // человекочитаемое имя
   std::string wkt;   // полный WKT

   bool isValid() const noexcept
   {
      return epsg != 0 || !wkt.empty();
   }

   bool sameAs(const Crs &other) const noexcept
   {
      if (!wkt.empty() && !other.wkt.empty())
         return wkt == other.wkt;
      return epsg != 0 && epsg == other.epsg;
   }
};

} // namespace gi