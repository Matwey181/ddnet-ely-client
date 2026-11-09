#ifndef IOS_VOICE_RING_H
#define IOS_VOICE_RING_H
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

// One producer and one consumer. No audio-device mutex, allocation or waits.
// Only the consumer may discard unread data.
template<size_t Capacity>
class CIosVoiceRing
{
 static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0);
 static_assert(std::atomic<uint64_t>::is_always_lock_free);
 std::array<uint8_t, Capacity> m_Data{};
 alignas(64) std::atomic<uint64_t> m_Read{0};
 alignas(64) std::atomic<uint64_t> m_Write{0};
public:
 size_t Size() const
 {
  const auto Read = m_Read.load(std::memory_order_acquire);
  const auto Write = m_Write.load(std::memory_order_acquire);
  // A concurrent consumer flush can advance Read beyond an older Write sample.
  return Write >= Read ? std::min<size_t>(Write - Read, Capacity) : 0;
 }
 size_t Push(const void *pBytes, size_t Length)
 {
  const auto Write = m_Write.load(std::memory_order_relaxed);
  const auto Read = m_Read.load(std::memory_order_acquire);
  const size_t Count = std::min(Length, Capacity - static_cast<size_t>(Write - Read));
  const size_t First = std::min(Count, Capacity - static_cast<size_t>(Write & (Capacity - 1)));
  if(Count) { std::memcpy(m_Data.data() + (Write & (Capacity - 1)), pBytes, First); std::memcpy(m_Data.data(), static_cast<const uint8_t *>(pBytes) + First, Count - First); }
  m_Write.store(Write + Count, std::memory_order_release);
  return Count;
 }
 size_t Pop(void *pBytes, size_t Length)
 {
  const auto Read = m_Read.load(std::memory_order_relaxed);
  const auto Write = m_Write.load(std::memory_order_acquire);
  const size_t Count = std::min<size_t>(Length, Write - Read);
  const size_t First = std::min(Count, Capacity - static_cast<size_t>(Read & (Capacity - 1)));
  if(Count) { std::memcpy(pBytes, m_Data.data() + (Read & (Capacity - 1)), First); std::memcpy(static_cast<uint8_t *>(pBytes) + First, m_Data.data(), Count - First); }
  m_Read.store(Read + Count, std::memory_order_release);
  return Count;
 }
 void Discard() { m_Read.store(m_Write.load(std::memory_order_acquire), std::memory_order_release); }
};
#endif
