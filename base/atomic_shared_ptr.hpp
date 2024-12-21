#pragma once

#include "base/macros.hpp"

#include <memory>

namespace base
{
// Template which provides methods for concurrently using shared pointers.
template <typename T>
class AtomicSharedPtr final
{
public:
  using ContentType = T const;
  using ValueType = std::shared_ptr<ContentType>;

  AtomicSharedPtr() = default;

// TODO drop this condition and the else branch when we finally have
// full C++20 standard compliancy across the board
#if  __cpp_lib_atomic_shared_ptr == 201711L
  void Set(ValueType value) noexcept { m_wrapped.store(value); }
  ValueType Get() const noexcept { return m_wrapped.load(); }

private:
  std::atomic<ValueType> m_wrapped = std::make_shared<ContentType>();
#else
  void Set(ValueType value) noexcept { m_wrapped.store(value); }                                                                                                                                        
  ValueType Get() const noexcept { return m_wrapped.load(); }                                                                                                                                           

 private:
  std::atomic<ValueType> m_wrapped = std::make_shared<ContentType>;
#endif

  DISALLOW_COPY_AND_MOVE(AtomicSharedPtr);
};
}  // namespace base
