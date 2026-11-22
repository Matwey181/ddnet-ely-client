#!/usr/bin/env python3
"""Make DDNet's iOS demo renderer output 2560x1440 MP4 and install a bundled demo."""
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
def replace(path, needle, replacement):
    p = root / path
    data = p.read_text(encoding="utf-8")
    if needle not in data:
        raise RuntimeError(f"Patch anchor missing in {path}: {needle[:100]!r}")
    p.write_text(data.replace(needle, replacement, 1), encoding="utf-8")

# The iPhone's drawable is not 2560x1440.  Explicitly resize captured
# frames before encoding.  This creates an actual 2560x1440 MP4 but
# does not manufacture detail beyond the phone's source framebuffer.
video = "src/ios/ios_video_recorder.mm"
replace(video, '#import <Foundation/Foundation.h>\n',
        '#import <Foundation/Foundation.h>\n#import <Accelerate/Accelerate.h>\n')
replace(video,
        'm_pStorage(pStorage), m_Width(Width & ~1), m_Height(Height & ~1),',
        'm_pStorage(pStorage), m_Width(2560), m_Height(1440),')
replace(video,
        'if(Format != CImageInfo::FORMAT_RGBA || Width < (uint32_t)m_Width ||\n'
        '           Height < (uint32_t)m_Height || Pixels.size() < (size_t)Width * Height * 4)',
        'if(Format != CImageInfo::FORMAT_RGBA || Width == 0 || Height == 0 ||\n'
        '           Pixels.size() < (size_t)Width * Height * 4)')
replace(video,
        '        CVPixelBufferRef Buffer = nullptr;\n',
        '''        // Crop symmetrically to 16:9: no stretching or black borders.
        const uint32_t CropWidth = (uint64_t)Width * 9 > (uint64_t)Height * 16
            ? (uint32_t)((uint64_t)Height * 16 / 9) : Width;
        const uint32_t CropHeight = (uint64_t)Width * 9 > (uint64_t)Height * 16
            ? Height : (uint32_t)((uint64_t)Width * 9 / 16);
        if(CropWidth == 0 || CropHeight == 0)
            return;
        const uint32_t StartX = (Width - CropWidth) / 2;
        const uint32_t StartY = (Height - CropHeight) / 2;
        std::vector<uint8_t> Upscaled((size_t)m_Width * m_Height * 4);
        vImage_Buffer Source = {
            Pixels.data() + ((size_t)StartY * Width + StartX) * 4,
            (vImagePixelCount)CropHeight, (vImagePixelCount)CropWidth, (size_t)Width * 4
        };
        vImage_Buffer Destination = {
            Upscaled.data(), (vImagePixelCount)m_Height,
            (vImagePixelCount)m_Width, (size_t)m_Width * 4
        };
        if(vImageScale_ARGB8888(&Source, &Destination, nullptr,
                               kvImageHighQualityResampling) != kvImageNoError)
        {
            log_error("ios-video", "Failed to scale demo to 2560x1440");
            return;
        }
        Pixels.swap(Upscaled);
        Width = (uint32_t)m_Width;

        CVPixelBufferRef Buffer = nullptr;
''')
replace("CMakeLists.txt",
        '  find_library(AUDIOTOOLBOX AudioToolbox)\n',
        '  find_library(ACCELERATE Accelerate)\n  find_library(AUDIOTOOLBOX AudioToolbox)\n')
replace("CMakeLists.txt",
        '  set(PLATFORM_CLIENT_LIBS\n    # tidy-alphabetical-start\n',
        '  set(PLATFORM_CLIENT_LIBS\n    # tidy-alphabetical-start\n    ' + chr(36) + '{ACCELERATE}\n')

# The .demo bytes are added to Payload/ElyClient.app/data/demos/ after the
# unsigned IPA has been compiled, to avoid storing the user's binary recording
# in the source repository.  This install-once hook makes the demo appear in
# the regular Demos browser, also on upgrades with existing settings.
storage = "src/engine/shared/storage.cpp"
replace(storage,
        '\t\tif(!Success)\n\t\t{\n\t\t\tlog_error("storage", "failed to create default folders in the user directory");\n\t\t}\n',
        '''#if defined(CONF_PLATFORM_IOS)
        if(InitializationType == EInitializationType::CLIENT && Success)
        {
            constexpr const char *pBundled = "data/demos/bundled_232.demo";
            constexpr const char *pDemoFileName = "блокаа232.demo";
            char aDestination[IO_MAX_PATH_LENGTH];
            str_format(aDestination, sizeof(aDestination), "%s/demos/%s",
                       m_aaStoragePaths[TYPE_SAVE], pDemoFileName);
            IOHANDLE Existing = io_open(aDestination, IOFLAG_READ);
            if(Existing)
                io_close(Existing);
            else if(IOHANDLE Source = io_open(pBundled, IOFLAG_READ))
            {
                IOHANDLE Destination = io_open(aDestination, IOFLAG_WRITE);
                if(Destination)
                {
                    bool CopySucceeded = true;
                    char aChunk[16384];
                    unsigned BytesRead;
                    while((BytesRead = io_read(Source, aChunk, sizeof(aChunk))) != 0)
                    {
                        if(io_write(Destination, aChunk, BytesRead) != BytesRead)
                        {
                            CopySucceeded = false;
                            break;
                        }
                    }
                    io_close(Destination);
                    if(CopySucceeded)
                        log_info("storage", "Installed bundled demo: %s", aDestination);
                    else
                        log_error("storage", "Could not finish copying bundled demo");
                }
                else
                    log_error("storage", "Could not create bundled demo");
                io_close(Source);
            }
        }
#endif

\t\tif(!Success)
\t\t{
\t\t\tlog_error("storage", "failed to create default folders in the user directory");
\t\t}
''')
# Hide on-screen mobile buttons only while exporting; restore controls afterward.
replace("src/engine/client/ios_video_recorder.h",
        "    int m_PreviousBackButton = 1;\n",
        "    int m_PreviousBackButton = 1;\n    int m_PreviousTouchControls = 1;\n")
replace(video,
        "m_PreviousBackButton = g_Config.m_ClBackButton;\n        g_Config.m_ClBackButton = 0;",
        "m_PreviousBackButton = g_Config.m_ClBackButton;\n        m_PreviousTouchControls = g_Config.m_ClTouchControls;\n        g_Config.m_ClBackButton = 0;\n        g_Config.m_ClTouchControls = 0;")
replace(video,
        "g_Config.m_ClBackButton = m_PreviousBackButton;\n",
        "g_Config.m_ClBackButton = m_PreviousBackButton;\n    g_Config.m_ClTouchControls = m_PreviousTouchControls;\n")

# Emotes (hearts etc.) also stay out of demo export.
replace("src/engine/client/client.cpp",
        "    g_Config.m_ClVideoShowImportantAlerts = 0;\n#endif\n",
        "    g_Config.m_ClVideoShowImportantAlerts = 0;\n    g_Config.m_ClShowEmotes = 0;\n#endif\n")

print("iOS 2560x1440 output and preinstalled demo hook configured")
