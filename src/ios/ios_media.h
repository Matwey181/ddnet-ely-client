#ifndef IOS_IOS_MEDIA_H
#define IOS_IOS_MEDIA_H

#include <cstddef>
#include <cstdint>
#include <vector>

struct SMediaDecodedFrames;
struct SMediaDecodeLimits;

// Native ImageIO / AVFoundation decoding, with the same limits as BestClient.
bool IosDecodeMedia(const unsigned char *pData, size_t Size, SMediaDecodedFrames &Out, const SMediaDecodeLimits &Limits);
void *IosVideoOpen(const char *pPath, int &Width, int &Height, int MaxDimension = 2048);
bool IosVideoRead(void *pState, std::vector<uint8_t> &Pixels, int &DurationMs, bool Loop);
void IosVideoClose(void *pState);

#endif
