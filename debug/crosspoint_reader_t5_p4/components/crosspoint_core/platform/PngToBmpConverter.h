#pragma once

class HalFile;

class PngToBmpConverter {
public:
    static bool pngFileToBmpStream(HalFile &, HalFile &, bool = false) { return false; }
    static bool pngFileTo1BitBmpStreamWithSize(HalFile &, HalFile &, int, int) { return false; }
};
