#pragma once

class HalFile;

class JpegToBmpConverter {
public:
    static bool jpegFileToBmpStream(HalFile &, HalFile &, bool = false) { return false; }
    static bool jpegFileTo1BitBmpStreamWithSize(HalFile &, HalFile &, int, int) { return false; }
};
