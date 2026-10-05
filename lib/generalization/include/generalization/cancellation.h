#pragma once

#include <exception>
#include <functional>

namespace gen
{

using CancellationCheck = std::function<bool()>;

class Cancelled final : public std::exception
{
 public:
   const char *what() const noexcept override
   {
      return "Generalization cancelled";
   }
};

// Не зависит от Qt. Исключение отмены нельзя превращать в результат-фолбэк.
inline void checkCancelled(const CancellationCheck &cancelled)
{
   if (cancelled && cancelled())
      throw Cancelled{};
}

} // namespace gen
