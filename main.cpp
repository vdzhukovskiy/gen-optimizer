#include "geometry-importer/importer.h"

#include <cstdio>
#include <exception>

int main(int argc, char **argv)
{
   if (argc < 2)
   {
      std::fprintf(stderr, "usage: %s <file.shp>\n", argv[0]);
      return 1;
   }
   try
   {
      gi::Importer imp(argv[1]);
      for (const auto &name : imp.layerNames())
      {
         auto feats = imp.readLayer(name);
         auto b = imp.layerBounds(name);
         std::printf("layer %s: %zu features, bounds [%.4f, %.4f]..[%.4f, %.4f]\n", name.c_str(),
                     feats.size(), b.minX, b.minY, b.maxX, b.maxY);
      }
   }
   catch (const std::exception &e)
   {
      std::fprintf(stderr, "error: %s\n", e.what());
      return 1;
   }
   return 0;
}